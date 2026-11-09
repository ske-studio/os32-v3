#!/usr/bin/env python3
"""Product OF entry canary, with poisoned BSS and runtime mutations on mac99."""
import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
sys.dont_write_bytecode = True
from run import ROOT, ROM, QMP, command, require, CriterionFailure

CHECK = dict(name='ppc-kboot-entry', tier=2, budget=600,
             covers=['arch/ppc/kernel_entry.S', 'arch/ppc/kernel_entry.h',
                     'arch/ppc/kernel.ld', 'platform/powermac/kboot_layout.h',
                     'tools/ppc/kboot_entry.py', 'tools/ppc/run.py',
                     'platform/powermac/ofw.c', 'platform/powermac/ofw.h',
                     'platform/powermac/escc.c', 'platform/powermac/escc_tree.c',
                     'platform/powermac/ppc_layout.h', 'arch/ppc/ppc_boot.h',
                     'lib/kstring_c.c', 'lib/kstring.h', 'include/types.h',
                     'include/memmap.h', 'tools/gen_memmap.py', 'build/arch/ppc.mk'])
PREFIX = '/home/hight/opt/cross-ppc/bin/powerpc-elf-'
FLAGS = ['-mcpu=7450', '-m32', '-mbig-endian', '-msoft-float', '-mno-altivec',
         '-msdata=none', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
         '-nostdlib', '-fcommon', '-fsigned-char', '-fno-short-enums', '-O2',
         '-D__cdecl=', '-I.', '-Iinclude', '-Ilib', '-Iarch/ppc', '-Iplatform/powermac']
CANARY = r'''
#include "kernel_entry.h"
#include "ofw.h"
#include "platform/powermac/kboot_layout.h"
volatile unsigned captured[3] = {1,1,1};
volatile unsigned char bss_canary[37];
void observe(ppc_ci ci) {
    unsigned bad = 0;
    for (volatile char *p=__bss_start; p<__bss_end; ++p) bad |= *p;
    unsigned sp=captured[0];
    unsigned ok = !bad && sp == (unsigned)__kernel_stack_top-PPC_KERNEL_ENTRY_FRAME
        && !(sp&15) && captured[1]==0 && captured[2]==(unsigned)ci
        && sp >= (unsigned)__kernel_stack_bottom;
    struct ofw_env e; struct pm_escc s;
    ofw_init(ci);
    if (ofw_setup(&e) || pm_escc_prepare(&e,&s)) for (;;) {}
    const char *text = ok ? "ENTRY: CI BSS STACK ALIGN PASS\r\n" : "ENTRY: FAIL\r\n";
    pm_escc_write(&s,text,ok ? sizeof("ENTRY: CI BSS STACK ALIGN PASS\r\n")-1 : sizeof("ENTRY: FAIL\r\n")-1);
    pm_escc_drain(&s);
}
'''
# Test-only observer captures the boundary before any C compiler prologue.
OBSERVER = r'''
.text
.globl ppc_kernel_start
ppc_kernel_start:
 lis 8,captured@ha
 addi 8,8,captured@l
 stw 1,0(8)
 lwz 9,0(1)
 stw 9,4(8)
 stw 3,8(8)
 b observe
.globl ppc_load8
ppc_load8:
 lbz 3,0(3)
 eieio
 blr
.globl ppc_store8
ppc_store8:
 stb 4,0(3)
 eieio
 blr
'''


def build(work, source, static=False):
    cc=PREFIX+'gcc'
    command(['python3', '-B', 'tools/gen_memmap.py', '--layout-header', work/'memmap_layout.h'])
    command([cc, '-E', '-P', '-x', 'c', '-I'+str(work), '-I.',
             'arch/ppc/kernel.ld', '-o', work/'kernel.ld'])
    (work/'entry.S').write_text(source)
    (work/'observe.c').write_text(CANARY)
    (work/'observe.S').write_text(OBSERVER)
    objects=[]
    for i, src in enumerate([work/'entry.S', work/'observe.c', work/'observe.S',
                            ROOT/'platform/powermac/ofw.c',
                            ROOT/'platform/powermac/escc.c',
                            ROOT/'platform/powermac/escc_tree.c', ROOT/'lib/kstring_c.c']):
        obj=work/f'{i}.o'
        command([cc, *FLAGS, '-I'+str(work), '-Iarch/ppc', '-c', src, '-o', obj])
        objects.append(obj)
    elf=work/'kernel.elf'
    command([cc, *FLAGS, '-Wl,--build-id=none', '-Wl,-T,'+str(work/'kernel.ld'), *objects, '-o', elf])
    symbols={m[2]:int(m[0],16) for line in command([PREFIX+'nm','-n',elf]).splitlines()
             if len(m:=line.split())==3}
    if static:
        dis=command([PREFIX+'objdump','-d',objects[0]])
        (work/'objdump.txt').write_text(dis)
        (work/'readelf.txt').write_text(command([PREFIX+'readelf','-hSWls',elf]))
        require(not re.search(r'\br(?:2|13)\b|\bmtmsr\b',dis), 'entry uses reserved registers/MSR')
        sections=command([PREFIX+'objdump','-h',objects[0]])
        require('.text.entry' in sections, 'entry section')
        data=elf.read_bytes()
        require(struct.unpack_from('>I',data,24)[0]==symbols['ppc_kernel_entry']==symbols['__kernel_image_start'], 'entry first')
        require(not command([PREFIX+'nm','-u',elf]).strip(), 'undefined symbols')
    # Firmware normally zeros NOBITS. Turn the LOAD tail into file bytes filled
    # with poison so missing stores (including the ledger endpoints) are visible.
    data=bytearray(elf.read_bytes())
    phoff=struct.unpack_from('>I',data,28)[0]
    phsize, phnum=struct.unpack_from('>HH',data,42)
    for i in range(phnum):
        off=phoff+i*phsize
        p=list(struct.unpack_from('>8I',data,off))
        if p[0]!=1: continue
        newoff=(len(data)+4095)&~4095
        payload=bytearray(p[5])
        payload[:p[4]]=data[p[1]:p[1]+p[4]]
        first=symbols['__bss_start']-p[2]; end=symbols['__bss_end']-p[2]
        payload[first:end]=b'\xa5'*(end-first)
        data.extend(b'\0'*(newoff-len(data)))
        data.extend(payload)
        p[1]=newoff; p[4]=p[5]
        struct.pack_into('>8I',data,off,*p)
    elf.write_bytes(data)
    return symbols


def guest(work, symbols):
    cd=work/'cd'; cd.mkdir(exist_ok=True)
    shutil.copyfile(work/'kernel.elf',cd/'ENTRY.ELF')
    iso=work/'entry.iso'
    command(['genisoimage','-quiet','-hfs','-r','-part','-no-desktop','-o',iso,cd])
    a,b=work/'serial-a.log',work/'serial-b.log'
    args=['qemu-system-ppc','-M','mac99','-cpu','7450','-m','128M','-smp','1',
          '-accel','tcg','-bios',str(ROM),'-cdrom',str(iso),'-boot','d',
          '-prom-env','boot-device=cd:,\\ENTRY.ELF','-prom-env','auto-boot?=true',
          '-nographic','-serial','file:'+str(a),'-serial','file:'+str(b),
          '-monitor','none','-qmp','stdio','-no-reboot']
    q=None
    with (work/'qemu.stderr').open('w') as err:
        p=subprocess.Popen(args,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err,bufsize=0)
        try:
            deadline=time.monotonic()+30
            q=QMP(p,deadline)
            stable=0
            while time.monotonic()<deadline:
                q.call('stop')
                regs=q.call('human-monitor-command',{'command-line':'info registers'})
                pc=re.search(r'NIP\s+([0-9a-fA-F]+)',regs)
                stable=stable+1 if pc and int(pc[1],16)==symbols['ppc_kernel_entry_stop'] else 0
                if stable==2: break
                q.call('cont'); time.sleep(0.1)
            (work/'qmp.log').write_text(regs)
            serial=a.read_bytes().replace(b'\r\r\n',b'\r\n')
            require(stable==2 and serial.count(b'ENTRY: CI BSS STACK ALIGN PASS\r\n')==1
                    and b'ENTRY:' not in b.read_bytes(), 'runtime entry canary: '+repr(serial[-500:]))
        finally:
            if q: q.close()
            if p.poll() is None:
                p.terminate()
                try: p.wait(timeout=2)
                except subprocess.TimeoutExpired: p.kill(); p.wait()


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--mutate',action='store_true')
    args=parser.parse_args(); started=time.monotonic()
    out=Path(os.environ['TMPDIR'])/'kboot-entry'; out.mkdir(exist_ok=True)
    source=(ROOT/'arch/ppc/kernel_entry.S').read_text()
    control=out/'control'; control.mkdir(exist_ok=True)
    guest(control,build(control,source,True)); print('GREEN product entry',flush=True)
    if args.mutate:
        mutations=[('ci','mr 3,5','li 3,0'),
                   ('bss-first','addi 8,8,__bss_start@l','addi 8,8,__bss_start@l+1'),
                   ('bss-last','addi 9,9,__bss_end@l','addi 9,9,__bss_end@l-1'),
                   ('stack-top','addi 1,1,__kernel_stack_top@l','addi 1,1,__kernel_stack_top@l-16'),
                   ('frame-alignment','stwu 0,-PPC_KERNEL_ENTRY_FRAME(1)','stwu 0,-(PPC_KERNEL_ENTRY_FRAME-4)(1)')]
        for name,before,after in mutations:
            assert source.count(before)==1, name+' unique anchor'
            work=out/name; work.mkdir(exist_ok=True)
            symbols=build(work,source.replace(before,after))
            try: guest(work,symbols)
            except CriterionFailure as exc:
                (work/'red.txt').write_text(str(exc)); print('RED '+name+' (runtime; build rc=0)',flush=True)
            else: raise AssertionError('SURVIVED '+name)
            guest(control,build(control,source))
    print(f'PASS elapsed={time.monotonic()-started:.2f}s')


if __name__=='__main__': main()
