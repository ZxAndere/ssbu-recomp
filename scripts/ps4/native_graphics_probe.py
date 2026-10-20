"""Validate native desktop GPU storage and GPU copy using an original fixture."""
import argparse
import json
from pathlib import Path
import subprocess
from lift import ROOT


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--llvm-bin',type=Path,required=True)
    p.add_argument('--vulkan-headers',type=Path,required=True)
    args=p.parse_args()
    out=ROOT/'build_ps4/native-graphics-probe'; out.mkdir(parents=True,exist_ok=True)
    executable=out/'probe.exe'
    command=[str(args.llvm_bin/'clang.exe'),'-std=c17','-O1','-g0','-Wall','-Wextra','-Werror',
        '-DSP_GUEST_VULKAN','-I'+str(args.vulkan_headers),'-I'+str(ROOT/'runtime/platform'),
        str(ROOT/'platform/ps4/native_graphics_probe.c'),str(ROOT/'runtime/platform/common.c'),
        str(ROOT/'runtime/platform/windows.c'),'-o',str(executable)]
    subprocess.run(command,check=True)
    result=subprocess.run([str(executable)],capture_output=True,text=True,errors='replace',timeout=60)
    output=result.stdout+result.stderr; (out/'run.log').write_text(output,encoding='utf-8')
    report={'host_exit':result.returncode,'desktop_gpu_transfer_verified':result.returncode==0 and 'GPU buffer transfer verified' in output,
            'ps4_hardware_tested':False,'game_image_rendered':False,'output':output}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8'); print(json.dumps(report,indent=2))
    if not report['desktop_gpu_transfer_verified']: raise SystemExit(1)


if __name__=='__main__': main()
