"""Stage C: real shell/engine, ring consumption, serial and wire mutations."""
CHECK = dict(name='hwb-probe-host', tier=1, budget=9.4, needs=[], covers=[
    'userland/shell/hwb*.c', 'userland/shell/hwb.h', 'userland/shell/cmd_hw.c',
    'userland/shell/shell.h', 'sdk/include/os32/*.h', 'include/**',
    'kdebug/**', 'kernel/module/module_runtime.h', 'kernel/module/module_image.h',
    'drivers/char/serial*.h', 'drivers/pci/pci_hwb.h', 'exec/exec_hwb.h',
    'mm/paging.h', 'lib/kstring.h', 'arch/x86/arch_io.h', 'platform/pc98/platform_io.h',
    'tools/tests/hwb_probe_host.c', 'tools/tests/hwb_probe_engine.c',
    'tools/tests/hwb_shell_host.c', 'tools/tests/probe_host.c',
    'tools/tests/hwb_fake.h', 'tools/tests/hwb_host.py', 'tools/tests/host32.py',
    'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py'])
import sys
import tempfile
from pathlib import Path
import hwb_host as host
FIXTURE = 'tools/tests/hwb_probe_host.c'
SOURCES = ('tools/tests/hwb_probe_engine.c',)
MUTATIONS = [
    ('consume', 'kdebug/probe.c', 'used--; drain_item = 0;',
     'used = 0; drain_item = 0;', 'probe-drain'),
    ('serial', 'kdebug/probe.c', 'if (serial)\n            kprintf',
     'if (false && serial)\n            kprintf', 'probe-serial'),
    ('wire-size', 'userland/shell/hwb_probe.c',
     'w->size != PROBE_WIRE_BYTES || w->version != PROBE_VERSION',
     'w->version != PROBE_VERSION', 'probe-wire'),
]
def main():
    with tempfile.TemporaryDirectory(prefix='hwb-probe-') as d:
        p = Path(d)
        (p/'string.h').write_text('int strcmp(const char *,const char *);\n')
        (p/'hwb_shell_scripts.h').write_text('struct script_data {const char *name,*source;u32 bytes;};\nstatic const struct script_data scripts_data[]={{"empty","end",3}};\n')
        flags = ('-I'+d, '-I'+str(host.ROOT/'sdk/include/os32'), '-D__OS32_USERLAND__', '-D__KERNEL_BUILD__', '-Wno-attributes')
        host.verify(FIXTURE,sources=SOURCES,flags=flags)
        if '--mutate' in sys.argv:
            return host.mutations(FIXTURE,MUTATIONS,sources=SOURCES,flags=flags)
    return 0

def run(ctx):
    import os
    import subprocess
    return subprocess.call([sys.executable,'-B',__file__, *(['--mutate'] if ctx.mutate else [])],
        cwd=ctx.root,env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners)))
if __name__ == '__main__': sys.exit(main())
