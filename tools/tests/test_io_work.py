#!/usr/bin/env python3
"""IO_TIER_DESIGN §8 rows 1–4: execute the actual dispatcher, including mutants."""
CHECK = dict(name='io-work-host', tier=3, budget=20, needs=[], covers=[
    'kernel/io_work.c', 'kernel/io_work.h', 'kernel/io_clock.h', 'kernel/io_policy.h',
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
    ('Tier order', 'i = first(pending[t] & ~visited);', 'i = first(pending[2 - t] & ~visited);'),
    ('fair cursor', 'cursor[fair] = ((unsigned int)i + 1) % IO_SLOTS;', 'cursor[fair] = 0;'),
    ('Tier fair cursor', 'fair_next = (unsigned int)fair ^ 1u;', 'fair_next = 0;'),
    ('carry right', 'rounds[t] = minimum(rounds[t] + 1, IO_FAIR_ROUNDS);',
     'rounds[t] = (rounds[t] + 1) % IO_FAIR_ROUNDS;'),
    ('compat empty', 'compat_count == 0 && !any_pending()', '!any_pending()'),
    ('begin count', 'convert(budget_us, next.clock_source', 'convert(budget_us + 1, next.clock_source'),
    ('empty clock', 'if (compat_count == 0 && !any_pending()) return 0;',
     'if (compat_count == 0 && !any_pending()) { u32 a, b, c; raw_read(&a, &b, &c); return 0; }'),
    ('token generation', 'source != s->clock_source || gen != s->clock_generation', 'source != s->clock_source'),
    ('running', 'if (running) return 0;', 'if (0) return 0;'),
    ('new request', 'if (result < 0) stats.last_error = result;',
     'pending[0] = pending[1] = pending[2] = 0; if (result < 0) stats.last_error = result;'),
    ('visited', 'visited |= 1u << i;\n        int result', 'visited |= 0;\n        int result'),
    ('clock failure', 'if (!valid || clock_failed) break;', 'clock_failed = 0;'),
]


def exercise(root, work, product):
    # Local product copy resolves real kernel headers through -I, including mutants.
    (work / 'io_work.c').write_text(product.replace('/* A5_COMPAT_BODY */', '++compat_hits;'))
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
