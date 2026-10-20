"""Compile original and split synthetic C and compare register state."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/ps4'))
from link_game import split_large_functions,add_checkpoints
from build import run


class SplitTests(unittest.TestCase):
    def test_native_split_matches_unsplit_control_flow(self):
        config=ROOT/'build_ps4/toolchain.json'
        if not config.is_file(): self.skipTest('Build host target first')
        clang=Path(json.loads(config.read_text())['llvm_bin'])/'clang.exe'
        base=0x7100000020
        lines=['#include "runtime.h"','void f_7100000020(Ctx *c) {','  c->fn = 0x7100000020ULL;','  uint64_t jt = 0;']
        for i in range(512):
            statement='c->x[0] = c->x[0]*3+%dULL;'%i
            if i==0: statement+='c->x[1]=3;'
            if i==17: statement+='goto L_%x;'%(base+120*4)
            if i==256: statement+='jt=c->x[3]; goto L_dispatch;'
            if i==511: statement+='if (--c->x[1]) goto L_%x; return;'%(base+300*4)
            lines.append('L_%x: %s'%(base+i*4,statement))
        lines+=['L_dispatch: switch (jt) {']
        lines+=['case 0x%xULL: goto L_%x;'%(base+i*4,base+i*4) for i in range(512)]
        lines+=['default: call_indirect(c,jt); return; }','}']
        original='\n'.join(lines)+'\n'
        split=add_checkpoints(split_large_functions(original,128))
        self.assertIn('sp_piece_7100000020_3',split)
        self.assertLess(split.count('SP_GUEST_POLL'),30)
        harness='''#include "runtime.h"
#include <stdlib.h>
uint64_t sp_recomp_budget=1000000;
void sp_recomp_checkpoint(uint64_t pc) { (void)pc; exit(2); }
void guest_trap(const char *what,uint64_t pc) { (void)what; (void)pc; exit(3); }
void call_indirect(Ctx *c,uint64_t pc) { (void)c; (void)pc; exit(4); }
void reference_fn(Ctx *c); void f_7100000020(Ctx *c);
int main(void) {
 for (unsigned i=0;i<1000;++i) {
  Ctx a={0},b={0}; a.x[0]=(uint64_t)i*7919;
  a.x[3]=0x7100000020ULL+(257+i%240)*4; b=a;
  reference_fn(&a); f_7100000020(&b);
  if (memcmp(&a,&b,sizeof a)) return 5;
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory(dir=ROOT/'build_ps4') as folder:
            out=Path(folder)
            (out/'original.c').write_text(original.replace('f_7100000020','reference_fn'))
            (out/'split.c').write_text(split); (out/'main.c').write_text(harness)
            command=[str(clang),'-O0','-std=c17','-I',str(ROOT/'runtime'),'-include',str(ROOT/'runtime/platform/guest_bridge.h'),
                     str(out/'original.c'),str(out/'split.c'),str(out/'main.c'),'-o',str(out/'test.exe')]
            run(command,capture=True,timeout=60)
            subprocess.run([str(out/'test.exe')],capture_output=True,check=True,timeout=10)


if __name__=='__main__': unittest.main()
