"""Linux clipboard and CUPS subprocesses for the host agent."""
import os
import shutil
import subprocess
import tempfile


class ServiceUnavailable(Exception):
    """A required host service or executable is unavailable."""


class _Proc:
    def __init__(self, argv, data=None):
        self._out = tempfile.TemporaryFile()
        self._err = tempfile.TemporaryFile()
        self._input = None
        try:
            if data is not None:
                self._input = tempfile.TemporaryFile()
                self._input.write(data)
                self._input.seek(0)
            self.p = subprocess.Popen(argv, stdin=self._input or subprocess.DEVNULL,
                                      stdout=self._out, stderr=self._err)
        except OSError as exc:
            self.close()
            raise ServiceUnavailable(str(exc)) from exc
        self._output = None
        self._stderr = None

    def poll(self):
        return self.p.poll()

    def output(self):
        if self._output is None:
            self._out.seek(0)
            self._output = self._out.read()
        return self._output

    def stderr(self):
        if self._stderr is None:
            self._err.seek(0)
            self._stderr = self._err.read()
        return self._stderr

    def kill(self):
        try:
            self.p.kill()
        except ProcessLookupError:
            pass
        self.close()

    def close(self):
        for stream in (self._input, self._out, self._err):
            if stream is not None:
                stream.close()


def _command(name):
    path = shutil.which(name)
    if path is None:
        raise ServiceUnavailable('missing executable: ' + name)
    return path


def clip_start(action: str, data: bytes, backend: str) -> _Proc:
    if backend == 'auto':
        backend = 'wayland' if os.environ.get('WAYLAND_DISPLAY') else 'xclip'
    if backend == 'wayland' and os.environ.get('WAYLAND_DISPLAY'):
        argv = [_command('wl-paste' if action == 'get' else 'wl-copy')]
        if action == 'get':
            argv += ['--no-newline']
    elif backend == 'xclip' and os.environ.get('DISPLAY'):
        argv = [_command('xclip'), '-selection', 'clipboard',
                '-out' if action == 'get' else '-in']
    else:
        raise ServiceUnavailable('no clipboard display or backend')
    if action not in ('get', 'put'):
        raise ValueError('unknown clipboard action: ' + action)
    return _Proc(argv, data if action == 'put' else None)


def print_start(data: bytes, printer: str | None, raw: bool) -> _Proc:
    argv = [_command('lp')]
    if printer is not None:
        argv += ['-d', printer]
    if raw:
        argv += ['-o', 'raw']
    return _Proc(argv, data)
