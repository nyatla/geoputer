#!/usr/bin/env python3
"""Non-Japanese Wordbooks: expand TI records, convert full labels, rebuild from the final table."""
from __future__ import annotations
from collections import Counter,defaultdict
from functools import lru_cache
from itertools import zip_longest
from pathlib import Path
import configparser,csv,importlib.metadata,io,json,shutil,sys
from build_english_wordbooks import (Converter,local_references,decode_dom,region_slots,latin,
    GaluchatGisWordBookDom,verify,digest)
from rebuild_n03_records import checksums

DATE='2026-11-02'
EVIDENCE=['id','階層','種別','原文','変換後','状態','is_inferred','出典','資料内位置','判断','候補と補足']

def fields(depth):return ['id']+[v for n in range(1,depth+1) for v in (f'階層{n}',f'変換後階層{n}')]+['備考']
def decision_fields(depth):return ['id']+[f'階層{n}' for n in range(1,depth+1)]+['対象階層','変換後','状態','is_inferred','出典','資料内位置','判断']
def table(path,columns,rows):
 with path.open('w',encoding='utf-8',newline='') as f:
  w=csv.DictWriter(f,columns,delimiter='\t',lineterminator='\n');w.writeheader();w.writerows(rows)
def target_row(code,path,values,flags=None):
 row={'id':code}
 for n,(native,target) in enumerate(zip(path,values),1):row.update({f'階層{n}':native,f'変換後階層{n}':'NULL' if target is None else target})
 missing=[str(n) for n,t in enumerate(values,1) if t is None]
 inferred=[str(n) for n,flag in enumerate(flags or (),1) if flag]
 row['備考']=' / '.join((['未解決階層='+','.join(missing)] if missing else [])+(['規則による翻字・推定階層='+','.join(inferred)] if inferred else [])) or '資料照合または原文保持（evidence.tsv参照）'
 return row

def paths_from_records(file,originals,name_slots):
 """The final record table, rather than a TT conversion array, drives serialization."""
 depth=len(originals[0]);result=[]
 with file.open(encoding='utf-8',newline='') as f:
  reader=csv.DictReader(f,delimiter='\t')
  if reader.fieldnames!=fields(depth):raise ValueError('Unexpected record schema')
  for code,(row,native) in enumerate(zip_longest(reader,originals),1):
   if row is None or native is None:raise ValueError('Record count changed')
   if int(row['id'])!=code or tuple(row[f'階層{n}'] for n in range(1,depth+1))!=native:raise ValueError('Native hierarchy or code changed')
   targets=tuple(row[f'変換後階層{n}'] for n in range(1,depth+1))
   for slot,(old,target) in enumerate(zip(native,targets)):
    if (old=='')!=(target==''):raise ValueError('Empty hierarchy changed')
    if slot in name_slots:
     if target=='NULL' or not latin(target):raise ValueError('Unresolved place name')
    elif target!=old:raise ValueError('Attribute changed')
   result.append(targets)
 return result

def apply_decisions(path,originals,values,evidence,name_slots):
 depth=len(originals[0]);seen=set()
 if not path.is_file():return 0
 with path.open(encoding='utf-8',newline='') as f:
  reader=csv.DictReader(f,delimiter='\t')
  if reader.fieldnames!=decision_fields(depth):raise ValueError('Unexpected decision schema')
  for row in reader:
   code=int(row['id']);slot=int(row['対象階層'])-1
   if not 1<=code<=len(originals) or slot not in name_slots:raise ValueError('Invalid decision target')
   if (code,slot) in seen:raise ValueError('Duplicate decision')
   if tuple(row[f'階層{n}'] for n in range(1,depth+1))!=originals[code-1]:raise ValueError('Decision belongs to another hierarchy')
   if values[code-1][slot] is not None:raise ValueError('Decision overwrites resolved name')
   target=row['変換後'];state=row['状態'];flag=row['is_inferred']
   if not target or target=='NULL' or not latin(target):raise ValueError('Invalid English name')
   if state not in ('verified','context_verified','inferred','descriptive_translation'):raise ValueError('Invalid decision state')
   if flag!=str(state in ('inferred','descriptive_translation')).lower():raise ValueError('Invalid inference flag')
   if not all(row[k].strip() for k in ('出典','資料内位置','判断')):raise ValueError('Missing decision evidence')
   values[code-1][slot]=target
   evidence[code-1][slot].update(変換後=target,状態=state,is_inferred=flag,出典=row['出典'],資料内位置=row['資料内位置'],判断=row['判断'])
   seen.add((code,slot))
 return len(seen)

def build_release(root,out,entry,greek):
 name=entry['directory'];language=entry['language']
 if language=='ja':raise ValueError('Japanese datasets have their own record resolver')
 src=root/name;dest=out/name;source=dest/'source';source.mkdir(parents=True,exist_ok=True)
 if any(source.iterdir()):raise ValueError('Refusing nonempty output source directory')
 original=(src/entry['wordbook']).read_bytes()
 if digest(original)!=entry['wordbook_sha256']:raise ValueError('Original Wordbook hash differs')
 dom,texts,refs=decode_dom(original);paths=[tuple(texts[tt] for tt in ref) for ref in refs]
 depth=dom.index.max_depth;slots=region_slots(src,depth);sources=local_references(src,language,greek)
 paired_transliterated=set()
 if language=='el':
  for file in src.glob('*.names.csv'):
   with file.open(encoding='utf-8-sig',newline='') as f:
    for row in csv.DictReader(f):
     for a,b in (('VIL_NM_G','VIL_NM_E'),('QRTR_NM_G','QRTR_NM_E')):
      if row[b] and not latin(row[b]):
       target=greek.convert(row[b]);paired_transliterated.add((row[a],target))
       left=[v.strip() for v in row[a].split(',')];right=[v.strip() for v in target.split(',')]
       if len(left)==len(right)>1:paired_transliterated.update(zip(left,right))
 converter=Converter(language,sources,greek)
 # A cache of conversions of complete labels is an optimization only. All decisions
 # and output values are stored at their original record id and hierarchy position.
 convert=lru_cache(maxsize=None)(converter.convert)
 values=[];evidence=[];expanded=[];methods=Counter()
 for code,path in enumerate(paths,1):
  row_values=[];row_evidence=[]
  expanded.append(target_row(code,path,[None if value and slot in slots else value for slot,value in enumerate(path)]))
  for slot,native in enumerate(path):
   if slot not in slots:target,method,reference=native,'attribute-passthrough','original Wordbook/TI00'
   else:target,method,reference=convert(native)
   if method=='source-paired-field' and (native,target) in paired_transliterated:method='source-paired-transliterated'
   if not native:state='empty';flag='false'
   elif slot not in slots:state='attribute_passthrough';flag='false'
   elif target is None:state='unresolved';flag=''
   elif method in ('greek-ungegn','serbian-latin','korean-rr','hanyu-pinyin','source-paired-transliterated'):state='transliterated';flag='true'
   elif method=='encoding-repair':state='encoding_repair';flag='false'
   else:state='verified';flag='false'
   row_values.append(target);methods[method]+=1
   row_evidence.append(dict(zip(EVIDENCE,[code,slot+1,'区域' if slot in slots else '属性',native,'NULL' if target is None else target,state,flag,
    reference or 'original Wordbook/TI00',f'TI id={code}; level={slot+1}; method={method}',method,
    json.dumps(sorted(sources.entries.get(native,{})),ensure_ascii=False) if slot in slots and sources.entries.get(native) else ''])))
  values.append(row_values);evidence.append(row_evidence)
 table(source/'records-expanded.tsv',fields(depth),expanded)
 initial=[target_row(code,path,row,[e['is_inferred']=='true' for e in es]) for code,(path,row,es) in enumerate(zip(paths,values,evidence),1)]
 table(source/'records-initial.tsv',fields(depth),initial)
 initial_unresolved=[r for r in initial if any(r[f'変換後階層{s+1}']=='NULL' for s in slots)]
 table(source/'unresolved-initial.tsv',fields(depth),initial_unresolved)
 decisions=src/'source/record-decisions.tsv';reviewed=apply_decisions(decisions,paths,values,evidence,slots)
 if decisions.is_file():shutil.copyfile(decisions,source/'record-decisions.tsv')
 else:table(source/'record-decisions.tsv',decision_fields(depth),[])
 final=[target_row(code,path,row,[e['is_inferred']=='true' for e in es]) for code,(path,row,es) in enumerate(zip(paths,values,evidence),1)]
 table(source/'records.tsv',fields(depth),final)
 unresolved=[r for r in final if any(r[f'変換後階層{s+1}']=='NULL' for s in slots)]
 table(source/'unresolved.tsv',fields(depth),unresolved)
 table(source/'evidence.tsv',EVIDENCE,(cell for row in evidence for cell in row))
 for p in src.iterdir():
  if p.is_file() and p.name not in ('README.md','dataset.ini','SHA256SUMS') and not p.name.endswith(('.en.giswordbook','.glc','.dat')):shutil.copyfile(p,dest/p.name)
 if (src/'source/inputs').is_dir():shutil.copytree(src/'source/inputs',source/'inputs')
 output=None;output_hash=None;output_tt=0;output_bytes=0;verified=0
 if not unresolved:
  expected=paths_from_records(source/'records.tsv',paths,slots)
  packed=original if expected==paths else GaluchatGisWordBookDom.createFromPaths(expected,page_size=dom.text_table.page_size,metadata=dom.header.metadata,text_encoding='utf-8').toBytes()
  rebuilt,new_texts,_=verify(packed,expected,texts,refs,slots)
  if rebuilt.header.metadata!=dom.header.metadata:raise ValueError('Metadata changed')
  output=Path(entry['wordbook']).stem+'.en.giswordbook'
  if len(output.encode('utf-8'))>127:raise ValueError('Output filename exceeds 127 bytes')
  (dest/output).write_bytes(packed);output_hash=digest(packed);output_tt=len(new_texts);output_bytes=len(packed);verified=len(paths)
 ini=configparser.ConfigParser(interpolation=None);ini.optionxform=str;ini.read(src/'dataset.ini',encoding='utf-8')
 if output:
  en=dict(ini[language]);en['wordbook']=output
  import re
  for k in ('display_line1','display_line2'):en[k]=' '.join('%'+v for v in re.findall(r'%(\d+)',en[k]))
  ini['en']=en
 elif language!='en':ini.remove_section('en')
 else:raise ValueError('Native English dataset unexpectedly unresolved')
 b=io.StringIO();ini.write(b,space_around_delimiters=False);(dest/'dataset.ini').write_text('# UTF-8. map.glc will be provided after map resolution selection.\n'+b.getvalue(),encoding='utf-8')
 uses=defaultdict(set)
 for ref,row in zip(refs,values):
  for tt,value in zip(ref,row):uses[tt].add(value)
 unresolved_tt=sum(None in choices for choices in uses.values())
 inputs=[dict(file='../'+p.name,sha256=digest(p.read_bytes())) for p in sorted(src.glob('*.names.csv'))]
 inputs += [dict(file=str(p.relative_to(source)),sha256=digest(p.read_bytes())) for p in sorted((source/'inputs').rglob('*')) if p.is_file()]
 inputs.append(dict(file='record-decisions.tsv',sha256=digest((source/'record-decisions.tsv').read_bytes())))
 report=dict(format_version='ti-record-research/2',created=DATE,directory=name,language=language,source_wordbook=entry['wordbook'],source_sha256=digest(original),source_zip=entry['source_zip'],source_zip_sha256=entry['source_sha256'],original_records=len(paths),depth=depth,name_slots=sorted(slots),id_origin=1,initial_unresolved_records=len(initial_unresolved),reviewed_cells=reviewed,fully_resolved_records=len(paths)-len(unresolved),unresolved_records=len(unresolved),unresolved_cells=sum(v is None for row in values for v in row),inferred_cells=sum(e['is_inferred']=='true' for row in evidence for e in row),validated_context_records=len(paths),validated_records=verified,source_tt=len(texts),ready_tt=len(texts)-unresolved_tt,unresolved_tt=unresolved_tt,methods=dict(methods),methods_unit='record-cell',output_wordbook=output,output_sha256=output_hash,output_tt=output_tt,output_bytes=output_bytes,inputs=inputs,versions=dict(python=sys.version.split()[0],icu=greek.version if language=='el' else None,**{m:importlib.metadata.version(m) for m in ('pypinyin','koroman')}))
 (source/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 status=f'全{verified:,}レコードを最終表から生成し再読込み検証済み。' if output else '未解決があるため英字辞書を生成保留。NULLという文字列を辞書へ入れない。'
 (source/'README.md').write_text(f'''# TIレコード単位の英字Wordbook生成: {name}

作成日: {DATE}。入力言語: `{language}`。全{len(paths):,}レコード、{depth}階層。区域の位置は0始まりで`{sorted(slots)}`。それ以外は属性として保持する。

調査表は`id, 階層1, 変換後階層1, …, 備考`。idは元TIの葉レコード順に1から振ったマップ参照コード。原文階層・空欄・属性を変えない。既存の英字は保持し、原資料の対訳を優先する。ギリシャ語、セルビア語、韓国語、繁体字中国語の翻字方式は従来と同じ。元TT全体から原文階層を復元するが、TMの断片を翻字せず、旧TT対応表や旧英字辞書を入力にしない。

`records-expanded.tsv`は原文展開直後（属性はそのまま）、`records-initial.tsv`は初回変換後、`record-decisions.tsv`は全原文階層付きのid・対象階層別の追加判断、`records.tsv`は最終表。未解決は初回・最終の`unresolved-initial.tsv`／`unresolved.tsv`にNULLで残す。`evidence.tsv`は全セルの根拠と推定フラグ。規則による翻字は`transliterated / is_inferred=true`として公称の英名との違いを明示する。原文英字・資料の対訳・空欄・属性はfalse。可逆な文字化け修復は`encoding_repair`。未知の非対応文字を暗黙の汎用翻字で埋めない。

{status} 英字辞書の入力は最終の`records.tsv`であり、TT番号の変換配列ではない。同じ英字名になった複数の区域も元のIDと順序を保つ。未解決がある場合は生成とINIの`[en]`追加を保留する。`report.json`は段階別件数・出典ハッシュ・翻字器の版を記録し、`methods`の単位はレコード内セル。TT件数は共通一覧との互換用集計だけであり、判断には使わない。

元辞書・元CSVとその`NOTICE.md`は保持する。入力資料は`report.json`の`inputs`を参照。[共通手順](../../RECORD_RESEARCH.md)と[言語別の方針](../../NAME_CONVERSION.md)に従う。
''',encoding='utf-8')
 readme=(src/'README.md').read_text(encoding='utf-8').split('\n## 英字Wordbook',1)[0]
 (dest/'README.md').write_text(readme+'\n## 英字Wordbook\n\n[TIレコード単位の調査表と結果](source/README.md)を参照。'+status+'\n',encoding='utf-8')
 checksums(source);checksums(dest,recursive=False)
 return report
