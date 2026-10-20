import sys
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/ps4'))
from boot_probe import summarize


class BootProbeTests(unittest.TestCase):
    def test_controlled_stop_is_not_reported_as_gameplay(self):
        output='GAME PHASE: nninitStartup\nGAME CONSTRUCTOR: 0 at 0x7100000020\nGAME CONSTRUCTOR: 1 at 0x7100000044\nGAME BOOT: execution status=-6 unresolved=0 x0=7 stop=missing API\n'
        report=summarize(output,1)
        self.assertEqual(report['constructors_completed'],1)
        self.assertFalse(report['nnmain_entered'])
        self.assertFalse(report['gameplay_verified'])
        self.assertEqual(report['guest_status'],-6)

    def test_crash_or_absent_result_is_rejected(self):
        for text,code in [('',0),('GAME BOOT: starting',1),('GAME BOOT: execution status=0 unresolved=0 x0=0 stop=',-11)]:
            with self.assertRaises(RuntimeError): summarize(text,code)

    def test_explicit_completions_survive_stop_after_last_constructor(self):
        output='GAME CONSTRUCTOR: 0 at 0x7100000020\nGAME CONSTRUCTOR DONE: 0\nGAME BOOT: execution status=-6 unresolved=0 x0=0 stop=nnMain export absent\n'
        self.assertEqual(summarize(output,1)['constructors_completed'],1)
        self.assertTrue(summarize(output,1)['constructor_completion_trace'])
