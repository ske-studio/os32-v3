"""M4 real surface static/dynamic membership, with runtime-only guard mutations."""
CHECK = dict(name='ledger-surface-map-host', tier=1, budget=4.4,
             covers=['mm/pgalloc.c', 'mm/pgalloc.h', 'mm/pgalloc_*.inc',
                     'mm/physmem.c', 'mm/physmem.h', 'include/memmap.h',
                     'tools/tests/ledger_surface_map_host.c',
                     'tools/tests/test_ledger_surface_map.py',
                     'tools/tests/pgalloc_host_fixture.h',
                     'tools/tests/host_arch/arch_io.h',
                     'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
                     'build/config.mk'], needs=[])
from pathlib import Path
import os
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'mm/pgalloc_surface.inc'
# A1 acceptance and A2 rejection, including resource provenance.
# Mutations compile successfully and must fail in the C harness.
MUTATIONS = [
    ('static-only', 'ledger_resource_page_mapped(i, p)',
     '(ledger_resources[i].map_first <= p && p < ledger_resources[i].map_end)',
     'FAIL ledger_surface_validate(&sf) == expected'),
    ('allow-holes', 'ledger_resource_page_mapped(i, p)', '1',
     'FAIL ledger_surface_validate(&sf) == expected'),
    ('ignore-resource', '(r->res_mask & (1U << i)) &&', '(r->res_mask || 1) &&',
     'FAIL !ledger_surface_validate(&sf)'),
]


def main():
    source = (ROOT / PRODUCT).read_text()
    with tempfile.TemporaryDirectory(prefix='ledger-surface-map-') as name:
        tmp = Path(name)
        # Private copies preserve quoted includes and never mutate the tree.
        (tmp / 'pgalloc.c').write_text((ROOT / 'mm/pgalloc.c').read_text().replace(
            '#include "io.h"', '#include "arch_io.h"'))
        harness = ROOT / 'tools/tests/ledger_surface_map_host.c'
        exe = tmp / 'host'

        def build(body):
            (tmp / 'pgalloc_surface.inc').write_text(body)
            subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                            '-Wall', '-Wextra', '-Werror', '-ffreestanding',
                            '-fno-pie', '-fno-stack-protector', '-ffunction-sections',
                            '-fdata-sections', '-nostdlib', '-static', '-no-pie',
                            '-Wl,--gc-sections', '-I' + str(tmp),
                            # Empty SLACK, matching the allocator unit fixtures.
                            '-Wl,--defsym=__kernel_resident_end=0x375000',
                            '-I' + str(ROOT / 'tools/tests/host_arch'),
                            '-I' + str(ROOT / 'tools/tests'), *include_flags(ROOT),
                            str(harness), str(ROOT / 'mm/physmem.c'), '-o', str(exe)],
                           check=True)

        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            build(source)
            host32.run([str(exe)], runner=runner, check=True)
            print(f'GREEN runner={runner}', flush=True)
            if '--mutate' in sys.argv:
                for label, old, new, expected in MUTATIONS:
                    assert source.count(old) == 1, (label, source.count(old))
                    build(source.replace(old, new))
                    result = host32.run([str(exe)], runner=runner,
                                        capture_output=True, text=True)
                    assert result.returncode == 1 and expected in result.stdout, (
                        label, result.returncode, result.stdout, result.stderr)
                    print(f'MUTATE runtime RED {label}: {expected}', flush=True)
                build(source)
                host32.run([str(exe)], runner=runner, check=True)
                print(f'RESTORED GREEN runner={runner}', flush=True)


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_ledger_surface_map.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
