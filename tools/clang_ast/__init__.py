"""Actual build flags and fail-closed libclang parsing for OS32 checks."""
import ctypes
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


@functools.lru_cache(maxsize=128)
def gcc_predefines(compiler, argv, root):
    # Remove source/include/user macros: collect only compiler definitions, then
    # apply the original -D/-U/-include arguments after them in flags().
    options = []
    it = iter(argv)
    for arg in it:
        if arg in ('-I', '-isystem', '-iquote', '-idirafter', '-include', '-imacros', '-D', '-U'):
            next(it, None)
        elif not arg.startswith(('-I', '-D', '-U')) and arg not in DROP:
            options.append(arg)
    r = subprocess.run([compiler, *options, '-dM', '-E', '-x', 'c', '-'],
                       input='', capture_output=True, text=True, cwd=root)
    if r.returncode:
        raise ParseError('GCC predefines failed: ' + r.stderr)
    definitions = []
    for line in r.stdout.splitlines():
        if line.startswith('#define '):
            name, _, value = line[8:].partition(' ')
            definitions.append('-D' + name + '=' + value)
    if not definitions:
        raise ParseError('GCC predefines: empty result')
    return definitions

@functools.lru_cache(maxsize=4)
def gcc_include(compiler):
    r = subprocess.run([compiler, '-print-file-name=include'],
                       capture_output=True, text=True, check=True)
    include = pathlib.Path(r.stdout.strip())
    if not include.is_dir():
        raise ParseError('missing GCC builtin headers: ' + str(include))
    return str(include)


@functools.lru_cache(maxsize=1)
def cross_free_notice():
    print('clang AST: LIMITED cross-free mode; GCC predefined macros/branches '
          'are NOT verified (clang defaults, system newlib).', file=sys.stderr)


def cross_compiler(cc=None):
    compiler = cc or os.environ.get('OS32_CC', 'i386-elf-gcc')
    cross = shutil.which(compiler)
    if not cross:
        cross = str(pathlib.Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc')
    return cross if os.access(cross, os.X_OK) and pathlib.Path(cross).is_file() else None


@functools.lru_cache(maxsize=4)
def cross_notice(cross, inc):
    print(f'clang AST: FULL cross mode; GCC={cross}; newlib={inc}', file=sys.stderr)


def flags(argv, root=ROOT, cc=None):
    root = pathlib.Path(root).resolve()
    out = ['--target=i386-unknown-none-elf','-Wgnu-folding-constant']
    cross = cross_compiler(cc)
    if cross:
        inc = pathlib.Path(cross).resolve().parents[1] / 'i386-elf/include'
        if not (inc / 'stdio.h').is_file():
            raise ParseError('missing cross newlib headers: ' + str(inc))
        cross_notice(cross, str(inc))
        # Clang's generic ELF target rejects GCC i386's __float128 (stddef.h).
        # The Linux i386 frontend accepts it; -undef + GCC definitions select
        # bare-metal branches and -nostdinc prevents all host libc headers.
        out[0] = '--target=i386-unknown-linux-gnu'
        out += ['-nostdinc']
        out += ['-undef', '-Wno-builtin-macro-redefined',
                '-U__has_feature', '-U__has_extension', '-U__has_warning',
                '-U__is_identifier', '-U__building_module', '-U__has_declspec_attribute',
                '-U__has_constexpr_builtin', '-U__has_embed']
        out += gcc_predefines(cross, tuple(argv), str(root))
        out += ['-isystem', gcc_include(cross)]
    elif os.environ.get('OS32_CLANG_CROSS_FREE') == '1':
        inc = SYSTEM_NEWLIB
        cross_free_notice()
    else:
        raise ParseError('cross GCC unavailable; CROSS_DIR=' + os.environ.get('CROSS_DIR', '<default>') +
                         '; use OS32_CLANG_CROSS_FREE=1 only for limited static CI')
    out += [a for a in argv if a not in DROP]
    resource = resource_dir()
    out += ['-isystem', resource + '/include', '-working-directory=' + str(root)]
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
    tu.os32_parse_input = (src, args, text if text is not None else pathlib.Path(src).read_text())
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


def spelling_position(location):
    """Physical macro-definition position, rather than expansion call position."""
    file = ctypes.c_void_p()
    line, column, offset = ctypes.c_uint(), ctypes.c_uint(), ctypes.c_uint()
    cx.conf.lib.clang_getSpellingLocation(location, ctypes.byref(file),
        ctypes.byref(line), ctypes.byref(column), ctypes.byref(offset))
    return (cx.File(ctypes.cast(file, cx.c_object_p)).name if file.value else None, offset.value)


def first_token_location(cursor):
    """Get the single spelling token even when LLVM 18 cannot tokenize a range.

    Nested macros in different files may give a range with incompatible ends;
    clang_getToken still resolves its starting macro location correctly.
    """
    get = cx.conf.lib.clang_getToken
    get.argtypes = [cx.TranslationUnit, cx.SourceLocation]
    get.restype = ctypes.POINTER(cx.Token)
    token = get(cursor._tu, cursor.extent.start)
    if not token:
        return None
    try:
        return cx.conf.lib.clang_getTokenLocation(cursor._tu, token.contents)
    finally:
        cx.conf.lib.clang_disposeTokens(cursor._tu, token, 1)
