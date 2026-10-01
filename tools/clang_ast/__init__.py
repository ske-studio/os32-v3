"""Actual build flags and fail-closed libclang parsing for OS32 checks."""
import functools
import os
import pathlib
import shutil
import subprocess
import sys
from .build import dry_run, parse_compile_lines
try:
    from clang import cindex as cx
except ImportError:
    # setup-python on Ubuntu does not include apt's dist-packages.
    sys.path.append('/usr/lib/python3/dist-packages')
    from clang import cindex as cx

ROOT = pathlib.Path(__file__).resolve().parents[2]
K = cx.CursorKind
T = cx.TypeKind
SYSTEM_NEWLIB = pathlib.Path('/usr/include/newlib')
# Code-generation-only flags, and warnings GCC supports but clang does not.
DROP = {'-fno-toplevel-reorder', '-fno-reorder-functions', '-fno-reorder-blocks',
        '-fno-reorder-blocks-and-partition', '-fno-tree-loop-distribute-patterns',
        '-mpreferred-stack-boundary=2', '-mincoming-stack-boundary=2',
        '-Wno-stringop-truncation', '-Wno-format-truncation', '-Wno-array-bounds',
        '-Wno-maybe-uninitialized', '-Wno-clobbered',
        '-Wc90-c99-compat', '-Wc99-c11-compat',
        '-P', '-dM', '-dD', '-C', '-CC'}

class ParseError(RuntimeError):
    pass

@functools.lru_cache(maxsize=1)
def resource_dir():
    return subprocess.run(['clang','-print-resource-dir'],capture_output=True,text=True,
                          check=True).stdout.strip()


def flags(argv, root=ROOT, cc=None):
    root = pathlib.Path(root).resolve()
    out = ['--target=i386-unknown-none-elf','-Wgnu-folding-constant']
    for a in argv:
        if a in DROP:
            continue
        out.append(a)
    compiler = cc or os.environ.get('OS32_CC', 'i386-elf-gcc')
    cross = shutil.which(compiler)
    if not cross:
        cross = str(pathlib.Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc')
    inc = pathlib.Path(cross).resolve().parents[1] / 'i386-elf/include'
    resource = resource_dir()
    out += ['-isystem', resource + '/include', '-working-directory=' + str(root)]
    if not inc.is_dir() and SYSTEM_NEWLIB.is_dir():
        inc = SYSTEM_NEWLIB  # Ubuntu's libnewlib-dev for cross-free static CI.
    if inc.is_dir():
        out += ['-isystem', str(inc)]
    return out

def parse(src, argv=(), root=ROOT, text=None):
    src = str(pathlib.Path(root, src).absolute())
    args = flags(argv, root)
    if text is None and not pathlib.Path(src).is_file():
        raise ParseError("missing translation unit: " + src)
    try:
        tu = cx.Index.create().parse(src, args=args,
            unsaved_files=[(src, text)] if text is not None else None,
            options=cx.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
    except cx.TranslationUnitLoadError as e:
        raise ParseError(f'{src}: {e}') from e
    errors = [str(d) for d in tu.diagnostics if d.severity >= cx.Diagnostic.Error]
    if errors:
        raise ParseError('\n'.join(errors))
    return tu

def units(root=ROOT):
    rc, out, err = dry_run(root)
    if rc:
        raise ParseError('make dry-run: ' + err)
    us = parse_compile_lines(out)
    if not us:
        raise ParseError('no compile commands')
    result = []
    seen = set()
    for u in us:
        src = u['src']
        # dry_run's BUILD_OUT is temporary; generated input must exist in real output.
        if not pathlib.Path(root, src).is_file() and src.endswith('/build_id.c'):
            src = 'build/out/build_id.c'
        argv = [a for a in u['argv'] if a != u['src']]
        key = (src, tuple(argv))
        if key not in seen:
            result.append(dict(u, src=src, argv=argv))
            seen.add(key)
    return result

def walk(cursor, function='<file>'):
    if cursor.kind == K.FUNCTION_DECL and cursor.is_definition():
        function = cursor.spelling
    yield cursor, function
    for child in cursor.get_children():
        yield from walk(child, function)

def relpath(cursor, root=ROOT):
    if not cursor.location.file:
        return None
    p = pathlib.Path(os.path.abspath(cursor.location.file.name))
    try:
        return p.relative_to(pathlib.Path(root).absolute()).as_posix()
    except ValueError:
        return None

def pointer(typ):
    return typ.get_canonical().kind == T.POINTER

def integer(typ):
    return typ.get_canonical().kind in (T.BOOL,T.CHAR_U,T.UCHAR,T.USHORT,T.UINT,T.ULONG,
        T.ULONGLONG,T.CHAR_S,T.SCHAR,T.SHORT,T.INT,T.LONG,T.LONGLONG,T.ENUM)

def alignment_cast(cursor):
    if cursor.kind != K.CSTYLE_CAST_EXPR or not pointer(cursor.type):
        return False
    children = list(cursor.get_children())
    if not children or not pointer(children[-1].type):
        return False
    a = children[-1].type.get_pointee().get_canonical().get_align()
    b = cursor.type.get_pointee().get_canonical().get_align()
    return a > 0 and b > a
