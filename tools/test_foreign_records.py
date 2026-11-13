import csv,json,tempfile,unittest
from pathlib import Path
from rebuild_foreign_records import (fields,decision_fields,target_row,paths_from_records,table,build_release)
from build_english_wordbooks import GaluchatGisWordBookDom,decode_dom,digest,IcuGreek

class ForeignRecordsTests(unittest.TestCase):
 def directory(self):
  temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);return Path(temp.name)
 def fixture(self,language,paths,slots):
  root=self.directory();name='fixture';src=root/name;src.mkdir();(src/'source').mkdir()
  binary=GaluchatGisWordBookDom.createFromPaths(paths).toBytes();(src/'native.giswordbook').write_bytes(binary)
  (src/'data-spec.md').write_text('### 3.2 Fields\n'+''.join(f"| {'区域' if i in slots else '属性'}{i+1} | field |\n" for i in range(len(paths[0])))+'## 4. Next\n')
  (src/'dataset.ini').write_text(f'[dataset]\ndefault_language={language}\n[{language}]\nwordbook=native.giswordbook\ndisplay_line1=%0\ndisplay_line2=%1\n[en]\nwordbook=old.en.giswordbook\n' if language!='en' else '[dataset]\ndefault_language=en\n[en]\nwordbook=native.giswordbook\ndisplay_line1=%0\ndisplay_line2=%1\n')
  (src/'README.md').write_text('Original package\n');(src/'NOTICE.md').write_text('Original notice\n')
  (src/'source/tt-en.tsv').write_text('obsolete\nFAKE\n')
  entry=dict(directory=name,language=language,wordbook='native.giswordbook',wordbook_sha256=digest(binary),source_zip='original.zip',source_sha256='fixture')
  return root,self.directory(),entry
 def test_layout_and_expansion_distinguish_empty_unknown_and_attributes(self):
  row=target_row(1,('地名','','属性'),[None,'','属性'])
  self.assertEqual(list(row),fields(3));self.assertEqual(row['変換後階層1'],'NULL');self.assertEqual(row['変換後階層2'],'');self.assertEqual(row['変換後階層3'],'属性')
 def test_latin_preserved_and_old_tt_table_ignored(self):
  paths=[('Saint-Étienne',''),('Sainte-Marie','x')];root,out,e=self.fixture('fr',paths,{0})
  r=build_release(root,out,e,IcuGreek());dest=out/e['directory']
  self.assertEqual((dest/r['output_wordbook']).read_bytes(),(root/e['directory']/e['wordbook']).read_bytes());self.assertEqual(r['validated_records'],2)
  self.assertFalse((dest/'source/tt-en.tsv').exists());self.assertTrue((dest/'source/records.tsv').exists())
 def test_shared_name_attribute_and_duplicate_english_codes_preserved(self):
  paths=[('Ја','Ја'),('Ja','同じ'),('Ја','')];root,out,e=self.fixture('sr-cyrl',paths,{0});r=build_release(root,out,e,IcuGreek())
  dom,texts,refs=decode_dom((out/e['directory']/r['output_wordbook']).read_bytes())
  self.assertEqual([tuple(texts[i] for i in ref) for ref in refs],[('Ja','Ја'),('Ja','同じ'),('Ja','')])
  with (out/e['directory']/'source/evidence.tsv').open() as f:rows=list(csv.DictReader(f,delimiter='\t'))
  self.assertEqual(rows[0]['is_inferred'],'true');self.assertEqual(rows[1]['状態'],'attribute_passthrough')
 def test_unresolved_retained_and_en_ini_omitted(self):
  root,out,e=self.fixture('fr',[('未確認','')],{0});r=build_release(root,out,e,IcuGreek())
  self.assertIsNone(r['output_wordbook']);self.assertEqual(r['unresolved_records'],1)
  self.assertNotIn('[en]',(out/e['directory']/'dataset.ini').read_text())
 def test_final_table_rejects_changed_id_attribute_or_empty(self):
  p=self.directory()/'records.tsv';native=[('Name','属性','')];base=target_row(1,native[0],native[0])
  table(p,fields(3),[base]);self.assertEqual(paths_from_records(p,native,{0}),native)
  for updates in ({'id':2},{'変換後階層2':'Other'},{'変換後階層3':'Filled'},{'変換後階層1':'NULL'}):
   table(p,fields(3),[dict(base,**updates)])
   with self.assertRaises(ValueError):paths_from_records(p,native,{0})
 def test_full_record_decisions_can_resolve_unknown(self):
  paths=[('未確認','属性')];root,out,e=self.fixture('fr',paths,{0})
  row=dict(zip(decision_fields(2),[1,*paths[0],1,'Verified','verified','false','official','page1','matching full record']))
  table(root/e['directory']/'source/record-decisions.tsv',decision_fields(2),[row])
  r=build_release(root,out,e,IcuGreek());self.assertEqual(r['reviewed_cells'],1);self.assertEqual(r['validated_records'],1)

if __name__=='__main__':unittest.main()
