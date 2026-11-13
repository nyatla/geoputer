"""Check app-local Galuchat range APIs and unchanged released Reader behavior."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="geoputer-ranges-") as directory:
    for name in ("chunk_storage_test", "sequential_reader_compatibility_test", "rect_without_tail_test", "chunk_range_test", "block_range_test", "rle_span_test", "raw_row_test"):
        binary = Path(directory) / name
        subprocess.run([
            "g++", "-O2", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-fsanitize=undefined", "-fno-sanitize-recover=all", "-D_GLIBCXX_ASSERTIONS",
            "-I", str(root / "third_party/galuchat/include"),
            str(root / "tests/galuchat" / (name + ".cpp")), "-o", str(binary),
        ], check=True, timeout=90)
        subprocess.run([str(binary)], check=True, timeout=30)

with tempfile.TemporaryDirectory(prefix="geoputer-block-index-") as directory:
    binary = Path(directory) / 'block-index'
    subprocess.run([
        'g++', '-O2', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-fsanitize=undefined', '-fno-sanitize-recover=all', '-D_GLIBCXX_ASSERTIONS',
        '-DARDUINO_ARCH_ESP32','-DGEOPUTER_TEST_FATFS',
        '-I', str(root / 'tests/support/reader'),'-I',str(root / 'tests'),
        '-I', str(root / 'application'), '-I', str(root / 'third_party/galuchat/include'),
        str(root / 'tests/disk_block_index_test.cpp'),
        str(root / 'application/task/storage/galuchat/DiskIndexedMapReader.cpp'),
        '-o', str(binary),
    ], check=True, timeout=90)
    subprocess.run([str(binary)], check=True, timeout=30)
