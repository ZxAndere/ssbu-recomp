"""Build/run the external GCN compiler on Windows with original SPIR-V fixtures.

Reuses its prepared Mesa sources; keeps PS4 objects intact. Successful compiler
output does not prove that a PS4 GPU executes the resulting binaries correctly.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from lift import ROOT


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stack',type=Path,required=True)
    p.add_argument('--llvm-bin',type=Path,required=True)
    args=p.parse_args(); root=args.stack.resolve()/'opengnm-psbc'; llvm=args.llvm_bin.resolve()
    out=ROOT/'build_ps4/graphics-probe/host'; out.mkdir(parents=True,exist_ok=True)
    config=(root/'config.orbis.mak').read_text().replace('\\\n',' ')
    shared=re.search(r'(?m)^SHARED_FLAGS=([^\n]+)',config)[1].replace('$(OPENGNM_INCLUDE)','../opengnm/include').split()
    compat=ROOT/'build_ps4/graphics-probe/mesa-compat'
    common=['-O1','-g0','-I'+str(compat),*shared,'-Dalloca=__builtin_alloca','-DBLAKE3_NO_SSE2','-DBLAKE3_NO_SSE41','-DBLAKE3_NO_AVX2','-DBLAKE3_NO_AVX512',
            '-Wno-unused-function','-Wno-unused-variable','-Wno-macro-redefined']
    # Each completed Orbis object identifies a source from the actual selected
    # compiler build. Windows uses a different object directory and ABI.
    sources=[]
    for obj in sorted(root.rglob('*.orbis.o')):
        source=Path(str(obj).replace('.orbis.o','.c'))
        if source.exists(): sources.append(source)
    for obj in sorted(root.rglob('*.orbis.cpp.o')):
        source=Path(str(obj).replace('.orbis.cpp.o','.cpp'))
        if source.exists(): sources.append(source)
    headers=b''.join(x.read_bytes() for folder in ('include','src/compiler','src/util','src/amd') for x in sorted((root/folder).rglob('*.h')))
    headers+=b''.join(x.read_bytes() for x in sorted(compat.glob('*.h')))
    # HAVE_PTHREAD selects winpthreads' C11 mutex (8 bytes), so the monotonic
    # condition helper must use pthread APIs too, rather than casting that
    # mutex to a 40-byte Windows CRITICAL_SECTION. Keep the public 8-byte
    # condition storage, checked against the actual winpthreads ABI.
    condition=out/'cnd_monotonic.c'
    body=(root/'src/util/cnd_monotonic.c').read_text().replace('#ifdef _WIN32','#if defined(_WIN32) && !defined(HAVE_PTHREAD)').replace('&cond->cond','(pthread_cond_t *)cond')
    body='#include "util/cnd_monotonic.h"\n_Static_assert(sizeof(struct u_cnd_monotonic)==sizeof(pthread_cond_t),"winpthreads condition storage");\n'+body
    if not condition.exists() or condition.read_text()!=body: condition.write_text(body)
    threading=out/'u_thread.c'
    body=(root/'src/util/u_thread.c').read_text().replace('#if defined(HAVE_PTHREAD) && !DETECT_OS_FUCHIA','#if defined(HAVE_PTHREAD) && !DETECT_OS_FUCHIA && !defined(_WIN32)')
    if not threading.exists() or threading.read_text()!=body: threading.write_text(body)
    def compile_one(source):
        relative=source.relative_to(root); obj=out/(str(relative).replace('\\','_').replace('/','_')+'.o')
        if source.name=='radv_shader.c': source=ROOT/'build_ps4/graphics-probe/radv_shader.c'
        if source.name=='psbc_compile.c' and (ROOT/'build_ps4/graphics-probe/psbc_compile.c').exists(): source=ROOT/'build_ps4/graphics-probe/psbc_compile.c'
        if source.name=='cnd_monotonic.c': source=condition
        if source.name=='u_thread.c': source=threading
        cxx=source.suffix=='.cpp'; compiler=llvm/('clang++.exe' if cxx else 'clang.exe')
        command=[str(compiler),'-std='+('c++17' if cxx else 'gnu11'),*(['-include','algorithm'] if cxx else []),*common,'-c',str(source),'-o',str(obj)]
        signature=hashlib.sha256(repr(command).encode()+headers+source.read_bytes()).hexdigest(); stamp=obj.with_suffix('.sha256')
        if obj.exists() and stamp.exists() and stamp.read_text()==signature: return obj,0
        result=subprocess.run(command,cwd=root,capture_output=True,text=True,errors='replace',timeout=180)
        obj.with_suffix('.log').write_text(result.stdout+result.stderr)
        if not result.returncode: stamp.write_text(signature)
        return obj,result.returncode
    with ThreadPoolExecutor(max_workers=2) as pool: results=list(pool.map(compile_one,sources))
    failures=[str(obj.with_suffix('.log')) for obj,rc in results if rc]
    report={'sources':len(sources),'compiled':len(sources)-len(failures),'failures':failures,'hardware_tested':False}
    if not failures:
        # Response files avoid Windows' command-line length limit.
        archive=out/'libpsbc-host.a'; response=out/'archive.rsp'
        response.write_text('\n'.join(json.dumps(x) for x in ['rcs',str(archive),*[str(obj) for obj,_ in results]]))
        subprocess.run([str(llvm/'llvm-ar.exe'),'@'+str(response)],check=True)
        fixture=out/'fixture.o'
        subprocess.run([str(llvm/'clang.exe'),'-std=c17','-O1','-I'+str(root/'libpsbc'),'-I'+str(root/'include'),'-I'+str(args.stack.resolve()/'opengnm/include'),'-c',str(ROOT/'platform/ps4/shader_compile_probe.c'),'-o',str(fixture)],check=True)
        command=[str(fixture),str(archive),'-lwinpthread','-ldbghelp','-lpsapi','-lsynchronization','-o',str(out/'shader_compile_probe.exe')]
        response=out/'link.rsp'; response.write_text('\n'.join(json.dumps(x) for x in command))
        result=subprocess.run([str(llvm/'clang++.exe'),'@'+str(response)],cwd=root,capture_output=True,text=True)
        (out/'link.log').write_text(result.stdout+result.stderr); report['link_exit']=result.returncode
        if not result.returncode:
            env=dict(os.environ,PATH=str(llvm)+os.pathsep+os.environ['PATH'])
            result=subprocess.run([str(out/'shader_compile_probe.exe')],env=env,capture_output=True,text=True,timeout=120)
            (out/'run.log').write_text(result.stdout+result.stderr); report['run_exit']=result.returncode; report['output']=result.stdout
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    if failures or report.get('link_exit')!=0 or report.get('run_exit')!=0: raise SystemExit(1)


if __name__=='__main__': main()
