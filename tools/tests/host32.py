"""Explicit ILP32 host execution; never replace subprocess or fall back.
SIGSYS means the runner is unavailable and is not a test verdict. Other
signals are test failures: preserve the negative return code for the suite
(including mutation checks), and report signal N (runner=...).
"""
import os
import pathlib
import shutil
import signal
import subprocess
import sysconfig
import sys


def verify_native():
    stdlib = pathlib.Path(sysconfig.get_path('stdlib')) / 'subprocess.py'
    functions = (subprocess.run, getattr(subprocess.Popen, '__init__', None),
                 getattr(subprocess.Popen, '_execute_child', None))
    if (pathlib.Path(subprocess.__file__).resolve() != stdlib.resolve() or
            any(not hasattr(fn, '__code__') or
                pathlib.Path(fn.__code__.co_filename).resolve() != stdlib.resolve()
                for fn in functions)):
        raise RuntimeError('native: subprocess was replaced; runner cannot be verified')

    verify_binfmt()


def verify_binfmt(root=pathlib.Path('/proc/sys/fs/binfmt_misc')):
    """Reject enabled magic handlers matching i386 ELF; unreadable is unknown."""
    try:
        if (root / 'status').read_text().strip() != 'enabled':
            return
        entries = list(root.iterdir())
    except OSError:
        return
    header = b'\x7fELF\x01\x01\x01' + bytes(9) + b'\x02\x00\x03\x00'
    for entry in entries:
        if entry.name in ('status', 'register'):
            continue
        try:
            lines = entry.read_text().splitlines()
        except OSError:
            continue
        if not lines or lines[0] != 'enabled':
            continue
        fields = dict(line.split(' ', 1) for line in lines[1:] if ' ' in line)
        try:
            magic = bytes.fromhex(fields['magic'])
            mask = bytes.fromhex(fields.get('mask', 'ff' * len(magic)))
            offset = int(fields.get('offset', '0'))
        except (KeyError, ValueError):
            continue
        # A magic entry must actually constrain the ELF signature, class and
        # EM_386; compare masked bytes (binfmt commonly masks ET_DYN/OSABI).
        required = (0, 1, 2, 3, 4, 18, 19)
        if (len(mask) != len(magic) or offset < 0 or
                any(not offset <= i < offset + len(magic) or
                    mask[i - offset] != 255 for i in required)):
            continue
        sample = header[offset:offset + len(magic)]
        if len(sample) == len(magic) and all((a & m) == (b & m)
                for a, b, m in zip(sample, magic, mask)):
            raise RuntimeError(f'native: enabled i386 ELF binfmt handler: {entry.name}')


def selected_runner(runner=None):
    if runner is not None:
        return runner
    runners = os.environ.get('HOST32_RUNNERS', 'native qemu').split()
    if not runners:
        raise ValueError('HOST32_RUNNERS is empty')
    return runners[0]


def report_signal(returncode, runner=None):
    if returncode < 0:
        message = f'signal {-returncode} (runner={selected_runner(runner)})'
        if returncode == -signal.SIGSYS:
            raise RuntimeError(message + '; not a test verdict')
        print(message, file=sys.stderr)


def is_ilp32(args, cwd=None):
    if not isinstance(args, (list, tuple)) or not args:
        return False
    path = pathlib.Path(args[0])
    if not path.is_absolute():
        path = pathlib.Path(cwd or os.getcwd()) / path
    try:
        with path.open('rb') as stream:
            header = stream.read(20)
    except OSError:
        return False
    return header[:5] == b'\x7fELF\x01' and header[18:20] == b'\x03\x00'


def command(args, runner=None, cwd=None):
    """Select an explicit runner for an ILP32 fixture, including stream I/O."""
    if not is_ilp32(args, cwd):
        return args
    runner = selected_runner(runner)
    if runner == 'native':
        verify_native()
        return args
    if runner == 'qemu':
        return [shutil.which('qemu-i386') or 'qemu-i386', *args]
    raise ValueError(runner)


def run(args, *, runner=None, **kwargs):
    # Legacy mixed LP64/ILP32 suites pass their runtime calls here explicitly.
    # Existing suites use the first exported runner; four suites loop controls.
    ilp32 = not kwargs.get('shell') and is_ilp32(args, kwargs.get('cwd'))
    if ilp32:
        args = command(args, runner, kwargs.get('cwd'))
    result = subprocess.run(args, **kwargs)
    if ilp32:
        report_signal(result.returncode, runner)
    return result
