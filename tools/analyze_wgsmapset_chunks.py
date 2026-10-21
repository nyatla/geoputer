"""Report GI01 stored sizes using the local Galuchat Python SDK (read-only)."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import sys
import tempfile


BINS = [0, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576, math.inf]
LABELS = ['4KiB未満', '4〜8KiB未満', '8〜16KiB未満', '16〜32KiB未満',
          '32〜64KiB未満', '64〜128KiB未満', '128〜256KiB未満',
          '256〜512KiB未満', '512KiB〜1MiB未満', '1MiB以上']
PLOT_LABELS = ['<4', '4–8', '8–16', '16–32', '32–64', '64–128',
               '128–256', '256–512', '512–1024', '≥1024']


def read_chunks(source: Path, sdk_source: Path):
    sys.path.insert(0, str(sdk_source.resolve()))
    from galuchat.io import FileReaderFactory
    from galuchat.format.wgsmapset3.WGSMapSetHeader3 import WGSMapSetHeader3
    from galuchat.chunk.gi01 import GaluchatImageDataChunk01Reader

    factory = FileReaderFactory(source)
    with factory.create() as reader:
        header = WGSMapSetHeader3.unpack(reader)
        offset = reader.pos
        name = reader.readAsBytes(4)
        if name == b'LAYO':
            reader.skipInByte(reader.readMbUInt())
            offset = reader.pos
            name = reader.readAsBytes(4)
        if name != b'GI01':
            raise ValueError('WGSMapSet/3 does not start its images with GI01')
    rows = []
    with factory.create(offset) as reader:
        for index, (west, south) in enumerate(header.iterMapset()):
            begin = reader.pos
            chunk = GaluchatImageDataChunk01Reader(reader)
            block_start = reader.pos
            chunk.skipToEnd()
            end = reader.pos
            rows.append(dict(
                chunk_index=index, file_offset=offset + begin,
                end_offset=offset + end, stored_bytes=end - begin,
                stored_kib=(end - begin) / 1024,
                block_data_bytes=end - block_start,
                prefix_bytes=block_start - begin,
                width_cells=chunk.width, height_cells=chunk.height,
                block_side_cells=chunk.square_unit,
                block_count=chunk.hus * chunk.vus,
                west_cell=west, south_cell=south,
                west_deg=west / header.unit_inv_x,
                south_deg=south / header.unit_inv_y,
            ))
        if offset + reader.pos != source.stat().st_size:
            raise ValueError('GI01 sequence does not end at file end')
    if len(rows) != header.numofmap or not rows:
        raise ValueError('GI01 count does not match GLCH')
    if sum(r['stored_bytes'] for r in rows) + offset != source.stat().st_size:
        raise ValueError('GI01 sizes do not account for the complete file')
    return rows, offset, header.unit_inv_x, header.unit_inv_y


def plot_histogram(bins, target: Path):
    os.environ.setdefault('MPLCONFIGDIR', str(Path(tempfile.gettempdir()) / 'reversegeoputer-mpl'))
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(2, 1, figsize=(10, 7), sharex=True, layout='constrained')
    positions = range(len(bins))
    for ax, values, ylabel in [
        (axes[0], [b['count'] for b in bins], 'Number of chunks'),
        (axes[1], [b['stored_bytes'] / 1048576 for b in bins], 'Stored size (MiB)'),
    ]:
        bars = ax.bar(positions, values, color='#187b83', width=0.72)
        ax.bar_label(bars, fmt='%g' if ax is axes[0] else '%.2f', padding=3, fontsize=9)
        ax.set_ylabel(ylabel)
        ax.set_ylim(0, max(values) * 1.18)
        ax.grid(axis='y', alpha=0.2)
        ax.set_axisbelow(True)
        ax.spines[['top', 'right']].set_visible(False)
    axes[0].set_title(f'GI01 chunk sizes ({sum(b["count"] for b in bins)} chunks)')
    axes[1].set_xticks(list(positions), PLOT_LABELS, rotation=25, ha='right')
    axes[1].set_xlabel('Stored GI01 size (KiB; lower bound inclusive, upper bound exclusive)')
    fig.savefig(target)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mapset', type=Path)
    parser.add_argument('--sdk-source', type=Path,
                        default=Path(__file__).resolve().parents[2] / 'galuchat-gis-sdk/python/src')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--name', default='WGSMapSetチャンクサイズ分布')
    parser.add_argument('--plot', action='store_true')
    args = parser.parse_args()
    source = args.mapset.resolve()
    rows, prefix_bytes, unit_x, unit_y = read_chunks(source, args.sdk_source)
    sizes = [r['stored_bytes'] for r in rows]
    total = sum(sizes)
    bins = []
    for lower, upper, label in zip(BINS, BINS[1:], LABELS):
        selected = [r for r in rows if lower <= r['stored_bytes'] < upper]
        size = sum(r['stored_bytes'] for r in selected)
        bins.append(dict(label=label, count=len(selected),
                         count_percent=100 * len(selected) / len(rows),
                         stored_bytes=size, stored_percent=100 * size / total))
    thresholds = []
    for size in (4096, 8192, 16384):
        selected = [r for r in rows if r['stored_bytes'] < size]
        thresholds.append(dict(threshold=size, count=len(selected),
                               count_percent=100 * len(selected) / len(rows),
                               stored_bytes=sum(r['stored_bytes'] for r in selected)))
    result = dict(source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  file_bytes=source.stat().st_size, prefix_bytes=prefix_bytes,
                  gi01_stored_bytes=total, chunks=len(rows),
                  blocks=sum(r['block_count'] for r in rows), unit_inv_x=unit_x, unit_inv_y=unit_y,
                  size_definition='GI01 tag + length field + complete payload; bytes as stored in GLC',
                  min_bytes=min(sizes), median_bytes=statistics.median(sizes),
                  mean_bytes=statistics.mean(sizes), max_bytes=max(sizes),
                  full_8192_square_chunks=sum((r['width_cells'], r['height_cells']) == (8192, 8192) for r in rows),
                  bins=bins, below_thresholds=thresholds)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stem = args.output_dir / args.name
    with stem.with_suffix('.csv').open('w', encoding='utf-8', newline='') as out:
        writer = csv.DictWriter(out, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    stem.with_suffix('.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    if args.plot:
        plot_histogram(bins, stem.with_suffix('.svg'))
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
