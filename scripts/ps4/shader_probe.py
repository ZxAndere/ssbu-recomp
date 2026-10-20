"""Resume the external SPIR-V -> GCN compiler build with missing Mesa codegen.

Requires explicit local stack, GNU make, LLVM and OpenOrbis paths. Only generated
files and native objects in that supplied external working copy are written.
This does not imply shader correctness or successful PS4 execution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('stack','make','llvm-bin','sdk'): p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--jobs',type=int,default=2)
    p.add_argument('--link-probe',action='store_true',help='Link an Orbis CPU shader-compiler executable with original fixtures')
    args=p.parse_args(); root=args.stack.resolve()/'opengnm-psbc'
    if not 1<=args.jobs<=8: p.error('--jobs must be 1..8')
    def generate(script,output,arguments=(),capture=False):
        target=root/output
        if target.exists() and target.stat().st_size and target.stat().st_mtime_ns>(root/script).stat().st_mtime_ns: return
        command=[sys.executable,script,*arguments]
        if capture:
            temporary=target.with_suffix(target.suffix+'.tmp')
            with temporary.open('w',encoding='utf-8') as stream: subprocess.run(command,cwd=root,stdout=stream,check=True)
            os.replace(temporary,target)
        else: subprocess.run([*command,output],cwd=root,check=True)
    for out,option in [('u_format_gen.h','--enums'),('u_format_pack.h','--header'),('u_format_table.c',None)]:
        generate('src/util/format/u_format_table.py','src/util/format/'+out,['src/util/format/u_format.yaml']+([option] if option else []),True)
    for suffix in ('h','c'): generate('src/compiler/builtin_types_'+suffix+'.py','src/compiler/builtin_types.'+suffix)
    generate('src/util/process_shader_stats.py','src/util/shader_stats.h',['src/util/shader_stats.rnc','src/util/shader_stats.xml'],True)
    packets='src/amd/packets/'
    packet_inputs=[packets+name for name in ('cp_pm4_table_data_gfx11.json','pm4_it_opcodes_gfx11.h','cp_pm4_table_data_gfx12.json','pm4_it_opcodes_gfx12.h')]
    for generation in ('gfx11','gfx12'):
        generate(packets+'parse_cp_pm4_table_data_json.py','src/amd/common/amd_cp_packets_'+generation+'.h',[*packet_inputs,generation,'packets_h'],True)
    generate('src/util/format_srgb.py','src/util/format_srgb.c',capture=True)
    generate('src/amd/common/gfx10_format_table.py','src/amd/common/gfx10_format_table.c',
             ['src/util/format/u_format.yaml','src/amd/registers/gfx10-rsrc.json','src/amd/registers/gfx11-rsrc.json'],True)
    xml=str(args.stack.resolve()/'Vulkan-Headers/registry/vk.xml'); util='src/vulkan/util/'
    jobs=[('vk_struct_type_cast_gen.py',['--out',util+'vk_struct_type_cast.h','--beta','false']),
          ('vk_dispatch_table_gen.py',['--out-c',util+'vk_dispatch_table.c','--out-h',util+'vk_dispatch_table.h','--beta','false']),
          ('gen_enum_to_str.py',['--out-c',util+'vk_enum_to_str.c','--out-h',util+'vk_enum_to_str.h','--out-d',util+'vk_enum_defines.h','--beta','false']),
          ('vk_extensions_gen.py',['--out-c',util+'vk_extensions.c','--out-h',util+'vk_extensions.h'])]
    for script,options in jobs: subprocess.run([sys.executable,util+script,'--xml',xml,*options],cwd=root,check=True)
    sdk=args.sdk.resolve().as_posix(); llvm=args.llvm_bin.resolve()
    env=dict(os.environ,OO_PS4_TOOLCHAIN=sdk)
    arch='--target=x86_64-pc-freebsd12-elf -fPIC -nostdlibinc -DORBIS -D__ORBIS__ -D__PS4__ '+ ' '.join('-I'+sdk+suffix for suffix in ('/include','/include/c++/v1','/include/orbis'))+' -Iopenorbis-compat/include'
    command=[str(args.make.resolve()),'-f','Makefile.orbis','-j'+str(args.jobs),'PYTHON='+Path(sys.executable).as_posix(),
             'CC='+(llvm/'clang.exe').as_posix(),'CXX='+(llvm/'clang++.exe').as_posix(),'AR='+(llvm/'llvm-ar.exe').as_posix(),'ARCHFLAGS='+arch]
    # The extracted RADV device shim stores compiler_info as a pointer. Its
    # copied source still takes the address of the old inline member. Compile
    # a corrected copy while preserving the external checkout's source file.
    stage=Path(__file__).resolve().parents[2]/'build_ps4/graphics-probe'; stage.mkdir(exist_ok=True)
    # Upstream's standalone vk_pipeline.h replaces subgroup selection with
    # a no-op. GFX7 cannot execute wave32, including small compute groups.
    # Restore selection/clamping semantics before RADV chooses an ISA wave.
    compat=stage/'mesa-compat'; compat.mkdir(exist_ok=True)
    pipeline=(root/'src/vulkan/runtime/vk_pipeline.h').read_text()
    old='''   (void)shader; (void)subgroup_size; (void)min_subgroup_size;
   (void)max_subgroup_size; (void)spirv_version; (void)info_pNext;
   (void)allow_varying; (void)require_full;'''
    new='''   uint32_t required=0;
   for (const VkBaseInStructure *p=info_pNext;p;p=p->pNext)
      if (p->sType==VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO)
         required=((const VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *)p)->requiredSubgroupSize;
   if (required) {
      assert(required<=128 && required && !(required&(required-1)));
      shader->info.api_subgroup_size=required;
      shader->info.min_subgroup_size=shader->info.max_subgroup_size=required;
   } else if (!allow_varying && spirv_version<0x10600 && subgroup_size) {
      shader->info.api_subgroup_size=subgroup_size;
      shader->info.max_subgroup_size=subgroup_size;
      if (require_full) shader->info.min_subgroup_size=subgroup_size;
   }
   if (max_subgroup_size) {
      assert(min_subgroup_size);
      shader->info.max_subgroup_size=MIN2(shader->info.max_subgroup_size,max_subgroup_size);
      shader->info.min_subgroup_size=MAX2(shader->info.min_subgroup_size,min_subgroup_size);
   }
   assert(shader->info.min_subgroup_size<=shader->info.max_subgroup_size);'''
    if old not in pipeline: raise RuntimeError('Unknown upstream subgroup shim; review before staging')
    header=compat/'vk_pipeline.h'; pipeline=pipeline.replace(old,new)
    if not header.exists() or header.read_text()!=pipeline: header.write_text(pipeline)
    command[-1]+=' -I'+compat.as_posix()
    shader=stage/'radv_shader.c'; original=(root/'src/amd/vulkan/radv_shader.c').read_text()
    corrected=original.replace('&device->compiler_info','device->compiler_info')
    if not shader.exists() or shader.read_text()!=corrected: shader.write_text(corrected)
    rule='src/amd/vulkan/radv_shader.orbis.o: '+shader.as_posix()+' '+header.as_posix()+'\n\t$(CC) -I'+compat.as_posix()+' $(CFLAGS) -c '+shader.as_posix()+' -o $@\n'
    compiler=stage/'psbc_compile.c'; original=(root/'libpsbc/psbc_compile.c').read_text()
    corrected=original.replace('compiler_info.ac = &ac_info;',
        'compiler_info.ac = &ac_info;\n    compiler_info.spirv_caps.Shader = true;\n'
        '    compiler_info.spirv_caps.DemoteToHelperInvocation = true;\n'
        '    compiler_info.subgroup_size = (gfxlevel >= GFX10) ? 32 : 64;\n'
        '    compiler_info.min_subgroup_size = (gfxlevel >= GFX10) ? 32 : 64;\n'
        '    compiler_info.max_subgroup_size = 64;')
    corrected=corrected.replace('RADV_PIPELINE_GRAPHICS, false, &stage.info',
        'mesa_stage == MESA_SHADER_COMPUTE ? RADV_PIPELINE_COMPUTE : RADV_PIPELINE_GRAPHICS, false, &stage.info')
    if not compiler.exists() or compiler.read_text()!=corrected: compiler.write_text(corrected)
    rule+='libpsbc/psbc_compile.orbis.o: '+compiler.as_posix()+'\n\t$(CC) $(CFLAGS) -c '+compiler.as_posix()+' -o $@\n'
    # This source set ships only BLAKE3's portable implementation. Jaguar has
    # no AVX512; suppress unavailable SIMD dispatch targets instead of stubbing
    # hashing functions or returning invented cache digests.
    blake_config=stage/'blake3-portable.h'
    config='\n'.join('#define BLAKE3_NO_'+name+' 1' for name in ('SSE2','SSE41','AVX2','AVX512'))+'\n'
    if not blake_config.exists() or blake_config.read_text()!=config: blake_config.write_text(config)
    rule+='src/util/blake3/blake3_dispatch.orbis.o: '+blake_config.as_posix()+'\n\t$(CC) $(CFLAGS) -include '+blake_config.as_posix()+' -c src/util/blake3/blake3_dispatch.c -o $@\n'
    rule+='src/util/log.orbis.o:\n\t$(CC) $(CFLAGS) -D_GNU_SOURCE -c src/util/log.c -o $@\n'
    # Link the real format and logging routines excluded from the upstream
    # Orbis source list; the shader compiler's format table references them.
    command+=['--eval',rule]
    subprocess.run(command,cwd=root,env=env,check=True)
    extra=['src/util/log.orbis.o','src/util/format/u_format_s3tc.orbis.o']
    subprocess.run([*command,*extra],cwd=root,env=env,check=True)
    subprocess.run([str(llvm/'llvm-ar.exe'),'rcs','libpsbc.orbis.a',*extra],cwd=root,check=True)
    if args.link_probe:
        source=Path(__file__).resolve().parents[2]/'platform/ps4/shader_compile_probe.c'; obj=stage/'shader_compile_probe.o'; elf=stage/'shader_compile_probe.elf'
        sdk_path=args.sdk.resolve()
        subprocess.run([str(llvm/'clang.exe'),'--target=x86_64-pc-freebsd12-elf','-fPIC','-std=c17','-O1','-nostdlibinc',
            '-isystem',str(sdk_path/'include'),'-I',str(root/'libpsbc'),'-I',str(root/'include'),'-I',str(args.stack.resolve()/'opengnm/include'),'-c',str(source),'-o',str(obj)],check=True)
        subprocess.run([str(llvm/'ld.lld.exe'),'-m','elf_x86_64','-pie','--script',str(sdk_path/'link.x'),'--eh-frame-hdr',
            '-L'+str(sdk_path/'lib'),str(sdk_path/'lib/crt1.o'),str(obj),str(root/'libpsbc.orbis.a'),
            str(stage/'opengnm/libopengnm.a'),'-lc','-lkernel','-lc++','-lc++abi','-lunwind','-lSceGnmDriver','-lSceVideoOut','-o',str(elf)],check=True)
        from build import verify_elf
        verify_elf(elf)
        report={'orbis_elf_linked':True,'sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'bytes':elf.stat().st_size,
                'shader_compiler_executed':False,'gpu_executed':False,'game_shaders_tested':False}
        (stage/'shader-link-report.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report),flush=True)


if __name__=='__main__': main()
