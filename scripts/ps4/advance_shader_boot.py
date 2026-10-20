"""Resume bounded real boot/capture/translation cycles, stopping on any other error.

Generated game material and logs stay in ignored build_ps4/. Does not execute
or upload anything to PS4, and never treats a controlled stop as gameplay.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
from lift import ROOT


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('executable','image','captures','llvm-bin','glslang'): p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--romfs',type=Path,required=True)
    p.add_argument('--cycles',type=int,default=16)
    args=p.parse_args(); captures=args.captures.resolve()
    if not captures.is_relative_to((ROOT/'build_ps4').resolve()): p.error('--captures must stay under build_ps4/')
    if not 1<=args.cycles<=64: p.error('--cycles must be 1..64')
    captures.mkdir(parents=True,exist_ok=True); (captures/'translated').mkdir(exist_ok=True)
    runs=captures/'boot-runs'; runs.mkdir(exist_ok=True)
    existing=[int(path.stem) for path in runs.glob('*.json') if path.stem.isdigit()]; next_run=max(existing,default=-1)+1
    env=dict(os.environ,SSBU_ROMFS=str(args.romfs.resolve()),SSBU_SHADER_CAPTURE=str(captures),SSBU_SHADER_CACHE=str(captures/'translated'))
    previous_capture_count=-1
    for cycle in range(args.cycles):
        output=runs/f'{next_run+cycle:04}.json'
        result=subprocess.run([sys.executable,str(ROOT/'scripts/ps4/boot_probe.py'),'--executable',str(args.executable),
            '--image',str(args.image),'--out',str(output),'--checkpoints','30000000'],env=env,cwd=ROOT,capture_output=True,text=True,timeout=190)
        if result.returncode: raise RuntimeError('Boot probe crashed or timed out: '+result.stderr[-1000:])
        report=json.loads(output.read_text()); print(f"cycle={cycle} constructors={report['constructors_completed']} stop={report['stop']}",flush=True)
        if 'NVN Maxwell shader translation/native module creation pending' not in report['stop']: break
        capture_count=len(list(captures.glob('*.control')))
        if capture_count==previous_capture_count: raise RuntimeError('Shader compilation did not advance native module creation; inspect the saved boot log')
        previous_capture_count=capture_count
        result=subprocess.run([sys.executable,str(ROOT/'scripts/ps4/maxwell_probe.py'),'--captures',str(captures),
            '--llvm-bin',str(args.llvm_bin),'--glslang',str(args.glslang)],cwd=ROOT,capture_output=True,text=True,timeout=180)
        (runs/f'{next_run+cycle:04}-translate.log').write_text(result.stdout+result.stderr)
        if result.returncode: raise RuntimeError('Private shader translation failed; inspect the saved local log')
    print('Latest reproducible boot report: '+str(output),flush=True)


if __name__=='__main__': main()
