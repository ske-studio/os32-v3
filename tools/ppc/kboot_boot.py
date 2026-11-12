#!/usr/bin/env python3
"""D3 product bootstrap with common mm, bounded mac99 acceptance."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
sys.dont_write_bytecode = True
from run import ROOT, ROM, QMP, command, require
from kboot_vectors import FLAGS, CROSS, symbols
sys.path.insert(0, str(ROOT/'tools/tests'))
from kernel_includes import include_flags
CHECK = dict(name='ppc-kboot-boot', tier=2, budget=600, covers=[
    'arch/ppc/**', 'platform/powermac/**', 'mm/**', 'include/**', 'lib/kstring*',
    'tools/ppc/kboot_boot*', 'tools/ppc/kboot_vectors.py', 'tools/ppc/run.py',
    'tools/tests/kernel_includes.py', 'build/arch/ppc.mk'])
PRODUCT = ['arch/ppc/entry.S', 'arch/ppc/kernel_entry.S', 'arch/ppc/kernel_takeover.S',
    'arch/ppc/kernel_boot.c', 'arch/ppc/kernel_cpu.c', 'arch/ppc/kernel_vectors.S',
    'arch/ppc/kernel_mmu.S', 'arch/ppc/kernel_hpt.c', 'arch/ppc/kernel_trap.c',
    'arch/ppc/kboot_unavailable.c',
    'arch/ppc/pd_walk.c', 'arch/ppc/mmu.S', 'arch/ppc/cache_copy.S',
    'platform/powermac/rh_observe.c',
    'platform/powermac/ofw.c', 'platform/powermac/escc.c',
    'platform/powermac/escc_tree.c', 'platform/powermac/kboot_capture.c',
    'platform/powermac/gmac_tree.c',
    'platform/powermac/kboot_reserve.c', 'platform/powermac/memory.c',
    'platform/powermac/mpic_tree.c',
    'mm/memory_boot.c', 'mm/paging.c', 'mm/physmem.c', 'mm/pgalloc.c',
    'lib/kstring_c.c', 'tools/ppc/kboot_boot_guest.c']


def build(out, negative=0, mutation=None):
    out.mkdir(parents=True, exist_ok=True)
    flags = FLAGS + ['-ffunction-sections','-fdata-sections',
                     '-I'+str(ROOT/'build/out'), *include_flags(ROOT),
                     '-DBOOT_NEGATIVE='+str(negative)]
    objects=[]
    for i, name in enumerate(PRODUCT):
        obj=out/f'{i}.o'; objects.append(obj)
        src=ROOT/name
        if mutation and name==mutation[1]:
            text=src.read_text()
            require(text.count(mutation[2])==1,'unique mutation anchor '+mutation[0])
            src=out/Path(name).name
            src.write_text(text.replace(mutation[2],mutation[3]))
        command([str(CROSS)+'gcc', *flags, '-c', src, '-o', obj])
    ld=out/'kernel.ld'
    command([str(CROSS)+'gcc', *flags, '-E','-P','-x','c', ROOT/'arch/ppc/kernel.ld', '-o', ld])
    elf=out/'BOOT.ELF'
    command([str(CROSS)+'gcc', *flags, '-no-pie', '-Wl,--gc-sections',
        '-Wl,--wrap=ppc_kernel_real_start',
        '-Wl,-T,'+str(ld), '-Wl,-Map,'+str(out/'kernel.map'), *objects, '-o', elf])
    (out/'readelf.log').write_text(command([str(CROSS)+'readelf','-hlSW',elf]))
    (out/'objdump.log').write_text(command([str(CROSS)+'objdump','-d',elf]))
    require(not command([str(CROSS)+'nm','-u',elf]).strip(), 'undefined symbols')
    return elf


def observe(out, elf, cpu, negative, red=None):
    syms=symbols(elf)
    cd=out/'cd'; cd.mkdir(exist_ok=True)
    shutil.copyfile(elf,cd/'BOOT.ELF')
    iso=out/'boot.iso'
    command(['genisoimage','-quiet','-hfs','-r','-part','-no-desktop','-o',iso,cd])
    serial=out/'serial.log'
    args=['qemu-system-ppc','-M','mac99','-cpu',cpu,'-m','128M','-accel','tcg',
        '-bios',ROM,'-cdrom',iso,'-boot','d','-prom-env','boot-device=cd:,\\BOOT.ELF',
        '-prom-env','auto-boot?=true','-nographic','-serial','file:'+str(serial),
        '-serial','null','-monitor','none','-qmp','stdio','-no-reboot']
    deadline=time.monotonic()+60
    q=None
    with (out/'stderr.log').open('w') as err:
        p=subprocess.Popen([str(x) for x in args],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err,bufsize=0)
        try:
            q=QMP(p,deadline)
            while time.monotonic()<deadline:
                q.call('stop')
                regs=q.call('human-monitor-command',{'command-line':'info registers'})
                m=re.search(r'NIP\s+([0-9a-fA-F]+)',regs)
                pc=int(m[1],16) if m else None
                if pc in [syms[x] for x in ['fixture_done','fixture_fail','ppc_stop_loop',
                          'ppc_kernel_vector_stop','ppc_boot_walk_stop','ofw_detached_stop']]: break
                q.call('cont'); time.sleep(.1)
            else: raise TimeoutError('D3 terminal not reached within 60 seconds')
            (out/'qmp.log').write_text(regs)
            data=serial.read_bytes()
            require(pc==syms[red[0] if red else 'fixture_done'], 'unexpected terminal '+str(hex(pc or 0))+' '+repr(data))
            for marker in (red[1] if red else [b'TAKEOVER', b'PAGING:MAIN',
                           b'FATAL:DSI' if negative else b'BOOT:PASS']):
                require(marker in data, 'missing marker '+repr(marker)+' '+repr(data))
            q.call('quit'); p.wait(timeout=2)
            require(p.returncode==0, 'QEMU exit code')
        finally:
            if q: q.close()
            if p.poll() is None:
                p.terminate(); p.wait(timeout=2)
    print('RED' if red else 'GREEN',cpu,'negative='+str(negative),flush=True)



# name, product file, unique anchor, replacement, guest probe, stop + reached marks.
MUTATIONS = [
 ('no-alias','arch/ppc/kernel_boot.c','        install_alias(root, &escc_alias);',
  '        /* mutant: omit ESCC */',0,('ppc_boot_walk_stop',[b'TAKEOVER'])),
 ('hpt-present','arch/ppc/kernel_boot.c','            *leaf(root, a) = 0;',
  '            *leaf(root, a) = a | PAGE_RW;',0,('ppc_boot_walk_stop',[b'TAKEOVER'])),
 ('of-after-detach','arch/ppc/kernel_boot.c','    info->of_calls_at_detach = ofw_detach();',
  '    info->of_calls_at_detach = ofw_detach();\n    ofw_write(e.out, mark, sizeof(mark)-1);',
  0,('ofw_detached_stop',[b'TAKEOVER'])),
 ('no-bat-retirement','arch/ppc/kernel_boot.c','    ppc_cpu_disable_bats(pvr);',
  '    (void)pvr;',1,('fixture_fail',[b'TAKEOVER',b'PAGING:MAIN',b'PROBE:LOW',b'FAIL:SURVIVED'])),
]

def host(out, bad, mutant=False):
    out.mkdir(parents=True,exist_ok=True)
    src=(ROOT/'arch/ppc/kernel_boot.c').read_text()
    if mutant:
        anchor='        if (!boot_walk_valid(root)) ppc_boot_walk_stop();'
        require(src.count(anchor)==1,'unique walk anchor')
        src=src.replace(anchor,'        if (0 && !boot_walk_valid(root)) ppc_boot_walk_stop();')
    (out/'kernel_boot_source.c').write_text(src)
    exe=out/'host'
    command(['gcc','-m32','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
        '-ffreestanding','-fno-builtin','-fno-pie','-no-pie','-fno-stack-protector',
        '-nostdlib','-static','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-D__cdecl=','-DBOOT_BAD_WALK='+str(int(bad)), '-I'+str(out),
        '-I'+str(ROOT/'arch/ppc'),'-I'+str(ROOT/'platform/powermac'),
        *include_flags(ROOT), ROOT/'tools/ppc/kboot_boot_host.c',
        ROOT/'arch/ppc/pd_walk.c','-o',exe])
    p=subprocess.run(['qemu-i386',str(exe)],capture_output=True,text=True,timeout=30)
    expected=31 if mutant else 0
    require(p.returncode==expected and 'REACHED:ALIAS' in p.stdout and
            'REACHED:ROOT' in p.stdout and
            ('FAIL:WALK-SKIPPED' if mutant else 'GREEN:REJECT' if bad else 'GREEN:ROOT') in p.stdout,
            'host verdict '+str(p.returncode)+' '+p.stdout)
    print('RED: walk-skipped' if mutant else 'GREEN: host',p.stdout,flush=True)


def lan_host(out, case, mutation=None):
    out.mkdir(parents=True, exist_ok=True)
    src=(ROOT/'arch/ppc/kernel_boot.c').read_text()
    if mutation:
        require(src.count(mutation[0]) == 1, 'unique LAN mutation')
        src=src.replace(mutation[0], mutation[1])
    (out/'kernel_boot_source.c').write_text(src)
    exe=out/'host'
    command(['gcc','-m32','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
        '-ffreestanding','-fno-builtin','-fno-pie','-no-pie','-fno-stack-protector',
        '-nostdlib','-static','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-D__cdecl=','-DBOOT_BAD_WALK=0','-DLAN_CASE='+str(case), '-I'+str(out),
        '-I'+str(ROOT/'arch/ppc'),'-I'+str(ROOT/'platform/powermac'),
        *include_flags(ROOT), ROOT/'tools/ppc/kboot_boot_host.c',
        ROOT/'arch/ppc/pd_walk.c','-o',exe])
    p=subprocess.run(['qemu-i386',str(exe)],capture_output=True,text=True,timeout=30)
    if mutation:
        require(p.returncode == mutation[2] and mutation[3] in p.stdout and
                'REACHED:PLAN' in p.stdout and 'REACHED:ROOT' in p.stdout,
                'LAN mutant verdict '+str(p.returncode)+' '+p.stdout)
    else:
        require(p.returncode == 0 and 'REACHED:PLAN' in p.stdout and
                'REACHED:LAN' in p.stdout and 'GREEN:ROOT' in p.stdout,
                'LAN verdict '+str(p.returncode)+' '+p.stdout)
    print('RED LAN' if mutation else 'GREEN LAN', case, p.stdout, flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate',action='store_true')
    args=parser.parse_args()
    out=Path(os.environ['TMPDIR'])/'kboot-boot'
    for negative in range(3):
        directory=out/str(negative)
        elf=build(directory,negative)
        for cpu in ['7450','7455']:
            observe(directory,elf,cpu,negative)
    for case in range(6):
        lan_host(out/('lan-'+str(case)),case)
    host(out/'host',False)
    host(out/'host-bad',True)
    host(out/'host-order',2)
    if args.mutate:
        lan_mutations = [
            (2, '        if (boot_devices.gmac_ready)\n            for (u32 i = 0; i < 3; i++) install_alias(root, &gmac_alias[i]);',
             '        if (1)\n            for (u32 i = 0; i < 3; i++) install_alias(root, &gmac_alias[i]);', 43, 'FAIL:DISABLED-ALIAS'),
            (0, 'info->gmac.bar_pa, PPC_BOOT_GMAC_BYTES, PPC_BOOT_GMAC_VA',
             'info->gmac.bar_pa, info->gmac.bar_bytes, PPC_BOOT_GMAC_VA', 41, 'FAIL:READY'),
            (2, 'boot_devices.gmac_ready && !gmac_aliases_safe(root)',
             'boot_devices.gmac_ready && 0 && !gmac_aliases_safe(root)', 41, 'FAIL:READY'),
            (2, 'boot_devices.gmac_ready = 0;', 'ppc_stop();', 99, 'FAIL:STOP'),
            (0, 'for (u32 i = 0; i < 3; i++) install_alias(root, &gmac_alias[i]);',
             'for (u32 i = 0; i < 3; i++) { install_alias(root, &gmac_alias[i]); *leaf(root, gmac_alias[i].va & PTE_FRAME_MASK) &= ~PTE_PCD; }', 99, 'FAIL:GMAC-WALK\nSTOP:WALK'),
            (1, 'info->gmac_rc == 0 && info->gmac.bar_bytes',
             'info->gmac.bar_bytes', 41, 'FAIL:READY'),
        ]
        for i, (case, old, new, code, marker) in enumerate(lan_mutations):
            lan_host(out/('lan-mutant-'+str(i)),case,(old,new,code,marker))
        for m in MUTATIONS:
            directory=out/m[0]
            elf=build(directory,m[4],m)
            observe(directory,elf,'7450',m[4],m[5])
        host(out/'host-walk-mutant',True,True)
    return 0

if __name__=='__main__': sys.exit(main())
