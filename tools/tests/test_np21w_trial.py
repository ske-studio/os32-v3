"""Synthetic host integration only; never contact an emulator or real ini."""
import copy
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from contextlib import contextmanager, redirect_stdout
from unittest import mock
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import np21w_trial as trial
import np21w_ini as ini
from np21w_ini import IniError
from np21w_ini_live import wire

EXE = r'C:\Trial Fixture\np21x64w.exe'
BASE = r'C:\Trial Fixture\np21x64w.ini'
CWD = r'C:\Trial Fixture'
CREATED = '2026-09-09T01:02:03.0000000Z'
HDD = 'os32_fresh.nhd'
D88 = 'os32_boot.d88'
CP932_FDD = b'C:\\np21w\\\x93\xfa\x96{.d88'
RAW = (b'\xef\xbb\xbf[NekoProject21]\r\nUSEGD5430 = false ;keep\nGD5430TYPE=91\r\n'
       b'USEPEGCP=false\r\nExMemory=16\r\ne_resume = true\r\n'
       b'HDD1FILE=C:\\np21w\\work.nhd\r\nFDD1FILE=' + CP932_FDD + b'\r\nFDD2FILE=\r\n'
       b'opaque=\x82\xa0\n')
POST_EXIT = RAW + b'; normal exit saved settings\r\n'

def plan(**kw):
    fields = dict(exe=EXE, baseline=BASE, cwd=CWD, pid=42, created=CREATED,
                  hdd=HDD, fdd_eject=True, fdd_arg=D88)
    fields.update(kw)
    return trial.make_plan(**fields)


@contextmanager
def image_fixture(windows=CWD, temp=None):
    """NP21W_DIR とその Windows 表記は贋物 (`-w` / `-u` の往復つき)。
    実 ini・実プロセスは触らない。"""
    with tempfile.TemporaryDirectory() as fresh:
        temp = temp or fresh
        for name in (HDD, D88):
            Path(temp, name).write_bytes(b'')
        def fake_wslpath(argv, **kwargs):
            assert argv[0] == 'wslpath' and argv[1] in ('-w', '-u'), argv
            if argv[1] == '-w':
                assert argv[2] == temp, argv
                text = windows
            else:
                assert argv[2] == windows, argv
                text = temp
            return types.SimpleNamespace(returncode=0, stdout=text.encode('ascii') + b'\n')
        with mock.patch.dict(os.environ, {'NP21W_DIR': temp}), \
             mock.patch.object(ini.subprocess, 'run', side_effect=fake_wslpath):
            yield temp


class Images(unittest.TestCase):
    def setUp(self):
        fixture = image_fixture()
        self.dir = fixture.__enter__()
        self.addCleanup(fixture.__exit__, None, None, None)

class Transport:
    def __init__(self, bound, fail=None, stay=False, old_command=None):
        self.plan = bound
        self.calls = []
        self.fail = fail
        self.stay = stay
        self.files = {BASE: RAW}
        self.rows = [dict(pid=42, created=CREATED, exe=EXE,
                          command=old_command or '"' + EXE + '"')]
    def exchange(self, request):
        op, a = request['op'], wire(request['args'], decode=True)
        self.calls.append(op)
        if self.fail == len(self.calls):
            return {'ok': False}
        value = True
        if op == 'query':
            value = copy.deepcopy(self.rows)
        elif op == 'close':
            if not self.stay:
                self.rows = []
                self.files[BASE] = POST_EXIT
        elif op == 'snapshot':
            assert not self.rows, 'baseline read before exit'
            value = dict(data=self.files[BASE], signature='post-exit')
        elif op == 'create':
            assert not self.rows
            assert a['expected']['data'] == self.files[BASE]
            assert self.plan['trial'] not in self.files
            self.files[self.plan['trial']] = a['data']
        elif op == 'verify':
            assert self.files[BASE] == a['expected']['data']
            assert self.files[self.plan['trial']] == a['data']
        elif op == 'start':
            self.rows = [dict(pid=43, created='2026-09-09T01:03:03.0000000Z', exe=EXE,
                              command=trial.launch_command(self.plan))]
            value = copy.deepcopy(self.rows[0])
        return dict(ok=True, value=wire(value))
    def close(self):
        self.calls.append('dispose')

class TrialTests(Images):
    def run_bound(self, p=None, **kw):
        p = p or plan()
        transport = Transport(p, **kw)
        factory = lambda bound: trial.WindowsExecutor(bound, transport)
        gate = trial.bind_trial(p, factory, authorized=True, exclusive=True)
        return p, transport, gate

    def test_transform_changes_only_the_planned_disk_set_and_resume(self):
        changes = plan()['changes']
        actual, diff = trial.transform_trial(RAW, changes)
        self.assertEqual(actual, RAW.replace(b'e_resume = true', b'e_resume = false')
                         .replace(b'HDD1FILE=C:\\np21w\\work.nhd',
                                  ('HDD1FILE=' + CWD + '\\' + HDD).encode('ascii'))
                         .replace(b'FDD1FILE=' + CP932_FDD, b'FDD1FILE='))
        self.assertEqual(diff, ['HDD1FILE: set -> ' + CWD + '\\' + HDD,
                                'FDD1FILE: set -> empty', 'e_resume: true -> false'])
        # Cirrus keys are outside this plan's change set and stay byte-identical.
        self.assertIn(b'USEGD5430 = false ;keep', actual)
        self.assertIn(b'GD5430TYPE=91', actual)
        self.assertEqual(trial.transform_trial(actual, changes), (actual, []))

    def test_unknown_missing_duplicate_resume_or_disk_field_rejected(self):
        changes = plan()['changes']
        for raw in [RAW.replace(b'e_resume = true', b''), RAW + b'e_resume=false\n',
                    RAW.replace(b'e_resume = true', b'e_resume = 1'),
                    RAW.replace(b'HDD1FILE=C:\\np21w\\work.nhd\r\n', b''),
                    RAW.replace(b'FDD2FILE=\r\n', b''),
                    RAW.replace(b'FDD2FILE=\r\n', b'FDD2FILE=\r\nfdd2file=\r\n'),
                    b'\xff\xfe' + RAW]:
            with self.subTest(raw=raw[:40]), self.assertRaises(IniError):
                trial.transform_trial(raw, changes)
        for bad in [dict(changes, e_resume='true'), {k: v for k, v in changes.items()
                                                     if k != 'e_resume'}, None]:
            with self.subTest(changes=bad), self.assertRaises(IniError):
                trial.transform_trial(RAW, bad)

    def test_plan_rejects_unsafe_target_and_unknown_setup(self):
        os.mkdir(os.path.join(self.dir, 'as_dir.nhd'))  # 往復 1 の B5
        for change in [dict(pid=True), dict(created='yesterday'), dict(exe=r'C:\x.exe'),
                       dict(baseline=r'C:\other.ini'), dict(cwd=r'C:\elsewhere'),
                       dict(baseline=r'C:\Trial Fixture\..\np21x64w.ini'),
                       dict(hdd='missing.nhd'), dict(hdd=D88), dict(hdd=None),
                       dict(hdd=CWD + '\\' + HDD), dict(fdd_arg='missing.d88'),
                       dict(fdd_arg=HDD), dict(fdd_eject='yes'), dict(fdd_eject=1),
                       dict(hdd='as_dir.nhd')]:
            args = dict(exe=EXE, baseline=BASE, cwd=CWD, pid=42, created=CREATED,
                        hdd=HDD, fdd_eject=True, fdd_arg=D88)
            args.update(change)
            with self.subTest(change=change), self.assertRaises(IniError):
                trial.make_plan(**args)
        self.assertNotEqual(plan()['trial'], plan()['trial'])

    def test_plan_binds_the_resolved_disk_paths_and_launch_command(self):
        p = plan()
        self.assertEqual((p['hdd'], p['fdd_eject'], p['fdd_arg']),
                         (HDD, True, CWD + '\\' + D88))
        self.assertEqual((p['hdd_path'], p['hdd_host'], p['fdd_arg_host']),
                         (CWD + '\\' + HDD, os.path.join(self.dir, HDD),
                          os.path.join(self.dir, D88)))
        self.assertEqual(p['changes'], {'HDD1FILE': CWD + '\\' + HDD, 'FDD1FILE': '',
                                        'FDD2FILE': '', 'e_resume': 'false'})
        self.assertEqual(trial.launch_command(p),
                         '"%s" "/i%s" "%s\\%s"' % (EXE, p['trial'], CWD, D88))
        bare = plan(fdd_arg=None, fdd_eject=False)
        self.assertEqual(bare['changes'], {'HDD1FILE': CWD + '\\' + HDD, 'e_resume': 'false'})
        self.assertIsNone(bare['fdd_arg_host'])
        self.assertEqual(trial.launch_command(bare), '"%s" "/i%s"' % (EXE, bare['trial']))
        self.assertNotIn('.d88', trial.launch_command(bare))

    def test_a_later_environment_change_cannot_move_the_approved_hdd(self):
        """`NP21W_DIR` を A → B に変えて同じ JSON を dispatch しても、B の同名
        ファイルには切り替わらない (往復 1 の B2)。"""
        approved = plan(fdd_arg=None)
        frozen = json.dumps(approved)
        other = tempfile.TemporaryDirectory()
        self.addCleanup(other.cleanup)
        with image_fixture(windows=r'C:\Other Fixture', temp=other.name):
            self.assertEqual(trial.make_plan(exe=EXE, baseline=BASE, cwd=CWD, pid=42,
                                             created=CREATED, hdd=HDD,
                                             fdd_eject=True)['hdd_path'],
                             r'C:\Other Fixture\os32_fresh.nhd')
            p, t, gate = self.run_bound(json.loads(frozen))
            self.assertTrue(gate(frozen)['ok'])
        written = t.files[approved['trial']]
        self.assertIn(('HDD1FILE=' + CWD + '\\' + HDD).encode('ascii'), written)
        self.assertNotIn(b'Other Fixture', written)

    def test_bound_paths_must_agree_with_each_other_and_the_host(self):
        p = plan()
        for changed in [dict(p, hdd_path=r'C:\Other Fixture\os32_fresh.nhd'),
                        dict(p, hdd_path=CWD + '\\other.nhd'),
                        dict(p, hdd_host='/nowhere/' + HDD),
                        dict(p, hdd_host=os.path.join(self.dir, D88)),
                        dict(p, hdd_host=self.dir),
                        dict(p, hdd_host=None), dict(p, hdd_path=None),
                        dict(p, fdd_arg_host=None),
                        dict(p, fdd_arg_host=os.path.join(self.dir, HDD)),
                        dict(p, fdd_arg=r'C:\Other Fixture\os32_boot.d88',
                             fdd_arg_host=os.path.join(self.dir, D88))]:
            with self.subTest(changed=changed['hdd_path']), self.assertRaises(IniError):
                trial.bind_trial(changed, self.fail, authorized=True, exclusive=True)
        os.remove(os.path.join(self.dir, HDD))
        os.mkdir(os.path.join(self.dir, HDD))
        with self.assertRaises(IniError):
            trial.bind_trial(p, self.fail, authorized=True, exclusive=True)

    def test_gate_mismatch_no_executor_construction_single_use(self):
        p = plan()
        calls = []
        for proposal in [json.dumps(dict(p, pid=43)), '{}', json.dumps(p) + ' trailing',
                         '{"action":"cirrus-trial","action":"cirrus-trial"}']:
            gate = trial.bind_trial(p, lambda bound: calls.append(bound), authorized=True, exclusive=True)
            with self.assertRaises(IniError):
                gate(proposal)
        self.assertEqual(calls, [])

    def test_approval_flags_required_before_executor_and_bound_changes_fixed(self):
        p = plan()
        def forbidden(bound):
            self.fail('unapproved executor construction')
        for flags in [dict(), dict(authorized=True), dict(exclusive=True),
                      dict(authorized=1, exclusive=True)]:
            gate = trial.bind_trial(p, forbidden, **flags)
            with self.assertRaises(IniError):
                gate(json.dumps(p))
        for changed in [dict(p, changes={'USEGD5430': 'false'}), dict(p, shell='anything'),
                        dict(p, trial=BASE), dict(p, hdd='missing.nhd'),
                        dict(p, fdd_eject=False), dict(p, fdd_arg=r'C:\elsewhere\os32_boot.d88'),
                        dict(p, fdd_arg=''), dict(p, fdd_arg=CWD + '\\missing.d88')]:
            with self.subTest(changed=sorted(changed)), self.assertRaises(IniError):
                trial.bind_trial(changed, forbidden, authorized=True, exclusive=True)

    def test_immutable_binding_and_consumed_failure(self):
        p, t, gate = self.run_bound(fail=1)
        approved = json.dumps(p)
        p['pid'] = 777
        result = gate(approved)
        self.assertFalse(result['ok'])
        self.assertEqual(t.calls, ['lock', 'dispose'])
        with self.assertRaises(IniError):
            gate(approved)

    def test_full_mock_transport_order_post_exit_bytes_original_never_written(self):
        p, t, gate = self.run_bound()
        result = gate(json.dumps(p))
        self.assertTrue(result['ok'])
        self.assertEqual(t.calls, ['lock', 'preflight', 'query', 'query', 'close', 'query',
                                   'snapshot', 'query', 'create', 'query', 'verify', 'start',
                                   'query', 'query', 'dispose'])
        self.assertEqual(t.files[BASE], POST_EXIT)
        self.assertEqual(t.files[p['trial']],
                         trial.transform_trial(POST_EXIT, p['changes'])[0])
        self.assertEqual(result['process']['pid'], 43)
        self.assertEqual(result['process']['command'], trial.launch_command(p))
        self.assertIn('"/i' + p['trial'] + '"', result['process']['command'])
        self.assertEqual(result['cwd'], CWD)

    def test_full_mock_transport_without_a_disk_argument(self):
        p, t, gate = self.run_bound(plan(fdd_arg=None))
        result = gate(json.dumps(p))
        self.assertTrue(result['ok'])
        self.assertEqual(result['process']['command'], '"%s" "/i%s"' % (EXE, p['trial']))
        self.assertEqual(t.files[p['trial']],
                         trial.transform_trial(POST_EXIT, p['changes'])[0])
        self.assertNotIn(b'FDD1FILE=C', t.files[p['trial']])

    def test_old_command_is_not_active_ini_evidence(self):
        for command in ['"' + EXE + '"', '"' + EXE + '" "C:\\old.ini"', 'unfamiliar old launch']:
            p, t, gate = self.run_bound(old_command=command)
            self.assertTrue(gate(json.dumps(p))['ok'])
            self.assertEqual(t.files[BASE], POST_EXIT)

    def test_exit_failure_prevents_reads_writes_and_restart(self):
        p, t, gate = self.run_bound(stay=True)
        result = gate(json.dumps(p))
        self.assertFalse(result['ok'])
        self.assertNotIn('snapshot', t.calls)
        self.assertEqual(t.files, {BASE: RAW})

    def test_every_failed_exchange_prevents_next_operation(self):
        for index in range(1, 15):
            p, t, gate = self.run_bound(fail=index)
            result = gate(json.dumps(p))
            self.assertFalse(result['ok'], index)
            self.assertEqual(len(t.calls), index + 1)
            self.assertEqual(t.calls[-1], 'dispose')
            self.assertNotIn('restore', t.calls)
            self.assertLessEqual(t.calls.count('start'), 1)

    def test_pid_creation_exe_multiple_and_malformed_fail_before_close(self):
        for change in [dict(pid=43), dict(created='2026-09-09T00:00:00.0000000Z'), dict(exe=r'C:\other.exe')]:
            p, t, gate = self.run_bound()
            t.rows[0].update(change)
            self.assertFalse(gate(json.dumps(p))['ok'])
            self.assertNotIn('close', t.calls)
        for rows in [None, [], [{}, {}], [{}]]:
            p, t, gate = self.run_bound()
            t.rows = rows
            self.assertFalse(gate(json.dumps(p))['ok'])
            self.assertNotIn('close', t.calls)

    def test_dry_cli_no_factory_or_llm_and_execute_needs_exclusivity(self):
        args = ['--exe', EXE, '--baseline', BASE, '--cwd', CWD, '--pid', '42', '--created', CREATED,
                '--hdd', HDD, '--fdd-eject', '--fdd-arg', D88]
        def forbidden(*a):
            self.fail('dry-run made external call')
        with redirect_stdout(io.StringIO()) as out:
            self.assertEqual(trial.main(args, executor_factory=forbidden, llm=forbidden), 0)
        dry = json.loads(out.getvalue())
        self.assertEqual(dry['mode'], 'dry_run')
        self.assertEqual((dry['plan']['hdd'], dry['plan']['fdd_eject'],
                          dry['plan']['fdd_arg']), (HDD, True, CWD + '\\' + D88))
        with redirect_stdout(io.StringIO()):
            self.assertNotEqual(trial.main(args + ['--execute'], executor_factory=forbidden, llm=forbidden), 0)

    def test_transport_cleanup_failure_cannot_report_success(self):
        p, t, gate = self.run_bound()
        def fail_close():
            raise OSError('sensitive transport diagnostic')
        t.close = fail_close
        result = gate(json.dumps(p))
        self.assertFalse(result['ok'])
        self.assertNotIn('sensitive', json.dumps(result))

    def test_new_process_mismatch_and_midflight_identity_change(self):
        for op, mutation in [('start', 'command'), ('start', 'created'), ('query', 'created')]:
            p, t, gate = self.run_bound()
            exchange = t.exchange
            def corrupt(request):
                response = exchange(request)
                if request['op'] == op:
                    if op == 'start':
                        response['value'][mutation] = CREATED if mutation == 'created' else 'wrong command'
                    elif t.calls.count('query') == 2:
                        response['value'][0]['created'] = '2026-09-09T00:00:00.0000000Z'
                return response
            t.exchange = corrupt
            self.assertFalse(gate(json.dumps(p))['ok'])
            self.assertLessEqual(t.calls.count('start'), 1)
            if op == 'query':
                self.assertNotIn('close', t.calls)


    def test_launch_command_matches_the_observed_cim_command_line(self):
        """受入 F1 で Win32_Process の CommandLine に実際に現れた形を固定する:
        各引数が二重引用符、区切りは 1 個の空白、ini は `/i` の直後に連結。"""
        p = plan()
        self.assertEqual(trial.launch_command(p),
                         '"' + EXE + '" "/i' + p['trial'] + '" "' + CWD + '\\' + D88 + '"')
        bare = plan(fdd_arg=None)
        self.assertEqual(trial.launch_command(bare), '"' + EXE + '" "/i' + bare['trial'] + '"')
        for command, quotes in ((trial.launch_command(p), 6), (trial.launch_command(bare), 4)):
            self.assertNotIn('  ', command)
            self.assertNotIn('" "/i ', command)
            self.assertEqual(command.count('"'), quotes)

    def test_started_identity_tolerates_cim_precision_and_exe_case(self):
        """CIM の CreationDate は 7 桁目が 0 のマイクロ秒 (`.7896090Z`)。
        Python 側は created を「古い行と違うこと」だけで見る。exe は大小文字を
        無視する (CIM は実体の綴りを返す)。"""
        p = plan()
        cim = '2026-09-14T09:15:42.7896090Z'
        started = dict(pid=43, created=cim, exe=EXE.upper(),
                       command=trial.launch_command(p))
        old = dict(pid=42, created=CREATED, exe=EXE, command='"' + EXE + '"')
        self.assertEqual(trial.identity_mismatch(started, old, p), [])
        self.assertEqual(trial._identity(dict(started)), started)

    def test_started_identity_names_what_disagrees(self):
        p = plan()
        old = dict(pid=42, created=CREATED, exe=EXE, command='"' + EXE + '"')
        good = dict(pid=43, created='2026-09-14T09:15:42.7896090Z', exe=EXE,
                    command=trial.launch_command(p))
        cases = [(dict(good, command='"' + EXE + '" "' + p['trial'] + '"'), ['command']),
                 (dict(good, command=trial.launch_command(p) + ' extra'), ['command']),
                 (dict(good, exe=r'C:\Other\np21x64w.exe'), ['exe']),
                 (dict(good, created=CREATED), ['created']),
                 (dict(good, created=CREATED, exe=r'C:\Other\np21x64w.exe'), ['created', 'exe'])]
        for started, expected in cases:
            with self.subTest(started=started['command'][-24:]):
                self.assertEqual(trial.identity_mismatch(started, old, p), expected)

    def test_start_failure_keeps_the_started_process_in_the_result(self):
        p, t, gate = self.run_bound()
        exchange = t.exchange
        def corrupt(request):
            response = exchange(request)
            if request['op'] == 'start':
                response['value']['command'] = '"' + EXE + '" "' + p['trial'] + '"'
            return response
        t.exchange = corrupt
        result = gate(json.dumps(p))
        self.assertFalse(result['ok'])
        self.assertEqual(result['stage'], 'start')
        self.assertEqual(result['process']['pid'], 43)      # 起動した対象が残る
        self.assertIn('identity mismatch: command', result['reason'])
        self.assertIn('no automatic recovery', result['reason'])

    def test_executor_rejection_reports_its_code_and_started_pid(self):
        class Rejecting(Transport):
            def exchange(self, request):
                if request['op'] == 'start':
                    self.calls.append('start')
                    return {'ok': False, 'code': 'identity mismatch: created',
                            'pid': 4321}
                return Transport.exchange(self, request)
        p = plan()
        t = Rejecting(p)
        gate = trial.bind_trial(p, lambda bound: trial.WindowsExecutor(bound, t),
                                authorized=True, exclusive=True)
        result = gate(json.dumps(p))
        self.assertFalse(result['ok'])
        self.assertEqual(result['stage'], 'start')
        self.assertEqual(result['started_pid'], 4321)
        self.assertIn('executor: identity mismatch: created', result['reason'])
        self.assertNotIn('process', result)

    def test_rejection_details_are_a_fixed_vocabulary(self):
        """executor から届く自由文・巨大な値・偽の PID は理由に出さない。"""
        for code, pid in [('C:\\secret\\path.ini', 7), ('x' * 200, 7), (None, 7),
                          ('identity mismatch: command', 'many'),
                          ('identity mismatch: command', True),
                          ('identity mismatch: command', -1), (123, 0)]:
            class Rejecting(Transport):
                def exchange(self, request):
                    if request['op'] == 'start':
                        self.calls.append('start')
                        return {'ok': False, 'code': code, 'pid': pid}
                    return Transport.exchange(self, request)
            p = plan()
            t = Rejecting(p)
            gate = trial.bind_trial(p, lambda bound: trial.WindowsExecutor(bound, t),
                                    authorized=True, exclusive=True)
            result = gate(json.dumps(p))
            with self.subTest(code=code, pid=pid):
                self.assertFalse(result['ok'])
                text = json.dumps(result)
                self.assertNotIn('secret', text)
                self.assertNotIn('x' * 80, text)
                if not isinstance(pid, int) or isinstance(pid, bool) or pid <= 0:
                    self.assertNotIn('started_pid', result)


    def test_cim_row_jitter_after_start_names_the_field(self):
        """起動確認の 2 回の照会で CIM の行が揺れたとき (同じ PID で command の
        綴りだけ違う等)、どの項目かを理由に出す。実走 F3 で dispose 段まで
        進んだのでここは通ったが、揺れたときの診断を固定しておく。"""
        for field, value in [('command', '"' + EXE + '" "/iC:\\other.ini"'),
                             ('created', '2026-09-09T02:03:04.0000000Z'),
                             ('pid', 44)]:
            p, t, gate = self.run_bound()
            exchange = t.exchange
            def corrupt(request, field=field, value=value):
                response = exchange(request)
                if request['op'] == 'query' and t.calls.count('query') == 6:
                    response['value'][0][field] = value
                return response
            t.exchange = corrupt
            result = gate(json.dumps(p))
            with self.subTest(field=field):
                self.assertFalse(result['ok'])
                self.assertIn('identity unstable: ' + field, result['reason'])
                self.assertEqual(result['started_pid'], 43)
                self.assertEqual(result['process']['pid'], 43)
                self.assertEqual(result['stage'], 'query')

    def test_exe_case_alone_is_not_instability(self):
        p, t, gate = self.run_bound()
        exchange = t.exchange
        def recase(request):
            response = exchange(request)
            if request['op'] == 'query' and t.calls.count('query') >= 6:
                response['value'][0]['exe'] = EXE.upper()
            return response
        t.exchange = recase
        self.assertTrue(gate(json.dumps(p))['ok'])
        self.assertEqual(trial.identity_unstable([], dict(pid=43)), ['rows'])

    def test_dispose_failure_reports_its_cause_and_keeps_the_started_pid(self):
        """起動した NP21/W は PowerShell の stdout ハンドルを継承するので、
        reader は EOF に届かない。失敗を成功には変えない (既存の不変条件) が、
        理由と起動済み PID は結果に残す。"""
        for code, expected in [('cleanup: inherited pipe still open', 'inherited pipe'),
                               ('cleanup: executor exit timeout', 'exit timeout')]:
            p, t, gate = self.run_bound()
            def fail_close(code=code):
                raise trial._coded(IniError('Windows executor cleanup timeout'), code)
            t.close = fail_close
            result = gate(json.dumps(p))
            with self.subTest(code=code):
                self.assertFalse(result['ok'])
                self.assertEqual(result['stage'], 'dispose')
                self.assertIn(expected, result['reason'])
                self.assertEqual(result['started_pid'], 43)
                self.assertEqual(result['process']['command'], trial.launch_command(p))

    def test_transport_close_marks_which_cleanup_failed(self):
        """実パイプでの検査は test_np21w_transport.py。ここは印だけを見る。"""
        import subprocess as sp

        class Channel(trial.PowerShellTransport):
            def __init__(self, wait):
                self.process = types.SimpleNamespace(
                    stdin=types.SimpleNamespace(close=lambda: None), wait=wait)
                self.reader = types.SimpleNamespace(join=lambda timeout: None,
                                                    is_alive=lambda: True)

        def timeout(timeout):
            raise sp.TimeoutExpired('powershell.exe', 3)
        with self.assertRaises(sp.TimeoutExpired) as caught:
            Channel(timeout).close()
        self.assertEqual(caught.exception.code, 'cleanup: executor exit timeout')
        with self.assertRaises(IniError) as caught:
            Channel(lambda timeout: 0).close()
        self.assertEqual(caught.exception.code, 'cleanup: inherited pipe still open')

    def test_cli_execute_llm_gate_and_no_model_mutation(self):
        args = ['--exe', EXE, '--baseline', BASE, '--cwd', CWD, '--pid', '42', '--created', CREATED,
                '--hdd', HDD, '--fdd-eject', '--execute', '--exclusive-operator']
        calls = []
        def factory(bound):
            calls.append(bound)
            return trial.WindowsExecutor(bound, Transport(bound))
        def malicious(bound, url, model):
            bound['pid'] = 777
            return json.dumps(bound)
        with redirect_stdout(io.StringIO()):
            self.assertEqual(trial.main(args, executor_factory=factory, llm=malicious), 2)
        self.assertEqual(calls, [])
        with redirect_stdout(io.StringIO()) as out:
            self.assertEqual(trial.main(args, executor_factory=factory,
                                       llm=lambda p, u, m: json.dumps(p)), 0)
        self.assertTrue(json.loads(out.getvalue())['ok'])
        self.assertEqual(len(calls), 1)

    def test_generated_ps_is_narrow_normal_close_and_explicit_launch(self):
        """**生成されるコード文字列の検査だけ**。CheckFile が Windows 上で実際に
        ディレクトリを拒否すること、`UseShellExecute = $true` が本当にハンドル
        継承を断つことの実証ではない (PowerShell は動かしていない)。"""
        ps = trial.PS_SERVER
        # 起動は ShellExecute 経由 = パイプを継承させない (実走 F3、PM 判断 ③)。
        # リダイレクトを一切しないことが前提なので、指定が現れないことも見る。
        self.assertIn('$si.UseShellExecute = $true', ps)
        self.assertNotIn('$si.UseShellExecute = $false', ps)
        self.assertNotIn('RedirectStandard', ps)
        self.assertIn('if ($p.HasExited) {', ps)
        self.assertNotIn('$p.WaitForExit(1000)', ps)
        # StartTime は ShellExecute 起動では読めないことがあるので保護する。
        self.assertIn('try { $startUtc = $p.StartTime.ToUniversalTime() } catch', ps)
        self.assertEqual(ps.count('{') - ps.count('}'), 0, 'unbalanced braces')
        for required in ['CloseMainWindow()', 'WaitForExit(10000)', 'AssertAbsent',
                         '$handle = $p.Handle', 'CreateNew', '$plan.trial', '$plan.cwd',
                         'AssertProcess', 'FileIdentity', 'CheckPath',
                         "$arguments = '\"/i' + $plan.trial + '\"'",
                         "if ($item.PSIsContainer) { throw 'directory where a disk image is required' }",
                         "CheckFile $plan.hdd_path",
                         "if ($plan.fdd_arg) {",
                         "CheckFile $plan.fdd_arg",
                         # created は 2 秒の許容、exe は大小文字を無視、
                         # command だけ -cne (自分が渡した文字列なので)。
                         "$rowUtc = [DateTime]::Parse($rows[0].created",
                         "TotalSeconds) -gt 2",
                         "$rows[0].exe -ine $plan.exe",
                         "$code = 'identity mismatch: ' + ($mismatch -join ',')",
                         "$startedPid = $p.Id",
                         "if ($code) { $failure.code = $code }",
                         "if ($startedPid) { $failure.pid = $startedPid }",
                         "$arguments = $arguments + ' \"' + $plan.fdd_arg + '\"'",
                         "$rows[0].command -cne ('\"' + $plan.exe + '\" ' + $arguments)"]:
            self.assertIn(required, ps)
        for forbidden in ['.Kill(', 'Stop-Process', 'File]::Replace', 'Copy-Item', 'Invoke-Expression']:
            self.assertNotIn(forbidden, ps)

WINDOWS_PARSER = '--windows-parser' in sys.argv
if WINDOWS_PARSER:
    sys.argv.remove('--windows-parser')

@unittest.skipUnless(WINDOWS_PARSER, 'opt-in Windows parser only; no lifecycle execution')
class PowerShellParser(unittest.TestCase):
    def test_generated_script_parser_only(self):
        import base64
        import subprocess
        import tempfile
        # Actual generated source is stdin DATA. ParseInput returns AST only;
        # no ScriptBlock.Create/Invoke, dot-source, or lifecycle execution.
        parser = r"""
$source = [Console]::In.ReadToEnd()
$tokens = $null; $errors = $null
$null = [Management.Automation.Language.Parser]::ParseInput($source, [ref]$tokens, [ref]$errors)
[Console]::WriteLine(('syntax errors: ' + $errors.Count))
if ($errors.Count) { exit 1 }
"""
        encoded = base64.b64encode(parser.encode('utf-16le')).decode('ascii')
        with tempfile.TemporaryDirectory(prefix='np21w-trial-parser-') as tmp:
            source = Path(tmp) / 'source.txt'
            source.write_bytes(trial.PS_SERVER.encode('utf-8'))
            with source.open('rb') as data:
                result = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-NonInteractive',
                                         '-EncodedCommand', encoded], stdin=data,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr.decode('utf-8', errors='replace'))
        self.assertIn(b'syntax errors: 0', result.stdout)

if __name__ == '__main__':
    unittest.main()
