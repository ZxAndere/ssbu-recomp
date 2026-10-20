import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/ps4'))
from lift import write_manifest
from link_game import stage
from generate_smoke import fixture


class LinkGameTests(unittest.TestCase):
    def prepare(self,folder):
        root=Path(folder);generated=root/'generated';generated.mkdir();build=root/'build';build.mkdir()
        (generated/'table.c').write_text('/* table fixture */\n')
        (generated/'imports.c').write_text('/* imports fixture */\n')
        image=root/'main.elf';image.write_bytes(fixture()[0])
        write_manifest(generated,image)
        return generated,image,build

    def test_changed_input_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            generated,image,build=self.prepare(folder)
            image.write_bytes(image.read_bytes()+b'x')
            with self.assertRaisesRegex(RuntimeError,'does not match'): stage(generated,image,build)

    def test_changed_generated_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            generated,image,build=self.prepare(folder)
            (generated/'table.c').write_text('modified')
            with self.assertRaisesRegex(RuntimeError,'source changed'): stage(generated,image,build)

    def test_extra_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            generated,image,build=self.prepare(folder)
            (generated/'stale.c').write_text('stale source')
            with self.assertRaisesRegex(RuntimeError,'file set changed'): stage(generated,image,build)

    def test_stage_preserves_original_and_adds_checkpoints(self):
        with tempfile.TemporaryDirectory() as folder:
            generated,image,build=self.prepare(folder)
            original='void test(void) { L_7100000020: goto L_7100000020; }\n'
            (generated/'table.c').write_text(original);write_manifest(generated,image)
            sources,_=stage(generated,image,build)
            self.assertEqual((generated/'table.c').read_text(),original)
            self.assertIn('SP_GUEST_POLL',next(p for p in sources if p.name=='table.c').read_text())


if __name__=='__main__': unittest.main()
