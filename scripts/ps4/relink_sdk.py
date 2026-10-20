"""Replace only a private SDK closure in an existing complete game link.

Reuses all existing main objects and keeps previous SDK objects/response files.
Inputs and outputs stay local; this never uploads generated material.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from lift import ROOT
from link_game import stage


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('generated','image','response','out','llvm-bin'): p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--target',choices=('host','ps4'),required=True)
    p.add_argument('--sdk',type=Path)
    p.add_argument('--first-sdk-index',type=int,default=1162)
    args=p.parse_args(); out=args.out.resolve()
    if not out.is_relative_to((ROOT/'build_ps4').resolve()): p.error('--out must be a dedicated build_ps4/ child')
    if args.target=='ps4' and not args.sdk: p.error('PS4 requires --sdk')
    out.mkdir(parents=True,exist_ok=True)
    sources,metadata=stage(args.generated,args.image,out,module='sdk')
    flags=['-std=c17','-O0','-g0','-Wall','-Wextra','-Werror','-Wno-unused-label',
           '-I'+str(ROOT/'runtime'),'-I'+str(ROOT/'runtime/platform'),'-include',str(ROOT/'runtime/platform/guest_bridge.h')]
    if args.target=='ps4': flags+=['--target=x86_64-pc-freebsd12-elf','-fPIC','-funwind-tables','-DSSBU_PS4',
                                   '-D_POSIX_C_SOURCE=200809L','-nostdlibinc','-isystem',str(args.sdk/'include')]
    headers=b''.join(x.read_bytes() for x in sorted((ROOT/'runtime').glob('*.h')))
    headers+=b''.join(x.read_bytes() for x in sorted((ROOT/'runtime/platform').glob('*.h')))
    objects=[]; compiled=0
    for index,source in enumerate([*sources,metadata]):
        obj=out/f'sdk_{index:03}.o'; objects.append(obj)
        command=[str(args.llvm_bin/'clang.exe'),*flags,*(['-Dhost_import=sp_sdk_host_import'] if source!=metadata else []),'-c',str(source),'-o',str(obj)]
        signature=hashlib.sha256(repr(command).encode()+headers+source.read_bytes()).hexdigest(); stamp=obj.with_suffix('.sha256')
        if obj.is_file() and stamp.is_file() and stamp.read_text()==signature: continue
        result=subprocess.run(command,capture_output=True,text=True,errors='replace',timeout=120)
        obj.with_suffix('.log').write_text(result.stdout+result.stderr)
        if result.returncode: raise RuntimeError(f'SDK compilation failed; see {obj.with_suffix(".log")}')
        stamp.write_text(signature); compiled+=1
    original=[json.loads(line) for line in args.response.read_text().splitlines() if line.strip()]
    command=[]; replaced_metadata=False; reused=0
    for item in original:
        match=re.search(r'[/\\]game_(\d+)\.o$',item)
        if match:
            if int(match[1])>=args.first_sdk_index: continue
            reused+=1
        if re.search(r'[/\\]local_sdk_program\.(c|o)$',item):
            replaced_metadata=True; continue
        command.append(item)
    if not replaced_metadata or not reused: raise RuntimeError('Original response lacks the expected private main/SDK linkage')
    at=command.index('-o'); command[at:at]=[str(obj) for obj in objects]
    response=out/'relink.rsp'; response.write_text('\n'.join(json.dumps(item) for item in command)+'\n')
    linker=args.llvm_bin/('ld.lld.exe' if args.target=='ps4' else 'clang.exe')
    result=subprocess.run([str(linker),'@'+str(response)],capture_output=True,text=True,errors='replace',timeout=180)
    (out/'link.log').write_text(result.stdout+result.stderr)
    if result.returncode: raise RuntimeError('SDK relink failed; see '+str(out/'link.log'))
    if args.target=='ps4':
        from build import verify_elf
        verify_elf(Path(command[command.index('-o')+1]))
    report={'main_objects_reused':reused,'main_objects_recompiled':0,'sdk_objects_compiled':compiled,
            'sdk_objects_total':len(objects),'link_exit':result.returncode,'hardware_tested':False}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n'); print(json.dumps(report,indent=2))


if __name__=='__main__': main()
