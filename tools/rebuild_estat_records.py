#!/usr/bin/env python3
"""Expand e-Stat TI records, resolve hierarchy, retain explicit record decisions."""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
from functools import lru_cache
from contextlib import ExitStack
from itertools import zip_longest
import configparser,csv,io,json,re,shutil,sys
from pathlib import Path

from rebuild_n03_records import Cell,read_book,digest,write_tsv,checksums,RegionalSources
from abr_wordbook_names import TownMaster,Bucket,Match,normalized,without_marker,romanize_kana,latin

DATASETS={
 'jp-est-y2020-1':dict(depth=4,month='202010',area=2,kinds=('都道府県','市区町村・行政区','小地区','下位地区')),
 'jp-esc-y2026-1':dict(depth=6,month='202601',area=4,kinds=('都道府県','郡','市区町村','行政区','小地区','下位地区'))}
BASE_EVIDENCE=['id','階層','種別','原文','変換後','状態','is_inferred','出典','資料内位置','判断','候補と補足']
DIRECTIONS={'東':'East','西':'West','南':'South','北':'North','中':'Central','上':'Upper','下':'Lower'}
LABELS={'その他':'Other areas','不詳':'Unspecified area','町丁字不詳':'Unspecified locality','町丁・字等不詳':'Unspecified locality',
 '所属未定地':'Unassigned area','水面':'Water area','海面':'Sea area','河川敷':'Riverbed','地先':'Adjacent area'}


def fields(depth):return ['id']+[v for n in range(1,depth+1) for v in (f'階層{n}',f'変換後階層{n}')]+['備考']
def decision_fields(depth):return ['id']+[f'階層{n}' for n in range(1,depth+1)]+['対象階層','変換後','状態','is_inferred','出典','資料内位置','判断']
def folded(s):return without_marker(s).translate(str.maketrans({'ヶ':'ケ','ヵ':'カ','ノ':'の','澤':'沢','濱':'浜','濵':'浜','邊':'辺','邉':'辺','﨑':'崎','髙':'高'}))
def base_city(city):
 m=re.fullmatch(r'(.+市).+区',city)
 return m[1] if m else None

def view4(path):
 if len(path)==4:return path
 return (path[0],path[1]+path[2]+path[3],path[4],path[5])


@dataclass(frozen=True)
class Finding:
 cell:Cell
 details:tuple=()


def result_cell(match,note=''):
 if match.state=='ready':
  return Finding(Cell(match.target,'inferred' if match.is_inferred else 'verified',match.is_inferred,
    'mt_town_all.csv' if match.method.startswith('abr') else 'original Wordbook/TI00' if match.method in ('source-kana','original-latin') else 'inputs/KEN_ALL_ROME.zip',match.reference,note or match.method),match.details)
 return Finding(Cell(None,note=note or match.method or '同一自治体の資料では読みを確定できない'),match.details)


class AddressEvidence:
 """Municipality- and parent-scoped spelling variants from original CSV rows."""
 def __init__(self,views):
  self.wanted={folded(s) for path in views for s in path[2:] if s}
  self.index={};self.rows_with_chome_and_koaza=0
 def add(self,mapping,key,parts,ref):mapping.setdefault(key,Bucket()).add(parts,ref)
 def __call__(self,scope,row,parent_parts,child_parts,ref):
  parent=row['oaza_cho'];child=row['koaza'] or row['chome']
  if row['koaza'] and row['chome']:self.rows_with_chome_and_koaza+=1
  pairs=[('area','',parent,parent_parts),('child',parent,child,child_parts),('area','',parent+child,parent_parts+child_parts)]
  for kind,p,n,parts in pairs:
   if n and folded(n) in self.wanted:
    self.add(self.index,(scope,kind,folded(p),folded(n)),parts,ref+':native='+n)
 def alternative(self,path,slot):
  scope=(normalized(path[0]),normalized(path[1]));kind='area' if slot==2 else 'child'
  parent='' if slot==2 else path[2]
  b=self.index.get((scope,kind,folded(parent),folded(path[slot])))
  if not b:return Match('missing')
  found=b.resolve()
  return Match(found.state,found.target,'abr-orthographic-variant',found.reference,found.state=='ready',({'rule':'orthographic-variant','name':path[slot],'source':found.reference},))


class AdministrativeCatalog:
 def __init__(self,inputs,month):
  raw=RegionalSources(inputs,month);self.index=defaultdict(dict);self.month=month
  self.pref={};city_by_code={}
  def unmark(s):return re.sub(r'^\(旧\)|^\*\s*','',s)
  entries=[e for values in raw.index.values() for e in values]
  for e in entries:
   if e['level']==3:self.pref[e['name']]=e
   if e['level'] in (8,9,10,12,13):city_by_code[e['code']]=e
  for e in entries:
   if e['level'] not in (8,9,10,11,12,13):continue
   native=unmark(e['name']);target=unmark(e['english']);aliases=[native]
   if e['level']==11:
    parent=city_by_code.get(e['parent'])
    if parent is None:continue
    aliases=[unmark(parent['name'])+native];target=unmark(parent['english'])+' '+target
   elif unmark(e['group']).endswith('郡'):aliases.append(unmark(e['group'])+native)
   for alias in aliases:self.index[(e['pref'],alias)].setdefault(target,[]).append(e)
 @lru_cache(maxsize=None)
 def resolve(self,pref,city):
  e=self.pref.get(pref)
  first=Finding(Cell(e['english'],'verified',False,'inputs/estat-regions-jp.json + inputs/estat-regions-en.json',e['locator'],'県の地域コード付き対訳')) if e else Finding(Cell(None,note='県の対訳未確定'))
  if not city:return (first,Finding(Cell('','empty',False,'original Wordbook/TI00',note='空階層を保持')))
  options=self.index.get((pref,city),{})
  if len(options)==1:
   target,entries=next(iter(options.items()));loc=' | '.join(e['locator'] for e in entries)
   return (first,Finding(Cell(target,'verified',False,'inputs/estat-regions-jp.json + inputs/estat-regions-en.json',loc,
    '同じ県・当時の有効期間・地域階層を照合。失効後に付加された(旧)／*は履歴表示として除外。行政区は所属市のコードで連結。')))
  return (first,Finding(Cell(None,note='当該年月・県の自治体対訳で一意に確定できない')))


class N03AdministrativeRecords:
 def __init__(self,root):
  folder=root/'jp-n03-y2026-1/source'
  with (folder/'records.tsv').open() as f:rows=list(csv.DictReader(f,delimiter='\t'))
  with (folder/'evidence.tsv').open() as f:evidence={(int(e['id']),int(e['階層'])):e for e in csv.DictReader(f,delimiter='\t')}
  self.records={}
  for row in rows:
   code=int(row['id']);key=tuple(row[f'階層{n}'] for n in (1,3,4,5));out=[]
   for n in (1,3,4,5):
    e=evidence[(code,n)];out.append(Finding(Cell(e['変換後'],e['状態'],e['is_inferred']=='true',
     'inputs/n03-2026-records.tsv + inputs/n03-2026-evidence.tsv',f'N03 id={code}; level={n}; '+e['資料内位置'],
     '原文の都道府県・郡・市区町村・行政区の組が一致する確認済みN03レコードを参照。')))
   if key in self.records and tuple(f.cell.value for f in self.records[key])!=tuple(f.cell.value for f in out):raise ValueError('Conflicting N03 hierarchy')
   self.records[key]=tuple(out)
 def resolve(self,path):
  return self.records.get(path[:4],tuple(Finding(Cell(None,note='N03行政階層の全組が未一致')) for _ in range(4)))


class RecordResolver:
 def __init__(self,master,extra):self.master=master;self.extra=extra
 def direct(self,path,slot):
  found=self.master.resolve(path,slot)
  if found.state in ('ready','ambiguous'):return found
  alt=self.extra.alternative(path,slot)
  if alt.state in ('ready','ambiguous'):return alt
  postal=self.master.postal_fallback(path,slot)
  return postal if postal.state=='ready' else found
 @lru_cache(maxsize=None)
 def lower(self,path):
  result=[]
  for slot in (2,3):
   name=path[slot]
   if not name:result.append(Finding(Cell('','empty',False,'original Wordbook/TI00',note='空階層を保持')));continue
   found=self.direct(path,slot)
   if found.state not in ('ready','ambiguous'):
    if latin(name):found=Match('ready',name,'original-latin','original Wordbook/TI00')
    else:
     kana=romanize_kana(name)
     if kana:found=Match('ready',kana,'source-kana','original Wordbook/TI00')
   if found.state not in ('ready','ambiguous'):
    found=self.master.additional_match(path,slot)
   if found.state not in ('ready','ambiguous'):
    descriptor=self.describe(path,slot,result[0] if result else None)
    if descriptor:result.append(descriptor);continue
   if found.state not in ('ready','ambiguous') and base_city(path[1]):
    broad=(path[0],base_city(path[1]),path[2],path[3]);candidate=self.direct(broad,slot)
    if candidate.state not in ('ready','ambiguous'):candidate=self.master.additional_match(broad,slot)
    if candidate.state=='ready':
     found=Match('ready',candidate.target,'abr-same-city-cross-ward',candidate.reference,True,
      ({'rule':'same-city-across-current-wards','original_ward':path[1],'city':broad[1],'source':candidate.reference},))
   result.append(result_cell(found))
  return tuple(result)
 def describe(self,path,slot,parent):
  name=path[slot]
  if name in LABELS:
   return Finding(Cell(LABELS[name],'descriptive_translation',True,'original Wordbook/TI00',f'level={slot+1}; name={name}',
    'このレコードの区域を表す状態・地理的説明として英訳。固有名の読みと区別。'))
  if slot==2 and name in ('字','大字','小字') and path[3]:
   return Finding(Cell({'字':'Aza','大字':'Oaza','小字':'Koaza'}[name],'descriptive_translation',True,
    'original Wordbook/TI00',f'parent={name}; child={path[3]}','下位地区の前に単独で分かれた住所階層標識として解釈。'))
  if slot==3 and parent and parent.cell.value and name in DIRECTIONS:
   return Finding(Cell(DIRECTIONS[name],'descriptive_translation',True,'original Wordbook/TI00',
    f'parent={path[2]}; child={name}','同レコードで確認した親区域内の位置区分として英訳。未確認の固有名読みを補わない。'))
  m=re.fullmatch(r'(.+?)(?:の一部|[（(]一部[）)])',name)
  if m:
   native=m[1];sub=list(path);sub[slot]=native;found=self.direct(tuple(sub),slot)
   if found.state not in ('ready','ambiguous'):found=self.master.additional_match(tuple(sub),slot)
   if found.state=='ready':
    return Finding(Cell(found.target+' (part)','inferred',True,'mt_town_all.csv',found.reference,
     '同じ所属で確認した区域名に、一部を表す説明を付加。'))
   if native in DIRECTIONS and parent and parent.cell.value:
    return Finding(Cell(DIRECTIONS[native]+' part','descriptive_translation',True,'original Wordbook/TI00',
     f'parent={path[2]}; descriptor={name}','確認済み親区域内の方角と一部という区分を説明訳。'))
  return None


def export_row(code,path,findings):
 row={'id':code}
 for n,(native,finding) in enumerate(zip(path,findings),1):
  row[f'階層{n}']=native;row[f'変換後階層{n}']='NULL' if finding.cell.value is None else finding.cell.value
 missing=[str(i) for i,f in enumerate(findings,1) if f.cell.value is None]
 inferred=[str(i) for i,f in enumerate(findings,1) if f.cell.inferred]
 row['備考']=' / '.join((['未解決階層='+','.join(missing)] if missing else [])+(['推定・説明訳の階層='+','.join(inferred)] if inferred else [])) or '資料照合済み（詳細はevidence.tsv）'
 return row


def load_release(root,name):
 with (root/'catalog.tsv').open() as f:entry=next(e for e in csv.DictReader(f,delimiter='\t') if e['directory']==name)
 native=root/name/entry['wordbook']
 if digest(native)!=entry['wordbook_sha256']:raise ValueError('Source Wordbook checksum changed')
 dom,texts,refs,paths=read_book(native.read_bytes());assert dom.index.max_depth==DATASETS[name]['depth']
 return entry,dom,texts,refs,paths


def make_master(csv_path,releases):
 views={view4(p) for release in releases.values() for p in release[4]}
 views.update((p[0],base_city(p[1]),p[2],p[3]) for p in list(views) if base_city(p[1]))
 text_ids={};virtual=[]
 for path in sorted(views):
  row=[]
  for value in path:
   if value not in text_ids:text_ids[value]=len(text_ids)
   row.append(text_ids[value])
  virtual.append(tuple(row))
 texts=list(text_ids);extra=AddressEvidence(views)
 master=TownMaster(csv_path,texts,virtual,'2026-11-01',include_city_scopes=True,observer=extra)
 return master,extra


def build_release(root,out,name,release,resolver,admin,n03):
 config=DATASETS[name];entry,dom,texts,refs,paths=release;dest=out/name;source=dest/'source';source.mkdir(parents=True,exist_ok=False)
 stats=Counter()
 for filename in ('records-expanded.tsv','records-initial.tsv','unresolved-initial.tsv'):
  with (source/filename).open('w',encoding='utf-8',newline='') as f:csv.DictWriter(f,fields(config['depth']),delimiter='\t',lineterminator='\n').writeheader()
 writers={};handles=[]
 for filename in ('records-expanded.tsv','records-initial.tsv','unresolved-initial.tsv'):
  h=(source/filename).open('a',encoding='utf-8',newline='');handles.append(h);writers[filename]=csv.DictWriter(h,fields(config['depth']),delimiter='\t',lineterminator='\n')
 ledger=(source/'initial-findings.jsonl').open('w',encoding='utf-8')
 try:
  for code,(path,ref) in enumerate(zip(paths,refs),1):
   empty=tuple(Finding(Cell('','empty',False)) if not s else Finding(Cell(None)) for s in path)
   writers['records-expanded.tsv'].writerow(export_row(code,path,empty))
   administrative=admin.resolve(path[0],path[1]) if len(path)==4 else n03.resolve(path)
   lower=resolver.lower(view4(path));findings=administrative+lower
   row=export_row(code,path,findings);writers['records-initial.tsv'].writerow(row)
   missing=sum(f.cell.value is None for f in findings);stats['records']+=1;stats['unresolved_records']+=missing>0;stats['unresolved_cells']+=missing
   if missing:writers['unresolved-initial.tsv'].writerow(row)
   stats['inferred_cells']+=sum(f.cell.inferred is True for f in findings)
   ledger.write(json.dumps([dict(value=f.cell.value,state=f.cell.state,inferred=f.cell.inferred,source=f.cell.source,locator=f.cell.locator,note=f.cell.note,details=f.details) for f in findings],ensure_ascii=False,separators=(',',':'))+'\n')
 finally:
  ledger.close()
  for h in handles:h.close()
 (source/'initial-report.json').write_text(json.dumps(dict(stats),ensure_ascii=False,indent=2)+'\n')
 print(name,dict(stats),flush=True)
 return stats


def read_decisions(path,paths,depth):
 """An explicit full native record is required, even when a local name repeats."""
 decisions={}
 if path is None or not path.exists():return decisions
 with path.open(encoding='utf-8',newline='') as f:
  reader=csv.DictReader(f,delimiter='\t')
  if reader.fieldnames!=decision_fields(depth):raise ValueError('Unexpected decision schema')
  for row in reader:
   code=int(row['id']);slot=int(row['対象階層'])-1
   if not 1<=code<=len(paths) or not 0<=slot<depth:raise ValueError('Invalid decision address')
   if tuple(row[f'階層{n}'] for n in range(1,depth+1))!=paths[code-1]:raise ValueError('Decision hierarchy mismatch')
   if (code,slot) in decisions:raise ValueError('Duplicate decision')
   value=row['変換後'];state=row['状態'];flag=row['is_inferred']
   if not value or value=='NULL' or not latin(value) or not paths[code-1][slot]:raise ValueError('Invalid decision name')
   if state not in ('verified','context_verified','descriptive_translation','inferred'):raise ValueError('Invalid decision state')
   if flag!=str(state in ('descriptive_translation','inferred')).lower():raise ValueError('Inconsistent inference flag')
   if not all(row[k].strip() for k in ('出典','資料内位置','判断')):raise ValueError('Decision lacks evidence')
   decisions[code,slot]=Finding(Cell(value,state,flag=='true',row['出典'],row['資料内位置'],row['判断']))
 return decisions


def checked_findings(code,path,row,ledger):
 if int(row['id'])!=code or tuple(row[f'階層{n}'] for n in range(1,len(path)+1))!=path:raise ValueError('Initial record hierarchy mismatch')
 if len(ledger)!=len(path):raise ValueError('Initial evidence depth mismatch')
 findings=[]
 for n,(native,d) in enumerate(zip(path,ledger),1):
  value=d['value']
  if row[f'変換後階層{n}']!=('NULL' if value is None else value):raise ValueError('Initial value differs from evidence')
  if (not native and value!='') or (native and value==''):raise ValueError('Empty hierarchy changed')
  if value and (value=='NULL' or not latin(value)):raise ValueError('Non-Latin result')
  if value is not None:
   if not d['source'] or d['inferred'] not in (True,False):raise ValueError('Initial result lacks provenance')
   if d['state'] not in ('verified','context_verified','empty','inferred','descriptive_translation'):raise ValueError('Invalid initial state')
  findings.append(Finding(Cell(value,d['state'],d['inferred'],d['source'],d['locator'],d['note']),tuple(d.get('details',()))))
 return findings


def english_paths_from_records(path,originals,depth):
 result=[]
 with path.open(encoding='utf-8',newline='') as f:
  reader=csv.DictReader(f,delimiter='\t')
  if reader.fieldnames!=fields(depth):raise ValueError('Unexpected final table schema')
  for code,(row,native) in enumerate(zip_longest(reader,originals),1):
   if row is None or native is None:raise ValueError('Record count differs')
   if int(row['id'])!=code or tuple(row[f'階層{n}'] for n in range(1,depth+1))!=native:raise ValueError('Final hierarchy differs')
   target=tuple(row[f'変換後階層{n}'] for n in range(1,depth+1))
   if any(t=='NULL' or (bool(t)!=bool(s)) or (t and not latin(t)) for s,t in zip(native,target)):raise ValueError('Unresolved or invalid final record')
   result.append(target)
 return result


def finish_release(root,initial,out,name,release,decisions_path=None,research=None,town_master=None):
 config=DATASETS[name];depth=config['depth'];entry,dom,texts,refs,paths=release
 src=root/name;dest=out/name;source=dest/'source';before=initial/name/'source';source.mkdir(parents=True,exist_ok=True)
 if before.resolve()!=source.resolve():
  for fn in ('records-expanded.tsv','records-initial.tsv','unresolved-initial.tsv'):shutil.copyfile(before/fn,source/fn)
 if decisions_path is None:decisions_path=src/'source/record-decisions.tsv'
 decisions=read_decisions(decisions_path,paths,depth)
 if decisions_path.exists():shutil.copyfile(decisions_path,source/'record-decisions.tsv')
 else:write_tsv(source/'record-decisions.tsv',decision_fields(depth),[])
 stats=Counter();tt_values=defaultdict(set);groups=Counter();used=set()
 with ExitStack() as stack:
  initial_file=stack.enter_context((before/'records-initial.tsv').open(encoding='utf-8',newline=''))
  ledger_file=stack.enter_context((before/'initial-findings.jsonl').open(encoding='utf-8'))
  initial_rows=csv.DictReader(initial_file,delimiter='\t')
  if initial_rows.fieldnames!=fields(depth):raise ValueError('Initial schema mismatch')
  writers={}
  for fn,columns in (('records.tsv',fields(depth)),('unresolved.tsv',fields(depth)),('evidence.tsv',BASE_EVIDENCE)):
   h=stack.enter_context((source/fn).open('w',encoding='utf-8',newline=''));w=csv.DictWriter(h,columns,delimiter='\t',lineterminator='\n');w.writeheader();writers[fn]=w
  for code,(path,ref,row,line) in enumerate(zip_longest(paths,refs,initial_rows,ledger_file),1):
   if any(v is None for v in (path,ref,row,line)):raise ValueError('Initial evidence record count mismatch')
   findings=checked_findings(code,path,row,json.loads(line));initial_missing=sum(f.cell.value is None for f in findings)
   stats['initial_unresolved_records']+=initial_missing>0;stats['initial_unresolved_cells']+=initial_missing
   for slot in range(depth):
    key=code,slot
    if key in decisions:
     if findings[slot].cell.value is not None:raise ValueError(f'Decision overwrites resolved cell {code}:{slot+1}')
     findings[slot]=decisions[key];used.add(key)
   final=export_row(code,path,findings);writers['records.tsv'].writerow(final)
   missing=sum(f.cell.value is None for f in findings);stats['records']+=1;stats['unresolved_records']+=missing>0;stats['unresolved_cells']+=missing
   if missing:writers['unresolved.tsv'].writerow(final)
   stats['inferred_cells']+=sum(f.cell.inferred is True for f in findings)
   for n,(native,f,tt) in enumerate(zip(path,findings,ref),1):
    c=f.cell;tt_values[tt].add(c.value)
    evidence=[code,n,config['kinds'][n-1],native,'NULL' if c.value is None else c.value,c.state,'' if c.inferred is None else str(c.inferred).lower(),c.source,c.locator,c.note,json.dumps(f.details,ensure_ascii=False,separators=(',',':')) if f.details else '']
    writers['evidence.tsv'].writerow(dict(zip(BASE_EVIDENCE,evidence)))
    if c.value is None:groups[(view4(path)[0],view4(path)[1],view4(path)[2],n,c.note)]+=1
 if used!=set(decisions):raise ValueError('Unused decision')
 group_fields=['都道府県','市区町村・行政区','親地区','未解決階層','未解決理由','件数']
 write_tsv(source/'unresolved-groups.tsv',group_fields,(dict(zip(group_fields,[*key,count])) for key,count in groups.most_common()))
 for p in src.iterdir():
  if p.is_file() and p.name not in ('README.md','dataset.ini','SHA256SUMS') and not p.name.endswith(('.en.giswordbook','.glc','.dat')):shutil.copyfile(p,dest/p.name)
 shutil.copytree(src/'source/inputs',source/'inputs',dirs_exist_ok=True)
 if research:shutil.copytree(research,source/'inputs/record-research',dirs_exist_ok=True)
 # The shared original ABR source is copied unchanged so either package can be rebuilt alone.
 csv_path=town_master or src/'source/mt_town_all.csv'
 if not csv_path.is_file():csv_path=root/'jp-est-y2020-1/source/mt_town_all.csv'
 shutil.copyfile(csv_path,source/'mt_town_all.csv')
 if not (dest/'NOTICE-ABR.md').is_file():shutil.copyfile(root/'jp-est-y2020-1/NOTICE-ABR.md',dest/'NOTICE-ABR.md')
 if depth==6:
  for original,fn in [('records.tsv','n03-2026-records.tsv'),('evidence.tsv','n03-2026-evidence.tsv')]:
   shutil.copyfile(root/'jp-n03-y2026-1/source'/original,source/'inputs'/fn)
 output=None;verified=0;output_tt=0
 if not stats['unresolved_records']:
  from rebuild_n03_records import GaluchatGisWordBookDom
  expected=english_paths_from_records(source/'records.tsv',paths,depth)
  packed=GaluchatGisWordBookDom.createFromPaths(expected,page_size=dom.text_table.page_size,metadata=dom.header.metadata,text_encoding='utf-8').toBytes()
  rebuilt,new_texts,_,actual=read_book(packed)
  if actual!=expected or rebuilt.header.metadata!=dom.header.metadata:raise ValueError('Rebuilt Wordbook differs from final records or metadata')
  output=Path(entry['wordbook']).stem+'.en.giswordbook';(dest/output).write_bytes(packed);verified=len(paths);output_tt=len(new_texts)
 ini=configparser.ConfigParser(interpolation=None);ini.read(src/'dataset.ini',encoding='utf-8')
 if output:
  en=dict(ini['ja']);en.update(title='Japan '+name[3:],description='Statistical areas with Latin-script place names.',wordbook=output)
  for key in ('display_line1','display_line2'):en[key]=' '.join('%'+v for v in re.findall(r'%(\d+)',en[key]))
  ini['en']=en
 elif ini.has_section('en'):
  ini.remove_section('en')
  if ini['dataset'].get('default_language')=='en':ini['dataset']['default_language']='ja'
 b=io.StringIO();ini.write(b,space_around_delimiters=False);(dest/'dataset.ini').write_text('# UTF-8. map.glc will be provided after map resolution selection.\n'+b.getvalue(),encoding='utf-8')
 inputs=[dict(file=str(p.relative_to(source)),sha256=digest(p)) for p in sorted((source/'inputs').rglob('*')) if p.is_file()]
 inputs.extend(dict(file=fn,sha256=digest(source/fn)) for fn in ('mt_town_all.csv','record-decisions.tsv'))
 unresolved_tt=sum(None in values for values in tt_values.values())
 report=dict(format_version='ti-record-research/2',created='2026-11-02',address_master_as_of='2026-11-01',directory=name,language='ja',source_wordbook=entry['wordbook'],source_sha256=digest(src/entry['wordbook']),source_zip=entry['source_zip'],source_zip_sha256=entry['source_sha256'],reference_month=config['month'],original_records=len(paths),depth=depth,id_origin=1,**{k:stats[k] for k in ('initial_unresolved_records','initial_unresolved_cells','unresolved_records','unresolved_cells','inferred_cells')},reviewed_cells=len(decisions),fully_resolved_records=len(paths)-stats['unresolved_records'],validated_context_records=len(paths),validated_records=verified,output_wordbook=output,output_sha256=digest(dest/output) if output else None,output_bytes=(dest/output).stat().st_size if output else 0,output_tt=output_tt,source_tt=len(texts),ready_tt=len(texts)-unresolved_tt,unresolved_tt=unresolved_tt,inputs=inputs)
 (source/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 result_description=(f'英字辞書`{output}`を最終表から構築し、TIの順序・階層・空欄・メタデータを全件再読込み検証した。' if output else '英字辞書は未生成。未確定を含んだ辞書やINIの`[en]`参照は作らない。今後すべて解決した時点で、最終表の各レコードからTT00・TM00を再構成し、TIの順序と階層を全件再読込み検証する。')
 (source/'README.md').write_text(f'''# TIレコード単位の地名調査: {name}

元TI00の全{len(paths):,}レコードを{depth}階層のまま展開した。階層は「{'／'.join(config['kinds'])}」。`id, 階層1, 変換後階層1, …, 備考`の順。idは元TIの葉レコード順（1始まりの地図参照コード）。空の階層を詰めず、未確定の英名はNULLにする。元のWordbook・町字マスターを変更せず、旧TT対応表や旧英字辞書は入力にしない。

| 段階 | 件数 |
| --- | ---: |
| 全レコード | {len(paths):,} |
| 初回照合で未解決のレコード | {stats['initial_unresolved_records']:,} |
| 資料を追加調査して判断したセル | {len(decisions):,} |
| 全階層を解決したレコード | {report['fully_resolved_records']:,} |
| 最終未解決レコード | {stats['unresolved_records']:,} |
| 最終未解決セル | {stats['unresolved_cells']:,} |

`records-expanded.tsv`は原TI展開直後、`records-initial.tsv`は初回照合後、`record-decisions.tsv`は追加調査のid・階層別判断、`records.tsv`はその適用後。初回・最終の未解決行は`unresolved-initial.tsv`／`unresolved.tsv`、次の調査対象を所属・親地区でまとめた一覧は`unresolved-groups.tsv`。`evidence.tsv`は全セルの根拠・位置・判断・推定フラグで、`is_inferred=true`を資料の直接一致と区別する。

初回は県・市区町村・行政区・親地区を照合して町字マスターを優先する。同じ自治体内で住所表記や区域の分割位置から推定したものは推定扱い。行政区再編前の統計区は元所属を保持する。統計上の学区名は住所とは別の項目なので、追加の市の学区対訳資料等から判断した。京都市で学区名と町名の連結と解釈したラベルは、元の文字列を全部説明できる場合だけ推定として採用し、重複や空階層を削除しない。

保存資料は`inputs/record-research/`に置いた。調査用対訳の`scoped-name-research.tsv`には県・自治体・資料内位置を付け、個別判断表には対象idの全原文階層を再掲する。舞鶴の同名を他市へ流用しない。河川・駅・旧郡・URLからの読みの転用も推定とする。全未解決行を個別に調査し終えたという意味ではない。根拠が足りない小字・列挙・施設名などは残している。

{result_description}`validated_context_records`は調査表の原文・コード・空欄・英字候補の検査数、`validated_records`は実際に生成した英字辞書の検証数。TT件数は共通一覧との互換用集計だけで判断には使わない。

出典と再現手順は[RECORD_RESEARCH.md](../../RECORD_RESEARCH.md)、町字マスターの出典・利用条件は[NOTICE-ABR.md](../NOTICE-ABR.md)、元データの条件は[NOTICE.md](../NOTICE.md)を参照。追加資料は地名の読み・綴りの根拠に限り使い、統計区の形状・所属を新しい時点へ更新しない。
''',encoding='utf-8')
 original_readme=(src/'README.md').read_text(encoding='utf-8').split('\n## 英字Wordbook',1)[0]
 (dest/'README.md').write_text(original_readme+'\n## 英字Wordbook\n\n[TIレコード単位の調査表と結果](source/README.md)を参照。'+result_description+'元言語の辞書と区域コードは維持する。\n',encoding='utf-8')
 checksums(source);checksums(dest,recursive=False)
 return report


def build_all(root,out,names,town_master=None,initial=None,decisions_root=None,research=None,initial_only=False):
 releases={n:load_release(root,n) for n in names}
 csv_path=town_master or root/'jp-est-y2020-1/source/mt_town_all.csv'
 if initial is None:
  print('Building address source index from original CSV',flush=True)
  master,extra=make_master(csv_path,releases);master.load_postal(root/names[0]/'source/inputs/KEN_ALL_ROME.zip')
  resolver=RecordResolver(master,extra);n03=N03AdministrativeRecords(root)
  for name,release in releases.items():
   admin=AdministrativeCatalog(root/name/'source/inputs',DATASETS[name]['month'])
   build_release(root,out,name,release,resolver,admin,n03)
  (out/'address-source.json').write_text(json.dumps(dict(sha256=master.sha256,input_statistics=dict(master.stats),rows_with_chome_and_koaza=extra.rows_with_chome_and_koaza),ensure_ascii=False,indent=2)+'\n')
  initial=out
 if initial_only:return []
 reports=[]
 for name,release in releases.items():
  decision=decisions_root/name/'record-decisions.tsv' if decisions_root else None
  report=finish_release(root,initial,out,name,release,decision,research,csv_path);reports.append(report)
  if initial==out:(out/name/'source/initial-findings.jsonl').unlink()
  print(name,'reviewed',report['reviewed_cells'],'resolved records',report['fully_resolved_records'],'remaining',report['unresolved_records'],flush=True)
 return reports


def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--mapsets',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--only',nargs='+',choices=list(DATASETS),default=list(DATASETS));p.add_argument('--town-master',type=Path)
 p.add_argument('--resume-initial',type=Path);p.add_argument('--decisions-root',type=Path);p.add_argument('--research-inputs',type=Path);p.add_argument('--initial-only',action='store_true')
 args=p.parse_args();root=args.mapsets.resolve();out=args.output.resolve()
 if out==root or root in out.parents:raise ValueError('Use separate output directory')
 build_all(root,out,args.only,args.town_master,args.resume_initial,args.decisions_root,args.research_inputs,args.initial_only)
if __name__=='__main__':main()
