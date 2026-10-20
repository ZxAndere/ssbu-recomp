"""Lift a bounded direct-call closure of the real module entry for boot probes.

Omitted callees fail explicitly at execution. This is not a complete game lift.
All game-derived sources and reports remain in ignored build_ps4/.
"""
import argparse
import collections
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
from lift import ROOT, write_manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--entry', type=lambda text:int(text, 0))
    parser.add_argument('--entry-symbol', default='nninitStartup')
    parser.add_argument('--base', type=lambda text:int(text,0), default=0x7100000000)
    parser.add_argument('--cut-imports', type=Path, help='Main generated imports.c: SDK exports with host bridges become closure boundaries')
    parser.add_argument('--max-functions', type=int, default=2000)
    parser.add_argument('--initializers', action='store_true', help='Also seed static constructors and nnMain from this image')
    parser.add_argument('--root', action='append', type=lambda text:int(text,0), default=[], help='Additional observed indirect-call target (repeatable)')
    args = parser.parse_args()
    out, allowed = args.out.resolve(), (ROOT/'build_ps4').resolve()
    if out == allowed or not out.is_relative_to(allowed) or out.exists():
        parser.error('--out must be a new dedicated child of build_ps4/')
    if not 1 <= args.max_functions <= 20000:
        parser.error('--max-functions must be 1..20000')
    out.mkdir(parents=True)
    inputs = out/'inputs'; inputs.mkdir()
    image = inputs/'main.elf'; shutil.copy2(args.image, image)
    os.environ['RECOMP_BIN'] = str(inputs)
    if args.base not in (0x7100000000,0x7200000000): parser.error('Unsupported guest module base')
    os.environ['RECOMP_BASE'] = hex(args.base)
    spec = importlib.util.spec_from_file_location('bootstrap_recomp', ROOT/'tools/lifter/recomp.py')
    recomp = importlib.util.module_from_spec(spec); spec.loader.exec_module(recomp)
    if args.entry is None:
        for i in range((recomp.STRTAB-recomp.SYMTAB)//24):
            name, _, _, section, value, _ = struct.unpack_from('<IBBHQQ', recomp.IMG, recomp.FILE_OFF+recomp.SYMTAB+i*24)
            start = recomp.FILE_OFF+recomp.STRTAB+name
            if section and recomp.IMG[start:recomp.IMG.index(b'\0',start)].decode('latin-1') == args.entry_symbol:
                args.entry = recomp.BASE+value
                break
        if args.entry is None:
            parser.error('Entry symbol is not defined in this module')
    env = recomp.Env(); env.full = True; env.imports = set()
    if args.cut_imports:
        import re
        cuts=set(re.findall(r'\{\s*"([^"\n]+)"',args.cut_imports.read_text()))
        exports={}
        for i in range((recomp.STRTAB-recomp.SYMTAB)//24):
            name,_,_,section,value,_=struct.unpack_from('<IBBHQQ',recomp.IMG,recomp.FILE_OFF+recomp.SYMTAB+i*24)
            start=recomp.FILE_OFF+recomp.STRTAB+name
            name=recomp.IMG[start:recomp.IMG.index(b'\0',start)].decode('latin-1')
            if section and name in cuts and recomp.BASE+value!=args.entry: exports[recomp.BASE+value]=i
        original_call=env.call
        def cut_call(target):
            symbol=env.plt_entry(target)
            if symbol and symbol[1] and recomp.BASE+symbol[2] in exports:
                return 'host_import(c,%d);'%exports[recomp.BASE+symbol[2]]
            if target in exports: return 'host_import(c,%d);'%exports[target]
            return original_call(target)
        env.call=cut_call
    env.compute_starts()
    if not recomp.BASE <= args.entry < recomp.BASE+recomp.TEXT_SIZE:
        parser.error('Entry outside text')
    env.starts = sorted(set(env.starts) | {args.entry})
    roots = [args.entry,*args.root]
    if any(not recomp.BASE<=address<recomp.BASE+recomp.TEXT_SIZE or address%4 for address in roots):
        parser.error('Root outside aligned module code')
    env.starts=sorted(set(env.starts)|set(roots))
    if args.initializers:
        raw=recomp.IMG; phoff=struct.unpack_from('<Q',raw,32)[0]; phnum=struct.unpack_from('<H',raw,56)[0]
        tags={}
        for i in range(phnum):
            ph=phoff+i*56
            if struct.unpack_from('<I',raw,ph)[0] == 2:
                offset,size=struct.unpack_from('<Q',raw,ph+8)[0],struct.unpack_from('<Q',raw,ph+32)[0]
                for j in range(0,size,16):
                    tag,value=struct.unpack_from('<QQ',raw,offset+j)
                    if not tag: break
                    tags[tag]=value
        # The original module's init-array slots are filled by RELATIVE relocs.
        initializers={}
        for i in range(recomp.RELASZ//24):
            offset,info,addend=struct.unpack_from('<QQq',raw,recomp.FILE_OFF+recomp.RELA+i*24)
            if info==0x403 and tags.get(25,0)<=offset<tags.get(25,0)+tags.get(27,0):
                initializers[offset]=recomp.BASE+addend
        roots.extend(initializers[a] for a in sorted(initializers))
        for i in range((recomp.STRTAB-recomp.SYMTAB)//24):
            name,_,_,section,value,_=struct.unpack_from('<IBBHQQ',raw,recomp.FILE_OFF+recomp.SYMTAB+i*24)
            start=recomp.FILE_OFF+recomp.STRTAB+name
            if section and raw[start:raw.index(b'\0',start)] == b'nnMain': roots.append(recomp.BASE+value)
    pending = collections.deque(dict.fromkeys(roots)); scheduled = set(roots)
    bodies, instructions, bad = {}, 0, []
    while pending and len(bodies) < args.max_functions:
        address = pending.popleft()
        body, count, missing = recomp.lift_function(env, address)
        bodies[address] = body; instructions += count; bad.extend(missing)
        for target in env.wanted:
            if target not in scheduled and recomp.BASE <= target < recomp.BASE+recomp.TEXT_SIZE:
                scheduled.add(target); pending.append(target)
    generated = out/'generated'; generated.mkdir()
    all_targets = sorted(set(bodies) | set(env.wanted))
    (generated/'funcs.h').write_text(''.join('void f_%x(Ctx *c);\n'%a for a in all_targets))
    prefix = '#include "runtime.h"\n#include "funcs.h"\n'
    entries = sorted(bodies)
    for index in range(0, len(entries), 64):
        (generated/('c_%04d.c'%index)).write_text(prefix+'\n'.join(bodies[a] for a in entries[index:index+64]))
    omitted = sorted(set(env.wanted)-set(bodies))
    (generated/'omitted.c').write_text(prefix+''.join('void f_%x(Ctx *c) { (void)c; guest_trap("outside bounded bootstrap lift",0x%xULL); }\n'%(a,a) for a in omitted))
    (generated/'table.c').write_text(prefix+'const FnEntry fn_table[] = {\n'+''.join('{0x%xULL,f_%x},\n'%(a,a) for a in entries)+'};\nconst int fn_table_count=%d;\n'%len(entries))
    symbols = []
    for i in range((recomp.STRTAB-recomp.SYMTAB)//24):
        name, _, _, section, value, _ = struct.unpack_from('<IBBHQQ', recomp.IMG, recomp.FILE_OFF+recomp.SYMTAB+i*24)
        start = recomp.FILE_OFF+recomp.STRTAB+name
        name = recomp.IMG[start:recomp.IMG.index(b'\0',start)].decode('latin-1')
        symbols.append('{%s,0x%xULL,%d},\n'%(json.dumps(name,ensure_ascii=True),value,bool(section)))
    (generated/'imports.c').write_text('#include "runtime.h"\nconst DynSym dyn_syms[]={\n'+''.join(symbols)+'};\nconst int dyn_sym_count=%d;\n'%len(symbols))
    write_manifest(generated,image)
    report = {'bounded_bootstrap':True,'functions':len(bodies),'instructions':instructions,
              'untranslated_instructions':len(bad),'omitted_callees':len(omitted),'entry':hex(args.entry)}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report)); print('Private generated sources:', generated)


if __name__ == '__main__':
    main()
