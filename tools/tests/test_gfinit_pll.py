"""S4 integer PLL, production rule execution and link closure; synthetic tables only."""
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
            '-I'+str(ROOT/'include'), str(ROOT/'tools/tests/gfinit_pll_host.c'), '-o', str(exe)]


MUTATIONS = [
    ('pll_write_limit', 'if (q->count < GF_PLL_WRITES_MAX)', 'if (q->count <= GF_PLL_WRITES_MAX)'),
    ('pll_low_clock', 'min = clk * GF_PLL_P_MAX', 'min = clk << GF_PLL_P_MAX'),
    ('pll_input_exit', 'if (GF_PLL_REF / m < a->min_input) return s->actual ? 0 : -1;', 'if (GF_PLL_REF / m < a->min_input) break;'),
    ('pll_version', 'r[table + GF_PLL_HDR_VERSION] != GF_PLL_TBL_VERSION', 'false'),
    ('pll_ref', 'l->ref == GF_PLL_REF', 'true'),
    ('pll_round', '(cp * m + GF_PLL_REF / 2) / GF_PLL_REF', '(cp * m) / GF_PLL_REF'),
    ('pll_output_round', '((n * GF_PLL_REF + div / 2) / div + m / 2) / m', '(n * GF_PLL_REF / div) / m'),
    ('pll_gain', 'n2 / m2 < 4 || n2 / m2 > 10', 'false'),
    ('pll_mirror', 'GF_PLL_MEMORY_MIRROR, preg, keep, bits)', 'GF_PLL_MEMORY_MIRROR, preg, keep, bits ^ 1U)'),
    ('pll_gate_restore_video', '~0U, 0); /* video gate restore */', '~0U, 1U); /* video gate restore */'),
    ('pll_fallback_not_first', 'u32 at = table + header, found = 0;', 'u32 at = table + header + len, found = 0;'),
    ('pll_match_ignored', '        at = table + header + i * len;\n', '        (void)i;\n'),
    ('pll_gate_restore_clock', '~0U, 0); /* clock gate restore */', '~0U, 1U); /* clock gate restore */'),
]
SHARED_MUTATIONS = []


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
        obj = pathlib.Path(td)/'pll.o'
        subprocess.run(['gcc', *FLAGS, '-O2', '-I'+str(ROOT/'include'), '-c',
                        str(ROOT/'kdebug/gfinit_pll.c'), '-o', str(obj)], check=True)
        undefined = subprocess.check_output(['nm', '-u', str(obj)], text=True)
        if undefined.strip():
            raise RuntimeError('PLL unresolved symbols: '+undefined)
        print('PLL standalone ILP32 object: undefined symbols 0', flush=True)
        subprocess.run(command(exe), cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=120)
        if result.returncode:
            return result.returncode
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation,
            [('kdebug/gfinit_pll.c', *item) for item in MUTATIONS] +
            [('kdebug/recover.c', *item) for item in SHARED_MUTATIONS],
            ('kdebug/gfinit_pll.c', 'control', '', ''))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
