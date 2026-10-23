"""Generate v1 indexes from real/synthetic sources and verify format + rollback."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import struct
import zlib

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('mapset', type=Path)
parser.add_argument('wordbook', type=Path)
args = parser.parse_args()


class Bits:
    def __init__(self, data, offset=0):
        self.data, self.bit = data, offset*8

    def read(self, count):
        assert self.bit+count <= len(self.data)*8
        value = 0
        for _ in range(count):
            value = value*2 + ((self.data[self.bit//8] >> (7-self.bit%8)) & 1)
            self.bit += 1
        return value

    def mb(self):
        head = self.read(8)
        if head < 252:
            return head
        count = 256-head
        bases = {1:252, 2:508, 3:66044, 4:16843260}
        return bases[count]+self.read(count*8)

    def skip(self, bits):
        assert self.bit+bits <= len(self.data)*8
        self.bit += bits


def mb(value):
    if value < 252:
        return bytes([value])
    for count, base in enumerate((252,508,66044,16843260),1):
        if value-base < 1 << (8*count):
            return bytes([256-count])+(value-base).to_bytes(count,'big')
    raise AssertionError('MBUInt out of range')


def packed(fields):
    text = ''.join(f'{value:0{count}b}' for value,count in fields)
    text += '0'*(-len(text)%8)
    return int(text,2).to_bytes(len(text)//8,'big')


def chunk(tag, payload):
    return tag.encode('ascii')+mb(len(payload))+payload


def source_chunk(data, offset, tag):
    assert data[offset:offset+4] == tag
    reader = Bits(data, offset+4)
    length = reader.mb()
    start = reader.bit//8
    assert start+length <= len(data)
    return start, start+length


def check_index(directory, expected_blocks=None):
    b = (directory / '_mapindex.dat').read_bytes()
    u = lambda offset: struct.unpack_from('<I', b, offset)[0]
    assert b[:8] == b'RGPMIDX\0' and u(16) == len(b)
    assert zlib.crc32(b[:-4]) == u(len(b)-4)
    map_data = (directory/'map.glc').read_bytes()
    book_data = (directory/'book.giswordbook').read_bytes()
    glch_start, glch_end = source_chunk(map_data, 0, b'GLCH')
    assert u(20) == len(map_data) and u(28) == zlib.crc32(b'map.glc')
    assert u(32) == zlib.crc32(map_data[:glch_end])
    position = 64
    group_count = 0
    for _ in range(u(36)):
        groups = u(position+36)
        chunk_start, chunk_bytes, first_block, block_count = (u(position+n) for n in (20,24,28,32))
        payload_start, chunk_end = source_chunk(map_data, chunk_start, b'GI01')
        reader = Bits(map_data, payload_start)
        width, height, exponent = reader.mb(), reader.mb(), reader.read(8)
        assert [width,height,1 << exponent] == [u(position+n) for n in (8,12,16)]
        assert chunk_end == chunk_start+chunk_bytes and reader.bit//8 == first_block
        assert block_count == ((width+(1 << exponent)-1) >> exponent)*((height+(1 << exponent)-1) >> exponent)
        if expected_blocks is not None:
            expected = []
            offset, number, total, count = first_block, 0, 0, 0
            for block in expected_blocks:
                total += len(block)
                count += 1
                if total >= 65536:
                    expected.append((number,count,offset,total))
                    offset += total
                    number += count
                    total = count = 0
            if count:
                expected.append((number,count,offset,total))
            if chunk_bytes < 16384:
                expected = []
            actual = [struct.unpack_from('<IIII',b,position+40+i*16) for i in range(groups)]
            assert actual == expected
        group_count += groups
        position += 40+16*groups
    assert position == 64+u(60)
    wb = position
    assert b[wb:wb+32].split(b'\0')[0] == b'ja'
    ti_count, tt_count, tm_count = (u(wb+n) for n in (92,96,100))
    assert wb+128+ti_count*16+tt_count*8+tm_count*16 == len(b)-4
    # Check every Wordbook entry against a separate bit parser of the source.
    _, gw_end = source_chunk(book_data,0,b'GW00')
    assert u(wb+36) == len(book_data) and u(wb+32) == zlib.crc32(b'book.giswordbook')
    assert u(wb+44) == zlib.crc32(book_data[:gw_end])
    cursor = gw_end
    for tag, info in ((b'TM00',68),(b'TT00',60),(b'TI00',52)):
        start, end = source_chunk(book_data,cursor,tag)
        assert (u(wb+info),u(wb+info+4)) == (start,end-start)
        cursor = end
    assert cursor == len(book_data)
    ti = Bits(book_data,u(wb+52))
    depth, records, code_bits, roots, stream_bytes = [ti.mb() for _ in range(5)]
    assert (records,roots,code_bits,depth) == (u(wb+76),ti_count,b[wb+90],struct.unpack_from('<H',b,wb+88)[0])
    assert ti.bit//8+stream_bytes == u(wb+52)+u(wb+56)
    def node(level):
        if level == depth-1:
            leaves = ti.mb()
            ti.skip(leaves*code_bits)
            return leaves
        ti.read(code_bits)
        leaves, bits = ti.mb(), ti.mb()
        if bits:
            ti.skip(bits)
        else:
            count = 0
            while count < leaves:
                count += node(level+1)
            assert count == leaves
        return leaves
    first = 0
    position = wb+128
    for i in range(roots):
        bit = ti.bit
        leaves = node(0)
        entry = struct.unpack_from('<IIIB3x',b,position+i*16)
        assert entry == (first,leaves,bit//8,bit%8)
        first += leaves
    assert first == records
    position += roots*16
    tt = Bits(book_data,u(wb+60))
    components, page_size, pages, token_bits = [tt.mb() for _ in range(4)]
    assert (components,pages,token_bits) == (u(wb+80),tt_count,b[wb+91])
    code = 0
    for i in range(pages):
        assert struct.unpack_from('<II',b,position+i*8) == (code,tt.bit//8)
        assert tt.read(8) == 0
        record_tokens, count, stream = tt.mb(), tt.mb(), tt.mb()
        assert 0 < count <= page_size
        tt.skip(stream*8)
        code += count
    assert code == components and tt.bit//8 == u(wb+60)+u(wb+64)
    position += pages*8
    tm = Bits(book_data,u(wb+68))
    tokens, pages = tm.mb(), tm.mb()
    assert (tokens,pages) == (u(wb+84),tm_count)
    token = 0
    for i in range(pages):
        assert tm.read(8) == 0
        size, count, stream = tm.mb(), tm.mb(), tm.mb()
        assert struct.unpack_from('<IIII',b,position+i*16) == (token,count,size,tm.bit//8)
        assert stream == size*count
        tm.skip(stream*8)
        token += count
    assert token == tokens and tm.bit//8 == u(wb+68)+u(wb+72)
    print(f'Independent format/CRC: {len(b)} B, {u(36)} chunks, {group_count} groups, '
          f'TI/TT/TM={ti_count}/{tt_count}/{tm_count}')
    return b, wb

with tempfile.TemporaryDirectory(prefix='reversegeoputer-index-') as tmp:
    directory = Path(tmp)
    binary = directory/'index-tests'
    subprocess.run(['g++','-std=c++17','-O2','-g','-Wall','-Wextra','-Werror','-pedantic',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-D_GLIBCXX_ASSERTIONS',
                    '-I',str(root/'application'),'-I',str(root/'third_party/galuchat/include'),
                    str(root/'tests/dataset_index_tests.cpp'),
                    str(root/'tests/wordbook_index_tests.cpp'),
                    str(root/'application/task/storage/galuchat/DiskIndexedWordBookReader.cpp'),
                    str(root/'application/task/storage/dataset/DatasetIndex.cpp'),
                    str(root/'application/task/storage/dataset/DatasetMetadata.cpp'),
                    str(root/'application/Settings.cpp'),
                    str(root/'application/task/storage/galuchat/TextEncoding.cpp'),
                    '-Wl,--wrap=fwrite,--wrap=fsync,--wrap=fclose,--wrap=rename',
                    '-o',str(binary)],check=True,timeout=120)
    target = directory/'real'
    subprocess.run([str(binary),str(target),str(args.mapset.resolve()),str(args.wordbook.resolve())],check=True,timeout=120)
    data, wb = check_index(target)
    # CRC-correct malformed tables must fail structural validation too.
    corrupt_path = directory/'corrupt.dat'
    for offset, value in ((36,0xffffffff),(60,0xffffffff),(64+36,0xffffffff),
                          (wb+48,128),(wb+92,0),(wb+128+12,8),(wb+112,1),
                          (wb+128+16,0)):
        corrupt = bytearray(data)
        struct.pack_into('<I',corrupt,offset,value)
        struct.pack_into('<I',corrupt,len(corrupt)-4,zlib.crc32(corrupt[:-4]))
        corrupt_path.write_bytes(corrupt)
        result = subprocess.run([str(binary),'--validate',str(corrupt_path)])
        assert result.returncode == 1, (offset,value)

    # Synthetic sources cover depth=1/non-byte TI roots, payload_bits=0,
    # RAW/RAWS/LZSS boundaries, the 16KiB threshold, and 64KiB grouping.
    raw = bytes([0,0x28,3])
    raws = bytes([0x10,2,0x28,3])
    lz = packed([(0,1),(1,4),(0x28,8),(3,8)])
    lzss = bytes([0x20])+mb(len(lz))+lz
    flat_root = [(1,8),(0,3),(2,8),(1,3),(2,3),(1,8),(3,3)]
    deep_root = [(0,3),(3,8),(0,8),
                 (1,3),(2,8),(0,8),(2,8),(0,3),(1,3),
                 (2,3),(1,8),(11,8),(1,8),(2,3),
                 (3,3),(1,8),(0,8),(4,3),(1,8),(11,8),(1,8),(3,3)]
    for label, width, height, blocks, depth, roots in (
            ('mixed',4,4,[raw,raws,lzss,raw],1,flat_root),
            ('deep',4,4,[raw]*4,3,deep_root),
            ('tm-same',4,4,[raw]*4,1,flat_root),
            ('below16k',2,10914,[raw]*5457,1,flat_root),
            ('exact16k',2,10914,[raws]+[raw]*5456,1,flat_root),
            ('above16k',2,10916,[raw]*5458,1,flat_root),
            ('groups64k',512,256,[raw]*32768,1,flat_root)):
        glch = b'WGSMapSet/3'.ljust(16,b'\0')+bytes([1,1,0,1,0,0])
        image = mb(width)+mb(height)+bytes([1])+b''.join(blocks)
        source_map = directory/(label+'.glc')
        source_map.write_bytes(chunk('GLCH',glch)+chunk('LAYO',b'abc')+chunk('GI01',image))
        gw_meta = b'{"token_encoding":"utf-8"}'
        gw = b'GisWordBook/0'.ljust(16,b'\0')+mb(len(gw_meta))+gw_meta
        tm = (bytes([2,2,0,1,1,1])+b'A'+bytes([0,1,1,1])+b'B' if label=='tm-same'
              else bytes([1,1,0,1,1,1])+b'A')
        stream = packed([(2,8),(0,2),(16,8),(1,15),(0,1),(2,2),(64,8)]+[(v,4) for _ in range(8) for v in (1,0)])
        tt = bytes([8,8,1,1,0,1,8])+mb(len(stream))+stream
        ti_stream = packed(roots)
        ti = bytes([depth,4,3,3 if depth==1 else 2])+mb(len(ti_stream))+ti_stream
        source_book = directory/(label+'.giswordbook')
        source_book.write_bytes(chunk('GW00',gw)+chunk('TM00',tm)+chunk('TT00',tt)+chunk('TI00',ti))
        target = directory/label
        subprocess.run([str(binary),str(target),str(source_map),str(source_book)],check=True,timeout=30)
        check_index(target,blocks)
