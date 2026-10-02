"""T2d d4: real caller/copy + managed walk, with MMU/IRQ host boundaries."""
TARGET_SRC = ['exec/redir_access.c', 'exec/access_walk.c', 'kernel/kselftest.c']

import argparse
import hashlib
import subprocess
import time
import test_access_walk as walk
from mutpar import run_ordered

MUTANTS = [
    ('if (per_page_irq) { irq_restore(flags); check_irq_epoch++; }', 'if (per_page_irq && len <= n) { irq_restore(flags); check_irq_epoch++; }', 'write check IRQ spans pages'),
    ('unsigned int flags = irq_save();', 'unsigned int flags = host_arch_if;', 'missing IRQ interval'),
    ('if (!dst[i]) { ok = 1; break; }', 'if (!dst[i]) { u32 next; ok = caller_access_page(c, va + i + 1, 0, &next); break; }', 'probe after NUL'),
    ('if (!dst[i]) { ok = 1; break; }', 'if (!dst[i]) { ok = 1; }', 'read after NUL'),
    ('if (!va || !dst || !cap) goto out;', 'if (!va || !dst) { goto out; } if (!cap) { ok = 1; goto out; }', 'cap zero'),
    ('u32 i = 0; i < cap; i++', 'u32 i = 0; i <= cap; i++', 'cap overread'),
    ('if (!dst[i]) { ok = 1; break; }', 'if (!dst[i] || i + 1 == cap) { ok = 1; break; }', 'unterminated success'),
    ('dst[i] = *(const char *)P2V(pa);', 'dst[i] = *(const char *)P2V(pa); if (i == 0) { u32 ahead; if (!caller_access_page(c, va + cap - 1, 0, &ahead)) goto out; }', 'preflight cap beyond NUL'),
    ('len - 1 > ~(u32)0 - va', '0', 'range overflow'),
    ('(!len || staging) && caller_range(c, va, len, write, 0)', '(!len || staging)', 'copy before full preflight'),
    ('caller_range(c, (u32)(uptr)dst, len, 1, 1)', 'caller_range(c, (u32)(uptr)dst, len, 0, 1)', 'write range ignores RW'),
    ('caller_range(c, va, len, write, 0)', 'caller_range(c, va, len, 0, 0)', 'copyout preflight ignores RW'),
    ('if (write) kmemcpy(P2V(pa), bytes, n);', 'if (write) kmemcpy(P2V(pa), bytes, 1);', 'short copyout'),
    ('else kmemcpy(bytes, P2V(pa), n);', 'else kmemcpy(bytes, P2V(pa), 1);', 'short copyin'),
    ('bytes += n;', 'bytes += 0;', 'staging page offset'),
    ('irq_restore(flags);\n    return ok;\n}', 'irq_restore(flags | 0x200);\n    return ok;\n}', 'unconditional STI'),
    ('va >= MEM_APP_BAND_BASE))', 'va > MEM_APP_BAND_BASE))', 'trusted cstr upper edge'),
    ('len > MEM_APP_BAND_BASE - va', '0', 'trusted range preflight'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    sources = {k: (walk.ROOT / p).read_text() for k, p in walk.FILES.items()}
    paths = [walk.ROOT / p for p in walk.FILES.values()] + [
        walk.ROOT / 'tools/tests/caller_copy_host.c', walk.ROOT / 'tools/tests/access_walk_host.c']
    hashes = {p: hashlib.sha256(p.read_bytes()).digest() for p in paths}
    # Observe the real primitive's call order/IRQ, not a replacement walk.
    body = sources['redir_access'].replace(
        'int caller_access_page(', 'static int actual_caller_access_page(', 1)
    body = body.replace('if (per_page_irq) irq_restore(flags);',
                        'if (per_page_irq) { irq_restore(flags); check_irq_epoch++; }')
    body += '''
int caller_access_page(const struct caller_access *a, u32 va, int write, u32 *pa)
{
    CHECK(!(host_arch_if & 0x200));
    if (observing_check) {
        CHECK(!copy_probes || check_irq_epoch > check_irq_seen);
        check_irq_seen = check_irq_epoch;
    }
    copy_probes++;
    return actual_caller_access_page(a, va, write, pa);
}
'''
    sources['redir_access'] = body
    result = walk.run(sources, fixture='caller_copy_host.c')
    print(result.stdout + result.stderr, end='')
    assert result.returncode == 0, result.returncode
    if args.mutate:
        def one(m):
            old, new, name = m
            modified = dict(sources)
            assert old in body, name
            # IRQ restore mutation intentionally covers every copy exit;
            # shared range bound mutation also hits redirect's same contract.
            modified['redir_access'] = body.replace(old, new)
            start = time.monotonic()
            r = walk.run(modified, fixture='caller_copy_host.c')
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
