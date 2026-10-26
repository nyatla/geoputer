"""Exercise the vendored keyboard implementation with fake GPIO/I2C and an allocation probe."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
sources = root / 'third_party/M5Cardputer/src'
with tempfile.TemporaryDirectory(prefix='reversegeoputer-keyboard-') as directory:
    for name, extra in [
        ('keyboard_reader_tests', []),
        ('keyboard_capacity_tests', [
            sources/'utility/Keyboard/Keyboard.cpp',
            sources/'utility/Keyboard/KeyboardReader/IOMatrix.cpp',
            sources/'utility/Keyboard/KeyboardReader/TCA8418.cpp',
        ]),
    ]:
        binary = Path(directory)/name
        subprocess.run([
            'g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic',
            '-funsigned-char',  # Match ESP32's char representation for upstream key codes.
            '-Wno-unused-variable',  # Existing upstream getKey() local; unrelated to capacity.
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g', '-no-pie',
            '-include', str(root/'tests/support/keyboard/utility/Adafruit_TCA8418/Adafruit_TCA8418.h'),
            '-I', str(root/'tests/support/keyboard'), '-I', str(root/'application'), '-I', str(sources),
            str(root/'tests'/f'{name}.cpp'), str(root/'application/hardware/AdvKeyboardReader.cpp'),
            *map(str, extra), '-o', str(binary),
        ], check=True, timeout=60)
        subprocess.run([str(binary)], check=True, timeout=10)
