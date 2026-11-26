"""Process and QMP helpers for PowerMac shell observations."""
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
ROM = Path('/usr/share/qemu/openbios-ppc')


class CriterionFailure(AssertionError):
    """An acceptance criterion failed; eligible for an expected mutation RED."""


def require(condition, message):
    if not condition:
        raise CriterionFailure(message)


def command(args, cwd=ROOT, timeout=30):
    with subprocess.Popen([str(a) for a in args], cwd=cwd,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          start_new_session=True, text=True) as p:
        try:
            stdout, stderr = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.communicate()
            raise
        if p.returncode:
            raise RuntimeError(f'{args}: rc={p.returncode}\n{stdout}{stderr}')
        return stdout + stderr


class QMP:
    def __init__(self, process, deadline):
        self.process = process
        self.deadline = deadline
        self.buffer = b''
        self.read()
        self.call('qmp_capabilities')

    def read(self):
        while b'\n' not in self.buffer:
            remaining = self.deadline-time.monotonic()
            if remaining <= 0 or not select.select([self.process.stdout],[],[],remaining)[0]:
                raise RuntimeError('QMP parent deadline')
            data = os.read(self.process.stdout.fileno(),4096)
            if not data:
                raise RuntimeError('QMP EOF')
            self.buffer += data
        line, self.buffer = self.buffer.split(b'\n',1)
        return json.loads(line)

    def call(self, name, arguments=None):
        request = {'execute':name}
        if arguments is not None:
            request['arguments'] = arguments
        self.process.stdin.write(json.dumps(request).encode()+b'\n')
        self.process.stdin.flush()
        while True:
            response = self.read()
            if 'error' in response:
                raise RuntimeError(str(response))
            if 'return' in response:
                return response['return']

    def close(self):
        self.process.stdin.close()
        self.process.stdout.close()
