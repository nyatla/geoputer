"""Check app-local Galuchat range APIs and unchanged released Reader behavior."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="reversegeoputer-ranges-") as directory:
    for name in ("sequential_reader_compatibility_test", "rect_without_tail_test", "chunk_range_test"):
        binary = Path(directory) / name
        subprocess.run([
            "g++", "-O2", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-fsanitize=undefined", "-fno-sanitize-recover=all", "-D_GLIBCXX_ASSERTIONS",
            "-I", str(root / "third_party/galuchat/include"),
            str(root / "tests/galuchat" / (name + ".cpp")), "-o", str(binary),
        ], check=True, timeout=90)
        subprocess.run([str(binary)], check=True, timeout=30)
