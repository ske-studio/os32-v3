#!/usr/bin/env python3
"""Whole-TU service/GEM/PHY/log/wire/monitor integration, with MMIO fakes."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'platform/powermac/lan_service.c'
PRODUCTS = ['drivers/net/gem/gem_' + name + '.c'
            for name in ('reset', 'ring', 'mac', 'phy', 'tx', 'rx')]
PRODUCTS += ['platform/powermac/gmac_dma.c']
PRODUCTS += ['platform/powermac/lan_' + name + '.c'
             for name in ('log', 'wire', 'monitor', 'diag', 'terminal')]
CHECK = dict(name='ppc-lan-terminal-service', tier=2, budget=60,
             covers=['tools/ppc/test_lan_terminal_service.py'],
             inputs=['platform/powermac/lan_service.c', 'platform/powermac/lan_terminal.c',
                     'tools/ppc/lan_p24_fake.c', 'platform/powermac/*.h',
                     'drivers/net/gem/*.c', 'drivers/net/gem/*.h',
                     'platform/powermac/gmac_dma.c', 'platform/powermac/lan_*.c',
                     'mm/paging.h', 'include/*.h', 'arch/ppc/*.h', 'lib/kstring.h',
                     'sdk/include/os32/*.h'])
MUTATIONS = [
    ('alternate', 's.out_turn = !use_out;', 's.out_turn = 0;', 'alternate', 'H:alternate'),
    ('state-interval', 's.state_at = now + s.g.io.tb_hz;', 's.state_at = now;', 'state-interval', 'H:state-interval'),
]


def command(args):
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True, timeout=60)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = SOURCE.read_text()
    flags = ['gcc', '-DTERM_TEST', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie',
             '-fno-stack-protector', '-fno-builtin', '-ffunction-sections',
             '-fdata-sections', '-Wall', '-Wextra', '-Werror',
             '-I.', '-Iinclude', '-Iplatform/powermac', '-Iarch/ppc']
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        path = Path(tmp)
        objects = []
        for product in PRODUCTS:
            obj = path / (Path(product).stem + '.o')
            # Discard only the privileged copy8 wrapper; diagnostics resolve the
            # fake copy8. The complete monitor TU is compiled without edits.
            extra = ['-Dpm_lan_copy8=hardware_copy8'] if product.endswith('lan_monitor.c') else []
            result = command(flags + extra + ['-c', product, '-o', str(obj)])
            if result.returncode:
                raise RuntimeError('compile ' + product + ': ' + result.stderr)
            objects.append(str(obj))
        cases = [('baseline', original, None, None)]
        if args.mutate:
            for name, old, new, marker, diagnostic in MUTATIONS:
                if original.count(old) != 1:
                    raise RuntimeError('nonunique anchor: ' + name)
                cases.append((name, original.replace(old, new, 1), marker, diagnostic))
        for name, source, marker, diagnostic in cases:
            (path / 'lan_service.c').write_text(source)
            binary = path / 'test'
            result = command(flags[:1] + ['-I' + tmp] + flags[1:] + ['-nostdlib', '-static', '-no-pie',
                                      '-Wl,--gc-sections,--wrap=pm_gmac_dma_init,-Ttext-segment=0x100000',
                                      'tools/ppc/lan_p24_fake.c'] +
                             objects + ['-o', str(binary)])
            if result.returncode:
                raise RuntimeError(name + ' compile: ' + result.stderr)
            result = command(['qemu-i386', str(binary)])
            if marker is None:
                if result.returncode or not result.stdout.endswith('PASS\n') or result.stderr:
                    raise RuntimeError('baseline: ' + str(result.returncode) + '\n' + result.stdout + result.stderr)
                print(result.stdout, end='', flush=True)
            else:
                if (result.returncode != 1 or 'reached=' + marker + '\n' not in result.stdout
                        or not result.stdout.endswith(diagnostic + '\n') or result.stderr):
                    raise RuntimeError(name + ': unexpected ' + str(result.returncode) + '\n' + result.stdout + result.stderr)
                print('RED ' + name + ': reached=' + marker + '; ' + diagnostic, flush=True)
    print('PASS ppc-lan-p24')


if __name__ == '__main__':
    main()
