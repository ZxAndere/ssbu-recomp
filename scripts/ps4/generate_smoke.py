"""Generate original ARM64 fixtures and lift them with the upstream recomp.py.

No game data is used. The binary and translated C stay in build_ps4/smoke.
"""
import importlib.util
import os
import re
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
BASE = 0x7100000000


def fixture():
    data = bytearray(0x3020)
    text_size = 0x60
    programs = {
        0x20: [0xf9400001, 0x91001421, 0xf9000401, 0xd2800002, 0x91000442,
               0xf100145f, 0x54ffffc3, 0x8b020020, 0xd65f03c0],
        0x44: [0xd2809a40, 0xd65f03c0],
        0x4c: [0xd4000001, 0xd65f03c0],
        0x54: [0xf9400000, 0xd65f03c0],
        0x5c: [0x14000000],
    }
    header = b'\x7fELF\2\1\1' + bytes(9)
    header += struct.pack('<HHIQQQIHHHHHH', 3, 183, 1, 0x20, 64, 0, 0, 64, 56, 4, 64, 0, 0)
    data[:64] = header
    ph = [(1,5,0x1000,0,text_size,text_size), (1,4,0x2000,0x1000,0x300,0x300),
          (1,6,0x3000,0x2000,0x20,0x4000), (2,4,0x2040,0x1040,0xb0,0xb0)]
    for i,(kind,flags,offset,address,filesz,memsz) in enumerate(ph):
        struct.pack_into('<IIQQQQQQ',data,64+i*56,kind,flags,offset,address,address,filesz,memsz,0x1000 if kind==1 else 8)
    struct.pack_into('<I',data,0x1004,0x1000)
    data[0x2000:0x2004] = b'MOD0'
    struct.pack_into('<i',data,0x2004,0x40)
    tags = [(7,0x1240),(8,24),(9,24),(23,0x1258),(2,0),(6,0x1200),(5,0x1218),(10,1),(11,24),(20,7),(0,0)]
    for i,(tag,value) in enumerate(tags):
        struct.pack_into('<QQ',data,0x2040+i*16,tag,value)
    struct.pack_into('<QQq',data,0x2240,0x2010,0x403,0x20)
    struct.pack_into('<Q',data,0x3000,7)
    for offset,instructions in programs.items():
        for i,instruction in enumerate(instructions):
            struct.pack_into('<I',data,0x1000+offset+i*4,instruction)
    return bytes(data), programs


def fingerprint(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64)-1)
    return value


def generate():
    directory = ROOT / 'build_ps4/smoke'
    directory.mkdir(parents=True, exist_ok=True)
    data,programs = fixture()
    (directory/'main.elf').write_bytes(data)
    os.environ['RECOMP_BIN'] = str(directory)
    spec = importlib.util.spec_from_file_location('recomp',ROOT/'tools/lifter/recomp.py')
    recomp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recomp)
    env = recomp.Env()
    env.starts = [BASE+offset for offset in programs]
    env.imports = set()
    parts = ['#include "guest.h"']
    parts += ['void f_%x(Ctx *c);' % entry for entry in env.starts]
    for entry in env.starts:
        source,_,bad = recomp.lift_function(env,entry)
        if bad:
            raise RuntimeError(f'Untranslated synthetic instructions: {bad}')
        parts.append(source)
    parts.append('static const FnEntry smoke_functions[] = {')
    parts += ['{0x%xULL,f_%x},' % (entry,entry) for entry in env.starts]
    parts += ['};', 'const SpGuestProgram sp_smoke_program = {smoke_functions,%d,NULL,0,0x%xULL};' % (len(programs),fingerprint(data))]
    source=re.sub(r'\bf_([0-9a-f]+)',r'sp_smoke_f_\1','\n'.join(parts)+'\n')
    source=re.sub(r'(L_([0-9a-f]+):)',lambda m:m[1]+' SP_GUEST_POLL(0x'+m[2]+'ULL);',source)
    (directory/'smoke_generated.c').write_text(source,encoding='utf-8')
    print('Upstream lifter: 5 original ARM64 functions -> translated C; 0 untranslated instructions')
    return directory


if __name__ == '__main__':
    generate()
