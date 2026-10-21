"""GF rule evidence and real recover trace, without real opcode layouts (S2)."""
import os
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
    return ['gcc', *FLAGS, '-O0', '-nostdlib', '-static', '-no-pie',
            '-I'+str(ROOT/'include'), str(ROOT/'tools/tests/recover_gf_host.c'),
            '-o', str(exe)]

MUTATIONS = [
    ('n1_before', 'kdebug/recover.c', 'if (!guards(r, w, REC_BEFORE))',
     'if (false && !guards(r, w, REC_BEFORE))'),
    ('n1_after', 'kdebug/recover.c', 'if (!guards(r, w, REC_AFTER))',
     'if (false && !guards(r, w, REC_AFTER))'),
    ('alias', 'kdebug/recover.c', '? RECOVER_PRMCIO : RECOVER_PRMVIO',
     '? RECOVER_PRMVIO : RECOVER_PRMVIO'),
    ('index_data', 'kdebug/recover.c', 't->space == REC_VGA_INDEXED && dir != REC_SELECT',
     't->space == REC_VGA_INDEXED && dir == REC_SELECT'),
    ('dependency', 'kdebug/recover_gf.c', 'static const u32 dep_1[] = {0};',
     'static const u32 dep_1[] = {2};'),
    ('n1_cfg_guard', 'kdebug/recover_gf.c',
     '{0, 0xffffffffU, 0, REC_BEFORE | REC_AFTER}', '{0, 0xffffffffU, 0, REC_BEFORE}'),
    ('unclassified', 'kdebug/recover.c',
     'if (!w || !node_rule(n, w) || w->key != args[0].value || n->expr_id != w->value.expr_id ||',
     'if (!w) return true;\n    if (!node_rule(n, w) || w->key != args[0].value || n->expr_id != w->value.expr_id ||'),
]

def mutation(item):
    name, file, old, new = item
    source = (ROOT/file).read_text()
    if old and source.count(old) != 1:
        return f'{name}: ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gf-rule-mut-') as td:
        exe = pathlib.Path(td)/'host'
        try:
            tree = mutpar.build_in_tree(ROOT, td, {file: source.replace(old, new, 1) if old else source},
                                       [command(exe)], capture_output=True)
        except subprocess.CalledProcessError as e:
            return f'{name}: compile failed {e.stderr!r}', 1
        result = host32.run([str(exe)], cwd=tree, timeout=60, capture_output=True)
        color = 'GREEN' if result.returncode == 0 else 'RED'
        return f'{name}: {color} rc={result.returncode}', int(result.returncode == 0)

def main():
    with tempfile.TemporaryDirectory(prefix='gf-rule-') as td:
        exe = pathlib.Path(td)/'host'
        subprocess.run(command(exe), cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=60)
        if result.returncode:
            return result.returncode
        subprocess.run([str(pathlib.Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc'),
                        *FLAGS, '-O2', '-I'+str(ROOT/'include'), '-c',
                        str(ROOT/'kdebug/recover_gf.c'), '-o', str(pathlib.Path(td)/'profile.o')], check=True)
        print('TARGET GF profile compile PASS', flush=True)
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation, MUTATIONS,
                         ('control', 'kdebug/recover_gf.c', '', ''))))
    return 0

if __name__ == '__main__':
    sys.exit(main())
