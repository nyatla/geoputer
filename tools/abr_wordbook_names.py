"""Read ABR town names within their administrative and parent-area context.

This module never sees or transforms TM00 tokens. Inputs are complete native
hierarchy labels (from TI records or TT strings); supplied kana is the only
input to Hepburn conversion.
"""
from __future__ import annotations
from collections import Counter
from dataclasses import dataclass
from datetime import date
from functools import lru_cache
from pathlib import Path
import csv
import hashlib
import re
import unicodedata as ud
import zipfile
import io
import json


def file_sha256(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def latin(text):
    return bool(text) and text != 'NULL' and all(
        ('LATIN' in ud.name(c, '') or c in 'ʻʼʹʺ')
        if ud.category(c).startswith('L') else ud.category(c)[0] in 'MNPSZ'
        for c in text
    )


def kanji_number(text):
    if text.isascii() and text.isdigit():
        return int(text)
    digits = {c: i for i, c in enumerate('〇一二三四五六七八九')}
    digits['零'] = 0
    if text and all(c in digits for c in text):
        return int(''.join(str(digits[c]) for c in text))
    if not text or not re.fullmatch(r'(?:[一二三四五六七八九]?千)?(?:[一二三四五六七八九]?百)?(?:[一二三四五六七八九]?十)?[一二三四五六七八九]?', text):
        return None
    total = 0
    pending = 0
    for char in text:
        if char in digits:
            pending = digits[char]
        else:
            total += (pending or 1) * {'十': 10, '百': 100, '千': 1000}[char]
            pending = 0
    return total + pending


def normalized(text):
    text = ''.join(ud.normalize('NFKC', text).split())
    def street_number(match):
        value = kanji_number(match[1])
        return (str(value) if value is not None else match[1]) + match[2]
    # Only address-number positions; names such as 一二三 remain literal names.
    text = re.sub(r'([0-9〇零一二三四五六七八九十百千]+)(丁目|番町|番地|条|丁|番|号)', street_number, text)
    return text


def without_marker(text):
    text = normalized(text)
    if text.endswith('(番地)'):
        text = text[:-4]
    for prefix in ('大字', '小字', '字'):
        if text.startswith(prefix) and len(text) > len(prefix):
            return text[len(prefix):]
    return text


@lru_cache(maxsize=None)
def romanize_kana(kana):
    kana = ud.normalize('NFKC', kana).strip()
    if not kana or any(c in kana for c in '*〓�'):
        return None
    for char in kana:
        if ud.category(char).startswith('L') and not any(
            name in ud.name(char, '') for name in ('KATAKANA', 'HIRAGANA', 'LATIN')
        ):
            return None
    from pykakasi import kakasi
    result = ''.join(part['hepburn'] for part in kakasi().convert(kana)).capitalize()
    return result if latin(result) else None


@dataclass(frozen=True)
class Match:
    state: str
    target: str | None = None
    method: str = ''
    reference: str = ''
    is_inferred: bool = False
    details: tuple = ()


UNITS = {'丁目': ('chome', 'チョウメ'), '番町': ('bancho', 'バンチョウ'),
         '番地': ('banchi', 'バンチ'), '条': ('jo', 'ジョウ'),
         '丁': ('cho', 'チョウ'), '番': ('ban', 'バン'), '号': ('go', 'ゴウ')}
DIRECTIONS = {'東': ('higashi', 'ヒガシ'), '西': ('nishi', 'ニシ'),
              '南': ('minami', 'ミナミ'), '北': ('kita', 'キタ')}
STRUCTURE = re.compile(r'([0-9]+)(丁目|番町|番地|条|丁|番|号)|[東西南北]')


def structured(text):
    """Render explicit address units, not arbitrary Kanji readings."""
    words = []
    pos = 0
    for match in STRUCTURE.finditer(text):
        if match.start() != pos:
            return None
        words.append(match[1] + '-' + UNITS[match[2]][0] if match[1] else DIRECTIONS[match[0]][0])
        pos = match.end()
    return '-'.join(words) if words and pos == len(text) else None


def split_names(text):
    return tuple(p.strip() for p in re.split(r'[,，、]', text))


def stem_queries(text):
    """Only stems needed by actual TT strings, after explicit address affixes."""
    text = without_marker(text)
    yield text
    starts = [0] + [i for i in range(1, len(text)) if structured(text[:i])]
    ends = [len(text)] + [i for i in range(1, len(text)) if structured(text[i:])]
    for start in starts:
        for end in ends:
            if end-start >= 2:
                yield text[start:end]


def source_stems(name, kana):
    """Remove only paired native/kana affixes actually present in this source row."""
    name = normalized(name)
    kana = ud.normalize('NFKC', kana).strip()
    pending = [(name, kana)]
    seen = set()
    while pending:
        native, reading = pending.pop()
        if not native or not reading or (native, reading) in seen:
            continue
        seen.add((native, reading))
        yield native, reading
        for marker, spelling in [('大字','オオアザ'),('小字','コアザ'),('字','アザ')]:
            if native.startswith(marker):
                pending.append((native[len(marker):], reading[len(spelling):] if reading.startswith(spelling) else reading))
        for suffix, spellings in [('町',('チョウ','マチ')),('村',('ムラ','ソン'))]:
            if native.endswith(suffix) and len(native)-len(suffix) >= 2:
                for spelling in spellings:
                    if reading.endswith(spelling):pending.append((native[:-len(suffix)],reading[:-len(spelling)]))
        for direction, (_, spelling) in DIRECTIONS.items():
            if native.startswith(direction) and reading.startswith(spelling):pending.append((native[1:],reading[len(spelling):]))
            if native.endswith(direction) and reading.endswith(spelling):pending.append((native[:-1],reading[:-len(spelling)]))
        match = re.search(r'([0-9]+)(丁目|番町|番地|条|丁|番|号)$', native)
        if match:
            suffix = match[1] + UNITS[match[2]][1]
            if reading.endswith(suffix):pending.append((native[:match.start()],reading[:-len(suffix)]))


class MunicipalInference:
    """Verified source names/derived stems, confined to a single municipality.

    Only pieces that may serve an actual TT query are indexed. Inferred output
    is never fed back here, so guesses cannot bootstrap other guesses.
    """
    def __init__(self, texts, paths):
        members = {p for t in texts for p in split_names(t) if p}
        self.wanted = {without_marker(p) for p in members}
        self.pieces = {piece for p in members for piece in stem_queries(p)}
        self.combinations = {without_marker(texts[p[2]]+texts[p[3]]) for p in paths if texts[p[2]] and texts[p[3]]}
        self.names = {}
        self.stems = {}
        self.combined = {}

    @staticmethod
    def add(mapping, key, parts, reference):
        mapping.setdefault(key, Bucket()).add(parts, reference)

    def add_name(self, scope, name, parts, reference):
        key = without_marker(name)
        if not key:return
        if key in self.wanted:
            self.add(self.names, (scope,key), parts, reference + ':name=' + name)
        # A source stem needs a verified kana, including all source components.
        if not all(kana for _,kana in parts):return
        for stem,reading in source_stems(name,''.join(kana for _,kana in parts)):
            if stem in self.pieces:
                self.add(self.stems,(scope,stem),(('',reading),),reference + ':name=' + name + ':stem=' + stem)

    def add_record(self, scope, oaza, child, oaza_parts, child_parts, reference):
        if oaza:self.add_name(scope,oaza,oaza_parts,reference+':oaza_cho')
        if child:self.add_name(scope,child,child_parts,reference+':child')
        if oaza and child:
            combined = without_marker(oaza+child)
            if combined in self.combinations:
                self.add(self.combined,(scope,combined),oaza_parts+child_parts,reference+':oaza_cho+child')
            self.add_name(scope,oaza+child,oaza_parts+child_parts,reference+':oaza_cho+child')

    @staticmethod
    def inferred(found, method, detail):
        return Match(found.state,found.target,method,found.reference,found.state=='ready', (detail,))

    def resolve(self, scope, parent, name, combined):
        key = without_marker(name)
        if (scope,key) in self.names:
            found = self.names[(scope,key)].resolve()
            if found.state in ('ready','ambiguous'):
                return self.inferred(found,'abr-inferred-municipal-name',{'rule':'same-municipality-name','name':name,'source':found.reference})
        # TI may split the address inside an ABR oaza name, e.g. 西 / 14条南40丁目.
        full = self.combined.get((scope,without_marker(combined)))
        if full and structured(key):
            found=full.resolve()
            if found.state=='ready':
                return Match('ready',structured(key).capitalize(),'abr-inferred-boundary',found.reference,True,({'rule':'address-split','name':name,'combined_name':combined,'source':found.reference},))
        if (scope,key) in self.stems:
            found=self.stems[(scope,key)].resolve()
            if found.state in ('ready','ambiguous'):
                return self.inferred(found,'abr-inferred-affix',{'rule':'verified-stem','name':name,'source':found.reference})
        candidates=[]
        for start in range(len(key)):
            prefix = structured(key[:start]) if start else ''
            if prefix is None:continue
            for end in range(start+2,len(key)+1):
                suffix=structured(key[end:]) if end<len(key) else ''
                if suffix is None:continue
                anchor=key[start:end]
                bucket=self.stems.get((scope,anchor))
                if not bucket:continue
                found=bucket.resolve()
                if found.state=='ambiguous':
                    candidates.append((len(anchor),found))
                elif found.state=='ready':
                    target='-'.join(p for p in (prefix,found.target,suffix) if p)
                    target=target[0].upper()+target[1:]
                    detail={'rule':'verified-stem-and-address-affix','name':name,'stem':anchor,'stem_reading':found.target,'prefix':key[:start],'suffix':key[end:],'source':found.reference}
                    candidates.append((len(anchor),Match('ready',target,'abr-inferred-affix',found.reference,True,(detail,))))
        if candidates:
            longest=max(length for length,_ in candidates)
            best=[found for length,found in candidates if length==longest]
            if any(found.state=='ambiguous' for found in best) or len({found.target.casefold() for found in best})>1:
                return Match('ambiguous',method='abr-inference-ambiguous')
            return best[0]
        return Match('missing')


class Bucket:
    def __init__(self):
        self.variants = {}
        self.cached = None

    def add(self, parts, reference):
        self.variants.setdefault(parts, reference)
        self.cached = None

    def resolve(self):
        if self.cached is not None:
            return self.cached
        if not self.variants:
            return Match('missing')
        readings = set()
        candidates = {}
        unresolved_variants = 0
        for parts, reference in self.variants.items():
            words = []
            used_kana = False
            if all(kana for _, kana in parts):
                readings.add(''.join(ud.normalize('NFKC', kana).replace(' ', '') for _, kana in parts))
            for roma, kana in parts:
                if roma and latin(roma):
                    words.append(roma)
                else:
                    word = romanize_kana(kana)
                    if word is None:
                        unresolved_variants += 1
                        break
                    words.append(word)
                    used_kana = True
            else:
                target = ' '.join(words)
                key = ''.join(target.casefold().split())
                candidates.setdefault(key, (target, used_kana, reference))
        if len(candidates) == 1 and not (len(readings) > 1 and unresolved_variants):
            target, used_kana, reference = next(iter(candidates.values()))
            result = Match('ready', target, 'abr-kana-hepburn' if used_kana else 'abr-roma', reference)
        elif len(candidates) > 1 and len(readings) == 1:
            target = romanize_kana(next(iter(readings)))
            result = Match('ready', target, 'abr-kana-hepburn', next(iter(self.variants.values()))) if target else Match('unreadable', method='abr-unreadable')
        elif len(candidates) > 1 or len(readings) > 1:
            result = Match('ambiguous', method='abr-ambiguous', reference=' | '.join(list(self.variants.values())[:3]))
        else:
            result = Match('unreadable', method='abr-unreadable', reference=next(iter(self.variants.values())))
        self.cached = result
        return result


class TownMaster:
    """Index only names/municipalities needed by a Wordbook, streaming the CSV."""
    def __init__(self, path, texts, paths, as_of, *, include_city_scopes=False, observer=None):
        self.path = Path(path)
        self.include_city_scopes = include_city_scopes
        self.observer = observer
        self.exact = {}
        self.loose = {}
        self.prefectures = {}
        self.municipalities = {}
        self.postal = {}
        self.stats = Counter()
        self.as_of = as_of
        self.needed_scopes = {(normalized(texts[p[0]]), normalized(texts[p[1]])) for p in paths}
        self.wanted_names = {normalized(s) for s in texts} | {without_marker(s) for s in texts}
        self.wanted_names.update(normalized(part) for text in texts for part in split_names(text))
        self.wanted_names.update(without_marker(part) for text in texts for part in split_names(text))
        # e-Stat sometimes leaves the 字 separator at the end of the parent slot.
        self.wanted_names.update(normalized(texts[p[2]])[:-1] for p in paths if texts[p[3]] and normalized(texts[p[2]]).endswith('字'))
        self.inference = MunicipalInference(texts, paths)
        self.load()

    def component(self, row, field):
        roma = row.get(field + '_roma', '').strip()
        kana = row.get(field + '_kana', '').strip()
        if field == 'oaza_cho' and row.get('chome') and kana.endswith('チョウメ') and not normalized(row['oaza_cho']).endswith('丁目'):
            # This kana includes a child name and cannot be used as the parent's reading.
            self.stats['parent_kana_includes_child'] += 1
            kana = ''
        return (roma, kana)

    @staticmethod
    def add(mapping, key, parts, reference):
        mapping.setdefault(key, Bucket()).add(parts, reference)

    def wanted(self, name):
        return normalized(name) in self.wanted_names or without_marker(name) in self.wanted_names

    def add_name(self, scope, kind, parent, name, parts, reference):
        if not name or not self.wanted(name):
            return
        exact = (scope, kind, normalized(parent), normalized(name))
        loose = (scope, kind, without_marker(parent), without_marker(name))
        self.add(self.exact, exact, parts, reference)
        self.add(self.loose, loose, parts, reference)

    def load(self):
        required = {'lg_code', 'machiaza_id', 'pref', 'pref_kana', 'pref_roma', 'city', 'city_kana', 'city_roma', 'ward', 'ward_kana', 'ward_roma', 'oaza_cho', 'oaza_cho_kana', 'oaza_cho_roma', 'chome', 'chome_kana', 'chome_number', 'koaza', 'koaza_kana', 'koaza_roma', 'efct_date', 'ablt_date'}
        self.sha256 = file_sha256(self.path)
        with self.path.open(encoding='utf-8-sig', newline='') as f:
            reader = csv.DictReader(f)
            if not required <= set(reader.fieldnames or []):
                raise ValueError('ABR CSV missing required columns: ' + ','.join(sorted(required - set(reader.fieldnames or []))))
            for row in reader:
                self.stats['csv_rows'] += 1
                for name in ('efct_date', 'ablt_date'):
                    if row[name]:
                        date.fromisoformat(row[name])
                if (row['efct_date'] and row['efct_date'] > self.as_of) or (row['ablt_date'] and row['ablt_date'] <= self.as_of):
                    self.stats['inactive_rows'] += 1
                    continue
                ref = f"source/mt_town_all.csv:lg_code={row['lg_code']},machiaza_id={row['machiaza_id']}"
                pref = normalized(row['pref'])
                self.add(self.prefectures, pref, (self.component(row, 'pref'),), ref + ':pref')
                city = row['city'] + row['ward']
                city_parts = (self.component(row, 'city'),) + ((self.component(row, 'ward'),) if row['ward'] else ())
                aliases = [(city, city_parts)]
                if self.include_city_scopes and row['ward']:
                    aliases.append((row['city'], (self.component(row, 'city'),)))
                if row.get('county'):
                    aliases.append((row['county'] + city, (self.component(row, 'county'),) + city_parts))
                scopes = []
                for name, parts in aliases:
                    scope = (pref, normalized(name))
                    if scope in self.needed_scopes:
                        scopes.append(scope)
                        self.add(self.municipalities, scope, parts, ref + ':city+ward')
                if not scopes:
                    continue
                self.stats['scoped_csv_rows'] += 1
                oaza, child = row['oaza_cho'], row['koaza'] or row['chome']
                oaza_parts = (self.component(row, 'oaza_cho'),)
                child_field = 'koaza' if row['koaza'] else 'chome'
                if child_field == 'chome' and row['chome'] and row['chome_number'].isdigit() and normalized(row['chome']) in (str(int(row['chome_number'])) + '丁目', str(int(row['chome_number'])) + '丁'):
                    unit = '-chome' if normalized(row['chome']).endswith('丁目') else '-cho'
                    child_parts = ((str(int(row['chome_number'])) + unit, row['chome_kana']),)
                else:
                    child_parts = (self.component(row, child_field),)
                for scope in scopes:
                    if self.observer is not None:
                        self.observer(scope, row, oaza_parts, child_parts, ref)
                    self.inference.add_record(scope,oaza,child,oaza_parts,child_parts,ref)
                    self.add_name(scope, 'area', '', oaza, oaza_parts, ref + ':oaza_cho')
                    if oaza and self.wanted(oaza):
                        exact_key = (scope, 'parent-for-child', normalized(oaza), normalized(child))
                        loose_key = (scope, 'parent-for-child', without_marker(oaza), without_marker(child))
                        self.add(self.exact, exact_key, oaza_parts, ref + ':oaza_cho/child-qualified')
                        self.add(self.loose, loose_key, oaza_parts, ref + ':oaza_cho/child-qualified')
                    if child:
                        self.add_name(scope, 'child', oaza, child, child_parts, ref + ':' + child_field)
                        if oaza:
                            self.add_name(scope, 'area', '', oaza + child, oaza_parts + child_parts, ref + ':oaza_cho+' + child_field)
                        else:
                            self.add_name(scope, 'area', '', child, child_parts, ref + ':' + child_field)
        self.stats['unmatched_scopes'] = len(self.needed_scopes - self.municipalities.keys())

    def lookup(self, scope, kind, parent, name):
        key = (scope, kind, normalized(parent), normalized(name))
        if key in self.exact:
            return self.exact[key].resolve()
        key = (scope, kind, without_marker(parent), without_marker(name))
        if key in self.loose:
            return self.loose[key].resolve()
        return Match('missing')

    def child_lookup(self, scope, parent, child):
        found = self.lookup(scope, 'child', parent, child)
        if found.state == 'missing' and normalized(parent).endswith('字'):
            found = self.lookup(scope, 'child', normalized(parent)[:-1], child)
        return found

    def resolve(self, path, slot):
        pref, city, area, child = path
        scope = (normalized(pref), normalized(city))
        if slot == 0:
            return self.prefectures[scope[0]].resolve() if scope[0] in self.prefectures else Match('missing')
        if slot == 1:
            return self.municipalities[scope].resolve() if scope in self.municipalities else Match('missing')
        if scope not in self.municipalities:
            return Match('missing', method='abr-administration-not-found')
        if slot == 2:
            specific = self.lookup(scope, 'parent-for-child', area, child)
            if specific.state in ('ready', 'ambiguous'):
                return specific
            found = self.lookup(scope, 'area', '', area)
            if found.state == 'missing' and child and normalized(area).endswith('字'):
                # Drop a separator only when the ABR explicitly has that parent-child relation.
                relation = self.lookup(scope, 'child', normalized(area)[:-1], child)
                if relation.state != 'missing':
                    specific = self.lookup(scope, 'parent-for-child', normalized(area)[:-1], child)
                    found = specific if specific.state in ('ready', 'ambiguous') else self.lookup(scope, 'area', '', normalized(area)[:-1])
            return found
        return self.child_lookup(scope, area, child)

    def load_postal(self, path):
        with zipfile.ZipFile(path) as z:
            for row in csv.reader(io.StringIO(z.read('KEN_ALL_ROME.CSV').decode('cp932'))):
                pref, city, name = row[1], row[2], row[3]
                if not self.wanted(name):
                    continue
                cities = [normalized(city)]
                if '郡' in city:
                    cities.append(normalized(city.split('郡', 1)[1]))
                for value in cities:
                    scope = (normalized(pref), value)
                    if scope in self.needed_scopes:
                        self.add(self.postal, (scope, normalized(name)), ((row[6], ''),), 'inputs/KEN_ALL_ROME.zip:' + row[0])

    def postal_fallback(self, path, slot):
        if slot != 2:
            return Match('missing')
        scope = (normalized(path[0]), normalized(path[1]))
        key = (scope, normalized(path[slot]))
        if key not in self.postal:
            return Match('missing')
        found = self.postal[key].resolve()
        return Match(found.state, found.target, 'postal-municipality' if found.state == 'ready' else found.method, found.reference)

    def additional_match(self, path, slot):
        """After direct lookup, resolve lists and municipal evidence, never other cities."""
        if slot not in (2,3):return Match('missing')
        scope=(normalized(path[0]),normalized(path[1]))
        if scope not in self.municipalities:return Match('missing')
        name=path[slot]
        members=split_names(name)
        if len(members)>1:
            details=[];results=[]
            for member in members:
                child_path=list(path);child_path[slot]=member
                found=self.resolve(child_path,slot)
                if found.state in ('missing','unreadable'):
                    postal=self.postal_fallback(child_path,slot)
                    if postal.state=='ready':found=postal
                if found.state in ('missing','unreadable') and member:
                    found=self.additional_match(child_path,slot)
                if found.state in ('missing','unreadable'):
                    kana=romanize_kana(member)
                    if kana:found=Match('ready',kana,'kana-hepburn')
                results.append(found)
                details.append({'rule':'enumeration-member','name':member,'target':found.target,'state':found.state,'is_inferred':found.is_inferred if found.state=='ready' else None,'method':found.method,'source':found.reference,'details':found.details})
            if all(member and found.state=='ready' for member,found in zip(members,results)):
                return Match('ready',', '.join(found.target for found in results),'abr-enumeration',' | '.join(found.reference for found in results if found.reference),any(found.is_inferred for found in results),tuple(details))
            return Match('ambiguous' if any(found.state=='ambiguous' for found in results) else 'missing',method='abr-enumeration-unresolved',details=tuple(details))
        return self.inference.resolve(scope,path[2] if slot==3 else '',name,path[2]+path[3])


def context_key(path, slot):
    return (slot, path[slot], path[0], path[1], path[2] if slot == 3 else -1, path[3] if slot == 2 else -1)


def apply_town_master(master, texts, paths, table, baseline, old_resolver, pref_resolver, legacy_overrides=None):
    """Return values per source-TT use; no municipality's value becomes a global guess."""
    legacy_overrides = legacy_overrides or {}
    original_status = {row['source_tt']: row['status'] for row in table}
    usage = Counter(context_key(path, slot) for path in paths for slot in range(4))
    overrides = {}
    rows = []
    by_tt = {}
    inferred_by_tt = {}
    stats = Counter()
    for key, count in usage.items():
        slot, tt, pref, city, area, child = key
        values = (texts[pref], texts[city], texts[tt] if slot == 2 else texts[area] if area >= 0 else '', texts[tt] if slot == 3 else texts[child] if child >= 0 else '')
        if not texts[tt]:
            result = Match('ready', '', 'empty', '')
        else:
            result = master.resolve(values, slot)
            if result.state in ('missing', 'unreadable'):
                postal = master.postal_fallback(values, slot)
                if postal.state == 'ready':
                    result = postal
                elif slot in (0, 1):
                    value, method, ref = pref_resolver(texts[pref], texts[tt])
                    if value is not None:
                        result = Match('ready', value, method, ref)
            if result.state in ('missing','unreadable'):
                if latin(texts[tt]) or all(not ud.category(c).startswith('L') or 'HIRAGANA' in ud.name(c, '') or 'KATAKANA' in ud.name(c, '') for c in texts[tt]):
                    value, method, ref = old_resolver(texts[tt])
                    if value is not None:
                        result = Match('ready', value, method, ref)
            if result.state in ('missing','unreadable'):
                additional=master.additional_match(values,slot)
                if additional.state!='missing' or additional.method:
                    result=additional
        value = result.target if result.state == 'ready' else None
        overrides[key] = value
        previous = legacy_overrides.get((tt, pref), baseline[tt])
        stats['contexts'] += 1
        stats['references'] += count
        stats['abr_contexts'] += result.state == 'ready' and result.method.startswith('abr-')
        stats['inferred_contexts'] += value is not None and result.is_inferred
        stats['master_contexts'] += value is not None and result.method.startswith('abr-') and not result.is_inferred
        stats['enumeration_contexts'] += value is not None and result.method=='abr-enumeration'
        stats['unresolved_contexts'] += value is None
        stats['ambiguous_contexts'] += result.state == 'ambiguous'
        stats['overridden_previous_contexts'] += value is not None and previous is not None and value != previous
        rows.append(dict(source_tt=tt,source_name=texts[tt],slot=slot,prefecture_tt=pref,prefecture=texts[pref],municipality_tt=city,municipality=texts[city],parent_area_tt=area if area>=0 else '',parent_area=texts[area] if area>=0 else '',child_tt=child if child>=0 else '',child=texts[tt] if slot==3 else texts[child] if child>=0 else '',target_name='NULL' if value is None else value,status='unresolved' if value is None else 'ready',is_inferred='' if value is None else str(result.is_inferred).lower(),method=result.method or 'unresolved-reading',source_reference=result.reference,inference_details=json.dumps(result.details,ensure_ascii=False,separators=(',',':')) if result.details else '',result_tt='',reference_count=count,previous_target='NULL' if previous is None else previous))
        by_tt.setdefault(tt, []).append(value)
        inferred_by_tt.setdefault(tt,[]).append(result.is_inferred)
    targets = list(baseline)
    for row in table:
        tt = row['source_tt']
        values = by_tt.get(tt)
        if values is None:
            continue
        known = {v for v in values if v is not None}
        if any(v is None for v in values):
            row.update(target_name='NULL',status='unresolved',method='abr-context-unresolved',source_reference='source/tt-context-en.tsv')
            targets[tt] = None
        elif len(known)>1:
            row.update(target_name='NULL',status='context-dependent',method='abr-context-split',source_reference='source/tt-context-en.tsv')
            targets[tt] = None
        else:
            value = next(iter(known))
            row.update(target_name=value,status='ready',method='abr-context',source_reference='source/tt-context-en.tsv')
            targets[tt] = value
        row['candidates'] = ' | '.join(sorted(known))
        row['is_inferred'] = '' if row['status']=='unresolved' else str(any(inferred_by_tt[tt])).lower()
    for path in paths:
        for slot, source_tt in enumerate(path):
            value = overrides[context_key(path, slot)]
            assert value is None or value == '' or latin(value)
            assert (value == '') == (texts[source_tt] == '')
    stats['validated_context_records'] = len(paths)
    stats['fully_resolved_records'] = sum(all(overrides[context_key(path, slot)] is not None for slot in range(4)) for path in paths)
    stats['unresolved_records'] = len(paths) - stats['fully_resolved_records']
    stats['previous_fully_resolved_records'] = sum(all(legacy_overrides.get((tt,path[0]),baseline[tt]) is not None for tt in path) for path in paths)
    stats['newly_resolved_tt'] = sum(original_status[row['source_tt']]=='unresolved' and row['status']!='unresolved' for row in table)
    stats['newly_unresolved_tt'] = sum(original_status[row['source_tt']]!='unresolved' and row['status']=='unresolved' for row in table)
    stats['inferred_tt'] = sum(row.get('is_inferred')=='true' for row in table)
    return targets, overrides, rows, dict(stats)
