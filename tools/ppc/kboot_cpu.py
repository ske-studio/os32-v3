#!/usr/bin/env python3
"""C4: product host instruction trace, cross ELF and mac99 BAT/HPT guards."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
sys.dont_write_bytecode = True
import kboot_vectors as vectors
from run import ROOT, ROM, QMP, command, require, CriterionFailure
from kboot_vectors import CROSS, FLAGS, symbols
BASE_PRODUCT = list(vectors.PRODUCT)

CHECK = dict(name='kboot-cpu', tier=2, budget=600, needs=[],
             covers=['arch/ppc/kernel_cpu.c', 'arch/ppc/ppc_*.h',
                     'tools/ppc/kboot_cpu*', 'tools/ppc/kboot_vectors.py',
                     'tools/ppc/run.py', 'arch/ppc/entry.S', 'arch/ppc/ofw_note.S',
                     'arch/ppc/mmu.S', 'arch/ppc/cache_copy.S',
                     'arch/ppc/kernel_vectors.S', 'arch/ppc/kernel_hpt.c',
                     'arch/ppc/pd_walk.c', 'platform/powermac/ofw.c',
                     'platform/powermac/ofw.h', 'platform/powermac/escc*.c',
                     'platform/powermac/ppc_layout.h', 'include/types.h',
                     'include/callconv.h', 'include/memmap.h',
                     'include/appmem_types.h', 'mm/paging.h',
                     'lib/kstring*', 'build/arch/ppc.mk'])


def host(out, source):
    out.mkdir(parents=True, exist_ok=True)
    flags=['-std=gnu11','-m32','-O2','-ffreestanding','-fno-pie','-no-pie',
           '-fno-stack-protector','-nostdlib','-static','-ffunction-sections',
           '-fdata-sections','-Wl,--gc-sections','-DPPC_CPU_HOST_TEST',
           '-DPPC_HPT_HOST_TEST','-D__cdecl=','-Wall','-Wextra','-Werror']
    exe=out/'host'
    command(['gcc',*flags,*[x for x in FLAGS if x.startswith('-I')],source,
             ROOT/'arch/ppc/kernel_hpt.c',ROOT/'tools/ppc/kboot_cpu_host.c','-o',exe])
    p=subprocess.run(['qemu-i386',str(exe)],timeout=10)
    # Only the host assertion exit is RED; signals and hangs are infrastructure errors.
    if p.returncode not in (0,1):
        raise RuntimeError('host unexpected exit '+str(p.returncode))
    require(p.returncode==0,'host privileged-operation runtime verdict')
    print('host product PVR/HID0/BAT/SR/SDR1/order PASS',flush=True)


def build(out, source):
    vectors.PRODUCT=[x for x in BASE_PRODUCT if 'kboot_vectors_fixture' not in x]
    vectors.PRODUCT += ['tools/ppc/kboot_cpu_fixture.S','tools/ppc/kboot_cpu_fixture.c',str(source)]
    # C2 is used directly; vectors.build normally renames its spill for the D1 wrapper.
    # Provide that alias without altering any product source.
    vectors.FLAGS = FLAGS + ['-Wl,--wrap=ppc_isync']
    elf=vectors.build(out,ROOT/'arch/ppc/kernel_vectors.S')
    return elf


def static(out, source):
    obj=out/'cpu.o'
    command([str(CROSS)+'gcc',*FLAGS,'-c',source,'-o',obj])
    dis=command([str(CROSS)+'objdump','-dr',obj]); (out/'cpu.objdump').write_text(dis)
    meta=command([str(CROSS)+'readelf','-hSW',obj]); (out/'cpu.readelf').write_text(meta)
    require('PowerPC' in meta and 'big endian' in meta,'cross ABI')
    # Decode SPR numbers from the actual instructions (objdump uses BAT aliases).
    words=[int(w,16) for w in re.findall(r'^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s',dis,re.M)]
    # GNU objdump on this target prints four byte columns.
    if not words:
        words=[int(''.join(w.split()),16) for w in re.findall(
            r'^\s*[0-9a-f]+:\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s',dis,re.M)]
    sprs=[((w>>16)&31)|((w>>6)&992) for w in words
          if w>>26==31 and ((w>>1)&1023)==467 and
          (((w>>16)&31)|((w>>6)&992)) != 8]
    require(sprs==[528,530,532,534,536,538,540,542,560,562,564,566,568,570,572,574,25],
            'BAT0..7 upper/SDR1 SPR sequence '+repr(sprs))
    srs=[(w>>16)&15 for w in words if w>>26==31 and ((w>>1)&1023)==210]
    require(srs==list(range(16)),'16 explicit SR writes '+repr(srs))
    require('dssall' in dis,'drain firmware data streams before HID0')
    require('cmpwi' in dis and 'beq' in dis and 'andis.' in dis,'PVR/upper BAT conditional branch')
    segments=dis.split('<ppc_cpu_load_segments>:',1)[1].split('<ppc_cpu_load_sdr1>:',1)[0]
    for n in range(16):
        require(re.search(r'li\s+r3,'+str(n)+r'\s*\n.*?ppc_hpt_vsid.*?oris\s+r3,r3,8192.*?mtsr\s+'+str(n)+r',r3',
                          segments,re.S), 'SR value Ks0/Kp1/fixed VSID '+str(n))
    hpt_obj=out/'hpt.o'
    command([str(CROSS)+'gcc',*FLAGS,'-c',ROOT/'arch/ppc/kernel_hpt.c','-o',hpt_obj])
    hpt_dis=command([str(CROSS)+'objdump','-d',hpt_obj])
    vsid=hpt_dis.split('<ppc_hpt_vsid>:',1)[1].split('blr',1)[0]
    require(re.search(r'ori\s+r3,r3,16',vsid),'C2 fixed VSID = 0x10 | segment')
    sdr=dis.split('<ppc_cpu_load_sdr1>:',1)[1]
    require(sdr.index('ppc_sync') < sdr.index('mtsdr1') < sdr.rindex('ppc_sync') <
            sdr.index('ppc_isync') < sdr.index('ppc_tlb_flush_all'),'SDR1 ordering')
    print('cross SPR/SR/order/readelf PASS',flush=True)


def observe(out, elf, cpu):
    out.mkdir(parents=True,exist_ok=True)
    syms=symbols(elf)
    cd=out/'cd'; cd.mkdir(exist_ok=True)
    shutil.copyfile(elf,cd/'CPU.ELF')
    iso=out/'fixture.iso'
    command(['genisoimage','-quiet','-hfs','-r','-part','-no-desktop','-o',iso,cd])
    a,b=out/'a.log',out/'b.log'
    args=['qemu-system-ppc','-M','mac99','-cpu',cpu,'-m','128M','-accel','tcg',
          '-bios',ROM,'-cdrom',iso,'-boot','d','-prom-env','boot-device=cd:,\\CPU.ELF',
          '-prom-env','auto-boot?=true','-nographic','-serial','file:'+str(a),
          '-serial','file:'+str(b),'-monitor','none','-qmp','stdio','-no-reboot']
    deadline=time.monotonic()+30
    q=None; regs=''; stable=0; last=None
    with (out/'qemu.stderr').open('w') as err:
        p=subprocess.Popen([str(x) for x in args],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                           stderr=err,bufsize=0)
        try:
            q=QMP(p,deadline)
            while time.monotonic()<deadline:
                q.call('stop')
                regs=q.call('human-monitor-command',{'command-line':'info registers'})
                match=re.search(r'NIP\s+([0-9a-fA-F]+)',regs)
                pc=int(match[1],16) if match else None
                terminal=pc in [syms[x] for x in ['fixture_done','ppc_stop_loop',
                                                 'ppc_kernel_vector_stop']]
                stable=stable+1 if terminal and pc==last else 0
                last=pc
                if stable>=1: break
                q.call('cont'); time.sleep(0.1)
            (out/'qmp.log').write_text(regs)
            serial=a.read_bytes(); (out/'result.log').write_bytes(serial)
            if stable<1:
                raise TimeoutError('no terminal PC within 30s: '+regs+' '+repr(serial[-2000:]))
            require(last==syms['fixture_done'],'terminal verdict '+regs+' '+repr(serial[-2000:]))
            markers=[b'HPT-PASS',b'LOW-BAT-GUARD-PASS',b'CPU-PASS']
            if cpu=='7455': markers.append(b'HIGH-BAT-GUARD-PASS')
            for marker in markers:
                require(serial.count(marker)==1,'runtime '+repr(marker)+' '+repr(serial[-2000:]))
            require(b'FAIL' not in serial,'runtime FAIL')
            q.call('quit'); p.wait(timeout=2)
        finally:
            if q: q.close()
            if p.poll() is None:
                p.terminate()
                try: p.wait(timeout=2)
                except subprocess.TimeoutExpired: p.kill(); p.wait()
    print('QEMU '+cpu+' HPT/guard PASS',flush=True)


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--mutate',action='store_true')
    a=parser.parse_args()
    out=ROOT/'build/out/ppc/kboot-cpu'
    src=ROOT/'arch/ppc/kernel_cpu.c'
    host(out/'normal',src); static(out/'normal',src)
    # The D1 helper renames C2's provider; fixture resolves the renamed symbol.
    elf=build(out/'normal',src)
    for cpu in ['7450','7455']: observe(out/'normal'/cpu,elf,cpu)
    if a.mutate:
        changes=[('ram-bat','    ppc_sync();\n    ppc_isync();\n}\n\nvoid ppc_cpu_load_segments',
                  '    ppc_bat_set(0, 0x12, 0x1ffe);\n    ppc_sync();\n    ppc_isync();\n}\n\nvoid ppc_cpu_load_segments'),
                 ('upper-bats','if (version == PPC_PVR_7455 && (hid & PPC_HID_HIGHBAT))',
                  'if (version == PPC_PVR_7450 && (hid & PPC_HID_HIGHBAT))'),
                 ('sr-keys','PPC_SR_KP | ppc_hpt_vsid(n)','0x40000000U | ppc_hpt_vsid(n)'),
                 ('sdr1-isync','    ppc_isync(); /* SDR1 context synchronization before any TLB use. */','')]
        text=src.read_text()
        for name,old,new in changes:
            require(text.count(old)==1,'unique mutation anchor '+name)
            dest=out/name; dest.mkdir(parents=True,exist_ok=True)
            mutant=dest/'kernel_cpu.c'; mutant.write_text(text.replace(old,new))
            try: host(dest,mutant)
            except CriterionFailure as e:
                (dest/'host-RED.log').write_text(str(e)); print(name+': host runtime RED',flush=True)
            else: raise CriterionFailure(name+': host survived')
            elf=build(dest,mutant)
            try: observe(dest/'7455',elf,'7455')
            except CriterionFailure as e:
                (dest/'qemu-RED.log').write_text(str(e)); print(name+': QEMU runtime RED',flush=True)
            else: raise CriterionFailure(name+': QEMU survived')


if __name__=='__main__': main()
