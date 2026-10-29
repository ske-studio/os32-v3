"""Runner/diagnostic adapter versus frozen pre-P6 traces and bounded controls."""
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
import mutpar

ROOT = Path(__file__).resolve().parents[2]


def command(exe, focused=False):
    return [
        'gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Werror=vla',
        '-m32', '-march=i386', '-O2', '-ffreestanding', '-fno-pie',
        '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
        *(['-DGF_RUNNER_TRACE_ONLY'] if focused else []),
        *['-I' + str(ROOT / d) for d in ('.', 'include', 'sdk/include/os32')],
        str(ROOT / 'tools/tests/gf_runner_host.c'),
        *map(str, sorted((ROOT / 'hwdev/gf/op').glob('*.c'))), '-o', str(exe)
    ]


MUTATIONS = [
    ('diagnostic_a2_skipped', 'gf_init_a2(&e->init,runner_decode_safe)', '((void)runner_decode_safe,0)'),
    ('diagnostic_runner_skipped', 'int rc = gf_init_run_one(&e->init,e->program);', 'int rc = 0;'),
    ('diagnostic_restore_omitted', 'return e && e->recover ? gf_init_restore(&e->init) : 0;', '(void)e; return 0;'),
]


def mutation(item):
    name, old, new = item
    path = 'kdebug/gfinit_exec.c'
    source = (ROOT / path).read_text()
    if old and source.count(old) != 1:
        return name + ': ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gf-runner-mut-') as td:
        exe = Path(td) / 'host'
        try:
            tree = mutpar.build_in_tree(ROOT, td, {path: source.replace(old, new, 1) if old else source},
                                       [command(exe, True)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return name + ': compile failed ' + str(exc.stderr), 1
        result = host32.run([str(exe)], cwd=tree, timeout=120, capture_output=True)
        return name + (': RED' if result.returncode else ': GREEN'), int(not result.returncode)


def main():
    with tempfile.TemporaryDirectory(prefix='gf-runner-') as td:
        exe = Path(td) / 'host'
        subprocess.run(command(exe), cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=120)
        if result.returncode:
            return result.returncode
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation, MUTATIONS, ('control', '', ''))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
