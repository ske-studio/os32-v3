"""Content-addressed translation-unit results; cache failures never skip checks."""
import ctypes
import functools
import hashlib
import json
import os
import pathlib
import re
import shlex
import subprocess
import tempfile

import clang_ast as ast

FORMAT = 1
RESULT_VERSION = 1
_WRITE_FAILED = False
CHECKERS = {'c-dialect': 'check_c_dialect.py', 'p2v': 'check_p2v.py',
            'arch-asm': 'check_arch_asm.py'}


def _json(value):
    return json.dumps(value, ensure_ascii=True, sort_keys=True,
                      separators=(',', ':'), allow_nan=False).encode()


def _digest(value):
    return hashlib.sha256(_json(value)).hexdigest()


@functools.lru_cache(maxsize=None)
def _checker_source_digest(tool):
    package = pathlib.Path(__file__).resolve().parent
    tools = package.parent
    paths = [tools / CHECKERS[tool], tools / 'source_roots.py']
    paths += sorted(p for p in package.rglob('*')
                    if p.is_file() and '__pycache__' not in p.parts)
    return _digest([(p.relative_to(tools).as_posix(),
                     hashlib.sha256(p.read_bytes()).hexdigest()) for p in paths])


@functools.lru_cache(maxsize=1)
def _compiler_identity():
    r = subprocess.run(['clang', '--version'], capture_output=True, check=True)
    version = ast.cx.conf.lib.clang_getClangVersion
    version.restype = ast.cx._CXString
    version.argtypes = []
    libversion = ast.cx._CXString.from_result(version())
    binding = pathlib.Path(ast.cx.__file__).resolve()
    return [r.stdout.decode(), libversion, str(binding),
            hashlib.sha256(binding.read_bytes()).hexdigest()]


def _name(value, root):
    value = re.sub(re.escape(str(root)) + r'(?=/|$)', '<root>', str(value))
    return re.sub(r'[^\s]*/c_dialect_out_[^/]+/out/', '<out>/', value)


def _arguments(argv, root):
    """Normalize path operands only; macro string values retain their bytes."""
    result = []
    path_next = False
    literal_next = False
    options = ('-I', '-isystem', '-iquote', '-idirafter', '-include', '-imacros',
               '-isysroot', '--sysroot=', '-resource-dir=', '-working-directory=')
    for arg in argv:
        if literal_next:
            result.append(arg)
            literal_next = False
            continue
        if arg in ('-D', '-U'):
            result.append(arg)
            literal_next = True
            continue
        if path_next:
            result.append(_name(arg, root))
            path_next = False
            continue
        for opt in options:
            if arg == opt:
                path_next = True
                result.append(arg)
                break
            if arg.startswith(opt) and len(arg) > len(opt):
                result.append(opt + _name(arg[len(opt):], root))
                break
        else:
            result.append(arg if arg.startswith('-') else _name(arg, root))
    return result


def _internal_cache_key(tool, src, argv, root, variant=None):
    """Resolve every dependency afresh, including system/generated headers."""
    root = pathlib.Path(root).absolute()
    args = ast.flags(argv, root)
    with tempfile.TemporaryDirectory(prefix='check_result_deps_') as tmp:
        depfile = pathlib.Path(tmp, 'unit.d')
        r = subprocess.run(['clang', *args, '-E', '-MD', '-MF', str(depfile),
                            '-MT', 'unit', str(root / src)],
                           cwd=root, capture_output=True)
        dependencies = depfile.read_text() if depfile.is_file() else ''
    if r.returncode:
        return None
    deps = shlex.split(dependencies.replace('\\\n', '').partition(':')[2])
    if not deps:
        return None
    paths = {pathlib.Path(os.path.normpath(root / dep.replace('$$', '$'))) for dep in deps}
    # Rewrite only line-marker filenames, never __FILE__ or expanded C strings.
    expanded = re.sub(rb'^(# \d+ ")([^"]+)(".*)$',
                      lambda m: m[1] + os.fsencode(_name(os.fsdecode(m[2]), root)) + m[3],
                      r.stdout, flags=re.M)
    inputs = [FORMAT, tool, RESULT_VERSION, _name(root / src, root),
              hashlib.sha256((root / src).read_bytes()).hexdigest(),
              _arguments(argv, root), _arguments(args, root),
              os.environ.get('OS32_CLANG_CROSS_FREE', ''),
              _compiler_identity(), _checker_source_digest(tool), variant]
    # Raw dependency contents catch comments and unexpanded macro changes too.
    inputs.append([(_name(p, root), hashlib.sha256(p.read_bytes()).hexdigest())
                   for p in sorted(paths, key=lambda p: _name(p, root))])
    inputs.append(hashlib.sha256(expanded).hexdigest())
    return _digest(inputs)


def _directory():
    override = os.environ.get('OS32_CHECK_CACHE')
    if override == 'off' or (override is None and os.environ.get('CI')):
        return None
    if override is not None:
        return pathlib.Path(override)
    return pathlib.Path(os.environ.get('XDG_CACHE_HOME', pathlib.Path.home() / '.cache')) / 'os32/checks'


def _read(path, tool, key):
    try:
        entry = json.loads(path.read_bytes())
        if (not isinstance(entry, dict) or type(entry.get('format')) is not int
                or entry['format'] != FORMAT or entry.get('tool') != tool
                or entry.get('key') != key or 'result' not in entry
                or not isinstance(entry['result'], (list, dict))
                or not isinstance(entry.get('digest'), str)
                or entry['digest'] != _digest(entry['result'])):
            return None
        return entry['result']
    except (OSError, ValueError, TypeError):
        return None


def _write(path, tool, key, result):
    global _WRITE_FAILED
    if _WRITE_FAILED:
        return
    temporary = None
    try:
        data = _json(dict(format=FORMAT, tool=tool, key=key, result=result,
                          digest=_digest(result)))
        path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=path.parent, suffix='.tmp', delete=False) as f:
            temporary = pathlib.Path(f.name)
            f.write(data)
        os.replace(temporary, path)
    except OSError:
        _WRITE_FAILED = True
    finally:
        if temporary is not None:
            try:
                temporary.unlink(missing_ok=True)
            except OSError:
                pass


def get_or_compute(tool, src, argv, root, compute, variant=None):
    """Return a JSON list/dict, or propagate compute's exception without saving.

    p2v may store findings and its function/scanned/include sets as JSON arrays
    within a dict. Rule aggregation and exception policy remain with the caller.
    variant is JSON for caller decisions made outside the unit's bytes (c-dialect's vendor
    exemption reads the spelling of src), so a hit never crosses them.
    """
    directory = _directory()
    if directory is None:
        return compute()
    try:
        key = _internal_cache_key(tool, src, argv, root, variant)
    except (OSError, ValueError, KeyError, ast.ParseError, subprocess.SubprocessError,
            ctypes.ArgumentError, AttributeError):
        key = None
    if key is None:
        return compute()
    path = directory / 'v1' / tool / key[:2] / (key + '.json')
    result = _read(path, tool, key)
    if result is not None:
        return result
    result = compute()
    _write(path, tool, key, result)
    return result
