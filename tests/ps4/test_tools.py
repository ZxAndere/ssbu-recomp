"""Host-independent checks for target selection and package validation failures."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("ps4_build", ROOT / "scripts/ps4/build.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class ToolTests(unittest.TestCase):
    def test_source_checkout_is_not_an_sdk(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(RuntimeError, "Incomplete OpenOrbis"):
                build.ps4_requirements(folder)

    def test_elf_machine_and_type(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "test.elf"
            header = bytearray(64)
            header[:6] = b"\x7fELF\x02\x01"
            for kind, machine, accepted in [(3, 62, True), (3, 183, False), (2, 62, False)]:
                with self.subTest(kind=kind, machine=machine):
                    struct.pack_into("<HH", header, 16, kind, machine)
                    path.write_bytes(header)
                    if accepted:
                        build.verify_elf(path)
                    else:
                        with self.assertRaises(RuntimeError):
                            build.verify_elf(path)

    def test_truncated_and_coff_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "bad.elf"
            for data in (b"\x7fELF", b"MZ" + bytes(62)):
                with self.subTest(data=data[:4]):
                    path.write_bytes(data)
                    with self.assertRaises(RuntimeError):
                        build.verify_elf(path)

    def test_validator_exit_zero_does_not_hide_bad_hash(self):
        valid = "\n".join("[OK] " + name for name in
                          ("PKG Header Signature", "Body Digest", "PFS Image Digest"))
        build.verify_pkg_output(valid)
        for marker in ("[ERROR] Hash invalid", "[FAIL] checksum", "[WARN] cannot validate"):
            with self.subTest(marker=marker):
                with self.assertRaises(RuntimeError):
                    build.verify_pkg_output(valid + "\n" + marker)

    def test_incomplete_validator_output_is_rejected(self):
        for output in ("", "[OK] PKG Header Signature", "tool started"):
            with self.subTest(output=output):
                with self.assertRaises(RuntimeError):
                    build.verify_pkg_output(output)


if __name__ == "__main__":
    unittest.main()
