"""KDBG stage B: real capture engine and aperture with fake IO/broker."""
CHECK = dict(name='probe-host', tier=1, covers=[
    'kdebug/probe.c', 'kdebug/probe_index.c', 'kdebug/probe_wire.h',
    'kdebug/hwb_access.c', 'kdebug/hwb_wire.h', 'kdebug/kdebug_window.c',
    'kdebug/kdebug_window.h', 'kdebug/recover.h', 'kdebug/generated/**',
    'include/debug_probe.h', 'kernel/module/module_runtime.h',
    'include/types.h', 'include/io.h', 'include/memmap.h', 'lib/kstring.h',
    'kernel/module/module_image.h',
    'drivers/char/serial.h', 'drivers/char/serial_plan.h', 'include/kprintf.h',
    'drivers/pci/pci_hwb.h', 'exec/exec_hwb.h', 'mm/paging.h',
    'tools/tests/probe_host.c', 'tools/tests/hwb_fake.h', 'tools/tests/hwb_host.py',
    'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
    'userland/shell/hwb.h', 'sdk/include/os32/os32_kapi_shared.h',
    'platform/pc98/platform_io.h', 'arch/x86/arch_io.h'], needs=[], budget=4.6)
import sys
import tempfile
from pathlib import Path
import hwb_host as host
FIXTURE = 'tools/tests/probe_host.c'
FLAGS = ('-D__KERNEL_BUILD__', '-Wno-attributes')
MUTATIONS = [
    ('width', 'kdebug/probe_index.c',
     '(p->width != 1 && p->width != 2 && p->width != 4)', '0', 'width'),
    ('full', 'kdebug/probe.c', 'if (used == PROBE_SNAPSHOT_MAX)', 'if (0)', 'overflow'),
    ('stop', 'kdebug/probe.c',
     'if (armed) debug_probe_control(PROBE_DETACH, &owner, sizeof(owner));',
     'if (false) debug_probe_control(PROBE_DETACH, &owner, sizeof(owner));', 'capacity'),
    ('epoch', 'kdebug/probe.c', '!setting_count || epoch == list->epoch',
     '!setting_count || (epoch == list->epoch || true)', 'epoch'),
    ('restore', 'kdebug/probe_index.c',
     'if (w.ops->store(w.opaque, index_va, 1, old))',
     'if (((void)old, 0))', 'pair-restore'),
    ('if-restore', 'kdebug/probe_index.c', 'irq_restore(flags);',
     '(void)flags;', 'pair'),
    ('data-destination', 'kdebug/probe_index.c',
     'p->address == p->data_address', 'false', 'data-destination'),
    ('failure-disable', 'kdebug/probe.c', 'p->enabled = 0;',
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
