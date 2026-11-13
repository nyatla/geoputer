#!/usr/bin/env python3
"""Build en GisWordBooks from TI record tables, preserving every leaf code.

Inputs are catalog.tsv, original Wordbooks/CSVs, source/inputs and explicit
record decisions.
Legacy TT tables/reports are never used as inputs. Explicit record decisions
include the entire native hierarchy and are validated before use.
"""
from __future__ import annotations
import argparse, collections, configparser, csv, ctypes as C, ctypes.util, hashlib, importlib.metadata, io, json, os, re, shutil, sys, unicodedata as U, zipfile
from pathlib import Path
SDK=Path(os.environ.get('GALUCHAT_SDK',Path(__file__).resolve().parents[2]/'galuchat-gis-sdk'))
sys.path.insert(0,str(SDK/'python/src'))
from galuchat.io import BytesBufferReader
from galuchat.wordbook.core.GaluchatGisWordBookDom import GaluchatGisWordBookDom
from galuchat.wordbook.chunk.HierarchicalIndexChunkReader import HierarchicalIndexChunkReader
from galuchat.wordbook.chunk.TextTableChunk import decode_page_records_as_token_ids

DATE='2026-11-01'
NULL='NULL'
JAPANESE_FILES=('estat-regions-jp.json','estat-regions-en.json','KEN_ALL_ROME.zip','post-download.html','post-explanation.html','gsi-gazetteer-2021.pdf','gsi-gazetteer-errata-20260324.pdf','gsi-gazetteer-index.html','estat-1950-census-northern-names.pdf')
ESTAT='https://dashboard.e-stat.go.jp/api/1.0/Json/getRegionInfo?CallSystem=dash&Lang={lang}&RegionLevel=3,5,6,8,9,10,11,12,13&TimeTo=20260101'
POST='https://www.post.japanpost.jp/service/search/zipcode/download/roman-zip.html'
CA="https://www23.statcan.gc.ca/imdb/p3VD.pl?CLV=4&CPV=6104006&CST=01012021&CVD=1341558&Function=getVD&MLV=4&TVD=1346772&wbdisable=true"

def digest(data):return hashlib.sha256(data).hexdigest()
def write_tsv(p,fields,rows):
 with p.open('w',encoding='utf-8',newline='') as f:
  w=csv.DictWriter(f,fields,delimiter='\t',lineterminator='\n');w.writeheader();w.writerows(rows)
def latin(text):
 return all(('LATIN' in U.name(c,'') or c in 'ʻʼʹʺ' if U.category(c).startswith('L') else U.category(c)[0] in 'MNPSZ') for c in text)
def decode_dom(data):
 dom=GaluchatGisWordBookDom.fromBytes(data)
 texts=[b''.join(dom.token_map.tokens[t] for t in ids).decode('utf-8') for page in dom.text_table.pages for ids in decode_page_records_as_token_ids(page,dom.text_table.token_bits)]
 assert len(texts)==dom.text_table.record_count
 idx=dom.index
 ir=HierarchicalIndexChunkReader(BytesBufferReader(idx.index_stream),idx.max_depth,idx.record_count,idx.code_bits,idx.root_count,len(idx.index_stream))
 paths=[tuple(row) for row in ir.iterCodeSetsFor(range(idx.record_count))]
 assert len(paths)==idx.record_count and all(len(row)==idx.max_depth for row in paths)
 return dom,texts,paths

def region_slots(folder,depth):
 spec=(folder/'data-spec.md').read_text(encoding='utf-8')
 section=spec.split('### 3.2',1)[1].split('## 4.',1)[0]
 entries=re.findall(r'^\|\s*(区域\d+|属性\d+)\s*\|',section,re.M)
 if len(entries)!=depth:raise ValueError(f'{folder.name}: cannot identify {depth} source slots: {entries}')
 return {i for i,label in enumerate(entries) if label.startswith('区域')}

class IcuGreek:
 """Use installed ICU via its public C API, without an implicit Any-Latin fallback."""
 def __init__(self):
  libname=ctypes.util.find_library('icui18n')
  if not libname:raise RuntimeError('ICU icui18n is required for Greek UNGEGN conversion')
  self.lib=C.CDLL(libname);suffix=re.search(r'\.(\d+)(?:\.|$)',libname)
  self.version=suffix.group(1) if suffix else ''
  def fn(name):return getattr(self.lib,name+'_'+self.version if self.version else name)
  self.open=fn('utrans_openU'); self.open.restype=C.c_void_p
  self.open.argtypes=[C.POINTER(C.c_uint16),C.c_int32,C.c_int,C.c_void_p,C.c_int32,C.c_void_p,C.POINTER(C.c_int32)]
  self.apply=fn('utrans_transUChars');self.apply.argtypes=[C.c_void_p,C.POINTER(C.c_uint16),C.POINTER(C.c_int32),C.c_int32,C.c_int32,C.POINTER(C.c_int32),C.POINTER(C.c_int32)]
  self.close=fn('utrans_close');self.close.argtypes=[C.c_void_p]
  raw='Greek-Latin/UNGEGN; Latin-ASCII'.encode('utf-16le');p=(C.c_uint16*(len(raw)//2)).from_buffer_copy(raw);err=C.c_int32()
  self.handle=self.open(p,len(p),0,None,0,None,C.byref(err))
  if err.value>0:raise RuntimeError(f'ICU transform creation failed: {err.value}')
 def convert(self,text):
  # Lowercase before context-sensitive Greek rules; restore source casing afterward.
  raw=text.lower().encode('utf-16le');cap=max(256,len(raw)*8);buf=(C.c_uint16*cap)();C.memmove(buf,raw,len(raw))
  length=C.c_int32(len(raw)//2);limit=C.c_int32(length.value);err=C.c_int32()
  self.apply(self.handle,buf,C.byref(length),cap,0,C.byref(limit),C.byref(err))
  if err.value>0:raise ValueError(f'ICU transform failed: {err.value}')
  out=bytes(buf)[:length.value*2].decode('utf-16le')
  return out.upper() if text.isupper() else out.title()

class References:
 def __init__(self):
  self.entries=collections.defaultdict(lambda:collections.defaultdict(set));self.scopes={};self.gsi_rows=[];self.readings=collections.defaultdict(set)
 def add(self,source,target,ref,scope=None):
  if not source or not target or not latin(target):return
  self.entries[source][U.normalize('NFC',target)].add(ref)
  if scope is not None:
   self.scopes.setdefault(scope,References()).add(source,target,ref)
 def choose(self,text):
  if len(self.readings.get(text,()))>1:return None,''
  options=self.entries.get(text,{})
  if options:
   # Prefer e-Stat's paired region names/readings to postal spelling variants.
   tier=lambda ref:0 if ref.startswith('inputs/gsi-') else 1 if ref.startswith('inputs/estat-regions-') else 2
   best=min(tier(ref) for refs in options.values() for ref in refs)
   eligible={value:{ref for ref in refs if tier(ref)==best} for value,refs in options.items() if any(tier(ref)==best for ref in refs)}
   if len({v.casefold() for v in eligible})==1:
    value=next(iter(eligible));evidence=sorted(eligible[value]);return value,evidence[0]+(f' (+{len(evidence)-1} equivalent source rows)' if len(evidence)>1 else '')
  return None,''

def gsi_references(inputs,refs):
 from pypdf import PdfReader
 pdf=PdfReader(inputs/'gsi-gazetteer-2021.pdf');rows=[];serials=set()
 for page_no,page in enumerate(pdf.pages[7:],8):
  for line in page.extract_text(extraction_mode='layout').splitlines():
   match=re.match(r'^\s*(\d+)\s+(\d{4})\s+(.+)$',line)
   if match:serial,grid,body=int(match[1]),match[2],match[3]
   else:
    match=re.match(r'^\s*(\d{1,4})\s{2,}(.+)$',line)
    if not match:continue
    serial,grid,body=int(match[1]),'',match[2]
   parts=re.split(r'\s{2,}',body.strip())
   if len(parts)==5:parts=['']+parts  # Text extraction omits six Kanji cells; the PDF itself contains the names.
   if len(parts)!=6:raise ValueError(f'Unexpected GSI row {serial}: {parts}')
   native,kana,roman,latitude,longitude,kind=parts
   if not re.match(r'\d+°',latitude) or not re.match(r'\d+°',longitude):raise ValueError(f'Unexpected GSI coordinates at {serial}')
   if serial in serials:raise ValueError('Duplicate GSI source row')
   serials.add(serial)
   # The currently downloaded PDF already includes these five errata.
   corrections={142:('Aragusuku Shima','Aragusuku Jima'),1461:('Kikai Shima','Kikai Jima'),2177:('Myojindake','Myojingatake'),2570:('Ohiki Shima','Ohiki Jima'),2984:('Sarugaishi Gawa','Sarukaishi Gawa')}
   if serial in corrections:
    before,after=corrections[serial]
    if roman not in (before,after):raise ValueError(f'GSI errata/source mismatch at {serial}: {roman}')
    roman=after
   row=dict(serial=serial,pdf_page=page_no,grid=grid,source_name=native,kana=kana,roman=roman,latitude=latitude,longitude=longitude,classification=kind)
   rows.append(row)
   if native:
    refs.add(native,roman,f'inputs/gsi-gazetteer-2021.pdf:No.{serial}/PDF page {page_no}; errata 2026-03-24 applied')
    if kana:refs.readings[native].add(kana)
 if serials!=set(range(1,4124)):raise ValueError(f'GSI source inventory incomplete: {len(serials)} records')
 refs.gsi_rows=rows

def japanese_references(inputs):
 refs=References();sets=[];readings=collections.defaultdict(set)
 for language in ('jp','en'):
  data=json.loads((inputs/f'estat-regions-{language}.json').read_text(encoding='utf-8'))
  body=data['GET_META_REGION_INF']; assert body['RESULT']['status']=='0';labels={}
  for group in body['METADATA_INF']['CLASS_INF']['CLASS_OBJ']:
   entries=group.get('CLASS',[]);entries=[entries] if isinstance(entries,dict) else entries
   for entry in entries:
    if entry['@fromDate']<='202601'<entry['@toDate']:
     key=(group['@parentRegionCode'],entry['@regionCode'],entry['@level'])
     labels[key]=entry['@name']
     if language=='jp' and entry.get('@hiragana'):readings[entry['@name']].add(entry['@hiragana'])
  sets.append(labels)
 pref_names={key[1][:2]:name for key,name in sets[0].items() if key[2]=='3'}
 for key,name in sets[0].items():
  scope=pref_names.get(key[1][:2])
  if key in sets[1]:refs.add(name,sets[1][key],'inputs/estat-regions-jp.json + estat-regions-en.json:'+','.join(key),scope)
 # Retain official kana independently, including names with multiple EN spellings.
 for group in json.loads((inputs/'estat-regions-jp.json').read_text())['GET_META_REGION_INF']['METADATA_INF']['CLASS_INF']['CLASS_OBJ']:
  entries=group.get('CLASS',[]);entries=[entries] if isinstance(entries,dict) else entries
  for entry in entries:
   if entry['@fromDate']<='202601'<entry['@toDate'] and entry.get('@hiragana'):
    refs.readings[entry['@name']].add(entry['@hiragana'])
    scope=pref_names.get(entry['@regionCode'][:2])
    if scope:refs.scopes.setdefault(scope,References()).readings[entry['@name']].add(entry['@hiragana'])
 with zipfile.ZipFile(inputs/'KEN_ALL_ROME.zip') as z:
  for row in csv.reader(io.StringIO(z.read('KEN_ALL_ROME.CSV').decode('cp932'))):
   if len(row)!=7:raise ValueError('Unexpected Japan Post CSV column count')
   for native,english in [(row[1],row[4]),(row[2],row[5]),(row[3],row[6])]:
    refs.add(native,english,'inputs/KEN_ALL_ROME.zip:'+row[0],row[1])
   county=re.fullmatch(r'(.+?郡)(.+)',row[2]);roman=re.fullmatch(r'(.+?) GUN (.+)',row[5])
   if county and roman:
    refs.add(county[1],roman[1].title()+'-gun','inputs/KEN_ALL_ROME.zip:explicit GUN component:'+row[0],row[1])
    refs.add(county[2],roman[2].title(),'inputs/KEN_ALL_ROME.zip:town component:'+row[0],row[1])

 gsi_references(inputs,refs)
 # Public county spellings in the Statistics Bureau 1950 census, printed p.14.
 # Match their member village to current official JP/EN regional information.
 northern=[('色丹郡','Shikotan-gun','色丹村','Shikotan-mura'),('国後郡','Kunashiri-gun','泊村','Tomari-mura'),('択捉郡','Etorofu-gun','留別村','Rubetsu-mura'),('紗那郡','Shana-gun','紗那村','Shana-mura'),('蘂取郡','Shibetoro-gun','蘂取村','Shibetoro-mura')]
 for native,roman,village,expected in northern:
  scope=refs.scopes['北海道'];name,_=scope.choose(village)
  if name is None or name.casefold()!=expected.casefold():raise ValueError(f'Northern county member cannot be verified: {village}: {name}')
  refs.add(native,roman,'inputs/estat-1950-census-northern-names.pdf:printed p.14; member '+village+'='+name,'北海道')
 return refs

def local_references(folder,language,greek):
 refs=References()
 for p in folder.glob('*.names.csv'):
  with p.open(encoding='utf-8-sig',newline='') as f:
   for row in csv.DictReader(f):
    if language=='zh-hant':pairs=[('VILLNAME','VILLENG')]
    elif language=='sr-cyrl':pairs=[('ImeOpsCir','ImeOpsLat'),('ImeNasCir','ImeNasLat')]
    elif language=='el':pairs=[('VIL_NM_G','VIL_NM_E'),('QRTR_NM_G','QRTR_NM_E')]
    else:continue
    for a,b in pairs:
     source,target=row[a],row[b]
     if language=='el' and target and not latin(target):target=greek.convert(target)
     refs.add(source,target,p.name+':'+a+'/'+b)
     if language=='el':
      # Paired compound fields are optional evidence, never a prerequisite for conversion.
      left=[v.strip() for v in source.split(',')];right=[v.strip() for v in target.split(',')]
      if len(left)==len(right)>1:
       for native,english in zip(left,right):refs.add(native,english,p.name+':paired comma-separated components')
 return refs

class Converter:
 def __init__(self,language,refs,greek=None):self.language=language;self.refs=refs;self.greek=greek
 def convert(self,text):
  if text=='':return '', 'empty', ''
  if self.language=='ja' and text=='所属未定地':return 'Unassigned area','descriptive-label','Descriptive translation of an unassigned-area label; not a proper-name reading'
  if text=='Sambaa K\u0092e':return "Sambaa K'e",'verified-name',CA
  # Reversible repairs are made from the original TT, not previous correction tables.
  if any('\x80'<=c<='\x9f' for c in text):
   try:
    repaired=text.encode('latin-1').decode('utf-8')
    if repaired.encode('utf-8').decode('latin-1')==text and latin(repaired):return repaired,'encoding-repair','Latin-1 bytes -> UTF-8; inverse roundtrip checked'
   except (UnicodeEncodeError,UnicodeDecodeError):pass
  if latin(text):return text,'original-latin','original TT00'
  candidate,ref=self.refs.choose(text)
  if candidate is not None:return candidate,'source-paired-field',ref
  language=self.language
  if language=='ja':
   readings=self.refs.readings.get(text,set())
   if len(readings)==1:
    from pykakasi import kakasi
    target=''.join(p['hepburn'] for p in kakasi().convert(next(iter(readings)))).capitalize()
    if latin(target):return target,'verified-kana-hepburn','inputs/estat-regions-jp.json:@hiragana; no inferred Kanji reading'
   if all(not U.category(c).startswith('L') or 'HIRAGANA' in U.name(c,'') or 'KATAKANA' in U.name(c,'') for c in text):
    from pykakasi import kakasi
    target=''.join(p['hepburn'] for p in kakasi().convert(text))
    if latin(target):return target,'kana-hepburn','pykakasi: kana input only'
   return None,'unresolved-reading','Verified name/reading not available or ambiguous; no automatic Kanji reading'
  if language=='el':target=self.greek.convert(text);method='greek-ungegn';ref='ICU Greek-Latin/UNGEGN; Latin-ASCII'
  elif language=='sr-cyrl':
   cyr='АБВГДЂЕЖЗИЈКЛЉМНЊОПРСТЋУФХЦЧЏШ';lat=['A','B','V','G','D','Đ','E','Ž','Z','I','J','K','L','Lj','M','N','Nj','O','P','R','S','T','Ć','U','F','H','C','Č','Dž','Š']
   mapping=dict(zip(cyr,lat));mapping.update({k.lower():v.lower() for k,v in zip(cyr,lat)})
   target=''.join(mapping.get(c,c) for c in text);target=target.upper() if text.isupper() else target;method='serbian-latin';ref='Serbian Cyrillic/Latin alphabet'
  elif language=='ko':
   from koroman import romanize
   suffixes={'특별자치도':'teukbyeoljachi-do','특별자치시':'teukbyeoljachi-si','특별시':'teukbyeol-si','광역시':'gwangyeok-si','도':'do','시':'si','군':'gun','구':'gu','읍':'eup','면':'myeon','리':'ri','동':'dong','가':'ga'}
   target=None
   for suffix,out in suffixes.items():
    if text.endswith(suffix) and len(text)>len(suffix):
     target=romanize(text[:-len(suffix)],use_pronunciation_rules=True).capitalize()+'-'+out;break
   if target is None:target=romanize(text,use_pronunciation_rules=True).capitalize()
   method='korean-rr';ref='koroman 1.0.16; administrative suffix separated before pronunciation rules'
  elif language=='zh-hant':
   from pypinyin import lazy_pinyin,Style
   target=' '.join(lazy_pinyin(text,style=Style.NORMAL,errors=lambda s:list(s))).title();method='hanyu-pinyin';ref='pypinyin 0.55.0; toneless character-separated Hanyu Pinyin'
  else:return None,'unresolved-script','No implicit Any-Latin fallback is configured for this language'
  if not latin(target):return None,'unresolved-script',ref+': unconverted characters remain'
  return U.normalize('NFC',target),method,ref

def resolved_text(tt,path,targets,overrides,slot=None):
 if slot is not None:
  from abr_wordbook_names import context_key
  key=context_key(path,slot) if len(path)==4 else None
  if key in overrides:return overrides[key]
 return overrides.get((tt,path[0]),targets[tt])

def rebuild(dom,texts,paths,targets,name_slots,overrides=None):
 overrides=overrides or {}
 missing={tt for path in paths for slot,tt in enumerate(path) if slot in name_slots and resolved_text(tt,path,targets,overrides,slot) is None}
 if missing:return None,missing
 outpaths=[tuple(resolved_text(tt,path,targets,overrides,slot) if slot in name_slots else texts[tt] for slot,tt in enumerate(path)) for path in paths]
 # Byte-identical reuse is valid when every referenced value remains unchanged.
 if outpaths==[tuple(texts[tt] for tt in path) for path in paths]:return dom,set()
 out=GaluchatGisWordBookDom.createFromPaths(outpaths,page_size=dom.text_table.page_size,metadata=dom.header.metadata,text_encoding='utf-8')
 return out,set()

def verify(data,expected_paths,old_texts,old_paths,name_slots):
 dom,texts,paths=decode_dom(data)
 assert len(paths)==len(old_paths)
 assert dom.index.max_depth==len(old_paths[0])
 for ordinal,(refs,expected,old) in enumerate(zip(paths,expected_paths,old_paths),1):
  actual=tuple(texts[i] for i in refs)
  if actual!=expected:raise AssertionError(f'TI00 record {ordinal} changed order/content')
  assert all((actual[s]=='')==(old_texts[old[s]]=='') for s in range(len(old)))
  assert all(actual[s]==old_texts[old[s]] for s in range(len(old)) if s not in name_slots)
  assert all(latin(actual[s]) for s in name_slots)
 return dom,texts,paths

def build_one(root,out,entry,jp_refs,jp_inputs,greek,town_master=None):
 if entry['directory'] in ('jp-n03-y2024-1','jp-n03-y2025-1','jp-n03-y2026-1'):
  from rebuild_n03_records import build_release
  return build_release(root,out,entry)
 if entry['directory'] in ('jp-est-y2020-1','jp-esc-y2026-1'):
  from rebuild_estat_records import build_all
  return build_all(root,out,[entry['directory']],town_master=town_master)[0]
 if entry['language']=='ja':raise ValueError('Unsupported Japanese record dataset')
 from rebuild_foreign_records import build_release
 return build_release(root,out,entry,greek)


def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--mapsets',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--japanese-inputs',type=Path);ap.add_argument('--only',nargs='*');ap.add_argument('--town-master',type=Path);args=ap.parse_args()
 root=args.mapsets.resolve();out=args.output.resolve()
 if args.town_master is not None and not args.town_master.is_file():raise FileNotFoundError(args.town_master)
 if root==out:raise ValueError('Generate into a separate staging directory, then review/install')
 with (root/'catalog.tsv').open(encoding='utf-8',newline='') as f:entries=list(csv.DictReader(f,delimiter='\t'))
 if args.only:entries=[r for r in entries if r['directory'] in args.only]
 jp_inputs=args.japanese_inputs or root/'jp-n03-y2026-1/source/inputs'
 jp_refs=japanese_references(jp_inputs) if any(r['language']=='ja' and r['directory'] not in ('jp-n03-y2024-1','jp-n03-y2025-1','jp-n03-y2026-1','jp-est-y2020-1','jp-esc-y2026-1') for r in entries) else None
 greek=IcuGreek();out.mkdir(parents=True,exist_ok=True);reports=[]
 for entry in entries:
  town=args.town_master or (root/entry['directory']/'source/mt_town_all.csv')
  report=build_one(root,out,entry,jp_refs,jp_inputs,greek,town if town.is_file() else None);reports.append(report)
  print(entry['directory'],'TT',report['source_tt'],'NULL',report['unresolved_tt'],'OUTPUT',report['output_wordbook'] or 'held',flush=True)
 columns=['directory','source_language','wordbook_records','source_tt','ready_tt','unresolved_tt','output_wordbook','validated_records']
 rows=[dict(zip(columns,[r['directory'],r['language'],r['original_records'],r['source_tt'],r['ready_tt'],r['unresolved_tt'],r['output_wordbook'] or '',r['validated_records']])) for r in reports]
 write_tsv(out/'name-conversion.tsv',columns,rows)
 shutil.copyfile(root/'catalog.tsv',out/'catalog.tsv')
 for filename in ('NAME_CONVERSION.md','RECORD_RESEARCH.md'):
  spec=root/filename
  if spec.is_file():shutil.copyfile(spec,out/spec.name)
 print('SETS',len(reports),'OUTPUTS',sum(bool(r['output_wordbook']) for r in reports),'HELD',sum(not bool(r['output_wordbook']) for r in reports),'VALIDATED',sum(r['validated_records'] for r in reports),flush=True)
if __name__=='__main__':main()
