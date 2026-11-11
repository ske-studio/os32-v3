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
CHECK = dict(name='ppc-lan-p24', tier=2, budget=60,
             covers=['platform/powermac/lan_service.c', 'tools/ppc/test_lan_p24.py',
                     'tools/ppc/lan_p24_fake.c'],
             inputs=['platform/powermac/gmac_dma.c', 'mm/paging.h',
                     'drivers/net/gem/gem_reset.c', 'drivers/net/gem/gem_ring.c',
                     'drivers/net/gem/gem_mac.c', 'drivers/net/gem/gem_phy.c',
                     'drivers/net/gem/gem_tx.c', 'drivers/net/gem/gem_rx.c',
                     'platform/powermac/lan_log.c', 'platform/powermac/lan_wire.c',
                     'platform/powermac/lan_monitor.c', 'platform/powermac/lan_diag.c',
                     'platform/powermac/lan_terminal.c',
                     'platform/powermac/lan_boot.h',
                     'drivers/net/gem/gem.h', 'include/*.h',
                     'arch/ppc/*.h', 'lib/kstring.h', 'sdk/include/os32/*.h',
                     'platform/powermac/kboot_info.h',
                     'platform/powermac/kboot_log.h'])
MUTATIONS = [
    ('log-again', 'pm_lan_log_complete(r);', 'pm_lan_log_complete(GEM_OK);',
     'log-again', 'H:A8 loss-count'),
    ('response-full', 'if (s.response.type) { ++s.request_drops; return GEM_OK; }',
     'if (s.response.type) { ++s.request_drops; }', 'response-full', 'H:A10 response-full'),
    ('rx-budget', 'r = receive();', 'r = receive(); r = receive();',
     'rx-budget', 'H:A11 budget'),
    ('reset-cursors', 's.g.ring_prepare_step = s.g.ring_cursor = s.g.ring_publish_step = s.g.rx_head = 0;',
     '(void)0;', 'reset-cursors', 'H:reset-cursors'),
    ('beacon-reset', 's.state = RESETTING;\n}', 's.state = RESETTING; s.beacon_at = 0;\n}',
     'beacon-reconnect', 'H:beacon-reconnect'),
    ('dst', '!same(s.rx, s.boot->mac, 6)', '0', 'filters', 'H:A10 filter'),
]


def command(args):
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True, timeout=60)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = SOURCE.read_text()
    flags = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie',
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
