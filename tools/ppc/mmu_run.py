#!/usr/bin/env python3
"""PPC MMU acceptance; artifacts only below build/out/ppc, no BSD checkout needed."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
sys.dont_write_bytecode = True
import time
from run import ROOT, ROM, QMP, command, require, CriterionFailure


# Independent cross target: explicitly invoked, not part of x86 make check.
CHECK = dict(name='ppc-mmu', tier=2, budget=600,
             covers=['arch/ppc/**','platform/powermac/**','build/ppc.mk','tools/ppc/**',
                     'include/types.h','lib/kstring.h','lib/kstring_c.c'])

def qemu(root, cross, timeout, stage, mode=0):
    out=root/'build/out/ppc'
    elf=out/'PPCMMU.ELF'
    symbols={m[2]:int(m[0],16) for l in command([cross/'bin/powerpc-elf-nm','-n',elf]).splitlines()
             if len(m:=l.split())==3}
    cd=out/'mmu-cd'
    cd.mkdir(exist_ok=True)
    shutil.copyfile(elf,cd/'PPCMMU.ELF')
    iso=out/'mmu.iso'
    command(['genisoimage','-quiet','-hfs','-r','-part','-no-desktop','-o',iso,cd])
    a,b=out/'mmu-a.log',out/'mmu-b.log'
    deadline=time.monotonic()+timeout
    args=['qemu-system-ppc','-M','mac99','-cpu','7450','-m','128M','-smp','1',
          '-accel','tcg','-bios',ROM,'-cdrom',iso,'-boot','d',
          '-prom-env','boot-device=cd:,\\PPCMMU.ELF','-prom-env','auto-boot?=true',
          '-nographic','-serial','file:'+str(a),'-serial','file:'+str(b),
          '-monitor','none','-qmp','stdio','-no-reboot']
    target="ppc_dec_observe" if stage=="C" and mode==0 else "ppc_stop_loop"
    q=None
    snapshots=[]
    counts=[]
    terminal_mismatch=0
    with (out/'mmu-qemu.stderr').open('w') as err:
        p=subprocess.Popen([str(x) for x in args],cwd=root,stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE,stderr=err,bufsize=0)
        try:
            q=QMP(p,deadline)
            while time.monotonic()<deadline:
                q.call('stop')
                regs=q.call('human-monitor-command',{'command-line':'info registers'})
                pc=re.search(r'NIP\s+([0-9a-fA-F]+)',regs)
                msr=re.search(r'MSR\s+([0-9a-fA-F]+)',regs)
                if pc and int(pc[1],16) in (symbols['ppc_stop_loop'],symbols['ppc_stop_other']) and int(pc[1],16)!=symbols[target]:
                    terminal_mismatch+=1
                    if terminal_mismatch==2:
                        (out/'mmu-qmp.log').write_text(regs)
                        raise CriterionFailure('unexpected terminal PC; '+regs+' serial='+repr(a.read_bytes()[-3000:]))
                else: terminal_mismatch=0
                if pc and msr and int(pc[1],16)==symbols[target] and bool(int(msr[1],16)&0x8000)==(stage=='C' and mode==0):
                    snapshots.append(regs)
                    if stage=='C' and mode==0:
                        value=q.call('human-monitor-command',{'command-line':f'xp /1wx 0x{symbols["count"]:x}'})
                        counts.append(int(value.split(':',1)[1].strip().split()[0],16))
                    if len(snapshots)==2: break
                q.call('cont')
                time.sleep(min(0.1,max(0,deadline-time.monotonic())))
            (out/'mmu-qmp.log').write_text('\nRESTART/RESTOP\n'.join(snapshots or [regs]))
            require(len(snapshots)==2,'stable target PC/EE; '+regs)
            if stage=='C' and mode==0:
                require(counts[0]>=4 and counts[1]>counts[0],'DEC count must continue '+repr(counts))
                (out/'mmu-dec-count.log').write_text(repr(counts)+'\n')
            serial=a.read_bytes().replace(b'\r\r\n',b'\r\n')
            for marker in ([b'TAKEOVER'] if mode==2 else [b'TAKEOVER',b'OWN-VECTORS',b'OWN-MMU',b'EE-PASS']):
                require(serial.count(marker)==1,repr(marker)+' missing/duplicate: '+repr(serial[-3000:]))
            if stage in ('B','C') and mode!=2:
                require(serial.count(b'PROBES-PASS')==1 and serial.count(b'DSI VECTOR=')==2 and serial.count(b'ISI VECTOR=')==1,'fault probes')
            require(b'FAIL:' not in serial and serial.count(b'FATAL VECTOR=')==(mode==1),'unexpected fault '+repr(serial[-3000:]))
            if stage=='C' and mode!=2:
                for marker in [b'CANARY-PASS',b'DEC-PASS']:
                    require(serial.count(marker)==1,repr(marker)+' '+repr(serial[-3000:]))
                require(serial.count(b'DEC VECTOR=')==2,'DEC dump limit')
            if mode==2:
                require(b'OWN-' not in serial,'boundary fail output')
            require(b'OWN-MMU' not in b.read_bytes(),'output on serial B')
            if mode!=2:
                require(int(msr[1],16)&0x72==0x32,'IP/IR/DR/RI')
                require(re.search(r'SDR1\s+0010000f',regs) is not None,'1MiB HPT base/mask')
                sprg=re.search(r'SPRG0\s+([0-9a-f]+)',regs)
                require(sprg is not None and int(sprg[1],16)==symbols['ppc_cpu_save'],'resident save SPRG0')
            q.call('quit')
            p.wait(timeout=max(0.01,deadline-time.monotonic()))
        finally:
            if q: q.close()
            if p.poll() is None:
                p.terminate()
                try: p.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    p.kill(); p.wait()
    return 'QEMU stage '+stage+' PASS\n'


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--stage',choices=['A','B','C'],default='C')
    p.add_argument('--mutate',action='store_true')
    p.add_argument('--mode',type=int,default=0)
    p.add_argument('--timeout',type=int,default=30)
    p.add_argument('--cross-dir',type=Path,default=Path('/home/hight/opt/cross-ppc'))
    a=p.parse_args()
    out=ROOT/'build/out/ppc';out.mkdir(parents=True,exist_ok=True)
    (out/'mmu-build.log').write_text(build(ROOT,a.cross_dir,a.stage,a.mode))
    print(host(ROOT),end='')
    print(static(ROOT,a.cross_dir),end='')
    print(api_rejection(ROOT,a.cross_dir),end='')
    print(qemu(ROOT,a.cross_dir,a.timeout,a.stage,a.mode),end='')
    if a.mutate: mutate(a)


# All detectors distinguish an assertion verdict from a build/loader/QMP failure.
def build(root,cross,stage='C',mode=0):
    targets=['all','build/out/ppc/mmu-fake','build/out/ppc/mmu-flow']
    return command(['make','-f','build/ppc.mk','CROSS_DIR='+str(cross),
                    'MMU_STAGE='+str('ABC'.index(stage)+1),'MMU_MODE='+str(mode),*targets],root)


def host(root):
    out=root/'build/out/ppc'
    for exe,args in [('mmu-fake',[])]+[('mmu-flow',[str(i)]) for i in range(6)]:
        p=subprocess.run(['qemu-i386',str(out/exe),*args],capture_output=True,timeout=10)
        if p.returncode==1 or p.returncode<0 or p.returncode==98:
            raise RuntimeError(f'{exe} loader/environment rc={p.returncode}: {p.stderr!r}')
        require(p.returncode==0,f'{exe} {args} criterion rc={p.returncode}')
    return 'host memory/CPU/HPT/DEC/OF handoff PASS\n'


def static(root,cross):
    out=root/'build/out/ppc'; elf=out/'PPCMMU.ELF'
    prefix=cross/'bin/powerpc-elf-'
    dis=command([str(prefix)+'objdump','-d',elf])
    (out/'mmu-objdump.log').write_text(dis)
    require(not command([str(prefix)+'nm','-u',elf]).strip(),'undefined/libgcc/newlib')
    require(len(elf.read_bytes())<=0x18000,'MMU ELF budget')
    ins=re.findall(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} ){4}\s*(.*)$',dis,re.M)
    for i in ins:
        require(not re.match(r'^(f|v|xv|lfs|lfd|stfs|stfd|lv|stv|lxv|stxv)',i.split()[0]),'FP/VMX '+i)
    def body(name):
        return dis.split('<'+name+'>:\n',1)[1].split('\n\n',1)[0]
    def ops(name):
        return re.findall(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} ){4}\s*(\w+)',body(name),re.M)
    # Decode mtspr fields, including BAT4-7 which objdump may name generically.
    def spr_writes(name):
        words=[int(''.join(m.split()),16) for m in re.findall(
            r'^\s*[0-9a-f]+:\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})',body(name),re.M)]
        return [(((w>>16)&31) | (((w>>11)&31)<<5),(w>>21)&31)
                for w in words if w>>26==31 and (w>>1)&1023==467]
    require(spr_writes('ppc_bat_reset')==[(s,4) for s in
            [528,530,532,534,536,538,540,542,560,562,564,566,568,570,572,574]],
            'BAT0-7 invalidation SPR sequence')
    require(spr_writes('ppc_bat_set')==[(529,4),(537,4),(528,5),(536,5)],
            'BAT0 lower before upper SPR sequence')
    require(spr_writes('ppc_write_hid0')==[(1008,3)],'HID0 write SPR')
    require('r3,8' in body('ppc_bat_reset') and ops('ppc_bat_reset')[10]=='bne',
            'upper BAT reset conditional on eight BATs')
    require(ops('ppc_cache_copy')[-5:]==['dcbst','icbi','hwsync','isync','blr'],'final cache line')
    require(ops('ppc_tlb_flush_all')==['hwsync','li','li','mtctr','tlbie','eieio','hwsync','addi','bdnz','tlbsync','hwsync','blr'],'full TLB order')
    require('r4,64' in body('ppc_tlb_flush_all') and 'r3,r3,4096' in body('ppc_tlb_flush_all'),'64 TLB pages')
    require('r3,r3,4098' in body('ppc_takeover'),'ME/RI instruction setting')
    take=body('ppc_takeover')
    positions=[take.index('<'+fn+'>') for fn in ('ppc_disable','ppc_cpu_setup','ppc_pmap_bootstrap','ppc_vector_install','pm_mmu_start')]
    require(positions==sorted(positions),'takeover order')
    require(take.index('mtsprg')<take.index('<ppc_vector_install>'),'SPRG0 before vector install')
    require('ppc_stop_loop' in body('ppc_stop_loop').splitlines()[-1],'stop loop target')
    for fn in ('ppc_takeover','ppc_cpu_setup','pm_mmu_start','ppc_exception','ppc_dec_interrupt','pm_mmu_dump'):
        require(not re.search(r'<(?:ofw_|powermac_mmu_boot)',body(fn)),'CI after boundary '+fn)
    # Decode copied vector relative branches independently from the ELF image.
    data=elf.read_bytes()
    import struct
    hdr=struct.unpack_from('>HHIIIIIHHHHHH',data,16)
    sections=[struct.unpack_from('>10I',data,hdr[5]+i*hdr[10]) for i in range(hdr[11])]
    nm=command([str(prefix)+'nm','-n',elf])
    symbols={m[2]:int(m[0],16) for l in nm.splitlines() if len(m:=l.split())==3}
    source=symbols['ppc_vector_source']
    section=next(s for s in sections if s[3]<=source<s[3]+s[5] and s[1]!=8)
    start=section[4]+source-section[3]
    for v in [*range(0x100,0x1800,0x100),0xf20]:
        word=struct.unpack_from('>I',data,start+v)[0]
        require(word>>26==18 and not word&3,'vector relative stub '+hex(v))
        off=word&0x3fffffc
        if off&0x2000000: off-=0x4000000
        require(0x1800<=v+off<0x3000,'vector trampoline '+hex(v))
    require(symbols['ppc_cpu_save']%16==0 and symbols['ppc_probe_pages']%4096==0,'resident alignment')
    return 'static OEA/vector/cache/ABI PASS\n'


def api_rejection(root,cross):
    out=root/'build/out/ppc/api';out.mkdir(parents=True,exist_ok=True)
    calls=['arch_mmu_current_root()','arch_mmu_load_root(0)',
           'arch_enter_user(0,0,0)','arch_call_on_stack(saved,0,0)',
           'inpw(0)','outpw(0,0)','inpd(0)','outpd(0,0)','io_wait()','io_wait_n(1)']
    for i,call in enumerate(calls):
        src=out/f'{i}.c';obj=out/f'{i}.o';exe=out/f'{i}.elf'
        src.write_text('#include "arch_io.h"\n#include "arch_cpu.h"\n#include "platform_io.h"\nu32 saved; void _start(void) { '+call+'; }\n')
        command([cross/'bin/powerpc-elf-gcc','-ffreestanding','-O2','-msoft-float','-mno-altivec','-msdata=none','-Iinclude','-Iarch/ppc','-Iplatform/powermac','-c',src,'-o',obj],root)
        p=subprocess.run([str(cross/'bin/powerpc-elf-gcc'),'-nostdlib','-Wl,-e,_start',str(obj),'-o',str(exe)],cwd=root,capture_output=True,text=True,timeout=10)
        (out/f'{i}.log').write_text(p.stdout+p.stderr)
        require(p.returncode!=0 and 'not_available' in p.stderr,'API linked or wrong refusal: '+call)
    return '10 unavailable API link rejections PASS\n'


MUTATIONS=[
 ('boundary-ci','arch/ppc/takeover.S','    b pm_mmu_start','    bl ofw_exit\n    b pm_mmu_start','static'),
 ('hpt-clear','arch/ppc/pmap.c','i++) hpt[i]=0;','i++) (void)hpt[i];','host'),
 ('7455-upper-clear','arch/ppc/mmu.S','    mtspr PPC_SPR_IBAT4U,4','    nop','static'),
 ('hpt-size','platform/powermac/mmu_boot.c','    n>>=1;','    n>>=0;','host'),
 ('7450-highbat','arch/ppc/cpu_setup.c','if (version==PPC_PVR_7455)','if (version==PPC_PVR_7455 || version==PPC_PVR_7450)','host'),
 ('cache-tail','arch/ppc/cache_copy.S','    icbi 0,3\n    sync','    nop\n    sync','static'),
 ('restore-ee','arch/ppc/mmu.S','    and 3,3,5','    mr 3,5','qemu'),
 ('pte-publish','arch/ppc/pmap.c','    pt[0] = shadow->hi;','    pt[0] = shadow->hi & ~PPC_PTE_V;','host'),
 ('tlbsync','arch/ppc/mmu.S','    tlbsync\n    sync\n    blr','    nop\n    sync\n    blr','static'),
 ('secondary-h','arch/ppc/pmap.c','if (secondary) shadow->hi |= PPC_PTE_H;','if (secondary) shadow->hi |= 0;','host'),
 ('dar','arch/ppc/vectors.S','    mfdar 30','    li 30,0','qemu'),
 ('isi-retry','arch/ppc/exception.c','if (f->vector==PPC_VECTOR_ISI)\n                    ppc_cache_copy','if (f->vector==PPC_VECTOR_ISI) f->srr0+=4;\n                if (f->vector==PPC_VECTOR_ISI)\n                    ppc_cache_copy','qemu'),
 ('pte-unset','arch/ppc/pmap.c','    pt[0] &= ~PPC_PTE_V;','    pt[0] |= PPC_PTE_V;','host'),
 ('r31','arch/ppc/vectors.S','    lwz 31,PPC_CALL_SIZE+124(1)','    lwz 31,PPC_CALL_SIZE+120(1)','qemu'),
 ('dec-reload','arch/ppc/decrementer.c','    ppc_write_dec((u32)ticks);','    (void)ticks;','host'),
 ('stop','arch/ppc/entry.S','    b ppc_stop_loop','    b ppc_stop_other','static'),
 ('boot-marker','platform/powermac/boot.c','OF: os32 ppc-boot','OF: os32 ppc-boox','boot'),
]


def detector(root,a,kind):
    if kind=='host': return host(root)
    if kind=='static': return static(root,a.cross_dir)
    if kind=='api': return api_rejection(root,a.cross_dir)
    if kind=='boot':
        from run import static as old_static,qemu as old_qemu
        return old_qemu(root,a.timeout,old_static(root,a.cross_dir))
    return qemu(root,a.cross_dir,a.timeout,'C')


def mutate(a):
    import json
    out=ROOT/'build/out/ppc/mmu-mutations'
    results=[]
    for name,file,before,after,kind in MUTATIONS:
        print('mutation '+name+': GREEN control',flush=True)
        detector(ROOT,a,kind)
        dest=out/name;dest.mkdir(parents=True,exist_ok=True)
        for directory in ('arch/ppc','platform/powermac','tools/ppc','include','lib'):
            shutil.copytree(ROOT/directory,dest/directory,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__'))
        (dest/'build').mkdir(exist_ok=True)
        shutil.copyfile(ROOT/'build/ppc.mk',dest/'build/ppc.mk')
        product=dest/file;source=product.read_text()
        if source.count(before)!=1: raise RuntimeError('nonunique mutation '+name)
        product.write_text(source.replace(before,after))
        (dest/'mutation.json').write_text(json.dumps((name,file,before,after,kind),indent=2))
        (dest/'build.log').write_text(build(dest,a.cross_dir))
        try: detector(dest,a,kind)
        except CriterionFailure as exc: (dest/'red.log').write_text(str(exc)+'\n')
        else: raise RuntimeError('mutation survived '+name)
        detector(ROOT,a,kind)
        results.append((name,file,before,after,kind,'GREEN/RED/GREEN'))
        print('mutation '+name+': GREEN/RED/GREEN (build rc=0)',flush=True)
    (out/'mutations.json').write_text(json.dumps(results,indent=2))

if __name__=="__main__": main()
