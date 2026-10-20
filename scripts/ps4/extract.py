"""Extract only ExeFS from a local update, without expanding the entire NSZ.

Keys are read through SSBU_PROD_KEYS / SSBU_TITLE_KEYS by the original reader.
Output must be a new ignored build_ps4/ child. No keys are copied or printed.
NCZ format: https://github.com/nicoboss/nsz/blob/master/docs/formats.md
"""
import argparse
import io
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/extract'))
import romfs_extract as rx
from nsz_to_nsp import Sections
import zstandard


def exact(source, size):
    data = source.read(size)
    if len(data) != size:
        raise ValueError('Truncated package or compressed entry')
    return data


class Entry(io.RawIOBase):
    """Independent bounded cursor; the enclosing package owns the file."""
    def __init__(self, source, start, size):
        self.source, self.start, self.size, self.pos = source, start, size, 0

    def readable(self):
        return True

    def read(self, size=-1):
        size = min(self.size-self.pos, size if size >= 0 else self.size)
        self.source.seek(self.start+self.pos)
        data = self.source.read(size)
        self.pos += len(data)
        return data


class Ncz:
    """Read original encrypted NCA ranges; retain at most a bounded prefix.

    Solid NCZ needs sequential decompression up to the requested offset. Block
    NCZ reads only the necessary blocks. Neither mode writes expanded RomFS.
    """
    def __init__(self, source, start, size, prefix_limit=512 << 20):
        self.source, self.start, self.size = source, start, size
        source.seek(start)
        self.header = exact(source, 0x4000)
        magic, count = struct.unpack('<8sQ', exact(source, 16))
        if magic != b'NCZSECTN' or not 1 <= count <= 65536 or 0x4010+count*64+8 > size:
            raise ValueError('Invalid NCZ sections')
        sections = []
        for _ in range(count):
            off, length, crypto, _ = struct.unpack('<QQQQ', exact(source, 32))
            key, ctr = exact(source, 16), exact(source, 16)
            if off < 0x4000 or not length or crypto not in (1, 3, 4):
                raise ValueError('Unsupported NCZ section')
            sections.append((off, length, crypto, key, ctr))
        self.sections = Sections(sections)
        self.total = max(off+length for off, length, *_ in sections)
        self.prefix_limit, self.prefix = prefix_limit, bytearray()
        self.prefix_start = self.stream_position = 0
        self.blocks = None
        marker = source.tell()
        if exact(source, 8) == b'NCZBLOCK':
            version, kind, _, exponent, count, total = struct.unpack('<BBBBIQ', exact(source, 16))
            if version not in (1, 2) or kind != 1 or not 14 <= exponent <= 26:
                raise ValueError('Unsupported NCZ block format')
            self.block_size = 1 << exponent
            if total != self.total-0x4000 or count != (total+self.block_size-1)//self.block_size:
                raise ValueError('Invalid NCZ block dimensions')
            sizes = struct.unpack('<%dI' % count, exact(source, count*4))
            position = source.tell()
            self.blocks = []
            for i, compressed in enumerate(sizes):
                wanted = min(self.block_size, total-i*self.block_size)
                if not 0 < compressed <= wanted or position+compressed > start+size:
                    raise ValueError('Invalid NCZ block size')
                self.blocks.append((position, compressed, wanted))
                position += compressed
            self.cached_index, self.cached_block = -1, b''
        else:
            self.stream_start = marker
            self.stream = zstandard.ZstdDecompressor().stream_reader(
                Entry(source, marker, start+size-marker))

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset+size > self.total:
            raise ValueError('NCZ read outside original NCA')
        result = b''
        if offset < 0x4000:
            result = self.header[offset:min(offset+size, 0x4000)]
            offset += len(result)
            size -= len(result)
        if not size:
            return result
        begin, end = offset-0x4000, offset+size-0x4000
        if self.blocks is None:
            if size > self.prefix_limit:
                raise ValueError('NCZ request exceeds bounded cache')
            if begin < self.prefix_start:
                self.stream.close()
                self.stream = zstandard.ZstdDecompressor().stream_reader(Entry(
                    self.source, self.stream_start, self.start+self.size-self.stream_start))
                self.prefix.clear()
                self.prefix_start = self.stream_position = 0
            # Updates can place ExeFS after RomFS. Stream past it, discarding
            # unrequested bytes instead of retaining multi-gigabyte prefixes.
            while self.stream_position < begin:
                data = self.stream.read(min(1 << 20, begin-self.stream_position))
                if not data:
                    raise ValueError('Truncated NCZ stream')
                self.stream_position += len(data)
                self.prefix.clear()
                self.prefix_start = self.stream_position
            while self.stream_position < end:
                data = self.stream.read(min(1 << 20, end-self.stream_position))
                if not data:
                    raise ValueError('Truncated NCZ stream')
                self.stream_position += len(data)
                self.prefix.extend(data)
            if len(self.prefix) > self.prefix_limit:
                trim = len(self.prefix)-self.prefix_limit
                del self.prefix[:trim]
                self.prefix_start += trim
            plain = bytes(self.prefix[begin-self.prefix_start:end-self.prefix_start])
        else:
            parts = []
            while begin < end:
                index, within = divmod(begin, self.block_size)
                if index != self.cached_index:
                    position, compressed, wanted = self.blocks[index]
                    self.source.seek(position)
                    raw = exact(self.source, compressed)
                    self.cached_block = raw if compressed == wanted else zstandard.ZstdDecompressor().decompress(raw, max_output_size=wanted)
                    if len(self.cached_block) != wanted:
                        raise ValueError('Wrong NCZ block length')
                    self.cached_index = index
                part = self.cached_block[within:within+min(end-begin, self.block_size-within)]
                parts.append(part)
                begin += len(part)
            plain = b''.join(parts)
        return result+self.sections.encrypt(offset, plain)


class Nsz(rx.Nsp):
    def __init__(self, path):
        super().__init__(path)
        self.compressed = {}
        for name, (offset, size) in list(self.files.items()):
            if name.endswith('.ncz'):
                reader = Ncz(self.f, offset, size)
                alias = name[:-4]+'.nca'
                self.files[alias] = (offset, reader.total)
                self.compressed[alias] = reader
                del self.files[name]

    def read(self, name, offset, size):
        if name in self.compressed:
            return self.compressed[name].read(offset, size)
        return super().read(name, offset, size)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--update', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    out, allowed = args.out.resolve(), (ROOT/'build_ps4').resolve()
    if not out.is_relative_to(allowed) or out == allowed or out.exists():
        parser.error('--out must be a new dedicated child of build_ps4/')
    package = Nsz(args.update) if args.update.suffix.lower() == '.nsz' else rx.open_package(args.update)
    nca = rx.Nca(package)
    from dump_folder import extract_exefs
    out.mkdir(parents=True)
    extract_exefs(nca, out/'exefs')
    for name in ('main', 'sdk'):
        subprocess.run([sys.executable, str(ROOT/'tools/extract/nso2elf.py'), str(out/'exefs'/name), str(out/(name+'.elf'))], check=True)
    print('Private ExeFS and verified ELF images:', out)


if __name__ == '__main__':
    main()
