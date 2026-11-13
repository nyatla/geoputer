import csv,json,tempfile,unittest,zipfile
from pathlib import Path
from rebuild_n03_records import (RegionalSources,Cell,DECISION_FIELDS,RECORD_FIELDS,
    apply_decisions,english_paths_from_records,record_rows,write_tsv,read_book,GaluchatGisWordBookDom)

class RecordTests(unittest.TestCase):
    def directory(self):
        t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);return Path(t.name)

    def sources(self):
        p=self.directory()
        groups=[('00000','全国',[('01000','北海道','Hokkaido',3),('02000','県甲','PrefA',3)]),
            ('01000','北海道',[('01300','振興局甲','OfficeA',5),('01400','振興局乙','OfficeB',5)]),
            ('01300','振興局甲',[('01303','泊村','Tomari-mura',13)]),
            ('01400','振興局乙',[('01403','泊村','Tomari-mura',13)]),
            ('02000','県甲',[('02100','市甲','CityA',8),('02200','市乙','CityB',8)]),
            ('02100','市甲',[('02101','中央区','Chuo-A',11)]),
            ('02200','市乙',[('02201','中央区','Chuo-B',11)])]
        for lang in ('jp','en'):
            result=[]
            for parent,name,entries in groups:
                result.append({'@parentRegionCode':parent,'@name':name,'CLASS':[
                    {'@regionCode':code,'@name':native if lang=='jp' else target,'@level':str(level),
                     '@fromDate':'200001','@toDate':'999912'} for code,native,target,level in entries]})
            (p/f'estat-regions-{lang}.json').write_text(json.dumps({'GET_META_REGION_INF':{
                'METADATA_INF':{'CLASS_INF':{'CLASS_OBJ':result}}}}))
        with zipfile.ZipFile(p/'KEN_ALL_ROME.zip','w') as z:
            z.writestr('KEN_ALL_ROME.CSV','"0000000","北海道","甲郡　泊村","","HOKKAIDO","GUNKO GUN TOMARI MURA",""\n'.encode('cp932'))
        return RegionalSources(p,'202601')

    def test_requested_table_order_and_one_based_ids(self):
        paths=[('県甲','','','市甲','')]
        cells=[[Cell('PrefA'),Cell(''),Cell(''),Cell('CityA'),Cell('')]]
        rows=record_rows(paths,cells)
        self.assertEqual(list(rows[0]),RECORD_FIELDS)
        self.assertEqual(RECORD_FIELDS,['id','階層1','変換後階層1','階層2','変換後階層2','階層3','変換後階層3','階層4','変換後階層4','階層5','変換後階層5','備考'])
        self.assertEqual(rows[0]['id'],1)

    def test_same_named_villages_use_subprefecture_identity(self):
        src=self.sources();a=src.resolve_record(('北海道','振興局甲','甲郡','泊村',''))
        b=src.resolve_record(('北海道','振興局乙','乙郡','泊村',''))
        self.assertEqual(a[3].value,b[3].value)
        self.assertIn('regionCode=01303',a[3].locator)
        self.assertIn('regionCode=01403',b[3].locator)

    def test_ward_belongs_to_city_resolved_in_that_record(self):
        src=self.sources()
        self.assertEqual(src.resolve_record(('県甲','','','市甲','中央区'))[4].value,'Chuo-A')
        self.assertEqual(src.resolve_record(('県甲','','','市乙','中央区'))[4].value,'Chuo-B')

    def test_county_postal_lookup_checks_member_and_whitespace(self):
        src=self.sources()
        self.assertEqual(src.resolve_record(('北海道','振興局甲','甲郡','泊村',''))[2].value,'Gunko-gun')
        self.assertIsNone(src.resolve_record(('北海道','振興局甲','甲郡','別村',''))[2].value)

    def test_record_reconstruction_rejects_shifted_id_and_unresolved(self):
        paths=[('県甲','','','市甲','')];cells=[[Cell('PrefA'),Cell(''),Cell(''),Cell('CityA'),Cell('')]]
        p=self.directory()/'records.tsv';rows=record_rows(paths,cells)
        write_tsv(p,RECORD_FIELDS,rows)
        self.assertEqual(english_paths_from_records(p,paths),[('PrefA','','','CityA','')])
        rows[0]['id']=2;write_tsv(p,RECORD_FIELDS,rows)
        with self.assertRaises(ValueError):english_paths_from_records(p,paths)
        rows[0]['id']=1;rows[0]['変換後階層4']='NULL';write_tsv(p,RECORD_FIELDS,rows)
        with self.assertRaises(ValueError):english_paths_from_records(p,paths)

    def test_decision_cannot_be_reused_for_other_native_hierarchy(self):
        paths=[('県甲','','','不明','')];cells=[[Cell('PrefA'),Cell(''),Cell(''),Cell(None),Cell('')]]
        row=dict(zip(DECISION_FIELDS,[1,'県乙','','','不明','',4,'Unknown','inferred','true','source','locator','reason']))
        p=self.directory()/'record-decisions.tsv';write_tsv(p,DECISION_FIELDS,[row])
        with self.assertRaises(ValueError):apply_decisions(paths,cells,p)
        row['階層1']='県甲';write_tsv(p,DECISION_FIELDS,[row])
        self.assertEqual(apply_decisions(paths,cells,p),1)
        self.assertEqual(cells[0][3].value,'Unknown')
        self.assertTrue(cells[0][3].inferred)

    def test_empty_slots_and_duplicate_english_records_survive_sdk(self):
        paths=[('PrefA','','','Same',''),('PrefA','','','Same','')]
        data=GaluchatGisWordBookDom.createFromPaths(paths).toBytes()
        self.assertEqual(read_book(data)[3],paths)
        self.assertEqual(len(read_book(data)[2]),2)

    def test_regional_pairs_use_each_releases_reference_month(self):
        p=self.directory()
        for language in ('jp','en'):
            pref={'@regionCode':'01000','@name':'北海道' if language=='jp' else 'Hokkaido','@level':'3','@fromDate':'200001','@toDate':'999912'}
            city={'@regionCode':'01100','@name':'札幌市' if language=='jp' else 'City-before-2025','@level':'8','@fromDate':'200001','@toDate':'999912' if language=='jp' else '202501'}
            cities=[city]
            if language=='en':cities.append(dict(city,**{'@name':'City-from-2025','@fromDate':'202501','@toDate':'999912'}))
            groups=[{'@parentRegionCode':'00000','@name':'全国','CLASS':[pref]},
                    {'@parentRegionCode':'01000','@name':'北海道','CLASS':cities}]
            (p/f'estat-regions-{language}.json').write_text(json.dumps({'GET_META_REGION_INF':{'METADATA_INF':{'CLASS_INF':{'CLASS_OBJ':groups}}}}))
        with zipfile.ZipFile(p/'KEN_ALL_ROME.zip','w') as z:z.writestr('KEN_ALL_ROME.CSV','')
        path=('北海道','','','札幌市','')
        self.assertEqual(RegionalSources(p,'202401').resolve_record(path)[3].value,'City-before-2025')
        self.assertEqual(RegionalSources(p,'202501').resolve_record(path)[3].value,'City-from-2025')
        self.assertEqual(RegionalSources(p,'202601').resolve_record(path)[3].value,'City-from-2025')

if __name__=='__main__':unittest.main()
