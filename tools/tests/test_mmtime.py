"""ILP32 freestanding multimedia timer contracts and runtime mutants."""
from pathlib import Path
import subprocess
import sys
import tempfile
from host32 import run as host_run

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('mmtime_clock.c', 'mmtime_event.c', 'mmtime_sleep.c', 'mmtime.h')
# Every anchor is unique in its file; compiler failures never count as RED.
MUTATIONS = (
    ('monotonic-max', 'mmtime_clock.c',
     'if (value < last_out) value = last_out;', '(void)last_out;'),
    ('tick-wrap', 'mmtime_clock.c',
     '(unsigned long long)(u32)(kapi->get_tick() - base_tick)',
     '((unsigned long long)kapi->get_tick() - base_tick)'),
    ('born-pass', 'mmtime_event.c',
     'e->born_pass == pass', '0'),
    ('catchup-nine', 'mmtime_event.c',
     '++calls == MMT_CATCHUP_MAX', '++calls == MMT_CATCHUP_MAX + 1'),
    ('deadline-phase', 'mmtime_event.c',
     'set_deadline(e, next + n * e->delay_us);', 'set_deadline(e, now);'),
    ('oneshot-free-after', 'mmtime_event.c',
     'free_event(e, i);', 'cb(id, 0, user, 0, 0); free_event(e, i); break;'),
    ('callback-recheck', 'mmtime_event.c',
     'if (e->id != id || !e->live || e->dying) break;', 'if (0) break;'),
    ('dying-next-due', 'mmtime_event.c',
     'if (!e->live || e->dying) continue;', 'if (!e->live) continue;'),
    ('reentrant-delivery', 'mmtime_event.c',
     'if (delivering || hold) return;', 'if (hold) return;'),
    ('nested-hold', 'mmtime_event.c', 'if (on) ++hold;', 'if (on) hold = 1;'),
    ('hold-underflow', 'mmtime_event.c', 'else if (hold) --hold;', 'else --hold;'),
    ('next-due-ceil', 'mmtime_event.c', '+ 999u) / 1000u', '+ 0u) / 1000u'),
    ('oneshot-stale-id', 'mmtime_event.c',
     'e->id += MMT_EVENT_MAX;', 'e->id += 0;'),
    ('held-dying-capacity', 'mmtime_event.c',
     'if (e->live) continue;', 'if (e->live && !e->dying) continue;'),
    ('sleep-zero-yield', 'mmtime_sleep.c',
     'if (!ms) {', 'if (!ms) { return;'),
)


def verify(work, mutation=None):
    dest = work / 'userland/lib/mmtime'
    dest.mkdir(parents=True, exist_ok=True)
    for name in SOURCES:
        source = (ROOT / 'userland/lib/mmtime' / name).read_text()
        if mutation and mutation[1] == name:
            _, _, old, new = mutation
            assert source.count(old) == 1, mutation
            source = source.replace(old, new)
        (dest / name).write_text(source)
    exe = work / 'test'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                    '-nostdlib', '-static', '-no-pie', '-D__cdecl=',
                    '-I' + str(work), '-I' + str(ROOT / 'sdk/include/os32'),
                    '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'arch/x86'),
                    str(ROOT / 'tools/tests/mmtime_host.c'), '-o', str(exe)], check=True)
    return host_run([str(exe)], cwd=work, timeout=3).returncode


def main(mutate=False):
    with tempfile.TemporaryDirectory(prefix='mmtime-') as td:
        work = Path(td)
        assert verify(work) == 0
        print('PASS mmtime ILP32', flush=True)
        if mutate:
            for mutation in MUTATIONS:
                rc = verify(work, mutation)
                assert rc == 1, (mutation[0], rc)
                print('RED runtime', mutation[0], 'rc=', rc, flush=True)
    return 0


def run(ctx):
    return main(ctx.mutate)


if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))
