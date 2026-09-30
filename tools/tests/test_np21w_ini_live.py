"""Synthetic host tests by default; opt-in PowerShell uses temporary files only.

  python3 -B tools/tests/test_np21w_ini_live.py            # cases
  python3 -B tools/tests/test_np21w_ini_live.py --mutate   # plus mutants (make check-np21w-ini-live-host)
"""
import copy
import importlib.util
import io
import sys
import tempfile
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import np21w_ini_live as live

TARGET = {'exe': r'C:\NP21\np21x64w.exe', 'ini': r'C:\NP21\chosen.ini'}
ROW = {'pid': 42, 'exe': TARGET['exe'],
       'command': '"' + TARGET['exe'] + '" "' + TARGET['ini'] + '"',
       'created': '20260908010000'}
RAW = (b'; opaque \xff\r\n[NekoProject21]\r\nUSEGD5430=false\r\nGD5430TYPE=91\n'
       b'USEPEGCP=false\nExMemory=16\nprivate=SECRET')
NEW = RAW.replace(b'USEGD5430=false', b'USEGD5430=true')
PEGC_ON = RAW.replace(b'USEPEGCP=false', b'USEPEGCP=true')
RAM_8MB = RAW.replace(b'ExMemory=16', b'ExMemory=7')
# tools/np21w_ctl.py start: Start-Process -ArgumentList @('"/i<ini>"'[, '"<fd>"']);
# measured 2026-09-30 as '"<exe>" "/i<ini>"' (plus ShellExecute's trailing blank).
FD = r'C:\NP21\os32_boot.d88'
CTL_COMMAND = '"' + TARGET['exe'] + '" "/i' + TARGET['ini'] + '" '
CTL_FD_COMMAND = '"' + TARGET['exe'] + '" "/i' + TARGET['ini'] + '" "' + FD + '" '
# FileIdentity.Read: Volume:IndexHigh:IndexLow:Creation:Write:SizeHigh:SizeLow.
# ReplaceFile keeps the replacement (candidate) file's volume/index.
CANDIDATE_KEY = '1:0:2'
REPLACED_SIG = CANDIDATE_KEY + ':3:4:0:5'


def launch_text(launch):
    # Independent restatement of the expected restart arguments (not live.launch_arguments).
    head = {'switch': '"/i', 'positional': '"'}[launch['form']]
    tail = ' "' + launch['fd'] + '"' if launch['fd'] is not None else ''
    return head + TARGET['ini'] + '"' + tail


class Fake:
    def __init__(self):
        self.rows = [copy.deepcopy(ROW)]
        self.snapshot = {'data': RAW, 'signature': 'initial'}
        self.calls = []
        self.fail = None
        self.hook = lambda op: None
        self.saved = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.calls.append('close')

    def call(self, op, **args):
        self.calls.append(op)
        self.hook(op)
        if op == self.fail:
            raise live.IniError('injected failure')
        if op == 'query':
            return copy.deepcopy(self.rows)
        if op == 'snapshot':
            return copy.deepcopy(self.snapshot)
        if op == 'stop':
            self.rows = []
        if op == 'backup':
            self.saved = copy.deepcopy(args['snapshot'])
            return 'a' * 32
        if op == 'replace':
            if self.snapshot != args['expected']:
                raise live.IniError('changed')
            self.snapshot = {'data': args['data'], 'signature': REPLACED_SIG}
            # The fixed PS 'replace' writes receipt.json itself, right after the
            # full readback, with 'applied' = that readback (2026-10-01), and
            # returns the candidate file's volume/index next to the readback.
            if 'record' in args:
                self.receipt_id = args.get('receipt')
                self.record = dict(copy.deepcopy(args['record']), applied=copy.deepcopy(self.snapshot))
            return {'applied': copy.deepcopy(self.snapshot),
                    'candidate': getattr(self, 'candidate_key', CANDIDATE_KEY)}
        if op == 'load':
            return copy.deepcopy(self.record)
        if op == 'start':
            # What .NET Process.Start shows as CommandLine: "<FileName>" <Arguments>.
            self.launch = copy.deepcopy(args.get('launch'))
            self.rows = [dict(ROW, pid=43, created='new',
                              command='"' + TARGET['exe'] + '" ' + launch_text(args['launch']))]
            return 43
        return True


class Identity(unittest.TestCase):
    def test_exact_identity(self):
        self.assertEqual(live.identify([ROW], TARGET, 42), ROW)

    def test_absence_is_not_query_failure(self):
        self.assertEqual(live.identify([], TARGET), None)
        for rows in (None, {}, False, [dict(ROW, exe=None)], [dict(ROW, created=None)]):
            with self.subTest(rows=rows), self.assertRaises(live.IniError):
                live.identify(rows, TARGET)

    def test_reject_ambiguous_commandlines(self):
        for command in (TARGET['exe'], '"' + TARGET['exe'] + '" relative.ini',
                        ROW['command'] + ' /f', ROW['command'] + ' other.ini',
                        '"' + TARGET['exe'] + '" -i "' + TARGET['ini'] + '"'):
            with self.subTest(command=command), self.assertRaises(live.IniError):
                live.identify([dict(ROW, command=command)], TARGET)

    def test_accepts_windows_trailing_whitespace(self):
        """Real CreateProcess command lines carry a trailing space after the
        last quoted token (measured 2026-09-09 on np21x64w.exe). Np2Arg::Parse
        tokenizes, so trailing blanks are insignificant. Extra tokens are not."""
        for suffix in (' ', '  ', '\t', ' \t '):
            with self.subTest(suffix=repr(suffix)):
                self.assertEqual(
                    live.identify([dict(ROW, command=ROW['command'] + suffix)], TARGET, 42),
                    dict(ROW, command=ROW['command'] + suffix))

    def test_executor_preserves_specific_diagnostics(self):
        """IniError subclasses ValueError; call() must not swallow its own
        precise message behind the generic transport diagnostic."""
        class Bad:
            def exchange(self, request):
                return {'ok': True, 'value': [dict(ROW, command='unsupported')]}
        ex = live.WindowsExecutor.__new__(live.WindowsExecutor)
        ex.transport = Bad()
        ex.target = TARGET
        with self.assertRaises(live.IniError) as caught:
            ex.call('query')
        self.assertNotIn('invalid Windows executor response', str(caught.exception))
        self.assertIn('identity', str(caught.exception))

    def test_wrong_pid_exe_config_or_multiple(self):
        for rows in ([dict(ROW, pid=99)], [dict(ROW, exe=r'D:\np21x64w.exe')],
                     [dict(ROW, command=ROW['command'].replace('chosen', 'other'))], [ROW, ROW]):
            with self.subTest(rows=rows), self.assertRaises(live.IniError):
                live.identify(rows, TARGET, 42)


class Workflow(unittest.TestCase):
    def setUp(self):
        self.f = Fake()
        self.service = live.Live(self.f, TARGET)

    def apply(self):
        return self.service.run('cirrus-on', live_apply=True, exclusive=True)

    def test_dry_run_exact_diff_without_mutations(self):
        result = self.service.run('cirrus-on')
        self.assertEqual(result.get('diff'), ['USEGD5430: false -> true'])
        self.assertEqual(self.f.calls, ['query', 'snapshot', 'close'])
        self.assertEqual(self.f.snapshot['data'], RAW)

    def test_bounded_operation_and_operator_gate(self):
        for kwargs in ({'operation': 'shell'}, {'operation': '../config'},
                       {'operation': 'cirrus-on', 'live_apply': True}):
            with self.subTest(kwargs=kwargs), self.assertRaises(live.IniError):
                self.service.run(**kwargs)
        self.assertEqual(self.f.calls, [])

    def test_exclusive_lock_before_any_contact(self):
        self.f.fail = 'lock'
        with self.assertRaises(live.IniError):
            self.apply()
        self.assertEqual(self.f.calls, ['lock', 'close'])

    def test_apply_order_bytes_backup_receipt_restart(self):
        result = self.apply()
        self.assertEqual(self.f.snapshot['data'], NEW)
        self.assertEqual(self.f.saved['data'], RAW)
        self.assertEqual(result.get('pid'), 43)
        calls = self.f.calls
        for first, second in (('lock', 'query'), ('stop', 'backup'), ('backup', 'replace'),
                              ('replace', 'start')):
            self.assertLess(calls.index(first), calls.index(second))
        # The receipt is written inside 'replace' (no separate executor step).
        self.assertNotIn('receipt', calls)
        self.assertEqual(self.f.receipt_id, result.get('receipt'))
        self.assertEqual(self.f.record['applied'], self.f.snapshot)
        self.assertIn('query', calls[calls.index('stop')+1:calls.index('backup')])
        self.assertEqual(calls[-3:], ['query', 'query', 'close'])

    def test_queries_fail_closed_at_every_phase(self):
        clean = Fake()
        live.Live(clean, TARGET).run('cirrus-on', live_apply=True, exclusive=True)
        self.assertGreaterEqual(clean.calls.count('query'), 7)
        for index in range(1, clean.calls.count('query') + 1):
            f = Fake()
            count = [0]
            def hook(op):
                if op == 'query':
                    count[0] += 1
                    if count[0] == index:
                        raise live.IniError('query unavailable')
            f.hook = hook
            with self.subTest(index=index), self.assertRaises(live.IniError):
                live.Live(f, TARGET).run('cirrus-on', live_apply=True, exclusive=True)

    def test_stop_requires_exit_not_acknowledgement(self):
        def hook(op):
            if op == 'query' and 'stop' in self.f.calls:
                self.f.rows = [ROW]
        self.f.hook = hook
        with self.assertRaises(live.IniError):
            self.apply()
        self.assertNotIn('backup', self.f.calls)

    def test_pid_reuse_before_stop(self):
        def hook(op):
            if op == 'query' and self.f.calls.count('query') == 2:
                self.f.rows = [dict(ROW, created='reused')]
        self.f.hook = hook
        with self.assertRaises(live.IniError):
            self.apply()
        self.assertNotIn('stop', self.f.calls)

    def test_stop_save_or_external_change_aborts(self):
        def hook(op):
            if op == 'snapshot' and 'stop' in self.f.calls:
                self.f.snapshot['signature'] = 'intervening'
        self.f.hook = hook
        with self.assertRaises(live.IniError):
            self.apply()
        self.assertNotIn('replace', self.f.calls)

    def test_backup_replace_readback_failures_never_restart(self):
        for failure in ('backup', 'replace'):
            self.setUp()
            self.f.fail = failure
            with self.subTest(failure=failure), self.assertRaises(live.IniError):
                self.apply()
            self.assertNotIn('start', self.f.calls)
        self.setUp()
        def hook(op):
            if op == 'snapshot' and 'replace' in self.f.calls:
                self.f.snapshot['data'] = b'corrupt'
        self.f.hook = hook
        with self.assertRaises(live.IniError):
            self.apply()
        self.assertNotIn('start', self.f.calls)

    def test_restart_requires_same_identity_and_observed_pid(self):
        def hook(op):
            if op == 'query' and 'start' in self.f.calls:
                self.f.rows = [dict(ROW, pid=77)]
        self.f.hook = hook
        with self.assertRaises(live.IniError):
            self.apply()
        self.assertEqual(self.f.snapshot['data'], NEW)

    def test_restore_dry_run_and_apply(self):
        receipt = self.apply().get('receipt')
        self.f.calls.clear()
        result = self.service.run('restore', receipt=receipt)
        self.assertEqual(result.get('diff'), ['USEGD5430: true -> false'])
        self.assertNotIn('stop', self.f.calls)
        self.service.run('restore', receipt=receipt, live_apply=True, exclusive=True)
        self.assertEqual(self.f.snapshot['data'], RAW)

    def test_restore_rejects_intervening_identity_or_bytes(self):
        for field, value in (('signature', 'new-identity'), ('data', RAW)):
            self.setUp()
            receipt = self.apply().get('receipt')
            self.f.snapshot[field] = value
            self.f.calls.clear()
            with self.subTest(field=field), self.assertRaises(live.IniError):
                self.service.run('restore', receipt=receipt, live_apply=True, exclusive=True)
            self.assertNotIn('stop', self.f.calls)

    def test_receipt_tampering_is_rejected(self):
        receipt = self.apply().get('receipt')
        self.assertIsNotNone(receipt)
        for field, value in (('target', {}), ('original', {'data': b'bad', 'signature': 'x'}),
                             ('operation', 'shell')):
            record = copy.deepcopy(self.f.record)
            self.f.record[field] = value
            with self.subTest(field=field), self.assertRaises(live.IniError):
                self.service.run('restore', receipt=receipt)
            self.f.record = record



class Transport:
    def __init__(self, response):
        self.response, self.requests, self.closed = response, [], False

    def exchange(self, request):
        self.requests.append(request)
        return self.response

    def close(self):
        self.closed = True


class WindowsContract(unittest.TestCase):
    def test_successful_empty_query_and_transport_cleanup(self):
        t = Transport({'ok': True, 'value': []})
        with live.WindowsExecutor(TARGET, t) as ex:
            self.assertEqual(ex.call('query'), [])
        self.assertTrue(t.closed)
        self.assertEqual(t.requests[0]['target'], TARGET)

    def test_query_error_malformed_output_fail_closed(self):
        for response in ({'ok': False}, {}, {'ok': True, 'value': None},
                         {'ok': True, 'value': {}}, {'ok': 1, 'value': []}):
            with self.subTest(response=response), self.assertRaises(live.IniError):
                with live.WindowsExecutor(TARGET, Transport(response)) as ex:
                    ex.call('query')

    def test_byte_transport_and_no_arbitrary_executor_operation(self):
        import base64
        t = Transport({'ok': True, 'value': {'data': base64.b64encode(RAW).decode(), 'signature': 'id'}})
        with live.WindowsExecutor(TARGET, t) as ex:
            self.assertEqual(ex.call('snapshot'), {'data': RAW, 'signature': 'id'})
            t.response = {'ok': True, 'value': {'applied': {'data': base64.b64encode(NEW).decode(),
                                                            'signature': REPLACED_SIG},
                                                'candidate': CANDIDATE_KEY}}
            ex.call('replace', expected={'data': RAW, 'signature': 'id'}, data=NEW)
            self.assertEqual(t.requests[-1]['args']['data'], base64.b64encode(NEW).decode())
            with self.assertRaises(live.IniError):
                ex.call('shell', command='anything')

    def test_powershell_query_stop_lock_contract(self):
        for fragment in ('Get-CimInstance -ClassName Win32_Process', '-ErrorAction Stop',
                         'CreationDate', 'ExecutablePath', 'CommandLine',
                         'WaitForExit(10000)', '.Kill()', '.Handle',
                         'System.Threading.Mutex', 'WaitOne(0)', 'ReleaseMutex()',
                         'ok=$false', 'value=@(Query)', 'finally'):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, live.PS_SERVER)

    def test_powershell_atomic_backup_identity_and_start_contract(self):
        for fragment in ('GetFileInformationByHandle', 'NumberOfLinks', 'ReparsePoint',
                         'CreateNew', 'Flush($true)', '[IO.File]::Replace(',
                         'AssertAbsent', 'AssertSnapshot', 'Readback',
                         'UseShellExecute=$false', '.Arguments=', '.FileName=',
                         '[Diagnostics.Process]::Start(', 'original.bin', 'receipt.json'):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, live.PS_SERVER)

    def test_cli_default_dry_run_and_explicit_apply(self):
        import contextlib
        import io
        calls = []
        def factory(target):
            calls.append(target)
            return Fake()
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(live.main(['cirrus-on', '--exe', TARGET['exe'], '--ini', TARGET['ini']], factory), 0)
        self.assertEqual(calls, [TARGET])
        self.assertIn('USEGD5430: false -> true', out.getvalue())
        self.assertNotIn('SECRET', out.getvalue())
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(live.main(['cirrus-on', '--exe', TARGET['exe'], '--ini', TARGET['ini'],
                                       '--live-apply', '--exclusive-operator'], factory), 0)
        self.assertIn('receipt:', out.getvalue())



class RecoveryAndBoundary(unittest.TestCase):
    def test_restore_when_start_failed_and_query_proves_absence(self):
        f = Fake()
        f.fail = 'start'
        service = live.Live(f, TARGET)
        with self.assertRaises(live.IniError) as error:
            service.run('cirrus-on', live_apply=True, exclusive=True)
        self.assertIn('a' * 32, str(error.exception))
        f.fail = None
        try:
            result = service.run('restore', receipt='a' * 32, live_apply=True, exclusive=True)
        except live.IniError as exc:
            self.fail('valid receipt plus successful absence query must allow recovery: ' + str(exc))
        self.assertEqual(f.snapshot['data'], RAW)
        self.assertEqual(result['pid'], 43)

    def test_model_binding_rejects_paths_shell_and_forged_authorization(self):
        self.assertTrue(callable(getattr(live, 'bind_model_operation', None)))
        f = Fake()
        operation = live.bind_model_operation(f, TARGET)
        self.assertEqual(operation({'operation': 'cirrus-on'})['diff'], ['USEGD5430: false -> true'])
        for request in ({'operation': 'shell'}, {'operation': 'cirrus-on', 'ini': 'evil'},
                        {'operation': 'cirrus-on', 'live_apply': True},
                        {'operation': 'cirrus-on', 'stopped': True}):
            with self.subTest(request=request), self.assertRaises(live.IniError):
                operation(request)
        self.assertNotIn('stop', f.calls)
        apply = live.bind_model_operation(f, TARGET, authorized_apply=True, exclusive=True,
                                          approved_request={'operation': 'cirrus-on'})
        self.assertEqual(apply({'operation': 'cirrus-on'})['pid'], 43)

    def test_mutating_model_binding_is_single_use(self):
        f = Fake()
        operation = live.bind_model_operation(f, TARGET, authorized_apply=True, exclusive=True,
                                              approved_request={'operation': 'cirrus-on'})
        operation({'operation': 'cirrus-on'})
        with self.assertRaises(live.IniError):
            operation({'operation': 'cirrus-off'})

    def test_bound_apply_requires_exact_operator_approved_request(self):
        f = Fake()
        operation = live.bind_model_operation(f, TARGET, authorized_apply=True, exclusive=True)
        with self.assertRaises(live.IniError):
            operation({'operation': 'cirrus-on'})
        self.assertEqual(f.calls, [])

    def test_intervening_start_during_backup_or_readback_aborts(self):
        for phase in ('backup', 'replace'):
            f = Fake()
            def hook(op):
                if op == 'query' and phase in f.calls:
                    f.rows = [ROW]
            f.hook = hook
            with self.subTest(phase=phase), self.assertRaises(live.IniError):
                live.Live(f, TARGET).run('cirrus-on', live_apply=True, exclusive=True)
            self.assertNotIn('start', f.calls)

    def test_reserved_device_paths_are_not_operator_targets(self):
        for ini in (r'C:\NP21\CON.ini', r'C:\NP21\nul\x.ini', r'C:\NP21\COM1.ini'):
            with self.subTest(ini=ini), self.assertRaises(live.IniError):
                live.Live(Fake(), dict(TARGET, ini=ini))

    def test_windows_native_identity_layout_and_private_new_files(self):
        self.assertIn('LayoutKind.Sequential, Pack=4', live.PS_SERVER)
        self.assertIn('FileSystemRights]::Write', live.PS_SERVER)
        self.assertIn('FileSecurity', live.PS_SERVER)


class ChannelFailure(unittest.TestCase):
    def test_nonzero_powershell_exit_overrides_success_frame(self):
        import queue
        from unittest.mock import Mock
        channel = object.__new__(live.PowerShellTransport)
        channel.process = Mock()
        channel.process.poll.return_value = 3
        channel.queue = queue.Queue()
        channel.queue.put(b'{"ok":true,"value":[]}\n')
        with self.assertRaises(live.IniError):
            channel.exchange({'op': 'query'})


class ReviewBlockers(unittest.TestCase):
    def test_replace_uses_true_null_string(self):
        import re
        call = re.search(r'\[IO.File\]::Replace\([^\n]+', live.PS_SERVER).group()
        self.assertEqual(call, '[IO.File]::Replace($temp, $target.ini, '
                         '[System.Management.Automation.Language.NullString]::Value)')

    def test_large_receipt_rejected_before_stop_or_write(self):
        for size in (2097199, live.LIMIT):
            f = Fake()
            f.snapshot['data'] = RAW + b'x' * (size - len(RAW))
            with self.subTest(size=size):
                with self.assertRaisesRegex(live.IniError, 'receipt'):
                    live.Live(f, TARGET).run('cirrus-on', live_apply=True, exclusive=True)
                self.assertEqual(f.calls, ['lock', 'query', 'snapshot', 'close'])
                self.assertIsNone(f.saved)
                self.assertEqual(len(f.snapshot['data']), size)

    def test_derived_paths_rejected_before_executor_contact(self):
        # Existing input limit accepts 227 chars; receipt adds 57 -> 284.
        for name in ('x' * 216, '\U0001f600' * 100):
            target = dict(TARGET, ini='C:\\NP21\\' + name + '.ini')
            f = Fake()
            f.rows = [dict(ROW, command='\"' + target['exe'] + '\" \"' + target['ini'] + '\"')]
            with self.subTest(name_length=len(name)):
                with self.assertRaises(live.IniError):
                    live.Live(f, target).run('cirrus-on', live_apply=True, exclusive=True)
                self.assertEqual(f.calls, [])



class ReceiptAndPathBoundaries(unittest.TestCase):
    class GrowingIdentityFake(Fake):
        def call(self, op, **args):
            value = super().call(op, **args)
            if op == 'replace':
                # Longest FileIdentity signature, same candidate volume/index.
                self.snapshot['signature'] = CANDIDATE_KEY + ':' + ':'.join(
                    '9' * n for n in (62, 62, 62, 61))
                assert len(self.snapshot['signature']) == live.SIGNATURE_LIMIT
                # PS 'replace' records its own readback in receipt.json.
                if 'record' in args:
                    self.record['applied'] = copy.deepcopy(self.snapshot)
                return {'applied': copy.deepcopy(self.snapshot), 'candidate': CANDIDATE_KEY}
            if op == 'start':
                self.rows = [dict(ROW, pid=2147483647,
                                  created='2026-09-09T01:00:00.0000000Z')]
                return self.rows[0]['pid']
            return value

    def test_review_counterexample_rejected_before_first_stop(self):
        f = self.GrowingIdentityFake()
        f.snapshot = {'data': RAW + b'x' * (1571320 - len(RAW)),
                      'signature': '1' * 53}
        before = copy.deepcopy(f.snapshot)
        with self.assertRaisesRegex(live.IniError, 'receipt'):
            live.Live(f, TARGET).run('cirrus-on', live_apply=True, exclusive=True)
        self.assertEqual(f.calls, ['lock', 'query', 'snapshot', 'close'])
        self.assertEqual(f.snapshot, before)
        self.assertIsNone(f.saved)

    def test_receipt_last_accepted_and_first_rejected_byte_sizes(self):
        import json
        def fixture(size):
            return RAW + b'x' * (size - len(RAW))
        def bound(size):
            raw = fixture(size)
            candidate, diff = live.transform(raw, live.OPERATIONS['cirrus-on'])
            return live.receipt_size_bound({
                'target': TARGET, 'operation': 'cirrus-on',
                'original': {'data': raw, 'signature': 'initial'},
                'applied': {'data': candidate, 'signature': 'pending'},
                'diff': diff, 'process': ROW})
        lo, hi = len(RAW), live.LIMIT
        while lo + 1 < hi:
            mid = (lo + hi) // 2
            if bound(mid) <= live.LIMIT:
                lo = mid
            else:
                hi = mid
        self.assertEqual(hi, lo + 1)
        self.assertLessEqual(bound(lo), live.LIMIT)
        self.assertGreater(bound(hi), live.LIMIT)
        for size in (lo, hi):
            f = self.GrowingIdentityFake()
            f.snapshot['data'] = fixture(size)
            service = live.Live(f, TARGET)
            if size == hi:
                with self.assertRaisesRegex(live.IniError, 'receipt'):
                    service.run('cirrus-on', live_apply=True, exclusive=True)
                self.assertEqual(f.calls, ['lock', 'query', 'snapshot', 'close'])
                self.assertIsNone(f.saved)
                self.assertEqual(f.snapshot['data'], fixture(size))
            else:
                receipt = service.run('cirrus-on', live_apply=True, exclusive=True)['receipt']
                # Synthetic serialized persistence, not Windows API validation.
                saved = json.dumps(live.wire(f.record)).encode('utf-8')
                self.assertLessEqual(len(saved), live.LIMIT)
                f.record = live.wire(json.loads(saved), decode=True)
                self.assertEqual(f.record['original']['data'], fixture(size))
                self.assertGreater(len(f.snapshot['signature']), len('initial'))
                f.calls.clear()
                restored = service.run('restore', receipt=receipt,
                                       live_apply=True, exclusive=True)
                self.assertTrue(restored['applied'])
                self.assertEqual(restored['diff'], ['USEGD5430: true -> false'])
                self.assertEqual(f.snapshot['data'], fixture(size))
                self.assertEqual(f.calls.count('stop'), 1)
                self.assertEqual(f.calls.count('replace'), 1)
                self.assertEqual(f.calls.count('start'), 1)
                restored_json = json.dumps(live.wire(f.record)).encode('utf-8')
                self.assertLessEqual(len(restored_json), live.LIMIT)
                self.assertEqual(live.wire(json.loads(restored_json), decode=True), f.record)

    def test_pegc_operations_are_available_and_independent(self):
        """USEPEGCP gates np2cfg.usepegcplane -> pegc.enable (np21w-src
        win9x/ini.cpp:687, io/pegc.c:375). Cirrus fields must not move."""
        self.assertIn('pegc-on', live.OPERATIONS)
        self.assertIn('pegc-off', live.OPERATIONS)
        on, diff = live.transform(RAW, live.OPERATIONS['pegc-on'])
        self.assertEqual(diff, ['USEPEGCP: false -> true'])
        self.assertEqual(on, PEGC_ON)
        self.assertIn(b'USEGD5430=false', on)
        off, diff = live.transform(on, live.OPERATIONS['pegc-off'])
        self.assertEqual((off, diff), (RAW, ['USEPEGCP: true -> false']))

    def test_pegc_and_cirrus_do_not_disturb_each_other(self):
        cirrus, _ = live.transform(RAW, live.OPERATIONS['cirrus-on'])
        both, diff = live.transform(cirrus, live.OPERATIONS['pegc-on'])
        self.assertEqual(diff, ['USEPEGCP: false -> true'])
        self.assertIn(b'USEGD5430=true', both)
        back, diff = live.transform(both, live.OPERATIONS['cirrus-off'])
        self.assertEqual(diff, ['USEGD5430: true -> false'])
        self.assertIn(b'USEPEGCP=true', back)

    def test_ram_operations_switch_only_extended_memory(self):
        """ExMemory は MB 単位の拡張メモリ。8MB は CUI の最低動作環境で、
        memory_boot の legacy フォールバック経路を通すための構成。
        GUI の最低要件ではない (INSTALL.md / docs/02_memory.md)。"""
        self.assertIn('ram-8mb', live.OPERATIONS)
        self.assertIn('ram-15mb', live.OPERATIONS)
        small, diff = live.transform(RAW, live.OPERATIONS['ram-8mb'])
        self.assertEqual(diff, ['EXMEMORY: 16 -> 7'])
        self.assertEqual(small, RAM_8MB)
        self.assertIn(b'USEGD5430=false', small)
        self.assertIn(b'USEPEGCP=false', small)
        back, diff = live.transform(small, live.OPERATIONS['ram-15mb'])
        self.assertEqual((back, diff), (RAW, ['EXMEMORY: 7 -> 16']))
        # ExMemory >= 16 では 16MB システム空間の 1MB が抜けるので、ゲストの
        # 報告量 + 1 を書く (32MB -> 33、128MB -> 129)。
        big, diff = live.transform(RAW, live.OPERATIONS['ram-32mb'])
        self.assertEqual(diff, ['EXMEMORY: 16 -> 33'])
        huge, diff = live.transform(big, live.OPERATIONS['ram-128mb'])
        self.assertEqual(diff, ['EXMEMORY: 33 -> 129'])
        back, diff = live.transform(huge, live.OPERATIONS['ram-15mb'])
        self.assertEqual((back, diff), (RAW, ['EXMEMORY: 129 -> 16']))

    def test_receipt_bound_covers_signature_escaping_and_restart_metadata(self):
        import json
        starts = {'cirrus-on': RAW, 'cirrus-off': NEW,
                  'pegc-on': RAW, 'pegc-off': PEGC_ON,
                  'ram-8mb': RAW, 'ram-9mb': RAW, 'ram-15mb': RAM_8MB,
                  'ram-32mb': RAW, 'ram-128mb': RAW}
        for operation in live.OPERATIONS:
            raw = starts[operation]
            candidate, diff = live.transform(raw, live.OPERATIONS[operation])
            planned = {'target': TARGET, 'operation': operation,
                       'original': {'data': raw, 'signature': 'initial'},
                       'applied': {'data': candidate, 'signature': 'pending'},
                       'diff': diff, 'process': ROW}
            reserved = live.receipt_size_bound(planned)
            for signature in ('9', '"', '\\', '\n', '\uffff', '\U0010ffff'):
                with self.subTest(operation=operation, signature=repr(signature)):
                    actual = copy.deepcopy(planned)
                    for key in ('original', 'applied'):
                        actual[key]['signature'] = signature * live.SIGNATURE_LIMIT
                        live.checked_snapshot(actual[key])
                    actual['process'] = dict(ROW, pid=2147483647,
                                             created='2026-09-09T01:00:00.0000000Z')
                    self.assertLessEqual(len(json.dumps(live.wire(actual)).encode('ascii')),
                                         reserved)
                    actual['original'], actual['applied'] = actual['applied'], actual['original']
                    actual['operation'] = 'restore'
                    actual['diff'] = [line.split(': ')[0] + ': ' + ' -> '.join(
                        reversed(line.split(': ')[1].split(' -> '))) for line in diff]
                    self.assertLessEqual(live.receipt_size_bound(actual), reserved)
                    self.assertLessEqual(len(json.dumps(live.wire(actual)).encode('ascii')),
                                         reserved)

    def test_receipt_metadata_also_rejected_before_stop(self):
        f = Fake()
        f.rows[0]['created'] = "<>&'\u65e5" * live.LIMIT
        with self.assertRaisesRegex(live.IniError, 'receipt'):
            live.Live(f, TARGET).run('cirrus-on', live_apply=True, exclusive=True)
        self.assertEqual(f.calls, ['lock', 'query', 'snapshot', 'close'])

    def test_transformed_growth_rejected_before_stop(self):
        f = Fake()
        f.snapshot['data'] = NEW + b'x' * (live.LIMIT - len(NEW))
        with self.assertRaises(live.IniError):
            live.Live(f, TARGET).run('cirrus-off', live_apply=True, exclusive=True)
        self.assertEqual(f.calls, ['lock', 'query', 'snapshot', 'close'])

    def test_all_derived_paths_at_utf16_policy_boundary(self):
        for unicode_name in (False, True):
            for units in (182, 183):
                room = units - len('C:\\NP21\\.ini')
                name = ('\U0001f600' * (room // 2) + 'x' * (room % 2)
                        if unicode_name else 'x' * room)
                ini = 'C:\\NP21\\' + name + '.ini'
                bundle = ini + '.np21w-live-' + 'a' * 32
                paths = (bundle, bundle + '\\original.bin', bundle + '\\receipt.json',
                         ini + '.pending-' + 'b' * 32)
                with self.subTest(unicode_name=unicode_name, units=units):
                    if units == 183:
                        with self.assertRaises(live.IniError):
                            live.Live(Fake(), dict(TARGET, ini=ini))
                    else:
                        live.Live(Fake(), dict(TARGET, ini=ini))
                        for path in paths:
                            self.assertLess(len(path.encode('utf-16le')) // 2, 240)
                            for component in path[3:].split('\\'):
                                self.assertLessEqual(len(component.encode('utf-16le')) // 2, 255)

    def test_executor_integration_preflight_failure_order(self):
        # Real Python adapter, synthetic transport. No Windows process/files.
        class ScriptedTransport(Transport):
            def exchange(self, request):
                self.requests.append(request)
                values = {'lock': True, 'query': [ROW], 'snapshot': {
                    'data': RAW + b'x' * (2097199 - len(RAW)), 'signature': 'initial'}}
                if request['op'] not in values:
                    raise AssertionError('mutation attempted after failed preflight')
                return {'ok': True, 'value': live.wire(values[request['op']])}
        t = ScriptedTransport(None)
        with self.assertRaisesRegex(live.IniError, 'receipt'):
            live.Live(live.WindowsExecutor(TARGET, t), TARGET).run(
                'cirrus-on', live_apply=True, exclusive=True)
        self.assertEqual([r['op'] for r in t.requests], ['lock', 'query', 'snapshot'])
        self.assertTrue(t.closed)
        t = ScriptedTransport(None)
        target = dict(TARGET, ini='C:\\NP21\\' + 'x' * 216 + '.ini')
        with self.assertRaises(live.IniError):
            live.Live(live.WindowsExecutor(target, t), target).run(
                'cirrus-on', live_apply=True, exclusive=True)
        self.assertEqual(t.requests, [])


class CtlLaunchShapes(unittest.TestCase):
    """np21w_ctl.py start launches '"<exe>" "/i<ini>" ["<fd>"]' (2026-09-30).

    Np2Arg::Parse (np21w-src win9x/np2arg.cpp) reads '/i' + rest as the ini;
    milstr_getarg drops the quotes. The restart must reuse the same shape."""

    def row(self, command):
        return dict(ROW, command=command)

    def test_accepts_ctl_switch_shapes(self):
        for command, launch in (
                (CTL_COMMAND, {'form': 'switch', 'fd': None}),
                (CTL_COMMAND.rstrip(), {'form': 'switch', 'fd': None}),
                (CTL_FD_COMMAND, {'form': 'switch', 'fd': FD}),
                (ROW['command'], {'form': 'positional', 'fd': None})):
            with self.subTest(command=command):
                self.assertEqual(live.identify([self.row(command)], TARGET, 42), self.row(command))
                self.assertEqual(live.launch_of(command, TARGET), launch)

    def test_preview_of_ctl_launched_process(self):
        for command in (CTL_COMMAND, CTL_FD_COMMAND):
            f = Fake()
            f.rows = [self.row(command)]
            with self.subTest(command=command):
                result = live.Live(f, TARGET).run('ram-8mb')
                self.assertEqual(result['diff'], ['EXMEMORY: 16 -> 7'])
                self.assertEqual(f.calls, ['query', 'snapshot', 'close'])

    def test_restart_keeps_launch_shape(self):
        for command, launch in ((CTL_COMMAND, {'form': 'switch', 'fd': None}),
                                (CTL_FD_COMMAND, {'form': 'switch', 'fd': FD}),
                                (ROW['command'], {'form': 'positional', 'fd': None})):
            f = Fake()
            f.rows = [self.row(command)]
            with self.subTest(command=command):
                result = live.Live(f, TARGET).run('ram-8mb', live_apply=True, exclusive=True)
                self.assertEqual(result['pid'], 43)
                self.assertEqual(f.launch, launch)
                self.assertEqual(f.rows[0]['command'].rstrip(), command.rstrip())
                self.assertEqual(f.snapshot['data'], RAM_8MB)

    def test_restore_after_failed_start_uses_recorded_shape(self):
        f = Fake()
        f.rows = [self.row(CTL_FD_COMMAND)]
        f.fail = 'start'
        service = live.Live(f, TARGET)
        with self.assertRaises(live.IniError):
            service.run('ram-8mb', live_apply=True, exclusive=True)
        self.assertEqual(f.rows, [])
        f.fail = None
        result = service.run('restore', receipt='a' * 32, live_apply=True, exclusive=True)
        self.assertEqual(result['pid'], 43)
        self.assertEqual(f.launch, {'form': 'switch', 'fd': FD})
        self.assertEqual(f.snapshot['data'], RAW)

    def test_restart_in_another_shape_is_rejected(self):
        f = Fake()
        f.rows = [self.row(CTL_COMMAND)]
        def hook(op):
            if op == 'query' and 'start' in f.calls:
                f.rows = [dict(ROW, pid=43, created='new')]  # positional, not /i
        f.hook = hook
        with self.assertRaisesRegex(live.IniError, 'launch shape'):
            live.Live(f, TARGET).run('ram-8mb', live_apply=True, exclusive=True)

    def test_rejects_unsupported_variants(self):
        exe, ini = '"' + TARGET['exe'] + '" ', TARGET['ini']
        for command in (
                exe + '"-i' + ini + '"',                      # '-i' (NP21/W accepts; ctl never emits)
                exe + '"/I' + ini + '"',                      # upper-case switch
                exe + '/i' + ini,                             # unquoted token
                exe + '"/i' + ini + '" "/i' + ini + '"',      # /i twice
                exe + '"/ichosen.ini"',                       # relative ini
                exe + '"/i ' + ini + '"',                     # blank after /i
                exe + '"/i' + ini + '" "/f"',                 # other switch
                exe + '"/i' + ini + '" "/x"',                 # unknown switch
                exe + '"/f" "/i' + ini + '"',                 # switch before the ini
                exe + '"/i' + ini + '" "os32_boot.d88"',      # relative disk
                exe + '"/i' + ini + '" "C:\\NP21\\a.iso"',    # CD, not FDD
                exe + '"/i' + ini + '" "C:\\NP21\\b.ini"',    # second ini
                exe + '"/i' + ini + '" "C:\\NP21\\c.np21cfg"',
                exe + '"/i' + ini + '" "' + FD + '" "' + FD + '"',  # two disks
                exe + '"' + ini + '" "' + FD + '"',           # disk with positional ini
                exe + '"/iC:\\NP21\\other.ini"',              # another ini
                exe + '"/i' + ini + '" ' + FD,                # unquoted disk
                '"C:\\NP21\\other.exe" "/i' + ini + '"'):     # another exe
            with self.subTest(command=command), self.assertRaises(live.IniError):
                live.identify([self.row(command)], TARGET)

    def test_executor_forwards_launch_to_fixed_start(self):
        t = Transport({'ok': True, 'value': 43})
        with live.WindowsExecutor(TARGET, t) as ex:
            self.assertEqual(ex.call('start', process=ROW, launch={'form': 'switch', 'fd': FD}), 43)
        self.assertEqual(t.requests[0]['args']['launch'], {'form': 'switch', 'fd': FD})

    def test_powershell_start_rebuilds_same_shape(self):
        for fragment in ("$l.form -ceq 'switch'", """$arg = '"/i' + $target.ini + '"'""",
                         "$l.form -ceq 'positional'", "$l.form -cne 'switch'",
                         'CheckPath $l.fd', "$arg += ' \"' + $l.fd + '\"'", '$si.Arguments=$arg'):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, live.PS_SERVER)

    def test_launch_arguments_match_restart_text(self):
        for launch in ({'form': 'switch', 'fd': None}, {'form': 'switch', 'fd': FD},
                       {'form': 'positional', 'fd': None}):
            with self.subTest(launch=launch):
                self.assertEqual(live.launch_arguments(TARGET, launch), launch_text(launch))


class PostReplaceFailure(unittest.TestCase):
    """PM incident 2026-10-01: ram-8mb --live-apply on a ctl-launched ("/i")
    NP21/W stopped the process, saved original.bin and replaced the ini, then
    the post-replace 'snapshot' failed (PS {"ok":false}). No receipt.json was
    written, so 'restore' was unusable. Any failure after the replacement must
    leave a receipt that restore accepts; nothing restarts automatically."""

    def fail_after_replace(self, f, op, nth=1):
        seen = [0]
        def hook(name):
            if name == op and 'replace' in f.calls[:-1]:
                seen[0] += 1
                if seen[0] == nth:
                    raise live.IniError('Windows operation failed: ' + op)
        f.hook = hook

    def test_incident_snapshot_failure_after_replace_leaves_restorable_receipt(self):
        f = Fake()
        f.rows = [dict(ROW, command=CTL_COMMAND)]
        self.fail_after_replace(f, 'snapshot')
        service = live.Live(f, TARGET)
        with self.assertRaises(live.IniError) as caught:
            service.run('ram-8mb', live_apply=True, exclusive=True)
        self.assertIn('a' * 32, str(caught.exception))
        self.assertIn('receipt ID for restore', str(caught.exception))
        self.assertNotIn('start', f.calls)
        self.assertEqual(f.rows, [])
        self.assertEqual(f.snapshot['data'], RAM_8MB)
        self.assertEqual(f.saved, {'data': RAW, 'signature': 'initial'})
        self.assertEqual(f.receipt_id, 'a' * 32)
        self.assertEqual(f.record['applied'], f.snapshot)
        self.assertEqual(f.record['original'], {'data': RAW, 'signature': 'initial'})
        self.assertEqual(f.record['process'], dict(ROW, command=CTL_COMMAND))
        # Operator restore from the stopped state, in the recorded "/i" shape.
        f.hook = lambda op: None
        result = service.run('restore', receipt='a' * 32, live_apply=True, exclusive=True)
        self.assertEqual(f.snapshot['data'], RAW)
        self.assertEqual(result['pid'], 43)
        self.assertEqual(f.launch, {'form': 'switch', 'fd': None})

    def test_every_failure_after_replace_keeps_receipt_and_never_restarts_twice(self):
        for op, nth in (('query', 1), ('snapshot', 1), ('start', 1), ('query', 2), ('query', 3)):
            f = Fake()
            self.fail_after_replace(f, op, nth)
            with self.subTest(op=op, nth=nth):
                with self.assertRaises(live.IniError) as caught:
                    live.Live(f, TARGET).run('cirrus-on', live_apply=True, exclusive=True)
                self.assertIn('receipt ID for restore: ' + 'a' * 32, str(caught.exception))
                self.assertEqual(f.record['applied'], f.snapshot)
                self.assertLessEqual(f.calls.count('start'), 1)
                self.assertNotIn('restore', f.calls)

    def test_failure_before_or_during_replace_does_not_claim_a_receipt(self):
        for failure in ('replace',):
            f = Fake()
            f.fail = failure
            with self.subTest(failure=failure):
                with self.assertRaises(live.IniError) as caught:
                    live.Live(f, TARGET).run('cirrus-on', live_apply=True, exclusive=True)
                self.assertIn('a' * 32, str(caught.exception))
                self.assertNotIn('receipt ID for restore', str(caught.exception))
                self.assertIn('receipt not confirmed', str(caught.exception))
                self.assertFalse(hasattr(f, 'record'))
                self.assertNotIn('start', f.calls)

    def test_changed_after_replace_is_still_refused_before_restart(self):
        f = Fake()
        def hook(op):
            if op == 'snapshot' and 'replace' in f.calls:
                f.snapshot = dict(f.snapshot, signature='rescanned')
        f.hook = hook
        with self.assertRaises(live.IniError) as caught:
            live.Live(f, TARGET).run('cirrus-on', live_apply=True, exclusive=True)
        self.assertIn('receipt ID for restore', str(caught.exception))
        self.assertNotIn('start', f.calls)

    def test_executor_carries_receipt_id_and_record_into_replace(self):
        import base64
        applied = {'data': base64.b64encode(NEW).decode(), 'signature': REPLACED_SIG}
        t = Transport({'ok': True, 'value': {'applied': applied, 'candidate': CANDIDATE_KEY}})
        record = {'target': TARGET, 'operation': 'cirrus-on',
                  'original': {'data': RAW, 'signature': 'initial'},
                  'applied': {'data': NEW, 'signature': 'pending'}, 'diff': [], 'process': ROW}
        with live.WindowsExecutor(TARGET, t) as ex:
            ex.call('replace', expected=record['original'], data=NEW, receipt='a' * 32, record=record)
            self.assertNotIn('receipt', ex.OPS)
        args = t.requests[0]['args']
        self.assertEqual(args['receipt'], 'a' * 32)
        self.assertEqual(args['record']['original']['data'], base64.b64encode(RAW).decode())

    def block(self, name):
        import re
        return re.search(r"\n    '" + name + r"' \{\n(.*?)\n    \}\n", live.PS_SERVER, re.S).group(1)

    def test_powershell_replace_writes_receipt_after_readback_before_absence_check(self):
        body = self.block('replace')
        order = [body.index(fragment) for fragment in (
            '[IO.File]::Replace(', '$applied = Snapshot $target.ini',
            "if ($applied.data -cne $a.data) { throw 'Readback failed' }",
            'WriteReceipt $a.receipt $a.record $applied')]
        self.assertEqual(order, sorted(order))
        self.assertLess(body.index('WriteReceipt'), body.rindex('AssertAbsent'))
        self.assertIn("'backup changed'", live.PS_SERVER)
        self.assertIn('$record.applied = $applied', live.PS_SERVER)
        self.assertNotIn("\n    'receipt' {", live.PS_SERVER)

    def test_powershell_snapshot_retries_only_bounded_sharing_violations(self):
        import re
        snapshot = re.search(r'function Snapshot\(\$path\) \{\n(.*?)\n\}\n', live.PS_SERVER, re.S).group(1)
        self.assertIn('$f = OpenShared $path', snapshot)
        self.assertNotIn('[IO.File]::Open(', snapshot)
        opener = re.search(r'function OpenShared\(\$path\) \{\n(.*?)\n\}\n', live.PS_SERVER, re.S).group(1)
        for fragment in ('[IO.FileAccess]::Read, [IO.FileShare]::Read', '-isnot [IO.IOException]',
                         '-band 0xFFFF', '-notin @(32,33)', '$clock.ElapsedMilliseconds -ge $TransientMs',
                         'throw', 'Start-Sleep -Milliseconds'):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, opener)
        budget = int(re.search(r'\$TransientMs = (\d+)\n', live.PS_SERVER).group(1))
        # The heaviest request ('replace') opens up to five files through
        # Snapshot; every retry budget must fit inside one transport exchange.
        self.assertLessEqual(5 * budget + 5000, 1000 * live.EXCHANGE_TIMEOUT)


class ReviewRetryRaces(unittest.TestCase):
    """Codex review of 296a360 (P2 x2, P3): while a sharing-violation retry
    waits, the path may be swapped for another file with the same bytes (P2-1)
    or for a reparse point (P2-2); a restore's own receipt is not restorable (P3)."""

    def test_file_key_is_volume_and_index_of_file_identity(self):
        self.assertEqual(live.file_key(REPLACED_SIG), CANDIDATE_KEY)
        for bad in ('replaced', '1:0:2:3:4:0', '1:0:2:3:4:0:5:6', '1:0:x:3:4:0:5', '', '1:0:2:3:4:0:-5'):
            with self.subTest(bad=bad), self.assertRaises(live.IniError):
                live.file_key(bad)

    def test_same_bytes_but_another_file_after_replace_is_not_accepted(self):
        f = Fake()
        f.candidate_key = '1:0:99'  # readback came from B, not the candidate A
        with self.assertRaises(live.IniError) as caught:
            live.Live(f, TARGET).run('cirrus-on', live_apply=True, exclusive=True)
        text = str(caught.exception)
        self.assertIn('readback identity differs from the candidate', text)
        self.assertIn('receipt not confirmed', text)
        self.assertNotIn('receipt ID for restore', text)
        self.assertIn('a' * 32, text)
        self.assertNotIn('start', f.calls)
        self.assertEqual(f.saved, {'data': RAW, 'signature': 'initial'})

    def test_executor_names_candidate_identity_failure_and_validates_replace_value(self):
        import base64
        t = Transport({'ok': False, 'reason': 'candidate-identity'})
        with self.assertRaises(live.IniError) as caught:
            with live.WindowsExecutor(TARGET, t) as ex:
                ex.call('replace', expected={'data': RAW, 'signature': 'id'}, data=NEW)
        self.assertIn('readback identity differs from the candidate', str(caught.exception))
        self.assertIn('no receipt written', str(caught.exception))
        for response in ({'ok': False, 'reason': 'other'}, {'ok': False, 'reason': 'candidate-identity', 'value': 1},
                         {'ok': True, 'value': True, 'reason': 'candidate-identity'}):
            with self.subTest(response=response), self.assertRaises(live.IniError) as caught:
                with live.WindowsExecutor(TARGET, Transport(response)) as ex:
                    ex.call('replace', expected={'data': RAW, 'signature': 'id'}, data=NEW)
            self.assertNotIn('differs from the candidate', str(caught.exception))
        # The identity reason is only meaningful for 'replace'.
        with self.assertRaises(live.IniError) as caught:
            with live.WindowsExecutor(TARGET, Transport({'ok': False, 'reason': 'candidate-identity'})) as ex:
                ex.call('snapshot')
        self.assertNotIn('differs from the candidate', str(caught.exception))
        good = {'data': base64.b64encode(NEW).decode(), 'signature': REPLACED_SIG}
        for value in ({'data': good['data'], 'signature': REPLACED_SIG},
                      {'applied': good}, {'applied': good, 'candidate': '1:0'},
                      {'applied': good, 'candidate': '1:0:2:3'}, {'applied': good, 'candidate': 7},
                      {'applied': good, 'candidate': CANDIDATE_KEY, 'extra': 1}):
            with self.subTest(value=value), self.assertRaises(live.IniError):
                with live.WindowsExecutor(TARGET, Transport({'ok': True, 'value': value})) as ex:
                    ex.call('replace', expected={'data': RAW, 'signature': 'id'}, data=NEW)

    def test_restore_failure_after_replace_does_not_offer_another_restore(self):
        f = Fake()
        service = live.Live(f, TARGET)
        receipt = service.run('cirrus-on', live_apply=True, exclusive=True)['receipt']
        f.fail = 'start'
        with self.assertRaises(live.IniError) as caught:
            service.run('restore', receipt=receipt, live_apply=True, exclusive=True)
        text = str(caught.exception)
        self.assertNotIn('receipt ID for restore', text)
        self.assertIn('restore-operation receipt ID (not restorable; investigate): ' + 'a' * 32, text)
        self.assertEqual(f.snapshot['data'], RAW)
        # ... and that receipt really is refused by restore.
        f.fail = None
        with self.assertRaises(live.IniError):
            service.run('restore', receipt='a' * 32, live_apply=True, exclusive=True)

    def function(self, name):
        import re
        return re.search(r'function ' + name + r'\((.*?)\) \{\n(.*?)\n\}\n', live.PS_SERVER, re.S).group(2)

    def test_powershell_replace_checks_candidate_identity_before_receipt(self):
        body = PostReplaceFailure.block(self, 'replace')
        order = [body.index(fragment) for fragment in (
            '$candidate = NewFile $temp',
            '[IO.File]::Replace(', '$applied = Snapshot $target.ini',
            "if ($applied.data -cne $a.data) { throw 'Readback failed' }",
            'if ((FileKey $applied.signature) -cne (FileKey $candidate.signature)) { throw $CandidateIdentity }',
            'WriteReceipt $a.receipt $a.record $applied',
            'AssertAbsent\n     $value = @{applied=$applied; candidate=(FileKey $candidate.signature)}')]
        self.assertEqual(order, sorted(order))
        self.assertIn("$CandidateIdentity = 'readback identity differs from the candidate'", live.PS_SERVER)
        self.assertIn("if ($_.Exception.Message -ceq $CandidateIdentity) "
                      "{ [Console]::WriteLine('{\"ok\":false,\"reason\":\"candidate-identity\"}') }",
                      live.PS_SERVER)
        key = self.function('FileKey')
        self.assertIn("-cnotmatch '^[0-9]+(:[0-9]+){6}$'", key)
        self.assertIn("$parts[0] + ':' + $parts[1] + ':' + $parts[2]", key)
        new_file = self.function('NewFile')
        self.assertIn('return $s', new_file)
        # Every other NewFile caller discards the snapshot it now returns (no stray output).
        import re
        for line in re.findall(r'^.*\bNewFile \(.*$', live.PS_SERVER, re.M):
            with self.subTest(line=line):
                self.assertIn('$null = NewFile', line)

    def test_powershell_open_rechecks_path_every_attempt_and_after_open(self):
        body = self.function('OpenShared')
        loop = body[body.index('while ($true) {'):]
        self.assertLess(loop.index('CheckPath $path'), loop.index('[IO.File]::Open('))
        after = loop[loop.index('continue'):]
        self.assertIn('try { CheckPath $path } catch { $f.Dispose(); throw }', after)
        self.assertLess(after.index('CheckPath $path'), after.index('return $f'))

    def test_powershell_retry_deadline_is_strict(self):
        body = self.function('OpenShared')
        self.assertIn('$left = $TransientMs - $clock.ElapsedMilliseconds', body)
        self.assertIn('-or $left -le 0) { throw }', body)
        self.assertIn('Start-Sleep -Milliseconds ([Math]::Min(250, $left))', body)
        self.assertIn('if ($clock.ElapsedMilliseconds -ge $TransientMs) { throw }', body)
        self.assertNotIn('Start-Sleep -Milliseconds 250\n', body)


WINDOWS_FIXTURES = '--windows-fixtures' in sys.argv
if WINDOWS_FIXTURES:
    sys.argv.remove('--windows-fixtures')


@unittest.skipUnless(WINDOWS_FIXTURES, 'opt-in real PowerShell temporary fixtures')
class RealPowerShellFixture(unittest.TestCase):
    def test_parser_null_marshaling_and_file_replace(self):
        import base64
        import re
        import subprocess
        replacement = re.search(r'\[IO.File\]::Replace\([^\n]+', live.PS_SERVER).group()
        # Never execute PS_SERVER: parse it only. All file IO below is confined
        # to a fresh GUID directory; no ini, CIM, emulator, env or process APIs.
        script = r"""
$ErrorActionPreference = 'Stop'
[Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
$source = [Console]::In.ReadToEnd()
$tokens = $null; $errors = $null
$null = [Management.Automation.Language.Parser]::ParseInput($source, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'PS_SERVER parse failed' }
Add-Type 'public static class NullProbe { public static bool IsNull(string s) { return s == null; } }'
Write-Output ('ordinary-null-is-null: ' + [NullProbe]::IsNull($null))
if (![NullProbe]::IsNull([System.Management.Automation.Language.NullString]::Value)) {
 throw 'NullString marshaling failed'
}
$root = 'C:\Windows\Temp\np21w-live-fixture-' + [Guid]::NewGuid().ToString('N')
[void][IO.Directory]::CreateDirectory($root)
$temp = $root + '\candidate.bin'
$target = @{ini=$root + '\destination.bin'}
try {
 [IO.File]::WriteAllBytes($temp, [byte[]]@(1,2,3))
 [IO.File]::WriteAllBytes($target.ini, [byte[]]@(4,5))
 REPLACEMENT
 if ([IO.File]::Exists($temp)) { throw 'source remains' }
 if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($target.ini)) -cne 'AQID') {
  throw 'destination mismatch'
 }
 Write-Output 'PS parser / NullString / File.Replace fixture: PASS'
} finally {
 [IO.File]::Delete($temp)
 [IO.File]::Delete($target.ini)
 [IO.Directory]::Delete($root)
}
""".replace('REPLACEMENT', replacement)
        command = base64.b64encode(script.encode('utf-16le')).decode('ascii')
        result = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-NonInteractive',
                                 '-EncodedCommand', command], input=live.PS_SERVER.encode('utf-8'),
                                capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr.decode('utf-8', errors='replace'))
        self.assertIn(b'File.Replace fixture: PASS', result.stdout)



# ---------------------------------------------------------------------------
# Mutants (--mutate), in the style of test_np21w_ctl.py: each mutant is a copy
# of np21w_ini_live.py in a temporary directory (the real source is only read);
# the whole suite runs against it and at least one case must fail (RED).
# A copy that cannot be imported is NOT COUNTED. The identity control must stay GREEN.
# ---------------------------------------------------------------------------
SRC = Path(__file__).resolve().parents[1] / 'np21w_ini_live.py'
IDENTITY = ("INI_SWITCH = '/i'  # exactly what np21w_ctl.py emits",
            "INI_SWITCH = '/i'  # exactly what np21w_ctl.py emits (identity)",
            'identity control (comment only; must stay GREEN)')
MUTATIONS = [
    ("    if token.startswith(INI_SWITCH):",
     "    if token[:2].lower() in ('/i', '-i'):",
     "accept '-i' / '/I' as the ctl switch"),
    ("    launch_of(row['command'], target)\n",
     "",
     "identify no longer checks the command-line shape"),
    ("        if form != 'switch' or not path_key(fd).endswith(FD_EXTENSIONS):",
     "        if not path_key(fd).endswith(FD_EXTENSIONS):",
     "accept a disk argument with the positional ini"),
    ("        if form != 'switch' or not path_key(fd).endswith(FD_EXTENSIONS):",
     "        if form != 'switch':",
     "accept a CD / cfg / ini file as the disk argument"),
    ("FD_EXTENSIONS = ('.d88', ",
     "FD_EXTENSIONS = (",
     "reject ctl's default os32_boot.d88"),
    ("COMMAND_RE = re.compile(r'\"([^\"\\r\\n]+)\"[ \\t]+\"([^\"\\r\\n]+)\"(?:[ \\t]+\"([^\"\\r\\n]+)\")?[ \\t]*')",
     "COMMAND_RE = re.compile(r'\"([^\"\\r\\n]+)\"[ \\t]+\"([^\"\\r\\n]+)\"(?:[ \\t]+\"([^\"\\r\\n]+)\")*[ \\t]*')",
     "accept any number of disk arguments"),
    ("                started = ex.call('start', process=process, launch=launch)",
     "                started = ex.call('start', process=process, launch={'form': 'positional', 'fd': None})",
     "restart always in the positional shape"),
    ("                started = ex.call('start', process=process, launch=launch)",
     "                started = ex.call('start', process=process, launch=dict(launch, fd=None))",
     "restart drops the FD argument"),
    ("                if launch_of(first['command'], self.target) != launch:",
     "                if False:",
     "do not compare the restarted shape"),
    ("""     if ($l.form -ceq 'switch') { $arg = '"/i' + $target.ini + '"' }""",
     """     if ($l.form -ceq 'switch') { $arg = '"' + $target.ini + '"' }""",
     "PS start drops /i"),
    ("      CheckPath $l.fd\n",
     "",
     "PS start does not check the disk path for reparse points"),
    ("      if ($l.form -cne 'switch' -or $l.fd -isnot [string] -or",
     "      if ($l.fd -isnot [string] -or",
     "PS start accepts a disk with the positional shape"),
    ("    text = ('\"' + INI_SWITCH if launch['form'] == 'switch' else '\"') + target['ini'] + '\"'",
     "    text = '\"' + target['ini'] + '\"'",
     "launch_arguments (restart text reserved by the receipt bound) drops /i"),
    # 2026-10-01: receipt written inside 'replace'; bounded sharing-violation retry.
    ("                done = ex.call('replace', expected=before, data=candidate,\n"
     "                               receipt=backup, record=planned_record)",
     "                done = ex.call('replace', expected=before, data=candidate)",
     "replace no longer carries the receipt (incident: no receipt.json after replace)"),
    ("                replaced = True\n",
     "",
     "failure after replace is reported as 'receipt not confirmed'"),
    ("                if checked_snapshot(ex.call('snapshot')) != applied:\n"
     "                    raise IniError('changed after replacement; retain backup and stopped state')",
     "                if False:\n"
     "                    raise IniError('changed after replacement; retain backup and stopped state')",
     "no snapshot comparison between replacement and restart"),
    ("     WriteReceipt $a.receipt $a.record $applied\n     AssertAbsent\n",
     "     AssertAbsent\n",
     "PS replace does not write the receipt"),
    ("     WriteReceipt $a.receipt $a.record $applied\n     AssertAbsent\n",
     "     AssertAbsent\n     WriteReceipt $a.receipt $a.record $applied\n",
     "PS replace writes the receipt only after the absence check"),
    (" $record.applied = $applied\n",
     "",
     "PS receipt keeps the 'pending' placeholder instead of the readback"),
    (" $f = OpenShared $path\n",
     " $f = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)\n",
     "PS Snapshot opens once (no transient retry)"),
    ("   if ($e -isnot [IO.IOException] -or (($e.HResult -band 0xFFFF) -notin @(32,33)) -or",
     "   if ($e -isnot [IO.IOException] -or",
     "PS retries every IOException (missing file, disk errors)"),
    ("   if ($clock.ElapsedMilliseconds -ge $TransientMs) { throw }\n",
     "",
     "PS attempts another open after sleeping past the deadline"),
    ("$TransientMs = 4000\n",
     "$TransientMs = 30000\n",
     "retry budget exceeds one transport exchange"),
    # Codex review of 296a360: same-bytes swap (P2-1), reparse swap (P2-2), P3.
    ("                if file_key(applied['signature']) != done['candidate']:",
     "                if False:",
     "accept a same-bytes readback of another file (runtime)"),
    ("            if op == 'replace' and response == {'ok': False, 'reason': 'candidate-identity'}:",
     "            if False:",
     "candidate-identity failure reported as a generic failure (runtime)"),
    ("FILE_KEY_RE = re.compile(r'[0-9]+(?::[0-9]+){6}')",
     "FILE_KEY_RE = re.compile(r'.+')",
     "file_key accepts a malformed identity (runtime)"),
    ("                        not CANDIDATE_KEY_RE.fullmatch(value['candidate'])):",
     "                        False):",
     "executor accepts a malformed candidate key (runtime)"),
    ("                if replaced and operation == 'restore':",
     "                if False:",
     "a failed restore offers its own (unrestorable) receipt for restore (runtime)"),
    ("  CheckPath $path\n  try { $f = [IO.File]::Open(",
     "  try { $f = [IO.File]::Open(",
     "PS does not recheck the path before each open attempt"),
    ("  try { CheckPath $path } catch { $f.Dispose(); throw }\n",
     "",
     "PS does not recheck the path after the successful open"),
    (" -or $left -le 0) { throw }",
     ") { throw }",
     "PS retries past the deadline"),
    ("     if ((FileKey $applied.signature) -cne (FileKey $candidate.signature)) { throw $CandidateIdentity }\n",
     "",
     "PS writes the receipt for a same-bytes readback of another file"),
    ("     $null = NewFile ($dir + '\\original.bin')",
     "     NewFile ($dir + '\\original.bin')",
     "PS backup leaks NewFile's snapshot into the response"),
    ("""   if ($_.Exception.Message -ceq $CandidateIdentity) { [Console]::WriteLine('{"ok":false,"reason":"candidate-identity"}') }\n""",
     "",
     "PS never reports the candidate-identity reason"),
    ("def launch_of(command, target)",
     "def launch_of(command, target",
     "syntax error: an unimportable copy is NOT COUNTED"),
]


def _load(path):
    spec = importlib.util.spec_from_file_location('np21w_ini_live_mut', str(path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _suite_failures(mod):
    global live
    saved = live
    live = mod
    try:
        suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
        res = unittest.TextTestRunner(stream=io.StringIO(), verbosity=0).run(suite)
        return len(res.failures) + len(res.errors)
    finally:
        live = saved


def mutate():
    original = SRC.read_text(encoding='utf-8')
    bad = red = green = uncounted = 0
    with tempfile.TemporaryDirectory(prefix='np21w-ini-live-mut-') as tmp:
        for i, (old, new, why) in enumerate([IDENTITY] + MUTATIONS):
            if original.count(old) != 1:
                print('MUTATION %d NOT APPLICABLE (%d matches): %s' % (i, original.count(old), why))
                bad += 1
                continue
            path = Path(tmp) / ('mut%d.py' % i)
            path.write_text(original.replace(old, new), encoding='utf-8')
            try:
                mod = _load(path)
            except Exception as exc:
                print('MUTATION %d NOT COUNTED (import: %s): %s' % (i, type(exc).__name__, why))
                uncounted += 1
                continue
            fails = _suite_failures(mod)
            if i == 0:
                ok = fails == 0
                print('IDENTITY %s (%d failed): %s' % ('GREEN' if ok else '**RED (harness broken)**', fails, why))
                bad += not ok
                continue
            if fails:
                red += 1
                print('MUTATION %d RED (%d): %s' % (i, fails, why))
            else:
                green += 1
                bad += 1
                print('MUTATION %d **GREEN (missed)**: %s' % (i, why))
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
