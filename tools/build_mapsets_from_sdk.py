#!/usr/bin/env python3
"""Build copy-ready Geoputer mapsets from Galuchat GIS SDK releases.

Each output directory contains one selected map, the supported Wordbooks, and
one dataset.ini. The directory can later be copied to /sd/dataset/NN/.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
import sys
import zipfile

SOURCE_LIST = Path(__file__).with_name('mapsets_sources.tsv')
SDK_ROOT = Path(__file__).resolve().parents[2] / 'galuchat-gis-sdk'
SDK_RELEASE = SDK_ROOT / 'release'
sys.path.insert(0, str(SDK_ROOT / 'python' / 'src'))

from galuchat.format.wgsmapset3.WGSMapSetHeader3 import WGSMapSetHeader3
from galuchat.io import BytesBufferReader
from galuchat.wordbook.core.GaluchatGisWordBookReader import GaluchatGisWordBookReader

EN_PACK = 'galuchat-languagepack-en-rev1.zip'
ESTAT_ID = 'jp-est-y2020-2'
ESTAT_SUBDIV = 'jp-2020-e-stat-go-jp-a002005212020-stat-small-area-subdiv-rev2.zip'
MAX_CHUNKS = 1500  # application/Config.h: MaxDatasetChunks
SOURCE_FIELDS = ('id', 'release_zip', 'language', 'title', 'description', 'display_line1', 'display_line2')
ENGLISH_JAPAN_TITLES = {
    'jp-n03-y2026-1': ('Japan Administrative Areas 2026', 'Prefecture and municipality boundaries.'),
    'jp-est-y2020-2': ('Japan Small Areas 2020', 'Census small-area boundaries with subdivisions.'),
    'jp-esc-y2026-2': ('Japan Coastal Small Areas', 'Coastline-adjusted small-area and administrative boundaries.'),
}


def source_rows() -> list[dict[str, str]]:
    with SOURCE_LIST.open(encoding='utf-8', newline='') as source:
        reader = csv.DictReader(source, delimiter='\t')
        if tuple(reader.fieldnames or ()) != SOURCE_FIELDS:
            raise ValueError('unexpected source catalog columns')
        rows = list(reader)
    ids = [row['id'] for row in rows]
    if not ids or len(ids) != len(set(ids)):
        raise ValueError('mapset IDs must be unique and nonempty')
    if any(not name.isascii() or len(name) > 15 or '/' in name for name in ids):
        raise ValueError('mapset IDs must use at most 15 ASCII characters')
    if {row['id'] for row in rows if row['language'] == 'ja'} != ENGLISH_JAPAN_TITLES.keys():
        raise ValueError('English titles are missing for a Japanese mapset')
    return rows


def original_wordbook(archive: zipfile.ZipFile) -> str:
    names = [entry.filename for entry in archive.infolist() if not entry.is_dir()]
    books = [name for name in names if name.endswith('.giswordbook')
             and not name.endswith(('.sjis.giswordbook', '.utf16.giswordbook'))]
    if len(books) != 1:
        raise ValueError(f'expected one UTF-8 original Wordbook in {archive.filename}: {books}')
    return books[0]


def selected_glc(archive: zipfile.ZipFile) -> tuple[str, int, int]:
    candidates = []
    for entry in archive.infolist():
        if not entry.is_dir() and entry.filename.endswith('.glc'):
            with archive.open(entry) as source:
                header = WGSMapSetHeader3.unpack(BytesBufferReader(source.read(256 * 1024)))
            if header.unit_inv_x != header.unit_inv_y:
                raise ValueError(f'unsupported unequal axis resolution: {entry.filename}')
            if 0 < header.numofmap <= MAX_CHUNKS:
                candidates.append((header.unit_inv_x, entry.filename, header.numofmap))
    if not candidates:
        raise ValueError(f'no usable GLC in {archive.filename}')
    highest = max(unit for unit, _, _ in candidates)
    choices = [item for item in candidates if item[0] == highest]
    if len(choices) != 1:
        raise ValueError(f'ambiguous highest resolution in {archive.filename}: {choices}')
    return choices[0][1], choices[0][0], choices[0][2]


def english_wordbook(pack: zipfile.ZipFile, release_stem: str) -> str:
    prefix = f'galuchat-languagepack-en/datasets/{release_stem}/'
    names = [entry.filename for entry in pack.infolist()
             if not entry.is_dir() and entry.filename.startswith(prefix)
             and entry.filename.endswith('.en.giswordbook')]
    if len(names) != 1:
        raise ValueError(f'expected one English Wordbook for {release_stem}: {names}')
    return names[0]


def copy_member(archive: zipfile.ZipFile, member: str, target: Path) -> None:
    if target.exists():
        raise FileExistsError(target)
    with archive.open(member) as source, target.open('wb') as output:
        while block := source.read(1024 * 1024):
            output.write(block)


def valid_filename(name: str) -> None:
    if not name or '/' in name or '\\' in name or ':' in name or name in ('.', '..'):
        raise ValueError(f'invalid dataset filename: {name}')
    if len(name.encode('utf-8')) > 127:
        raise ValueError(f'dataset filename exceeds 127 bytes: {name}')


def ini_text(row: dict[str, str], native: str | None, english: str) -> str:
    sections = ['[dataset]', 'version=1', 'map=map.glc',
                f"default_language={'ja' if native else 'en'}", '']
    languages = [('ja', row['title'], row['description'], native)] if native else []
    en_title, en_description = ENGLISH_JAPAN_TITLES[row['id']] if native else (row['title'], row['description'])
    languages.append(('en', en_title, en_description, english))
    for language, title, description, book in languages:
        assert book is not None
        valid_filename(book)
        sections.extend([f'[{language}]', f'title={title}', f'description={description}',
                         f'wordbook={book}', f"display_line1={row['display_line1']}",
                         f"display_line2={row['display_line2']}", ''])
    for line in sections:
        if '\n' in line or '\r' in line or len(line.encode('utf-8')) >= 256:
            raise ValueError(f'dataset.ini line exceeds parser capacity: {row["id"]}')
    return '\n'.join(sections)


def verify_wordbooks(folder: Path, native: str | None, english: str,
                     original_archive: zipfile.ZipFile) -> None:
    source_name = original_wordbook(original_archive)
    source = GaluchatGisWordBookReader.fromBytes(original_archive.read(source_name))
    en = GaluchatGisWordBookReader.fromFile(folder / english)
    if (source.record_count, source.depth) != (en.record_count, en.depth):
        raise ValueError(f'English Wordbook code layout differs: {folder.name}')
    if native:
        ja = GaluchatGisWordBookReader.fromFile(folder / native)
        if (ja.record_count, ja.depth) != (en.record_count, en.depth):
            raise ValueError(f'Japanese Wordbook code layout differs: {folder.name}')
    if folder.name == ESTAT_ID and (en.record_count, en.depth) != (207740, 4):
        raise ValueError('e-Stat must use the rev2 four-layer Wordbook')


def build(release_root: Path, output_root: Path, manifest: Path | None) -> None:
    rows = source_rows()
    if output_root.exists():
        raise FileExistsError(f'output already exists: {output_root}')
    for row in rows:
        if not (release_root / row['release_zip']).is_file():
            raise FileNotFoundError(release_root / row['release_zip'])
    if not (release_root / EN_PACK).is_file():
        raise FileNotFoundError(release_root / EN_PACK)
    output_root.mkdir(parents=True)
    selection = []
    with zipfile.ZipFile(release_root / EN_PACK) as pack:
        for row in rows:
            folder = output_root / row['id']
            folder.mkdir()
            with zipfile.ZipFile(release_root / row['release_zip']) as maps:
                map_name, resolution, chunks = selected_glc(maps)
                copy_member(maps, map_name, folder / 'map.glc')
                if row['language'] == 'ja':
                    if row['id'] == ESTAT_ID:
                        with zipfile.ZipFile(release_root / ESTAT_SUBDIV) as subdiv:
                            native = original_wordbook(subdiv)
                            valid_filename(native)
                            copy_member(subdiv, native, folder / native)
                    else:
                        native = original_wordbook(maps)
                        valid_filename(native)
                        copy_member(maps, native, folder / native)
                else:
                    native = None
                en_stem = Path(ESTAT_SUBDIV if row['id'] == ESTAT_ID else row['release_zip']).stem
                en_member = english_wordbook(pack, en_stem)
                english = en_member.rsplit('/', 1)[-1]
                valid_filename(english)
                copy_member(pack, en_member, folder / english)
                original_archive_path = release_root / (ESTAT_SUBDIV if row['id'] == ESTAT_ID else row['release_zip'])
                with zipfile.ZipFile(original_archive_path) as original:
                    verify_wordbooks(folder, native, english, original)
            (folder / 'dataset.ini').write_text(ini_text(row, native, english), encoding='utf-8')
            selection.append((row['id'], row['release_zip'], map_name, str(resolution), str(chunks), str((folder / 'map.glc').stat().st_size)))
            print(f"built {row['id']}: 1/{resolution}, {chunks} chunks", flush=True)
    if manifest:
        manifest.parent.mkdir(parents=True, exist_ok=True)
        with manifest.open('w', encoding='utf-8', newline='') as output:
            writer = csv.writer(output, delimiter='\t', lineterminator='\n')
            writer.writerow(('id', 'release_zip', 'selected_glc', 'unit_inv', 'chunks', 'glc_bytes'))
            writer.writerows(selection)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-release', type=Path, default=SDK_RELEASE)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--selection-manifest', type=Path)
    args = parser.parse_args()
    build(args.sdk_release, args.output, args.selection_manifest)
