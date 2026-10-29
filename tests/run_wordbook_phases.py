"""Exercise 32 TT references, TM batch boundaries and one shared Reader buffer."""
import os
import struct
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]


def mb(value):
    if value < 252:
        return bytes([value])
    for count, base in enumerate((252, 508, 66044, 16843260), 1):
        if value - base < 1 << (count * 8):
            return bytes([256 - count]) + (value - base).to_bytes(count, 'big')
    raise ValueError('fixture integer too large')


def packed(fields):
    bits = ''.join(f'{value:0{width}b}' for value, width in fields)
    bits += '0' * (-len(bits) % 8)
    return int(bits, 2).to_bytes(len(bits) // 8, 'big')


def chunk(tag, data):
    prefix = tag.encode('ascii') + mb(len(data))
    return prefix + data, len(prefix)


def fixture(folder, tokens):
    # One valid address at the supported maximum depth. Each component has its
    # own TT page; each fragment has its own TM page. All but the last component
    # contain one fragment. The last covers the rest, forcing TM batch overflow.
    components = 32
    token_bits = (tokens - 1).bit_length()
    book, _ = chunk('GW00', b'GisWordBook/0'.ljust(16, b'\0') +
                    mb(26) + b'{"token_encoding":"utf-8"}')
    tm = mb(tokens) + mb(tokens)
    tm_entries = []
    for token in range(tokens):
        tm += bytes([0, 1, 1, 1])
        tm_entries.append((token, 1, 1, len(tm)))
        tm += bytes([65 + token % 26])
    data, prefix = chunk('TM00', tm)
    tm_start = len(book) + prefix
    book += data
    tt = mb(components) + mb(1) + mb(components) + mb(token_bits)
    tt_entries = []
    expected = []
    for component in range(components):
        ids = [component] if component < components - 1 else list(range(component, tokens))
        expected.append(''.join(chr(65 + token % 26) for token in ids))
        fields = [(byte, 8) for byte in mb(2 * len(ids) + 1)]
        for token in ids:
            fields += [(1, 2), (4 + token_bits, 8), (1, 4), (token, token_bits),
                       (2, 2), (4, 8), (1, 4)]
        fields += [(2, 2), (4, 8), (0, 4)]
        stream = packed(fields)
        tt_entries.append((component, len(tt)))
        tt += bytes([0]) + mb(len(ids)) + mb(1) + mb(len(stream)) + stream
    data, prefix = chunk('TT00', tt)
    tt_start = len(book) + prefix
    book += data
    fields = []
    # Put the long component first in the TI path, while TT remains sorted by
    # token count. The bounded display path must exercise all TM page batches.
    path_codes = [components - 1] + list(range(components - 1))
    expected = expected[-1:] + expected[:-1]
    for code in path_codes[:-1]:
        fields += [(code, 5), (1, 8), (0, 8)]
    fields += [(1, 8), (path_codes[-1], 5)]
    stream = packed(fields)
    ti_header = mb(components) + mb(1) + mb(5) + mb(1) + mb(len(stream))
    ti = ti_header + stream
    data, prefix = chunk('TI00', ti)
    ti_start = len(book) + prefix
    book += data
    header = bytearray(128)
    header[:2] = b'ja'
    values = {36: len(book), 48: 128 + 16 + 8 * components + 16 * tokens,
              52: ti_start, 56: len(ti), 60: tt_start, 64: len(tt),
              68: tm_start, 72: len(tm), 76: 1, 80: components, 84: tokens,
              92: 1, 96: components, 100: tokens}
    for offset, value in values.items():
        struct.pack_into('<I', header, offset, value)
    struct.pack_into('<H', header, 88, components)
    header[90:92] = bytes([5, token_bits])
    root = struct.pack('<IIIB3x', 0, 1, ti_start + len(ti_header), 0)
    index = header + root
    for first, offset in tt_entries:
        index += struct.pack('<II', first, tt_start + offset)
    for first, count, width, offset in tm_entries:
        index += struct.pack('<IIII', first, count, width, tm_start + offset)
    assert len(index) == values[48]
    folder.mkdir()
    (folder / 'book.giswordbook').write_bytes(book)
    (folder / 'index.dat').write_bytes(index)
    (folder / 'expected.txt').write_text('\n'.join(expected) + '\n')


def main():
    with tempfile.TemporaryDirectory(prefix='rgp-wordbook-phases-') as tmp:
        directory = Path(tmp)
        for count in (32, 33, 65, 111, 159, 160):
            fixture(directory / str(count), count)
        for backend in ('stdio', 'fatfs'):
            binary = directory / backend
            flags = [] if backend == 'stdio' else [
                '-DARDUINO_ARCH_ESP32', '-DGEOPUTER_TEST_FATFS',
                '-I' + str(root / 'tests/support/reader')]
            subprocess.run(['g++', '-std=c++17', '-O1', '-g', '-no-pie',
                            '-Wall', '-Wextra', '-Werror', '-pedantic',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-D_GLIBCXX_ASSERTIONS', '-DREVERSEGEOPUTER_DIAGNOSTICS=1',
                            *flags, '-I' + str(root / 'application'),
                            '-I' + str(root / 'third_party/galuchat/include'),
                            str(root / 'tests/wordbook_phase_tests.cpp'),
                            str(root / 'application/task/storage/galuchat/DiskIndexedWordBookReader.cpp'),
                            str(root / 'application/task/storage/galuchat/TextEncoding.cpp'),
                            '-o', str(binary)], check=True, timeout=180)
            for count in (32, 33, 65, 111, 159, 160):
                subprocess.run([str(binary), str(directory / str(count)), str(count)],
                               check=True, timeout=30,
                               env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
            print('PASS:', backend, flush=True)


if __name__ == "__main__":
    main()
