"""Real local pipes; never execute a lifecycle script or touch an emulator."""
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import np21w_ini_live as live
import np21w_trial as trial


class PipeFixture:
    """Production reader on a real BufferedReader; owner releases writer in finally."""
    def __init__(self, transport_class, timeout=True):
        read_fd, write_fd = os.pipe()
        self.writer = os.fdopen(write_fd, 'wb', buffering=0)
        self.process = Mock(stdin=io.BytesIO(), stdout=os.fdopen(read_fd, 'rb'))
        self.process.wait.side_effect = (
            [subprocess.TimeoutExpired('safe fixture', 3), 0] if timeout else None)
        self.process.wait.return_value = 0
        self.process.poll.return_value = 0
        with patch('subprocess.Popen', return_value=self.process):
            self.channel = transport_class()
        # A complete frame proves the production reader reached the pipe.
        self.writer.write(b'{"ok":true,"value":true}\n')
        assert self.channel.exchange({}) == {'ok': True, 'value': True}
        self.errors = []
        self.done = threading.Event()
        self.closer = None

    def close_async(self, action=None):
        def close():
            try:
                (action or self.channel.close)()
            except Exception as exc:
                self.errors.append(exc)
            finally:
                self.done.set()
        self.closer = threading.Thread(target=close, daemon=True)
        self.closer.start()

    def release(self):
        self.writer.close()
        self.channel.reader.join(timeout=2)
        if self.closer is not None:
            self.closer.join(timeout=2)


class TransportCleanup(unittest.TestCase):
    def test_normal_eof_closes_streams_without_kill(self):
        for cls in (trial.PowerShellTransport, live.PowerShellTransport):
            with self.subTest(transport=cls.__module__):
                fixture = PipeFixture(cls, timeout=False)
                try:
                    fixture.writer.close()
                    fixture.close_async()
                    self.assertTrue(fixture.done.wait(0.5))
                    self.assertEqual(fixture.errors, [])
                    self.assertFalse(fixture.channel.reader.is_alive())
                    self.assertTrue(fixture.process.stdout.closed)
                    self.assertTrue(fixture.process.stdin.closed)
                    fixture.process.wait.assert_called_once_with(timeout=3)
                    fixture.process.kill.assert_not_called()
                finally:
                    fixture.release()

    def test_parent_exit_with_inherited_pipe_still_reports_failure(self):
        for cls in (trial.PowerShellTransport, live.PowerShellTransport):
            with self.subTest(transport=cls.__module__):
                fixture = PipeFixture(cls, timeout=False)
                try:
                    fixture.close_async()
                    self.assertTrue(fixture.done.wait(0.5))
                    self.assertEqual(len(fixture.errors), 1)
                    self.assertIsInstance(fixture.errors[0], live.IniError)
                    fixture.process.kill.assert_not_called()
                    self.assertFalse(fixture.process.stdout.closed)
                finally:
                    fixture.release()
                self.assertFalse(fixture.channel.reader.is_alive())
                self.assertTrue(fixture.process.stdout.closed)

    def test_trial_timeout_cli_preserves_verified_process_and_dispose_stage(self):
        # Lifecycle exchanges stay synthetic; dispose uses the real pipe reader.
        from test_np21w_trial import Transport, EXE, BASE, CWD, CREATED, HDD, image_fixture
        images = image_fixture()
        images.__enter__()
        self.addCleanup(images.__exit__, None, None, None)
        fixture = PipeFixture(trial.PowerShellTransport)
        transports = []
        output = io.StringIO()
        codes = []
        def factory(bound):
            transport = Transport(bound)
            transport.close = fixture.channel.close
            transports.append(transport)
            return trial.WindowsExecutor(bound, transport)
        def run_cli():
            with redirect_stdout(output):
                codes.append(trial.main(
                    ['--exe', EXE, '--baseline', BASE, '--cwd', CWD,
                     '--pid', '42', '--created', CREATED, '--hdd', HDD, '--fdd-eject',
                     '--execute', '--exclusive-operator'],
                    executor_factory=factory, llm=lambda p, u, m: json.dumps(p)))
        try:
            fixture.close_async(run_cli)
            self.assertTrue(fixture.done.wait(0.5))
            self.assertEqual(fixture.errors, [])
            self.assertEqual(codes, [2])
            result = json.loads(output.getvalue())
            self.assertIs(result['ok'], False)
            self.assertEqual(result['stage'], 'dispose')
            self.assertEqual(result['process'], transports[0].rows[0])
            self.assertEqual(result['completed'], transports[0].calls)
            self.assertEqual(result['completed'][-3:], ['start', 'query', 'query'])
            self.assertEqual(result['completed'].count('start'), 1)
            self.assertIs(result['retry'], False)
            self.assertIs(result['restore'], False)
            fixture.process.kill.assert_not_called()
        finally:
            fixture.release()
        self.assertTrue(fixture.process.stdout.closed)

    def test_live_wait_timeout_is_failure_even_when_kill_releases_reader(self):
        fixture = PipeFixture(live.PowerShellTransport)
        try:
            fixture.writer.close()
            fixture.channel.reader.join(timeout=2)
            fixture.close_async()
            self.assertTrue(fixture.done.wait(0.5))
            self.assertEqual(len(fixture.errors), 1, 'kill fallback must not hide wait timeout')
            self.assertIsInstance(fixture.errors[0], live.IniError)
            fixture.process.kill.assert_called_once_with()
            self.assertTrue(fixture.process.stdout.closed)
        finally:
            fixture.release()

    def test_real_host_subprocess_shutdown_success_and_timeout(self):
        # Fixed benign Python only. Outer cleanup owns this exact child; no
        # process discovery, PowerShell, emulator or descendant termination.
        popen = subprocess.Popen
        for cls in (trial.PowerShellTransport, live.PowerShellTransport):
            for timeout in (False, True):
                with self.subTest(transport=cls.__module__, timeout=timeout):
                    script = ('import sys,time; print(\'{"ok":true,"value":true}\', flush=True); '
                              'sys.stdin.buffer.read(); time.sleep(' + ('10' if timeout else '0') + ')')
                    process = popen([sys.executable, '-B', '-c', script],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=subprocess.DEVNULL)
                    with patch('subprocess.Popen', return_value=process):
                        channel = cls()
                    errors = []
                    def close():
                        try:
                            channel.close()
                        except Exception as exc:
                            errors.append(exc)
                    closer = threading.Thread(target=close, daemon=True)
                    try:
                        self.assertEqual(channel.queue.get(timeout=2), b'{"ok":true,"value":true}\n')
                        closer.start()
                        closer.join(timeout=4)
                        self.assertFalse(closer.is_alive(), 'shutdown exceeded bounded wait')
                        self.assertEqual(len(errors), int(timeout))
                        if timeout:
                            self.assertIsInstance(errors[0], (live.IniError, subprocess.TimeoutExpired))
                            if cls is trial.PowerShellTransport:
                                self.assertIsNone(process.poll(), 'trial must never kill its transport')
                        else:
                            self.assertEqual(process.returncode, 0)
                            self.assertTrue(process.stdout.closed)
                            self.assertFalse(channel.reader.is_alive())
                    finally:
                        if process.poll() is None:
                            process.kill()  # Test-owned benign child only.
                        process.wait(timeout=2)
                        channel.reader.join(timeout=2)
                        if closer.ident is not None:
                            closer.join(timeout=2)
                        process.stdin.close()
                    self.assertFalse(channel.reader.is_alive())
                    self.assertTrue(process.stdout.closed)

    def test_trial_cli_timeout_preserves_failure_json_and_completed_stages(self):
        from test_np21w_trial import Transport, EXE, BASE, CWD, CREATED, HDD, image_fixture
        images = image_fixture()
        images.__enter__()
        self.addCleanup(images.__exit__, None, None, None)
        for fail in (None, 1):
            with self.subTest(fail=fail):
                fixture = PipeFixture(trial.PowerShellTransport)
                output = io.StringIO()
                transports = []
                codes = []
                def factory(bound):
                    transport = Transport(bound, fail=fail)
                    transport.close = fixture.channel.close
                    transports.append(transport)
                    return trial.WindowsExecutor(bound, transport)
                def run():
                    with redirect_stdout(output):
                        codes.append(trial.main(
                            ['--exe', EXE, '--baseline', BASE, '--cwd', CWD,
                             '--pid', '42', '--created', CREATED, '--hdd', HDD, '--fdd-eject',
                             '--execute', '--exclusive-operator'], executor_factory=factory,
                            llm=lambda p, u, m: json.dumps(p)))
                try:
                    fixture.close_async(run)
                    self.assertTrue(fixture.done.wait(0.5), 'failure JSON blocked by reader cleanup')
                    self.assertEqual(fixture.errors, [])
                    self.assertEqual(codes, [2])
                    result = json.loads(output.getvalue())
                    self.assertFalse(result['ok'])
                    self.assertEqual(result['stage'], 'dispose' if fail is None else 'lock')
                    self.assertEqual(result['completed'], transports[0].calls if fail is None else [])
                    self.assertEqual(result['trial'], result['plan']['trial'])
                    self.assertEqual(result['cwd'], CWD)
                    self.assertFalse(result['retry'])
                    self.assertFalse(result['restore'])
                    self.assertNotIn('safe fixture', output.getvalue())
                    if fail is None:
                        self.assertEqual(result['process']['pid'], 43)
                    fixture.process.kill.assert_not_called()
                finally:
                    fixture.release()
                self.assertTrue(fixture.process.stdout.closed)

    def test_trial_preserves_wait_timeout_while_reader_is_blocked(self):
        fixture = PipeFixture(trial.PowerShellTransport)
        try:
            fixture.close_async()
            self.assertTrue(fixture.done.wait(0.5))
            self.assertEqual(len(fixture.errors), 1)
            self.assertIsInstance(fixture.errors[0], subprocess.TimeoutExpired)
            self.assertEqual(fixture.errors[0].timeout, 3)
            fixture.process.kill.assert_not_called()
        finally:
            fixture.release()

    def test_wait_timeout_with_blocked_reader_returns_failure_bounded(self):
        for cls in (trial.PowerShellTransport, live.PowerShellTransport):
            with self.subTest(transport=cls.__module__):
                fixture = PipeFixture(cls)
                try:
                    fixture.close_async()
                    self.assertTrue(fixture.done.wait(0.5),
                                    'close blocked on another thread\'s BufferedReader')
                    self.assertEqual(len(fixture.errors), 1, 'timeout must not become success')
                    self.assertIsInstance(fixture.errors[0], (live.IniError, subprocess.TimeoutExpired))
                    self.assertTrue(fixture.channel.reader.is_alive())
                    self.assertFalse(fixture.process.stdout.closed)
                    self.assertTrue(fixture.process.stdin.closed)
                    if cls is trial.PowerShellTransport:
                        fixture.process.kill.assert_not_called()
                    else:
                        fixture.process.kill.assert_called_once_with()
                finally:
                    fixture.release()
                self.assertFalse(fixture.channel.reader.is_alive())
                self.assertTrue(fixture.process.stdout.closed, 'reader must eventually own cleanup')


if __name__ == '__main__':
    unittest.main()
