"""X-8: CLI の読み取り操作は生成物の内容・mtime を変えない。"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'sdk/gen_kapi.py'


class KapiCli(unittest.TestCase):
    def test_read_only_arguments(self):
        with tempfile.TemporaryDirectory(prefix='os32-kapi-cli-') as tmp:
            root = Path(tmp)
            for sub in ('sdk/include/os32', 'sdk/rust/os32api/src',
                        'sdk/crt', 'sdk/link', 'kapi', 'exec'):
                (root / sub).mkdir(parents=True)
            (root / 'build').mkdir()
            for name in ('sdk/link/app.ld', 'sdk/link/app_sys.ld',
                         'sdk/link/shlib.ld', 'build/os32.ld'):
                (root / name).write_bytes((ROOT / name).read_bytes())
            (root / 'sdk/kapi.json').write_bytes((ROOT / 'sdk/kapi.json').read_bytes())
            # 既定の生成を実行して、生成物一式を動的に拾う。
            def run(args):
                return subprocess.run([sys.executable, '-B', str(SCRIPT), *args],
                                      cwd=root, capture_output=True, text=True)
            result = run([])
            self.assertEqual(result.returncode, 0, result.stderr)
            outputs = [p for p in root.rglob('*') if p.is_file()]
            for p in outputs:
                os.utime(p, ns=(1000000000, 1000000000))
            def state():
                return {str(p.relative_to(root)): (p.read_bytes(), p.stat().st_mtime_ns)
                        for p in root.rglob('*') if p.is_file()}
            before = state()
            for args, expected_rc in ((['--help'], 0), (['--check'], 0),
                                      (['--check-only'], 0), (['--unknown'], 2),
                                      (['--check-o'], 2),
                                      (['sdk/kapi.json', 'extra.json'], 2)):
                with self.subTest(args=args):
                    for p in outputs:
                        os.utime(p, ns=(1000000000, 1000000000))
                    result = run(args)
                    self.assertEqual(result.returncode, expected_rc, result.stderr)
                    if args == ['--help']:
                        self.assertIn('usage:', result.stdout)
                    self.assertEqual(state(), before, '生成物が書き換わった')


if __name__ == '__main__':
    unittest.main()
