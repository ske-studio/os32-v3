#!/usr/bin/env python3
"""Small standalone jobs.sh tests; deliberately not registered with make check."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / 'jobs.sh'


class JobsTest(unittest.TestCase):
    def setUp(self):
        self.home = tempfile.TemporaryDirectory()
        self.addCleanup(self.home.cleanup)
        self.env = dict(os.environ, HOME=self.home.name)
        self.root = Path(self.home.name) / 'os32-tmp/jobs'

    def call(self, *args):
        return subprocess.run([str(SCRIPT), *args], env=self.env,
                              capture_output=True, text=True, timeout=10)

    def load(self, label):
        return json.loads((self.root / f'{label}.json').read_text())

    def save(self, label, record):
        (self.root / f'{label}.json').write_text(json.dumps(record))

    def run_job(self, label, code, next_step='レビューへ'):
        return self.call('run', label, '--next', next_step, '--',
                         'python3', '-c', code)

    def test_run_status_ack_and_log(self):
        result = self.run_job('検査',
                             'import sys; print(sys.stdin.read()); '
                             'print("stdout"); print("stderr", file=sys.stderr); '
                             'sys.exit(7)', 'レビュー "確認"\n次')
        self.assertEqual(result.returncode, 7)
        self.assertEqual(result.stdout, '')
        self.assertEqual(result.stderr, '')
        record = self.load('検査')
        self.assertEqual(record['cwd'], os.getcwd())
        self.assertEqual(record['rc'], 7)
        self.assertGreaterEqual(record['end'], record['start'])
        self.assertEqual(record['next'], 'レビュー "確認"\n次')
        self.assertIn('stdout', Path(record['log']).read_text())
        self.assertIn('stderr', Path(record['log']).read_text())
        status = self.call('status')
        self.assertEqual(status.returncode, 0)
        self.assertIn('終了・未確認', status.stdout)
        self.assertIn('rc=7', status.stdout)
        self.assertIn('レビュー "確認"\\n次', status.stdout)
        self.assertEqual(len(status.stdout.splitlines()), 1)
        self.assertEqual(self.call('ack', '検査').returncode, 0)
        self.assertNotIn('未確認', self.call('status').stdout)
        self.assertTrue(self.load('検査')['ack'])

    def test_unknown_missing_pid_or_end(self):
        self.assertEqual(self.run_job('lost', 'pass').returncode, 0)
        record = self.load('lost')
        record.update(end=None, rc=None)
        record.pop('pid')
        self.save('lost', record)
        status = self.call('status')
        self.assertEqual(status.returncode, 0)
        self.assertIn('不明', status.stdout)
        self.assertNotIn('終了', status.stdout)
        self.assertNotIn('rc=0', status.stdout)
        self.assertNotEqual(self.call('ack', 'lost').returncode, 0)
        record['pid'] = 2147483647  # Outside Linux's allocatable PID range.
        self.save('lost', record)
        self.assertIn('不明', self.call('status').stdout)

    def test_running_and_duplicate_label(self):
        gate = Path(self.home.name) / 'release'
        code = ('import pathlib, time; '
                f'p = pathlib.Path({str(gate)!r}); '
                '\nwhile not p.exists(): time.sleep(0.02)')
        process = subprocess.Popen([str(SCRIPT), 'run', 'active', '--',
                                    'python3', '-c', code], env=self.env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 5
            while not (self.root / 'active.json').exists():
                if time.monotonic() > deadline:
                    self.fail('run did not create its record')
                time.sleep(0.02)
            self.assertIn('走行中', self.call('status').stdout)
            self.assertNotEqual(self.call('ack', 'active').returncode, 0)
            self.assertNotEqual(self.run_job('active', 'pass').returncode, 0)
        finally:
            gate.touch()
            process.communicate(timeout=5)
        self.assertEqual(process.returncode, 0)
        self.assertIn('rc=0', self.call('status').stdout)

    def test_prune_and_newest_first(self):
        for label in ('old', 'unacked', 'recent'):
            self.assertEqual(self.run_job(label, 'pass').returncode, 0)
        old_end = time.time() - 8 * 86400
        for label in ('old', 'unacked'):
            record = self.load(label)
            record.update(start=old_end - 1, end=old_end)
            self.save(label, record)
        self.assertEqual(self.call('ack', 'old').returncode, 0)
        self.assertEqual(self.call('ack', 'recent').returncode, 0)
        self.assertTrue(self.call('status').stdout.startswith('recent:'))
        self.assertTrue((self.root / 'old.json').exists())
        status = self.call('status', '--prune')
        self.assertEqual(status.returncode, 0)
        self.assertFalse((self.root / 'old.json').exists())
        self.assertFalse((self.root / 'old.log').exists())
        self.assertTrue((self.root / 'unacked.json').exists())
        self.assertTrue((self.root / 'recent.json').exists())

    def test_bad_arguments_and_missing_command(self):
        self.assertEqual(self.call('status').returncode, 0)
        for args in ((), ('run', '../bad', '--', 'true'),
                     ('run', 'empty', '--'), ('status', '--bad'),
                     ('ack', 'missing')):
            self.assertNotEqual(self.call(*args).returncode, 0)
        self.assertEqual(self.call('run', 'missing', '--',
                                   '/no/such/jobs-test-command').returncode, 127)
        self.assertIn('rc=127', self.call('status').stdout)


if __name__ == '__main__':
    unittest.main()
