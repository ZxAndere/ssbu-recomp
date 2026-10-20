"""Run all dump-free host, emulation, cross-compilation and packaging checks."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--llvm-bin',type=Path,required=True)
    parser.add_argument('--sdk',type=Path,required=True)
    args=parser.parse_args()
    output=ROOT/'build_ps4/verification';output.mkdir(parents=True,exist_ok=True)
    records={}
    def run(name,command):
        result=subprocess.run([str(a) for a in command],cwd=ROOT,capture_output=True,text=True,errors='replace',timeout=180)
        log=result.stdout+result.stderr;(output/(name+'.log')).write_text(log,encoding='utf-8')
        print(f'{name}: exit {result.returncode}',flush=True)
        if result.returncode:
            print(log[-6000:]);raise RuntimeError(name+' failed')
        records[name]={'exit_code':result.returncode}
        return log
    common=[sys.executable,ROOT/'scripts/ps4/build.py']
    host=run('host',[*common,'host','--llvm-bin',args.llvm_bin])
    records['host']['checks']={name or 'platform':int(count) for name,count in re.findall(r'(?m)^(?:(\w+)[ \t]+)?RESULT: (\d+) checks, 0 failures',host)}
    run('orbis_package',[*common,'ps4','--llvm-bin',args.llvm_bin,'--sdk',args.sdk,'--package'])
    tests=run('python_tests',[sys.executable,'-m','unittest','discover','-s','tests/ps4','-v'])
    records['python_tests']['tests']=int(re.search(r'Ran (\d+) tests',tests)[1])
    records['python_tests']['skipped']='skipped=' in tests or '... skipped ' in tests
    generated=ROOT/'build_ps4/local-link-fixture';generated.mkdir(exist_ok=True)
    source=(ROOT/'build_ps4/smoke/smoke_generated.c').read_text()
    source=source[:source.index('const SpGuestProgram sp_smoke_program')]
    source=source.replace('sp_smoke_f_','f_').replace('static const FnEntry smoke_functions','const FnEntry fn_table')
    (generated/'table.c').write_text(source+'\nconst int fn_table_count=5;\n')
    (generated/'imports.c').write_text('#include "runtime.h"\nconst DynSym dyn_syms[]={{"",0,0}};\nconst int dyn_sym_count=0;\n')
    from lift import write_manifest
    write_manifest(generated,ROOT/'build_ps4/smoke/main.elf')
    game=['--game-generated',generated,'--game-image',ROOT/'build_ps4/smoke/main.elf','--game-entry','0x7100000044']
    private=run('static_link_host',[*common,'host','--llvm-bin',args.llvm_bin,*game])
    if 'execution status=0 unresolved=0 x0=1234' not in private: raise RuntimeError('Static linked fixture did not return 1234')
    run('static_link_orbis',[*common,'ps4','--llvm-bin',args.llvm_bin,'--sdk',args.sdk,*game])
    clang=args.llvm_bin/'clang.exe'
    rb_sources=[ROOT/name for name in ['tests/rb_test.c','runtime/rb_input.c','runtime/rb_wire.c','runtime/rb_timesync.c']]
    run('upstream_rb_compile',[clang,'-O1','-Wall','-Wextra','-Werror','-I',ROOT/'runtime',*rb_sources,'-o',output/'rb_test.exe'])
    rb=run('upstream_rb_run',[output/'rb_test.exe'])
    records['upstream_rb_run']['summary']=rb.strip()
    for source in rb_sources:
        run('orbis_compile_'+source.stem,[clang,'--target=x86_64-pc-freebsd12-elf','-O1','-Wall','-Wextra','-Werror',
            '-D_POSIX_C_SOURCE=200809L','-nostdlibinc','-isystem',args.sdk/'include','-I',ROOT/'runtime','-c',source,'-o',output/(source.stem+'.o')])
    manifest=json.loads((ROOT/'build_ps4/ps4/package-manifest.json').read_text())
    report={'base_commit':'ac2ee45','hardware_tested':False,'gameplay_running':False,
            'unicorn_arm64_cases':1000 if not records['python_tests']['skipped'] else None,
            'package_sha256':manifest['package_sha256'],'checks':records}
    (ROOT/'docs/ps4/verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    (ROOT/'docs/ps4/host-results.txt').write_text('\n'.join(line for line in host.splitlines() if 'RESULT:' in line)+'\n'+tests+rb,encoding='utf-8')
    print('All available dump-free checks passed. No PS4 hardware tests were performed.')


if __name__=='__main__': main()
