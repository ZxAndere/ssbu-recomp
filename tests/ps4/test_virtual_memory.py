"""Exercise 16 KiB host / 4 KiB guest page interactions on Windows only."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/ps4'))
from build import run


class VirtualMemoryTests(unittest.TestCase):
    def test_partial_pages_permissions_and_zero_fill(self):
        config=ROOT/'build_ps4/toolchain.json'
        if not config.is_file() or sys.platform!='win32': self.skipTest('Windows host compiler required')
        clang=Path(json.loads(config.read_text())['llvm_bin'])/'clang.exe'
        harness='''#include <stdlib.h>
#include <string.h>
#include "platform.h"
#define SP_VM_NATIVE_PAGE 16384
#include "virtual_memory.c"
#define CHECK(test) do { if (!(test)) return __LINE__; } while (0)
int main(void) {
 GuestVm *vm=NULL;
 CHECK(vm_reserve(&vm,5*4096)==SP_OK);
 CHECK(vm->committed_bytes==0 && vm->reservation.size==32768);
 CHECK(vm_reserve(&vm,4096)==SP_INVALID);
 CHECK(!vm_pointer(vm,4096,1,0));
 CHECK(vm_commit(vm,4096,4096)==SP_OK && vm->committed_bytes==16384);
 unsigned char *first=vm_pointer(vm,4096,4096,1); CHECK(first && !first[0]);
 memset(first,0xa5,4096);
 CHECK(vm_commit(vm,8192,4096)==SP_OK && vm->committed_bytes==16384);
 unsigned char *second=vm_pointer(vm,8192,4096,1); CHECK(second); memset(second,0x5a,4096);
 CHECK(!vm_pointer(vm,0,1,1));
 CHECK(vm_permission(vm,4096,4096,1)==SP_OK && !vm_pointer(vm,4096,1,1));
 CHECK(*(unsigned char *)vm_pointer(vm,4096,1,0)==0xa5);
 CHECK(vm_permission(vm,4096,4096,0)==SP_OK && !vm_pointer(vm,4096,1,0));
 CHECK(vm_permission(vm,4096,4096,3)==SP_OK && first[0]==0xa5);
 CHECK(vm_decommit(vm,4096,4096)==SP_OK && vm->committed_bytes==16384);
 CHECK(!vm_pointer(vm,4096,1,0) && second[0]==0x5a);
 CHECK(vm_commit(vm,4096,4096)==SP_OK && first[0]==0 && second[0]==0x5a);
 CHECK(vm_decommit(vm,4096,8192)==SP_OK && vm->committed_bytes==0);
 CHECK(vm_commit(vm,16384,4096)==SP_OK && vm->committed_bytes==16384);
 CHECK(vm_pointer(vm,20479,1,1) && !vm_pointer(vm,20479,2,0));
 CHECK(vm_commit(vm,SIZE_MAX-4095,4096)==SP_INVALID);
 CHECK(vm_commit(vm,1,4096)==SP_INVALID && vm_commit(vm,0,1)==SP_INVALID);
 CHECK(vm_permission(vm,0,4096,3)==SP_INVALID);
 CHECK(vm_close(&vm)==SP_OK && !vm && vm_close(&vm)==SP_INVALID);
 CHECK(vm_reserve(&vm,512*1024*1024)==SP_OK && vm->committed_bytes==0);
 CHECK(vm_commit(vm,4096,4096)==SP_OK && vm->committed_bytes==16384);
 CHECK(vm_close(&vm)==SP_OK);
 return 0;
}
'''
        with tempfile.TemporaryDirectory(dir=ROOT/'build_ps4') as folder:
            out=Path(folder); (out/'test.c').write_text(harness)
            run([clang,'-O1','-std=c17','-Wall','-Wextra','-Werror','-I',ROOT/'runtime/platform',out/'test.c',
                 ROOT/'runtime/platform/windows.c','-o',out/'test.exe'],capture=True,timeout=60)
            subprocess.run([str(out/'test.exe')],capture_output=True,check=True,timeout=10)


if __name__=='__main__': unittest.main()
