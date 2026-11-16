#!/usr/bin/env python3
"""A4 runtime contract, no libc; PPC user-mode is optional on CI hosts."""
CHECK = dict(name='io-policy-host', tier=1, budget=20, needs=[], covers=[
    'kernel/io_policy.c', 'kernel/io_policy.h', 'tools/tests/io_policy_host.c',
    'tools/tests/test_io_policy.py', 'tools/tests/host32.py', 'include/types.h',
    'include/callconv.h', 'sdk/include/os32/os32_kapi_shared.h', 'include/app_ids.h'],
    basis=['IO_TIER_DESIGN §4', '§8', 'AC-IO-2'],
    env_limit='PPC byte-order runtime needs powerpc-elf-gcc and qemu-ppc user mode')

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import host32

# The local io.h replaces privileged instructions, not policy declarations.
IRQ_HEADER = 'unsigned int irq_save(void);\nvoid irq_restore(unsigned int state);\n'
MUTATIONS = [
    ('remove-splice', 'saved[successor].table = saved[owner].table; /* POLICY_SPLICE */',
     '(void)successor; /* no splice */'),
    ('commit-before-validation', 'defaults(&next); /* POLICY_BEFORE_VALIDATION */',
     'defaults(&next); (void)commit(&next, owner, gen);'),
    ('native-endian', 'value |= (u32)p[i] << (8 * i); /* POLICY_LE_READ */',
     'value = n == 2 ? *(const u16 *)p : *(const u32 *)p;'),
    ('remove-restore', 'active = next; /* POLICY_RESTORE */',
     '(void)next; /* no restoration */'),
    ('overwrite-saved-source',
     'if (owner && (!saved[owner].valid || saved[owner].gen != gen)) { /* POLICY_SAVE_ONCE */',
     'if (owner) {'),
    ('ignore-busy', 'if (io_dispatch_running()) { /* POLICY_BUSY */',
     'if (0 && io_dispatch_running()) {'),
    ('partial-commit', 'next.row[i].budget = budget; /* POLICY_ROW_VALIDATED */',
     'next.row[i].budget = budget; (void)commit(&next, owner, gen);'),
]


def exercise(root, work, source, ppc=False):
    (work / 'io_policy.c').write_text(source)
    (work / 'io.h').write_text(IRQ_HEADER)
    exe = work / ('host-ppc' if ppc else 'host-x86')
    compiler = (Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) /
                'bin/powerpc-elf-gcc') if ppc else 'gcc'
    machine = ['-mcpu=7450', '-mbig-endian', '-msoft-float', '-mno-altivec',
               '-msdata=none', '-mcall-sysv', '-mno-eabi'] if ppc else ['-no-pie']
    arch_include = root / 'arch/ppc' if ppc else root / 'arch/x86'
    subprocess.run([str(compiler), '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra',
        '-Werror', '-Werror=vla', '-Werror=implicit-int',
        '-Werror=implicit-function-declaration', '-ffreestanding', '-fno-pie',
        '-fno-stack-protector', '-nostdlib', '-static', *machine,
        '-I' + str(work), '-I' + str(root / 'include'),
        '-I' + str(root / 'kernel'), '-I' + str(root / 'sdk/include/os32'),
        '-I' + str(root / 'exec'), '-I' + str(root / 'mm'), '-I' + str(arch_include),
        str(root / 'tools/tests/io_policy_host.c'), str(work / 'io_policy.c'),
        '-o', str(exe)], check=True)
    args = ['qemu-ppc', str(exe)] if ppc else [str(exe)]
    return host32.run(args, timeout=30).returncode


def run(ctx):
    source = (ctx.root / 'kernel/io_policy.c').read_text()
    ppc_cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
    architectures = [False]
    if ppc_cc.is_file() and shutil.which('qemu-ppc'):
        architectures.append(True)
    else:
        print('SKIP PPC runtime: powerpc-elf-gcc / qemu-ppc unavailable', flush=True)
    with tempfile.TemporaryDirectory(prefix='io-policy-') as td:
        work = Path(td)
        for ppc in architectures:
            rc = exercise(ctx.root, work, source, ppc)
            assert rc == 0, ('control', 'ppc' if ppc else 'x86', rc)
            print('PASS policy bytes/owners/validation ' + ('ppc BE' if ppc else
                  'x86 LE runner=' + host32.selected_runner()), flush=True)
        if ctx.mutate:
            for name, old, new in MUTATIONS:
                assert source.count(old) == 1, name
                if name == 'native-endian' and True not in architectures:
                    print('SKIP native-endian mutation: needs PPC BE runtime', flush=True)
                    continue
                results = [exercise(ctx.root, work, source.replace(old, new, 1), ppc)
                           for ppc in architectures]
                assert all(rc >= 0 for rc in results), (name, results)
                assert any(rc > 0 for rc in results), (name, results)
                print('RED runtime: ' + name + ' rc=' + str(results), flush=True)
    return 0


if __name__ == '__main__':
    from types import SimpleNamespace
    sys.exit(run(SimpleNamespace(root=Path(__file__).resolve().parents[2],
        mutate='--mutate' in sys.argv or os.environ.get('MUTATE') == '1')))
