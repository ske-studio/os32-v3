#!/usr/bin/env python3
"""T2e e8b real kcg/utf8 + C LZ4, VFS stub. --mutate uses copies only.
Record: tools/tests/kcg_boot_tdd.md. Kernel uses Rust LZ4; decoder parity is
covered elsewhere. This suite verifies font loader boundaries and closure.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('guard', 'drivers/kcg.c',
     'if (!kcg_boot_phase_open) return OS32_ERR_NOSYS;', '',
     'FAIL closed VFS untouched'),
    ('closed-open', 'drivers/kcg.c',
     'if (!kcg_boot_phase_open) return OS32_ERR_NOSYS;',
     'if (!kcg_boot_phase_open) { vfs_open(path, 0); return OS32_ERR_NOSYS; }',
     'FAIL closed VFS untouched'),
    ('BB-clear', 'drivers/kcg.c',
     'kmemset(P2V(MEM_GFX_BB_BASE), 0, MEM_GFX_BB_SIZE);', '',
     'FAIL mailbox follows BB'),
    ('mailbox-clear', 'drivers/kcg.c',
     'kmemset(P2V(MEM_AUTOPLAY_MAILBOX_BASE), 0, MEM_AUTOPLAY_MAILBOX_SIZE);', '',
     'FAIL close completes order'),
    ('four-point', 'lib/utf8.c',
     'return jis_table_probe();', 'return 1;',
     'FAIL invalid table ready zero'),
)


def run(runner, mutation=None):
    sources = {p: (ROOT / p).read_text() for p in ('drivers/kcg.c', 'lib/utf8.c')}
    if mutation:
        name, path, old, new, expected = mutation
        assert sources[path].count(old) == 1, name + ': mutation target'
        sources[path] = sources[path].replace(old, new, 1)
    with tempfile.TemporaryDirectory(prefix='e8b-kcg-') as directory:
        work = Path(directory)
        source = sources['drivers/kcg.c'].replace('"../fs/vfs.h"', '"vfs.h"').replace(
            '"../lib/lz4.h"', '"lz4.h"').replace(
            'if (!utf8_validate_jis_table())', 'if (!host_validate_jis_table())')
        (work / 'kcg_source.inc').write_text(source)
        (work / 'utf8_source.inc').write_text(sources['lib/utf8.c'])
        cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O0', '-Wall', '-Wextra',
               '-Werror', '-Wno-unused-variable', '-Werror=vla', '-ffreestanding', '-fno-builtin', '-fno-pie',
               '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
               '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
               '-D__KERNEL_BUILD__=1']
        cmd += ['-I'+str(work)]
        cmd += ['-I'+str(ROOT/p) for p in ('tools/tests/host_arch', 'include',
                'arch/x86', 'platform/pc98', 'drivers', 'lib', 'fs', 'sdk/include/os32')]
        exe = work / 'fixture'
        result = subprocess.run(cmd + [str(ROOT/'tools/tests/kcg_boot_host.c'),
                str(ROOT/'lib/lz4.c'), '-o', str(exe)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError('build failure is not runtime RED\n'+result.stdout+result.stderr)
        result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
        if mutation:
            assert result.returncode == 1 and expected in result.stdout, (
                mutation[0], result.returncode, result.stdout, result.stderr)
            print('RED', mutation[0], expected)
        else:
            assert result.returncode == 0 and 'PASS kcg boot' in result.stdout, (
                result.returncode, result.stdout, result.stderr)
            print(result.stdout.strip())


def boot_order():
    source = (ROOT/'kernel/kernel.c').read_text()
    ordered = ['boot_font_load();', 'utf8_validate_jis_table()', 'shlib_init();',
               'bootlog_save();', 'kcg_boot_phase_close();', 'exec_run(cur_shell']
    positions = [source.index(s) for s in ordered]
    assert positions == sorted(positions), 'boot closure before normal AS'
    assert source.count('kcg_boot_phase_close();') == 1, 'one closure'
    assert 'bytes == (int)MEM_UNICODE_TABLE_SIZE &&\n            utf8_validate_jis_table()' in source
    print('PASS boot order and complete Unicode read gate')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=('native', 'qemu'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    boot_order()
    run(args.runner)
    if args.mutate:
        for mutation in MUTATIONS:
            run(args.runner, mutation)
        print('PASS 5/5 runtime mutations')


if __name__ == '__main__':
    main()
