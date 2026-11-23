"""PowerMac HW_OP §5 stage 5a, R3-P1/P2. Exercise the real exec session."""
import subprocess
import sys

import hwb_host
from test_exec_hwb_abort import FLAGS

CHECK = dict(name='ppc-hwb-session', tier=1, needs=[], budget=6,
    covers=['exec/**', 'gfx/**', 'mm/**', 'input/**', 'init/**', 'kernel/**',
        'include/**', 'arch/x86/**', 'platform/pc98/**', 'sdk/include/os32/**',
        'kapi/**', 'fs/**', 'lib/**', 'kdebug/hwb_wire.h', 'userland/shell/hwb.h',
        'tools/tests/host_arch/**', 'tools/tests/host_exec*.h', 'tools/tests/hwb_fake.h',
        'tools/tests/exec_hwb_abort_host.c', 'tools/tests/hwb_host.py', 'tools/tests/host32.py',
        'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py', 'build/config.mk',
        'tools/tests/test_exec_hwb_abort.py', 'tools/tests/ppc_hwb_session_host.c',
        'kernel/module/kdebug_call.c', 'kernel/module/*.h',
        'platform/powermac/platform_hwb_session.h', 'platform/powermac/terminal_fifo.inc', 'platform/powermac/terminal_input.c',
        'platform/powermac/lan_boot.h', 'platform/powermac/usb_boot.h',
        'kernel/idle_work.h', 'kernel/cli/resident_cli.h', 'arch/ppc/kernel_boot.h'],
    basis=['docs/decisions/realhw/HWOP_COMMON_DESIGN.md §2.2'])
FIXTURE = 'tools/tests/ppc_hwb_session_host.c'
SESSION_FLAGS = FLAGS + ('-DPM_LOAD_KDEBUG=1', '-DKBOOT_SH=1', '-nostdinc', '-isystem',
                         subprocess.check_output(['gcc', '-print-file-name=include'], text=True).strip())
MUTATIONS = [('sticky', 'exec/exec_abort.inc',
              'u32 pending = g_hwb_abort_pending;\n    g_hwb_abort_pending = 0;',
              'u32 pending = g_hwb_abort_pending;', 'take-clear')]

if __name__ == '__main__':
    hwb_host.verify(FIXTURE, flags=SESSION_FLAGS)
    if '--mutate' in sys.argv:
        raise SystemExit(hwb_host.mutations(FIXTURE, MUTATIONS, flags=SESSION_FLAGS))


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
