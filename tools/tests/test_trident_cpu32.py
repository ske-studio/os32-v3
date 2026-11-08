#!/usr/bin/env python3
"""Run real CLIENT transfer comparisons with and without GE callbacks."""
CHECK = dict(name='trident-cpu32', tier=1, needs=[], budget=10,
    covers=['kdebug/trident_command_test.c', 'kdebug/trident_command_test.h',
            'kdebug/tri_command_wire.h', 'kdebug/generated/**',
            'tools/tests/trident_cpu32_host.c', 'tools/tests/test_trident_cpu32.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
            'drivers/video/trident/*.h', 'include/*.h', 'gfx/*.h',
            'lib/serial_diag.h', 'drivers/char/serial.h', 'kernel/module/*.h',
            'sdk/include/os32/*.h', 'build/config.mk'])
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'kdebug/trident_command_test.c'
MUTATIONS = [
    ('present-count', 'run < TRI_PRESENT_RUNS', 'run < TRI_PRESENT_RUNS - 1u'),
    ('present-restore', 'd->present_method = saved;', 'd->present_method = TRI_PRESENT_BYTE; (void)saved;'),
    ('present-readback', 'if (actual != d->res->client[row * c->pitch + i]) ++mismatches;', 'if (actual == d->res->client[row * c->pitch + i]) ++mismatches;'),
    ('skip-no-ge', '    write_row(d, count, false, TRI_TEST_STRIDE);', '    return TRI_E_NOSYS;'),
    ('skip-stores', '"cld; rep movsl"', '"cld"'),
    ('skip-second-sync', 'if (test == 1u) {', 'if (test == 4u) {'),
    ('skip-scalar', 'p[i] = cpu_row[i];', 'p[i] = 0;'),
    ('skip-comparison', 'test < 3u', 'test < 2u'),
    ('wrong-mask', 'saved & TRI_TEST_NO_PCI_BURST', 'saved & 0xF8u'),
    ('keep-burst', '#define TRI_TEST_NO_PCI_BURST 0xF9u', '#define TRI_TEST_NO_PCI_BURST 0xFFu'),
    ('skip-save', 'd->cur_vga_base + 1u, &saved);', 'd->cur_vga_base + 1u, &(u8){0});'),
    ('skip-disabled-cases', 'if (!rc) remember(&result, cpu32_cases(o, disabled));', 'if (!rc) remember(&result, 0);'),
    ('restore-disabled', 'rc = cpu32_pci_write(d, saved);', 'rc = cpu32_pci_write(d, disabled);'),
    ('skip-restore', 'rc = cpu32_pci_write(d, saved);', 'rc = 0;'),
    ('hide-cr39', 'if (pci >= 0) {', 'if (pci > 255) {'),
    ('hide-restore-error', '    remember(&result, original);', '    result = original;'),
    ('skip-readback', 'rc = read_row(o, "cpu32_read", TRI_TEST_SOURCE_Y, count);', 'rc = 0;'),
]

def main():
    from mutpar import build_in_tree
    with tempfile.TemporaryDirectory(prefix='trident-cpu32-') as td:
        work = Path(td)
        cases = [('normal', None, None)]
        if '--mutate' in sys.argv:
            cases += MUTATIONS + [('restored', None, None)]
        for name, old, new in cases:
            changes = {}
            if old:
                source = (ROOT / PRODUCT).read_text()
                assert source.count(old) == 1
                changes[PRODUCT] = source.replace(old, new)
            exe = work / name
            cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                   '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                   '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
                   '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                   '-I.', '-Ikdebug', *include_flags(ROOT),
                   'tools/tests/trident_cpu32_host.c', '-o', str(exe)]
            build_in_tree(ROOT, work / (name+'-tree'), changes, [cmd], capture_output=True)
            for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                result = host32.run([str(exe)], runner=runner, capture_output=True)
                assert result.returncode == (1 if old else 0), (name, result)
            print(('MUTATE RED ' if old else 'GREEN ') + name, flush=True)
    return 0

def run(ctx):
    return subprocess.call(['python3', '-B', __file__,
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
if __name__ == '__main__':
    raise SystemExit(main())
