#!/usr/bin/env python3
"""Lightweight preflight fixtures; no network or OS build."""
CHECK = dict(name='preflight-host', tier=1,
    covers=['tools/preflight.sh', 'tools/fetch_fonts.py'],
    needs=[], budget=4.009738)

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PreflightTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.main = self.root / 'main'
        self.main.mkdir()
        self.git('init', '-q', cwd=self.main)
        self.git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                 'commit', '--allow-empty', '-qm', 'fixture', cwd=self.main)
        self.work = self.root / 'work'
        self.git('worktree', 'add', '-qb', 'fixture', str(self.work), cwd=self.main)
        (self.work / 'tools').mkdir()
        shutil.copyfile(ROOT / 'tools/preflight.sh', self.work / 'tools/preflight.sh')
        self.fonts = self.main / 'assets/fonts'
        self.fonts.mkdir(parents=True)
        data = b'verified fixture font'
        self.digest = hashlib.sha256(data).hexdigest()
        (self.fonts / 'ipaexg.ttf').write_bytes(data)
        (self.fonts / 'ipaexm.ttf').write_bytes(data)
        (self.work / 'tools/fetch_fonts.py').write_text(
            'MEMBERS = ' + repr({n: (n, self.digest) for n in ('ipaexg.ttf', 'ipaexm.ttf')}))
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.command('i386-elf-gcc', 'exit 0')
        self.command('qemu-i386', 'exit 0')
        # Keep filesystem assertions independent of the test host's /tmp mount.
        self.command('df', 'case "$1" in --output=fstype) echo fstype; echo ext4;; '
                     '*) echo avail; echo 4000000000;; esac')
        out = self.work / 'build/out'
        out.mkdir(parents=True)
        for name in ('vmkernel.lz4', 'kernel.map'):
            (out / name).touch()
        self.env = os.environ.copy()
        self.env.update(CROSS_DIR=str(self.root), TMPDIR=str(self.root),
                        HOST32_RUNNERS='qemu', PATH=str(self.bin) + ':' + self.env['PATH'])

    def git(self, *args, cwd):
        return subprocess.run(['git', *args], cwd=cwd, check=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True).stdout

    def command(self, name, body):
        path = self.bin / name
        path.write_text('#!/bin/bash\n' + body + '\n')
        path.chmod(0o755)

    def run_preflight(self, *args):
        # The fixture itself may live on /tmp. Mock only path classification
        # and storage probes; the production script still rejects real /tmp.
        self.command('realpath', 'case "$3" in /tmp) echo /tmp;; *) echo .;; esac')
        self.command('mktemp', 'exec /usr/bin/mktemp -d "' + str(self.root) + '/probe.XXXXXXXX"')
        return subprocess.run(['bash', 'tools/preflight.sh', *args], cwd=self.work,
                              env=self.env, capture_output=True, text=True, timeout=15)

    def test_missing_cross(self):
        self.env.pop('CROSS_DIR')
        result = self.run_preflight()
        self.assertEqual(result.returncode, 1)
        self.assertIn('NG CROSS_DIR: 未設定', result.stdout)

    def test_tmp_rejected(self):
        self.env['TMPDIR'] = '/tmp'
        result = self.run_preflight()
        self.assertEqual(result.returncode, 1)
        self.assertIn('NG TMPDIR:', result.stdout)

    def test_fonts_opt_in_and_hash(self):
        result = self.run_preflight()
        self.assertEqual(result.returncode, 1)
        self.assertFalse((self.work / 'assets/fonts').exists())
        (self.fonts / 'ipaexm.ttf').write_bytes(b'wrong hash')
        result = self.run_preflight('--fix-fonts')
        self.assertEqual(result.returncode, 1)
        self.assertEqual((self.work / 'assets/fonts/ipaexg.ttf').read_bytes(),
                         (self.fonts / 'ipaexg.ttf').read_bytes())
        self.assertFalse((self.work / 'assets/fonts/ipaexm.ttf').exists())

    def test_all_ok_and_base(self):
        result = self.run_preflight('--base', 'HEAD', '--fix-fonts')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('OK worktree: linked', result.stdout)
        result = self.run_preflight('--base', 'deadbeef')
        self.assertEqual(result.returncode, 1)
        self.assertIn('NG 基点:', result.stdout)

    def test_cargo_cache_presence(self):
        # Caches outside the worktree, in build/, or under .git are excluded.
        for path in (self.main / 'target', self.work / 'build/target',
                     self.work / 'nested/.git/target'):
            path.mkdir(parents=True)
        (self.work / 'outside').symlink_to(self.main, target_is_directory=True)
        result = self.run_preflight('--fix-fonts')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('OK 試験キャッシュ: cargo target/ 0 個 (main の実木と状態が違いうる)',
                      result.stdout)
        caches = (self.work / 'userland/rust/example/target', self.work / 'sdk/target')
        for path in caches:
            (path / 'debug/nested/target').mkdir(parents=True)
        result = self.run_preflight()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('OK 試験キャッシュ: cargo target/ 2 個 (main の実木と状態が違いうる)',
                      result.stdout)
        for path in caches:
            self.assertTrue((path / 'debug/nested/target').is_dir())

    def test_dotenv_literal_and_env_precedence(self):
        secret = 'SECRET_DO_NOT_PRINT'
        (self.work / '.env').write_text('TOKEN=' + secret + '\nexport CROSS_DIR="' +
                                      str(self.root) + '" # literal\n')
        self.env.pop('CROSS_DIR')
        result = self.run_preflight('--fix-fonts')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn(secret, result.stdout + result.stderr)
        self.env['CROSS_DIR'] = '/missing'
        result = self.run_preflight()
        self.assertIn('NG CROSS_DIR: コンパイラ実行不可', result.stdout)
        (self.work / '.env').write_text('CROSS_DIR=$(touch leaked)\n')
        self.env.pop('CROSS_DIR')
        self.run_preflight()
        self.assertFalse((self.work / 'leaked').exists())

    def test_old_artifact(self):
        os.utime(self.work / 'build/out/kernel.map', (1, 1))
        result = self.run_preflight('--fix-fonts')
        self.assertEqual(result.returncode, 1)
        self.assertIn('kernel.map が HEAD より古い', result.stdout)


if __name__ == '__main__':
    unittest.main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_preflight.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
