"""T4b-2: real dispatcher and virtual normal trial, fake HAL, ILP32."""
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
import mutpar

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'kdebug/recover_virtual.c'
MUTATIONS = [
    ('synthetic_access_allowed',
     '(void)opaque; (void)a; (void)value;\n    return -1;',
     '(void)opaque; (void)a; (void)value;\n    return 0;'),
    ('plan_leave_skipped', 'return recover_plan_leave(r, 0);', 'return 0;'),
    ('transport_not_restored', 'r->transport = saved_transport;',
     '(void)saved_transport;'),
]


def build_cmd(temp):
    source = (ROOT / 'kdebug/kdebug_main.c').read_text().split('static void selftest_run')[0]
    source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
    (temp / 'virtual_dispatch.inc').write_text(source)
    return ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
            '-static', '-no-pie', '-ffunction-sections', '-Wl,--gc-sections',
            '-I'+str(temp), '-I'+str(ROOT / 'tools/tests/recover_hostshim'),
            '-I'+str(ROOT / 'include'),
            str(ROOT / 'tools/tests/recover_virtual_host.c'), '-o', str(temp / 'virtual')]


def one(item):
    name, before, after = item
    source = (ROOT / SOURCE).read_text()
    expected = 2 if name == 'synthetic_access_allowed' else 1
    if before and source.count(before) != expected:
        return name + ': missing/ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='recover-virtual-mut-') as directory:
        temp = Path(directory)
        try:
            tree = mutpar.build_in_tree(ROOT, directory,
                {SOURCE: source.replace(before, after) if before else source},
                [build_cmd(temp)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return name + ': COMPILE FAIL ' + str(exc.stderr), 1
        try:
            result = host32.run([str(temp / 'virtual')], cwd=str(tree),
                                timeout=30, capture_output=True)
        except subprocess.TimeoutExpired:
            return mutpar.timeout_red((name + ': runtime boundedness failure', 0))
        return name + (': GREEN' if not result.returncode else ': RED'), int(result.returncode == 0)


def main():
    with tempfile.TemporaryDirectory(prefix='recover-virtual-') as directory:
        temp = Path(directory)
        subprocess.run(build_cmd(temp), check=True)
        rc = host32.run([str(temp / 'virtual')], timeout=30).returncode
        if rc:
            return rc
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(one, MUTATIONS, ('control', '', ''))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
