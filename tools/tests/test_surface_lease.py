"""T2e e2: real lease/query/B1/managed paging, ILP32 runtime mutations."""
TARGET_SRC = ['exec/lease.c', 'exec/surface_query.c', 'exec/redir_access.c',
              'exec/access_walk.c', 'kernel/paging.c', 'kernel/pgalloc.c']
import argparse
import host32
import hashlib
import pathlib
import statistics
import subprocess
import tempfile
import time
import test_access_walk as walk
from mutpar import run_ordered

ROOT = walk.ROOT
MUTANTS = [
    ('lease', 'source->backend != gfx_sf_backend()', '0', 'selected backend mismatch'),
    ('paging', 'if (phys % PAGE_SIZE || !pgalloc_page_owned(phys / PAGE_SIZE, as->owner) ||',
     'if (0 || !pgalloc_page_owned(phys / PAGE_SIZE, as->owner) ||', 'map PT alignment'),
    ('paging', 'if (!phys || phys % PAGE_SIZE || !pgalloc_page_owned(phys / PAGE_SIZE, as->owner))',
     'if (!phys || 0 || !pgalloc_page_owned(phys / PAGE_SIZE, as->owner))', 'read PT alignment'),
    ('lease', '        if (!lease_release(caller.as, view.token))',
     '        paging_load_cr3(paging_kernel_pd_phys());\n        if (!lease_release(caller.as, view.token))', 'rollback master hop'),
    ('lease', 'lease_rollback_fail_count++;', '(void)0;', 'rollback diagnostic removed'),
    ('lease', '    if (kctx_irq_depth || kctx_exc_depth) return OS32_ERR_INVAL;',
     '    /* entry context check omitted */', 'entry context check removed'),
    ('lease', 'as->lease_pt_phys[i] % PAGE_SIZE ||', '0 ||', 'check PT alignment'),
    ('lease', 'rc = lease_acquire(caller.as, &auth, &ref, 1, access, &view);',
     'paging_load_cr3(paging_kernel_pd_phys());\n    rc = lease_acquire(caller.as, &auth, &ref, 1, access, &view);', 'master hop'),
    ('lease', '    if (rc) return rc;\n    auth =',
     '    irq_restore(flags | 0x200);\n    if (rc) return rc;\n    auth =', 'IF changed'),
    ('lease', 'if (!lease_release(caller.as, view.token))', 'if (0)', 'rollback removed'),
    ('lease', 'lease_next_token = next;', '(void)next;', 'rollback token accounting'),
    ('lease', 'caller.as->leases[i] = old[i];', '(void)old[i];', 'rollback slot bytes'),
    ('lease', 'if (!copy_to_caller(&caller, user_out, &view, sizeof(view)))',
     'if (!copy_to_caller(&caller, user_out, &view, 4) ||\n        !copy_to_caller(&caller, user_out, &view, sizeof(view)))', 'partial out'),
    ('lease', 'sf->gen != refs[i].generation', '0', 'acquire generation'),
    ('lease', 'if (k != count) return LEASE_FULL;',
     'if (k != count) { lease_next_token++; return LEASE_FULL; }', 'FULL token mutation'),
    ('lease', 'if (rc) return surface_query_error(rc);',
     'if (rc) { lease_next_token++; return surface_query_error(rc); }', 'PT failure token mutation'),
    ('paging', 'if (!pending[k]) { rc = -2; goto fail; }',
     'if (!pending[k]) { as->leases[m->slot].token = m->token; rc = -2; goto fail; }', 'PT failure slot mutation'),
    ('paging', '            (d & ~(PTE_ACCESSED | PTE_DIRTY)) != (phys | PAGE_RW | PTE_USER))',
     '            (d & ~(PTE_ACCESSED | PTE_DIRTY | PTE_PS)) != (phys | PAGE_RW | PTE_USER))', 'managed PDE PS'),
]


@host32.control_session
def main(fixture_name="surface_lease_host.c", mutants=MUTANTS, extra_sources=None, lease_hook=""):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=['native', 'qemu'], default='native')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    host32.begin_control(args.mutate, args.runner, ROOT)
    files = dict(walk.FILES, lease='exec/lease.c', surface_query='exec/surface_query.c')
    files.update(extra_sources or {})
    paths = [ROOT / p for p in files.values()]
    paths += [ROOT / p for p in ('exec/lease.h', 'exec/surface_query.h',
        'tools/tests/' + fixture_name, 'tools/tests/test_surface_lease.py',
        'tools/tests/access_walk_host.c')]
    hashes = {p: hashlib.sha256(p.read_bytes()).digest() for p in paths}
    sources = {k: (ROOT / p).read_text() for k, p in files.items()}
    with tempfile.TemporaryDirectory(prefix='os32-e2-') as directory:
        tmp = pathlib.Path(directory)
        for key, body in sources.items():
            if key in ('lease', 'surface_query'): continue
            if key == 'v86':
                body = body.replace('(volatile u32 *)(V86_REMAP_START + PAGE_SIZE)',
                                    '(volatile u32 *)P2V(backing_phys)')
            if key == 'exec':
                body = body[body.index('        /* VRAM (テキスト 0xA0000'):body.index('        /* フォントキャッシュ')]
            if key == 'boot':
                body = body[body.index('static void test_caller_boot('):body.index('static void test_ledger(void)')]
            (tmp / (key + '_host_source.c')).write_text(body)
        io = (ROOT / 'tools/tests/host_arch/arch_io.h').read_text().replace(
            'static unsigned int host_arch_if = 0x202U;', 'extern unsigned int host_arch_if;')
        (tmp / 'arch_io.h').write_text(io)
        fixture = 'unsigned int host_arch_if = 0x202U;\n#include "' + fixture_name + '"\n'
        (tmp / 'fixture.c').write_text(fixture)
        cc = ['gcc', '-std=gnu11', '-m32', '-march=i386', '-ffreestanding', '-fno-pie',
              '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
              '-Wall', '-Wextra', '-Werror', '-DPHYSMEM_HOST_TEST=1',
              '-DHOST_HIGH_ENTRY_STACK=1', '-I' + str(tmp),
              *['-I' + str(ROOT / p) for p in ('tools/tests', 'tools/tests/host_arch',
                'include', 'arch/x86', 'platform/pc98', 'kernel', 'exec', 'fs', 'lib',
                'kapi', 'lib/sqlite3', 'sdk/include/os32')]]
        def compile_source(src, obj):
            host32.build(cc + ['-c', str(src), '-o', str(obj)], check=True,
                           capture_output=True, text=True)
        objects = {}
        for key, src in [('fixture', tmp / 'fixture.c'),
                         ('physmem', ROOT / 'kernel/physmem.c')]:
            objects[key] = tmp / (key + '.o'); compile_source(src, objects[key])
        for key in ('lease', 'surface_query'):
            src = tmp / (key + '.c')
            prefix = lease_hook + '#define copy_to_caller host_lease_copyout\n#define copy_caller_bytes host_lease_copyin\n' if key == 'lease' else ''
            src.write_text(prefix + sources[key])
            objects[key] = tmp / (key + '.o'); compile_source(src, objects[key])
        def run(key, changed=None):
            units = [unit for unit, _ in changed or []]
            assert len(units) == len(set(units)), (key, units)
            # v86/exec/paging each rebuild the fixture TU: never combine them.
            assert sum(unit in ("paging", "v86", "exec") for unit in units) <= 1, (key, units)
            objs = dict(objects)
            for unit, body in changed or []:
                src, obj = tmp / (key + '_' + unit + '.c'), tmp / (key + '_' + unit + '.o')
                if unit in ('paging', 'v86', 'exec'):
                    # Only the TU containing real paging is rebuilt. A private
                    # include name keeps parallel mutants independent.
                    if unit == 'v86':
                        body = body.replace('(volatile u32 *)(V86_REMAP_START + PAGE_SIZE)', '(volatile u32 *)P2V(backing_phys)')
                    if unit == 'exec':
                        body = body[body.index('        /* VRAM (テキスト 0xA0000'):body.index('        /* フォントキャッシュ')]
                    paging = tmp / (key + '_source.c'); paging.write_text(body)
                    access = (ROOT / 'tools/tests/access_walk_host.c').read_text().replace(
                        '#include "' + unit + '_host_source.c"', '#include "' + paging.name + '"')
                    af = tmp / (key + '_access.c'); af.write_text(access)
                    host = (ROOT / ('tools/tests/' + fixture_name)).read_text().replace(
                        '#include "access_walk_host.c"', '#include "' + af.name + '"')
                    host = host.replace('#include "' + unit + '_host_source.c"', '#include "' + paging.name + '"')
                    src.write_text('unsigned int host_arch_if = 0x202U;\n' + host)
                    objs['fixture'] = obj
                else:
                    src.write_text((lease_hook if unit == 'lease' else '') + '#define copy_to_caller host_lease_copyout\n#define copy_caller_bytes host_lease_copyin\n' + body)
                    objs[unit] = obj
                compile_source(src, obj)
            exe = tmp / (key + '.elf')
            host32.build(['gcc', '-m32', '-nostdlib', '-static', '-no-pie',
                '-Wl,--gc-sections', *map(str, objs.values()), '-o', str(exe)],
                check=True, capture_output=True, text=True)
            # Full checks run alongside other compiler/MMU fixtures. Keep
            # a bounded execution budget without treating timeouts as RED.
            return host32.run([str(exe)], runner=args.runner,
                              capture_output=True, text=True, timeout=host32.RUN_TIMEOUT)
        with host32.control(args.mutate, args.runner, ROOT) as normal:
            if normal:
                result = run('normal'); print(result.stdout + result.stderr, end='')
                assert result.returncode == 0, result.returncode
                print(f'PASS runner={args.runner}')
        if args.mutate:
            def one(entry):
                index, mutation = entry
                unit, old, new, name = mutation[:4]
                body = sources[unit]
                tail = ''
                if fixture_name == 'surface_lease_host.c' and unit == 'lease':
                    body, tail = body.split('/* PC98 DISPLAY bundle;', 1)
                    tail = '/* PC98 DISPLAY bundle;' + tail
                if fixture_name == 'surface_bundle_host.c' and unit == 'lease' and old == 'source->backend != gfx_sf_backend()':
                    head, body = body.split('/* PC98 DISPLAY bundle;', 1)
                    body = '/* PC98 DISPLAY bundle;' + body
                else:
                    head = ''
                assert body.count(old) == 1, (name, body.count(old))
                start = time.monotonic()
                changes = [(unit, head + body.replace(old, new) + tail)]
                for extra_unit, extra_old, extra_new in (mutation[4] if len(mutation) > 4 else []):
                    extra_body = sources[extra_unit]
                    assert extra_body.count(extra_old) == 1, (name, extra_old)
                    changes.append((extra_unit, extra_body.replace(extra_old, extra_new)))
                result = run(f'mut{index}', changes)
                assert result.returncode == 1 and 'FAIL:' in result.stdout, (
                    name, result.returncode, result.stdout, result.stderr)
                return name, time.monotonic() - start
            times = []
            for name, seconds in run_ordered(one, list(enumerate(mutants))):
                print(f'RED (runtime): {name} ({seconds:.2f}s)'); times.append(seconds)
            print(f'MUTATIONS {len(times)}/{len(mutants)} runtime RED; '
                  f'median={statistics.median(times):.2f}s max={max(times):.2f}s')
    assert all(hashlib.sha256(p.read_bytes()).digest() == h for p, h in hashes.items())

if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        print(error.stdout or '', error.stderr or '')
        raise
