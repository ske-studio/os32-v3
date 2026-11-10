"""Optional per-translation-unit ccache; never cache linked test verdicts."""
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def build(argv, **kwargs):
    """Preserve source spellings, CWD, debug paths and strict ccache defaults.

    Unlike SQLite-only callers, general tests use active __FILE__ and -g.
    Disable base_dir rewriting so absolute source/include paths stay intact.
    Split ordinary compile-and-link invocations to reuse unchanged TUs in
    mutation runs. Exotic driver modes retain the original invocation.
    """
    if kwargs.get('shell') or isinstance(argv, (str, bytes)):
        return subprocess.run(argv, **kwargs)
    cache = shutil.which('ccache')
    if os.environ.get('OS32_NO_CCACHE') == '1' or not cache:
        return subprocess.run(argv, **kwargs)
    argv = list(map(str, argv))
    prefix = [cache, 'compiler_check=content', 'base_dir=', 'sloppiness=', 'hash_dir=true']
    if '-c' in argv:
        return subprocess.run(prefix + argv, **kwargs)
    # These modes can depend on the output name or on joint driver state.
    if (kwargs.get('shell') or '-o' not in argv or
            any(a.startswith(('-g', '-flto', '-save-temps', '@')) or
                a in ('-E', '-S', '-x', '-MD', '-MMD', '-MF', '-MT', '-MQ') for a in argv)):
        return subprocess.run(argv, **kwargs)
    sources = [i for i, a in enumerate(argv) if a.endswith('.c') and
               not a.startswith('-') and argv[i-1] not in ('-include', '-imacros', '-o')]
    if not sources:
        return subprocess.run(argv, **kwargs)
    output = argv.index('-o')
    # Preserve options and their arguments; only remove link inputs/options.
    flags = []
    skip = False
    for i, arg in enumerate(argv[1:], 1):
        if skip:
            skip = False
            continue
        if i == output or arg in ('-l', '-L'):
            skip = True
        elif i not in sources and not arg.endswith(('.o', '.a', '.so')) and not arg.startswith(('-Wl,', '-l', '-L')):
            flags.append(arg)
    with tempfile.TemporaryDirectory(prefix='cc-cache-') as tmp:
        link = argv.copy()
        stdout, stderr = [], []
        for i in sources:
            obj = str(Path(tmp) / (str(i) + '.o'))
            result = subprocess.run(prefix + [argv[0], *flags, '-c', argv[i], '-o', obj], **kwargs)
            if result.returncode:
                return result
            if kwargs.get('capture_output') or kwargs.get('stdout') == subprocess.PIPE:
                stdout.append(result.stdout or ('' if kwargs.get('text') or kwargs.get('encoding') else b''))
            if kwargs.get('capture_output') or kwargs.get('stderr') == subprocess.PIPE:
                stderr.append(result.stderr or ('' if kwargs.get('text') or kwargs.get('encoding') else b''))
            link[i] = obj
        result = subprocess.run(link, **kwargs)
        if stdout:
            result.stdout = result.stdout[:0].join([*stdout, result.stdout])
        if stderr:
            result.stderr = result.stderr[:0].join([*stderr, result.stderr])
        return result
