"""Test the firmware's file adapter against supplied real MapSet/UTF-8 WordBook files."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("mapset", type=Path)
parser.add_argument("wordbook", type=Path)
parser.add_argument("--unsupported-wordbook", type=Path,
                    help="optional real legacy Wordbook to verify encoding rejection")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="geoputer-place-") as directory:
    binary = Path(directory) / "place-tests"
    subprocess.run([
        "g++", "-std=c++17", "-pthread", "-DREVERSEGEOPUTER_DIAGNOSTICS=1", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-D_GLIBCXX_ASSERTIONS", "-g",
        "-I", str(root / "tests/support"), "-I", str(root / "application"), "-I", str(root / "third_party/galuchat/include"),
        str(root / "tests/place_file_tests.cpp"),
        str(root / "tests/map_cache_tests.cpp"),
        str(root / "tests/dataset_file_tests.cpp"),
        str(root / "application/Settings.cpp"),
        str(root / "application/task/storage/SettingsStore.cpp"),
        str(root / "application/task/storage/log/LogReader.cpp"),
        str(root / "application/task/storage/log/LogWriter.cpp"),
        str(root / "application/task/storage/dataset/DatasetMetadata.cpp"),
        str(root / "application/task/storage/dataset/DatasetCatalog.cpp"),
        str(root / "application/task/storage/galuchat/CachedMapReader.cpp"),
        str(root / "application/task/storage/galuchat/DiskIndexedMapReader.cpp"),
        str(root / "application/task/storage/galuchat/DiskIndexedWordBookReader.cpp"),
        str(root / "application/task/storage/dataset/DatasetIndex.cpp"),
        str(root / "application/task/storage/galuchat/PlaceLookup.cpp"),
        str(root / "application/task/storage/galuchat/TextEncoding.cpp"), "-o", str(binary)
    ], check=True, timeout=180)
    command = [str(binary), directory, str(args.mapset.resolve()), str(args.wordbook.resolve())]
    if args.unsupported_wordbook:
        command.append(str(args.unsupported_wordbook.resolve()))
    subprocess.run(command,
                   check=True, timeout=120)
