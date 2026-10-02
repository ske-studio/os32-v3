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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=['native', 'qemu'], default='native')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    files = dict(walk.FILES, lease='exec/lease.c', surface_query='exec/surface_query.c')
    paths = [ROOT / p for p in files.values()]
    paths += [ROOT / p for p in ('exec/lease.h', 'exec/surface_query.h',
        'tools/tests/surface_lease_host.c', 'tools/tests/test_surface_lease.py',
        'tools/tests/access_walk_host.c')]
    hashes = {p: hashlib.sha256(p.read_bytes()).digest() for p in paths}
    sources = {k: (ROOT / p).read_text() for k, p in files.items()}
    with tempfile.TemporaryDirectory(prefix='os32-e2-') as directory:
        tmp = pathlib.Path(directory)
        for key, body in sources.items():
            if key in ('lease', 'surface_query'): continue
            if key == 'boot':
                body = body[body.index('static void test_caller_boot('):body.index('static void test_ledger(void)')]
            (tmp / (key + '_host_source.c')).write_text(body)
        io = (ROOT / 'tools/tests/host_arch/arch_io.h').read_text().replace(
            'static unsigned int host_arch_if = 0x202U;', 'extern unsigned int host_arch_if;')
        (tmp / 'arch_io.h').write_text(io)
        fixture = 'unsigned int host_arch_if = 0x202U;\n#include "surface_lease_host.c"\n'
        (tmp / 'fixture.c').write_text(fixture)
        cc = ['gcc', '-std=gnu11', '-m32', '-march=i386', '-ffreestanding', '-fno-pie',
              '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
              '-Wall', '-Wextra', '-Werror', '-DPHYSMEM_HOST_TEST=1',
              '-DHOST_HIGH_ENTRY_STACK=1', '-I' + str(tmp),
              *['-I' + str(ROOT / p) for p in ('tools/tests', 'tools/tests/host_arch',
                'include', 'arch/x86', 'platform/pc98', 'kernel', 'exec', 'fs', 'lib',
                'kapi', 'lib/sqlite3', 'sdk/include/os32')]]
        def compile_source(src, obj):
            subprocess.run(cc + ['-c', str(src), '-o', str(obj)], check=True,
                           capture_output=True, text=True)
        objects = {}
        for key, src in [('fixture', tmp / 'fixture.c'),
                         ('physmem', ROOT / 'kernel/physmem.c')]:
            objects[key] = tmp / (key + '.o'); compile_source(src, objects[key])
        for key in ('lease', 'surface_query'):
            src = tmp / (key + '.c')
            prefix = '#define copy_to_caller host_lease_copyout\n#define copy_caller_bytes host_lease_copyin\n' if key == 'lease' else ''
            src.write_text(prefix + sources[key])
            objects[key] = tmp / (key + '.o'); compile_source(src, objects[key])
        def run(key, changed=None):
            objs = dict(objects)
            if changed:
                unit, body = changed
                src, obj = tmp / (key + '.c'), tmp / (key + '.o')
                if unit == 'paging':
                    # Only the TU containing real paging is rebuilt. A private
                    # include name keeps parallel mutants independent.
                    paging = tmp / (key + '_paging.c'); paging.write_text(body)
                    access = (ROOT / 'tools/tests/access_walk_host.c').read_text().replace(
                        '#include "paging_host_source.c"', '#include "' + paging.name + '"')
                    af = tmp / (key + '_access.c'); af.write_text(access)
                    host = (ROOT / 'tools/tests/surface_lease_host.c').read_text().replace(
                        '#include "access_walk_host.c"', '#include "' + af.name + '"')
                    src.write_text('unsigned int host_arch_if = 0x202U;\n' + host)
                    objs['fixture'] = obj
                else:
                    src.write_text('#define copy_to_caller host_lease_copyout\n#define copy_caller_bytes host_lease_copyin\n' + body)
                    objs[unit] = obj
                compile_source(src, obj)
            exe = tmp / (key + '.elf')
            subprocess.run(['gcc', '-m32', '-nostdlib', '-static', '-no-pie',
                '-Wl,--gc-sections', *map(str, objs.values()), '-o', str(exe)],
                check=True, capture_output=True, text=True)
            # Full checks run alongside other compiler/MMU fixtures. Keep
            # a bounded execution budget without treating timeouts as RED.
            return host32.run([str(exe)], runner=args.runner,
                              capture_output=True, text=True, timeout=60)
        result = run('normal'); print(result.stdout + result.stderr, end='')
        assert result.returncode == 0, result.returncode
        print(f'PASS runner={args.runner}')
        if args.mutate:
            def one(entry):
                index, (unit, old, new, name) = entry
                body = sources[unit]; assert body.count(old) == 1, (name, body.count(old))
                start = time.monotonic()
                result = run(f'mut{index}', (unit, body.replace(old, new)))
                assert result.returncode == 1 and 'FAIL:' in result.stdout, (
                    name, result.returncode, result.stdout, result.stderr)
                return name, time.monotonic() - start
            times = []
            for name, seconds in run_ordered(one, list(enumerate(MUTANTS))):
                print(f'RED (runtime): {name} ({seconds:.2f}s)'); times.append(seconds)
            print(f'MUTATIONS {len(times)}/{len(MUTANTS)} runtime RED; '
                  f'median={statistics.median(times):.2f}s max={max(times):.2f}s')
    assert all(hashlib.sha256(p.read_bytes()).digest() == h for p, h in hashes.items())

if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        print(error.stdout or '', error.stderr or '')
        raise
