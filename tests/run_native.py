"""Compile and run queue/UI contracts and GPS reception tests using host adapters."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="geoputer-native-") as directory:
    binary = Path(directory) / "native-tests"
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-D_GLIBCXX_ASSERTIONS", "-g", "-pthread",
        "-I", str(root / "tests/support"), "-I", str(root / "application"),
        str(root / "tests/native_tests.cpp"),
        str(root / "tests/logging_tests.cpp"),
        str(root / "tests/log_ring_tests.cpp"),
        str(root / "tests/log_reader_tests.cpp"),
        str(root / "application/task/storage/log/LogReader.cpp"),
        str(root / "application/task/storage/log/LogWriter.cpp"),
        str(root / "tests/gps_tests.cpp"),
        str(root / "tests/settings_tests.cpp"),
        str(root / "tests/settings_language_tests.cpp"),
        str(root / "tests/place_tests.cpp"),
        str(root / "tests/wordbook_cache_tests.cpp"),
        str(root / "tests/key_binding_tests.cpp"),
        str(root / "tests/live_layout_tests.cpp"),
        str(root / "tests/boot_tests.cpp"),
        str(root / "tests/clock_tests.cpp"),
        str(root / "tests/font_tests.cpp"),
        str(root / "tests/license_tests.cpp"),
        str(root / "application/task/storage/galuchat/TextEncoding.cpp"),
        str(root / "application/Settings.cpp"),
        str(root / "application/task/storage/SettingsStore.cpp"),
        str(root / "application/task/ui/UiModel.cpp"),
        str(root / "application/task/ui/SettingsText.cpp"),
        str(root / "application/task/ui/LicenseText.cpp"),
        str(root / "application/task/ui/Shinonome.cpp"),
        str(root / "application/task/gps/NmeaParser.cpp"),
        str(root / "application/task/gps/GpsReceiver.cpp"), "-o", str(binary),
    ], check=True, timeout=180)
    subprocess.run([str(binary)], check=True, timeout=10)
