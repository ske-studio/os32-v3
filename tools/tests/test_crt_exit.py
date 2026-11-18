#!/usr/bin/env python3
"""Execute real OS32 startup with and without newlib under Linux user mode."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'sdk/allocator'))

SYSCALLS = ROOT / 'sdk/crt/syscalls.c'
FIXTURE = ROOT / 'tools/tests/crt_exit_host.c'
HELP_MODES = ('return', 'help-success', 'help-failure')
MUTATIONS = (
    ('syscalls', 'void __os32_libc_exit(int rc)', 'void renamed_libc_exit(int rc)'),
    ('syscalls', '{ exit(rc); }', '{ extern void _exit(int); _exit(rc); }'),
    ('start', '        rc = 0;', '        rc = 0, kapi->sys_exit(rc);'),
    ('start', ' __attribute__((weak))', ''),
)


def check_exit(work, start, compile_c, link, run, mutate):
    sources = {'start': start, 'syscalls': SYSCALLS}

    def build(case, libc=True, mutation=0):
        inputs = sources.copy()
        if mutation:
            key, anchor, replacement = MUTATIONS[mutation - 1]
            source = inputs[key].read_text()
            assert source.count(anchor) == 1, (key, anchor)
            inputs[key] = work / inputs[key].relative_to(ROOT)
            inputs[key].parent.mkdir(parents=True, exist_ok=True)
            if key == 'syscalls':
                for directory in ('sdk/allocator', 'userland'):
                    copy = work / directory
                    if not copy.exists():
                        copy.symlink_to(ROOT / directory, target_is_directory=True)
            inputs[key].write_text(source.replace(anchor, replacement))
        objects = []
        for key in ('start', 'syscalls') if libc else ('start',):
            obj = work / (key + '.o')
            compile_c(inputs[key], obj, [])
            objects.append(obj)
        flags = [] if libc else ['-DCRT_ONLY']
        if case != 'return':
            flags.append('-D' + case.upper().replace('-', '_'))
        fixture = work / 'fixture.o'
        compile_c(FIXTURE, fixture, flags)
        elf = work / 'exit.elf'
        return elf, link(elf, [fixture, *objects], libc)

    def execute(case, expected, libc=True, mutation=0):
        elf, link_result = build(case, libc, mutation)
        assert link_result.returncode == 0, link_result.stderr.decode()
        result = run(elf)
        assert (result.returncode, result.stdout, result.stderr) == (*expected, b''), result
        if libc:
            symbols = (work / 'exit.map').read_text().splitlines()
            bridge = any(line.split()[-1:] == ['__os32_libc_exit'] for line in symbols)
            assert bridge == (mutation != 1), 'libc exit bridge in link map'
            if mutation in (0, 3):
                assert 'libc_a-exit.o)' in '\n'.join(symbols), 'newlib exit in link map'

    for case in HELP_MODES:
        status = 0 if case == 'help-success' else 37
        execute(case, (status, b'ACB'))
        print(f'PASS {case}: ACB, status {status}', flush=True)
    execute('return', (37, b''), libc=False)
    print('PASS CRT-only: status 37', flush=True)
    if mutate:
        execute('return', (37, b''), mutation=1)
        print('RED 1 runtime: renamed bridge loses ACB')
        execute('return', (37, b''), mutation=2)
        print('RED 2 runtime: immediate exit loses ACB')
        execute('help-success', (0, b''), mutation=3)
        print('RED 3 runtime: help bypass loses ACB')
        _, result = build('return', libc=False, mutation=4)
        assert result.returncode and b'undefined reference' in result.stderr and b'__os32_libc_exit' in result.stderr, result
        print('RED 4 link: required bridge breaks CRT-only link')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=['native', 'qemu'], default=host32.selected_runner())
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))
    gcc = str(cross / 'bin/i386-elf-gcc')

    def compile_c(source, obj, flags):
        subprocess.run([gcc, '-std=gnu11', '-O2', '-ffreestanding', '-fno-pie',
                        '-ffunction-sections', '-fdata-sections', *flags,
                        '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/include/os32'),
                        '-c', str(source), '-o', str(obj)], check=True)

    from build_nano import build

    def link(elf, objects, libc):
        libs = [archive, '-lc', '-lgcc'] if libc else []
        return subprocess.run([gcc, '-nostdlib', '-static', '-Wl,--gc-sections,-Map=' + str(elf.with_suffix('.map')),
                               *objects, *libs,
                               '-o', elf], capture_output=True)

    with tempfile.TemporaryDirectory(prefix='crt-exit-host-') as directory:
        work = Path(directory)
        archive = build(cross, work / 'nano', crt=SYSCALLS)
        check_exit(work, ROOT / 'sdk/crt/crt0_c.c', compile_c, link,
                   lambda elf: host32.run([str(elf)], runner=args.runner,
                                          capture_output=True, timeout=60),
                   args.mutate)


if __name__ == '__main__':
    main()
