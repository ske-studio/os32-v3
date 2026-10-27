"""配備のコピー失敗が成功として報告されないことを検査する。

2026-09-10 の実障害 (POLICY_DEBUG §4-29): NHD が満杯で
`cp: No space left on device` が起きたのに `make deploy-nhd` は exit 0 で
「完了」と出し、/boot/vmkernel.lz4 は 446,464 B に切り詰められていた。
ゲストは古いカーネルで動き続けた。実ファイルシステムもエミュレータも使わない。

**実物の build/nhd/os32.nhd に左右されない** (2026-09-24): nhd_deploy は import 時に
NHD_LOCAL を決め、do_sync は旧配置の門 (legacy_pt_guard) でそれを読む。本体に
旧配置の NHD が置いてあると test_success_still_returns_true が断られて落ち、
無い worktree では通っていた。import の前に OS32_NHD_LOCAL を一時ディレクトリへ
向け、各試験でも NHD_LOCAL を差し替えて門を贋物にする (門そのものの試験は
tools/tests/test_hdd_stage1.py)。
"""
import contextlib
import inspect
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
# import より前に: 実物の build/nhd/os32.nhd を NHD_LOCAL にしない
_SESSION = tempfile.TemporaryDirectory(prefix='os32-nhd-sync-session-')
os.environ['OS32_NHD_LOCAL'] = os.path.join(_SESSION.name, 'absent.nhd')
import nhd_deploy as nd  # noqa: E402


class FakeCompleted(object):
    def __init__(self, rc, stderr=''):
        self.returncode = rc
        self.stderr = stderr
        self.stdout = ''


class SyncFailure(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='os32-nhd-sync-')
        self.root = pathlib.Path(self.tmp.name)
        self.src = self.root / 'vmkernel.lz4'
        self.src.write_bytes(b'K' * 4096)
        self.mount = self.root / 'mnt'
        (self.mount / 'boot').mkdir(parents=True)
        self.saved = {}
        for name in ('ensure_local_nhd', 'ensure_mounted', 'load_deploy_yaml',
                     'do_write_boot', 'resolve_files_from_entry', 'legacy_pt_guard',
                     'NHD_LOCAL', 'refresh_deploy_set'):
            self.saved[name] = getattr(nd, name)
        self.saved['MOUNT_POINT'] = nd.MOUNT_POINT
        # 実物の NHD を読まない: NHD_LOCAL は試験ごとの一時の名前 (作らない)、
        # 旧配置の門は通す贋物
        nd.NHD_LOCAL = str(self.root / 'os32.nhd')
        nd.legacy_pt_guard = lambda *a, **kw: True
        # Artifact regeneration is covered by test_deploy_set's temp tree.
        nd.refresh_deploy_set = lambda: True
        self.saved['run'] = subprocess.run
        nd.MOUNT_POINT = str(self.mount)
        nd.ensure_local_nhd = lambda: True
        nd.ensure_mounted = lambda: True
        nd.do_write_boot = lambda p: True
        nd.load_deploy_yaml = lambda: {
            'boot': {},
            'filesystem': {'directories': [],
                           'files': [{'host': 'build/out/vmkernel.lz4',
                                      'guest': '/boot/vmkernel.lz4',
                                      'tags': ['core']}]}}
        nd.resolve_files_from_entry = lambda e: [
            (str(self.src), '/boot/vmkernel.lz4')]

    def tearDown(self):
        for name, value in self.saved.items():
            if name == 'run':
                subprocess.run = value
            else:
                setattr(nd, name, value)
        self.tmp.cleanup()

    def _patch_cp(self, rc, partial_bytes=None):
        real = self.saved['run']
        dest = self.mount / 'boot' / 'vmkernel.lz4'

        def fake(cmd, *a, **kw):
            if isinstance(cmd, list) and cmd[:2] == ['sudo', 'cp']:
                if partial_bytes is not None:
                    dest.write_bytes(b'K' * partial_bytes)
                if rc != 0:
                    return FakeCompleted(rc, "cp: error writing '%s': "
                                             "No space left on device" % dest)
                return FakeCompleted(0)
            if isinstance(cmd, list) and cmd[:2] == ['sudo', 'rm']:
                for p in cmd[2:]:
                    if not p.startswith('-') and os.path.exists(p):
                        os.remove(p)
                return FakeCompleted(0)
            if isinstance(cmd, list) and cmd and cmd[0] in ('sync', 'sudo'):
                return FakeCompleted(0)
            return real(cmd, *a, **kw)
        subprocess.run = fake
        return dest

    def test_enospc_is_not_reported_as_success(self):
        """満杯でコピーが落ちたら False を返すこと (旧実装は True だった)。"""
        self._patch_cp(1, partial_bytes=1024)
        self.assertIs(nd.do_sync(), False)

    def test_partial_destination_is_removed(self):
        """切り詰められた成果物を置き去りにしない。

        残すとゲストは「存在するが壊れたカーネル」を掴む。消えていれば
        NOT FOUND で失敗が見える。"""
        dest = self._patch_cp(1, partial_bytes=1024)
        nd.do_sync()
        self.assertFalse(dest.exists(), '切り詰められた %s が残っている' % dest)

    def test_success_still_returns_true(self):
        self._patch_cp(0)
        self.assertIs(nd.do_sync(), True)

    def test_sealed_from_real_nhd(self):
        """試験の NHD_LOCAL はリポジトリの build/nhd を指さない。"""
        real = str(ROOT / 'build' / 'nhd')
        self.assertFalse(nd.NHD_LOCAL.startswith(real), nd.NHD_LOCAL)
        self.assertFalse(self.saved['NHD_LOCAL'].startswith(real), self.saved['NHD_LOCAL'])


class MountedImage(unittest.TestCase):
    """DEPLOY-BOOT-1: real entry points, only host commands are replaced."""
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='nhd-mounted-')
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.mount = self.root / 'mount'
        self.mount.mkdir()
        self.local = self.root / 'own tree/os32.nhd'
        self.local.parent.mkdir()
        self.local.write_bytes(b'own image')
        self.other = self.root / 'other tree/os32.nhd'
        self.other.parent.mkdir()
        self.other.write_bytes(b'other image')
        self.source = self.root / 'kernel'
        self.source.write_bytes(b'new kernel')
        self.target = self.mount / 'kernel'
        self.target.write_bytes(b'old kernel')
        self.backing = str(self.local)
        self.mounted = True
        self.calls = []
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)
        for name, value in [('NHD_LOCAL', str(self.local)),
                            ('NHD_REMOTE', str(self.other)),
                            ('MOUNT_POINT', str(self.mount)), ('PROFILE', None),
                            ('legacy_pt_guard', lambda: True)]:
            self.stack.enter_context(patch.object(nd, name, value))
        self.stack.enter_context(patch.object(nd.subprocess, 'run', self.command))
        self.stack.enter_context(patch.dict(os.environ, {'HOSTDRV_DIR': str(self.root)}))

    def command(self, args, **kwargs):
        self.calls.append(args)
        output = ''
        rc = 0
        if args[0] == 'mountpoint':
            rc = 0 if self.mounted else 1
        elif args[0] == 'findmnt':
            self.assertEqual(args[-1], str(self.mount))
            output = '/dev/loop-test\n'
        elif args[0] == 'losetup':
            if args[1] != '-j':
                self.assertEqual(args[-1], '/dev/loop-test')
                output = json.dumps({'loopdevices': [{'back-file': self.backing}]})
        elif args[:3] == ['sudo', 'losetup', '-f']:
            self.assertEqual(args[-1], str(self.local))
            output = '/dev/loop-test\n'
        elif args[:2] == ['sudo', 'mount']:
            self.assertEqual(args[-2:], ['/dev/loop-test', str(self.mount)])
            self.mounted = True
        elif args[0] == 'sync':
            pass
        elif args[:2] == ['sudo', 'cp']:
            pathlib.Path(args[-1]).write_bytes(pathlib.Path(args[-2]).read_bytes())
        else:
            raise AssertionError('unexpected host command: ' + repr(args))
        return subprocess.CompletedProcess(args, rc, output, '')

    def test_own_image_continues(self):
        alias = self.root / 'alias.nhd'
        alias.symlink_to(self.local)
        self.backing = str(alias)
        self.assertTrue(nd.do_mount())
        self.assertTrue(nd.ensure_mounted_for_kernel())
        self.assertTrue(nd.do_copy([str(self.source)]))
        self.assertEqual(self.target.read_bytes(), b'new kernel')
        self.assertFalse(any(c[:2] == ['sudo', 'mount'] for c in self.calls))

    def test_foreign_image_stops_without_writes(self):
        actions = [nd.do_mount, nd.ensure_mounted, nd.ensure_mounted_for_kernel,
                   nd.do_umount, nd.do_deploy, nd.do_format, nd.do_pull, nd.do_init,
                   lambda: nd.do_migrate_pt('loader', 'kernel'),
                   lambda: nd.do_copy([str(self.source)]),
                   nd.do_sync, nd.do_sync_from_hostdrv]
        for profile_name in (None, 't2h'):
            with patch.object(nd, 'PROFILE', profile_name):
                for action in actions:
                    with self.subTest(profile=profile_name, action=action):
                        self.calls.clear()
                        self.backing = str(self.other)
                        stamp = pathlib.Path(nd.stamp_path())
                        stamp.write_bytes(b'keep stamp')
                        log = io.StringIO()
                        with contextlib.redirect_stderr(log):
                            self.assertFalse(action())
                        self.assertIn(str(self.other), log.getvalue())
                        self.assertIn(str(self.local), log.getvalue())
                        self.assertIn('python3 tools/nhd_deploy.py umount', log.getvalue())
                        self.assertTrue(all(c[0] in ('mountpoint', 'findmnt', 'losetup')
                                            for c in self.calls), self.calls)
                        self.assertEqual(self.target.read_bytes(), b'old kernel')
                        self.assertEqual(self.local.read_bytes(), b'own image')
                        self.assertEqual(self.other.read_bytes(), b'other image')
                        self.assertEqual(stamp.read_bytes(), b'keep stamp')

    def test_unmounted_mounts_own_image(self):
        self.mounted = False
        self.assertTrue(nd.ensure_mounted())
        self.assertTrue(self.mounted)
        self.assertTrue(any(c[:3] == ['sudo', 'losetup', '-f'] for c in self.calls))
        self.assertFalse(any(c[0] == 'findmnt' for c in self.calls))

    def test_unknown_backing_refuses(self):
        with patch.object(nd.subprocess, 'run', return_value=
                          subprocess.CompletedProcess([], 1, '', 'unavailable')):
            self.assertFalse(nd.verify_mounted_nhd())


def main():
    suite = lambda: unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner().run(suite())
    if not result.wasSuccessful():
        return 1
    if '--mutate' in sys.argv:
        source = inspect.getsource(nd.verify_mounted_nhd)
        original = 'if backing is not None and backing == expected:'
        assert source.count(original) == 1
        namespace = dict(nd.__dict__)
        exec(source.replace(original, 'if True:'), namespace)
        with patch.object(nd, 'verify_mounted_nhd', namespace['verify_mounted_nhd']), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = unittest.TextTestRunner(stream=io.StringIO()).run(
                unittest.defaultTestLoader.loadTestsFromName(
                    'test_foreign_image_stops_without_writes', MountedImage))
        if result.wasSuccessful():
            raise AssertionError('survived: mounted NHD comparison removed')
        if result.errors:
            raise AssertionError('mutation must fail assertions, not crash: ' + str(result.errors))
        print('RED: mounted NHD comparison removed (one line)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
