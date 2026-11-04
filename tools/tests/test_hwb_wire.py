"""HW-BASIC a0 contracts and shell-backend substrate self-test (no libc)."""
CHECK = dict(name='hwb-wire-host', tier=1, budget=2.0, needs=[],
    covers=['kdebug/hwb_wire.h', 'userland/shell/hwb.h',
            'drivers/pci/pci_hwb.h', 'exec/exec_hwb.h',
            'tools/tests/hwb_fake.h', 'tools/tests/hwb_host.py',
            'tools/tests/hwb_wire_host.c', 'tools/tests/host32.py',
            'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py',
            'include/types.h', 'sdk/include/os32/os32_kapi_shared.h',
            'kdebug/recover_wire.h', 'kdebug/gfinit_wire.h',
            'kdebug/gf_foundation_wire.h', 'kdebug/gf_command_wire.h'])

import sys
import hwb_host

FIXTURE = 'tools/tests/hwb_wire_host.c'
MUTATIONS = [
    ('bdf-device-shift', 'kdebug/hwb_wire.h', '#define HW_BDF_DEV_SHIFT 3U',
     '#define HW_BDF_DEV_SHIFT 4U', 'bdf'),
    ('wire-write-action', 'kdebug/hwb_wire.h', 'HW_WRITE = 2', 'HW_WRITE = 4', 'wire'),
    ('call-depth', 'userland/shell/hwb.h', '#define HWB_CALL_DEPTH 8U',
     '#define HWB_CALL_DEPTH 7U', 'limits'),
]


def main():
    hwb_host.verify(FIXTURE, flags=['-D__KERNEL_BUILD__=1'])
    hwb_host.verify(FIXTURE, flags=['-DHWB_SHELL_HEADERS=1'])
    if '--mutate' in sys.argv:
        return int(bool(hwb_host.mutations(FIXTURE, MUTATIONS, flags=['-D__KERNEL_BUILD__=1'])))
    return 0


if __name__ == '__main__':
    sys.exit(main())


def run(ctx):
    import os
    import subprocess
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_hwb_wire.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)
