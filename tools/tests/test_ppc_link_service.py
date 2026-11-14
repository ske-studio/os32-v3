"""P1g foreground ownership, bind ordering and HELLO observation."""
CHECK = dict(name='ppc-link-service-host', tier=2, budget=30, needs=[], covers=[
    'build/ppc-kboot.mk', 'platform/powermac/lan_service.c',
    'platform/powermac/nic_gem.c', 'platform/powermac/rh_observe.c',
    'net/nic.c', 'tools/tests/test_ppc_link_service.py',
    'tools/tests/ppc_link_service_host.c','include/*.h', 'platform/powermac/*.h', 'arch/ppc/*.h',
            'net/*.h', 'mm/kmalloc.h', 'include/kprintf.h',
            'drivers/net/gem/gem.h', 'sdk/include/os32/*.h',
            'tools/tests/host32.py'])
from pathlib import Path
import subprocess
import tempfile
import host32


def run(ctx):
    source = (ctx.root / 'platform/powermac/lan_service.c').read_text()
    mutations = [
        ('if (!link_bound) {', 'if (!link_bound) { gem_rx(&s.g, 0, 0, 0);'),
        ('s.state = RUNNING; rh_log();',
         'net_adapter_register(PM_NET_NIC_GEM, &net_gem_adapter); s.state = RUNNING; rh_log();'),
        ('if (link_hello_ok) pm_rh_stage(PM_RH_LINK_HELLO_ESTABLISHED);', 'if (0) pm_rh_stage(PM_RH_LINK_HELLO_ESTABLISHED);')]
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        cases = [('baseline', source)]
        if ctx.mutate:
            for old, new in mutations:
                assert source.count(old) == 1, old
                cases.append((old, source.replace(old, new)))
        for name, text in cases:
            (work / 'lan_service.c').write_text(text)
            exe = work / 'probe'
            cmd = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-fno-pie', '-fno-builtin', '-fno-stack-protector', '-ffunction-sections',
                   '-fdata-sections', '-nostdlib', '-static', '-no-pie',
                   '-Wl,--gc-sections', '-I'+td]
            cmd += ['-I'+str(ctx.root / p) for p in
                    ['.', 'include', 'platform/powermac', 'arch/ppc', 'sdk/include',
                     'mm', 'lib']]
            subprocess.run(cmd + [str(ctx.root / 'tools/tests/ppc_link_service_host.c'),
                                  '-o', str(exe)], check=True)
            rc = host32.run([str(exe)], runner='qemu').returncode
            assert rc == (0 if name == 'baseline' else 10), (name, rc)
            print(('GREEN ' if name == 'baseline' else 'RED runtime ') + name)
    return 0
