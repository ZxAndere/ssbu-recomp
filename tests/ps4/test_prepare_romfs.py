"""Private preparation boundaries tested with original synthetic bytes only."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/ps4'))
try:
    import prepare_romfs as prepare
except ImportError:
    prepare=None


@unittest.skipIf(prepare is None,'zstandard and pycryptodome are required')
class PrepareRomfsTests(unittest.TestCase):
    def test_output_and_entry_boundaries(self):
        for path in (ROOT,ROOT/'runtime'/'private',ROOT.parent.anchor):
            with self.assertRaises(ValueError): prepare.private_output(Path(path))
        root=(ROOT/'build_ps4'/'synthetic-romfs').resolve()
        self.assertEqual(prepare.safe_target(root,'/folder/file.bin'),root/'folder/file.bin')
        for name in ('../escape','folder/../escape','a//b','a\\b','C:escape',''):
            with self.assertRaises(ValueError): prepare.safe_target(root,name)

    def test_resume_verifies_content_id_and_detects_cache_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); source=root/'owned.nsp'; source.write_bytes(b'synthetic package')
            cache=root/'cache'; cache.mkdir()
            data=b'original synthetic NCA bytes'*200
            checksum=hashlib.sha256(data).hexdigest(); name=checksum[:32]+'.nca'
            class Package:
                def __init__(self): self.files={name:(0,len(data))}; self.f=source.open('rb')
                def read(self,entry,offset,size): return data[offset:offset+size]
            class Nca:
                def __init__(self,package): self.name=name; self.header=bytes(0x220)
            partial=cache/(name[:-4]+'.part'); partial.write_bytes(data[:100])
            identity={'source':str(source.resolve()),'source_size':source.stat().st_size,
                      'source_mtime_ns':source.stat().st_mtime_ns,'entry':name,'size':len(data)}
            state=cache/(name[:-4]+'.json'); state.write_text(json.dumps({'input':identity,'complete':False}))
            with patch.object(prepare.rx,'open_package',side_effect=lambda _:Package()),patch.object(prepare.rx,'Nca',Nca):
                nca,digest=prepare.cached_program(source,cache)
                self.assertEqual(digest,checksum)
                self.assertEqual(nca.nsp.read(name,17,100),data[17:117]); nca.nsp.file.close()
                self.assertTrue(json.loads(state.read_text())['complete'])
                (cache/name).write_bytes(b'X'+data[1:])
                with self.assertRaisesRegex(ValueError,'integrity'): prepare.cached_program(source,cache)
