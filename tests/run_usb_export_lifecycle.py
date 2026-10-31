"""Run the real USB export state machine with allocation/close failure injection."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="reversegeoputer-usb-export-") as directory:
    binary=Path(directory)/"usb-export-tests"
    sources=["tests/usb_export_lifecycle_tests.cpp","application/apps/usb/UsbStorageTask.cpp",
             "application/task/storage/export/LogExporter.cpp","application/task/storage/log/LogReader.cpp",
             "application/task/storage/log/LogWriter.cpp"]
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-pedantic","-fno-access-control",
        "-fsanitize=undefined","-fno-sanitize-recover=all","-D_GLIBCXX_ASSERTIONS","-g","-pthread",
        "-I",str(root/"tests/support/gps-control"),"-I",str(root/"tests/support/map-app"),"-I",str(root/"tests/support/storage-task"),"-I",str(root/"tests/support/usb"),
        "-I",str(root/"tests/support"),"-I",str(root/"application"),"-I",str(root/"third_party/galuchat/include"),
        *[str(root/p) for p in sources],"-Wl,--wrap=fclose","-o",str(binary)],check=True,timeout=120)
    subprocess.run([str(binary)],check=True,timeout=15)
