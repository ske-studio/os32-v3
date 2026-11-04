"""Reject libgcc inputs in the maps of the OS core and its paired modules."""
CHECK = dict(name='no-libgcc', tier=1,
    covers=['build/*.mk', 'Makefile', 'build/out/**'],
    # all owns the normal kernel and boot media. Requesting kernel again
    # after all would FORCE-relink it and stale those freshly built media.
    needs=['make:all', 'make:kernel-r1', 'make:boot-debug'], budget=30.0)

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
# Every distributed .mod has a required map, even when R1 shares its ELF.
LINKS = [
    ('build/kernel.mk', 'build/out/kernel.elf', 'build/out/kernel.map'),
    ('build/kernel.mk', 'build/out/r1/kernel.elf', 'build/out/r1/kernel.map'),
    ('build/boot.mk', 'boot/loader_hdd.elf', 'boot/loader_hdd.map'),
    ('build/boot.mk', 'boot/loader_hdd_debug.elf', 'boot/loader_hdd_debug.map'),
    ('build/kdebug.mk', 'build/out/kdebug/kdebug.elf', 'build/out/kdebug/kdebug.map'),
    ('build/hwdev.mk', 'build/out/hwdev/hwdev.elf', 'build/out/hwdev/hwdev.map'),
    ('build/hwdev.mk', 'build/out/r1/hwdev/hwdev.elf', 'build/out/r1/hwdev/hwdev.map'),
    ('build/sqlite-proto.mk', 'build/out/sqlite-proto/sqlite.elf', 'build/out/sqlite-proto/sqlite.map'),
] + [(f'build/{name}-module.mk', f'build/out/{name}/{name}.elf', f'build/out/{name}/{name}.map')
     for name in ('fep', 'cd', 'kcg', 'fm', 'pcm', 'v86', 'lan', 'cirrus', 'geforce')]


def audit(paths):
    errors = []
    for path in paths:
        if not path.is_file():
            errors.append(f'{path}: missing map')
        elif re.search(r'libgcc(?:_eh)?\.a(?:\(|\b)', path.read_text()):
            # Also reject LOAD-only archives: restoring an unused -lgcc is
            # already a policy regression, before any new arithmetic uses it.
            errors.append(f'{path}: forbidden libgcc archive/member')
    return errors


def product_maps():
    out = ROOT / 'build/out'
    paths = {ROOT / row[2] for row in LINKS}
    # Include optional variants that have actually been built (e.g. GFA/VPLL).
    dirs = {Path(row[2]).parts[2] for row in LINKS if row[2].startswith('build/out/') and len(Path(row[2]).parts) > 3}
    dirs.update(('geforce-accept', 'vpll'))
    for directory in dirs:
        paths.update((out / directory).rglob('*.map'))
    return sorted(paths)


def mutation_checks():
    env = dict(os.environ)
    for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'GNUMAKEFLAGS'):
        env.pop(name, None)
    cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
    libdir = str(Path(subprocess.check_output([cc, '-print-libgcc-file-name'], text=True).strip()).parent)
    with tempfile.TemporaryDirectory(prefix='no-libgcc-') as td:
        work = Path(td)
        # Dry-run only: all actual relinks below write solely into work.
        result = subprocess.run(['make', '--no-print-directory', '-n', '-B', '-j1',
            *(row[1] for row in LINKS)], cwd=ROOT, env=env,
            text=True, capture_output=True, check=True)
        for source, elf, mapname in LINKS:
            output = elf
            commands = []
            for line in result.stdout.splitlines():
                if ' -o ' not in line:
                    continue
                args = shlex.split(line)
                if '-o' in args and args[args.index('-o') + 1] == output:
                    commands.append(args)
            assert len(commands) == 1, (output, len(commands))
            command = commands[0]
            dest = work / Path(elf).name
            destmap = dest.with_suffix('.map')
            command[command.index('-o') + 1] = str(dest)
            maps = [i for i, a in enumerate(command) if a.startswith('-Map=')]
            assert len(maps) == 1, (source, elf)
            command[maps[0]] = '-Map=' + str(destmap)
            # A normal relink must pass the same runtime map audit.
            subprocess.run(command, cwd=ROOT, env=env, check=True, capture_output=True)
            assert not audit([destmap]), elf
            # Restore -lgcc alone: ld emits LOAD even if it needs no member.
            mutant = [*command, '-L' + libdir, '-lgcc']
            subprocess.run(mutant, cwd=ROOT, env=env, check=True, capture_output=True)
            assert audit([destmap]), elf
            print(f'RED {source} {elf}: restored -lgcc, map audit rejected LOAD', flush=True)
            # Demand an otherwise unused compiler helper to exercise actual
            # archive extraction too (not a fabricated map fixture).
            subprocess.run([*mutant, '-u', '__clzdi2'], cwd=ROOT, env=env,
                           check=True, capture_output=True)
            assert re.search(r'libgcc\.a\([^)]*\)', destmap.read_text()), elf
            assert audit([destmap]), elf
            print(f'RED {elf}: actual libgcc.a member rejected', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--map', type=Path, action='append')
    args = parser.parse_args()
    paths = args.map if args.map is not None else product_maps()
    errors = audit(paths)
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print(f'GREEN no libgcc archive/member in {len(paths)} maps', flush=True)
    if args.mutate:
        mutation_checks()
    return 0


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_no_libgcc.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
