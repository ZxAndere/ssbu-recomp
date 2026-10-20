"""Run the original lifter on a local, already extracted main.elf.

All copies, generated sources and manifests stay in ignored build_ps4/.
This tool neither obtains keys nor decrypts/downloads game content.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]


def sha256(path):
    digest=hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda:source.read(1024*1024),b''): digest.update(block)
    return digest.hexdigest()


def write_manifest(generated,image):
    files={path.name:sha256(path) for path in sorted(Path(generated).iterdir()) if path.suffix in ('.c','.h')}
    data={'version':1,'image_sha256':sha256(image),'generated_files':files}
    (Path(generated)/'lift-manifest.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image',type=Path,required=True)
    parser.add_argument('--out',type=Path,default=ROOT/'build_ps4/local-lift')
    parser.add_argument('--mode',choices=['full','eh'],default='full')
    parser.add_argument('--jobs',type=int,default=2)
    args=parser.parse_args()
    destination=args.out.resolve(); allowed=(ROOT/'build_ps4').resolve()
    if destination==allowed or not destination.is_relative_to(allowed): parser.error('--out must be a dedicated child of build_ps4/')
    if destination.exists(): parser.error('Output exists; choose a new child directory to preserve the previous lift')
    with args.image.open('rb') as source: header=source.read(64)
    if len(header)!=64 or header[:6]!=b'\x7fELF\2\1' or struct.unpack_from('<H',header,18)[0]!=183:
        parser.error('Expected extracted AArch64 main.elf, not an encrypted NSZ/NSP or a Windows DLL')
    if not 1<=args.jobs<=32: parser.error('--jobs must be 1..32')
    destination.mkdir(parents=True)
    inputs=destination/'inputs';inputs.mkdir();image=inputs/'main.elf'
    shutil.copy2(args.image,image)
    generated=destination/'generated'
    env=dict(os.environ,RECOMP_BIN=str(inputs))
    tool=ROOT/'tools/lifter'/('recomp_eh.py' if args.mode=='eh' else 'recomp.py')
    command=[sys.executable,str(tool)]+([] if args.mode=='eh' else ['full'])+[str(generated),str(args.jobs)]
    subprocess.run(command,cwd=destination,env=env,check=True)
    write_manifest(generated,image)
    print(f'Private generated sources: {generated}\nInput: {image}\nNo content was uploaded.')


if __name__=='__main__': main()
