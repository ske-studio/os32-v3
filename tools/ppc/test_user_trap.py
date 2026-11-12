#!/usr/bin/env python3
"""Exercise USER trap dispatch and its SC mutation."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ppc-user-trap', tier=2, budget=120,
             covers=['tools/ppc/test_user_trap*'],
             inputs=['arch/ppc/kernel_trap.c', 'arch/ppc/pd_walk.c',
                     'arch/ppc/ppc_*.h', 'include/*.h'])

def check(source, expected):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / 'trap.c').write_text(source)
        subprocess.run(['gcc', '-m32', '-ffreestanding', '-fno-pie', '-no-pie',
                        '-nostdlib', '-static', '-std=gnu11', '-O2', '-Wall',
                        '-Wextra', '-Werror', '-DPPC_TRAP_HOST_TEST',
                        '-I' + str(ROOT / 'arch/ppc'), '-I' + str(ROOT / 'include'),
                        '-I' + str(ROOT), '-I' + str(ROOT / 'platform/powermac'),
                        str(path / 'trap.c'),
                        str(ROOT / 'arch/ppc/pd_walk.c'),
                        str(ROOT / 'tools/ppc/test_user_trap_host.c'),
                        '-o', str(path / 'host')], check=True)
        result = subprocess.run(['qemu-i386', str(path / 'host')], capture_output=True, check=False)
        assert result.returncode == expected, (result.returncode, expected)
        if expected:
            assert result.stdout == b'SC-RED\n'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'arch/ppc/kernel_trap.c').read_text()
    check(source, 0)
    if args.mutate:
        old = '''        if (f->vector == 0xc00) {
            struct arch_syscall call = { f };
            exec_syscall_dispatch(&call);
            return;
        }'''
        assert source.count(old) == 1
        check(source.replace(old, '        if (f->vector == 0xc00) { reason = PPC_TRAP_SC | PPC_TRAP_UNSUPPORTED; goto fatal; }'), 10)
        print('SC unsupported: runtime RED (1)')
    print('USER trap PASS')

if __name__ == '__main__':
    main()
