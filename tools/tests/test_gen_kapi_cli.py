"""X-8: CLI の読み取り操作は生成物の内容・mtime を変えない。"""
import os
import shutil
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'sdk/gen_kapi.py'


class KapiCli(unittest.TestCase):
    def test_logical_metadata_and_rejections(self):
        import re
        spec = json.loads((ROOT / 'sdk/kapi.json').read_text())
        subprocess.run([sys.executable, '-B', str(SCRIPT)], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL)
        internal = (ROOT / 'include/kapi_call_generated.h').read_text()
        fixed = [[a for a in api.get('args', []) if a != '...'] for api in spec['api']]
        self.assertIn('#define KAPI_FIXED_MAX %du' % max(map(len, fixed)), internal)
        counts = internal.split('kapi_fixed_count[] = {', 1)[1].split('};', 1)[0]
        self.assertEqual([int(n) for n in re.findall(r'^    (\d+),', counts, re.M)], list(map(len, fixed)))
        rows = internal.split('kapi_fixed_kind[][KAPI_FIXED_MAX] = {', 1)[1].split('};', 1)[0]
        self.assertEqual(len(rows.strip().splitlines()), len(fixed))
        for row, declarations in zip(rows.strip().splitlines(), fixed):
            kinds = re.findall(r'KAPI_VALUE_\w+', row)
            self.assertEqual(kinds, ['KAPI_VALUE_POINTER' if '*' in a else 'KAPI_VALUE_INTEGER' for a in declarations])
        with tempfile.TemporaryDirectory(prefix='kapi-kinds-') as d:
            path = Path(d) / 'kapi.json'
            for declaration in ('struct Packet value', 'u64 value', 'double value', 'float value'):
                bad = json.loads(json.dumps(spec))
                bad['api'][0]['args'] = [declaration]
                path.write_text(json.dumps(bad))
                result = subprocess.run([sys.executable, '-B', str(SCRIPT), '--check', str(path)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, declaration)
                self.assertIn('unsupported x86 by-value', result.stderr)
            result = subprocess.run([sys.executable, '-B', str(SCRIPT), '--check', '--target', 'ppc-powermac'], cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('unsupported', result.stderr)

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

            # Provider retargeting changes only the kernel wrapper artifact.
            # Public names, slots, SDK/Rust declarations and exec init stay fixed.
            spec_path = root / 'sdk/kapi.json'
            original = spec_path.read_bytes()
            spec = json.loads(original)
            for entry in spec['api']:
                target = entry.get('target', '')
                if target.startswith('input_'):
                    entry['target'] = 'kbd_' + target[len('input_'):]
                elif target.startswith('port_'):
                    entry['target'] = 'serial_' + target[len('port_'):]
                elif target.startswith('pointer_'):
                    entry['target'] = 'mouse_' + target[len('pointer_'):]
                elif entry['name'] == 'mouse_poll':
                    self.assertEqual(entry['body'], 'pointer_poll((MouseState *)info);')
                    entry['body'] = 'mouse_poll((MouseState *)info);'
            spec_path.write_text(json.dumps(spec))
            result = run([])
            self.assertEqual(result.returncode, 0, result.stderr)
            legacy = state()
            for name, (data, _) in before.items():
                if name not in ('sdk/kapi.json', 'kapi/kapi_generated.c'):
                    self.assertEqual(legacy[name][0], data, name)
            self.assertNotEqual(legacy['kapi/kapi_generated.c'][0],
                                before['kapi/kapi_generated.c'][0])
            spec_path.write_bytes(original)
            self.assertEqual(run([]).returncode, 0)
            first = {name: data for name, (data, _) in state().items()}
            self.assertEqual(run([]).returncode, 0)
            self.assertEqual(first, {name: data for name, (data, _) in state().items()})


def verify_mutations():
    variants = (
        ('logical-max', 'sdk/gen_kapi.py',
         'fixed_max = max(map(len, fixed_args), default=0)',
         'fixed_max = max(map(len, fixed_args), default=0) + 1'),
        ('unknown-value-type', 'sdk/kapi_abi_x86.py',
         'if kind not in _SCALARS:', 'if False:'),
    )
    for name, source, old, new in variants:
        with tempfile.TemporaryDirectory(prefix='kapi-cli-mutant-') as d:
            root = Path(d)
            for directory in ('tools/tests', 'sdk/include/os32', 'sdk/rust/os32api/src',
                              'sdk/crt', 'sdk/link', 'kapi', 'exec', 'build', 'include'):
                (root / directory).mkdir(parents=True, exist_ok=True)
            paths = ('tools/tests/test_gen_kapi_cli.py', 'sdk/gen_kapi.py',
                     'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sdk/kapi.json',
                     'sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'build/os32.ld')
            for path in paths:
                shutil.copy2(ROOT / path, root / path)
            target = root / source
            text = target.read_text()
            assert text.count(old) == 1, name
            target.write_text(text.replace(old, new))
            result = subprocess.run([sys.executable, '-B', 'tools/tests/test_gen_kapi_cli.py'],
                                    cwd=root, capture_output=True, text=True)
            assert result.returncode == 1 and 'AssertionError' in result.stderr, (name, result.stderr)
            print('runtime RED product generator: ' + name)


if __name__ == '__main__':
    suite = unittest.main(argv=[sys.argv[0]], exit=False)
    if not suite.result.wasSuccessful():
        sys.exit(1)
    if '--mutate' in sys.argv:
        verify_mutations()
