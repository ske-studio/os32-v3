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
import time


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
    if ilp32:
        kwargs.setdefault('timeout', RUN_TIMEOUT)
    result = subprocess.run(args, **kwargs)
    if ilp32:
        report_signal(result.returncode, runner)
    return result


# Default bound for unspecified ILP32 calls. Explicit suite deadlines remain
# effective; mutpar retries timeouts once after concurrent workers finish.
RUN_TIMEOUT = 120


def build(args, **kwargs):
    """Reuse a compiled fixture across runners, keyed by preprocessed inputs.

    GCC still resolves every header on each call. Changed source, includes,
    compiler, flags or objects invalidate the cache; failed builds never enter
    it. flock protects publication, cache hits and pruning, not compilation.
    Cache files are ordinary build artifacts under build/out, not leaked tmp.
    """
    import hashlib
    import fcntl
    import re
    import tempfile
    if '-o' not in args or not any(str(a).endswith('.c') for a in args):
        return subprocess.run(args, **kwargs)
    args = list(map(str, args))
    output = pathlib.Path(args[args.index('-o') + 1])
    cwd = pathlib.Path(kwargs.get('cwd') or os.getcwd())
    if not output.is_absolute():
        output = cwd / output
    preprocess = args[:args.index('-o')] + args[args.index('-o') + 2:]
    preprocess = [a for a in preprocess if a != '-c']
    pre = subprocess.run(preprocess + ['-E', '-P'], capture_output=True,
                         cwd=kwargs.get('cwd'), timeout=RUN_TIMEOUT)
    if pre.returncode:
        return subprocess.run(args, **kwargs)
    root = pathlib.Path(__file__).resolve().parents[2]
    # Normalize only temporary directory names, retaining filenames and flags.
    tmp_prefix = re.escape(tempfile.gettempdir()) + r'/[^/\s]+/'
    def stable(value):
        return re.sub(tmp_prefix, '<tmp>/', value)
    digest = hashlib.sha256(pre.stdout)
    digest.update(stable(' '.join(preprocess)).encode())
    digest.update(repr('-c' in args).encode())
    compiler = pathlib.Path(shutil.which(args[0]) or args[0]).resolve()
    stamp = compiler.stat()
    digest.update(repr((str(compiler), stamp.st_size, stamp.st_mtime_ns)).encode())
    digest.update(subprocess.check_output([args[0], '--version'], timeout=RUN_TIMEOUT))
    for a in preprocess:
        path = cwd / a
        if a.endswith(('.o', '.a')) and path.is_file():
            digest.update(path.read_bytes())
    cache = root / 'build/out/host32-fixtures'
    cache.mkdir(parents=True, exist_ok=True)
    cached = cache / digest.hexdigest()
    with (cache / '.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        prune_fixtures(cache)
        if cached.exists():
            os.utime(cached, None)
            shutil.copy2(cached, output)
            return subprocess.CompletedProcess(args, 0, '' if kwargs.get('text') else b'',
                                                '' if kwargs.get('text') else b'')
    fd, name = tempfile.mkstemp(prefix=cached.name + '-', suffix='.pending', dir=cache)
    os.close(fd)
    pending = pathlib.Path(name)
    compile_args = args.copy()
    compile_args[compile_args.index('-o') + 1] = str(pending)
    try:
        result = subprocess.run(compile_args, **kwargs)
        if result.returncode == 0:
            with (cache / '.lock').open('a') as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                pending.replace(cached)
                shutil.copy2(cached, output)
                prune_fixtures(cache)
        return result
    finally:
        pending.unlink(missing_ok=True)


# 32 MiB / 1024 entries retain a working set of small host fixtures, while
# bounding both storage and inode use across mutation generations. One lock
# protects readers, publication and LRU pruning. Each compiler owns its pending.
CACHE_BYTES = 32 * 1024 * 1024
CACHE_ENTRIES = 1024
# Compilers run outside the lock and can take arbitrarily long. Allow an hour
# before reclaiming pending files left by abnormal termination.
PENDING_MAX_AGE = 3600


def prune_fixtures(cache):
    entries = []
    cutoff = time.time() - PENDING_MAX_AGE
    for path in cache.iterdir():
        if path.name == '.lock':
            continue
        if path.suffix == '.pending':
            try:
                if path.stat().st_mtime < cutoff:
                    path.unlink(missing_ok=True)
            except FileNotFoundError:
                pass  # The compiler's finally can remove it outside the lock.
            continue
        if path.suffix == '.lock':
            path.unlink(missing_ok=True)
            continue
        stamp = path.stat()
        entries.append((stamp.st_mtime_ns, path, stamp.st_size))
    total = sum(size for _, _, size in entries)
    count = len(entries)
    for _, path, size in sorted(entries):
        if total <= CACHE_BYTES and count <= CACHE_ENTRIES:
            break
        path.unlink()
        total -= size
        count -= 1


def _receipt(root, runner):
    # Keep diagnostic receipts separate for concurrent make sessions. Direct
    # invocations share their invoking shell; receipts never skip a control.
    session = os.environ.get('OS32_CONTROL_SESSION', str(os.getppid()))
    return pathlib.Path(root) / 'build/out/host32-controls' / (
        pathlib.Path(sys.argv[0]).stem + '-' + selected_runner(runner) + '-' + session + '.json')


# Bound diagnostic receipt age while preserving concurrent recent sessions.
# Receipts do not authorize skipping work. Never prune our own receipt.
CONTROL_MAX_AGE = 6 * 3600


def prune_controls(cache, receipt, runner):
    prefix = pathlib.Path(sys.argv[0]).stem + '-' + selected_runner(runner) + '-'
    cutoff = time.time() - CONTROL_MAX_AGE
    for path in cache.iterdir():
        if path != receipt and path.name.startswith(prefix) and path.suffix == '.json':
            try:
                if path.stat().st_mtime < cutoff:
                    path.unlink(missing_ok=True)
            except FileNotFoundError:
                pass  # Another session pruned it concurrently.


def control(mutate, runner, root):
    """Run the normal control in every mode; receipts never replace execution."""
    import contextlib
    import hashlib
    import json
    @contextlib.contextmanager
    def checked():
        script = pathlib.Path(sys.argv[0]).resolve()
        cache = pathlib.Path(root) / 'build/out/host32-controls'
        cache.mkdir(parents=True, exist_ok=True)
        receipt = _receipt(root, runner)
        prune_controls(cache, receipt, runner)
        files = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z', '--cached', '--others', '--exclude-standard',
            '--', 'tools', 'exec', 'kernel', 'gfx', 'drivers', 'lib', 'include',
            'arch', 'platform', 'fs', 'sdk', 'userland', 'kapi', 'net',
            'boot', 'build', 'Makefile']).split(b'\0')
        digest = hashlib.sha256()
        for name in sorted(set(files)):
            path = pathlib.Path(root) / os.fsdecode(name)
            if path.is_file():
                digest.update(name); digest.update(path.read_bytes())
        compiler = pathlib.Path(shutil.which('gcc') or 'gcc').resolve()
        digest.update(str(compiler).encode())
        stamp = compiler.stat()
        digest.update(repr((stamp.st_size, stamp.st_mtime_ns)).encode())
        digest.update(subprocess.check_output([str(compiler), '--version']))
        expected = {'inputs': digest.hexdigest(), 'runner': selected_runner(runner),
                    'cross': os.environ.get('CROSS_DIR', '')}
        receipt.unlink(missing_ok=True)
        yield True
        receipt.write_text(json.dumps(expected))
    return checked()


def begin_control(mutate, runner, root):
    global _control_receipt
    _control_receipt = _receipt(root, runner)
    _control_receipt.unlink(missing_ok=True)


_control_receipt = None


def control_session(fn):
    """A later assertion/error must invalidate an earlier normal result."""
    import functools
    @functools.wraps(fn)
    def checked(*args, **kwargs):
        try:
            return fn(*args, **kwargs)
        except BaseException:
            if _control_receipt is not None:
                _control_receipt.unlink(missing_ok=True)
            raise
    return checked
