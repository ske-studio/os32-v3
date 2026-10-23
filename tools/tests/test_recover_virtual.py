"""T4b-8: real dispatcher and virtual normal/fault/trial-expire/poll-stop/txbusy-stop trials, fake HAL, ILP32."""
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
import mutpar

ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'kdebug/recover_virtual.c'
MUTATIONS = [
    ('uart_poll_limit_removed', 'if (r->uart_polls >= max_polls) break;',
     'if (r->uart_polls >= max_polls && 0) break;'),
    ('tx_injection_not_cleared', 'tx_busy = false;', '(void)tx_busy;'),
    ('step_poll_limit_removed',
     '++r->polls > r->profile->max_polls)',
     '(++r->polls > r->profile->max_polls && 0))'),
    ('freeze_offset_omitted',
     'clock_offset += saved_ops->ticks(opaque) - freeze_real;',
     'clock_offset += (saved_ops->ticks(opaque) - freeze_real) * 0U;'),

    ('trial_deadline_removed', 'if (r->spent >= r->budget ||',
     'if ((r->spent >= r->budget && 0) ||'),
    ('short_trial_omitted', 'profile.trial_limit = VIRTUAL_TRIAL_SECONDS * hz;',
     'profile.trial_limit = profile.opcode_limit;'),
    ('step_failure_ignored',
     'if (rc) {\n        fail(r, r->reason == REC_TIMEOUT ? REC_TIMEOUT : REC_OPERATION,',
     'if (rc && 0) {\n        fail(r, r->reason == REC_TIMEOUT ? REC_TIMEOUT : REC_OPERATION,'),
    ('inject_begin_omitted',
     'if (recover_record(r, "inject", "BEGIN", active_case, 0, 0, 0)) return -1;',
     '(void)active_case;'),
    ('paused_gate_removed',
     'if (r->state == REC_PAUSED && op != KDEBUG_OP_RECOVER_NOW &&',
     'if (0 && r->state == REC_PAUSED && op != KDEBUG_OP_RECOVER_NOW &&'),
    ('foreign_owner_allowed', 'r->ops == &ops &&', '((void)r, 1) &&'),

    ('synthetic_access_allowed',
     '(void)opaque; (void)a; (void)value;\n    return -1;',
     '(void)opaque; (void)a; (void)value;\n    return 0;'),
    ('plan_leave_skipped', 'return recover_plan_leave(r, 0);', 'return 0;'),
    ('transport_not_restored', 'r->transport = saved_transport;',
     '(void)saved_transport;'),
]


def build_cmd(temp, dispatch=None, expire=0):
    source = (dispatch if dispatch is not None else (ROOT / 'kdebug/kdebug_main.c').read_text()).split('static void selftest_run')[0]
    source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
    (temp / 'virtual_dispatch.inc').write_text(source)
    return ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            '-DTEST_EXPIRE='+str(expire), '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
            '-static', '-no-pie', '-ffunction-sections', '-Wl,--gc-sections',
            '-I'+str(temp), '-I'+str(ROOT / 'tools/tests/recover_hostshim'),
            '-I'+str(ROOT / 'include'),
            str(ROOT / 'tools/tests/recover_virtual_host.c'), '-o', str(temp / ('virtual'+str(expire)))]


def one(item):
    name, before, after = item
    path = {'paused_gate_removed': 'kdebug/kdebug_main.c',
            'step_failure_ignored': 'kdebug/recover.c',
            'trial_deadline_removed': 'kdebug/recover.c',
            'step_poll_limit_removed': 'kdebug/recover.c',
            'uart_poll_limit_removed': 'kdebug/recover.c'}.get(name, SOURCE)
    source = (ROOT / path).read_text()
    expected = 2 if name == 'synthetic_access_allowed' else 1
    if before and source.count(before) != expected:
        return name + ': missing/ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='recover-virtual-mut-') as directory:
        temp = Path(directory)
        changed = source.replace(before, after) if before else source
        try:
            tree = mutpar.build_in_tree(ROOT, directory,
                {path: changed},
                [build_cmd(temp, changed if path == 'kdebug/kdebug_main.c' else None, expire)
                 for expire in (0, 1, 2, 3)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return name + ': COMPILE FAIL ' + str(exc.stderr), 1
        try:
            for expire in (0, 1, 2, 3):
                result = host32.run([str(temp / ('virtual'+str(expire)))], cwd=str(tree),
                                    timeout=30, capture_output=True)
                if result.returncode:
                    return name + ': RED', 0
        except subprocess.TimeoutExpired:
            # Retried once by mutpar; a second timeout still counts as survived.
            return mutpar.timeout_red((name + ': TIMEOUT (watchdog failed)', 1))
        return name + ': GREEN', 1


def main():
    with tempfile.TemporaryDirectory(prefix='recover-virtual-') as directory:
        temp = Path(directory)
        for expire in (0, 1, 2, 3):
            subprocess.run(build_cmd(temp, expire=expire), check=True)
            rc = host32.run([str(temp / ('virtual'+str(expire)))], timeout=30).returncode
            if rc:
                return rc
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(one, MUTATIONS, ('control', '', ''))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
