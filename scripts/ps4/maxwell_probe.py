"""Compile locally captured real game shaders with the existing Maxwell translator.

All source captures and derived GLSL/SPIR-V stay under ignored build_ps4/.
The CLI glslang dependency is explicitly supplied; no downloads occur here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from lift import ROOT


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--captures',type=Path,required=True)
    p.add_argument('--llvm-bin',type=Path,required=True)
    p.add_argument('--glslang',type=Path,required=True)
    args=p.parse_args(); captures=args.captures.resolve(); allowed=(ROOT/'build_ps4').resolve()
    if not captures.is_relative_to(allowed): p.error('--captures must stay under ignored build_ps4/')
    out=captures/'translated'; out.mkdir(exist_ok=True)
    executable=out/'maxwell_compile_probe.exe'
    sources=[ROOT/name for name in ('platform/ps4/maxwell_compile_probe.c','runtime/mx_translate.c','runtime/mx_translate.h','runtime/mx_ops.h')]
    translator_hash=hashlib.sha256(b''.join(path.read_bytes() for path in sources)+str(args.llvm_bin.resolve()).encode()).hexdigest()
    stamp=out/'translator.sha256'
    if not executable.exists() or not stamp.exists() or stamp.read_text()!=translator_hash:
        subprocess.run([str(args.llvm_bin/'clang.exe'),'-std=c17','-O1','-g0','-I'+str(ROOT/'runtime'),
                        str(sources[0]),str(sources[1]),'-o',str(executable)],check=True)
        stamp.write_text(translator_hash)
    compiler_hash=hashlib.sha256(args.glslang.read_bytes()).digest()
    reports=[]
    for control in sorted(captures.glob('*.control')):
        prefix=control.with_suffix(''); raw=control.read_bytes(); stage=struct.unpack_from('<I',raw,0x714)[0]
        if stage!=5:
            header=prefix.with_suffix('.maxwell').read_bytes()[:4]
            stage={1:0,2:3,3:4,4:2,5:1}.get((struct.unpack('<I',header)[0]>>10)&15,-1)
        extension={0:'vert',1:'frag',2:'geom',3:'tesc',4:'tese',5:'comp'}.get(stage)
        report={'capture':control.stem,'stage':stage,'rendered':False}
        if not extension: report['error']='unsupported stage'; reports.append(report); continue
        text=out/(control.stem+'.'+extension); spirv=text.with_suffix('.spv')
        constants=prefix.with_suffix('.constants')
        pieces=(raw,prefix.with_suffix('.maxwell').read_bytes(),constants.read_bytes() if constants.exists() else b'')
        cache=out/(control.stem+'.cache'); stamp=out/(control.stem+'.sha256')
        signature=hashlib.sha256(translator_hash.encode()+compiler_hash+b''.join(pieces)).hexdigest()
        if spirv.exists() and cache.exists() and stamp.exists() and stamp.read_text()==signature:
            binary=spirv.read_bytes(); report.update(maxwell_exit=0,spirv_exit=0,spirv_bytes=len(binary),spirv_sha256=hashlib.sha256(binary).hexdigest(),reused=True)
            reports.append(report); continue
        result=subprocess.run([str(executable),str(prefix),str(text)],capture_output=True,text=True,timeout=60)
        (out/(control.stem+'.maxwell.log')).write_text(result.stdout+result.stderr)
        report['maxwell_exit']=result.returncode
        if not result.returncode:
            result=subprocess.run([str(args.glslang),'-V','--target-env','vulkan1.3','-S',extension,'-o',str(spirv),str(text)],capture_output=True,text=True,timeout=60)
            (out/(control.stem+'.glslang.log')).write_text(result.stdout+result.stderr); report['spirv_exit']=result.returncode
            if not result.returncode:
                binary=spirv.read_bytes(); report['spirv_bytes']=len(binary); report['spirv_sha256']=hashlib.sha256(binary).hexdigest()
                if len(binary)<20 or len(binary)%4 or binary[:4]!=b'\x03\x02\x23\x07': raise RuntimeError('Invalid SPIR-V output')
                fingerprint=14695981039346656037
                for piece in pieces:
                    for byte in piece: fingerprint=((fingerprint^byte)*1099511628211)&((1<<64)-1)
                cache.write_bytes(b'SSBUSPV1'+struct.pack('<QII',fingerprint,stage,len(binary)//4)+binary)
                stamp.write_text(signature)
        reports.append(report)
    report={'private_shader_count':len(reports),'shaders':reports,'game_image_rendered':False,'ps4_hardware_tested':False}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n'); print(json.dumps(report,indent=2))
    if not reports or any(item.get('spirv_exit')!=0 for item in reports): raise SystemExit(1)


if __name__=='__main__': main()
