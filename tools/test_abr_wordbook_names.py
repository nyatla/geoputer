import csv
import tempfile
import unittest
from pathlib import Path

from abr_wordbook_names import TownMaster, Bucket, normalized, romanize_kana, apply_town_master, kanji_number
from build_english_wordbooks import decode_dom, GaluchatGisWordBookDom, rebuild, verify

FIELDS = ['lg_code','machiaza_id','pref','pref_kana','pref_roma','county','county_kana','county_roma','city','city_kana','city_roma','ward','ward_kana','ward_roma','oaza_cho','oaza_cho_kana','oaza_cho_roma','chome','chome_kana','chome_number','koaza','koaza_kana','koaza_roma','efct_date','ablt_date']

def row(**kwargs):
    result = dict.fromkeys(FIELDS, '')
    result.update(lg_code='100001',machiaza_id='0001000',pref='県甲',pref_kana='ケンコウ',pref_roma='PrefA',city='市甲',city_kana='シコウ',city_roma='CityA',oaza_cho='本町',oaza_cho_kana='ホンチョウ',oaza_cho_roma='Honcho',efct_date='1947-04-17')
    result.update(kwargs)
    return result

class AbrTests(unittest.TestCase):
    def extended(self, master, path, slot):
        found=master.resolve(path,slot)
        return master.additional_match(path,slot) if found.state in ('missing','unreadable') else found

    def make_master(self, rows, names):
        temp = tempfile.TemporaryDirectory(); self.addCleanup(temp.cleanup)
        file = Path(temp.name)/'mt_town_all.csv'
        with file.open('w',encoding='utf-8',newline='') as f:
            w=csv.DictWriter(f,FIELDS);w.writeheader();w.writerows(rows)
        dom,texts,paths=decode_dom(GaluchatGisWordBookDom.createFromPaths(names).toBytes())
        return TownMaster(file,texts,paths,'2026-11-01'),dom,texts,paths

    def test_same_name_in_different_municipalities(self):
        a=('県甲','市甲','本町','');b=('県甲','市乙','本町','')
        master,_,_,_=self.make_master([row(),row(city='市乙',city_roma='CityB',oaza_cho_kana='モトマチ',oaza_cho_roma='Motomachi')],[a,b])
        self.assertEqual(master.resolve(a,2).target,'Honcho')
        self.assertEqual(master.resolve(b,2).target,'Motomachi')

    def test_same_municipality_name_in_different_prefectures(self):
        a=('県甲','市甲','本町','');b=('県乙','市甲','本町','')
        master,_,_,_=self.make_master([row(),row(pref='県乙',pref_roma='PrefB',oaza_cho_roma='Other',oaza_cho_kana='アザー')],[a,b])
        self.assertEqual(master.resolve(a,2).target,'Honcho')
        self.assertEqual(master.resolve(b,2).target,'Other')

    def test_same_child_is_scoped_by_parent_area(self):
        a=('県甲','市甲','親甲','上');b=('県甲','市甲','親乙','上')
        master,_,_,_=self.make_master([row(oaza_cho='親甲',oaza_cho_roma='ParentA',koaza='上',koaza_kana='カミ',koaza_roma='Kami'),row(oaza_cho='親乙',oaza_cho_roma='ParentB',koaza='上',koaza_kana='ウエ',koaza_roma='Ue')],[a,b])
        self.assertEqual(master.resolve(a,3).target,'Kami')
        self.assertEqual(master.resolve(b,3).target,'Ue')

    def test_conflict_inside_same_parent_remains_unknown(self):
        a=('県甲','市甲','本町','上')
        master,_,_,_=self.make_master([row(koaza='上',koaza_kana='カミ'),row(koaza='上',koaza_kana='ウエ')],[a])
        self.assertEqual(master.resolve(a,3).state,'ambiguous')

    def test_no_cross_municipality_fallback(self):
        a=('県甲','市乙','本町','')
        master,_,_,_=self.make_master([row()],[a])
        self.assertEqual(master.resolve(a,2).state,'missing')

    def test_verified_kana_and_kanji_are_distinct(self):
        self.assertEqual(romanize_kana('カミ'),'Kami')
        self.assertIsNone(romanize_kana('上'))

    def test_known_prefix_and_chome_notation(self):
        a=('県甲','市甲','大字本町','二丁目')
        master,_,_,_=self.make_master([row(chome='２丁目',chome_kana='２チョウメ',chome_number='2')],[a])
        self.assertEqual(master.resolve(a,2).target,'Honcho')
        self.assertEqual(master.resolve(a,3).target,'2-chome')
        self.assertEqual(normalized('三十二丁目'),'32丁目')

    def test_parent_ending_in_separator_requires_child_relation(self):
        a=('県甲','市甲','平泉字','樋渡');b=('県甲','市甲','平泉字','別名')
        master,_,_,_=self.make_master([row(oaza_cho='平泉',oaza_cho_kana='ヒライズミ',oaza_cho_roma='Hiraizumi',koaza='樋渡',koaza_roma='Hiwatari')],[a,b])
        self.assertEqual(master.resolve(a,2).target,'Hiraizumi')
        self.assertEqual(master.resolve(a,3).target,'Hiwatari')
        self.assertEqual(master.resolve(b,2).state,'missing')

    def test_hokkaido_jo_numbers_are_lookup_notation_only(self):
        a=('県甲','市甲','宮の森一条','二丁目')
        master,_,_,_=self.make_master([row(oaza_cho='宮の森１条',oaza_cho_kana='ミヤノモリ１ジョウ',oaza_cho_roma='Miyanomori 1-Jo',chome='２丁目',chome_kana='２チョウメ',chome_number='2')],[a])
        self.assertEqual(master.resolve(a,2).target,'Miyanomori 1-Jo')
        self.assertEqual(master.resolve(a,3).target,'2-chome')

    def test_parent_is_qualified_by_its_actual_child(self):
        a=('県甲','市甲','本町','一丁目');b=('県甲','市甲','本町','二丁目')
        master,_,_,_=self.make_master([row(chome='１丁目',chome_kana='１チョウメ',chome_number='1'),row(oaza_cho_kana='モトマチ',oaza_cho_roma='Motomachi',chome='２丁目',chome_kana='２チョウメ',chome_number='2')],[a,b])
        self.assertEqual(master.resolve(a,2).target,'Honcho')
        self.assertEqual(master.resolve(b,2).target,'Motomachi')

    def test_child_kana_is_not_duplicated_in_parent(self):
        a=('県甲','市甲','本町','二丁目')
        master,_,_,_=self.make_master([row(chome='１丁目',chome_kana='１チョウメ',chome_number='1'),row(oaza_cho_kana='ホンチョウニチョウメ',oaza_cho_roma='',chome='２丁目',chome_kana='２チョウメ',chome_number='2')],[a])
        self.assertEqual(master.resolve(a,2).target,'Honcho')
        self.assertEqual(master.resolve(a,3).target,'2-chome')

    def test_complete_tt_can_match_differently_split_source_fields(self):
        bucket=Bucket()
        bucket.add((('Honcho 1-chome','ホンチョウ１チョウメ'),),'record-one-field')
        bucket.add((('Honcho','ホンチョウ'),('1-chome','１チョウメ')),'record-two-fields')
        self.assertEqual(bucket.resolve().state,'ready')
        self.assertEqual(bucket.resolve().target,'Honcho 1-chome')

    def test_future_and_abolished_rows_are_not_used(self):
        a=('県甲','市甲','本町','')
        master,_,_,_=self.make_master([row(efct_date='2027-01-01'),row(ablt_date='2026-01-01')],[a])
        self.assertEqual(master.resolve(a,2).state,'missing')
        self.assertEqual(master.stats['inactive_rows'],2)

    def test_abr_precedence_and_ti_code_order(self):
        native=[('県甲','市甲','本町','上'),('県甲','市乙','本町','上')]
        master,dom,texts,paths=self.make_master([row(koaza='上',koaza_roma='Kami'),row(city='市乙',city_roma='CityB',oaza_cho_kana='モトマチ',oaza_cho_roma='Motomachi',koaza='上',koaza_roma='Ue')],native)
        table=[dict(source_tt=i,source_name=t,target_name='Old',status='ready',method='old',source_reference='old',candidates='',result_tt='',reference_count=0,role='name') for i,t in enumerate(texts)]
        targets,overrides,rows,stats=apply_town_master(master,texts,paths,table,['Old' for _ in texts],lambda t:(None,'',''),lambda p,t:(None,'',''))
        out,missing=rebuild(dom,texts,paths,targets,{0,1,2,3},overrides)
        self.assertFalse(missing)
        expected=[('PrefA','CityA','Honcho','Kami'),('PrefA','CityB','Motomachi','Ue')]
        verify(out.toBytes(),expected,texts,paths,{0,1,2,3})
        self.assertGreater(stats['overridden_previous_contexts'],0)

    def test_ambiguity_does_not_resurrect_an_old_guess(self):
        a=('県甲','市甲','本町','上')
        master,dom,texts,paths=self.make_master([row(koaza='上',koaza_kana='カミ'),row(koaza='上',koaza_kana='ウエ')],[a])
        table=[dict(source_tt=i,source_name=t,target_name='Old',status='ready',method='old',source_reference='old',candidates='',result_tt='',reference_count=0,role='name') for i,t in enumerate(texts)]
        targets,overrides,_,_=apply_town_master(master,texts,paths,table,['Old' for _ in texts],lambda t:('Old','',''),lambda p,t:('Old','',''))
        out,missing=rebuild(dom,texts,paths,targets,{0,1,2,3},overrides)
        self.assertIsNone(out)
        self.assertEqual(len(missing),1)

    def test_positional_and_digit_by_digit_numbers(self):
        self.assertEqual(kanji_number('十四'),14)
        self.assertEqual(kanji_number('一四'),14)
        self.assertEqual(kanji_number('四十'),40)
        self.assertEqual(normalized('一四条南四十丁目'),'14条南40丁目')
        self.assertEqual(normalized('百合が丘東五番町'),'百合が丘東5番町')
        self.assertEqual(normalized('一二三'),'一二三')
        self.assertEqual(normalized('三千二百十一番地'),'3211番地')

    def test_bancho_notation_is_master_match_not_inference(self):
        a=('県甲','市甲','百合が丘東五番町','')
        master,_,_,_=self.make_master([row(oaza_cho='百合が丘東５番町',oaza_cho_kana='ユリガオカヒガシ５バンチョウ',oaza_cho_roma='Yurigaokahigashi 5-Bancho')],[a])
        found=self.extended(master,a,2)
        self.assertEqual(found.target,'Yurigaokahigashi 5-Bancho')
        self.assertFalse(found.is_inferred)

    def test_digit_notation_parent_and_child_match_master(self):
        a=('県甲','市甲','西一四条南','四十丁目')
        master,_,_,_=self.make_master([row(oaza_cho='西１４条南',oaza_cho_kana='ニシ１４ジョウミナミ',oaza_cho_roma='Nishi14-Jominami',chome='４０丁目',chome_kana='４０チョウメ',chome_number='40')],[a])
        self.assertEqual(self.extended(master,a,2).target,'Nishi14-Jominami')
        self.assertEqual(self.extended(master,a,3).target,'40-chome')
        self.assertFalse(self.extended(master,a,3).is_inferred)

    def test_different_ti_address_split_can_use_combined_master(self):
        a=('県甲','市甲','西','一四条南四十丁目')
        master,_,_,_=self.make_master([row(oaza_cho='西１４条南',oaza_cho_kana='ニシ１４ジョウミナミ',oaza_cho_roma='Nishi14-Jominami',chome='４０丁目',chome_kana='４０チョウメ',chome_number='40')],[a])
        parent=self.extended(master,a,2);child=self.extended(master,a,3)
        self.assertEqual(parent.target,'Nishi')
        self.assertEqual(child.target,'14-jo-minami-40-chome')
        self.assertTrue(parent.is_inferred and child.is_inferred)

    def test_infer_stem_from_other_verified_name_in_same_city(self):
        a=('県甲','市甲','祖母谷','矢之助西')
        master,_,_,_=self.make_master([row(oaza_cho='矢之助町',oaza_cho_kana='ヤノスケチョウ',oaza_cho_roma='Yanosukecho')],[a])
        found=self.extended(master,a,3)
        self.assertEqual(found.target,'Yanosuke-nishi')
        self.assertTrue(found.is_inferred)
        self.assertIn('name=矢之助町',found.reference)
        self.assertEqual(found.details[0]['stem'],'矢之助')

    def test_inference_cannot_borrow_another_municipality(self):
        a=('県甲','市甲','祖母谷','矢之助西');b=('県甲','市乙','祖母谷','矢之助西')
        master,_,_,_=self.make_master([row(oaza_cho='矢之助町',oaza_cho_kana='ヤノスケチョウ',oaza_cho_roma='Yanosukecho'),row(city='市乙',city_roma='CityB')],[a,b])
        self.assertEqual(self.extended(master,a,3).state,'ready')
        self.assertEqual(self.extended(master,b,3).state,'missing')

    def test_competing_stem_readings_remain_unresolved(self):
        a=('県甲','市甲','祖母谷','矢之助西')
        master,_,_,_=self.make_master([row(oaza_cho='矢之助町',oaza_cho_kana='ヤノスケチョウ',oaza_cho_roma='Yanosukecho'),row(oaza_cho='矢之助村',oaza_cho_kana='ヤノジョムラ',oaza_cho_roma='Yanojomura')],[a])
        self.assertEqual(self.extended(master,a,3).state,'ambiguous')

    def test_exact_master_overrides_inferred_spelling(self):
        a=('県甲','市甲','祖母谷','矢之助西')
        master,_,_,_=self.make_master([row(oaza_cho='矢之助町',oaza_cho_kana='ヤノスケチョウ',oaza_cho_roma='Yanosukecho'),row(oaza_cho='祖母谷',koaza='矢之助西',koaza_kana='ヤノジョニシ',koaza_roma='Yanojo-nishi')],[a])
        found=self.extended(master,a,3)
        self.assertEqual(found.target,'Yanojo-nishi')
        self.assertFalse(found.is_inferred)

    def test_enumerated_names_keep_order_and_member_provenance(self):
        a=('県甲','市甲','平','宮ノ谷，宮ノ前，才勝')
        rows=[row(oaza_cho='平',koaza=n,koaza_kana=k,koaza_roma=r) for n,k,r in [('宮ノ谷','ミヤノタニ','Miyanotani'),('宮ノ前','ミヤノマエ','Miyanomae'),('才勝','サイカツ','Saikatsu')]]
        master,_,_,_=self.make_master(rows,[a])
        found=self.extended(master,a,3)
        self.assertEqual(found.target,'Miyanotani, Miyanomae, Saikatsu')
        self.assertFalse(found.is_inferred)
        self.assertEqual([d['name'] for d in found.details],['宮ノ谷','宮ノ前','才勝'])

    def test_list_with_one_inferred_member_has_true_flag(self):
        a=('県甲','市甲','平','宮ノ谷，矢之助西')
        master,_,_,_=self.make_master([row(oaza_cho='平',koaza='宮ノ谷',koaza_kana='ミヤノタニ',koaza_roma='Miyanotani'),row(oaza_cho='矢之助町',oaza_cho_kana='ヤノスケチョウ',oaza_cho_roma='Yanosukecho')],[a])
        found=self.extended(master,a,3)
        self.assertEqual(found.target,'Miyanotani, Yanosuke-nishi')
        self.assertTrue(found.is_inferred)
        self.assertEqual([d['is_inferred'] for d in found.details],[False,True])

    def test_unresolved_list_preserves_known_member_evidence(self):
        a=('県甲','市甲','平','宮ノ谷，未知地名')
        master,_,_,_=self.make_master([row(oaza_cho='平',koaza='宮ノ谷',koaza_kana='ミヤノタニ',koaza_roma='Miyanotani')],[a])
        found=self.extended(master,a,3)
        self.assertIsNone(found.target)
        self.assertEqual(found.details[0]['target'],'Miyanotani')
        self.assertEqual(found.details[1]['target'],None)

    def test_inference_flags_and_ti_order_survive_rebuild(self):
        names=[('県甲','市甲','本町',''),('県甲','市甲','矢之助西','')]
        master,dom,texts,paths=self.make_master([row(),row(oaza_cho='矢之助町',oaza_cho_kana='ヤノスケチョウ',oaza_cho_roma='Yanosukecho')],names)
        table=[dict(source_tt=i,source_name=t,target_name='Old',status='ready') for i,t in enumerate(texts)]
        targets,overrides,rows,stats=apply_town_master(master,texts,paths,table,['Old']*len(texts),lambda t:(None,'',''),lambda p,t:(None,'',''))
        out,missing=rebuild(dom,texts,paths,targets,{0,1,2,3},overrides)
        self.assertFalse(missing)
        verify(out.toBytes(),[('PrefA','CityA','Honcho',''),('PrefA','CityA','Yanosuke-nishi','')],texts,paths,{0,1,2,3})
        self.assertEqual(next(r for r in rows if r['source_name']=='本町')['is_inferred'],'false')
        self.assertEqual(next(r for r in rows if r['source_name']=='矢之助西')['is_inferred'],'true')
        self.assertEqual(next(r for r in table if r['source_name']=='矢之助西')['is_inferred'],'true')
        self.assertEqual(stats['inferred_contexts'],1)

    def test_literal_kana_is_not_replaced_by_municipal_inference(self):
        native=[('県甲','市甲','アツイ','')]
        master,_,texts,paths=self.make_master([row(koaza='小字アツイ',koaza_kana='コアザアツイ',koaza_roma='Koazaatsui')],native)
        table=[dict(source_tt=i,source_name=t,target_name='Old',status='ready') for i,t in enumerate(texts)]
        _,_,rows,_=apply_town_master(master,texts,paths,table,['Old']*len(texts),lambda t:('Atsui','kana-hepburn','') if t=='アツイ' else (None,'',''),lambda p,t:(None,'',''))
        found=next(row for row in rows if row['source_name']=='アツイ')
        self.assertEqual(found['target_name'],'Atsui')
        self.assertEqual(found['is_inferred'],'false')

if __name__=='__main__':unittest.main()
