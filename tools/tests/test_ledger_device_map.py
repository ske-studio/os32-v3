"""M1 real ledger window validation, with runtime-only guard mutations."""
CHECK = dict(name='ledger-device-map-host', tier=1, budget=30.0,
             covers=['mm/pgalloc.c', 'mm/pgalloc.h', 'mm/pgalloc_*.inc',
                     'mm/physmem.c', 'mm/physmem.h', 'include/memmap.h',
                     'tools/tests/ledger_device_map_host.c',
                     'tools/tests/test_ledger_device_map.py',
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
PRODUCT = 'mm/pgalloc_resource.inc'
# A1/A2 strict membership; A3 validation, capacity, ownership and failed
# transaction preservation. Each replacement must compile, then fail in C.
MUTATIONS = [
    ('dynamic-membership', 'm->end && m->rid == rid && m->first <= pfn && pfn < m->end',
     'm->end && m->rid == rid && 0', 'FAIL ledger_resource_page_mapped(rid, p) == expected'),
    ('hole-after-window', 'm->first <= pfn && pfn < m->end',
     'm->first <= pfn && pfn <= m->end', 'FAIL ledger_resource_page_mapped(rid, p) == expected'),
    ('device-owner', 'ledger_owners[owner].kind != LEDGER_KIND_DEVICE ||\n        rid >=',
     '0 ||\n        rid >=', 'FAIL ledger_device_map_prepare(owner, rid, first, end, unmap, &slot) == -1'),
    ('reservation-owner', 'g->type == LEDGER_R_DEVICE && g->owner == owner &&',
     'g->type == LEDGER_R_DEVICE &&', 'FAIL ledger_device_map_prepare(owner, rid, first, end, unmap, &slot) == -1'),
    ('reservation-resource', '(g->res_mask & (1UL << rid))', '1',
     'FAIL ledger_device_map_prepare(owner, rid, first, end, unmap, &slot) == -1'),
    ('reservation-cache', 'g->cache == LEDGER_CACHE_UC &&', '',
     'FAIL ledger_device_map_prepare(owner, rid, first, end, unmap, &slot) == -1'),
    ('static-overlap', 'first < s->map_end && s->map_first < end', '0',
     'FAIL ledger_device_map_prepare(owner, rid, first, end, unmap, &slot) == -1'),
    ('dynamic-overlap', 'else if (first < m->end && m->first < end) goto done;',
     'else if (0) goto done;', 'FAIL ledger_device_map_prepare(owner, rid, first, end, unmap, &slot) == -1'),
    ('capacity', 'if (free_slot == LEDGER_MAX_DEVICE_MAPS) goto done;',
     'if (0) goto done;', 'FAIL ledger_device_map_prepare(owner, rid, first, end, unmap, &slot) == -1'),
    ('unmap-exact', 'm->first == first && m->end == end', 'm->first == first',
     'FAIL ledger_device_map_prepare(owner, rid, first, end, unmap, &slot) == -1'),
    ('static-freeze', 'if (first != r->map_first || end != r->map_end)', 'if (0)',
     'FAIL !ledger_resource_set_map(rid, 0, 0)'),
    ('unmap-remove', 'else ledger_device_maps[slot] = (struct ledger_device_map){0};',
     'else ledger_device_maps[slot].end += 0;',
     'FAIL !ledger_device_maps[slot].end && !_irq_enabled()'),
    ('static-add-live', 'm->end && rec->map_first < m->end && m->first < rec->map_end',
     'm->end && 0', 'FAIL !ledger_resource_add(&rec, &slot) && slot == LEDGER_MAX_RESOURCES'),
]


def main():
    source = (ROOT / PRODUCT).read_text()
    with tempfile.TemporaryDirectory(prefix='ledger-device-map-') as name:
        tmp = Path(name)
        # Private copies preserve quoted includes and never mutate the tree.
        (tmp / 'pgalloc.c').write_text((ROOT / 'mm/pgalloc.c').read_text().replace(
            '#include "io.h"', '#include "arch_io.h"'))
        harness = ROOT / 'tools/tests/ledger_device_map_host.c'
        exe = tmp / 'host'

        def build(body):
            (tmp / 'pgalloc_resource.inc').write_text(body)
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
    return subprocess.call(['python3', '-B', 'tools/tests/test_ledger_device_map.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
