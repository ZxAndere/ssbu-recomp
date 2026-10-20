"""Resolve CPU-only NVN state functions from the user's matching local SDK.

Only names and addresses are emitted, under ignored build_ps4/. No proprietary
headers or copied function implementations are needed. The table addresses
were identified in the locally supplied 13.0.5 SDK's bootstrap loader.
"""
import argparse
import json
from pathlib import Path
import struct
from lift import ROOT


STATE_PREFIXES=('BlendState','ChannelMaskState','ColorState','DepthStencilState',
                'MultisampleState','PolygonState','VertexAttribState','VertexStreamState','TextureView','SamplerBuilder')


def resolve(image):
    blob=Path(image).read_bytes()
    if blob[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',blob,18)[0]!=183:
        raise ValueError('Expected the local AArch64 SDK ELF')
    offset=struct.unpack_from('<Q',blob,32)[0]; count=struct.unpack_from('<H',blob,56)[0]
    segments=[struct.unpack_from('<IIQQQQQQ',blob,offset+i*56) for i in range(count)]
    def read(address,size):
        for p in segments:
            if p[0]==1 and p[3]<=address and address+size<=p[3]+p[5]:
                start=p[2]+address-p[3]; return blob[start:start+size]
        raise ValueError('NVN metadata outside SDK load segments')
    tags={}
    for p in segments:
        if p[0]==2:
            for at in range(p[2],p[2]+p[5],16):
                tag,value=struct.unpack_from('<QQ',blob,at)
                if not tag: break
                tags[tag]=value
    relocations={}
    raw=read(tags[7],tags[8])
    for at in range(0,len(raw),24):
        address,info,addend=struct.unpack_from('<QQq',raw,at)
        if info&0xffffffff==0x403: relocations[address]=addend
    table=relocations[0xc3b348]; result={}
    for index in range(516):
        name_offset=struct.unpack('<I',read(0xa968e4+index*4,4))[0]
        name=read(0xa93230+name_offset,128).split(b'\0')[0].decode('ascii')
        if name.startswith(STATE_PREFIXES):
            address=relocations[table+index*8]
            if not 0x200<=address<0x540950 or address%4: raise ValueError('Invalid NVN code pointer')
            result['nvn'+name]=hex(0x7200000000+address)
    if len(result)<50: raise ValueError('Matching NVN state table not found')
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image',type=Path,required=True)
    p.add_argument('--seed',type=Path,default=ROOT/'docs/ps4/sdk-boot-roots.json')
    p.add_argument('--out',type=Path,required=True)
    args=p.parse_args(); out=args.out.resolve()
    if not out.is_relative_to((ROOT/'build_ps4').resolve()): p.error('--out must stay under build_ps4/')
    roots=json.loads(args.seed.read_text()); roots['nvn_state_functions']=resolve(args.image)
    roots['root_addresses']=list(roots.get('root_addresses',[]))+list(roots['nvn_state_functions'].values())
    out.parent.mkdir(parents=True,exist_ok=True); out.write_text(json.dumps(roots,indent=2)+'\n')
    print(f"Resolved {len(roots['nvn_state_functions'])} CPU-only NVN state functions; private roots: {out}")


if __name__=='__main__': main()
