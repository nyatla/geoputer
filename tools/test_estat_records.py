import csv,json,tempfile,unittest
from unittest.mock import patch
from dataclasses import replace
from pathlib import Path
from rebuild_estat_records import (DATASETS,Finding,Cell,fields,decision_fields,view4,folded,
    read_decisions,checked_findings,finish_release,english_paths_from_records,export_row,write_tsv,digest)
from rebuild_n03_records import GaluchatGisWordBookDom,read_book

class EstatRecordTests(unittest.TestCase):
 def directory(self):
  t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);return Path(t.name)
 def test_schema_is_record_paired_at_both_depths(self):
  for d in (4,6):
   columns=fields(d);self.assertEqual(columns[0],'id');self.assertEqual(columns[-1],'備考')
   self.assertEqual(columns[1:-1],[v for n in range(1,d+1) for v in (f'階層{n}',f'変換後階層{n}')])
 def test_coast_administrative_prefix_keeps_county_and_ward(self):
  self.assertEqual(view4(('県','郡','町','','地区','字')),('県','郡町','地区','字'))
  self.assertEqual(view4(('県','','市','区','地区','字')),('県','市区','地区','字'))
 def decisions(self,rows,depth=4):
  p=self.directory()/'decisions.tsv';write_tsv(p,decision_fields(depth),rows);return p
 def decision(self,path,code=1,slot=3):
  return dict(zip(decision_fields(len(path)),[code,*path,slot,'Town','inferred','true','source','page=1','same-municipality evidence']))
 def test_same_name_different_municipality_cannot_reuse_decision(self):
  path=('県','甲市','同名','');row=self.decision(path);file=self.decisions([row])
  self.assertEqual(read_decisions(file,[path],4)[1,2].cell.value,'Town')
  with self.assertRaises(ValueError):read_decisions(file,[('県','乙市','同名','')],4)
 def test_duplicate_and_unsupported_inference_flags_rejected(self):
  p=('県','市','町','');r=self.decision(p)
  with self.assertRaises(ValueError):read_decisions(self.decisions([r,r]),[p],4)
  r['is_inferred']='false'
  with self.assertRaises(ValueError):read_decisions(self.decisions([r]),[p],4)
 def test_empty_slots_and_unresolved_are_distinct(self):
  p=('県','市','不明','');fs=[Finding(Cell('Pref')),Finding(Cell('City')),Finding(Cell(None)),Finding(Cell(''))]
  row=export_row(1,p,fs)
  self.assertEqual(row['変換後階層3'],'NULL');self.assertEqual(row['変換後階層4'],'')
 def fixture(self):
  root=self.directory();name='jp-est-y2020-1';src=root/name;source=src/'source';(source/'inputs').mkdir(parents=True)
  (src/'README.md').write_text('Native dataset\n');(src/'NOTICE.md').write_text('Native notice\n');(src/'NOTICE-ABR.md').write_text('ABR notice\n');(source/'mt_town_all.csv').write_text('fixture\n')
  (src/'dataset.ini').write_text('[dataset]\ndefault_language=ja\n[ja]\ntitle=日本\ndescription=Test\nwordbook=native.giswordbook\ndisplay_line1=%0\ndisplay_line2=%1 %2\n[en]\nwordbook=stale.en.giswordbook\n')
  paths=[('県','市','地区','')];data=GaluchatGisWordBookDom.createFromPaths(paths).toBytes();(src/'native.giswordbook').write_bytes(data)
  dom,texts,refs,paths=read_book(data);entry=dict(wordbook='native.giswordbook',source_zip='native.zip',source_sha256='fixture')
  before=self.directory();b=before/name/'source';b.mkdir(parents=True)
  cs=[Finding(Cell('Pref','verified',False,'fixture','p1','verified')),Finding(Cell('City','verified',False,'fixture','p1','verified')),Finding(Cell(None)),Finding(Cell('','empty',False,'original Wordbook/TI00','', 'empty'))]
  row=export_row(1,paths[0],cs)
  for fn in ('records-expanded.tsv','records-initial.tsv','unresolved-initial.tsv'):write_tsv(b/fn,fields(4),[row])
  ledger=[dict(value=f.cell.value,state=f.cell.state,inferred=f.cell.inferred,source=f.cell.source,locator=f.cell.locator,note=f.cell.note,details=[]) for f in cs]
  (b/'initial-findings.jsonl').write_text(json.dumps(ledger)+'\n')
  return root,before,name,(entry,dom,texts,refs,paths),cs,row,ledger
 def test_incomplete_records_hold_binary_and_remove_stale_en_entry(self):
  root,before,name,release,*_=self.fixture();out=self.directory()
  r=finish_release(root,before,out,name,release)
  self.assertEqual(r['unresolved_records'],1);self.assertEqual(r['validated_records'],0);self.assertIsNone(r['output_wordbook'])
  self.assertNotIn('[en]',(out/name/'dataset.ini').read_text());self.assertFalse(list((out/name).glob('*.en.giswordbook')))
  with self.assertRaises(ValueError):english_paths_from_records(out/name/'source/records.tsv',release[4],4)
 def test_complete_records_rebuild_from_record_table_and_preserve_ids(self):
  root,before,name,release,*_=self.fixture();out=self.directory();decision=self.decisions([self.decision(release[4][0])])
  r=finish_release(root,before,out,name,release,decision)
  self.assertEqual(r['unresolved_records'],0);self.assertEqual(r['validated_records'],1)
  self.assertEqual(read_book((out/name/r['output_wordbook']).read_bytes())[3],[('Pref','City','Town','')])
 def test_resume_rejects_changed_id_empty_slot_and_value(self):
  _,_,_,release,cs,row,ledger=self.fixture();p=release[4][0]
  self.assertEqual(len(checked_findings(1,p,row,ledger)),4)
  for broken in [dict(row,id='2'),dict(row,**{'階層3':'別名'}),dict(row,**{'変換後階層3':'Invented'})]:
   with self.assertRaises(ValueError):checked_findings(1,p,broken,ledger)
  altered=[dict(d) for d in ledger];altered[3]['value']=None
  with self.assertRaises(ValueError):checked_findings(1,p,row,altered)
 def test_decision_cannot_overwrite_master_result(self):
  root,before,name,release,*_=self.fixture();d=self.decisions([self.decision(release[4][0],slot=1)])
  with self.assertRaises(ValueError):finish_release(root,before,self.directory(),name,release,d)

 def test_city_wide_lookup_is_opt_in_and_never_leaves_municipality(self):
  from test_abr_wordbook_names import row,FIELDS
  from abr_wordbook_names import TownMaster
  file=self.directory()/'town.csv'
  with file.open('w',newline='') as f:
   w=csv.DictWriter(f,FIELDS);w.writeheader();w.writerow(row(ward='中央区',ward_roma='Chuo-ku',ward_kana='チュウオウク'))
  names=[('県甲','市甲','本町',''),('県甲','市乙','本町','')]
  _,texts,refs,_=read_book(GaluchatGisWordBookDom.createFromPaths(names).toBytes())
  self.assertEqual(TownMaster(file,texts,refs,'2026-11-01').resolve(names[0],2).state,'missing')
  observed=[]
  master=TownMaster(file,texts,refs,'2026-11-01',include_city_scopes=True,observer=lambda *args:observed.append(args))
  self.assertEqual(master.resolve(names[0],2).target,'Honcho')
  self.assertEqual(master.resolve(names[1],2).state,'missing');self.assertTrue(observed)
 def test_generic_builder_delegates_before_old_tt_converter(self):
  import build_english_wordbooks as generic
  with patch('rebuild_estat_records.build_all',return_value=[{'fixture':True}]) as mocked:
   result=generic.build_one(Path('/source'),Path('/out'),{'directory':'jp-esc-y2026-1'},None,None,None)
  self.assertEqual(result,{'fixture':True});self.assertEqual(mocked.call_args.args[2],['jp-esc-y2026-1'])

if __name__=='__main__':unittest.main()
