"""Shared HW-BASIC libc-free ILP32 build/runtime/mutation helpers.

Fixtures #include real sources; use hwb_fake.h's always-active CHECK and _start.
MUTATIONS: (name, product_file, unique_old, new, failing_test_name).
No compile failure/signal/timeout qualifies as a killed mutant: CHECK exits 1
and prints FAIL:<failing_test_name>:. Each mutation gets an isolated dependency
copy; the unmodified copy must execute GREEN through exactly the same path.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import host32
from kernel_includes import include_flags
import mutpar

ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-m32', '-march=i386',
         '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector', '-O2']


def cross_gcc():
    cross = os.environ.get('CROSS_DIR')
    candidate = Path(cross) / 'bin/i386-elf-gcc' if cross else None
    compiler = str(candidate) if candidate and candidate.is_file() else shutil.which('i386-elf-gcc')
    if not compiler:
        raise RuntimeError('i386-elf-gcc is required (set CROSS_DIR)')
    return compiler


def build_command(root, fixture, output, *, cross=False, sources=(), flags=()):
    """sources/fixture are relative to root; flags allow local boundary overrides.

    Cross mode compiles the same fixture TU without linking Linux _start.
    Cross build_command accepts one TU; verify also compiles every extra TU
    separately. Host mode links extra TUs. No libc or libgcc is linked.
    """
    root, output = Path(root), Path(output)
    return [cross_gcc() if cross else 'gcc', *FLAGS, *flags,
            '-I' + str(root), '-I' + str(root / 'tools/tests'), *include_flags(root),
            *(['-c'] if cross else ['-nostdlib', '-static', '-no-pie', '-Wl,-e,_start']),
            str(root / fixture), *[str(root / p) for p in sources], '-o', str(output)]


def build(root, fixture, output, *, cross=False, sources=(), flags=()):
    result = host32.build(build_command(root, fixture, output, cross=cross,
                                       sources=sources, flags=flags),
                          cwd=root, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError('compile failure is NOT runtime RED:\n' + result.stderr)


def execute(exe, *, root=ROOT, runner=None):
    """Return result; explicit host32 runner, bounded runtime, no silent fallback."""
    return host32.run([str(exe)], cwd=root, runner=runner,
                      capture_output=True, text=True, timeout=60)


def verify(fixture, *, root=ROOT, sources=(), flags=()):
    """Run normal control, then compile same fixture with target compiler."""
    with tempfile.TemporaryDirectory(prefix='hwb-host-') as directory:
        tmp = Path(directory)
        build(root, fixture, tmp / 'host', sources=sources, flags=flags)
        result = execute(tmp / 'host', root=root)
        print(result.stdout, end='', flush=True)
        if result.returncode != 0:
            raise RuntimeError(f'normal control failed ({result.returncode}): {result.stderr}')
        for i, source in enumerate((fixture, *sources)):
            build(root, source, tmp / f'target-{i}.o', cross=True, flags=flags)
        print('GREEN host ILP32, libc-free; i386-elf compile PASS', flush=True)


def mutations(fixture, items, *, root=ROOT, sources=(), flags=()):
    """Validate five-tuples/unique anchors; isolated control plus runtime mutants."""
    items = list(items)
    names = set()
    for item in items:
        if len(item) != 5:
            raise ValueError('MUTATIONS entries must be five-tuples')
        name, path, old, new, test = item
        if name in names or not old or old == new or not test:
            raise ValueError(f'invalid mutation: {name}')
        names.add(name)
        if (Path(root) / path).read_text().count(old) != 1:
            raise ValueError(f'mutation anchor must be unique: {name} ({path})')

    def one(item):
        name, path, old, new, test = item
        with tempfile.TemporaryDirectory(prefix='hwb-mut-') as directory:
            tmp = Path(directory)
            cmd = build_command(root, fixture, tmp / 'host', sources=sources, flags=flags)
            edits = {} if path is None else {path: (Path(root) / path).read_text().replace(old, new)}
            tree = mutpar.mutant_tree(root, tmp / 'tree', edits, gcc_cmds=[cmd])
            build(tree, fixture, tmp / 'host', sources=sources, flags=flags)
            result = execute(tmp / 'host', root=tree)
            if result.returncode == 0:
                return f'{name}: GREEN', 1
            marker = f'FAIL:{test}:'
            if path is not None and result.returncode == 1 and marker in result.stdout:
                return f'{name}: RED runtime ({test})', 0
            raise RuntimeError(f'{name}: unexpected failure, NOT a killed mutant: '
                               f'{result.returncode}\n{result.stdout}\n{result.stderr}')

    return mutpar.run_with_control(one, items, ('control', None, '', '', ''))
