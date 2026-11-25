"""Host regression for tagged messages interrupting the module probe."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'accept'))
from ppc_module import module_lines


class ModuleSerialTest(unittest.TestCase):
    def test_probe_output(self):
        expected = ['[module] fep 1 2 3 4 5 6 7', '[module] result=0 starts=1']
        serial = '[usb] ready\r\nboot> ' + '\r\n'.join(expected) + '\r\n'
        self.assertEqual(module_lines(serial), expected)
        interrupted = serial.replace('2 3', '[usb] root 0:13.0 port=1 connected=1\r\n2 3')
        interrupted = interrupted.replace('starts=', 'sta[timer] tick\r\nrts=')
        self.assertEqual(module_lines(interrupted), expected)


if __name__ == '__main__':
    unittest.main()
