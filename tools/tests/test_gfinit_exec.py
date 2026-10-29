"""S3 complete mock traces, bounded execution and fail-stop; no external ROM."""
import pathlib
import subprocess
import sys
import tempfile
import host32
import mutpar

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-m32', '-march=i386',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector']


def command(exe, probe=False):
    return ['gcc', *FLAGS, *(['-DGF_VPLL_PROBE=1'] if probe else []), '-O2', '-nostdlib', '-static', '-no-pie',
            '-I'+str(ROOT/'include'), '-I'+str(ROOT/'sdk/include/os32'), str(ROOT/'tools/tests/gfinit_exec_host.c'), *[str(ROOT / p) for p in ('hwdev/gf/runner.c','hwdev/gf/save.c','hwdev/gf/pll.c')], *map(str, sorted((ROOT/'hwdev/gf/op').glob('*.c'))), '-o', str(exe)]


MUTATIONS = [
    ('approval_main', 'n->main_id != e->main_id', 'false'),
    ('runner_enter', 'recover_plan_enter(r,r->expected_node) ? -1 : 0;', '0 ? -1 : 0;'),
    ('exec_timeout_reason', 'e->recover->reason = REC_TIMEOUT;', 'e->recover->reason = REC_OPERATION;'),
]


SHARED_MUTATIONS = [
    ('kdebug/recover_plan/access.inc', 'write_readback_unknown',
     'rc || write ? RECOVER_NONE : *value', 'rc ? RECOVER_NONE : *value'),
]

def mutation(item):
    file, name, old, new = item
    source = (ROOT/file).read_text()
    if old and source.count(old) != 1:
        return f'{name}: ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gfinit-mut-') as td:
        exe = pathlib.Path(td)/'host'
        try:
            mutated = source.replace(old, new, 1) if old else source
            if name == 'pre_read_order':
                mutated = mutated.replace(PLANNED, PLANNED + PRECHECK, 1)
            tree = mutpar.build_in_tree(ROOT, td, {file: mutated},
                                       [command(exe, name in ("vpll_read", "vpll_repeat_drop", "vpll_verify_zero"))], capture_output=True)
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
        subprocess.run(command(exe, True), cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=120)
        if result.returncode:
            return result.returncode
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation,
            [('kdebug/gfinit_exec.c', *item) for item in MUTATIONS] + SHARED_MUTATIONS,
            ('kdebug/gfinit_exec.c', 'control', '', ''))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
