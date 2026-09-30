"""Host-only tests for tools/np21w_ctl.py.

Windows 側の操作 (CIM / taskkill / PowerShell) は FakeOps、aidebug の HTTP は
FakeHttp、時計と sleep は FakeClock に差し替える。実プロセス・実 ini・
ネットワークには触れない。

  python3 -B tools/tests/test_np21w_ctl.py            # 全ケース
  python3 -B tools/tests/test_np21w_ctl.py --mutate   # 否定側 (変異 + 恒等の対照)

`make check-tools-host` は unittest discover でケースだけを、
`make check-np21w-ctl-host` は変異まで回す。
"""
import base64
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
from urllib.parse import parse_qs

HERE = Path(__file__).resolve()
SRC = HERE.parents[1] / 'np21w_ctl.py'
sys.path.insert(0, str(HERE.parents[1]))
import np21w_ctl as ctl  # noqa: E402  (--mutate が写しに差し替える)


WIN = 'C:\\NP21'
EXE = WIN + '\\np21x64w.exe'
NHD = WIN + '\\os32.nhd'
ISO = WIN + '\\os32_install.iso'
D88 = WIN + '\\os32_boot.d88'
PID = 4242
TOKEN = 'f0' * 32
TOKEN_NAME = 'np21w_aidebug_8025.token'


class FakeClock(object):
    def __init__(self):
        self.now = 0.0
        self.events = []

    def clock(self):
        return self.now

    def sleep(self, sec):
        self.events.append(('sleep', sec))
        self.now += sec


class FakeOps(object):
    """running: 走っているプロセス。vanish: {pid: 何回目の一覧から消えるか}。
    lock_seq: {path: 'FLL…'} probe の n 回目の状態 (尽きたら最後の文字)。
    locks: {path: n} n 回目の probe まではロック。"""

    def __init__(self, fc, running=(), vanish=None, kill_works=True,
                 lock_seq=None, locks=None, missing=(), dies_after=None,
                 start_exe=EXE, probe_cost=0.0, probe_error=None):
        self.fc = fc
        self.running = {p.pid: p for p in running}
        self.vanish = dict(vanish or {})
        self.kill_works = kill_works
        self.lock_seq = dict(lock_seq or {})
        self.locks = dict(locks or {})
        self.missing = set(missing)
        self.dies_after = dies_after
        self.start_exe = start_exe
        self.probe_cost = probe_cost
        self.probe_error = probe_error
        self.list_calls = 0
        self.after_start = 0
        self.probes = []
        self.probe_timeouts = []
        self.killed = []
        self.started = []

    def list_processes(self):
        self.list_calls += 1
        for pid, n in list(self.vanish.items()):
            if self.list_calls >= n:
                self.running.pop(pid, None)
                del self.vanish[pid]
        if self.started:
            self.after_start += 1
            if self.dies_after is not None and self.after_start > self.dies_after:
                self.running.pop(PID, None)
        return list(self.running.values())

    def kill(self, pid):
        self.killed.append(pid)
        if self.kill_works:
            self.running.pop(pid, None)

    def probe(self, paths, timeout=60):
        if self.probe_error:
            raise self.probe_error
        self.fc.events.append(('probe', tuple(paths)))
        self.probes.append(list(paths))
        self.probe_timeouts.append(timeout)
        self.fc.now += self.probe_cost
        n = len(self.probes)
        out = {}
        for p in paths:
            if p in self.missing:
                out[p] = 'missing'
            elif p in self.lock_seq:
                seq = self.lock_seq[p]
                out[p] = 'locked' if seq[min(n, len(seq)) - 1] == 'L' else 'free'
            elif self.locks.get(p, 0) >= n:
                out[p] = 'locked'
            else:
                out[p] = 'free'
        return out

    def start(self, exe, args, cwd):
        self.fc.events.append(('start', exe))
        self.start_time = self.fc.now
        self.started.append((exe, list(args), cwd))
        self.running[PID] = ctl.Proc(PID, 'np21x64w.exe', self.start_exe)
        return PID


class FakeHttp(object):
    """aidebug の偽物。mode: 'new' (instance あり) / 'old' (404) / 'down'。
    プロセスが 1 つも走っていなければ届かない。"""

    def __init__(self, ops, mode='new', inst_pid=None, inst_exe=EXE,
                 quit_stops=True, dialog=None, instance_dialog=False,
                 api_up_after=0, tvram=None, fdd_status=200, fdd_error='x', token=TOKEN,
                 token_file=None, fdd_delay=2, fdd_accepts=True, paused=None,
                 quit_status=200, quit_error='', latency=0.0, quit_policy=None,
                 api_version=3, cd_status=None, cd_delay=2, cd_accepts=True,
                 trap_after_cd=False):
        self.ops = ops
        self.mode = mode
        self.inst_pid = inst_pid
        self.inst_exe = inst_exe
        self.quit_stops = quit_stops
        self.dialog = dialog
        self.instance_dialog = instance_dialog
        self.api_up_after = api_up_after
        self.tvram = tvram
        self.fdd_status = fdd_status
        self.fdd_error = fdd_error      # fdd_status != 200 のときの error
        self.token = token              # 正しいトークン (None = 検査しない)
        self.token_file = token_file    # /api/instance の token_file ('' = 書けていない)
        self.fdd_delay = fdd_delay      # insert 後、何回目の instance で path が立つか
        self.fdd_accepts = fdd_accepts  # False = 受理はするが path が立たない (開けない)
        self.paused = paused            # 'trap' / 'user' = pending のまま
        self.quit_status = quit_status
        self.quit_error = quit_error
        self.latency = latency          # 1 要求あたりに進める偽の時計 (秒)
        self.quit_policy = quit_policy  # 既に受け付けた save (None = 未要求)
        # stale_after_fdd: /api/fdd の後、何回目の /api/instance までを
        # media_fresh:false (操作の前の快照) で返すか。None = いつも fresh
        self.stale_after_fdd = None
        self.fdd_snapshot = None
        self.fdd_posted_at = None
        self.fdd = {1: {'path': D88, 'cfg': D88, 'pending': False}}
        self.fdd_inserted_at = {}
        # IDE: スロット 3 が CD-ROM (OS32 の ini の IDE3TYPE=2)、起動時は空
        self.api_version = api_version
        self.cd_status = cd_status      # (status, error) を返させる / None = 通常
        self.cd_delay = cd_delay        # 差し替えの後、何回目の instance で path が立つか
        self.cd_accepts = cd_accepts    # False = 開けない (空へは 422、差し替えは path が立たない)
        self.ide = {1: {'type': 'hdd', 'path': NHD}, 2: {'type': 'none', 'path': ''},
                    3: {'type': 'cdrom', 'path': '', 'changing': False, 'next': ''},
                    4: {'type': 'none', 'path': ''}}
        self.cd_changed_at = {}
        self.stale_after_cd = None      # /api/cd の後、何回目の instance まで古い快照か
        # trap_after_cd: 受理の後にブレークで止まる。実サーバーは快照を取り直せず
        # media_fresh:false (古い快照)、trap_pause は要求の時点の値で true (api_version 3)
        self.trap_after_cd = trap_after_cd
        self.cd_snapshot = None
        self.cd_posted_at = None
        self.base = 'http://127.0.0.1:8025'
        self.calls = []

    def _pid(self):
        if self.inst_pid is not None:
            return self.inst_pid
        return next(iter(self.ops.running), None)

    def _authorized(self, headers):
        return self.token is None or (headers or {}).get('X-Aidebug-Token') == self.token

    def _fdd_json(self):
        out = []
        n = len([c for c in self.calls if c[1] == '/api/instance'])
        for d in (1, 2, 3, 4):
            f = dict(self.fdd.get(d) or {'path': '', 'cfg': '', 'pending': False})
            at = self.fdd_inserted_at.get(d)
            if at is not None and f.get('pending'):
                if self.paused is None and self.fdd_accepts and n - at >= self.fdd_delay:
                    f['pending'] = False
                    f['path'] = f['cfg']
                elif not self.fdd_accepts and n - at >= 1:
                    f['pending'] = False      # 開けなかった: path は空のまま
            self.fdd[d] = f
            out.append({'drive': d, 'equip': d == 1 or d == 2, 'path': f['path'],
                        'cfg': f['cfg'], 'pending': f['pending'], 'ready': bool(f['path'])})
        return out

    def _ide_json(self):
        n = len([c for c in self.calls if c[1] == '/api/instance'])
        out = []
        for slot in (1, 2, 3, 4):
            e = dict(self.ide[slot])
            at = self.cd_changed_at.get(slot)
            if at is not None and e.get('changing') and self.paused is None \
                    and n - at >= self.cd_delay:
                e['changing'] = False
                e['path'] = e['next'] if self.cd_accepts else ''
                e['next'] = ''
                self.cd_changed_at.pop(slot)
            self.ide[slot] = e
            j = {'slot': slot, 'type': e['type'], 'ready': bool(e['path']), 'path': e['path']}
            if e['type'] == 'cdrom':
                j['changing'] = bool(e.get('changing'))
                if e.get('changing'):
                    j['next'] = e['next']
            out.append(j)
        return out

    def _cd(self, headers, body):
        if self.mode == 'old' or self.api_version < 3:
            return 404, '{"ok":false,"error":"unknown endpoint"}'
        if not self._authorized(headers):
            return 401, '{"ok":false,"error":"need the aidebug token"}'
        if self.paused == 'trap':
            # aidapp_cd: ブレーク中は受理しない
            return 409, ('{"ok":false,"error":"emulation is paused in the trap '
                         '(breakpoint/step/fault); POST /api/resume first"}')
        if self.cd_status is not None:
            if self.cd_status[1].startswith('cd request is being applied'):
                # COMMIT の後に 10 秒で終わらなかった: 操作自体は後で反映される
                self.cd_status_apply(body)
            return self.cd_status[0], json.dumps({'ok': False, 'error': self.cd_status[1]})
        q = parse_qs(body or '', keep_blank_values=True)
        if 'drive' in q:
            slot = int(q['drive'][0])
        else:
            slot = next((i for i in (1, 2, 3, 4) if self.ide[i]['type'] == 'cdrom'), None)
            if slot is None:
                return 409, '{"ok":false,"error":"no IDE slot is a CD-ROM drive"}'
        e = self.ide[slot]
        if e['type'] != 'cdrom':
            return 409, json.dumps({'ok': False,
                                    'error': 'IDE slot %d is not a CD-ROM drive' % slot})
        if e.get('changing'):
            return 409, '{"ok":false,"error":"a CD change is still pending"}'
        n = len([c for c in self.calls if c[1] == '/api/instance'])
        self.cd_snapshot = self._ide_json()
        self.cd_posted_at = n
        if self.trap_after_cd:
            self.stale_after_cd = 10 ** 6
        js = {'ok': True, 'drive': slot, 'action': q['action'][0], 'ini': 'unchanged'}
        if q['action'][0] == 'insert':
            want = q['path'][0]
            js['path'] = want
            if e['path']:
                self.ide[slot] = {'type': 'cdrom', 'path': '', 'changing': True, 'next': want}
                self.cd_changed_at[slot] = n
                js['state'] = 'changing'
            elif self.cd_accepts:
                self.ide[slot] = {'type': 'cdrom', 'path': want, 'changing': False, 'next': ''}
                js['state'] = 'ready'
            else:
                return 422, json.dumps({'ok': False, 'error': 'IDE slot %d: NP21/W could '
                                        'not open the image' % slot})
        else:
            self.ide[slot] = {'type': 'cdrom', 'path': '', 'changing': False, 'next': ''}
            js['state'] = 'empty'
        return 200, json.dumps(js)

    def cd_status_apply(self, body):
        q = parse_qs(body or '', keep_blank_values=True)
        slot = int(q['drive'][0]) if 'drive' in q else 3
        path = q['path'][0] if q['action'][0] == 'insert' else ''
        self.ide[slot] = {'type': 'cdrom', 'path': path, 'changing': False, 'next': ''}

    def request(self, method, path, body=None, timeout=10, headers=None):
        self.calls.append((method, path, body, timeout, dict(headers or {})))
        if self.latency:
            self.ops.fc.now += self.latency
        if self.mode == 'down' or not self.ops.running:
            return None
        n = len([c for c in self.calls if c[1] == path])
        if n <= self.api_up_after and path in ('/api/instance', '/api/status'):
            return None
        if path == '/api/dialog':
            if self.mode == 'old':
                return 404, '{"ok":false,"error":"unknown endpoint"}'
            if self.dialog:
                return 200, json.dumps({'ok': True, 'open': True, 'modal': True,
                                        'dialogs': [self.dialog]})
            return 200, '{"ok":true,"open":false,"modal":false,"dialogs":[]}'
        if self.dialog and path in ('/api/status', '/api/tvram'):
            return 503, '{"ok":false,"dialog":true}'
        if path == '/api/instance':
            if self.mode == 'old':
                return 404, '{"ok":false,"error":"unknown endpoint"}'
            js = {
                'ok': True, 'api_version': self.api_version, 'pid': self._pid(),
                'exe': self.inst_exe, 'ini': WIN + '\\test.ini',
                'instance_id': 'ab' * 16, 'started_at': '2026-09-25T00:00:00.000Z',
                'dialog': self.instance_dialog,
                'trap_pause': self.paused == 'trap', 'user_pause': self.paused == 'user',
                'fdd': self._fdd_json(), 'media_fresh': True,
                'ide': self._ide_json()}
            if self.stale_after_fdd is not None and self.fdd_posted_at is not None:
                since = n - self.fdd_posted_at
                if since <= self.stale_after_fdd:
                    js['fdd'] = self.fdd_snapshot
                    js['media_fresh'] = False
            if self.stale_after_cd is not None and self.cd_posted_at is not None:
                if n - self.cd_posted_at <= self.stale_after_cd:
                    js['ide'] = self.cd_snapshot
                    js['media_fresh'] = False
                if self.trap_after_cd:
                    js['trap_pause'] = True
            if self.token_file is not None:
                js['token_file'] = self.token_file
                if not self.token_file:
                    js['token_error'] = 'CreateFile failed (5)'
            return 200, json.dumps(js)
        if path == '/api/quit':
            if not self._authorized(headers):
                return 401, '{"ok":false,"error":"need the aidebug token"}'
            if self.quit_status != 200:
                return self.quit_status, json.dumps({'ok': False, 'error': self.quit_error})
            save = parse_qs(body or '').get('save', [''])[0]
            if self.quit_policy is not None and self.quit_policy != save:
                return 409, json.dumps({'ok': False, 'error':
                                        'quit already requested with save=%s' % self.quit_policy})
            self.quit_policy = save
            if self.quit_stops:
                self.ops.running.pop(self._pid(), None)
            return 200, '{"ok":true,"quitting":true}'
        if path == '/api/status':
            return 200, '{"phase":"p2"}'
        if path == '/api/tvram':
            return 200, self.tvram() if callable(self.tvram) else (self.tvram or '')
        if path == '/api/cd':
            return self._cd(headers, body)
        if path == '/api/fdd':
            if self.mode == 'old':
                return 404, '{"ok":false,"error":"unknown endpoint"}'
            if not self._authorized(headers):
                return 401, '{"ok":false,"error":"need the aidebug token"}'
            applying = self.fdd_error.startswith('fdd request is being applied')
            if self.fdd_status != 200 and not applying:
                return self.fdd_status, json.dumps({'ok': False, 'error': self.fdd_error})
            q = parse_qs(body or '')
            d = int(q['drive'][0])
            n = len([c for c in self.calls if c[1] == '/api/instance'])
            self.fdd_snapshot = self._fdd_json()     # 操作の前の快照
            self.fdd_posted_at = n
            if q['action'][0] == 'insert':
                self.fdd[d] = {'path': '', 'cfg': q['path'][0], 'pending': True}
                self.fdd_inserted_at[d] = n
            else:
                self.fdd[d] = {'path': '', 'cfg': '', 'pending': False}
                self.fdd_inserted_at.pop(d, None)
            if applying:
                # COMMIT の後に 10 秒で終わらなかった: 操作自体は後で反映される
                return self.fdd_status, json.dumps({'ok': False, 'error': self.fdd_error})
            return 200, '{"ok":true}'
        return 404, '{"ok":false,"error":"unknown endpoint"}'


INI_TEXT = (
    '[Other]\r\n'
    'HDD1FILE=C:\\WRONG\\other_section.nhd\r\n'
    '[NekoProject21]\r\n'
    'FDD1FILE=C:\\NP21\\os32_boot.d88\r\n'
    'FDD2FILE=\r\n'
    'HDD1FILE=C:\\NP21\\os32.nhd\r\n'
    'HDD1FILE=C:\\WRONG\\second_value_ignored.nhd\r\n'
    'HDD2FILE=\r\n'
    'CD1_FILE=C:\\NP21\\os32_install.iso\r\n'
    'CD2_FILE=\r\n'
    'CDfolder=C:\\Users\\x\\\u30c9\u30ad\u30e5\u30e1\u30f3\u30c8\\a.iso\r\n'
    '[NekoProject21]\r\n'
    'HDD3FILE=C:\\WRONG\\second_section.nhd\r\n'
)


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        for name in ('np21x64w.exe', 'os32.nhd', 'os32_boot.d88', 'other.d88',
                     'os32_install.iso', 'other.iso'):
            open(os.path.join(self.dir, name), 'wb').close()
        with open(os.path.join(self.dir, TOKEN_NAME), 'w') as f:
            f.write(TOKEN + '\r\n')
        self.write_ini(INI_TEXT)
        self.env = mock.patch.dict(os.environ, {}, clear=False)
        self.env.start()
        os.environ.pop('NP21W_AIDEBUG_TOKEN_FILE', None)
        self.fc = FakeClock()
        self.out = io.StringIO()
        self.err = io.StringIO()

    def tearDown(self):
        self.env.stop()
        self.tmp.cleanup()

    def write_ini(self, text, name='test.ini', encoding='cp932'):
        with open(os.path.join(self.dir, name), 'wb') as f:
            f.write(text.encode(encoding) if isinstance(text, str) else text)

    def make(self, http_mode='new', ops=None, **hk):
        self.ops = ops or FakeOps(self.fc)
        self.http = FakeHttp(self.ops, http_mode, **hk)
        return ctl.Ctl(ctl.Paths(self.dir, WIN), ops=self.ops, http=self.http,
                       sleep=self.fc.sleep, clock=self.fc.clock,
                       out=self.out, err=self.err, poll=1.0, settle=2.0)

    def run_main(self, c, argv):
        return ctl.main(argv, ctl_factory=lambda: c)

    def ours(self, pid=77):
        return ctl.Proc(pid, 'np21x64w.exe', EXE)


# ---------------------------------------------------------------------------
class IniParse(Base):
    def test_section_first_value_and_untouched(self):
        path = os.path.join(self.dir, 'test.ini')
        before = Path(path).read_bytes()
        got = ctl.ini_media(path, WIN)
        # 並びは MEDIA_KEYS 順 (HDD → CD → FDD → SCSI)。他の節・2 つ目の節・
        # 節内の 2 回目の値は NP21/W と同じく無視する
        self.assertEqual(got, [NHD, ISO, D88])
        self.assertEqual(Path(path).read_bytes(), before)

    def test_quotes_and_japanese_path(self):
        jp = 'C:\\\u30c9\u30ad\u30e5\u30e1\u30f3\u30c8\\np21\\os32.nhd'
        self.write_ini('[NekoProject21]\r\n'
                       'HDD1FILE =  " %s "  \r\n'
                       'CD1_FILE="C:\\a b\\x.iso"\r\n'
                       'FDD1FILE="half\r\n'
                       'SCSIHDD0=C:\\scsi\\s0.hdd\r\n' % jp)
        got = ctl.ini_media(os.path.join(self.dir, 'test.ini'), WIN)
        # 引用符は 1 組だけ外し、中の前後の空白も取る (profile.c ParseLine)。
        # 片側だけの引用符は外さない。相対値は NP21W_DIR 起点
        self.assertEqual(got, [jp, 'C:\\a b\\x.iso', WIN + '\\"half',
                               'C:\\scsi\\s0.hdd'])

    def test_bom_utf8_and_case_insensitive_keys(self):
        self.write_ini(b'\xef\xbb\xbf[nekoproject21]\nhdd1file=C:\\u\\\xe3\x81\x82.nhd\n')
        got = ctl.ini_media(os.path.join(self.dir, 'test.ini'), WIN)
        self.assertEqual(got, ['C:\\u\\\u3042.nhd'])

    def test_unreadable_ini_is_none(self):
        self.assertIsNone(ctl.ini_media(os.path.join(self.dir, 'nope.ini')))

    def test_forward_slash_absolute_is_not_made_relative(self):
        # Codex P2: `C:/NP21/os32.nhd` は絶対。区切りを揃えてから判定し、値も揃える
        self.write_ini('[NekoProject21]\r\n'
                       'HDD1FILE=C:/NP21/os32.nhd\r\n'
                       'CD1_FILE=//server/share/a.iso\r\n'
                       'FDD1FILE=sub/rel.d88\r\n')
        got = ctl.ini_media(os.path.join(self.dir, 'test.ini'), WIN)
        self.assertEqual(got, [NHD, '\\\\server\\share\\a.iso', WIN + '\\sub\\rel.d88'])
        self.assertNotIn('C:\\NP21\\C:', ' '.join(got))

    def test_is_win_abs_accepts_both_separators(self):
        for p in ('C:/x', 'c:\\x', '//srv/s', '\\\\srv\\s'):
            self.assertTrue(ctl.is_win_abs(p), p)
        for p in ('x.nhd', 'sub/x.nhd', '/mnt/c/x', 'C:x'):
            self.assertFalse(ctl.is_win_abs(p), p)


# ---------------------------------------------------------------------------
class LockWait(Base):
    def test_waits_until_lock_is_released_then_starts(self):
        c = self.make(ops=FakeOps(self.fc, locks={NHD: 3}))
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--fd', 'os32_boot.d88'])
        self.assertEqual(rc, 0, self.err.getvalue())
        # t=0..2 locked、t=3..8 free (5 秒) → settle 2 秒 → 起動直前の全体プローブ
        self.assertEqual(len(self.ops.probes), 10)
        # 安定した媒体も毎回プローブし続ける (Codex 1 / Fable M1)
        for p in self.ops.probes:
            self.assertEqual(sorted(p), sorted([NHD, ISO, D88]))
        self.assertEqual(self.ops.started, [(
            EXE, ['/i' + WIN + '\\test.ini', D88], WIN)])

    def test_stable_media_relocked_while_waiting_for_another(self):
        # ISO は最初から free だが、NHD を待っている間の 5 回目に一度掴まれる
        c = self.make(ops=FakeOps(self.fc, locks={NHD: 6},
                                  lock_seq={ISO: 'FFFFLF'}))
        rc = self.run_main(c, ['start', '--ini', 'test.ini'])
        self.assertEqual(rc, 0, self.err.getvalue())
        self.assertIn('locked again (数え直し): ' + ISO, self.out.getvalue())
        # NHD は t=6 から (t=11 で安定)、ISO は t=5 から数え直し (t=10)
        self.assertEqual(len(self.ops.started), 1)
        self.assertGreaterEqual(self.fc.now, 13)

    def test_settle_then_final_probe_right_before_start(self):
        c = self.make()
        self.assertEqual(self.run_main(c, ['start', '--ini', 'test.ini']), 0)
        ev = [e[0] if e[0] != 'sleep' else ('sleep', e[1]) for e in self.fc.events]
        i = ev.index('start')
        self.assertEqual(ev[i - 1], 'probe')          # 直前に全体プローブ
        self.assertEqual(ev[i - 2], ('sleep', 2.0))   # その前に settle
        self.assertEqual(len(self.fc.events[i - 1][1]), 3)

    def test_final_probe_relock_restarts_count(self):
        # 7 回目 (起動直前のプローブ) で NHD が掴まれる
        c = self.make(ops=FakeOps(self.fc, lock_seq={NHD: 'FFFFFFLF'}))
        rc = self.run_main(c, ['start', '--ini', 'test.ini'])
        self.assertEqual(rc, 0, self.err.getvalue())
        self.assertIn('locked again 起動の直前', self.out.getvalue())
        self.assertGreater(len(self.ops.probes), 7)

    def test_open_once_then_locked_again_restarts_the_count(self):
        c = self.make(ops=FakeOps(self.fc, lock_seq={NHD: 'FLLF'}))
        rc = self.run_main(c, ['start', '--ini', 'test.ini'])
        self.assertEqual(rc, 0, self.err.getvalue())
        self.assertIn('locked again', self.out.getvalue())
        self.assertEqual(len(self.ops.started), 1)
        self.assertGreaterEqual(self.fc.now, 10)

    def test_flapping_lock_times_out_as_unsteady(self):
        c = self.make(ops=FakeOps(self.fc, lock_seq={NHD: 'FLFFFLFFFFLF' * 10}))
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--timeout', '20'])
        self.assertEqual(rc, 1)
        self.assertIn(NHD, self.err.getvalue())
        self.assertEqual(self.ops.started, [])

    def test_timeout_names_locked_file_and_does_not_start(self):
        c = self.make(ops=FakeOps(self.fc, locks={ISO: 10 ** 6}))
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--timeout', '8'])
        self.assertEqual(rc, 1)
        self.assertIn('locked: ' + ISO, self.err.getvalue())
        self.assertNotIn('locked: ' + NHD, self.err.getvalue())
        self.assertEqual(self.ops.started, [])

    def test_probe_gets_remaining_time(self):
        # 1 回のプローブに 3 秒かかる。期限 6 秒では 5 秒の安定を満たす前に切れる
        c = self.make(ops=FakeOps(self.fc, probe_cost=3.0))
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--timeout', '6'])
        self.assertEqual(rc, 1)
        self.assertEqual(self.ops.started, [])
        self.assertLessEqual(self.ops.probe_timeouts[0], 6)
        self.assertLess(self.ops.probe_timeouts[-1], self.ops.probe_timeouts[0])
        for t in self.ops.probe_timeouts:
            self.assertGreater(t, 0)

    def test_result_after_deadline_is_not_success(self):
        # stable 0: 1 回目 (t=0→1) で安定、settle で t=3、起動直前のプローブは
        # 残り 0.5 秒で始まり t=4 に返る — 期限 3.5 を過ぎた結果は成功にしない
        c = self.make(ops=FakeOps(self.fc, probe_cost=1.0))
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--timeout', '3.5',
                               '--stable', '0'])
        self.assertEqual(rc, 1)
        self.assertEqual(self.ops.started, [])

    def test_timeout_shorter_than_stable_is_argument_error(self):
        c = self.make()
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--timeout', '3',
                               '--stable', '5'])
        self.assertEqual(rc, 2)
        self.assertEqual(self.ops.probes, [])

    def test_missing_fd_argument_fails(self):
        c = self.make(ops=FakeOps(self.fc, missing={WIN + '\\other.d88'}))
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--fd', 'other.d88'])
        self.assertEqual(rc, 2)
        self.assertEqual(self.ops.started, [])

    def test_missing_ini_media_is_warned_not_waited(self):
        c = self.make(ops=FakeOps(self.fc, missing={ISO}))
        self.assertEqual(self.run_main(c, ['start', '--ini', 'test.ini']), 0)
        self.assertIn(ISO, self.err.getvalue())
        self.assertNotIn(ISO, self.ops.probes[-1])

    def test_unreadable_ini_falls_back_to_default_three(self):
        warned = []
        targets, source = ctl.media_targets(ctl.Paths(self.dir, WIN), 'nope.ini',
                                            None, warned.append)
        self.assertEqual(source, 'default')
        self.assertEqual([t[0] for t in targets],
                         [WIN + '\\' + n for n in ctl.DEFAULT_MEDIA])
        self.assertIn('読めない', warned[0])


# ---------------------------------------------------------------------------
class ProbeBroken(Base):
    PATHS = [NHD, ISO]

    def test_rc_nonzero_is_probe_error_with_stderr(self):
        with self.assertRaises(ctl.ProbeError) as cm:
            ctl.parse_probe(self.PATHS, 1, 'free\t0\nfree\t1\nend\n', 'boom here')
        self.assertIn('boom here', str(cm.exception))

    def test_error_line_names_exception_type(self):
        with self.assertRaises(ctl.ProbeError) as cm:
            ctl.parse_probe(self.PATHS, 0,
                            'free\t0\nerror\t1\tSystem.UnauthorizedAccessException\nend\n', '')
        self.assertIn('UnauthorizedAccessException', str(cm.exception))
        self.assertIn(ISO, str(cm.exception))

    def test_unreported_path_is_probe_error(self):
        with self.assertRaises(ctl.ProbeError) as cm:
            ctl.parse_probe(self.PATHS, 0, 'free\t0\nend\n', 'warn')
        self.assertIn(ISO, str(cm.exception))
        self.assertIn('warn', str(cm.exception))

    def test_missing_end_is_probe_error(self):
        with self.assertRaises(ctl.ProbeError):
            ctl.parse_probe(self.PATHS, 0, 'free\t0\nfree\t1\n', '')

    def test_good_output(self):
        self.assertEqual(ctl.parse_probe(self.PATHS, 0,
                                         'locked\t0\nmissing\t1\nfree\t9\nend\n', ''),
                         {NHD: 'locked', ISO: 'missing'})

    def test_start_fails_immediately_on_broken_probe(self):
        c = self.make(ops=FakeOps(self.fc, probe_error=ctl.ProbeError('broken: x')))
        rc = self.run_main(c, ['start', '--ini', 'test.ini'])
        self.assertEqual(rc, 1)
        self.assertIn('broken: x', self.err.getvalue())
        self.assertEqual(self.ops.started, [])
        self.assertEqual(self.fc.now, 0)


# ---------------------------------------------------------------------------
class Scripts(unittest.TestCase):
    """合成した PowerShell を復号して照合する (日本語のディレクトリと "/i…")。"""

    JP = 'C:\\Users\\\u5c71\u7530\\\u30c9\u30ad\u30e5\u30e1\u30f3\u30c8\\np21'

    def ops(self, out, rc=0, err=''):
        o = ctl.WinOps()
        o.calls = []

        def run(args, timeout=60):
            o.calls.append((args, timeout))
            return rc, out, err
        o._run = run
        return o

    def decoded(self, o):
        args = o.calls[0][0]
        self.assertIn('-EncodedCommand', args)
        return ctl.ps_decode(args[args.index('-EncodedCommand') + 1])

    def test_lock_script_roundtrip(self):
        paths = [self.JP + "\\it's.nhd", self.JP + '\\b.iso']
        o = self.ops('free\t0\nlocked\t1\nend\n')
        self.assertEqual(o.probe(paths, timeout=7.5), {paths[0]: 'free', paths[1]: 'locked'})
        s = self.decoded(o)
        self.assertIn("$paths = @('" + self.JP + "\\it''s.nhd', '" + self.JP + "\\b.iso')", s)
        self.assertIn('catch [System.IO.IOException] { "locked`t$i" }', s)
        self.assertIn('"error`t$i`t$($_.Exception.GetType().FullName)"', s)
        self.assertIn("[IO.File]::Open($p, 'Open', 'ReadWrite', 'None')", s)
        self.assertEqual(o.calls[0][1], 7.5)

    def test_start_script_roundtrip(self):
        o = self.ops('pid\t321\n')
        ini = self.JP + '\\my test.ini'
        pid = o.start(self.JP + '\\np21x64w.exe', ['/i' + ini, self.JP + '\\a.d88'], self.JP)
        self.assertEqual(pid, 321)
        s = self.decoded(o)
        self.assertIn("-FilePath '%s\\np21x64w.exe'" % self.JP, s)
        self.assertIn("-ArgumentList @('\"/i%s\"', '\"%s\\a.d88\"')" % (ini, self.JP), s)
        self.assertIn("-WorkingDirectory '%s'" % self.JP, s)

    def test_start_failure(self):
        with self.assertRaises(ctl.CtlError):
            self.ops('', rc=1, err='denied').start('C:\\e.exe', [], 'C:\\')

    def test_process_list_decodes_base64_exe(self):
        exe = self.JP + '\\np21x64w.exe'

        def b(s):
            return base64.b64encode(s.encode('utf-8')).decode()
        o = self.ops('proc\t10\t%s\t%s\nproc\t11\t%s\t-\nend\n'
                     % (b('np21x64w.exe'), b(exe), b('np21w.exe')))
        procs = o.list_processes()
        self.assertEqual([(p.pid, p.name, p.exe) for p in procs],
                         [(10, 'np21x64w.exe', exe), (11, 'np21w.exe', None)])
        self.assertIn('ExecutablePath', self.decoded(o))

    def test_process_list_failure_is_error(self):
        with self.assertRaises(ctl.CtlError):
            self.ops('', rc=1, err='CIM broken').list_processes()


# ---------------------------------------------------------------------------
class Stop(Base):
    def others(self):
        return [ctl.Proc(90, 'np21x64w_helper.exe', WIN + '\\np21x64w_helper.exe'),
                ctl.Proc(91, 'np21x64w.exe', 'D:\\Other\\np21x64w.exe'),
                ctl.Proc(92, 'np21x64w.exe', None),
                ctl.Proc(93, 'np21x64w.exe', WIN + '\\sub\\np21x64w.exe')]

    def test_quit_via_api(self):
        ops = FakeOps(self.fc, running=[self.ours(77)])
        c = self.make(ops=ops)
        self.assertEqual(self.run_main(c, ['stop']), 0, self.err.getvalue())
        quit_calls = [x for x in self.http.calls if x[1] == '/api/quit']
        self.assertEqual(len(quit_calls), 1)
        self.assertEqual(quit_calls[0][0], 'POST')
        self.assertEqual(quit_calls[0][2], 'save=0&instance_id=' + 'ab' * 16)
        self.assertEqual(quit_calls[0][4], {'X-Aidebug-Token': TOKEN})
        self.assertEqual(ops.killed, [])
        self.assertIn('stopped (/api/quit)', self.out.getvalue())

    def test_missing_token_falls_back_to_force_kill_of_ours(self):
        os.remove(os.path.join(self.dir, TOKEN_NAME))
        ops = FakeOps(self.fc, running=[self.ours(77)] + self.others())
        c = self.make(ops=ops, inst_pid=77)
        self.assertEqual(self.run_main(c, ['stop']), 0, self.err.getvalue())
        self.assertEqual([x for x in self.http.calls if x[1] == '/api/quit'], [])
        self.assertEqual(ops.killed, [77])
        self.assertIn('トークンが読めない', self.err.getvalue())

    def test_quit_already_requested_with_other_policy_waits(self):
        # 先に save=1 で受け付けられている → 409。方針は最初に固定されるので待つだけ
        ops = FakeOps(self.fc, running=[self.ours(77)], vanish={77: 3})
        c = self.make(ops=ops, inst_pid=77, quit_policy='1')
        self.assertEqual(self.run_main(c, ['stop']), 0, self.err.getvalue())
        self.assertIn('別の方針', self.err.getvalue())
        self.assertEqual(ops.killed, [])
        self.assertIn('stopped (/api/quit)', self.out.getvalue())

    def test_quit_accepted_but_pid_stays_then_force_kill_ours_only(self):
        ops = FakeOps(self.fc, running=[self.ours(77)] + self.others())
        c = self.make(ops=ops, quit_stops=False, inst_pid=77)
        self.assertEqual(self.run_main(c, ['stop', '--timeout', '5']), 0, self.err.getvalue())
        self.assertEqual(ops.killed, [77])
        self.assertIn('強制終了に落とす', self.err.getvalue())

    def test_api_down_force_kills_only_exact_exe(self):
        ops = FakeOps(self.fc, running=[self.ours(77)] + self.others())
        c = self.make('down', ops=ops)
        self.assertEqual(self.run_main(c, ['stop']), 0, self.err.getvalue())
        self.assertEqual(ops.killed, [77])
        for pid in (90, 91, 92, 93):
            self.assertIn('pid=%d' % pid, self.out.getvalue())
            self.assertIn(pid, ops.running)

    def test_old_fork_says_deploy_and_kills_exact_exe(self):
        ops = FakeOps(self.fc, running=[self.ours(77)] + self.others())
        c = self.make('old', ops=ops)
        self.assertEqual(self.run_main(c, ['stop']), 0)
        self.assertIn('フォークが古い', self.err.getvalue())
        self.assertIn('make deploy', self.err.getvalue())
        self.assertEqual(ops.killed, [77])

    def test_api_answered_by_other_location_is_not_quit(self):
        ops = FakeOps(self.fc, running=[ctl.Proc(91, 'np21x64w.exe', 'D:\\Other\\np21x64w.exe')])
        c = self.make(ops=ops, inst_exe='D:\\Other\\np21x64w.exe')
        self.assertEqual(self.run_main(c, ['stop']), 0)
        self.assertEqual([x for x in self.http.calls if x[1] == '/api/quit'], [])
        self.assertEqual(ops.killed, [])
        self.assertIn('別の場所', self.err.getvalue())

    def test_nothing_running(self):
        c = self.make('down')
        self.assertEqual(self.run_main(c, ['stop']), 0)
        self.assertIn('プロセスは無い', self.out.getvalue())

    def test_survivor_after_force_kill_fails(self):
        ops = FakeOps(self.fc, running=[self.ours(77)], kill_works=False)
        c = self.make('down', ops=ops)
        self.assertEqual(self.run_main(c, ['stop', '--timeout', '4']), 1)
        self.assertIn('pid=77', self.err.getvalue())


# ---------------------------------------------------------------------------
class StartProcesses(Base):
    def test_waits_for_our_process_to_disappear(self):
        ops = FakeOps(self.fc, running=[self.ours(77)], vanish={77: 3})
        c = self.make(ops=ops)
        self.assertEqual(self.run_main(c, ['start', '--ini', 'test.ini']), 0, self.err.getvalue())
        self.assertEqual(len(ops.started), 1)

    def test_refuses_while_our_process_remains(self):
        ops = FakeOps(self.fc, running=[self.ours(77)])
        c = self.make('down', ops=ops)
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--timeout', '5'])
        self.assertEqual(rc, 1)
        self.assertIn('pid=77', self.err.getvalue())
        self.assertEqual(ops.probes, [])

    def test_other_location_does_not_block(self):
        ops = FakeOps(self.fc, running=[ctl.Proc(91, 'np21x64w.exe', 'D:\\Other\\np21x64w.exe')])
        c = self.make(ops=ops, inst_pid=PID)
        self.assertEqual(self.run_main(c, ['start', '--ini', 'test.ini']), 0, self.err.getvalue())
        self.assertIn('対象外', self.out.getvalue())


class AfterStart(Base):
    def test_success_checks_instance_pid_and_exe(self):
        c = self.make()
        self.assertEqual(self.run_main(c, ['start', '--ini', 'test.ini']), 0, self.err.getvalue())
        self.assertIn('instance pid=%d' % PID, self.out.getvalue())
        self.assertIn('alive 10s pid=%d, aidebug up' % PID, self.out.getvalue())
        self.assertGreaterEqual(self.ops.after_start, 11)

    def test_instance_pid_mismatch_fails(self):
        c = self.make(inst_pid=999)
        self.assertEqual(self.run_main(c, ['start', '--ini', 'test.ini']), 1)
        self.assertIn('起動したプロセスではない', self.err.getvalue())

    def test_instance_exe_mismatch_fails(self):
        c = self.make(inst_exe='D:\\Other\\np21x64w.exe')
        self.assertEqual(self.run_main(c, ['start', '--ini', 'test.ini']), 1)
        self.assertIn('D:\\Other', self.err.getvalue())

    def test_process_exits_right_after_start(self):
        c = self.make(ops=FakeOps(self.fc, dies_after=2))
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--wait-ready'])
        self.assertEqual(rc, 1)
        self.assertIn('起動直後に終了した', self.err.getvalue())
        self.assertIn('pid=%d' % PID, self.err.getvalue())

    def test_pid_rechecked_right_before_success(self):
        # 11 回目 (t=10 の先頭) までは生きていて、成功直前の確認で消えている
        c = self.make(ops=FakeOps(self.fc, dies_after=11))
        self.assertEqual(self.run_main(c, ['start', '--ini', 'test.ini']), 1)
        self.assertIn('確認の最後に消えた', self.err.getvalue())

    def test_api_silent_with_dialog_reports_dialog(self):
        dlg = {'hwnd': '0x1', 'modal': True, 'title': 'Neko Project 21/W',
               'text': ['HDD image open failed'], 'buttons': [{'id': 1, 'text': 'OK'}]}
        c = self.make(api_up_after=10 ** 6, dialog=dlg)
        self.assertEqual(self.run_main(c, ['start', '--ini', 'test.ini']), 1)
        # API の期限 (60 秒) を待たず、黙っていると分かった時点で見る
        self.assertLess(self.fc.now - self.ops.start_time, 5)
        self.assertIn('HDD image open failed', self.err.getvalue())
        self.assertIn('ボタン: OK', self.err.getvalue())

    def test_instance_reports_dialog(self):
        dlg = {'modal': True, 'title': 'T', 'text': ['resume?'], 'buttons': []}
        c = self.make(instance_dialog=True, dialog=dlg)
        self.assertEqual(self.run_main(c, ['start', '--ini', 'test.ini']), 1)
        self.assertIn('resume?', self.err.getvalue())

    def test_api_never_answers_without_dialog(self):
        c = self.make(api_up_after=10 ** 6)
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--api-timeout', '15'])
        self.assertEqual(rc, 1)
        self.assertIn('15 秒応答しない', self.err.getvalue())

    def test_api_comes_up_late(self):
        c = self.make(api_up_after=20)
        self.assertEqual(self.run_main(c, ['start', '--ini', 'test.ini']), 0, self.err.getvalue())

    def test_api_timeout_zero_does_not_wait_for_api(self):
        c = self.make('down')
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--api-timeout', '0'])
        self.assertEqual(rc, 0, self.err.getvalue())
        self.assertEqual([x for x in self.http.calls if x[1] == '/api/instance'], [])
        self.assertIn('API は待たない', self.out.getvalue())

    def test_api_timeout_zero_makes_no_http_call_at_all(self):
        # Codex P2: 0 なら /api/dialog も呼ばない (ダイアログが出ていても知らない)
        dlg = {'modal': True, 'title': 'T', 'text': ['stuck'], 'buttons': []}
        c = self.make(dialog=dlg)
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--api-timeout', '0'])
        self.assertEqual(rc, 0, self.err.getvalue())
        self.assertEqual(self.http.calls, [])

    def test_instance_answer_after_api_deadline_is_not_success(self):
        # Codex P2: 各 HTTP が 4 秒かかり、API の期限は 3 秒。最初の応答は t=4 に
        # 届く — 期限を過ぎているので確認に数えず、期限切れとして失敗する
        c = self.make(latency=4.0)
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--api-timeout', '3',
                               '--alive', '1'])
        self.assertEqual(rc, 1)
        self.assertIn('3 秒応答しない', self.err.getvalue())
        self.assertNotIn('aidebug up', self.out.getvalue())

    def test_http_gets_remaining_time_not_more(self):
        c = self.make(api_up_after=3)
        rc = self.run_main(c, ['start', '--ini', 'test.ini', '--api-timeout', '4'])
        self.assertEqual(rc, 0, self.err.getvalue())
        inst_calls = [x for x in self.http.calls if x[1] == '/api/instance']
        self.assertGreaterEqual(len(inst_calls), 2)
        # 1 秒おきに呼ぶので、渡す timeout は 5 → 残り時間 (4, 3, 2, ...) へ切り詰まる
        self.assertLessEqual(inst_calls[0][3], 4)
        self.assertLess(inst_calls[-1][3], inst_calls[0][3])
        for x in inst_calls:
            self.assertGreaterEqual(x[3], 1)

    def test_old_fork_after_start_uses_status(self):
        c = self.make('old')
        self.assertEqual(self.run_main(c, ['start', '--ini', 'test.ini']), 0, self.err.getvalue())
        self.assertIn('フォークが古い', self.err.getvalue())


class WaitReady(Base):
    def setUp(self):
        Base.setUp(self)
        self.ops0 = FakeOps(self.fc, running=[ctl.Proc(PID, 'np21x64w.exe', EXE)])

    def test_ready_when_text_appears(self):
        screens = ['booting', 'booting', 'OS32\nWaiting for commands via serial...\n']
        c = self.make(ops=self.ops0,
                      tvram=lambda: screens.pop(0) if len(screens) > 1 else screens[0])
        self.assertEqual(self.run_main(c, ['wait-ready']), 0)

    def test_timeout_shows_last_screen(self):
        c = self.make(ops=self.ops0, tvram='root panic\n')
        self.assertEqual(self.run_main(c, ['wait-ready', '--timeout', '6']), 1)
        self.assertIn('root panic', self.err.getvalue())

    def test_timeout_when_api_never_answers(self):
        c = self.make('down', ops=self.ops0)
        self.assertEqual(self.run_main(c, ['wait-ready', '--timeout', '6']), 1)
        self.assertIn('aidebug', self.err.getvalue())

    def test_dialog_during_wait(self):
        c = self.make(ops=self.ops0, dialog={'modal': True, 'title': 'X', 'text': ['stuck'],
                                             'buttons': []})
        self.assertEqual(self.run_main(c, ['wait-ready']), 1)
        self.assertIn('stuck', self.err.getvalue())

    def test_ready_text_arriving_after_deadline_is_not_success(self):
        # Codex P2: 1 回の /api/tvram に 5 秒かかる。期限 4 秒 → t=5 に届いた
        # "Waiting for commands" は成功に数えない
        c = self.make(ops=self.ops0, tvram='Waiting for commands via serial...\n',
                      latency=5.0)
        self.assertEqual(self.run_main(c, ['wait-ready', '--timeout', '4']), 1)
        self.assertNotIn('ready', self.out.getvalue())
        tv = [x for x in self.http.calls if x[1] == '/api/tvram']
        self.assertEqual(len(tv), 1)
        self.assertLessEqual(tv[0][3], 4)       # 残り時間を渡す (15 ではなく)

    def test_tvram_timeout_is_capped_at_remaining(self):
        screens = ['booting'] * 3 + ['Waiting for commands\n']
        c = self.make(ops=self.ops0,
                      tvram=lambda: screens.pop(0) if len(screens) > 1 else screens[0])
        self.assertEqual(self.run_main(c, ['wait-ready', '--timeout', '10']), 0)
        tv = [x[3] for x in self.http.calls if x[1] == '/api/tvram']
        self.assertEqual(tv, [10, 9, 8, 7])


class Fdd(Base):
    def setUp(self):
        Base.setUp(self)
        self.ops0 = FakeOps(self.fc, running=[ctl.Proc(PID, 'np21x64w.exe', EXE)])

    def test_insert(self):
        c = self.make(ops=self.ops0)
        self.assertEqual(self.run_main(c, ['fdd', '--drive', '2', '--insert', 'other.d88']), 0,
                         self.err.getvalue())
        call = [x for x in self.http.calls if x[1] == '/api/fdd'][0]
        self.assertEqual(parse_qs(call[2]), {'drive': ['2'], 'action': ['insert'],
                                             'path': [WIN + '\\other.d88'],
                                             'readonly': ['0']})
        self.assertEqual(call[4], {'X-Aidebug-Token': TOKEN})
        # 受理の後、/api/instance に path が立つまで待って "ready" (PM の実地確認 8)
        self.assertIn('fdd2 insert %s 受理' % (WIN + '\\other.d88'), self.out.getvalue())
        self.assertIn('fdd2 ready %s' % (WIN + '\\other.d88'), self.out.getvalue())
        inst = [x for x in self.http.calls if x[1] == '/api/instance']
        self.assertGreaterEqual(len(inst), 3)      # 事前 1 + 反映待ち 2 回以上

    def _pending_case(self, paused, word):
        self.fc = FakeClock()
        self.out, self.err = io.StringIO(), io.StringIO()
        ops = FakeOps(self.fc, running=[ctl.Proc(PID, 'np21x64w.exe', EXE)])
        c = self.make(ops=ops, paused=paused)
        rc = self.run_main(c, ['fdd', '--drive', '1', '--insert', 'other.d88'])
        self.assertEqual(rc, 1, paused)
        self.assertIn('pending', self.err.getvalue())
        self.assertIn(word, self.err.getvalue())
        self.assertGreaterEqual(self.fc.now, 5)

    def test_insert_pending_forever_when_paused_fails(self):
        # trap (ブレーク) / user (一時停止) / それ以外 (背景で停止など) で理由を分ける
        self._pending_case('trap', 'ブレーク')
        self._pending_case('user', '一時停止')
        self._pending_case('bg', '進んでいない')

    def test_reinsert_same_fd_with_stale_snapshot_fails(self):
        # 同じ FD を入れ直す。UI スレッドが答えず media_fresh:false の間は、
        # 操作前の快照でも path が一致してしまう — それを成功にしない (Codex P2)
        c = self.make(ops=self.ops0)
        self.http.stale_after_fdd = 10 ** 6
        rc = self.run_main(c, ['fdd', '--drive', '1', '--insert', 'os32_boot.d88'])
        self.assertEqual(rc, 1)
        self.assertIn('確かめられなかった', self.err.getvalue())
        self.assertIn('media_fresh:false', self.err.getvalue())
        self.assertNotIn('ready', self.out.getvalue())
        self.assertGreaterEqual(self.fc.now, 5)

    def test_reinsert_same_fd_fresh_later_succeeds(self):
        # 3 回は古い快照、その後 fresh になって反映が見える
        c = self.make(ops=self.ops0)
        self.http.stale_after_fdd = 3
        rc = self.run_main(c, ['fdd', '--drive', '1', '--insert', 'os32_boot.d88'])
        self.assertEqual(rc, 0, self.err.getvalue())
        self.assertIn('fdd1 ready ' + D88, self.out.getvalue())
        after = [x for x in self.http.calls if x[1] == '/api/instance'][1:]
        self.assertGreaterEqual(len(after), 4)     # 古い 3 回を読み飛ばした

    def test_insert_not_opened_fails(self):
        c = self.make(ops=self.ops0, fdd_accepts=False)
        rc = self.run_main(c, ['fdd', '--drive', '1', '--insert', 'other.d88'])
        self.assertEqual(rc, 1)
        self.assertIn('開けなかった', self.err.getvalue())

    def test_insert_ready_wait_zero_does_not_wait(self):
        c = self.make(ops=self.ops0)
        rc = self.run_main(c, ['fdd', '--drive', '1', '--insert', 'other.d88',
                               '--ready-wait', '0'])
        self.assertEqual(rc, 0, self.err.getvalue())
        self.assertIn('反映は待たない', self.out.getvalue())
        self.assertEqual(self.fc.now, 0)

    def test_eject(self):
        c = self.make(ops=self.ops0)
        self.assertEqual(self.run_main(c, ['fdd', '--drive', '1', '--eject']), 0)
        call = [x for x in self.http.calls if x[1] == '/api/fdd'][0]
        self.assertEqual(call[2], 'drive=1&action=eject')
        self.assertEqual(call[4], {'X-Aidebug-Token': TOKEN})
        self.assertIn('fdd1 empty', self.out.getvalue())

    def test_post_timeout_covers_server_worst_case(self):
        # /api/fdd も /api/cd と同じ状態機械で、最悪 21 秒 (5 + 6 + 10) 待って答える
        c = self.make(ops=self.ops0)
        self.assertEqual(self.run_main(c, ['fdd', '--drive', '1', '--eject']), 0,
                         self.err.getvalue())
        call = [x for x in self.http.calls if x[1] == '/api/fdd'][0]
        self.assertGreaterEqual(call[3], 25)

    def test_being_applied_503_checks_instance(self):
        # 適用中のまま 503: 失敗と決めつけず /api/instance の fdd[] で結果を見る
        c = self.make(ops=self.ops0, fdd_status=503,
                      fdd_error='fdd request is being applied but did not finish in '
                                'time; see GET /api/instance fdd[]')
        rc = self.run_main(c, ['fdd', '--drive', '2', '--insert', 'other.d88'])
        self.assertEqual(rc, 0, self.err.getvalue())
        out = self.out.getvalue()
        self.assertIn('being applied', out)
        self.assertIn('fdd2 ready %s' % (WIN + '\\other.d88'), out)

    def test_being_applied_503_eject_checks_instance(self):
        c = self.make(ops=self.ops0, fdd_status=503,
                      fdd_error='fdd request is being applied but did not finish in '
                                'time; see GET /api/instance fdd[]')
        self.assertEqual(self.run_main(c, ['fdd', '--drive', '1', '--eject']), 0,
                         self.err.getvalue())
        self.assertIn('fdd1 empty', self.out.getvalue())

    def test_being_applied_503_not_reflected_fails(self):
        # 適用中の 503 の後、反映が見えなければ失敗 (成功と決めつけない)
        c = self.make(ops=self.ops0, fdd_status=503, fdd_accepts=False,
                      fdd_error='fdd request is being applied but did not finish in '
                                'time; see GET /api/instance fdd[]')
        rc = self.run_main(c, ['fdd', '--drive', '1', '--insert', 'other.d88'])
        self.assertEqual(rc, 1)
        self.assertIn('開けなかった', self.err.getvalue())

    def test_other_503_is_failure(self):
        for err in ('fdd request withdrawn: the UI thread did not get to it in time '
                    '(nothing was changed)',
                    'emulation thread did not pause or the UI section was busy (2s); '
                    'nothing was changed',
                    'UI thread did not respond (5s); nothing was changed'):
            self.out, self.err = io.StringIO(), io.StringIO()
            c = self.make(ops=self.ops0, fdd_status=503, fdd_error=err)
            self.assertEqual(self.run_main(c, ['fdd', '--drive', '1', '--eject']), 1, err)
            self.assertIn('nothing was changed', self.err.getvalue())
            self.assertIn('503', self.err.getvalue())
            self.assertEqual([x for x in self.http.calls if x[1] == '/api/instance'][1:], [])

    def test_missing_token_file_fails_before_calling_fdd(self):
        os.remove(os.path.join(self.dir, TOKEN_NAME))
        c = self.make(ops=self.ops0)
        self.assertEqual(self.run_main(c, ['fdd', '--drive', '1', '--eject']), 1)
        self.assertIn('トークンが読めない', self.err.getvalue())
        self.assertNotIn(TOKEN, self.err.getvalue() + self.out.getvalue())
        self.assertEqual([x for x in self.http.calls if x[1] == '/api/fdd'], [])

    def test_wrong_token_is_reported_as_401(self):
        c = self.make(ops=self.ops0, token='11' * 32)
        self.assertEqual(self.run_main(c, ['fdd', '--drive', '1', '--eject']), 1)
        self.assertIn('401', self.err.getvalue())

    def test_token_file_from_env_wins(self):
        alt = os.path.join(self.dir, 'alt.token')
        with open(alt, 'w') as f:
            f.write('ab' * 32 + '\n')
        os.environ['NP21W_AIDEBUG_TOKEN_FILE'] = alt
        c = self.make(ops=self.ops0, token='ab' * 32,
                      token_file=WIN + '\\' + TOKEN_NAME)
        self.assertEqual(self.run_main(c, ['fdd', '--drive', '1', '--eject']), 0,
                         self.err.getvalue())

    def test_token_file_from_instance_maps_to_wsl(self):
        c = self.make(ops=self.ops0)
        self.assertEqual(c.token_file({'token_file': 'D:\\Emu\\np21w_aidebug_9000.token'}),
                         '/mnt/d/Emu/np21w_aidebug_9000.token')
        self.assertEqual(c.token_file({'token_file': ''}),
                         os.path.join(self.dir, TOKEN_NAME))
        self.assertEqual(ctl.to_wsl_path('\\\\srv\\x'), None)
        self.assertEqual(ctl.aidebug_port('http://127.0.0.1:9000'), 9000)
        self.assertEqual(ctl.aidebug_port('http://localhost'), 8025)

    def test_instance_without_token_file_explains(self):
        os.remove(os.path.join(self.dir, TOKEN_NAME))
        c = self.make(ops=self.ops0, token_file='')
        self.assertEqual(self.run_main(c, ['fdd', '--drive', '1', '--eject']), 1)
        self.assertIn('CreateFile failed', self.err.getvalue())

    def test_old_fork(self):
        c = self.make('old', ops=self.ops0)
        self.assertEqual(self.run_main(c, ['fdd', '--drive', '1', '--eject']), 1)
        self.assertIn('make deploy', self.err.getvalue())

    def test_missing_image_is_argument_error(self):
        c = self.make(ops=self.ops0)
        self.assertEqual(self.run_main(c, ['fdd', '--drive', '1', '--insert', 'none.d88']), 2)

    def test_other_location_refused(self):
        c = self.make(ops=self.ops0, inst_exe='D:\\Other\\np21x64w.exe')
        self.assertEqual(self.run_main(c, ['fdd', '--drive', '1', '--eject']), 1)
        self.assertEqual([x for x in self.http.calls if x[1] == '/api/fdd'], [])

    def test_api_error_is_reported(self):
        c = self.make(ops=self.ops0, fdd_status=409)
        self.assertEqual(self.run_main(c, ['fdd', '--drive', '4', '--eject']), 1)
        self.assertIn('409', self.err.getvalue())


class Status(Base):
    def test_ini_derived_media_listing(self):
        c = self.make('down', ops=FakeOps(self.fc, locks={ISO: 5}))
        self.assertEqual(self.run_main(c, ['status', '--ini', 'test.ini']), 0)
        out = self.out.getvalue()
        self.assertIn('媒体 (ini test.ini):', out)
        self.assertIn('free:   ' + NHD, out)
        self.assertIn('locked: ' + ISO, out)
        self.assertIn('free:   ' + D88, out)
        self.assertNotIn('WRONG', out)
        self.assertIn('aidebug: down', out)

    def test_default_media_and_instance_and_dialog(self):
        ops = FakeOps(self.fc, running=[ctl.Proc(PID, 'np21x64w.exe', EXE)])
        c = self.make(ops=ops, dialog={'modal': True, 'title': 'T', 'text': ['msg'],
                                       'buttons': [{'id': 1, 'text': 'OK'}]})
        self.assertEqual(self.run_main(c, ['status']), 0)
        out = self.out.getvalue()
        self.assertIn('媒体 (default):', out)
        self.assertIn('aidebug: up pid=%d' % PID, out)
        self.assertIn('dialog: open (modal)', out)
        self.assertIn('msg', out)

    def test_no_dialog_line(self):
        ops = FakeOps(self.fc, running=[ctl.Proc(PID, 'np21x64w.exe', EXE)])
        c = self.make(ops=ops)
        self.assertEqual(self.run_main(c, ['status']), 0)
        self.assertIn('dialog: none', self.out.getvalue())

    def test_token_and_pending_lines(self):
        ops = FakeOps(self.fc, running=[ctl.Proc(PID, 'np21x64w.exe', EXE)])
        c = self.make(ops=ops, token_file=WIN + '\\' + TOKEN_NAME, paused='user')
        self.http.fdd[2] = {'path': '', 'cfg': WIN + '\\other.d88', 'pending': True}
        self.http.fdd_inserted_at[2] = 0
        self.assertEqual(self.run_main(c, ['status']), 0)
        out = self.out.getvalue()
        self.assertIn('token: ' + WIN + '\\' + TOKEN_NAME, out)
        self.assertIn('paused: user', out)
        self.assertIn('fdd2: pending ' + WIN + '\\other.d88', out)
        self.assertNotIn(TOKEN, out)

    def test_old_fork_line(self):
        ops = FakeOps(self.fc, running=[ctl.Proc(PID, 'np21x64w.exe', EXE)])
        c = self.make('old', ops=ops)
        self.assertEqual(self.run_main(c, ['status']), 0)
        self.assertIn('古いフォーク', self.out.getvalue())


class Cd(Base):
    def setUp(self):
        Base.setUp(self)
        self.ops0 = FakeOps(self.fc, running=[ctl.Proc(PID, 'np21x64w.exe', EXE)])

    def cd_calls(self):
        return [x for x in self.http.calls if x[1] == '/api/cd']

    def test_insert_name_into_empty_default_drive(self):
        c = self.make(ops=self.ops0)
        self.assertEqual(self.run_main(c, ['cd', 'os32_install.iso']), 0, self.err.getvalue())
        call = self.cd_calls()[0]
        # --drive を省けば drive を送らない (NP21/W が CD-ROM の最初のスロットを選ぶ)
        self.assertEqual(parse_qs(call[2]), {'action': ['insert'], 'path': [ISO]})
        self.assertEqual(call[4], {'X-Aidebug-Token': TOKEN})
        out = self.out.getvalue()
        self.assertIn('ide3 (cdrom) insert %s 受理 (ready)' % ISO, out)
        self.assertIn('ide3 (cdrom) ready ' + ISO, out)

    def test_insert_with_drive_and_local_windows_path(self):
        win = 'D:\\iso\\a.iso'
        local = os.path.join(self.dir, 'os32_install.iso')
        c = self.make(ops=self.ops0)
        with mock.patch.object(ctl, 'to_wsl_path',
                               side_effect=lambda p: local if p == win else None):
            self.assertEqual(self.run_main(c, ['cd', win, '--drive', '3']), 0,
                             self.err.getvalue())
        self.assertEqual(parse_qs(self.cd_calls()[0][2]),
                         {'drive': ['3'], 'action': ['insert'], 'path': [win]})
        self.assertIn('ide3 (cdrom) ready ' + win, self.out.getvalue())

    def test_unc_is_refused_before_calling(self):
        # NP21/W は UNC を 400 で断る (HTTP スレッドの同期 I/O で止まらないため)。ctl も先に断る
        for unc in ('\\\\srv\\share\\a.iso', '//srv/share/a.iso'):
            c = self.make(ops=self.ops0)
            self.assertEqual(self.run_main(c, ['cd', unc]), 2, unc)
            self.assertIn('UNC', self.err.getvalue())
            self.assertEqual(self.cd_calls(), [])

    def test_image_spec_resolution(self):
        c = self.make(ops=self.ops0)
        self.assertEqual(c.cd_image('os32_install.iso'),
                         (ISO, os.path.join(self.dir, 'os32_install.iso')))
        self.assertEqual(c.cd_image('/mnt/d/iso/a.iso'), ('D:\\iso\\a.iso', '/mnt/d/iso/a.iso'))
        self.assertEqual(c.cd_image('C:/x/a.iso'), ('C:\\x\\a.iso', '/mnt/c/x/a.iso'))
        for bad in ('/home/me/a.iso', 'rel\\a.iso', 'a b.iso', '../a.iso', 'x:a.iso',
                    '\\\\srv\\a.iso'):
            with self.assertRaises(ctl.CtlError) as cm:
                c.cd_image(bad)
            self.assertEqual(cm.exception.code, 2, bad)

    def test_missing_image_is_argument_error(self):
        c = self.make(ops=self.ops0)
        self.assertEqual(self.run_main(c, ['cd', 'none.iso']), 2)
        self.assertEqual(self.run_main(c, ['cd', '/mnt/c/__no_such_dir__/a.iso']), 2)
        self.assertEqual(self.cd_calls(), [])

    def test_swap_waits_until_changed(self):
        c = self.make(ops=self.ops0)
        self.http.ide[3]['path'] = ISO
        self.assertEqual(self.run_main(c, ['cd', 'other.iso']), 0, self.err.getvalue())
        out = self.out.getvalue()
        self.assertIn('受理 (changing)', out)
        self.assertIn('ide3 (cdrom) ready ' + WIN + '\\other.iso', out)
        self.assertGreater(self.fc.now, 0)

    def test_trapped_before_post_is_409(self):
        # 実サーバーはブレーク中の POST を 409 で断る (受理しない)
        c = self.make(ops=self.ops0, paused='trap')
        self.assertEqual(self.run_main(c, ['cd', 'other.iso']), 1)
        err = self.err.getvalue()
        self.assertIn('409', err)
        self.assertIn('/api/resume', err)
        self.assertEqual(self.fc.now, 0)

    def test_trapped_after_accept_says_break_not_ui(self):
        # 受理の後にブレーク: 快照を取り直せず media_fresh:false のまま。
        # 「UI スレッドが答えない」ではなくブレークと言う
        c = self.make(ops=self.ops0, trap_after_cd=True)
        self.http.ide[3]['path'] = ISO
        self.assertEqual(self.run_main(c, ['cd', 'other.iso']), 1)
        err = self.err.getvalue()
        self.assertIn('media_fresh:false', err)
        self.assertIn('ブレーク', err)
        self.assertNotIn('UI スレッドが答えない', err)
        self.assertGreaterEqual(self.fc.now, 15)

    def test_stale_without_pause_says_ui(self):
        c = self.make(ops=self.ops0)
        self.http.stale_after_cd = 10 ** 6
        self.assertEqual(self.run_main(c, ['cd', 'other.iso']), 1)
        self.assertIn('UI スレッドが答えない', self.err.getvalue())

    def test_swap_stuck_while_user_paused_fails_with_reason(self):
        # 利用者の一時停止では快照は取れる (fresh) がエミュレーション時間が進まない
        c = self.make(ops=self.ops0, paused='user')
        self.http.ide[3]['path'] = ISO
        self.assertEqual(self.run_main(c, ['cd', 'other.iso']), 1)
        err = self.err.getvalue()
        self.assertIn('changing', err)
        self.assertIn('一時停止', err)
        self.assertGreaterEqual(self.fc.now, 15)

    def test_swap_not_opened_fails(self):
        c = self.make(ops=self.ops0, cd_accepts=False)
        self.http.ide[3]['path'] = ISO
        self.assertEqual(self.run_main(c, ['cd', 'other.iso']), 1)
        self.assertIn('開けなかった', self.err.getvalue())

    def test_insert_into_empty_not_opened_is_422(self):
        c = self.make(ops=self.ops0, cd_accepts=False)
        self.assertEqual(self.run_main(c, ['cd', 'other.iso']), 1)
        self.assertIn('422', self.err.getvalue())

    def test_reinsert_same_iso_with_stale_snapshot_fails(self):
        # 同じ ISO の入れ直し: 操作前の古い快照 (media_fresh:false) でも path が一致する
        c = self.make(ops=self.ops0)
        self.http.ide[3]['path'] = ISO
        self.http.stale_after_cd = 10 ** 6
        self.assertEqual(self.run_main(c, ['cd', 'os32_install.iso']), 1)
        self.assertIn('確かめられなかった', self.err.getvalue())
        self.assertNotIn('ready', self.out.getvalue())

    def test_ready_wait_zero_does_not_wait(self):
        c = self.make(ops=self.ops0)
        self.http.ide[3]['path'] = ISO
        self.assertEqual(self.run_main(c, ['cd', 'other.iso', '--ready-wait', '0']), 0,
                         self.err.getvalue())
        self.assertIn('ide3 (cdrom) changing (反映は待たない)', self.out.getvalue())
        self.assertEqual(self.fc.now, 0)

    def test_post_timeout_covers_server_worst_case(self):
        # サーバーは最悪 21 秒 (5 + 6 + 10) 待って答える。それより短く切らない
        c = self.make(ops=self.ops0)
        self.assertEqual(self.run_main(c, ['cd', 'os32_install.iso']), 0, self.err.getvalue())
        self.assertGreaterEqual(self.cd_calls()[0][3], 25)

    def test_being_applied_503_checks_instance(self):
        # 適用中のまま 503: 失敗と決めつけず /api/instance で結果を見る (drive 省略 = CD の最初)
        c = self.make(ops=self.ops0, cd_status=(503, 'cd request is being applied but did '
                                                'not finish in time; see GET /api/instance ide[]'))
        self.assertEqual(self.run_main(c, ['cd', 'os32_install.iso']), 0, self.err.getvalue())
        out = self.out.getvalue()
        self.assertIn('being applied', out)
        self.assertIn('ide3 (cdrom) ready ' + ISO, out)

    def test_other_503_is_failure(self):
        c = self.make(ops=self.ops0, cd_status=(503, 'cd request withdrawn: the UI thread did '
                                                'not get to it in time (nothing was changed)'))
        self.assertEqual(self.run_main(c, ['cd', 'os32_install.iso']), 1)
        self.assertIn('nothing was changed', self.err.getvalue())

    def test_eject(self):
        c = self.make(ops=self.ops0)
        self.http.ide[3]['path'] = ISO
        self.assertEqual(self.run_main(c, ['cd', '--eject']), 0, self.err.getvalue())
        self.assertEqual(self.cd_calls()[0][2], 'action=eject')
        self.assertIn('ide3 (cdrom) empty', self.out.getvalue())

    def test_eject_with_drive(self):
        c = self.make(ops=self.ops0)
        self.assertEqual(self.run_main(c, ['cd', '--eject', '--drive', '3']), 0)
        self.assertEqual(self.cd_calls()[0][2], 'drive=3&action=eject')

    def test_not_a_cd_slot_is_reported(self):
        c = self.make(ops=self.ops0)
        self.assertEqual(self.run_main(c, ['cd', '--eject', '--drive', '1']), 1)
        self.assertIn('409', self.err.getvalue())
        self.assertIn('not a CD-ROM', self.err.getvalue())

    def test_old_fork_by_api_version_does_not_post(self):
        c = self.make(ops=self.ops0, api_version=2)
        self.assertEqual(self.run_main(c, ['cd', '--eject']), 1)
        self.assertIn('api_version 3 未満', self.err.getvalue())
        self.assertEqual(self.cd_calls(), [])

    def test_unknown_endpoint_is_old_fork(self):
        c = self.make(ops=self.ops0, cd_status=(404, 'unknown endpoint'))
        self.assertEqual(self.run_main(c, ['cd', '--eject']), 1)
        self.assertIn('make deploy', self.err.getvalue())

    def test_missing_token_fails_before_calling_cd(self):
        os.remove(os.path.join(self.dir, TOKEN_NAME))
        c = self.make(ops=self.ops0)
        self.assertEqual(self.run_main(c, ['cd', '--eject']), 1)
        self.assertIn('トークンが読めない', self.err.getvalue())
        self.assertEqual(self.cd_calls(), [])

    def test_wrong_token_is_reported_as_401(self):
        c = self.make(ops=self.ops0, token='11' * 32)
        self.assertEqual(self.run_main(c, ['cd', '--eject']), 1)
        self.assertIn('401', self.err.getvalue())

    def test_other_location_refused(self):
        c = self.make(ops=self.ops0, inst_exe='D:\\Other\\np21x64w.exe')
        self.assertEqual(self.run_main(c, ['cd', '--eject']), 1)
        self.assertEqual(self.cd_calls(), [])

    def test_argument_errors(self):
        for argv in (['cd'], ['cd', 'os32_install.iso', '--eject'],
                     ['cd', '--eject', '--drive', '5'],
                     ['cd', 'os32_install.iso', '--ready-wait', '-1']):
            c = self.make(ops=self.ops0)
            self.assertEqual(self.run_main(c, argv), 2, argv)
            self.assertEqual(self.cd_calls(), [], argv)

    def test_status_shows_empty_and_changing_cd(self):
        c = self.make(ops=self.ops0)
        self.assertEqual(self.run_main(c, ['status']), 0)
        self.assertIn('ide3 (cdrom): empty', self.out.getvalue())
        self.assertIn('ide1 (hdd): ' + NHD, self.out.getvalue())
        self.out = io.StringIO()
        c = self.make(ops=self.ops0, paused='user')
        self.http.ide[3] = {'type': 'cdrom', 'path': '', 'changing': True, 'next': ISO}
        self.http.cd_changed_at[3] = 0
        self.assertEqual(self.run_main(c, ['status']), 0)
        self.assertIn('ide3 (cdrom): changing -> ' + ISO, self.out.getvalue())


class Arguments(Base):
    def test_paths_are_rejected(self):
        for bad in ('../x.ini', 'C:\\x.ini', 'a/b.ini', '.hidden'):
            c = self.make()
            self.assertEqual(self.run_main(c, ['start', '--ini', bad]), 2, bad)
            self.assertEqual(self.ops.started, [])

    def test_missing_ini_file_is_argument_error(self):
        c = self.make()
        self.assertEqual(self.run_main(c, ['start', '--ini', 'none.ini']), 2)


class Resolve(unittest.TestCase):
    def test_env_then_dotenv_then_sample(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, '.env.sample'), 'w') as f:
                f.write('NP21W_DIR=/sample\n')
            self.assertEqual(ctl.resolve_np21w_dir({}, d), '/sample')
            with open(os.path.join(d, '.env'), 'w') as f:
                f.write('X=1\nNP21W_DIR=/mnt/c/np\n')
            self.assertEqual(ctl.resolve_np21w_dir({}, d), '/mnt/c/np')
            self.assertEqual(ctl.resolve_np21w_dir({'NP21W_DIR': '/e'}, d), '/e')

    def test_win_np21w_dir_route(self):
        p = ctl.paths_from_env({'NP21W_DIR': '/mnt/c/np', 'WIN_NP21W_DIR': 'D:\\Emu\\np21\\'})
        self.assertEqual(p.win_of('a.ini'), 'D:\\Emu\\np21\\a.ini')
        self.assertEqual(p.wsl_of('a.ini'), '/mnt/c/np/a.ini')
        p = ctl.paths_from_env({'NP21W_DIR': '/mnt/c/Users/a/np21w'})
        self.assertEqual(p.win_of('a.ini'), 'C:\\Users\\a\\np21w\\a.ini')

    def test_win_norm(self):
        self.assertEqual(ctl.win_norm('C:/NP21/NP21X64W.EXE'), 'c:\\np21\\np21x64w.exe')


class HttpFallback(unittest.TestCase):
    def test_curl_status_parsing(self):
        h = ctl.Http('http://127.0.0.1:1')
        res = mock.Mock(returncode=0, stdout=b'{"dialog":true}\n503')
        with mock.patch.object(ctl.urllib.request, 'urlopen',
                               side_effect=ctl.urllib.error.URLError('x')), \
                mock.patch.object(ctl.subprocess, 'run', return_value=res):
            self.assertEqual(h.request('GET', '/api/status'), (503, '{"dialog":true}'))
        res = mock.Mock(returncode=0, stdout=b'\n000')
        with mock.patch.object(ctl.urllib.request, 'urlopen',
                               side_effect=ctl.urllib.error.URLError('x')), \
                mock.patch.object(ctl.subprocess, 'run', return_value=res):
            self.assertIsNone(h.request('GET', '/api/status'))


# ---------------------------------------------------------------------------
# 変異 (--mutate)。写しを変異させて全ケースを回し、1 件以上落ちれば RED。
# 写しが import できない変異は数えない (NOT COUNTED)。恒等の対照は GREEN が正しい。
# ---------------------------------------------------------------------------
IDENTITY = ('# ---------------------------------------------------------------------------\n# パス\n',
            '# ---------------------------------------------------------------------------\n# パス (identity)\n',
            '恒等の対照 (コメントだけ変える — GREEN でなければ仕掛けが壊れている)')

MUTATIONS = [
    ("                    free_since.pop(p, None)\n                    last_free.pop(p, None)",
     "                    pass",
     "ロックされても数え直さない (Codex 1 / Fable M1)"),
    ("        while True:\n            probe_list = [p for p in order if p not in missing]",
     "        while True:\n            probe_list = [p for p in order if p not in missing and not settled(p)]",
     "安定した媒体をプローブし直さない (Codex 1)"),
    ("                if all(state.get(p) in ('free', 'missing') for p in order):",
     "                if True:",
     "起動直前の全体プローブの結果を見ない (Fable M1)"),
    ("                if self.settle:\n                    self.sleep(self.settle)\n",
     "",
     "起動直前に settle を置かない"),
    ("                if result is None or self.clock() > deadline:\n                    raise timeout_error()\n                apply(result, self.clock(), final=True)",
     "                if result is None:\n                    raise timeout_error()\n                apply(result, self.clock(), final=True)",
     "期限を過ぎてから得た起動直前の結果を成功に数える (Codex 4)。"
     "待ちの途中の同じ検査は、この最後の検査が必ず後に来るので変異させても等価"),
    ("        return self.ops.probe(paths, remaining)",
     "        return self.ops.probe(paths, 60)",
     "プローブに残り時間を渡さない (Codex 4)"),
    ("    if rc != 0:\n        raise ProbeError(",
     "    if False:\n        raise ProbeError(",
     "PowerShell の失敗をロックと区別しない (Fable M2)"),
    ("    if errors:\n        raise ProbeError(",
     "    if False:\n        raise ProbeError(",
     "error 行を無視する (Fable M2)"),
    ("    if lacking or 'end' not in lines:",
     "    if False:",
     "報告されなかったパスを見逃す (Fable M2)"),
    ("  catch [System.IO.IOException] { \"locked`t$i\" }\n",
     "",
     "PowerShell の例外を分けない (Fable M2)"),
    ("    if len(value) >= 2 and value[0] == '\"' and value[-1] == '\"':",
     "    if False:",
     "ini の引用符を外さない (Codex 3)"),
    ("            if k in wanted and k not in found:",
     "            if k in wanted:",
     "節内の 2 回目の値で上書きする (NP21/W は最初を採る)"),
    ("            if in_section:\n                break\n",
     "",
     "2 つ目の節まで読む"),
    ("                   ['SCSIHDD%d' % i for i in range(0, 4)])",
     "                   [])",
     "SCSIHDD を待たない (Fable L2)"),
    ("            (ours if p.exe and win_norm(p.exe) == want else others).append(p)",
     "            (ours if 'np21' in p.name.lower() else others).append(p)",
     "名前の部分一致で強制終了の対象にする (Codex 2)"),
    ("            body = urllib.parse.urlencode({'save': '0',",
     "            body = urllib.parse.urlencode({'save': '1',",
     "quit で ini を書き戻させる"),
    ("                    if int(inst['pid']) != pid or win_norm(inst.get('exe')) != want_exe:",
     "                    if False:",
     "起動後に pid / exe を照合しない"),
    ("                # 成功を返す直前にもう一度\n                if not alive_now():",
     "                # 成功を返す直前にもう一度\n                if False:",
     "成功の直前に pid を見直さない (Codex 5)"),
    ("        if timeout < stable:\n            raise CtlError(",
     "        if False:\n            raise CtlError(",
     "--timeout < --stable を通す (Fable L1)"),
    ("        confirmed = not use_api\n",
     "        confirmed = False\n",
     "--api-timeout 0 でも API を待つ (Fable L3)"),
    ("                    self.fail_if_dialog('起動の確認中 (aidebug が応答しない)')",
     "                    pass",
     "API が黙っているときダイアログを見ない"),
    ("        if st == 200 and js and js.get('open') and js.get('modal', True):\n            return js",
     "        if False:\n            return js",
     "ダイアログを検出しない"),
    ("        if state == 'ok' and win_norm(inst.get('exe')) != want:",
     "        if False:",
     "別の場所の NP21/W にも quit を送る"),
    ("            args.append(self.paths.win_of(fd))",
     "            pass",
     "--fd を起動の引数に渡さない"),
    ("def win_norm(path):",
     "def win_norm(path)",
     "構文を壊す — import できない写しは RED にも GREEN にも数えないことの確認"),
    # ---- 2026-09-25 のレビュー (Codex P2 ×3、PM 8、トークン) ----
    ("        v = win_sep(values.get(_ascii_upper(key), ''))",
     "        v = values.get(_ascii_upper(key), '')",
     "ini の値の区切りを揃えない (Codex 9: C:/NP21/… が相対扱い or 生のまま)"),
    ("    path = win_sep(path)\n    return bool(re.match(",
     "    return bool(re.match(",
     "is_win_abs が `/` 区切りの絶対パスを相対と言う (Codex 9)"),
    ("        use_api = api_timeout > 0      # 0 = HTTP を 1 回も呼ばない (/api/dialog も)",
     "        use_api = True",
     "--api-timeout 0 でも HTTP (/api/dialog) を呼ぶ (Codex 10)"),
    ("            if value and now <= deadline:\n                return value",
     "            if value:\n                return value",
     "_until が期限を過ぎてから得た値を成功に数える (Codex 11)"),
    ("        if deadline is not None and self.clock() > deadline:\n            return None, None\n",
     "",
     "api() が期限を過ぎてから届いた応答を返す (Codex 11)"),
    ("            timeout = max(1.0, min(timeout, remaining))",
     "            timeout = timeout",
     "HTTP に残り時間を渡さない (Codex 11)"),
    ("            r = self.http.request('GET', '/api/tvram', None, max(1.0, min(15, remaining)))",
     "            r = self.http.request('GET', '/api/tvram', None, 15)",
     "wait-ready が /api/tvram に残り時間を渡さない (Codex 11)"),
    ("            st, js = self.api('POST', '/api/quit', body, timeout=10, headers=headers)",
     "            st, js = self.api('POST', '/api/quit', body, timeout=10)",
     "quit にトークンを付けない (7/12)"),
    ("        st, js = self.api('POST', '/api/fdd', urllib.parse.urlencode(params),\n                          timeout=FDD_POST_TIMEOUT, headers=headers)",
     "        st, js = self.api('POST', '/api/fdd', urllib.parse.urlencode(params),\n                          timeout=FDD_POST_TIMEOUT)",
     "fdd にトークンを付けない (7/12)"),
    ("            return win_norm(f.get('path') or '') == win_norm(want_path)",
     "            return True",
     "insert の反映 (fdd[].path) を確かめない (PM 8)"),
    ("            if f is None or not last.get('_fresh'):\n                return False",
     "            if f is None:\n                return False",
     "media_fresh:false の古い快照でも path が一致すれば成功にする (Codex P2)"),
    ("        if want_path and last.get('pending'):",
     "        if False:",
     "pending (エミュレーションが進んでいない) と開けない失敗を区別しない (PM 8)"),
    ("            if st == 409 and (js or {}).get('error', '').startswith('quit already requested'):",
     "            if False:",
     "先に別の方針で受け付けられた quit (409) を失敗として強制終了に落とす (Codex 6)"),
    ("        env = os.environ.get('NP21W_AIDEBUG_TOKEN_FILE')\n        if env:\n            return env\n",
     "",
     "NP21W_AIDEBUG_TOKEN_FILE を見ない"),
    ("            wsl = to_wsl_path(win)\n",
     "            wsl = None\n",
     "/api/instance の token_file を使わない"),
    # ---- 2026-09-26 cd (/api/cd) ----
    ("        st, js = self.api('POST', '/api/cd', urllib.parse.urlencode(params),\n                          timeout=CD_POST_TIMEOUT, headers=headers)",
     "        st, js = self.api('POST', '/api/cd', urllib.parse.urlencode(params),\n                          timeout=CD_POST_TIMEOUT)",
     "cd にトークンを付けない"),
    ("        if drive is not None:\n            params['drive'] = str(drive)",
     "        params['drive'] = str(drive)",
     "--drive を省いても drive を送る (NP21/W に CD-ROM の最初のスロットを選ばせない)"),
    ("            if not os.path.isfile(wsl):",
     "            if False:",
     "ISO の有無を確かめずに送る"),
    ("            win = to_win_path(image)\n            if win == image:",
     "            win = image\n            if False:",
     "/mnt/<x>/… を Windows のパスに直さない"),
    ("        if int(inst.get('api_version') or 0) < CD_API_VERSION:",
     "        if False:",
     "古いフォーク (api_version < 3) にも /api/cd を送る"),
    ("            if e is None or not last.get('_fresh') or e.get('changing'):",
     "            if e is None or e.get('changing'):",
     "cd: media_fresh:false の古い快照で成功にする"),
    ("        if last.get('changing'):\n            why =",
     "        if False:\n            why =",
     "cd: 差し替え待ち (changing) と開けない失敗を区別しない"),
    ("        if win.startswith('\\\\\\\\'):\n            raise CtlError('UNC",
     "        if False:\n            raise CtlError('UNC",
     "cd: UNC の ISO を NP21/W へ送る (Codex 1 回目 P2)"),
    ("    if last.get('_trap'):\n        return 'ブレーク",
     "    if False:\n        return 'ブレーク",
     "media_fresh:false をブレーク中でも「UI スレッドが答えない」と言う (Codex 1 回目 P3)"),
    ("CD_POST_TIMEOUT = 25\n",
     "CD_POST_TIMEOUT = 15\n",
     "/api/cd をサーバーの最悪 21 秒より短く切る (Fable 2 回目 P3-2)"),
    ("        if st == 503 and 'being applied' in (js or {}).get('error', ''):\n            # 適用は始まった (COMMIT の後) が 10 秒で終わらなかった。結果は ide[] で見る",
     "        if False:\n            # 適用は始まった (COMMIT の後) が 10 秒で終わらなかった。結果は ide[] で見る",
     "適用中の 503 を失敗と決めつけ、結果を見に行かない (Fable 2 回目 P3-2)"),
    # ---- 2026-09-26 fdd を cd と同じ状態機械に (代行レビュー P3-3) ----
    ("FDD_POST_TIMEOUT = 25\n",
     "FDD_POST_TIMEOUT = 15\n",
     "/api/fdd をサーバーの最悪 21 秒より短く切る (P3-3)"),
    ("        if st == 503 and 'being applied' in (js or {}).get('error', ''):\n            # 適用は始まった (COMMIT の後) が 10 秒で終わらなかった。結果は fdd[] で見る。",
     "        if False:\n            # 適用は始まった (COMMIT の後) が 10 秒で終わらなかった。結果は fdd[] で見る。",
     "fdd: 適用中の 503 を失敗と決めつけ、結果を見に行かない (P3-3)"),
    ("        if st == 503 and 'being applied' in (js or {}).get('error', ''):\n            # 適用は始まった (COMMIT の後) が 10 秒で終わらなかった。結果は fdd[] で見る。",
     "        if st == 503:\n            # 適用は始まった (COMMIT の後) が 10 秒で終わらなかった。結果は fdd[] で見る。",
     "fdd: 何も変えていない 503 まで結果を見に行く (P3-3)"),
    ("                elif s.get('type') == 'cdrom':",
     "                elif False:",
     "status に空の CD ドライブを出さない"),
]


def _load(path):
    spec = importlib.util.spec_from_file_location('np21w_ctl_mut', str(path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _suite_failures(mod):
    global ctl
    saved = ctl
    ctl = mod
    try:
        suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
        res = unittest.TextTestRunner(stream=io.StringIO(), verbosity=0).run(suite)
        return len(res.failures) + len(res.errors)
    finally:
        ctl = saved


def mutate():
    original = SRC.read_text(encoding='utf-8')
    bad = 0
    red = green = uncounted = 0
    with tempfile.TemporaryDirectory(prefix='np21w-ctl-mut-') as tmp:
        for i, (old, new, why) in enumerate([IDENTITY] + MUTATIONS):
            if original.count(old) != 1:
                print('MUTATION %d NOT APPLICABLE (%d 箇所): %s' % (i, original.count(old), why))
                bad += 1
                continue
            path = Path(tmp) / ('mut%d.py' % i)
            path.write_text(original.replace(old, new), encoding='utf-8')
            try:
                mod = _load(path)
            except Exception as exc:   # 写しが import できない = 数えない
                print('MUTATION %d NOT COUNTED (import: %s): %s' % (i, type(exc).__name__, why))
                uncounted += 1
                continue
            fails = _suite_failures(mod)
            if i == 0:
                ok = fails == 0
                print('IDENTITY %s (%d 件落ちた): %s'
                      % ('GREEN' if ok else '**RED (仕掛けが壊れている)**', fails, why))
                bad += not ok
                continue
            if fails:
                red += 1
                print('MUTATION %d RED (%d 件): %s' % (i, fails, why))
            else:
                green += 1
                bad += 1
                print('MUTATION %d **GREEN (見逃し)**: %s' % (i, why))
    print('MUTATION SUMMARY red=%d green=%d not_counted=%d identity=1' % (red, green, uncounted))
    return bad


if __name__ == '__main__':
    do_mutate = '--mutate' in sys.argv
    argv = [a for a in sys.argv if a != '--mutate']
    prog = unittest.main(argv=argv, exit=False)
    rc = 0 if prog.result.wasSuccessful() else 1
    if do_mutate and rc == 0:
        rc = 1 if mutate() else 0
    sys.exit(rc)
