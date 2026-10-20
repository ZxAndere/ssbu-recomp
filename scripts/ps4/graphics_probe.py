"""Compile an explicitly supplied OpenGNM stack without altering its sources.

This checks C/ELF compatibility only. It neither links a shader compiler nor
claims to execute the GPU. External sources and all outputs stay local.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import subprocess
from lift import ROOT


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stack',type=Path,required=True)
    parser.add_argument('--llvm-bin',type=Path,required=True)
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--repair-duplicate-stubs',action='store_true',help='Stage dispatch without stubs shadowing existing real implementations')
    args=parser.parse_args()
    out=ROOT/'build_ps4/graphics-probe'; out.mkdir(parents=True,exist_ok=True)
    stack=args.stack.resolve(); gnm=stack/'opengnm'; vk=stack/'vulkan-ps4'
    clang=args.llvm_bin/'clang.exe'; ar=args.llvm_bin/'llvm-ar.exe'
    flags=['--target=x86_64-pc-freebsd12-elf','-fPIC','-std=c11','-O1','-g0','-nostdlibinc',
           '-isystem',str(args.sdk/'include'),'-D_POSIX_C_SOURCE=200809L']
    gnm_sources=[gnm/('src/'+name+'.c') for name in ('commandbuffer','dataformat','depthrendertarget',
        'drawcommandbuffer','error','helpers','rendertarget','shader','texture','driver_orbis','platform_orbis')]
    for folder in ('gcn','pm4','gpuaddr'): gnm_sources+=sorted((gnm/'src'/folder).glob('*.c'))
    vk_sources=[vk/name for name in re.findall(r'\bsrc/(vk_ps4_\w+\.c)',(vk/'Makefile.orbis').read_text())]
    vk_sources=[vk/'src'/p.name for p in vk_sources]
    report={'hardware_tested':False,'renderer_integrated':False,'shader_compiler_linked':False,'components':{}}
    staged_dispatch=None
    if args.repair_duplicate_stubs:
        implementations=set(); other_sources=''
        for source in vk_sources:
            if source.name!='vk_ps4_dispatch.c':
                body=source.read_text(); other_sources+=body
                implementations.update(re.findall(r'VKAPI_CALL\s+(vk_ps4_\w+)\s*\([^;{}]*\)\s*\{',body))
        source=(vk/'src/vk_ps4_dispatch.c').read_text()
        removed=[]
        def remove_stub(match):
            if match[1] not in implementations and re.search(r'\b'+match[1]+r'\s*\(',other_sources): return match[0]
            removed.append(match[1]); return '/* Redundant or unreferenced private stub: '+match[1]+' */'
        source=re.sub(r'(?m)^STUB_NOT_IMPL\((vk_ps4_\w+),[^\n]*$',remove_stub,source)
        staged_dispatch=out/'vk_ps4_dispatch.c'; staged_dispatch.write_text(source)
        report['dispatch_repair']={'removed_duplicate_stubs':removed,'external_sources_modified':False}
    for name,root,sources,extra in [
        ('opengnm',gnm,gnm_sources,['-DOPENGNM_ORBIS','-DOPENGNM_REQUIRE_ABI','-I',str(gnm/'include'),'-I',str(gnm/'src')]),
        ('vulkan_ps4',vk,vk_sources,['-DORBIS','-D__ORBIS__','-D__PS4__','-DVK_USE_PLATFORM_PS4',
          '-DVK_PS4_HAVE_PSBC=0','-I',str(vk/'include'),'-I',str(gnm/'include'),'-I',str(stack/'Vulkan-Headers/include'),
          '-I',str(stack/'opengnm-psbc/libpsbc'),'-I',str(stack/'opengnm-psbc/include')])]:
        directory=out/name; directory.mkdir(exist_ok=True)
        headers=b''.join(p.read_bytes() for directory in (root/'include',gnm/'include',stack/'Vulkan-Headers/include') for p in sorted(directory.rglob('*.h')))
        def compile_one(source):
            obj=directory/('_'.join(source.relative_to(root).with_suffix('').parts)+'.o')
            stamp=obj.with_suffix('.sha256'); command=[str(clang),*flags,*extra,'-c',str(source),'-o',str(obj)]
            if source.name=='vk_ps4_dispatch.c' and staged_dispatch: source=staged_dispatch; command[-3]=str(source)
            signature=hashlib.sha256(repr(command).encode()+headers+source.read_bytes()).hexdigest()
            log=obj.with_suffix('.log')
            if obj.exists() and stamp.exists() and stamp.read_text()==signature: return obj,0
            result=subprocess.run(command,capture_output=True,text=True,errors='replace',timeout=180)
            log.write_text(result.stdout+result.stderr,encoding='utf-8')
            if not result.returncode: stamp.write_text(signature)
            return obj,result.returncode
        with ThreadPoolExecutor(max_workers=2) as pool: results=list(pool.map(compile_one,sources))
        failed=[str(obj.with_suffix('.log')) for obj,rc in results if rc]
        linked=False
        if not failed:
            subprocess.run([str(ar),'rcs',str(directory/('lib'+name+'.a')),*[str(obj) for obj,_ in results]],check=True)
            linked=True
        report['components'][name]={'revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
            'c_sources':len(sources),'compiled':len(sources)-len(failed),'archive_built':linked,'failures':failed}
        print(name+': '+json.dumps(report['components'][name]),flush=True)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__': main()
