#!/usr/bin/env python3
"""N03: expand native TI records, resolve their hierarchy, apply record decisions, rebuild."""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import configparser
import csv
from dataclasses import dataclass
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import sys
import unicodedata
import zipfile

SDK = Path(os.environ.get('GALUCHAT_SDK', Path(__file__).resolve().parents[2] / 'galuchat-gis-sdk'))
sys.path.insert(0, str(SDK / 'python/src'))
from galuchat.io import BytesBufferReader
from galuchat.wordbook.core.GaluchatGisWordBookDom import GaluchatGisWordBookDom
from galuchat.wordbook.chunk.HierarchicalIndexChunkReader import HierarchicalIndexChunkReader
from galuchat.wordbook.chunk.TextTableChunk import decode_page_records_as_token_ids

KINDS = ('都道府県', '振興局', '郡', '市区町村', '行政区')
RECORD_FIELDS = ['id'] + [v for n in range(1, 6) for v in (f'階層{n}', f'変換後階層{n}')] + ['備考']
EVIDENCE_FIELDS = ['id', '階層', '種別', '原文', '変換後', '状態', 'is_inferred', '出典', '資料内位置', '判断']
DECISION_FIELDS = ['id'] + [f'階層{n}' for n in range(1, 6)] + ['対象階層', '変換後', '状態', 'is_inferred', '出典', '資料内位置', '判断']
NULL = 'NULL'
N03_DATASETS = ('jp-n03-y2024-1', 'jp-n03-y2025-1', 'jp-n03-y2026-1')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_book(data):
    dom = GaluchatGisWordBookDom.fromBytes(data)
    texts = [b''.join(dom.token_map.tokens[t] for t in ids).decode('utf-8')
             for page in dom.text_table.pages
             for ids in decode_page_records_as_token_ids(page, dom.text_table.token_bits)]
    index = dom.index
    reader = HierarchicalIndexChunkReader(BytesBufferReader(index.index_stream), index.max_depth,
        index.record_count, index.code_bits, index.root_count, len(index.index_stream))
    refs = [tuple(row) for row in reader.iterCodeSetsFor(range(index.record_count))]
    assert len(refs) == index.record_count and len(texts) == dom.text_table.record_count
    return dom, texts, refs, [tuple(texts[i] for i in row) for row in refs]


def clean(value):
    return ''.join(value.split())


def latin(value):
    return bool(value) and all(not unicodedata.category(c).startswith('L') or
        'LATIN' in unicodedata.name(c, '') for c in value)


def write_tsv(path, fields, rows):
    with path.open('w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fields, delimiter='\t', lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


@dataclass(frozen=True)
class Cell:
    value: str | None
    state: str = 'unresolved'
    inferred: bool | None = None
    source: str = ''
    locator: str = ''
    note: str = ''
    region_code: str = ''


class RegionalSources:
    """Official JP/EN pairs by regional identity; no global place-name replacement table."""
    def __init__(self, inputs, month):
        self.month = month
        self.entries = []
        paired = []
        for language in ('jp', 'en'):
            body = json.loads((inputs / f'estat-regions-{language}.json').read_text(encoding='utf-8'))
            groups = body['GET_META_REGION_INF']['METADATA_INF']['CLASS_INF']['CLASS_OBJ']
            entries = {}
            for group in groups:
                rows = group.get('CLASS', [])
                if isinstance(rows, dict): rows = [rows]
                for row in rows:
                    if row['@fromDate'] <= month < row['@toDate']:
                        key = (group['@parentRegionCode'], row['@regionCode'], int(row['@level']))
                        if key in entries: raise ValueError(f'Duplicate active region identity: {key}')
                        entries[key] = (group, row)
            paired.append(entries)
        jp, en = paired
        prefectures = {key[1][:2]: value[1]['@name'] for key, value in jp.items() if key[2] == 3}
        self.index = defaultdict(list)
        for key, (group, row) in jp.items():
            if key in en and latin(en[key][1]['@name']):
                entry = dict(pref=prefectures.get(key[1][:2]), parent=key[0], code=key[1], level=key[2],
                    name=row['@name'], english=en[key][1]['@name'], group=group['@name'],
                    locator=f'parentRegionCode={key[0]}; regionCode={key[1]}; level={key[2]}; at={month}')
                self.index[(entry['pref'], entry['name'])].append(entry)
        self.postal_counties = defaultdict(dict)
        self.postal_members = defaultdict(dict)
        with zipfile.ZipFile(inputs / 'KEN_ALL_ROME.zip') as z:
            for ordinal, row in enumerate(csv.reader(io.StringIO(z.read('KEN_ALL_ROME.CSV').decode('cp932'))), 1):
                county = re.fullmatch(r'(.+郡)(.+)', clean(row[2]))
                english = re.fullmatch(r'(.+?) GUN (.+)', row[5])
                if county and english:
                    key = (clean(row[1]), county[1], county[2])
                    self.postal_counties[key].setdefault(english[1].title() + '-gun',
                        f'KEN_ALL_ROME.CSV row={ordinal}; postcode={row[0]}; native={row[2]}; english={row[5]}')
                    self.postal_members[key].setdefault(english[2], ordinal)

    def resolve_record(self, path):
        cells = []
        municipality = None
        subprefecture = None
        for slot, name in enumerate(path):
            if not name:
                cells.append(Cell('', 'empty', False, 'original Wordbook/TI00', note='元の空スロットを保持'))
                continue
            matches = []
            for entry in self.index[(path[0], name)]:
                level = entry['level']
                valid = ((slot == 0 and level == 3) or (slot == 1 and level == 5) or
                    (slot == 2 and level == 6) or
                    (slot == 3 and level in (8, 9, 10, 12, 13) and
                        (not path[2] or entry['group'] == path[2] or path[0] == '北海道')) or
                    (slot == 4 and level == 11 and municipality is not None and entry['parent'] == municipality))
                if valid: matches.append(entry)
            if slot == 3 and subprefecture:
                local = [e for e in matches if e['parent'] == subprefecture]
                if local: matches = local
            values = {e['english'] for e in matches}
            codes = {e['code'] for e in matches}
            if len(values) == 1 and len(codes) == 1:
                entry = matches[0]
                cells.append(Cell(entry['english'], 'verified', False,
                    'inputs/estat-regions-jp.json + inputs/estat-regions-en.json', entry['locator'],
                    '県・階層種別を照合。行政区は同レコードで確定した市の地域コードに所属するものを使用。', entry['code']))
                if slot == 1: subprefecture = entry['code']
                if slot == 3: municipality = entry['code']
            elif slot == 2 and len(self.postal_counties.get((path[0], name, path[3]), {})) == 1:
                value, locator = next(iter(self.postal_counties[(path[0], name, path[3])].items()))
                cells.append(Cell(value, 'verified', False, 'inputs/KEN_ALL_ROME.zip', locator,
                    '同一レコードの県・郡・所属町村を和文住所で照合し、英文の明示的なGUN欄から郡名を確認。全角空白は照合時だけ除去。'))
            else:
                cells.append(Cell(None, note='同じレコードの所属・階層種別から直接確定できない。個別調査対象。'))
        return cells


def record_rows(paths, cells):
    result = []
    for code, (path, translated) in enumerate(zip(paths, cells), 1):
        row = {'id': code}
        for n, (native, cell) in enumerate(zip(path, translated), 1):
            row[f'階層{n}'] = native
            row[f'変換後階層{n}'] = NULL if cell.value is None else cell.value
        unresolved = [str(n) for n, cell in enumerate(translated, 1) if cell.value is None]
        reviewed = [f'階層{n}: {cell.note}' for n, cell in enumerate(translated, 1) if cell.state in ('context_verified', 'descriptive_translation')]
        inferred = [str(n) for n, cell in enumerate(translated, 1) if cell.inferred]
        notes = (['未解決階層=' + ','.join(unresolved)] if unresolved else []) + reviewed
        if inferred: notes.append('推定フラグ=trueの階層=' + ','.join(inferred))
        row['備考'] = ' / '.join(notes) if notes else '資料照合済み（詳細はevidence.tsv）'
        result.append(row)
    return result


def apply_decisions(paths, cells, decision_path):
    if decision_path is None: return 0
    seen = set()
    with decision_path.open(encoding='utf-8', newline='') as f:
        reader = csv.DictReader(f, delimiter='\t')
        if reader.fieldnames != DECISION_FIELDS: raise ValueError('Unexpected record decision schema')
        for row in reader:
            code, slot = int(row['id']), int(row['対象階層']) - 1
            if not 1 <= code <= len(paths) or not 0 <= slot < 5: raise ValueError('Invalid record decision address')
            if (code, slot) in seen: raise ValueError('Duplicate record decision')
            seen.add((code, slot))
            if tuple(row[f'階層{n}'] for n in range(1, 6)) != paths[code-1]:
                raise ValueError(f'Decision original hierarchy differs at code {code}')
            if cells[code-1][slot].value is not None: raise ValueError(f'Decision overwrites already resolved cell at {code}:{slot+1}')
            if not latin(row['変換後']) or row['変換後'] == NULL: raise ValueError('Invalid decision English value')
            if row['is_inferred'] not in ('true', 'false') or not all(row[k] for k in ('出典','資料内位置','判断')):
                raise ValueError('Missing decision provenance/flag')
            if row['状態'] not in ('context_verified','descriptive_translation','inferred'): raise ValueError('Invalid decision state')
            cells[code-1][slot] = Cell(row['変換後'],row['状態'],row['is_inferred']=='true',row['出典'],row['資料内位置'],row['判断'])
    return len(seen)


def english_paths_from_records(path, originals):
    with path.open(encoding='utf-8', newline='') as f:
        reader = csv.DictReader(f, delimiter='\t')
        if reader.fieldnames != RECORD_FIELDS: raise ValueError('Unexpected records.tsv schema')
        rows = list(reader)
    if len(rows) != len(originals): raise ValueError('Record count changed')
    result = []
    for code, (row, original) in enumerate(zip(rows, originals), 1):
        if row['id'] != str(code): raise ValueError('Record order/id changed')
        if tuple(row[f'階層{n}'] for n in range(1,6)) != original: raise ValueError('Original hierarchy changed')
        values = tuple(row[f'変換後階層{n}'] for n in range(1,6))
        if NULL in values: raise ValueError('Unresolved record cannot be packed')
        if any((v=='') != (s=='') or v and not latin(v) for s,v in zip(original,values)):
            raise ValueError('Empty slot or English value changed incorrectly')
        result.append(values)
    return result


def checksums(folder, recursive=True):
    paths = folder.rglob('*') if recursive else folder.iterdir()
    lines = [f'{digest(p)}  {p.relative_to(folder).as_posix()}\n' for p in sorted(paths)
             if p.is_file() and p.name != 'SHA256SUMS']
    (folder/'SHA256SUMS').write_text(''.join(lines),encoding='utf-8')


def build_release(root, out, entry, decisions=None, initial_only=False):
    name = entry['directory']; src = root/name; dest = out/name; source = dest/'source'
    source.mkdir(parents=True,exist_ok=False)
    native = src/entry['wordbook']
    if digest(native) != entry['wordbook_sha256']: raise ValueError('Original Wordbook checksum changed')
    dom, texts, refs, paths = read_book(native.read_bytes())
    if dom.index.max_depth != 5: raise ValueError('N03 must have five levels')
    month = re.search(r'-y(\d{4})-',name)[1]+'01'
    sources = RegionalSources(src/'source/inputs',month)
    blank = [[Cell('','empty',False) if not text else Cell(None) for text in path] for path in paths]
    write_tsv(source/'records-expanded.tsv',RECORD_FIELDS,record_rows(paths,blank))
    cells = [sources.resolve_record(path) for path in paths]
    initial = record_rows(paths,cells)
    write_tsv(source/'records-initial.tsv',RECORD_FIELDS,initial)
    initial_unresolved = [row for row,cs in zip(initial,cells) if any(c.value is None for c in cs)]
    write_tsv(source/'unresolved-initial.tsv',RECORD_FIELDS,initial_unresolved)
    if decisions is None and (src/'source/record-decisions.tsv').is_file():decisions=src/'source/record-decisions.tsv'
    reviewed = apply_decisions(paths,cells,decisions) if not initial_only else 0
    if decisions and not initial_only:shutil.copyfile(decisions,source/'record-decisions.tsv')
    final = record_rows(paths,cells)
    write_tsv(source/'records.tsv',RECORD_FIELDS,final)
    unresolved = [row for row,cs in zip(final,cells) if any(c.value is None for c in cs)]
    write_tsv(source/'unresolved.tsv',RECORD_FIELDS,unresolved)
    evidence = [dict(zip(EVIDENCE_FIELDS,[code,n,KINDS[n-1],path[n-1],NULL if cell.value is None else cell.value,
        cell.state,'' if cell.inferred is None else str(cell.inferred).lower(),cell.source,cell.locator,cell.note]))
        for code,(path,cs) in enumerate(zip(paths,cells),1) for n,cell in enumerate(cs,1)]
    write_tsv(source/'evidence.tsv',EVIDENCE_FIELDS,evidence)
    for p in src.iterdir():
        if p.is_file() and p.name not in ('README.md','dataset.ini','SHA256SUMS') and not p.name.endswith(('.en.giswordbook','.glc','.dat')):shutil.copyfile(p,dest/p.name)
    shutil.copytree(src/'source/inputs',source/'inputs')
    output = None; verified = 0
    if not unresolved:
        expected = english_paths_from_records(source/'records.tsv',paths)
        packed = GaluchatGisWordBookDom.createFromPaths(expected,page_size=dom.text_table.page_size,metadata=dom.header.metadata,text_encoding='utf-8').toBytes()
        _,_,_,actual = read_book(packed)
        if actual != expected:raise ValueError('Serialized TI records differ')
        output = native.stem+'.en.giswordbook';(dest/output).write_bytes(packed);verified=len(paths)
    ini=configparser.ConfigParser(interpolation=None);ini.read(src/'dataset.ini',encoding='utf-8')
    if output:
        en=dict(ini['ja']);en.update(title='Japan '+name[3:],description='Administrative areas with Latin-script place names.',wordbook=output)
        for key in ('display_line1','display_line2'):en[key]=' '.join('%'+v for v in re.findall(r'%(\d+)',en[key]))
        ini['en']=en
    elif ini.has_section('en'):ini.remove_section('en')
    b=io.StringIO();ini.write(b,space_around_delimiters=False);(dest/'dataset.ini').write_text('# UTF-8. map.glc will be provided after map resolution selection.\n'+b.getvalue(),encoding='utf-8')
    # TT counts remain summary compatibility only; decisions/packing are entirely record-based.
    uses=defaultdict(list)
    for rr,cc in zip(refs,cells):
        for tt,cell in zip(rr,cc):uses[tt].append(cell.value)
    unresolved_tt=sum(any(value is None for value in uses.get(tt,())) for tt in range(len(texts)))
    used_names={'estat-regions-jp.json','estat-regions-en.json','KEN_ALL_ROME.zip','estat-1950-census-northern-names.pdf'}
    inputs=[dict(file='inputs/'+p.name,sha256=digest(p)) for p in sorted((source/'inputs').iterdir()) if p.is_file() and p.name in used_names]
    if (source/'record-decisions.tsv').exists():inputs.append(dict(file='record-decisions.tsv',sha256=digest(source/'record-decisions.tsv')))
    report=dict(format_version='ti-record-research/1',created='2026-11-01',directory=name,language='ja',source_wordbook=native.name,
        source_sha256=digest(native),source_zip=entry['source_zip'],source_zip_sha256=entry['source_sha256'],reference_month=month,
        original_records=len(paths),depth=5,id_origin=1,initial_unresolved_records=len(initial_unresolved),
        initial_unresolved_cells=sum(row[f'変換後階層{n}']==NULL for row in initial for n in range(1,6)),reviewed_cells=reviewed,
        unresolved_records=len(unresolved),unresolved_cells=sum(c.value is None for row in cells for c in row),
        inferred_cells=sum(c.inferred is True for row in cells for c in row),output_wordbook=output,
        output_sha256=digest(dest/output) if output else None,validated_records=verified,inputs=inputs,
        source_tt=len(texts),ready_tt=len(texts)-unresolved_tt,unresolved_tt=unresolved_tt)
    (source/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    readme=f'''# TIレコード単位の地名調査: {name}

元TI00を全{len(paths):,}レコード・固定5階層で展開した。idはマップが参照する1始まりの区域コード。原文・コード順・空欄を維持し、英名は各レコードの所属と階層種別から判断した。旧TT対応表と旧英字Wordbookは判定に使っていない。

調査表は `id, 階層1, 変換後階層1, …, 階層5, 変換後階層5, 備考`。階層は都道府県／振興局／郡／市区町村／行政区。未確定はNULL、元から存在しない階層は空欄。

| ファイル | 内容 |
| --- | --- |
| records-expanded.tsv | 原TI展開直後。存在する名称の英名はすべてNULL。 |
| records-initial.tsv | 所属付きの公的対訳照合後。個別判断の適用前。 |
| unresolved-initial.tsv | 最初の照合で未解決階層が残ったレコード。 |
| record-decisions.tsv | 未解決のid・階層ごとの調査判断。全原文階層、英名、根拠、推定フラグを持つ。 |
| records.tsv | 調査判断を反映した最終表。この表から英字Wordbookを構築する。 |
| unresolved.tsv | 最終的に未解決階層を持つレコード。 |
| evidence.tsv | id・階層番号ごとの出典、資料内位置、判断、is_inferred。 |
| report.json | 元資料のハッシュ、段階別件数、構築・再読込み検証結果。 |

初回未解決: {len(initial_unresolved)}レコード。個別判断: {reviewed}セル。最終未解決: {len(unresolved)}レコード。再読込み検証: {verified:,}レコード。英字版: `{output or '未生成'}`。

`is_inferred=false`は公的対訳または資料間の所属関係で確認した名称、trueは説明訳・推定、空欄は未確定。「所属未定地」は地名の読みではなく状態表示として説明訳する。空スロットはfalseで保持する。

日本郵便資料は県・郡・所属町村を組にして照合する。北方の郡名は統計局1950年国勢調査英文解説のPDF2ページ（印刷14–15ページ）の所属村と、同レコードで確認した村の英名を照合して個別判断する。古い資料は名称の根拠にのみ使用し、現行の所属や区域を更新しない。

再現方法と調査表の規則は[RECORD_RESEARCH.md](../../RECORD_RESEARCH.md)を参照。原典の利用条件は[NOTICE.md](../NOTICE.md)、追加資料の出典と利用条件は共通仕様を参照。元のWordbookと入力資料は改変しない。
'''
    (source/'README.md').write_text(readme,encoding='utf-8')
    original_readme=(src/'README.md').read_text().split('\n## 英字Wordbook',1)[0]
    original_readme+='\n## 英字Wordbook\n\nTIの全レコードを階層ごとに調査し、[records.tsv](source/records.tsv)から再構築した。詳細は[調査手順と検証結果](source/README.md)を参照。\n'
    (dest/'README.md').write_text(original_readme,encoding='utf-8')
    checksums(source);checksums(dest,recursive=False)
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mapsets',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--only',nargs='+',default=list(N03_DATASETS))
    parser.add_argument('--decisions',type=Path);parser.add_argument('--initial-only',action='store_true')
    args=parser.parse_args();root=args.mapsets.resolve();out=args.output.resolve()
    if out==root or root in out.parents:raise ValueError('Use a separate staging directory')
    with (root/'catalog.tsv').open() as f:entries={r['directory']:r for r in csv.DictReader(f,delimiter='\t')}
    for name in args.only:
        if name not in N03_DATASETS:raise ValueError('This record-research pipeline applies to N03 2024, 2025 and 2026')
        r=build_release(root,out,entries[name],args.decisions,args.initial_only)
        print(name,'initial unresolved',r['initial_unresolved_records'],'reviewed',r['reviewed_cells'],'unresolved',r['unresolved_records'],'validated',r['validated_records'],flush=True)
if __name__=='__main__':main()
