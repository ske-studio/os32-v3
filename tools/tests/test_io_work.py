#!/usr/bin/env python3
"""IO_TIER_DESIGN §8 rows 1–4: execute the actual dispatcher, including mutants."""
CHECK = dict(name='io-work-host', tier=3, budget=20, needs=[], covers=[
    'kernel/idle_work.h', 'kernel/io_work.c', 'kernel/io_work.h', 'kernel/io_clock.h', 'kernel/io_policy.h',
    'tools/tests/io_work_host.c', 'tools/tests/test_io_work.py', 'tools/tests/host32.py',
            'include/types.h', 'include/callconv.h',
            'sdk/include/os32/os32_kapi_shared.h'],
    basis=['IO_TIER_DESIGN §4', '§5', '§8'])
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import host32

MUTANTS = [
    ('register while running', '    /* Allowed while io_dispatch runs', '    if (running) return OS32_ERR_BUSY;\n    /* Allowed while io_dispatch runs'),
    ('limit entry cache', 'slots[i].limit.us = slots[i].desc.default_budget_us;', 'slots[i].limit.us = 0;'),
    ('failed unit read', 'if (raw_read(now, &source, &gen) < 0) return 0;',
     'if (raw_read(now, &source, &gen) < 0) { *now = s->start_count; return 1; }'),
    ('limit time', 's->limit_count = minimum(remaining, bound_count);', 's->limit_count = bound_count; (void)remaining;'),
    ('limit units', 's->max_units = minimum(units_left, max_units);', 's->max_units = max_units; (void)units_left;'),
    ('limit elapsed', 'elapsed < s->limit_count ? s->limit_count - elapsed : 0', 's->limit_count + (elapsed & 0u)'),
    ('limit token', 'if (!token_read(s, &now)) return OS32_ERR_AGAIN;', 'if (!token_read(s, &now)) now = s->start_count;'),
    ('limit conversion failure', 'if (rc < 0) return rc;\n        i = next++', 'if (rc < 0) bound_count = 0;\n        i = next++'),
    ('limit policy cache', 'slots[i].limit.policy == policy', '1'),
    ('limit cache reuse', 'if (i < IO_SLOTS) bound_count = slots[i].limit.count;', 'if (i < IO_SLOTS) { if (convert(budget_us, s->clock_source, s->clock_generation, &bound_count) < 0) return OS32_ERR_AGAIN; }'),

    ('Tier order', 'i = first(pending[t] & ~visited);', 'i = first(pending[2 - t] & ~visited);'),
    ('fair cursor', 'cursor[fair] = ((unsigned int)i + 1) % IO_SLOTS;', 'cursor[fair] = 0;'),
    ('Tier fair cursor', 'fair_next = (unsigned int)fair ^ 1u;', 'fair_next = 0;'),
    ('carry right', 'rounds[t] = minimum(rounds[t] + 1, IO_FAIR_ROUNDS);',
     'rounds[t] = (rounds[t] + 1) % IO_FAIR_ROUNDS;'),
    ('compat empty', 'idle_work_compat_count() == 0 && !any_pending()', '!any_pending()'),
    ('begin count', 'convert(budget_us, next.clock_source', 'convert(budget_us + 1, next.clock_source'),
    ('empty clock', 'if (idle_work_compat_count() == 0 && !any_pending()) return 0;',
     'if (idle_work_compat_count() == 0 && !any_pending()) { u32 a, b, c; raw_read(&a, &b, &c); return 0; }'),
    ('token generation', 'source != s->clock_source || gen != s->clock_generation', 'source != s->clock_source'),
    ('running', 'if (running) return 0;', 'if (0) return 0;'),
    ('new request', 'if (result < 0) stats.last_error = result;',
     'pending[0] = pending[1] = pending[2] = 0; if (result < 0) stats.last_error = result;'),
    ('visited', 'visited |= 1u << i;\n        int result', 'visited |= 0;\n        int result'),
    ('terminal clock failure', 'if (result == IO_MORE) io_request(slots[i].desc.id);',
     'if (result == IO_MORE || clock_failed) io_request(slots[i].desc.id);'),
    ('fair fallback', 'fair = -1; /* Preserve the unserved right; use ordinary Tier order. */',
     'break; /* Incorrectly discard the remaining round. */'),
    ('start reset', 'now - s->start_count >= s->limit_count',
     'now - (s->start_count = now) >= s->limit_count'),
    ('unit limit', 's->units >= s->max_units', 's->units > s->max_units'),
    ('time boundary', 'now - s->start_count >= s->limit_count', 'now - s->start_count > s->limit_count'),
    ('clock failure', 'if (!valid || clock_failed) break;', 'clock_failed = 0;'),
]


def exercise(root, work, product):
    # Local product copy resolves real kernel headers through -I, including mutants.
    (work / 'io_work.c').write_text(product)
    (work / 'io.h').write_text('/* irq_save/restore supplied by io_work_host.c */\n')
    exe = work / 'host'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-Werror=vla', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
        '-nostdlib', '-static', '-no-pie', '-I' + str(work),
        '-I' + str(root / 'kernel'), '-I' + str(root / 'include'),
        '-I' + str(root / 'sdk/include/os32'),
        str(root / 'tools/tests/io_work_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def run(ctx):
    product = (ctx.root / 'kernel/io_work.c').read_text()
    with tempfile.TemporaryDirectory(prefix='io-work-') as td:
        work = Path(td)
        assert exercise(ctx.root, work, product) == 0, 'normal control'
        print('PASS dispatcher (ILP32, no libc)', flush=True)
        if ctx.mutate:
            for name, old, new in MUTANTS:
                assert product.count(old) == 1, name
                assert exercise(ctx.root, work, product.replace(old, new, 1)) == 1, name
                print('RED runtime: ' + name, flush=True)
    return 0


if __name__ == '__main__':
    from types import SimpleNamespace
    sys.exit(run(SimpleNamespace(root=Path(__file__).resolve().parents[2],
        mutate=os.environ.get('MUTATE') == '1' or '--mutate' in sys.argv)))
