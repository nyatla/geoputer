"""Test the firmware's file adapter against supplied real MapSet/SJIS WordBook files."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("mapset", type=Path)
parser.add_argument("wordbook", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="geoputer-place-") as directory:
    binary = Path(directory) / "place-tests"
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-D_GLIBCXX_ASSERTIONS", "-g",
        "-I", str(root / "application"), "-I", str(root / "third_party/galuchat/include"),
        str(root / "tests/place_file_tests.cpp"),
        str(root / "tests/map_cache_tests.cpp"),
        str(root / "application/task/storage/galuchat/CachedMapReader.cpp"),
        str(root / "application/task/storage/galuchat/PlaceLookup.cpp"),
        str(root / "application/task/storage/galuchat/TextEncoding.cpp"), "-o", str(binary)
    ], check=True, timeout=60)
    subprocess.run([str(binary), directory, str(args.mapset.resolve()), str(args.wordbook.resolve())],
                   check=True, timeout=120)
