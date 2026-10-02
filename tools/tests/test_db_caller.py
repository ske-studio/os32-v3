"""T2d d5: real wrappers/copy/walk, SQLite entry-only, small source closure."""
import argparse
import hashlib
import subprocess
import time
import test_access_walk as walk
from mutpar import run_ordered


def sources():
    result = {k: (walk.ROOT / p).read_text() for k, p in walk.FILES.items()}
    src = (walk.ROOT / 'exec/exec.c').read_text()
    constants = '\n'.join(line for line in src.splitlines() if line.startswith('#define RING3_')) + '\n'
    result['guards'] = constants + src[src.index('int ring3_ptr_ok(u32 p)'):src.index('#include "ksetjmp.h"')]
    result['db'] = (walk.ROOT / 'kapi/kapi_db.c').read_text()
    result['str'] = (walk.ROOT / 'exec/ring3_str.c').read_text()
    return result

MUTANTS = [
    ('redir_access', 'dst[i] = *(const char *)P2V(pa);', 'dst[i] = src[i];', 'PA copy replaced by VA'),
    ('guards', 'return ok;\n    }', 'return ok && 0;\n    }', 'valid lease early refusal'),
    ('db', 'PATH_COPY_BUF_SIZE, 1)', 'PATH_COPY_BUF_SIZE, 0)', 'path copied as bytes'),
    ('db', 'SQL_COPY_BUF_SIZE, 1)', 'SQL_COPY_BUF_SIZE, 0)', 'SQL copied as bytes'),
    ('db', 'if (slot->active_stmt) {\n        slot_note_teardown(slot, sqlite3_finalize(slot->active_stmt));\n        slot->active_stmt = (sqlite3_stmt *)0;\n    }\n    slot->bindable = 0;', '', 'no finalize before copy refusal'),
    ('guards', 'if (!caller_access_get_user(&c))', 'if (!caller_access_get(&c))', 'always becomes WM trusted'),
    ('guards', 'check_caller_write_range(&c, (void *)(uptr)vb, lb)', '((void)vb, 1)', 'second output unchecked'),
    ('guards', 'caller_access_page(&c, va, 0, &pa)', 'caller_access_page(&c, va, 1, &pa)', 'RO input refused'),
    ('guards', 'check_caller_write_range(&c, (void *)(uptr)va, la)', '1', 'first output unchecked'),
    ('guards', 'ring3_range_reject_count++;', '(void)0;', 'invalid caller refusal not counted'),
    ('guards', 'ring3_range_refuse(RING3_RANGE_WR_TABLE, la ? va : vb, 0)',
     'ring3_range_refuse(RING3_RANGE_WR_PDE, la ? va : vb, 0)', 'invalid caller wrong reason'),
    ('guards', 'if (!ring3_guard_active(ring3_in_syscall, ring3_wm_depth)) return 1;\n    if (!p)',
     'if (!ring3_in_syscall) return 1;\n    if (!p)', 'read guard WM bypass removed'),

]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    paths = [walk.ROOT / p for p in list(walk.FILES.values()) +
             ['exec/exec.c', 'exec/ring3_str.c', 'kapi/kapi_db.c', 'tools/tests/db_caller_host.c']]
    hashes = {p: hashlib.sha256(p.read_bytes()).digest() for p in paths}
    src = sources()
    r = walk.run(src, fixture='db_caller_host.c')
    print(r.stdout + r.stderr, end='')
    assert r.returncode == 0, r.returncode
    if args.mutate:
        def one(m):
            key, old, new, name = m
            mutated = dict(src)
            assert old in mutated[key], name
            mutated[key] = mutated[key].replace(old, new)
            start = time.monotonic()
            r = walk.run(mutated, fixture='db_caller_host.c')
            assert r.returncode != 0 and 'FAIL:' in r.stdout, (name, r.returncode, r.stdout, r.stderr)
            return name, time.monotonic() - start
        for name, seconds in run_ordered(one, MUTANTS):
            print(f'RED (runtime): {name} ({seconds:.2f}s)')
        print(f'MUTATIONS {len(MUTANTS)}/{len(MUTANTS)} runtime RED')
    assert all(hashlib.sha256(p.read_bytes()).digest() == h for p, h in hashes.items())

if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as e:
        print(e.stdout or '', e.stderr or '')
        raise
