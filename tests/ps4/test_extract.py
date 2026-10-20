"""Synthetic NCZ fixtures only. No console keys or game data required."""
import io
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts/ps4'))
try:
    from extract import Ncz, zstandard
except ImportError:
    Ncz = None


@unittest.skipIf(Ncz is None, 'zstandard and pycryptodome are required')
class NczTests(unittest.TestCase):
    def fixture(self, block=False, crypto=1, body=None):
        body = body if body is not None else bytes(range(256))*300
        head = b'H'*0x4000
        sections = struct.pack('<8sQ', b'NCZSECTN', 1)
        sections += struct.pack('<QQQQ', 0x4000, len(body), crypto, 0)+bytes(32)
        if block:
            parts = [body[i:i+0x4000] for i in range(0, len(body), 0x4000)]
            chunks = [min((p, zstandard.ZstdCompressor().compress(p)), key=len) for p in parts]
            data = struct.pack('<8sBBBBIQ', b'NCZBLOCK', 2, 1, 0, 14, len(parts), len(body))
            data += struct.pack('<%dI' % len(parts), *map(len, chunks))+b''.join(chunks)
        else:
            data = zstandard.ZstdCompressor().compress(body)
        return head+sections+data, head+body

    def test_solid_forward_overlap_and_rewind(self):
        compressed, expected = self.fixture()
        ncz = Ncz(io.BytesIO(compressed), 0, len(compressed), prefix_limit=2048)
        for offset, size in [(0, 0x4000), (0x10000, 32), (0x10010, 64), (0x10000, 16), (0x5000, 99), (0x5001, 7)]:
            self.assertEqual(ncz.read(offset, size), expected[offset:offset+size])
        self.assertLessEqual(len(ncz.prefix), 2048)

    def test_blocks_cross_boundary_and_last_block(self):
        compressed, expected = self.fixture(block=True)
        ncz = Ncz(io.BytesIO(compressed), 0, len(compressed))
        for offset, size in [(0x3fff, 100), (0x7ffe, 50), (len(expected)-11, 11), (0x4100, 100)]:
            self.assertEqual(ncz.read(offset, size), expected[offset:offset+size])

    def test_uncompressed_block_and_exact_multiple(self):
        # Stored blocks and an exactly full last block are both valid.
        import random
        body = random.Random(17).randbytes(0x8000)
        compressed, expected = self.fixture(block=True, body=body)
        ncz = Ncz(io.BytesIO(compressed), 0, len(compressed))
        self.assertEqual(ncz.read(0x4000, len(body)), body)

    def test_ctr_unaligned_read(self):
        compressed, _ = self.fixture(crypto=3)
        ncz = Ncz(io.BytesIO(compressed), 0, len(compressed))
        whole = ncz.read(0x4000, 0x1000)
        self.assertEqual(ncz.read(0x4013, 55), whole[19:74])

    def test_invalid_and_truncated(self):
        compressed, expected = self.fixture()
        for broken in [compressed[:100], compressed[:0x4010], compressed[:0x4000]+bytes(80)]:
            with self.assertRaises((ValueError, struct.error)):
                Ncz(io.BytesIO(broken), 0, len(broken))
        ncz = Ncz(io.BytesIO(compressed), 0, len(compressed))
        for offset, size in [(-1, 1), (0, -1), (len(expected), 1)]:
            with self.assertRaises(ValueError):
                ncz.read(offset, size)


if __name__ == '__main__':
    unittest.main()
