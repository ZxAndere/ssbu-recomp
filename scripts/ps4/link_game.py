"""Stage locally generated lifter C for static Orbis linking, without changing it.

Only called when --game-generated and --game-image are explicitly supplied.
Generated objects and game data never enter the public source tree.
"""
from pathlib import Path
import shutil
import struct
import re
import json


def split_large_functions(source, block_instructions=256):
    """Keep the C compiler's control-flow graph bounded for huge lifted funcs.

    All persistent guest state is in Ctx. A chunk returns the next guest PC,
    or zero for RET. Indirect branches outside the function retain tail-call
    behavior. Strictly recognize the upstream plain lifter format only.
    """
    def split(match):
        entry,body=match[1],match[2]
        labels=list(re.finditer(r'(?m)^L_([0-9a-f]+):([^\n]*)',body))
        if len(labels)<=block_instructions: return match[0]
        prefix=body[:labels[0].start()]
        expected=r'\s*c->fn = 0x'+entry+r'ULL;\s*(?:uint64_t jt = 0;\s*)?'
        if not re.fullmatch(expected,prefix): return match[0]
        addresses=[int(label[1],16) for label in labels]
        if any(address!=addresses[0]+i*4 for i,address in enumerate(addresses)): return match[0]
        tail=body[labels[-1].end():].split('L_dispatch:',1)[0]
        chunks=[]
        for index in range(0,len(labels),block_instructions):
            part=labels[index:index+block_instructions]; allowed={label[1] for label in part}
            lines=[]
            for label in part:
                statement=re.sub(r'goto L_([0-9a-f]+);',lambda jump:jump[0] if jump[1] in allowed else 'return 0x'+jump[1]+'ULL;',label[2])
                statement=statement.replace('goto L_dispatch;','{ if (!jt) guest_trap("invalid indirect branch target",jt); return jt; }').replace('return;','return 0;')
                lines.append('L_'+label[1]+':'+statement)
            first=addresses[index]; piece=index//block_instructions
            dispatch=''.join('case 0x%sULL: goto L_%s;\n'%(label[1],label[1]) for label in part)
            suffix=('return 0x%xULL;' % addresses[index+len(part)]) if index+len(part)<len(labels) else tail.replace('return;','return 0;')+'\nreturn 0;'
            chunks.append('static __attribute__((noinline)) uint64_t sp_piece_%s_%d(Ctx *c,uint64_t pc) {\nuint64_t jt=0; (void)jt;\nswitch(pc) {\n%sdefault: guest_trap("invalid translated block entry",pc); return 0;\n}\n%s\n%s\n}\n'%(entry,piece,dispatch,'\n'.join(lines),suffix))
        cases=''.join('case %d: pc=sp_piece_%s_%d(c,pc); break;\n'%(i,entry,i) for i in range(len(chunks)))
        wrapper='void f_%s(Ctx *c) {\nc->fn=0x%sULL; uint64_t pc=0x%sULL;\nwhile(pc) {\nSP_GUEST_POLL(pc);\nif (pc<0x%xULL || pc>=0x%xULL || (pc&3)) { call_indirect(c,pc); return; }\nswitch ((pc-0x%xULL)/%d) {\n%s}\n}\n}\n'%(entry,entry,entry,addresses[0],addresses[-1]+4,addresses[0],block_instructions*4,cases)
        return ''.join(chunks)+wrapper
    return re.sub(r'(?ms)^void f_([0-9a-f]+)\(Ctx \*c\) \{\n(.*?)^}',split,source)


def image_fingerprint(path):
    value=14695981039346656037
    with Path(path).open('rb') as source:
        for chunk in iter(lambda:source.read(1024*1024),b''):
            for byte in chunk: value=((value^byte)*1099511628211)&((1<<64)-1)
    return value


def add_checkpoints(source):
    """Bound loops without turning every ARM instruction into three host blocks.

    Direct branch destinations, indirect dispatch, and periodic labels all
    poll. Every cycle therefore crosses a checkpoint, including a one-label
    branch loop. The budget counts checkpoints, not ARM instruction throughput.
    """
    branches=re.sub(r'(?m)^\s*case 0x[0-9a-f]+(?:ULL)?: goto L_[0-9a-f]+;\s*$','',source)
    targets=set(re.findall(r'goto L_([0-9a-f]+)',branches))
    entries=set(re.findall(r'void f_([0-9a-f]+)\(Ctx \*c\) \{',source))
    counter=0
    def label(match):
        nonlocal counter
        poll=counter%64==0 or match[2] in targets or match[2] in entries
        counter+=1
        return match[1]+(' SP_GUEST_POLL(0x'+match[2]+'ULL);' if poll else '')
    source=re.sub(r'(L_([0-9a-f]+):)',label,source)
    return source.replace('L_dispatch:','L_dispatch: SP_GUEST_POLL(jt);')


def stage(generated,image,build,entry=0,module='main'):
    generated,image=Path(generated).resolve(),Path(image).resolve()
    if not (generated/'table.c').is_file() or not (generated/'imports.c').is_file():
        raise RuntimeError('Expected a complete upstream lift directory with table.c and imports.c')
    manifest_path=generated/'lift-manifest.json'
    if not manifest_path.is_file():
        raise RuntimeError('Missing lift-manifest.json; generate with scripts/ps4/lift.py to bind sources to the input ELF')
    from lift import sha256
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest.get('version')!=1 or manifest.get('image_sha256')!=sha256(image):
        raise RuntimeError('Generated code manifest does not match the input image')
    present={p.name for p in generated.iterdir() if p.suffix in ('.c','.h')}
    if set(manifest.get('generated_files',{}))!=present:
        raise RuntimeError('Generated file set changed after lifting')
    for name,expected in manifest['generated_files'].items():
        if sha256(generated/name)!=expected: raise RuntimeError('Generated source changed after lifting: '+name)
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
                source=split_large_functions(source)
                source=add_checkpoints(source)
            if module=='sdk':
                for name in ('fn_table','fn_table_count','dyn_syms','dyn_sym_count'):
                    source=re.sub(r'\b'+name+r'\b','sp_sdk_'+name,source)
            target.write_text(source,encoding='utf-8')
        else: shutil.copy2(path,target)
        if target.suffix=='.c': sources.append(target)
    fingerprint=image_fingerprint(image)
    metadata=build/'local_game_program.c'
    metadata_source='''#include "guest.h"
const SpGuestProgram *sp_linked_game_program(void) {
    static SpGuestProgram program;
    program.functions=fn_table; program.function_count=(size_t)fn_table_count;
    program.symbols=dyn_syms; program.symbol_count=(size_t)dyn_sym_count;
    program.image_fingerprint=0x%xULL;
    return &program;
}
uint64_t sp_linked_game_entry(void) { return 0x%xULL; }
'''%(fingerprint,entry)
    if module=='sdk':
        metadata_source=metadata_source.replace('sp_linked_game_','sp_linked_sdk_')
        for name in ('fn_table','fn_table_count','dyn_syms','dyn_sym_count'):
            metadata_source=re.sub(r'\b'+name+r'\b','sp_sdk_'+name,metadata_source)
        metadata_source=metadata_source.replace('#include "guest.h"','#include "guest.h"\nextern const FnEntry sp_sdk_fn_table[];\nextern const int sp_sdk_fn_table_count;\nextern const DynSym sp_sdk_dyn_syms[];\nextern const int sp_sdk_dyn_sym_count;')
    metadata.write_text(metadata_source,encoding='utf-8')
    return sources,metadata
