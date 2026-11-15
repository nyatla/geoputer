#!/usr/bin/env python3
"""Build local Geoputer firmware and one archive per multilingual mapset."""

from __future__ import annotations

import argparse
import configparser
import csv
import io
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile


PROJECT = Path(__file__).resolve().parents[1]
SDK_RELEASE = PROJECT.parent / 'galuchat-gis-sdk' / 'release'
SOURCE_CATALOG = PROJECT / 'tools' / 'mapsets_sources.tsv'
FIRMWARE = PROJECT / '.pio' / 'build' / 'cardputer-adv' / 'firmware.bin'
VERSION_HEADER = PROJECT / 'application' / 'Version.h'
EN_PACK = 'galuchat-languagepack-en-rev2.zip'
ESTAT_ID = 'jp-est-y2020-2'
ESTAT_SUBDIV = 'jp-2020-e-stat-go-jp-a002005212020-stat-small-area-subdiv-rev2.zip'
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def application_version() -> str:
    text = VERSION_HEADER.read_text(encoding='utf-8')
    match = re.search(r'ApplicationVersion\[\]\s*=\s*"([0-9A-Za-z][0-9A-Za-z.+-]*)"', text)
    if not match:
        raise ValueError(f'ApplicationVersion was not found in {VERSION_HEADER}')
    return match.group(1)


def read_catalog() -> dict[str, dict[str, str]]:
    with SOURCE_CATALOG.open(encoding='utf-8', newline='') as source:
        rows = list(csv.DictReader(source, delimiter='\t'))
    catalog = {row['id']: row for row in rows}
    if not rows or len(catalog) != len(rows):
        raise ValueError('mapset source catalog has missing or duplicate IDs')
    return catalog


def read_ini(data: bytes) -> configparser.ConfigParser:
    config = configparser.ConfigParser(interpolation=None, strict=True)
    config.optionxform = str
    config.read_file(io.StringIO(data.decode('utf-8-sig')))
    if not config.has_section('dataset'):
        raise ValueError('dataset.ini has no [dataset] section')
    return config


def plain_name(name: str) -> str:
    if not name or name in ('.', '..') or '/' in name or '\\' in name or ':' in name:
        raise ValueError(f'invalid filename in dataset.ini: {name!r}')
    if len(name.encode('utf-8')) > 127:
        raise ValueError(f'filename exceeds 127 bytes: {name!r}')
    return name


def zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    return info


def add_bytes(archive: zipfile.ZipFile, name: str, data: bytes) -> None:
    archive.writestr(zip_info(name), data, compresslevel=6)


def add_file(archive: zipfile.ZipFile, name: str, source: Path) -> None:
    with source.open('rb') as input_file, archive.open(zip_info(name), 'w', force_zip64=True) as output:
        shutil.copyfileobj(input_file, output, 1024 * 1024)


def source_notice(release_root: Path, release_zip: str) -> bytes:
    with zipfile.ZipFile(release_root / release_zip) as source:
        names = [name for name in source.namelist()
                 if name.rsplit('/', 1)[-1].lower() == 'notice.md'
                 or name.rsplit('/', 1)[-1].lower().endswith('.notice.md')]
        if len(names) != 1:
            raise ValueError(f'expected one NOTICE document in {release_zip}: {names}')
        return source.read(names[0])


def english_notice(release_root: Path, release_zip: str) -> bytes:
    stem = Path(release_zip).stem
    name = f'galuchat-languagepack-en/datasets/{stem}/NOTICE.md'
    with zipfile.ZipFile(release_root / EN_PACK) as source:
        return source.read(name)


def package_readme(mapset_id: str, languages: list[str], source_zip: str) -> bytes:
    lines = [f'# Geoputer mapset: {mapset_id}', '',
             f'Languages: {", ".join(languages)}',
             f'Map source: {source_zip}', '',
             'This archive contains one map and all supported Wordbooks for that map.',
             f'Extract the ZIP into a directory named {mapset_id}.',
             'Copy that directory into geoputer/dataset/ on a FAT32 SD card.',
             f'The resulting path must be geoputer/dataset/{mapset_id}/dataset.ini.',
             'Select the dataset on the device. All included languages can then be selected.',
             'The device creates _mapindex.dat when the dataset is applied.',
             'Dataset sources and attribution are in the NOTICE-*.md files here.']
    return ('\n'.join(lines) + '\n').encode('utf-8')


def mapset_files(folder: Path) -> tuple[configparser.ConfigParser, list[str], list[str]]:
    config = read_ini((folder / 'dataset.ini').read_bytes())
    languages = [section for section in config.sections() if section != 'dataset']
    if not languages or set(languages) - {'en', 'ja'}:
        raise ValueError(f'unsupported language list in {folder}: {languages}')
    if config.get('dataset', 'version') != '1':
        raise ValueError(f'unsupported dataset.ini version in {folder}')
    if config.get('dataset', 'default_language') not in languages:
        raise ValueError(f'default language missing from {folder}')
    filenames = [plain_name(config.get('dataset', 'map'))]
    for language in languages:
        wordbook = plain_name(config.get(language, 'wordbook'))
        if not wordbook.endswith('.giswordbook'):
            raise ValueError(f'not a Wordbook: {folder / wordbook}')
        filenames.append(wordbook)
    if len(filenames) != len(set(filenames)):
        raise ValueError(f'duplicate map or Wordbook filename in {folder}')
    for filename in filenames:
        if not (folder / filename).is_file():
            raise FileNotFoundError(folder / filename)
    return config, languages, filenames


def build_archive(folder: Path, catalog: dict[str, dict[str, str]],
                  release_root: Path, destination: Path) -> list[str]:
    mapset_id = folder.name
    if len(mapset_id) > 15 or not re.fullmatch(r'[a-z](?:[a-z0-9]|-(?=[a-z0-9]))*', mapset_id):
        raise ValueError(f'mapset ID cannot be used by firmware: {mapset_id}')
    if mapset_id not in catalog:
        raise ValueError(f'unknown mapset: {mapset_id}')
    _, languages, filenames = mapset_files(folder)
    source_zip = catalog[mapset_id]['release_zip']
    with zipfile.ZipFile(destination, 'w', allowZip64=True) as archive:
        add_file(archive, 'dataset.ini', folder / 'dataset.ini')
        for filename in filenames:
            add_file(archive, filename, folder / filename)
        add_bytes(archive, 'NOTICE-map.md', source_notice(release_root, source_zip))
        if 'en' in languages:
            wordbook_zip = ESTAT_SUBDIV if mapset_id == ESTAT_ID else source_zip
            add_bytes(archive, 'NOTICE-en.md', english_notice(release_root, wordbook_zip))
        if 'ja' in languages and mapset_id == ESTAT_ID:
            add_bytes(archive, 'NOTICE-ja.md', source_notice(release_root, ESTAT_SUBDIV))
        add_bytes(archive, 'README.md', package_readme(mapset_id, languages, source_zip))
    verify_archive(destination, mapset_id, languages, filenames)
    return languages


def verify_archive(path: Path, mapset_id: str, languages: list[str], filenames: list[str]) -> None:
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or archive.testzip() is not None:
            raise ValueError(f'ZIP has duplicate or damaged entries: {path}')
        required = {'dataset.ini', *filenames, 'README.md', 'NOTICE-map.md'}
        if 'en' in languages:
            required.add('NOTICE-en.md')
        if 'ja' in languages and mapset_id == ESTAT_ID:
            required.add('NOTICE-ja.md')
        if path.stem != mapset_id or set(names) != required:
            raise ValueError(f'ZIP must contain only flat files for {mapset_id}: {path}')
        config = read_ini(archive.read('dataset.ini'))
        if config.sections() != ['dataset', *languages]:
            raise ValueError(f'language list differs in {mapset_id}')
        if config.get('dataset', 'default_language') not in languages:
            raise ValueError(f'no valid default language in {mapset_id}')


def find_pio(explicit: Path | None) -> str:
    if explicit:
        return str(explicit)
    candidates = [PROJECT / '.venv' / 'bin' / 'pio', Path('/tmp/geoputer-pio-venv/bin/pio')]
    found = shutil.which('pio') or shutil.which('platformio')
    if found:
        return found
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    raise FileNotFoundError('PlatformIO not found; pass --pio /path/to/pio')


def build(args: argparse.Namespace) -> Path:
    version = application_version()
    if args.version and args.version != version:
        raise ValueError(f'--version {args.version} differs from application version {version}')
    output = args.output.resolve()
    target = output / f'v{version}'
    if target.exists():
        raise FileExistsError(f'release already exists: {target}')
    if not args.skip_firmware_build:
        subprocess.run([find_pio(args.pio), 'run', '-e', 'cardputer-adv'], cwd=PROJECT, check=True)
    if not FIRMWARE.is_file() or FIRMWARE.stat().st_size == 0:
        raise FileNotFoundError(f'firmware image missing: {FIRMWARE}')
    with FIRMWARE.open('rb') as firmware_image:
        if firmware_image.read(1) != b'\xe9':
            raise ValueError(f'firmware image has no ESP32 image header: {FIRMWARE}')

    catalog = read_catalog()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.release-stage-', dir=output) as temporary:
        stage = Path(temporary)
        mapsets = args.mapsets.resolve() if args.mapsets else stage / 'mapsets'
        if args.mapsets is None:
            subprocess.run([sys.executable, str(PROJECT / 'tools' / 'build_mapsets_from_sdk.py'),
                            '--sdk-release', str(args.sdk_release.resolve()), '--output', str(mapsets)],
                           cwd=PROJECT, check=True)
        found_ids = {path.name for path in mapsets.iterdir() if path.is_dir()}
        if found_ids != catalog.keys():
            raise ValueError(f'mapset directory IDs differ from source catalog: {sorted(found_ids ^ catalog.keys())}')
        ready = stage / f'v{version}'
        ready.mkdir()
        shutil.copyfile(FIRMWARE, ready / f'geoputer-v{version}-firmware.bin')
        for folder in sorted(path for path in mapsets.iterdir() if path.is_dir()):
            destination = ready / f'{folder.name}.zip'
            languages = build_archive(folder, catalog, args.sdk_release.resolve(), destination)
            print(f'verified {destination.name}: {", ".join(languages)}, {destination.stat().st_size} bytes', flush=True)
        ready.rename(target)
    print(f'release ready: {target}', flush=True)
    return target


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=PROJECT / 'release')
    parser.add_argument('--sdk-release', type=Path, default=SDK_RELEASE)
    parser.add_argument('--version', help='must match application/Version.h')
    parser.add_argument('--pio', type=Path, help='PlatformIO executable')
    parser.add_argument('--skip-firmware-build', action='store_true', help='use an existing firmware.bin')
    parser.add_argument('--mapsets', type=Path, help='use existing mapsets instead of rebuilding from SDK')
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        parser.exit(1, f'release build failed: {error}\n')


if __name__ == '__main__':
    main()
