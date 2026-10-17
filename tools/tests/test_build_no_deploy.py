"""ビルド目標の対の部品と、X-7 の明示的な FD 配備を確認する。"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BuildNoDeploy(unittest.TestCase):
    def test_kernel_paired_modules(self):
        result = subprocess.run(['make', '-qp'], cwd=ROOT,
                                capture_output=True, text=True)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        for target, out in (('kernel', 'build/out'),
                            ('kernel-r1', 'build/out/r1')):
            with self.subTest(target=target):
                module = out + '/kdebug.bin'
                for name, prerequisite in ((target, module),
                                           (module, out + '/vmkernel.lz4')):
                    rule = re.search(r'^' + re.escape(name) + r': (.+)$',
                                     result.stdout, re.M)
                    self.assertIsNotNone(rule, name)
                    self.assertIn(prerequisite, rule.group(1).split())

    def test_all_and_fd_deploy(self):
        with tempfile.TemporaryDirectory(prefix='os32-build-no-deploy-') as tmp:
            for target in ('all', 'deploy-fd'):
                with self.subTest(target=target):
                    result = subprocess.run(
                        ['make', '-B', '-n', '-j1', target, 'NP21W_DIR=' + tmp],
                        cwd=ROOT, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    if target == 'all':
                        self.assertFalse(tmp in result.stdout,
                                         'ビルドが NP21W_DIR を配備先に使っている')
                    else:
                        for name in ('os32_boot.d88', 'os32_boot144.img'):
                            self.assertIn("cp images/%s '%s/%s'" % (name, tmp, name),
                                          result.stdout)
            self.assertEqual(list(Path(tmp).iterdir()), [])


if __name__ == '__main__':
    unittest.main()
