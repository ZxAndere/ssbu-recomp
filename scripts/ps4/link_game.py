"""Stage locally generated lifter C for static Orbis linking, without changing it.

Only called when --game-generated and --game-image are explicitly supplied.
Generated objects and game data never enter the public source tree.
"""
from pathlib import Path
import shutil
import struct
import re


def image_fingerprint(path):
    value=14695981039346656037
    with Path(path).open('rb') as source:
        for chunk in iter(lambda:source.read(1024*1024),b''):
            for byte in chunk: value=((value^byte)*1099511628211)&((1<<64)-1)
    return value


def stage(generated,image,build,entry=0):
    generated,image=Path(generated).resolve(),Path(image).resolve()
    if not (generated/'table.c').is_file() or not (generated/'imports.c').is_file():
        raise RuntimeError('Expected a complete upstream lift directory with table.c and imports.c')
    with image.open('rb') as source: header=source.read(64)
    if len(header)!=64 or header[:6]!=b'\x7fELF\2\1' or struct.unpack_from('<H',header,18)[0]!=183:
        raise RuntimeError('--game-image must be the original AArch64 main.elf used for this lift')
    destination=build/'local-generated'
    destination.mkdir(parents=True,exist_ok=True)
    sources=[]
    # Use the updated runtime memory boundary, never an old copied runtime.h.
    for path in sorted(generated.iterdir()):
        if path.suffix not in ('.c','.h') or path.name in ('runtime.h','neon.h','fpexact.h'): continue
        if not path.is_file(): continue
        target=destination/path.name
        if target.suffix=='.c':
            source=path.read_text(encoding='utf-8')
            if 'SP_GUEST_POLL' not in source:
                source=re.sub(r'(L_([0-9a-f]+):)',lambda m:m[1]+' SP_GUEST_POLL(0x'+m[2]+'ULL);',source)
            target.write_text(source,encoding='utf-8')
        else: shutil.copy2(path,target)
        if target.suffix=='.c': sources.append(target)
    fingerprint=image_fingerprint(image)
    metadata=build/'local_game_program.c'
    metadata.write_text('''#include "guest.h"
const SpGuestProgram *sp_linked_game_program(void) {
    static SpGuestProgram program;
    program.functions=fn_table; program.function_count=(size_t)fn_table_count;
    program.symbols=dyn_syms; program.symbol_count=(size_t)dyn_sym_count;
    program.image_fingerprint=0x%xULL;
    return &program;
}
uint64_t sp_linked_game_entry(void) { return 0x%xULL; }
'''%(fingerprint,entry),encoding='utf-8')
    return sources,metadata
