#!/usr/bin/env python3
"""NOTEST: C3 product assembly state machine and mac99 runtime acceptance."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
sys.dont_write_bytecode = True
from run import ROOT, ROM, command, require, CriterionFailure

CHECK = dict(name='kboot-mmu', tier=2, budget=180,
             covers=['arch/ppc/**', 'platform/powermac/**',
                     'tools/ppc/kboot_mmu.py', 'tools/ppc/kboot_mmu_guest.c',
                     'tools/ppc/kboot_hpt.py', 'tools/ppc/kboot_hpt_host.c',
                     'tools/ppc/run.py', 'build/ppc.mk', 'include/types.h',
                     'include/callconv.h', 'mm/paging.h',
                     'lib/kstring.h', 'lib/kstring_c.c'], needs=[])
CROSS = Path('/home/hight/opt/cross-ppc/bin/powerpc-elf-')
MUTATIONS = [
    ('same-root', '    stw 3,.Lcurrent_root@l(5)',
     '    lwz 6,.Lcurrent_root@l(5)\n    cmpw 3,6\n    beq .Lfinish\n    stw 3,.Lcurrent_root@l(5)'),
    ('no-hpt-clear', '    bl ppc_hpt_clear', '    nop'),
    ('no-tlb-completion', '    tlbsync\n    sync', '    nop\n    nop'),
    ('no-msr-restore', '    mtmsr 5\n    sync\n    isync', '    nop\n    sync\n    isync'),
    ('no-post-mtmsr-sync', '    mtmsr 6\n    sync\n    isync',
     '    mtmsr 6\n    isync'),
    ('no-tlbie', '    tlbie 3', '    nop'),
    ('short-tlb', '    li 4,PPC_TLB_COUNT', '    li 4,32'),
]


def assemble(source, out):
    obj=out/'root.o'
    command([str(CROSS)+'gcc','-mcpu=7450','-m32','-mbig-endian','-msoft-float','-mno-altivec',
             '-I'+str(ROOT/'arch/ppc'),'-c',source,'-o',obj])
    dis=command([str(CROSS)+'objdump','-dr',obj])
    (out/'root-objdump.log').write_text(dis)
    require('elf32-powerpc' in dis, 'PPC object')
    header=command([str(CROSS)+'readelf','-h',obj])
    (out/'root-readelf.log').write_text(header)
    require('ELF32' in header and 'big endian' in header and 'PowerPC' in header,'ELF32 BE PPC')
    return dis


def host(dis):
    """Interpret decoded product instructions; no copy of product control flow.

    Privileged CPU effects and C2's call are observation points. C2's actual
    zeroing C is separately executed by kboot_hpt.py (including clear-short).
    Relocations identify the real root cell and the external C call.
    """
    ins={}
    reloc={}
    symbols={}
    for line in dis.splitlines():
        m=re.match(r'([0-9a-f]+) <([^>]+)>:',line)
        if m: symbols[m[2]]=int(m[1],16)
        m=re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s+){4}(\w+)\s*(.*)',line)
        if m: ins[int(m[1],16)]=(m[2],m[3].split('#')[0].strip())
        m=re.match(r'\s*([0-9a-f]+): R_PPC_(\w+)\s+(.*)',line)
        if m: reloc[int(m[1],16)&~3]=(m[2],m[3])
    for pc,(op,arg) in ins.items():
        if op=='mtmsr':
            require(ins.get(pc-4,('', ''))[0]=='hwsync' and
                    [ins.get(pc+n,('', ''))[0] for n in (4,8)]==['hwsync','isync'],
                    'MSR change requires sync / mtmsr / sync / isync')
    root_addr=0x200000
    def scenario(entry, current, new, saved):
        regs=[0]*32; regs[1]=0x100000; regs[3]=new
        memory={root_addr:current}; events=[]; tlb=[]; eq=False; ctr=0
        msr=saved; lr=0xabcdef; pc=symbols[entry]; cleared=0
        for step in range(3000):
            op,arg=ins[pc]; here=pc; pc+=4
            parts=arg.split(',') if arg else []
            def reg(s): return int(s.removeprefix('r'))
            def val(s): return regs[reg(s)]
            if op in ('li','lis'):
                regs[reg(parts[0])]=int(parts[1],0)*(65536 if op=='lis' else 1)
                if here in reloc: regs[reg(parts[0])]=root_addr
            elif op in ('lwz','stw','stwu'):
                offset,base=re.match(r'(-?\d+)\((r\d+)\)',parts[1]).groups()
                addr=val(base)+int(offset)
                if here in reloc: addr=root_addr
                if op=='lwz': regs[reg(parts[0])]=memory[addr]
                else:
                    if addr==root_addr:
                        require(msr&0x8030==0,'root updated outside real interval')
                        events.append('root')
                    memory[addr]=val(parts[0])
                    if op=='stwu': regs[reg(base)]=addr
            elif op=='mflr': regs[reg(parts[0])]=lr
            elif op=='mtlr': lr=val(parts[0])
            elif op=='mfmsr': regs[reg(parts[0])]=msr
            elif op=='mtmsr': msr=val(parts[0]); events.append(('msr',msr))
            elif op=='ori': regs[reg(parts[0])]=val(parts[1])|int(parts[2],0)
            elif op=='andc': regs[reg(parts[0])]=val(parts[1])&~val(parts[2])
            elif op=='addi': regs[reg(parts[0])]=val(parts[1])+int(parts[2],0)
            elif op=='cmpwi': eq=val(parts[0])==int(parts[1],0)
            elif op=='cmpw': eq=val(parts[0])==val(parts[1])
            elif op in ('beq','bne','b','bdnz'):
                take=op=='b' or (op=='beq' and eq) or (op=='bne' and not eq)
                if op=='bdnz': ctr-=1; take=ctr!=0
                if take: pc=int(parts[0].split()[0],16)
            elif op=='mtctr': ctr=val(parts[0])
            elif op=='bl':
                require(reloc.get(here,('', ''))[1]=='ppc_hpt_clear','clear call target')
                require(msr&0x8030==0,'C clear must run real, EE disabled')
                require(memory[root_addr]==(new if entry=='ppc_root_load' else current),'current before clear')
                events.append('clear'); cleared+=1
                # ABI volatile registers may be clobbered by actual C.
                for r in range(3,13): regs[r]=0xdddddddd
                lr=here+4
            elif op=='tlbie':
                require(msr&0x8030==0 and cleared==1,'TLB before HPT clear/in translated mode')
                tlb.append(val(parts[0])); events.append('tlbie')
            elif op in ('hwsync','eieio','isync','tlbsync','nop'): events.append(op)
            elif op=='blr': break
            else: raise RuntimeError('unmodeled product instruction '+op)
        else: raise RuntimeError('instruction budget exceeded')
        if entry=='ppc_root_current':
            require(regs[3]==current,'current getter'); return
        expected=new if entry=='ppc_root_load' else current
        require(memory[root_addr]==expected,'root storage')
        require(cleared==1 and tlb==list(range(0,64*4096,4096)),'unconditional complete flush')
        last=max(i for i,e in enumerate(events) if e=='tlbie')
        require(events[last+1:last+9]==['eieio','hwsync','tlbsync','hwsync','hwsync',('msr',saved if expected else saved&~0x30),'hwsync','isync'],'TLB completion then MSR restore')
        require(msr==(saved if expected else saved&~0x30),'MSR restored')
        require(regs[1]==0x100000 and lr==0xabcdef,'ABI stack/LR restored')
        if entry=='ppc_root_load': require(events.index('root')<events.index('clear'),'root publication order')
    for saved in (0x1002,0x1032,0x9032):
        for old,new in ((0,0x3000),(0x3000,0x4000),(0x3000,0x3000),(0,0)):
            for entry in ('ppc_root_load','ppc_root_flush','ppc_root_current'):
                scenario(entry,old,new,saved)
    print('host decoded product: 36 root/MSR/ABI/clear/TLB scenarios PASS',flush=True)


def build_guest(out):
    c=['platform/powermac/ofw.c','platform/powermac/escc_tree.c',
       'platform/powermac/escc.c','platform/powermac/mmu_boot.c',
       'arch/ppc/cpu_setup.c','arch/ppc/pmap.c','arch/ppc/kernel_hpt.c',
       'arch/ppc/pd_walk.c','lib/kstring_c.c','tools/ppc/kboot_mmu_guest.c']
    # Mutant assembly lives in the output directory; make's pattern rules
    # expect repository-relative sources, so compile and link it separately.
    asm=['arch/ppc/entry.S','arch/ppc/ofw_note.S','arch/ppc/takeover.S',
         'arch/ppc/mmu.S','arch/ppc/cache_copy.S','arch/ppc/vectors.S']
    elf=out/'PPCMMU.ELF'
    command(['make','-f','build/ppc.mk','CROSS_DIR=/home/hight/opt/cross-ppc',
             'OUT='+str(out),'MMU_C='+' '.join(c),'MMU_S='+' '.join(asm),
             'MMU_OBJECTS='+' '.join(str(out/'mmu-3-0'/Path(s).with_suffix('.o')) for s in c+asm)+' '+str(out/'root.o'),
             str(elf)],timeout=120)
    require(not command([str(CROSS)+'nm','-u',elf]).strip(),'guest unresolved symbols')
    return elf


def guest(out, elf):
    cd=out/'cd'; cd.mkdir(exist_ok=True); shutil.copyfile(elf,cd/'PPCMMU.ELF')
    iso=out/'mmu.iso'
    command(['genisoimage','-quiet','-hfs','-r','-part','-no-desktop','-o',iso,cd])
    serial=out/'serial.log'
    serial.unlink(missing_ok=True)
    args=['qemu-system-ppc','-M','mac99','-cpu','7450','-m','128M','-smp','1',
          '-accel','tcg','-bios',ROM,'-cdrom',iso,'-boot','d',
          '-prom-env','boot-device=cd:,\\PPCMMU.ELF','-prom-env','auto-boot?=true',
          '-nographic','-serial','file:'+str(serial),'-serial','null',
          '-monitor','none','-no-reboot']
    began=time.monotonic()
    with (out/'qemu.log').open('w') as log:
        p=subprocess.Popen([str(x) for x in args],stdin=subprocess.DEVNULL,stdout=log,stderr=log)
        try:
            while time.monotonic()-began<30:
                data=serial.read_bytes() if serial.exists() else b''
                if b'C3-PASS' in data or b'C3-FAIL' in data or b'C3-FAULT' in data: break
                if p.poll() is not None: raise RuntimeError('QEMU exited '+str(p.returncode))
                time.sleep(0.1)
            if not any(marker in data for marker in (b'C3-PASS',b'C3-FAIL',b'C3-FAULT')):
                raise RuntimeError('guest produced no terminal verdict: '+repr(data[-1000:]))
            require(b'C3-PASS' in data and b'C3-FAIL' not in data and b'C3-FAULT' not in data,'guest serial verdict '+repr(data[-1000:]))
        finally:
            p.terminate()
            try: p.wait(timeout=2)
            except subprocess.TimeoutExpired: p.kill(); p.wait()
    print(f'QEMU mac99 translated load/same-root/flush PASS {time.monotonic()-began:.2f}s',flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate',action='store_true')
    args=parser.parse_args()
    out=Path(os.environ['TMPDIR'])/'kboot-mmu'; out.mkdir(parents=True,exist_ok=True)
    source=ROOT/'arch/ppc/kernel_mmu.S'
    host(assemble(source,out))
    # Execute the actual C2 clear implementation, not a Python substitute.
    print(command(['python3','-B','tools/ppc/kboot_hpt.py']),end='',flush=True)
    guest(out,build_guest(out))
    if args.mutate:
        original=source.read_text()
        for name,before,after in MUTATIONS:
            require(original.count(before)==1,'unique mutation '+name)
            dest=out/name; dest.mkdir(exist_ok=True)
            changed=dest/'kernel_mmu.S'; changed.write_text(original.replace(before,after))
            dis=assemble(changed,dest)
            try: host(dis)
            except CriterionFailure as exc:
                (dest/'red.log').write_text(str(exc)+'\n')
            else: raise RuntimeError('mutation survived '+name)
            print('RED '+name+' decoded product runtime',flush=True)
            if name in ('same-root','no-hpt-clear','no-msr-restore','no-tlbie'):
                try: guest(dest,build_guest(dest))
                except CriterionFailure as exc:
                    (dest/'qemu-red.log').write_text(str(exc)+'\n')
                else: raise RuntimeError('QEMU mutation survived '+name)
                print('RED '+name+' QEMU runtime',flush=True)
                guest(out,out/'PPCMMU.ELF')
            host(assemble(source,out))


if __name__=='__main__': main()
