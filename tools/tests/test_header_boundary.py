#!/usr/bin/env python3
"""Stage A: exercise the real preprocessor, assembler and header dependencies."""
CHECK = dict(name='header-boundary', tier=1,
    covers=['include/cpu.h', 'include/kernel_tick.h', 'include/types.h',
            'arch/x86/**', 'arch/ppc/arch_cpu.h', 'include/bootlog.h',
            'init/bootlog_save.c', 'exec/ring3_ls.S', 'exec/ring3_ls.h',
            'build/kernel-objects.mk', 'tools/tests/kernel_includes.py',
            'tools/tests/host_arch/arch_cpu.h', 'sdk/include/os32/**'],
    needs=[], budget=5.0)

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]


def command(argv, root=ROOT):
    return subprocess.run(argv, cwd=root, check=True, capture_output=True, text=True).stdout


def probe(directory, cpu):
    """A mock architecture must not inherit x86 flags from the contract."""
    (directory / 'cpu.h').write_text(cpu)
    (directory / 'arch_cpu.h').write_text('/* architecture without x86 flags */\n')
    source = directory / 'probe.c'
    source.write_text('#include "cpu.h"\n')
    flags = ['gcc', '-E', '-dM', '-I' + str(directory), *include_flags(ROOT)]
    macros = command([*flags, str(source)])
    deps = command(['gcc', '-M', '-I' + str(directory), *include_flags(ROOT), str(source)])
    return 'EFLAGS_IF' not in macros and 'x86_flags.h' not in deps


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cc = str(Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc')
    with tempfile.TemporaryDirectory(prefix='header-boundary-') as temp:
        directory = Path(temp)
        cpu = (ROOT / 'include/cpu.h').read_text()
        assert probe(directory, cpu), 'flags leaked into a non-x86 architecture'
        print('PASS mock architecture dependencies (normal control)', flush=True)
        for label in ('normal', 'r1'):
            out = directory / (label + '.o')
            command([cc, '-m32', '-Isdk/include/os32',
                     *(['-DOS32_R1_FIXTURE'] if label == 'r1' else []),
                     '-c', 'exec/ring3_ls.S', '-o', str(out)])
            dep = command([cc, '-m32', '-Isdk/include/os32', '-M', 'exec/ring3_ls.S'])
            assert 'x86_flags.h' not in dep, dep
            print('PASS ' + label + ' real ring3 ASM and dependencies', flush=True)
        dep = command([cc, *include_flags(ROOT), '-M', 'init/bootlog_save.c'])
        assert 'kernel_tick.h' in dep and 'idt.h' not in dep and 'gdt.h' not in dep, dep
        print('PASS bootlog tick dependencies', flush=True)
        if args.mutate:
            anchor = '#define CPU_H\n'
            assert cpu.count(anchor) == 1
            # Use the same original include spelled absolutely in the isolated copy.
            mutant = cpu.replace(anchor, anchor + '#include "' + str(ROOT / 'arch/x86/x86_flags.h') + '"\n')
            assert not probe(directory, mutant), 'flags re-entry mutation escaped'
            print('MUTATION flags re-entry RED (runtime dependency assertion)', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())


def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
