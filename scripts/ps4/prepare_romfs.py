"""Prepare private patched RomFS from local backups, with resumable NCA caches.

Only the Program NCA is expanded. Original backups/keys are read-only. Outputs
may live on a separate local disk; never in the public source tree outside
ignored build_ps4/. No downloads or uploads occur.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import time
from extract import Nsz, ROOT, rx


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(8<<20),b''): h.update(block)
    return h.hexdigest()


def save(path,data):
    temporary=path.with_suffix(path.suffix+'.tmp')
    temporary.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
    os.replace(temporary,path)


def private_output(path):
    path=path.resolve()
    if path==ROOT or (path.is_relative_to(ROOT) and not path.is_relative_to(ROOT/'build_ps4')):
        raise ValueError('Private outputs must stay outside public sources or inside ignored build_ps4/')
    if path==path.parent: raise ValueError('A drive root is not an output directory')
    return path


def cached_program(source,cache):
    package=Nsz(source) if source.suffix.lower()=='.nsz' else rx.open_package(source)
    nca=rx.Nca(package)
    if nca.header[0x205]!=0: raise ValueError('Expected a Program NCA')
    size=package.files[nca.name][1]
    target=cache/nca.name
    state=target.with_suffix('.json')
    identity={'source':str(source.resolve()),'source_size':source.stat().st_size,
              'source_mtime_ns':source.stat().st_mtime_ns,'entry':nca.name,'size':size}
    if state.exists():
        record=json.loads(state.read_text())
        if record['input']!=identity: raise ValueError('Cache belongs to a different backup')
    else:
        if target.exists() or target.with_suffix('.part').exists(): raise ValueError('Unowned cache file exists')
        record={'input':identity,'complete':False}; save(state,record)
    if not record['complete']:
        partial=target.with_suffix('.part')
        if target.exists(): raise ValueError('Uncommitted complete NCA cache exists; preserve it for inspection')
        offset=partial.stat().st_size if partial.exists() else 0
        if offset>size: raise ValueError('Partial NCA is oversized')
        if shutil.disk_usage(cache).free<size-offset+(64<<20): raise ValueError('Insufficient cache disk space')
        if nca.name in getattr(package,'compressed',{}): package.compressed[nca.name].prefix_limit=16<<20
        started,last=time.monotonic(),0
        with partial.open('r+b' if partial.exists() else 'x+b') as output:
            output.seek(offset)
            while offset<size:
                wanted=min(4<<20,size-offset)
                block=package.read(nca.name,offset,wanted)
                if len(block)!=wanted: raise ValueError('Truncated NCA cache input')
                output.write(block); offset+=len(block)
                if time.monotonic()-last>5:
                    print('Program cache %.1f%% (%d/%d bytes)'%(100*offset/size,offset,size),flush=True); last=time.monotonic()
        checksum=digest(partial)
        if len(nca.name)==36 and checksum[:32]!=nca.name[:32]:
            raise ValueError('Reconstructed Program NCA does not match its content ID')
        os.replace(partial,target)
        record.update(complete=True,sha256=checksum,seconds=time.monotonic()-started); save(state,record)
    if target.stat().st_size!=size or digest(target)!=record['sha256']:
        raise ValueError('NCA cache integrity check failed')
    # Keep the already resolved local key/ticket in memory; serve encrypted NCA
    # bytes from disk for BKTR's nonsequential reads, without replaying NCZ.
    class Cached:
        def __init__(self,path): self.file=path.open('rb')
        def read(self,name,offset,size):
            self.file.seek(offset); data=self.file.read(size)
            if len(data)!=size: raise ValueError('Read outside cached NCA')
            return data
    nca.nsp=Cached(target)
    package.f.close()
    print('Verified private Program cache:',target,flush=True)
    return nca,record['sha256']


def safe_target(root,name):
    parts=name.strip('/').split('/')
    if not parts or any(part in ('','.','..') or '\\' in part or ':' in part for part in parts):
        raise ValueError('Unsafe RomFS entry path')
    target=root.joinpath(*parts).resolve()
    if not target.is_relative_to(root): raise ValueError('RomFS entry escapes output')
    return target


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=Path,required=True)
    parser.add_argument('--update',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--cache',type=Path,required=True)
    args=parser.parse_args()
    out,cache=private_output(args.out),private_output(args.cache)
    if out==cache or out.is_relative_to(cache) or cache.is_relative_to(out): parser.error('Separate RomFS and cache directories required')
    state=out.with_name(out.name+'.manifest.json')
    if out.exists() and not state.exists(): parser.error('Existing output has no owned extraction manifest')
    cache.mkdir(parents=True,exist_ok=True)
    base,base_hash=cached_program(args.base,cache)
    update,update_hash=cached_program(args.update,cache)
    if (int.from_bytes(base.header[0x210:0x218],'little')&~0x800)!=(int.from_bytes(update.header[0x210:0x218],'little')&~0x800):
        parser.error('Base and update title IDs do not match')
    romfs=rx.PatchedRomfs(base,update)
    entries=sorted(rx.walk(romfs,romfs.level6),key=lambda entry:entry[1])
    if len({name for name,_,_ in entries})!=len(entries): raise ValueError('Duplicate RomFS paths')
    total=sum(size for _,_,size in entries)
    record={'base_sha256':base_hash,'update_sha256':update_hash,'files':{},'complete':False}
    if state.exists():
        previous=json.loads(state.read_text())
        if any(previous[key]!=record[key] for key in ('base_sha256','update_sha256')): raise ValueError('RomFS output belongs to different inputs')
        record=previous
    record.update(file_count=len(entries),total_bytes=total)
    out.mkdir(parents=True,exist_ok=True); save(state,record)
    if shutil.disk_usage(out).free<total-sum(item['bytes'] for item in record['files'].values())+(64<<20):
        raise ValueError('Insufficient RomFS disk space')
    print('Patched RomFS: %d files, %d bytes -> %s'%(len(entries),total,out),flush=True)
    for name,offset,size in entries:
        target=safe_target(out,name)
        if name in record['files']:
            known=record['files'][name]
            if target.stat().st_size!=size or digest(target)!=known['sha256']: raise ValueError('Extracted file changed: '+name)
            continue
        if target.exists(): raise ValueError('Unowned output file exists: '+name)
        target.parent.mkdir(parents=True,exist_ok=True)
        partial=target.with_name(target.name+'.part')
        at=partial.stat().st_size if partial.exists() else 0
        if at>size: raise ValueError('Oversized partial RomFS file')
        last=0
        with partial.open('r+b' if partial.exists() else 'x+b') as stream:
            stream.seek(at)
            while at<size:
                wanted=min(8<<20,size-at); block=romfs.read(offset+at,wanted)
                if len(block)!=wanted: raise ValueError('Truncated patched RomFS read')
                stream.write(block); at+=wanted
                if time.monotonic()-last>5:
                    print('RomFS %s: %.1f%%'%(name,100*at/max(size,1)),flush=True); last=time.monotonic()
        checksum=digest(partial); os.replace(partial,target)
        record['files'][name]={'bytes':size,'sha256':checksum}; save(state,record)
    record['complete']=True; save(state,record)
    print('Private patched resources ready:',out,flush=True)


if __name__=='__main__': main()
