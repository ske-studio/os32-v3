"""S2 checked decoder, real layouts and synthetic inputs; no external ROM."""
import pathlib
import subprocess
import sys
import tempfile
import host32
import mutpar

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-m32', '-march=i386',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector']


def command(exe):
    return ['gcc', *FLAGS, '-O2', '-nostdlib', '-static', '-no-pie',
            '-I'+str(ROOT/'include'), str(ROOT/'tools/tests/gfinit_host.c'), '-o', str(exe)]


MUTATIONS = [
    ('repeat_zero', 'if (!o->count || o->count >= 255)', 'if (o->count >= 255)'),
    ('repeat_c_overflow', 'if (!o->count || o->count >= 255)', 'if (!o->count)'),
    ('unknown_opcode', 'default: return -1;\n    }\n    if (span', 'default: fixed = 1; break;\n    }\n    if (span'),
    ('rom_crc', 'if (crc32_core_final(crc) != s->crc)', 'if (false && crc32_core_final(crc) != s->crc)'),
    ('copy_shift', 'if (!shift_ok(o->shift) || o->ref & 3U)', 'if ((!shift_ok(o->shift) && false) || o->ref & 3U)'),
    ('table_index', 'if (entry >= count)', 'if (false && entry >= count)'),
    ('call_depth', 'calls > GF_INIT_CALL_DEPTH', 'false'),
    ('repeat_depth', 'repeats > GF_INIT_REPEAT_DEPTH', 'false'),
    ('selection_coverage', '|| (o->mask >> o->shift) >= o->count', ''),
    ('sequence_overflow', 'o->count - 1 > (0xffffffffU - o->target) / 4', 'false'),
]

SHARED_MUTATIONS = [
    ('capture_ordinal', 'c->read_ordinal)', '0)'),
    ('capture_dominance', '!bit_get(in, plan->captures[j].producer_node)', 'false'),
    ('capture_consumed', 'r->capture_valid &= ~(1U << i);', 'r->capture_valid |= 1U << i;'),
    ('unresolved_bind', '!plan->entries || plan->unresolved_count ||', '!plan->entries ||'),
]


def mutation(item):
    file, name, old, new = item
    source = (ROOT/file).read_text()
    if old and source.count(old) != 1:
        return f'{name}: ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gfinit-mut-') as td:
        exe = pathlib.Path(td)/'host'
        try:
            tree = mutpar.build_in_tree(ROOT, td, {file: source.replace(old, new, 1) if old else source},
                                       [command(exe)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return f'{name}: compile failed {exc.stderr!r}', 1
        result = host32.run([str(exe)], cwd=tree, timeout=60, capture_output=True)
        return f'{name}: {"GREEN" if result.returncode == 0 else "RED"} rc={result.returncode}', int(result.returncode == 0)


def main():
    with tempfile.TemporaryDirectory(prefix='gfinit-') as td:
        exe = pathlib.Path(td)/'host'
        subprocess.run(command(exe), cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=120)
        if result.returncode:
            return result.returncode
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation,
            [('kdebug/gfinit_decode.c', *item) for item in MUTATIONS] +
            [('kdebug/recover.c', *item) for item in SHARED_MUTATIONS],
            ('kdebug/gfinit_decode.c', 'control', '', ''))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
