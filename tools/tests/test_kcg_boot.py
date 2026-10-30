#!/usr/bin/env python3
"""T7a-1b: real legacy KAPI stubs, no low writes/file I/O; runtime mutants."""
import argparse
from pathlib import Path
import subprocess
import shutil
import tempfile
from mutpar import run_ordered
import host32

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('init-keeps-scale', 'drivers/kcg.c',
     '    kcg_scale = 1;', '', 'FAIL init resets scale'),
    ('old-font-fallback', 'drivers/kcg.c',
     '    return OS32_ERR_NOSYS;',
     '    return vfs_open(path, 0);', 'FAIL legacy VFS untouched'),
    ('old-low-cache', 'drivers/kcg.c',
     '    kcg_rom_read_ank(ch, buf);',
     '    (void)ch; buf[0] = *(u8 *)P2V(0x01000UL);', 'FAIL ROM ANK bytes'),
)



def run(runner, mutation=None):
    sources = {p: (ROOT / p).read_text() for p in ('drivers/kcg.c',)}
    if mutation:
        name, path, old, new, expected = mutation
        assert sources[path].count(old) == 1, name + ': mutation target'
        sources[path] = sources[path].replace(old, new, 1)
    with tempfile.TemporaryDirectory(prefix='e8b-kcg-') as directory:
        work = Path(directory)
        shutil.copyfile(ROOT / 'include/kcg_contract.h', work / 'kcg_contract.h')
        source = sources['drivers/kcg.c'].replace('"../fs/vfs.h"', '"vfs.h"').replace(
            '"../lib/lz4.h"', '"lz4.h"')
        (work / 'kcg_source.inc').write_text(source)
        cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O0', '-Wall', '-Wextra',
               '-Werror', '-Wno-unused-variable', '-Werror=vla', '-ffreestanding', '-fno-builtin', '-fno-pie',
               '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
               '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
               '-D__KERNEL_BUILD__=1']
        cmd += ['-I'+str(work)]
        cmd += ['-I'+str(ROOT/p) for p in ('tools/tests/host_arch', 'include',
                'arch/x86', 'platform/pc98', 'kernel', 'drivers', 'lib', 'fs', 'sdk/include/os32')]
        exe = work / 'fixture'
        result = host32.build(cmd + [str(ROOT/'tools/tests/kcg_boot_host.c'),
                '-o', str(exe)], capture_output=True, text=True)
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
    source = (ROOT/'init/kernel.c').read_text()
    assert 'boot_font_load' not in source and 'kcg_font_loaded' not in source
    assert 'kcg_boot_phase_close' not in source
    assert source.index('cd_module_load();') < source.index('kcg_module_load();')
    assert 'is_gui && !kcg_module_ready()' in source
    assert source.count('unicode_file_load();') == 1
    print('PASS POST_ROOT KCG, CUI gate, no legacy boot load/closure')



@host32.control_session
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=('native', 'qemu'))
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    host32.begin_control(args.mutate, args.runner, ROOT)
    boot_order()
    with host32.control(args.mutate, args.runner, ROOT) as normal:
        if normal:
            run(args.runner)
    if args.mutate:
        for _ in run_ordered(lambda mutation: run(args.runner, mutation), MUTATIONS):
            pass
        print(f'PASS {len(MUTATIONS)}/{len(MUTATIONS)} runtime mutations')


if __name__ == '__main__':
    main()
