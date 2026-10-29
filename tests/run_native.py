"""Compile and run queue/UI contracts and GPS reception tests using host adapters."""
from pathlib import Path
import subprocess
import tempfile
import os
import sys
from check_export_files import check_exports

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="geoputer-native-") as directory:
    binary = Path(directory) / "native-tests"
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-D_GLIBCXX_ASSERTIONS", "-g", "-pthread",
        "-I", str(root / "tests/support"), "-I", str(root / "application"),
        str(root / "tests/native_tests.cpp"),
        str(root / "tests/storage_reply_tests.cpp"),
        str(root / "tests/logging_tests.cpp"),
        str(root / "tests/log_ring_tests.cpp"),
        str(root / "tests/log_reader_tests.cpp"),
        str(root / "application/task/storage/log/LogReader.cpp"),
        str(root / "application/task/storage/log/LogWriter.cpp"),
        str(root / "tests/gps_tests.cpp"),
        str(root / "tests/map_tests.cpp"),
        str(root / "tests/settings_tests.cpp"),
        str(root / "tests/settings_language_tests.cpp"),
        str(root / "tests/dialog_tests.cpp"),
        str(root / "tests/usb_mode_tests.cpp"),
        str(root / "tests/export_tests.cpp"),
        str(root / "application/task/storage/export/LogExporter.cpp"),
        str(root / "application/BootMode.cpp"),
        str(root / "tests/place_tests.cpp"),
        str(root / "tests/wordbook_cache_tests.cpp"),
        str(root / "tests/key_binding_tests.cpp"),
        str(root / "tests/live_layout_tests.cpp"),
        str(root / "tests/boot_tests.cpp"),
        str(root / "tests/dataset_tests.cpp"),
        str(root / "tests/dataset_borrow_tests.cpp"),
        str(root / "tests/dataset_selection_tests.cpp"),
        str(root / "application/task/storage/dataset/DatasetMetadata.cpp"),
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
    # Exercise the hardware guard with GPIO adapters, independent of USB/GPS mode.
    guard = Path(directory) / "lora-safety-tests"
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic',
                    '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-I', str(root/'tests/support/lora'), '-I', str(root/'application'),
                    str(root/'tests/lora_safety_tests.cpp'), str(root/'application/hardware/LoraSafety.cpp'),
                    '-o', str(guard)], check=True, timeout=30)
    subprocess.run([str(guard)], check=True, timeout=5)
    setup_body = (root/'application/main.cpp').read_text().split('void setup() {', 1)[1].lstrip()
    assert setup_body.startswith('geoputer::holdUnusedLoraInReset();'), "Guard must precede mode selection and peripheral initialization"
    subprocess.run([sys.executable, str(root/'tests/run_keyboard.py')], check=True, timeout=180)
    export_folder=Path(directory)/'export-fixtures'
    subprocess.run([str(binary)], check=True, timeout=15,
                   env=dict(os.environ,GEOPUTER_EXPORT_FIXTURES=str(export_folder)))
    check_exports(export_folder)
    backend=Path(directory)/'usb-backend-tests'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-pthread',
                    '-I',str(root/'tests/support/usb'),'-I',str(root/'tests/support'),'-I',str(root/'application'),
                    str(root/'tests/usb_backend_tests.cpp'),str(root/'application/task/storage/usb/UsbStorage.cpp'),
                    str(root/'application/BootMode.cpp'),'-o',str(backend)],check=True,timeout=60)
    subprocess.run([str(backend)],check=True,timeout=15)

subprocess.run([sys.executable, str(root / "tests/build_metadata_tests.py")], check=True, timeout=60)

subprocess.run([sys.executable,str(root / "tests/run_metadata.py")],check=True,timeout=120)
