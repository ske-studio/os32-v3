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
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

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
                     'NHD_LOCAL'):
            self.saved[name] = getattr(nd, name)
        self.saved['MOUNT_POINT'] = nd.MOUNT_POINT
        # 実物の NHD を読まない: NHD_LOCAL は試験ごとの一時の名前 (作らない)、
        # 旧配置の門は通す贋物
        nd.NHD_LOCAL = str(self.root / 'os32.nhd')
        nd.legacy_pt_guard = lambda *a, **kw: True
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


if __name__ == '__main__':
    unittest.main()
