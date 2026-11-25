"""KDBG stage B: real capture engine and aperture with fake IO/broker."""
CHECK = dict(name='probe-host', tier=1, covers=[
    'sys/dev/char/kdebug/probe.c', 'sys/kern/kdebug/probe_index.c', 'sys/kern/kdebug/probe_wire.h', 'sys/kern/kdebug/probe_validate.h',
    'sys/kern/kdebug/hwb_access.c', 'sys/kern/kdebug/hwb_wire.h', 'sys/arch/x86/kdebug/kdebug_window.c',
    'sys/arch/x86/kdebug/kdebug_window.h', 'sys/dev/video/geforce/diag/recover.h', 'kdebug/generated/**',
    'sys/include/debug_probe.h', 'sys/kern/module/module_runtime.h',
    'sys/include/types.h', 'sys/include/io.h', 'sys/include/memmap.h', 'sys/kern/kstring.h',
    'sys/kern/module/module_image.h',
    'sys/dev/char/serial.h', 'sys/dev/char/serial_plan.h', 'sys/include/kprintf.h',
    'sys/dev/pci/pci_hwb.h', 'sys/kern/exec/exec_hwb.h', 'sys/kern/mm/paging.h',
    'tools/tests/probe_host.c', 'tools/tests/hwb_fake.h', 'tools/tests/hwb_host.py',
    'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
    'userland/shell/hwb.h', 'sdk/include/os32/os32_kapi_shared.h',
    'sys/platform/pc98/platform_io.h', 'sys/arch/x86/arch_io.h'], needs=[], budget=4.6,
    basis=['KDBG_PROBE_DESIGN'],
)
import sys
import tempfile
from pathlib import Path
import hwb_host as host
FIXTURE = 'tools/tests/probe_host.c'
FLAGS = ('-D__KERNEL_BUILD__', '-Wno-attributes')
MUTATIONS = [
    ('width', 'sys/kern/kdebug/probe_index.c',
     '(p->width != 1 && p->width != 2 && p->width != 4)', '0', 'width'),
    ('full', 'sys/dev/char/kdebug/probe.c', 'if (used == PROBE_SNAPSHOT_MAX)', 'if (0)', 'overflow'),
    ('stop', 'sys/dev/char/kdebug/probe.c',
     'if (armed) debug_probe_control(PROBE_DETACH, &owner, sizeof(owner));',
     'if (false) debug_probe_control(PROBE_DETACH, &owner, sizeof(owner));', 'capacity'),
    ('epoch', 'sys/dev/char/kdebug/probe.c', '!setting_count || epoch == list->epoch',
     '!setting_count || (epoch == list->epoch || true)', 'epoch'),
    ('restore', 'sys/kern/kdebug/probe_index.c',
     'if (w.ops->store(w.opaque, index_va, 1, old))',
     'if (((void)old, 0))', 'pair-restore'),
    ('if-restore', 'sys/kern/kdebug/probe_index.c', 'irq_restore(flags);',
     '(void)flags;', 'pair'),
    ('data-destination', 'sys/kern/kdebug/probe_index.c',
     'p->address == p->data_address', 'false', 'data-destination'),
    ('failure-disable', 'sys/dev/char/kdebug/probe.c', 'p->enabled = 0;',
     'p->enabled = 1;', 'failure-stop'),
]
def main():
    host.verify(FIXTURE, flags=FLAGS)
    with tempfile.TemporaryDirectory(prefix='probe-target-') as d:
        for source in ('probe.c', 'probe_index.c'):
            host.build(host.ROOT, 'kdebug/' + source, Path(d)/(source+'.o'), cross=True, flags=FLAGS)
    if '--mutate' in sys.argv:
        return host.mutations(FIXTURE, MUTATIONS, flags=FLAGS)
    return 0

def run(ctx):
    import os
    import subprocess
    return subprocess.call([sys.executable, '-B', __file__,
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))

if __name__ == '__main__':
    sys.exit(main())
