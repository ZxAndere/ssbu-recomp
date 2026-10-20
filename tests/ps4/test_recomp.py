"""Independent ARM64 emulation vs native lifted C, plus adversarial ELF inputs."""
import importlib.util
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'build_ps4/python-deps'))
EXE=ROOT/'build_ps4/host/diagnostic.exe'
IMAGE=ROOT/'build_ps4/smoke/main.elf'


@unittest.skipUnless(EXE.is_file() and IMAGE.is_file(),'build the host target first')
class RecompilerTests(unittest.TestCase):
    def inspect(self,data):
        with tempfile.TemporaryDirectory(dir=ROOT/'build_ps4') as folder:
            image=Path(folder)/'input.elf'
            image.write_bytes(data)
            return subprocess.run([str(EXE),'--guest-inspect',str(image)],capture_output=True,timeout=5).returncode

    def test_valid_guest_elf(self):
        self.assertEqual(self.inspect(IMAGE.read_bytes()),0)

    def test_invalid_headers_and_segments(self):
        original=IMAGE.read_bytes()
        mutations=[original[:n] for n in (0,4,63,128,4096)]
        fields=[(18,'H',62),(32,'Q',(1<<64)-16),(56,'H',65535),
                (64+16,'Q',1<<40),(64+32,'Q',len(original)+1),
                (64+40,'Q',1<<40),(64+56+16,'Q',0), (24,'Q',(1<<64)-1)]
        for offset,kind,value in fields:
            data=bytearray(original);struct.pack_into('<'+kind,data,offset,value);mutations.append(data)
        for i,data in enumerate(mutations):
            with self.subTest(case=i): self.assertEqual(self.inspect(data),1)

    def test_relocation_bounds_and_types(self):
        original=IMAGE.read_bytes()
        for offset,value in [(0x2240,0x20),(0x2240,1<<40),(0x2248,0x999),(0x2250,1<<40)]:
            data=bytearray(original);struct.pack_into('<Q',data,offset,value)
            with self.subTest(offset=offset,value=value): self.assertEqual(self.inspect(data),1)

    def test_initializer_array_bounds(self):
        original=IMAGE.read_bytes()
        # Replace SYMENT (not required by this fixture) and STRSZ (no imports).
        for address,size in [(0x2000,8),(1<<40,8),(0x2000,7),(0x2000,32776)]:
            data=bytearray(original)
            struct.pack_into('<QQ',data,0x2040+7*16,25,address)
            struct.pack_into('<QQ',data,0x2040+8*16,27,size)
            with self.subTest(address=address,size=size):
                self.assertEqual(self.inspect(data),0 if (address,size)==(0x2000,8) else 1)

    def test_lifted_c_matches_unicorn_arm64(self):
        if importlib.util.find_spec('unicorn') is None: self.skipTest('install unicorn in build_ps4/python-deps')
        from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
        from unicorn.arm64_const import UC_ARM64_REG_X0,UC_ARM64_REG_X30
        rng=random.Random(0x505334)
        inputs=[0,1,7,(1<<64)-1,(1<<64)-5]+[rng.getrandbits(64) for _ in range(995)]
        native=subprocess.run([str(EXE),'--guest-batch',str(IMAGE)],input='\n'.join(map(str,inputs))+'\n',
                              text=True,capture_output=True,check=True,timeout=30)
        results=[tuple(map(int,line.split(','))) for line in native.stdout.splitlines()]
        self.assertEqual(len(results),len(inputs))
        image=IMAGE.read_bytes();base=0x7100000000
        emulator=Uc(UC_ARCH_ARM64,UC_MODE_ARM);emulator.mem_map(base,0x10000)
        emulator.mem_write(base,image[0x1000:0x1060])
        for value,native_result,native_memory in results:
            emulator.mem_write(base+0x2000,struct.pack('<QQ',value,0))
            emulator.reg_write(UC_ARM64_REG_X0,base+0x2000)
            emulator.reg_write(UC_ARM64_REG_X30,base+0xff0)
            emulator.emu_start(base+0x20,base+0xff0,count=1000)
            actual=emulator.reg_read(UC_ARM64_REG_X0)
            stored=struct.unpack('<Q',emulator.mem_read(base+0x2008,8))[0]
            self.assertEqual((native_result,native_memory),(actual,stored),f'input {value}')


if __name__=='__main__': unittest.main()
