"""T4b-1: real op 11 dispatcher, 40-byte wire rejection, no HAL calls."""
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
import mutpar

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'kdebug/kdebug_main.c'
INCLUDES = ('.', 'include', 'kernel', 'lib', 'fs', 'arch/x86', 'platform/pc98',
            'sdk/include', 'sdk/include/os32', 'exec', 'gfx', 'drivers', 'kapi', 'kdebug')

MUTATIONS = [
    ('route_dispatch_now', 'recover_route_test_session(r, out->session) :', 'recover_now_session(r, out->session) :'),
    ('route_gf_ops_accepted', '(r->ops && r->ops != &recovery_ops)', 'false'),
    ('reserved_unchecked', 'if (req->reserved[i]) return KDEBUG_RECOVER_VIRTUAL_INVALID;',
     'if (req->reserved[i] && 0) return KDEBUG_RECOVER_VIRTUAL_INVALID;'),
    ('unknown_case_accepted', 'req->case_id != RECOVER_VIRTUAL_TXBUSY_STOP)\n            return KDEBUG_RECOVER_VIRTUAL_UNSUPPORTED;',
     'req->case_id != RECOVER_VIRTUAL_TXBUSY_STOP && 0)\n            return KDEBUG_RECOVER_VIRTUAL_UNSUPPORTED;'),
    ('virtual_falls_to_kpeek', 'if (op == KDEBUG_OP_RECOVER_VIRTUAL) {\n        if (req->action',
     'if (0) {\n        if (req->action'),
]


def build_cmd(exe):
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
            '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-D__KERNEL_BUILD__', '-nostdlib',
            '-static', '-no-pie', '-DRECOVER_ROUTE_WIRE',
            '-I'+str(ROOT / 'tools/tests/recover_hostshim'), *['-I'+str(ROOT / x) for x in INCLUDES],
            str(ROOT / 'tools/tests/recover_wire_host.c'),
            str(ROOT / SOURCE), '-o', str(exe)]


def one(item):
    name, before, after = item
    source = (ROOT / SOURCE).read_text()
    if before and source.count(before) != 1:
        return name + ': missing/ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='recover-wire-mut-') as directory:
        exe = Path(directory) / 'wire'
        try:
            tree = mutpar.build_in_tree(ROOT, directory,
                {SOURCE: source.replace(before, after, 1) if before else source},
                [build_cmd(exe)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return name + ': COMPILE FAIL ' + str(exc.stderr), 1
        try:
            result = host32.run([str(exe)], cwd=str(tree), timeout=30, capture_output=True)
        except subprocess.TimeoutExpired:
            return name + ': runtime boundedness failure', 0
        return name + (': GREEN' if result.returncode == 0 else ': RED rc=' + str(result.returncode)), int(result.returncode == 0)


def main():
    with tempfile.TemporaryDirectory(prefix='recover-wire-') as directory:
        exe = Path(directory) / 'wire'
        subprocess.run(build_cmd(exe), check=True)
        result = host32.run([str(exe)], timeout=30)
        if result.returncode:
            return result.returncode
        print('recover wire: 40B/version/reserved/size/attach-effects/PAUSED PASS')
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(one, MUTATIONS, ('control', '', ''))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
