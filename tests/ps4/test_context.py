"""Execute the actual Orbis SysV context-switch instructions in Unicorn x86-64."""
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'build_ps4/python-deps'))


class ContextTests(unittest.TestCase):
    def test_sysv_context_preserves_registers_and_fp_control(self):
        if importlib.util.find_spec('unicorn') is None: self.skipTest('unicorn not installed')
        obj=ROOT/'build_ps4/ps4/context_ps4.o'
        settings=ROOT/'build_ps4/toolchain.json'
        if not obj.is_file() or not settings.is_file(): self.skipTest('build the PS4 target first')
        from unicorn import Uc,UC_ARCH_X86,UC_MODE_64
        from unicorn.x86_const import UC_X86_REG_RDI,UC_X86_REG_RSI,UC_X86_REG_RSP,UC_X86_REG_RBX,UC_X86_REG_RBP,UC_X86_REG_R12,UC_X86_REG_R13,UC_X86_REG_R14,UC_X86_REG_R15,UC_X86_REG_MXCSR,UC_X86_REG_FPCW
        tool=Path(json.loads(settings.read_text())['llvm_bin'])/'llvm-objcopy.exe'
        with tempfile.TemporaryDirectory(dir=ROOT/'build_ps4') as folder:
            raw=Path(folder)/'context.bin'
            subprocess.run([str(tool),'--only-section=.text','-O','binary',str(obj),str(raw)],check=True,capture_output=True)
            code=raw.read_bytes()
        emulator=Uc(UC_ARCH_X86,UC_MODE_64)
        for address in (0x100000,0x200000,0x300000): emulator.mem_map(address,0x10000)
        emulator.mem_write(0x100000,code)
        saved=[0x301000,11,12,13,14,15,16]
        restored=[0x303000,21,22,23,24,25,26]
        registers=[UC_X86_REG_RSP,UC_X86_REG_RBX,UC_X86_REG_RBP,UC_X86_REG_R12,UC_X86_REG_R13,UC_X86_REG_R14,UC_X86_REG_R15]
        for register,value in zip(registers,saved): emulator.reg_write(register,value)
        emulator.reg_write(UC_X86_REG_MXCSR,0x1f80);emulator.reg_write(UC_X86_REG_FPCW,0x037f)
        emulator.mem_write(0x200100,struct.pack('<7QIHH',*restored,0x3f80,0x077f,0))
        emulator.mem_write(restored[0],struct.pack('<Q',0x100fff))
        emulator.reg_write(UC_X86_REG_RDI,0x200000);emulator.reg_write(UC_X86_REG_RSI,0x200100)
        emulator.emu_start(0x100000,0x100fff,count=100)
        context=struct.unpack('<7QIHH',emulator.mem_read(0x200000,64))
        self.assertEqual(context[:7],tuple(saved))
        self.assertEqual(context[7:9],(0x1f80,0x037f))
        restored[0]+=8
        self.assertEqual([emulator.reg_read(register) for register in registers],restored)
        self.assertEqual(emulator.reg_read(UC_X86_REG_MXCSR),0x3f80)
        self.assertEqual(emulator.reg_read(UC_X86_REG_FPCW),0x077f)


if __name__=='__main__': unittest.main()
