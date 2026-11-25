#!/usr/bin/env python3
"""I1 composition: the real MM/ledger and surface lifecycle in one executable."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
from kernel_layout import linker_flags
from paging_source import paging_source

CHECK = dict(name='gfx2-i1-host', tier=2, needs=['make:all'], budget=60,
    covers=['tools/tests/test_gfx2_i1.py', 'tools/tests/gfx2_i1_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'tools/tests/kernel_layout.py', 'tools/tests/paging_source.py',
            'tools/tests/pgalloc_host_fixture.h', 'tools/tests/host_arch/**',
            'sys/hal/gfx/gfx_core.c', 'gfx/gfx_core_*.inc', 'gfx/v2/**', 'gfx/sync/**', 'sys/hal/gfx/gfx.h', 'sys/hal/gfx/gfx_policy.h', 'sys/hal/gfx/palette.c', 'sys/hal/gfx/palette.h',
            'mm/**', 'include/**', 'sdk/include/**', 'sys/kern/kstring.h',
            'sys/kern/kstring_c.c', 'lib/dma_contract.c', 'kernel/module/**',
            'sys/kern/console/display_restore.h',
            'sys/kern/exec/lease.c', 'sys/kern/exec/lease.h', 'sys/kern/exec/appslot.h',
            'sys/platform/pc98/kernel.c', 'build/os32.ld', 'userland/diag/gfx2_i1.c', 'userland/deploy.yaml'],
    basis=['HAL_V2_DESIGN §7-4', 'HAL_V2_DESIGN §13-1', 'HAL_V2_DESIGN §13-2'],
    env_limit='hostでallocator/ledger/PTの回収を確認するが、CPU MMU/deviceがfakeのため旧VAの実アクセス拒否とTLBは保証しない。',
)
ROOT = Path(__file__).resolve().parents[2]
UNITS = ['backend_reg', 'caps', 'surface', 'vram_heap', 'gart', 'lock',
         'fence', 'owner_release', 'map_audit', 'sysmem', 'v1_shim', 'a0_weak']
MUTATIONS = [
    ('caps-init', 'sys/hal/gfx/v2/caps.c', 'if (!a->be->caps(a->ctx, &c)) a->caps = c;', '(void)a; (void)c;'),
    ('adapter-gen', 'sys/hal/gfx/v2/backend_reg.c', '*gen = a->gen;', '*gen = a->gen + 1;'),
    ('map-probe', 'sys/kern/mm/paging_gfx2_selftest.inc', '= 0xa5a5f00dUL;', '= 0xa5a5f00cUL;'),
    ('owner-unmap', 'sys/hal/gfx/v2/owner_release.c', 'if (gfx2_unmap(s)) {', 'if (0) {'),
    ('sysmem-free', 'sys/hal/gfx/v2/sysmem.c', 'if (!pgalloc_free_n_owner(s->owner, s->sys_pfn, (int)s->sys_n))', 'if (0)'),
    ('palette-shadow', 'sys/hal/gfx/v2/palette_state.c', '(u32)p[i].r * 17 << 16', '(u32)p[i].r * 16 << 16'),
    ('sw-dispatch', 'sys/hal/gfx/v2/sw_table.c', '[GFX2_OP_FILL] = gfx2_sw_fill,', '[GFX2_OP_FILL] = gfx2_sw_copy,'),
]

def observe(work, mutation=None, fixture="gfx2_i1_host.c", extra=(), stage=None, native=(), cflags=()):
    # Read includes through the same source view used by existing MM fixtures.
    overrides = {}
    if mutation:
        name, path, old, new = mutation
        body = (ROOT / path).read_text()
        assert body.count(old) == 1, name
        overrides[path] = body.replace(old, new)
    for path, name in [('sys/kern/mm/paging.c', 'paging_host_source.c'),
                       ('sys/kern/mm/pgalloc.c', 'pgalloc_host_source.c')]:
        body = paging_source(ROOT / path)
        for source, replacement in overrides.items():
            if source.startswith('mm/' + Path(path).stem + '_'):
                original = (ROOT / source).read_text()
                assert body.count(original) == 1
                body = body.replace(original, replacement)
        (work / name).write_text(body)
    arch = (ROOT / 'tools/tests/host_arch/arch_cpu.h').read_text()
    arch = arch.replace('#define ARCH_CPU_H', '#define ARCH_CPU_H\nextern u32 host_cr3;\nstatic inline void arch_cpu_serialize(void) {}')
    (work / 'arch_cpu.h').write_text(arch)
    for source, replacement in overrides.items():
        if source.endswith('.h'):  # a mutated header shadows the tree copy ("" include, -I work first)
            (work / Path(source).name).write_text(replacement)
    sources = []
    for path in [*['gfx/v2/' + n + '.c' for n in UNITS], 'sys/kern/exec/lease.c',
                 'sys/hal/gfx/v2/palette_state.c', 'sys/hal/gfx/v2/sw_table.c', 'sys/hal/gfx/palette.c',
                 'sys/hal/gfx/sync/a0_weak.c', 'sys/hal/gfx/sync/sync_storage.c', *extra]:
        target = work / path.replace('/', '_')
        body = paging_source(ROOT / path) if path == 'sys/hal/gfx/gfx_core.c' else (ROOT / path).read_text()
        target.write_text(overrides.get(path, body))
        sources.append(str(target))
    if stage:
        stage(work, overrides)
    exe = work / 'i1'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O1', '-Wall', '-Wextra',
           '-Werror', '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie',
           '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
           *cflags, '-nostdlib', '-static', '-DPHYSMEM_HOST_TEST=1', '-Wl,--gc-sections',
           *linker_flags(), '-I' + str(work),
           '-I' + str(ROOT / 'tools/tests/host_arch'), '-I' + str(ROOT / 'gfx/v2'),
           '-I' + str(ROOT / 'gfx/sync'), '-I' + str(ROOT / 'userland/lib/gfx/v2'),
           *include_flags(ROOT), str(ROOT / 'tools/tests' / fixture),
           *sources, *(str(ROOT / path) for path in native), str(ROOT / 'sys/kern/mm/physmem.c'), str(ROOT / 'sys/kern/kstring_c.c'),
           str(ROOT / 'lib/dma_contract.c'), '-o', str(exe)]
    subprocess.run(cmd, check=True)
    result = host32.run([str(exe)], runner='qemu', text=True, capture_output=True, timeout=60)
    if mutation:
        assert result.returncode == 1 and result.stdout.startswith('FAIL '), (mutation, result)
        print('RED runtime ' + mutation[0], flush=True)
    else:
        assert result.returncode == 0, result
        print(result.stdout, end='', flush=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    # Check guest commands against the actual image's deployment paths.
    deployment = json.loads((ROOT / 'build/out/deploy-set.json').read_text())
    paths = {entry['guest'] for entry in deployment['files']}
    commands = re.findall(r'^#define (?:CHILD|AUDIT_CHILD) "([^"]+)"',
                          (ROOT / 'userland/diag/gfx2_i1.c').read_text(), re.M)
    assert len(commands) == 2 and all(command.split()[0] in paths for command in commands)
    for mutation in [None, *MUTATIONS, None] if args.mutate else [None]:
        with tempfile.TemporaryDirectory(prefix='gfx2-i1-') as folder:
            observe(Path(folder), mutation)

def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)

if __name__ == '__main__':
    main()
