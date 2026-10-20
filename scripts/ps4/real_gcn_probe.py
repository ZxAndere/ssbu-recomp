"""Compile already translated private game shaders into GFX7 containers on PC.

This checks compilation and container structure, never GPU execution or visual
correctness. All inputs/outputs stay under ignored build_ps4/.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
from lift import ROOT


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('translated','stack','llvm-bin'): p.add_argument('--'+name,type=Path,required=True)
    args=p.parse_args(); translated=args.translated.resolve()
    if not translated.is_relative_to((ROOT/'build_ps4').resolve()): p.error('--translated must stay under build_ps4/')
    out=translated/'gcn'; out.mkdir(exist_ok=True)
    compiler=args.stack.resolve()/'opengnm-psbc'; obj=out/'probe.o'; executable=out/'probe.exe'
    subprocess.run([str(args.llvm_bin/'clang.exe'),'-std=c17','-O1','-g0','-I'+str(compiler/'libpsbc'),'-I'+str(compiler/'include'),
                    '-I'+str(args.stack.resolve()/'opengnm/include'),'-c',str(ROOT/'platform/ps4/spirv_file_probe.c'),'-o',str(obj)],check=True)
    subprocess.run([str(args.llvm_bin/'clang++.exe'),str(obj),str(ROOT/'build_ps4/graphics-probe/host/libpsbc-host.a'),
                    '-lwinpthread','-ldbghelp','-lpsapi','-lsynchronization','-o',str(executable)],check=True)
    env=dict(os.environ,PATH=str(args.llvm_bin.resolve())+os.pathsep+os.environ['PATH']); reports=[]
    for entry in json.loads((translated/'report.json').read_text())['shaders']:
        if entry.get('spirv_exit')!=0: continue
        source=translated/(entry['capture']+'.spv'); binary=out/(entry['capture']+'.pssl')
        result=subprocess.run([str(executable),str(source),str(entry['stage']),str(binary)],env=env,capture_output=True,text=True,timeout=120)
        output=result.stdout+result.stderr; (out/(entry['capture']+'.log')).write_text(output)
        reports.append({'stage':entry['stage'],'exit':result.returncode,'container_valid':result.returncode==0 and 'valid_container=1' in output,
                        'warnings':'WARNING' in output,'output':output,'gpu_executed':False})
    report={'shader_count':len(reports),'compiled':sum(item['container_valid'] for item in reports),'shaders':reports,
            'gpu_executed':False,'resource_binding_execution_verified':False,'shader_semantics_compared':False,'game_image_rendered':False}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n'); print(json.dumps(report,indent=2))
    if not reports or report['compiled']!=len(reports) or any(item['warnings'] for item in reports): raise SystemExit(1)


if __name__=='__main__': main()
