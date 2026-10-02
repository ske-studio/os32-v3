"""Explicit host runner selection and rejection of replaced native execution."""
import pathlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import host32


class Host32(unittest.TestCase):
    def test_native_standard_library(self):
        host32.verify_native()

    def test_binfmt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'status').write_text('enabled\n')
            entry = root / 'qemu-i386'
            magic = '7f454c4601010100000000000000000002000300'
            entry.write_text('enabled\noffset 0\nmagic ' + magic + '\n')
            with self.assertRaisesRegex(RuntimeError, 'binfmt handler'):
                host32.verify_binfmt(root)
            entry.write_text('disabled\noffset 0\nmagic ' + magic + '\n')
            host32.verify_binfmt(root)
            entry.write_text('enabled\noffset 0\nmagic ' + magic[:-4] + '3e00\n')
            host32.verify_binfmt(root)
        host32.verify_binfmt(root)

    def test_native_replaced_run(self):
        with patch.object(subprocess, 'run', lambda *a, **kw: None):
            with self.assertRaisesRegex(RuntimeError, 'subprocess was replaced'):
                host32.verify_native()

    def test_native_startup_replaced_popen(self):
        # A sitecustomize hook can replace Popen before host32 is imported.
        # No saved baseline is needed: compare implementation file origins.
        with patch.object(subprocess, 'Popen', lambda *a, **kw: None):
            with self.assertRaisesRegex(RuntimeError, 'subprocess was replaced'):
                host32.verify_native()

    def test_qemu_command_and_signal(self):
        with tempfile.TemporaryDirectory(prefix='os32-runner-') as directory:
            exe = pathlib.Path(directory, 'fixture')
            exe.write_bytes(b'\x7fELF\x01' + bytes(13) + b'\x03\x00')
            with patch.object(host32.shutil, 'which', return_value='/qemu-i386'):
                self.assertEqual(host32.command([str(exe), 'arg'], 'qemu'),
                                 ['/qemu-i386', str(exe), 'arg'])
                with patch.object(subprocess, 'run', return_value=
                                  subprocess.CompletedProcess([], -31)):
                    with self.assertRaisesRegex(RuntimeError, 'not a test verdict'):
                        host32.run([str(exe)], runner='qemu')
                from test_access_walk import run_host32
                with patch.object(subprocess, 'run', return_value=
                                  subprocess.CompletedProcess([], -11)):
                    self.assertEqual(run_host32(exe, 'qemu').returncode, -11)
            with self.assertRaises(ValueError):
                host32.command([str(exe)], 'unknown')

    def test_lp64_command_stays_direct(self):
        with tempfile.TemporaryDirectory(prefix='os32-runner-') as directory:
            exe = pathlib.Path(directory, 'fixture')
            exe.write_bytes(b'\x7fELF\x02' + bytes(15))
            self.assertEqual(host32.command([str(exe)], 'qemu'), [str(exe)])


if __name__ == '__main__':
    unittest.main()
