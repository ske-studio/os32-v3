"""T2h: isolated names, real pull/copy/sync/deploy and set verification.

Only mount, raw boot and sudo are faked. Remote is a JSON file representing a
writable guest filesystem; starting it changes only remote, never local.
"""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import nhd_deploy as nd
import nhd_profile as profile


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='nhd-profile-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.mount = self.root / 'mnt'
        self.mount.mkdir()
        self.remote_dir = self.root / 'remote'
        self.remote_dir.mkdir()
        self.patches = contextlib.ExitStack()
        self.addCleanup(self.patches.close)
        self.patches.enter_context(patch.dict(os.environ, {}, clear=True))
        for name, value in [('PROJ_DIR', str(self.root)), ('NP21W_DIR', str(self.remote_dir)),
                            ('PROFILE', None), ('NHD_LOCAL', nd.NHD_LOCAL),
                            ('NHD_REMOTE', nd.NHD_REMOTE), ('MOUNT_POINT', nd.MOUNT_POINT)]:
            self.patches.enter_context(patch.object(nd, name, value))
        self.patches.enter_context(patch.object(profile, 'MOUNT', str(self.mount)))
        nd.configure_profile('t2h')
        self.remote = Path(nd.NHD_REMOTE)
        self.local = Path(nd.NHD_LOCAL)
        self.remote.write_text('{}')
        self.mounted = False
        self.patches.enter_context(patch.object(nd, 'is_mounted', lambda: self.mounted))
        self.patches.enter_context(patch.object(nd, 'do_mount', self.mount_image))
        self.patches.enter_context(patch.object(nd, 'do_umount', self.umount_image))
        self.patches.enter_context(patch.object(nd, 'legacy_pt_guard', lambda **kw: True))
        self.patches.enter_context(patch.object(nd, 'do_write_boot', lambda _p: True))
        self.patches.enter_context(patch.object(nd.subprocess, 'run', self.command))

    def command(self, args, **kw):
        if args[0] == 'sync':
            pass
        elif args[:3] == ['sudo', 'cp', '--']:
            shutil.copyfile(args[-2], args[-1])
        elif args[:3] == ['sudo', 'mkdir', '-p']:
            Path(args[-1]).mkdir(parents=True, exist_ok=True)
        else:
            raise AssertionError('unexpected host command: ' + repr(args))
        return subprocess.CompletedProcess(args, 0, '', '')

    def mount_image(self):
        if self.mounted:
            return True
        for p in self.mount.rglob('*'):
            if p.is_file():
                p.unlink()
        for path, data in json.loads(self.local.read_text()).items():
            p = self.mount / path.lstrip('/')
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(bytes.fromhex(data))
        self.mounted = True
        return True

    def umount_image(self):
        if self.mounted:
            self.local.write_text(json.dumps({
                '/' + p.relative_to(self.mount).as_posix(): p.read_bytes().hex()
                for p in self.mount.rglob('*') if p.is_file()}, sort_keys=True))
        self.mounted = False
        return True

    def manifest(self):
        entries, expected = [], []
        for host, guest, data in [
            ('build/out/vmkernel.lz4', '/boot/vmkernel.lz4', b'kernel'),
            ('build/out/unicode.bin', '/sys/unicode.bin', b'unicode'),
            ('build/out/libos32gui.shlib', '/sys/lib/libos32gui.shlib', b'shlib'),
            ('userland/a.bin', '/usr/bin/a.bin', b'app'),
        ]:
            src = self.root / host
            src.parent.mkdir(parents=True, exist_ok=True)
            src.write_bytes(data)
            entries.append({'host': host, 'guest': guest})
            expected.append({'host': host, 'guest': guest, 'size': len(data),
                             'sha256': hashlib.sha256(data).hexdigest()})
        cfg = {'filesystem': {'files': entries, 'directories': []}}
        self.patches.enter_context(patch.object(nd, 'load_deploy_yaml', lambda: cfg))
        (self.mount / 'etc').mkdir(exist_ok=True)
        for name in ('settings.db', 'system.cfg'):
            (self.mount / 'etc' / name).write_text('guest setting')
        data = {'format': 1, 'generation_build_id': 'L-h-test', 'kernel_commit': 'test',
                'kapi_version': 74, 'generations': {}, 'files': expected,
                'allow_list': [{'guest': p, 'check': 'exists'} for p in
                               ('/etc/settings.db', '/etc/system.cfg', '/var/log/*')]}
        self.set_file = self.root / 'deploy-set.json'
        self.set_file.write_text(json.dumps(data))
        return data

    def test_names_and_rejection(self):
        remote, local, mount, stamp = profile.paths(str(self.root), str(self.remote_dir))
        self.assertEqual(Path(remote).name, 'os32_t2h_install.nhd')
        self.assertEqual(Path(local).name, 'os32_t2h.nhd')
        self.assertEqual(stamp, local + '.pulled')
        for key, wrong in [('OS32_NHD_REMOTE', str(self.remote_dir / 'os32.nhd')),
                           ('OS32_NHD_LOCAL', str(self.root / 'build/nhd/os32.nhd')),
                           ('OS32_NHD_MOUNT', '/tmp/os32'),
                           ('OS32_NHD_STAMP', str(self.root / 'build/nhd/os32.nhd.pulled'))]:
            with patch.dict(os.environ, {key: wrong}), patch.object(sys, 'argv',
                    ['nhd_deploy.py', 'pull', '--profile', 't2h']):
                self.assertFalse(nd.main(), key)
        # Hard links and symbolic aliases also cannot select production.
        production = self.remote_dir / 'os32.nhd'
        production.write_text('production')
        self.remote.unlink()
        self.remote.symlink_to(production)
        with self.assertRaises(ValueError):
            nd.configure_profile('t2h')
        self.assertEqual(production.read_text(), 'production')

    def test_roundtrip_and_verify(self):
        self.assertTrue(nd.do_pull())
        data = self.manifest()
        self.assertTrue(nd.do_sync())
        self.assertTrue(nd.do_verify_set(str(self.set_file)))
        self.assertIn('/sys/unicode.bin', [f['guest'] for f in data['files']])
        for kind in ('missing', 'modified', 'extra', 'setting'):
            target = self.mount / 'usr/bin/a.bin'
            original = target.read_bytes()
            extra = self.mount / 'sys/lib/stale.shlib'
            if kind == 'missing': target.unlink()
            if kind == 'modified': target.write_bytes(b'bad')
            if kind == 'extra': extra.write_bytes(b'stale')
            if kind == 'setting': (self.mount / 'etc/settings.db').write_text('different')
            self.assertEqual(nd.do_verify_set(str(self.set_file)), kind == 'setting', kind)
            target.write_bytes(original)
            if extra.exists(): extra.unlink()
        self.assertTrue(nd.do_deploy())
        # Guest writes only remote. Stale local must not pass either consumer.
        remote = json.loads(self.remote.read_text())
        remote['/usr/bin/a.bin'] = b'guest modified'.hex()
        self.remote.write_text(json.dumps(remote))
        before = self.local.read_bytes()
        self.assertFalse(nd.do_deploy())
        self.assertFalse(nd.do_deploy(force=True))
        log = io.StringIO()
        with contextlib.redirect_stderr(log):
            self.assertFalse(nd.do_verify_set(str(self.set_file)))
        self.assertIn('pull 後にゲスト起動あり', log.getvalue())
        self.assertEqual(self.local.read_bytes(), before)
        self.assertTrue(nd.do_umount())
        self.assertTrue(nd.do_pull()) # Must refresh an existing local.
        self.assertFalse(nd.do_verify_set(str(self.set_file))) # Detect remote alteration now.
        self.assertTrue(nd.do_copy([str(self.root / 'userland/a.bin')], dest_dir='/usr/bin'))
        self.assertTrue(nd.do_verify_set(str(self.set_file)))
        self.assertTrue(nd.do_deploy())
        self.assertEqual(json.loads(self.remote.read_text())['/usr/bin/a.bin'], b'app'.hex())
        profile.mark_guest_started(self.root)
        self.assertTrue(json.loads(Path(nd.stamp_path()).read_text())['guest_started'])
        self.assertFalse(nd.do_verify_set(str(self.set_file)))
        self.assertTrue(nd.do_pull())
        self.assertTrue(nd.do_verify_set(str(self.set_file)))

    def test_start_invalidates_without_remote_write(self):
        self.assertTrue(nd.do_pull())
        self.manifest()
        self.assertTrue(nd.do_sync())
        self.assertTrue(nd.do_verify_set(str(self.set_file)))
        before = self.remote.read_bytes()
        profile.mark_guest_started(self.root)
        self.assertEqual(self.remote.read_bytes(), before)
        self.assertFalse(nd.do_verify_set(str(self.set_file)))
        # An unmounted verifier must not auto-pull or mount (read only).
        self.assertTrue(nd.do_umount())
        self.assertTrue(nd.do_pull())
        self.assertTrue(nd.do_umount())
        before = self.local.read_bytes()
        self.assertFalse(nd.do_verify_set(str(self.set_file)))
        self.assertEqual(self.local.read_bytes(), before)

    def test_pull_mounted_refuses(self):
        self.assertTrue(nd.do_pull())
        before = self.local.read_bytes()
        self.assertFalse(nd.do_pull())
        self.assertEqual(before, self.local.read_bytes())

    def test_set_invalid_and_symlink(self):
        self.assertTrue(nd.do_pull())
        self.manifest()
        self.assertTrue(nd.do_sync())
        (self.mount / 'usr/bin/a.bin').unlink()
        (self.mount / 'usr/bin/a.bin').symlink_to(self.root / 'userland/a.bin')
        self.assertFalse(nd.do_verify_set(str(self.set_file)))
        self.set_file.write_text('{}')
        self.assertFalse(nd.do_verify_set(str(self.set_file)))

    def test_allow_list_membership_from_set(self):
        self.assertTrue(nd.do_pull())
        data = self.manifest()
        self.assertTrue(nd.do_sync())
        journal = self.mount / 'etc/settings.db-journal'
        journal.write_text('guest journal')
        self.assertFalse(nd.do_verify_set(str(self.set_file)))
        data['allow_list'].append({'guest': '/etc/settings.db-journal', 'check': 'exists'})
        self.set_file.write_text(json.dumps(data))
        self.assertTrue(nd.do_verify_set(str(self.set_file)))
        journal.write_text('changed journal contents')
        self.assertTrue(nd.do_verify_set(str(self.set_file)))
        journal.unlink()
        self.assertFalse(nd.do_verify_set(str(self.set_file)))
        # The manifest may also omit the old default names, including all of them.
        data['allow_list'] = []
        self.set_file.write_text(json.dumps(data))
        self.assertFalse(nd.do_verify_set(str(self.set_file)))
        for name in ('settings.db', 'system.cfg'):
            (self.mount / 'etc' / name).unlink()
        self.assertTrue(nd.do_verify_set(str(self.set_file)))

    def test_allow_list_glob_is_one_level(self):
        self.assertTrue(nd.do_pull())
        data = self.manifest()
        self.assertTrue(nd.do_sync())
        data['allow_list'].append({'guest': '/etc/logs/*.log', 'check': 'exists'})
        self.set_file.write_text(json.dumps(data))
        self.assertTrue(nd.do_verify_set(str(self.set_file)))  # Zero matches is valid.
        logs = self.mount / 'etc/logs'
        logs.mkdir()
        (logs / 'guest.log').write_text('first log')
        self.assertTrue(nd.do_verify_set(str(self.set_file)))
        (logs / 'guest.log').write_text('changed log')
        self.assertTrue(nd.do_verify_set(str(self.set_file)))
        extra = logs / 'guest.txt'
        extra.write_text('not a log')
        self.assertFalse(nd.do_verify_set(str(self.set_file)))
        extra.unlink()
        (logs / 'nested').mkdir()
        (logs / 'nested/guest.log').write_text('not a direct child')
        self.assertFalse(nd.do_verify_set(str(self.set_file)))

    def test_allow_list_invalid_shapes(self):
        self.assertTrue(nd.do_pull())
        data = self.manifest()
        self.assertTrue(nd.do_sync())
        for invalid in [
            None, {}, [None], [{'guest': '/etc/settings.db'}],
            [{'guest': '/etc/settings.db', 'check': 'sha256'}],
            *[[{'guest': guest, 'check': 'exists'}] for guest in (
                'etc/settings.db', '/', '//etc/settings.db', '/etc/../settings.db',
                '/etc/./settings.db', '/etc//settings.db', '/etc/settings.db/',
                '/etc/**', '/etc/*/log', '/etc/log?.txt', '/etc/log[12]', '/etc/\x00')],
        ]:
            with self.subTest(allow_list=invalid):
                data['allow_list'] = invalid
                self.set_file.write_text(json.dumps(data))
                with patch.object(sys, 'argv', ['nhd_deploy.py', 'verify-set',
                                               '--set', str(self.set_file)]):
                    self.assertFalse(nd.main())


def real_set(path):
    expected = json.loads(Path(path).read_text())
    case = ProfileTests()
    case.setUp()
    try:
        assert nd.do_pull()
        # Use real merged deploy definitions and bytes through do_sync.
        entries = []
        for item in expected['files']:
            source = ROOT / item['host']
            dest = case.root / item['host']
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, dest)
            entries.append({'host': item['host'], 'guest': item['guest']})
        cfg = nd._load_merged([str(ROOT / rel) for rel in nd.CORE_MANIFEST_RELPATHS])
        assert cfg
        case.patches.enter_context(patch.object(nd, 'load_deploy_yaml', lambda: cfg))
        for item in expected['allow_list']:
            if '*' not in item['guest']:
                guest_file = case.mount / item['guest'].lstrip('/')
                guest_file.parent.mkdir(parents=True, exist_ok=True)
                guest_file.write_text('guest-owned')
        assert nd.do_sync()
        assert nd.do_verify_set(str(Path(path).resolve()))
        assert any(item['guest'] == '/sys/unicode.bin' for item in expected['files'])
        print('h1 deploy-set integration: PASS, {} files'.format(len(entries)))
    finally:
        case.doCleanups()


def main():
    # Normal suite also runs first in mutation mode.
    result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(ProfileTests))
    if not result.wasSuccessful(): return 1
    if '--set' in sys.argv:
        real_set(sys.argv[sys.argv.index('--set') + 1])
    if '--mutate' in sys.argv:
        mutations = [('stamp bypass', 'verify_pull_stamp', lambda: (True, '')),
                     ('verify bypass', 'do_verify_set', lambda _p: True),
                     ('pull skips refresh', 'do_pull', lambda: nd.ensure_local_nhd())]
        for name, attr, value in mutations:
            with patch.object(nd, attr, value), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                result = unittest.TextTestRunner(stream=io.StringIO()).run(
                    unittest.defaultTestLoader.loadTestsFromTestCase(ProfileTests))
            if result.wasSuccessful():
                raise AssertionError('survived: ' + name)
            print('RED: ' + name)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
