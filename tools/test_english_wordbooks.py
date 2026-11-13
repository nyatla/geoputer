#!/usr/bin/env python3
"""Regression checks for stable TI00 codes and conservative TT conversion."""
import unittest
from build_english_wordbooks import Converter,References,IcuGreek,GaluchatGisWordBookDom,decode_dom,rebuild,verify,latin

class WordBookTests(unittest.TestCase):
 def test_ti_order_empty_slots_and_duplicate_translations(self):
  source=[('区域乙',''),('区域甲','属性'),('区域乙',''),('区域丙','属性')]
  dom,texts,paths=decode_dom(GaluchatGisWordBookDom.createFromPaths(source).toBytes())
  replacements={'区域乙':'Beta','区域甲':'Alpha','区域丙':'Alpha'}
  targets=[replacements.get(s,s) for s in texts]
  out,missing=rebuild(dom,texts,paths,targets,{0})
  self.assertFalse(missing)
  expected=[('Beta',''),('Alpha','属性'),('Beta',''),('Alpha','属性')]
  verified,_,_=verify(out.toBytes(),expected,texts,paths,{0})
  self.assertEqual(verified.index.record_count,4)
 def test_shared_tt_name_and_attribute(self):
  dom,texts,paths=decode_dom(GaluchatGisWordBookDom.createFromPaths([('同名','同名')]).toBytes())
  out,missing=rebuild(dom,texts,paths,['Name' for t in texts],{0})
  self.assertFalse(missing)
  verify(out.toBytes(),[('Name','同名')],texts,paths,{0})
 def test_context_specific_tt_preserves_code_order(self):
  source=[('県A','同名'),('県B','同名'),('県A','同名')]
  dom,texts,paths=decode_dom(GaluchatGisWordBookDom.createFromPaths(source).toBytes())
  targets=[{'県A':'A','県B':'B'}.get(s) for s in texts]
  same=texts.index('同名');a=texts.index('県A');b=texts.index('県B')
  out,missing=rebuild(dom,texts,paths,targets,{0,1},{(same,a):'ReadingA',(same,b):'ReadingB'})
  self.assertFalse(missing)
  verify(out.toBytes(),[('A','ReadingA'),('B','ReadingB'),('A','ReadingA')],texts,paths,{0,1})
 def test_verified_kana_can_be_used_without_guessing(self):
  refs=References();refs.readings['日本橋'].add('にほんばし')
  self.assertEqual(Converter('ja',refs).convert('日本橋')[0],'Nihonbashi')
 def test_multiple_verified_readings_are_not_chosen_arbitrarily(self):
  refs=References();refs.readings['日本橋'].update(['にほんばし','にっぽんばし']);refs.add('日本橋','Nihonbashi','inputs/gsi-gazetteer-2021.pdf')
  self.assertIsNone(Converter('ja',refs).convert('日本橋')[0])
 def test_unknown_tt_blocks_generation(self):
  dom,texts,paths=decode_dom(GaluchatGisWordBookDom.createFromPaths([('難読地名','')]).toBytes())
  out,missing=rebuild(dom,texts,paths,[None if s else '' for s in texts],{0})
  self.assertIsNone(out);self.assertEqual(len(missing),1)
 def test_japanese_never_guesses_kanji(self):
  c=Converter('ja',References())
  self.assertIsNone(c.convert('日本橋')[0])
  self.assertEqual(c.convert('カタカナ')[0],'katakana')
 def test_verified_japanese_names_take_priority(self):
  refs=References();refs.add('北海道','Hokkaido','inputs/estat-regions-en.json');refs.add('北海道','HOKKAIDO','inputs/KEN_ALL_ROME.zip')
  self.assertEqual(Converter('ja',refs).convert('北海道')[0],'Hokkaido')
 def test_conflicting_readings_remain_null(self):
  refs=References();refs.add('同名','ReadingA','official:a');refs.add('同名','ReadingB','official:b')
  self.assertIsNone(Converter('ja',refs).convert('同名')[0])
 def test_greek_converts_without_csv(self):
  c=Converter('el',References(),IcuGreek())
  self.assertEqual(c.convert('ΛΕΥΚΩΣΙΑ')[0],'LEFKOSIA')
  self.assertEqual(c.convert('ΚΑΪΜΑΚΛΙ')[0],'KAIMAKLI')
 def test_repair_and_serbian_alphabet(self):
  self.assertEqual(Converter('mul',References()).convert('KaipÄ\x81tiki')[0],'Kaipātiki')
  self.assertEqual(Converter('sr-cyrl',References()).convert('ПАЛИЛУЛА (БЕОГРАД)')[0],'PALILULA (BEOGRAD)')
 def test_latin_names_keep_accents(self):
  c=Converter('fr',References());self.assertEqual(c.convert('Saint-Étienne')[0],'Saint-Étienne')
  self.assertFalse(latin('FΑLEIA'));self.assertFalse(latin('a\u0092b'))
if __name__=='__main__':unittest.main()
