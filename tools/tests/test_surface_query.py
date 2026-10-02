"""T2e e1: real query, saved caller, B1, paging and ledger (ILP32).
Fixed low fixture stack; explicit native/qemu execution, no silent fallback.
Compile failures and signals are never mutation kills.
"""
TARGET_SRC = ['exec/surface_query.c', 'exec/redir_access.c', 'exec/access_walk.c', 'kernel/paging.c', 'kernel/pgalloc.c']
import argparse
import hashlib
import pathlib
import subprocess
import tempfile
import time
import statistics
import test_access_walk as walk
from mutpar import run_ordered

ROOT = walk.ROOT
MUTANTS = [
    ('done:\n    irq_restore(flags);\n    return rc;\n}\n\nint surface_query(', 'done:\n    irq_restore(flags | 0x200);\n    return rc;\n}\n\nint surface_query(', 'refs IF restore'),
    ('!s->ready', '0', 'uninitialized'),
    ('c.origin != CALLER_USER', '0', 'trusted'),
    ('slot->state != APP_STATE_RUNNING', '0', 'parked'),
    ('gui && gfx && appslot_gfx_owner() != c.app_id', '0', 'GUI fullscreen CLIENT'),
    ('!gfx || (gui && appslot_gfx_owner() != c.app_id)', '!gfx', 'GUI DISPLAY owner'),
    ('!gfx || (gui && appslot_gfx_owner() != c.app_id)', '(gui && appslot_gfx_owner() != c.app_id)', 'DISPLAY GFX'),
    ('if (gui) return OS32_ERR_INVAL;', 'if (0) return OS32_ERR_INVAL;', 'GUI TVRAM'),
    ('s->count != want', 's->count > want', 'missing face'),
    ('sf->gen != r->generation', '0', 'snapshot generation'),
    ('|| sf->closing ||', '|| 0 ||', 'snapshot closing'),
    ('!sf->npages)', '0)', 'retired'),
    ('sf->role != s->role || sf->backend != s->backend', 'sf->role != s->role', 'selected backend'),
    ('s->refs[j].sid == r->sid', '0', 'duplicate source'),
    ('sf->gen == refs[i].generation && !sf->closing && sf->npages', 'sf->gen == 0xffffffffU', 'different bundle'),
    ('            stale = 1;\n        } else if', '            stale = 0;\n        } else if', 'reinit changed sid'),
    ('refs[i].sid >= LEDGER_MAX_SURFACES', '0', 'ref sid bound'),
    ('!refs[i].generation', '0', 'ref zero generation'),
    ('sf->gen == refs[i].generation &&', '1 &&', 'alien ref generation'),
    ('!sf->closing && sf->npages', '1 && sf->npages', 'alien ref closing'),
    ('&& sf->npages', '&& 1', 'alien ref pages'),
    ('refs[j].sid == refs[i].sid', '0', 'duplicate input'),
    ('s->backend < LEDGER_SF_PC98', '0', 'backend lower bound'),
    ('s->backend > LEDGER_SF_CIRRUS', '0', 'backend upper bound'),
    ('else if (s->backend)', 'else if (0)', 'system backend'),
    ('s->backend == LEDGER_SF_PC98 ? SURFACE_QUERY_MAX : 1', 's->backend != 0 ? SURFACE_QUERY_MAX : 1', 'PEGC DISPLAY count'),
    ('refs[i].generation != snap.desc[i].ref.generation', '0', 'old refs'),
    ('access == LEDGER_PERM_RW && snap.desc[i].access_max != LEDGER_PERM_RW', '0', 'RO escalation'),
    ('s->role == LEDGER_ROLE_UNICODE && sf->perm_max != LEDGER_PERM_RO', '0', 'Unicode RO'),
    ('refs[i].generation != snap.desc[i].ref.generation', 'refs[0].generation != snap.desc[0].ref.generation', 'last ref generation'),
    ('d->plane_offset[j] = sf->plane_offset[j];', 'd->plane_offset[j] = 0;', 'geometry'),
    ('!copy_to_caller(&c, user_out, &snap, sizeof(snap))', '0', 'copyout'),
    ('sizeof(snap)))\n        rc = OS32_ERR_INVAL;\ndone:\n    irq_restore(flags);', 'sizeof(snap)))\n        rc = OS32_ERR_INVAL;\ndone:\n    irq_restore(flags | 0x200);', 'IF restore'),
    ('return OS32_ERR_NOSPC;', 'return OS32_ERR_FULL;', 'error mapping'),
]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mutate', action='store_true')
    p.add_argument('--runner', choices=['native', 'qemu'], default='native')
    args = p.parse_args()
    paths = [ROOT / v for v in walk.FILES.values()] + [ROOT / 'exec/surface_query.c']
    paths += [ROOT / x for x in ('exec/surface_query.h', 'tools/tests/surface_query_host.c',
                                 'tools/tests/test_surface_query.py', 'tools/tests/access_walk_host.c')]
    hashes = {f: hashlib.sha256(f.read_bytes()).digest() for f in paths}
    sources = {k: (ROOT / v).read_text() for k, v in walk.FILES.items()}
    sources['surface_query'] = (ROOT / 'exec/surface_query.c').read_text()
    with tempfile.TemporaryDirectory(prefix='os32-e1-') as directory:
        tmp = pathlib.Path(directory)
        for key, body in sources.items():
            if key == 'surface_query': continue
            if key == 'boot':
                body = body[body.index('static void test_caller_boot('):body.index('static void test_ledger(void)')]
            (tmp / (key + '_host_source.c')).write_text(body)
        # Share the host IRQ register across TUs, so the normal source closure
        # is compiled once and each mutation compiles only surface_query.c.
        io = (ROOT / 'tools/tests/host_arch/arch_io.h').read_text()
        io = io.replace('static unsigned int host_arch_if = 0x202U;',
                        'extern unsigned int host_arch_if;')
        (tmp / 'arch_io.h').write_text(io)
        (tmp / 'surface_query_host_source.c').write_text('#include "surface_query.h"\n')
        (tmp / 'fixture.c').write_text('unsigned int host_arch_if = 0x202U;\n#include "surface_query_host.c"\n')
        cc = ['gcc', '-std=gnu11', '-m32', '-march=i386', '-ffreestanding',
              '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
              '-Wall', '-Wextra', '-Werror', '-DPHYSMEM_HOST_TEST=1',
              '-DHOST_HIGH_ENTRY_STACK=1', '-I' + str(tmp),
              *['-I' + str(ROOT / x) for x in ('tools/tests', 'tools/tests/host_arch',
                'include', 'arch/x86', 'platform/pc98', 'kernel', 'exec', 'fs',
                'lib', 'kapi', 'lib/sqlite3', 'sdk/include/os32')]]
        for src, obj in [(tmp / 'fixture.c', tmp / 'fixture.o'),
                         (ROOT / 'kernel/physmem.c', tmp / 'physmem.o')]:
            subprocess.run(cc + ['-c', str(src), '-o', str(obj)], check=True,
                           capture_output=True, text=True)
        def run(body, key):
            src, obj, exe = [tmp / (key + ext) for ext in ('.c', '.o', '.elf')]
            src.write_text(body)
            subprocess.run(cc + ['-c', str(src), '-o', str(obj)], check=True,
                           capture_output=True, text=True)
            subprocess.run(['gcc', '-m32', '-nostdlib', '-static', '-no-pie',
                            '-Wl,--gc-sections', str(tmp / 'fixture.o'),
                            str(tmp / 'physmem.o'), str(obj), '-o', str(exe)],
                           check=True, capture_output=True, text=True)
            return walk.run_host32(exe, args.runner)
        body = sources['surface_query']
        r = run(body, 'normal')
        print(r.stdout + r.stderr, end='')
        assert r.returncode == 0, r.returncode
        print(f'PASS runner={args.runner}')
        if args.mutate:
            def one(entry):
                index, (old, new, name) = entry
                assert body.count(old) == 1, (name, body.count(old))
                start = time.monotonic()
                r = run(body.replace(old, new), f'mut{index}')
                assert r.returncode != 0 and 'FAIL:' in r.stdout, (name, r.returncode, r.stdout, r.stderr)
                return name, time.monotonic() - start
            times = []
            for name, seconds in run_ordered(one, list(enumerate(MUTANTS))):
                print(f'RED (runtime): {name} ({seconds:.2f}s)')
                times.append(seconds)
            print(f'MUTATIONS {len(times)}/{len(MUTANTS)} runtime RED; median={statistics.median(times):.2f}s max={max(times):.2f}s')
    assert all(hashlib.sha256(f.read_bytes()).digest() == h for f, h in hashes.items())

if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as e:
        print(e.stdout or '', e.stderr or '')
        raise
