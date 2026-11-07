#!/usr/bin/env python3
"""I1 resident SFS boot wiring, borrowed lifetime and common peer (ILP32)."""
CHECK = dict(name='hostsvc-init', tier=1, needs=[], budget=5, covers=[
    'init/kernel.c', 'init/kernel_boot_memory.inc', 'init/sysconfig.h', 'init/boot_platform.h',
    'include/config.h', 'include/types.h', 'include/kprintf.h', 'include/net_eth.h',
    'fs/serialfs/serialfs.h', 'fs/serialfs/sfs_lan.h', 'fs/serialfs/sfs_lan.c',
    'fs/serialfs/sfs_client.h', 'fs/serialfs/sfs_proto.h', 'fs/vfs.h',
    'net/nic.h', 'lib/kstring.h', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/hostsvc_init_host.c', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'build/config.mk'])
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('legacy-before-start', 'boot_platform_run(BOOT_NIC_LEGACY, boot);', '(void)boot;'),
    ('FD-config', 'int enabled = sysconfig_get_int(SYS_NETCD_CFG, "SFS_LAN", 0) == 1;',
     'int enabled = 0;'),
    ('peer', 'have_peer ? boot_sfs_lan.peer : 0', '0'),
    ('resident', 'rc = sfs_persistent_start(&cfg);', '(void)cfg; rc = 0;'),
    ('clock', 'return tick_count;', 'return 0;'),
    ('init', 'sfs_lan_init(&boot_sfs_lan, 0, &boot_sfs_clock, 0);',
     '(void)boot_sfs_clock;'),
    ('restore-bind', '        net_start(NET_NIC_AUTO, 0);', '        (void)0;'),
)
def build_run(work, source):
    (work / 'hostsvc_init_source.inc').write_text(source)
    exe = work / 'init'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
        '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
        '-ffunction-sections', '-fdata-sections', '-I'+str(work), '-I'+str(ROOT),
        *include_flags(ROOT), '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
        str(ROOT / 'tools/tests/hostsvc_init_host.c'), str(work / 'lan.c'), '-o', str(exe)], check=True)
    return [host32.run([str(exe)], input=bytes([m]), capture_output=True).returncode for m in range(9)]
def main():
    source = (ROOT / 'init/kernel_boot_memory.inc').read_text().split('static void kernel_boot_memory(')[0]
    with tempfile.TemporaryDirectory(prefix='hostsvc-init-') as td:
        work=Path(td)
        lan=(ROOT / 'fs/serialfs/sfs_lan.c').read_text()
        lan=lan.replace('sfs_lan_init', 'body_lan_init').replace('sfs_lan_set_peer', 'body_lan_peer').replace('sfs_lan_io', 'body_lan_io')
        (work / 'lan.c').write_text(lan)
        results=build_run(work,source)
        assert not any(results), results
        print('GREEN absent / SFS / peer only / SFS+peer / invalid / missing module / NIC failure+restore / mount failure / restore failure', flush=True)
        if '--mutate' in sys.argv:
            for name,before,after in MUTATIONS:
                assert source.count(before)==1, name
                results=build_run(work,source.replace(before,after))
                assert any(rc>0 for rc in results) and not any(rc<0 for rc in results), (name,results)
                print('RED',name,results,flush=True)
def run(ctx):
    for runner in ctx.runners:
        rc=subprocess.call([sys.executable,'-B',__file__,*(['--mutate'] if ctx.mutate else [])],
                           env=dict(os.environ,HOST32_RUNNERS=runner))
        if rc: return rc
    return 0
if __name__=='__main__': main()
