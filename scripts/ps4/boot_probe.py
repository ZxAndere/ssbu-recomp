"""Capture a native PC boot probe without treating a controlled guest stop as gameplay."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def summarize(output,returncode):
    result=re.search(r'GAME BOOT: execution status=(-?\d+) unresolved=(\d+) x0=(\d+) stop=(.*)',output)
    if returncode not in (0,1) or not result:
        raise RuntimeError('Boot probe crashed or produced no controlled execution result')
    constructors=[int(value) for value in re.findall(r'GAME CONSTRUCTOR: (\d+) at ',output)]
    stopped_in_constructor=bool(constructors and 'GAME PHASE: nnMain' not in output and int(result[1])!=0)
    completed=[int(value) for value in re.findall(r'GAME CONSTRUCTOR DONE: (\d+)',output)]
    resources=re.search(r'GAME RESOURCE SUMMARY: opens=(\d+) reads=(\d+) bytes=(\d+)',output)
    if completed and (len(set(completed))!=len(completed) or any(value not in constructors for value in completed)):
        raise RuntimeError('Invalid constructor completion trace')
    return {'host_process_exit':returncode,'guest_status':int(result[1]),'unresolved_relocations':int(result[2]),
            'constructors_entered':len(constructors),
            'constructors_completed':len(completed) if completed else len(constructors)-int(stopped_in_constructor),
            'constructor_completion_trace':bool(completed),
            'nninit_startup_entered':'GAME PHASE: nninitStartup' in output,
            'application_lifecycle_initialized':'GAME APPLICATION: lifecycle initialized' in output,
            'nnmain_entered':'GAME PHASE: nnMain' in output,'stop':result[4],
            'romfs_mounted':'GAME ROMFS: mounted ' in output,
            'native_desktop_vulkan_device_created':'GAME GRAPHICS: real Vulkan device ' in output,
            'game_image_rendered':False,
            'resource_opens':int(resources[1]) if resources else 0,
            'resource_reads':int(resources[2]) if resources else 0,
            'resource_bytes_read':int(resources[3]) if resources else 0,
            'game_initialization_complete':False,
            'hardware_tested':False,'gameplay_verified':False}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable',type=Path,required=True)
    parser.add_argument('--image',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    result=subprocess.run([str(args.executable.resolve()),'--game-boot',str(args.image.resolve())],capture_output=True,text=True,errors='replace',timeout=180)
    output=result.stdout+result.stderr
    report=summarize(output,result.returncode)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.with_suffix('.log').write_text(output,encoding='utf-8')
    args.out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__': main()
