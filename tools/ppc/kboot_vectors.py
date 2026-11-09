#!/usr/bin/env python3
"""D1 product vectors: ELF/host ABI plus bounded mac99 runtime mutations."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
sys.dont_write_bytecode = True
from run import ROOT, ROM, QMP, command, require, CriterionFailure

CHECK = dict(name='kboot-vectors', tier=2, budget=600,
             covers=['arch/ppc/kernel_vectors.S', 'arch/ppc/ppc_frame.h',
                     'tools/ppc/kboot_vectors*','arch/ppc/entry.S', 'arch/ppc/ofw_note.S', 'arch/ppc/mmu.S',
                     'arch/ppc/cache_copy.S', 'arch/ppc/kernel_hpt.c',
                     'arch/ppc/pd_walk.c', 'arch/ppc/ppc_*.h', 'mm/paging.h',
                     'platform/powermac/ofw.c', 'platform/powermac/escc*.c',
                     'platform/powermac/ofw.h', 'platform/powermac/ppc_layout.h',
                     'lib/kstring_c.c', 'lib/kstring.h', 'include/types.h', 'include/callconv.h',
                     'include/memmap.h', 'include/appmem_types.h',
                     'tools/ppc/run.py', 'build/arch/ppc.mk'], needs=[])
CROSS = Path('/home/hight/opt/cross-ppc/bin/powerpc-elf-')
FLAGS = ['-std=gnu11','-mcpu=7450','-m32','-mbig-endian','-msoft-float','-mno-altivec',
         '-msdata=none','-fcommon','-fsigned-char','-fno-short-enums','-ffreestanding','-fno-pie','-fno-stack-protector',
         '-fno-builtin','-fno-asynchronous-unwind-tables','-nostdlib','-O2',
         '-Wall','-Wextra','-Werror','-D__cdecl=','-I'+str(ROOT),
         '-I'+str(ROOT/'arch/ppc'),'-I'+str(ROOT/'platform/powermac'),
         '-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib')]
PRODUCT = ['arch/ppc/entry.S','arch/ppc/ofw_note.S','arch/ppc/mmu.S',
           'arch/ppc/cache_copy.S','arch/ppc/kernel_hpt.c','arch/ppc/pd_walk.c',
           'platform/powermac/ofw.c','platform/powermac/escc_tree.c',
           'platform/powermac/escc.c','lib/kstring_c.c',
           'tools/ppc/kboot_vectors_fixture.S','tools/ppc/kboot_vectors_fixture.c']


def build(out, source):
    out.mkdir(parents=True,exist_ok=True)
    objects=[]
    for i, name in enumerate(PRODUCT+['arch/ppc/kernel_vectors.S']):
        src=source if name.endswith('/kernel_vectors.S') else ROOT/name
        obj=out/f'{i}.o'; objects.append(obj)
        extra=['-Dppc_hpt_spill=fixture_hpt_spill'] if name.endswith('/kernel_hpt.c') else []
        command([str(CROSS)+'gcc',*FLAGS,*extra,'-c',src,'-o',obj])
    ld=out/'fixture.ld'
    ld.write_text('''OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(7); note PT_NOTE FLAGS(4); }
SECTIONS {
 . = 0x20000;
 .text : { *(.text .text.*) } :image
 .rodata : { *(.rodata .rodata.*) } :image
 .note.ofw : { *(.note.ofw) } :image :note
 .data : { *(.data .data.*) *(.sdata*) } :image
 . = ALIGN(4); _edata = .;
 .bss (NOLOAD) : { *(.bss .bss.*) *(.sbss*) *(COMMON)
 . = ALIGN(16); . += 16384; __stack_top = .; _end = .; } :image
 /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note.GNU-stack) }
 ASSERT(_end < 0x400000, "fixture below 4MiB")
}''')
    elf=out/'VECTORS.ELF'
    command([str(CROSS)+'gcc',*FLAGS,'-no-pie','-Wl,-N','-Wl,-T,'+str(ld),
             '-Wl,-Map,'+str(out/'fixture.map'),*objects,'-o',elf])
    return elf


def static(out, elf):
    dis=command([str(CROSS)+'objdump','-d',elf])
    (out/'objdump.log').write_text(dis)
    readelf=command([str(CROSS)+'readelf','-hSWl',elf])
    (out/'readelf.log').write_text(readelf)
    require('PowerPC' in readelf and 'big endian' in readelf,'PPC ELF ABI')
    require(not command([str(CROSS)+'nm','-u',elf]).strip(),'undefined symbols')
    syms=symbols(elf)
    # Decode every installed branch and its stub, including the short 0xf20 slot.
    binary=out/'image.bin'
    command([str(CROSS)+'objcopy','-O','binary',elf,binary])
    raw=binary.read_bytes(); base=syms['ppc_kernel_vector_source']-0x20000
    for vector in list(range(0x100,0x1800,0x100))+[0xf20]:
        word=int.from_bytes(raw[base+vector:base+vector+4],'big')
        require(word>>26==18 and word&3==0,'vector branch '+hex(vector))
        delta=word&0x3fffffc
        if delta&0x2000000: delta-=0x4000000
        stub=base+vector+delta
        ins=[int.from_bytes(raw[stub+i:stub+i+4],'big') for i in range(0,24,4)]
        require(ins[3]==0x38200000|vector,'vector number '+hex(vector))
        require(ins[-1]&0xfc000003==0x48000002,'absolute entry')
        require((ins[-1]&0x3fffffc)==syms['ppc_kernel_vector_entry'],'entry target')
    entry=dis.split('<ppc_kernel_vector_entry>:',1)[1].split('<ppc_kernel_frame_complete>:',1)[0]
    require('mtmsr' not in entry,'no translation before completed frame')
    stores={(int(r),int(offset)) for r,offset in
            re.findall(r'stw\s+r(\d+),(\d+)\(r1\)',entry)}
    for r in range(28):
        require((30 if r==1 else r,32+4*r) in stores,'GPR store offset '+str(r))
    for offset in range(112,128,4):
        require((30,32+offset) in stores,'saved GPR store offset '+str(offset))
    for offset in range(128,164,4):
        require((30,32+offset) in stores,'special register offset '+str(offset))
    # Real C offsetof, independent of assembly's immediate expressions.
    host=out/'offset.c'
    fields={'lr':'LR','cr':'CR','ctr':'CTR','xer':'XER','srr0':'SRR0',
            'srr1':'SRR1','dar':'DAR','dsisr':'DSISR','vector':'VECTOR'}
    host.write_text('#include "ppc_frame.h"\n'+''.join(
        f'_Static_assert(__builtin_offsetof(struct ppc_frame,{f})==PPC_FRAME_{m},"{f}");\n'
        for f,m in fields.items())+'void _start(void){__asm__ volatile("movl $1,%eax; xorl %ebx,%ebx; int $0x80");}\n')
    command(['gcc','-std=gnu11','-m32','-ffreestanding','-fno-pie','-no-pie',
             '-nostdlib','-static','-I'+str(ROOT/'include'),'-I'+str(ROOT/'arch/ppc'),
             '-I'+str(ROOT/'platform/powermac'),host,'-o',out/'offset'])
    command(['qemu-i386',out/'offset'])
    require('addi' not in dis.split('<ppc_kernel_vector_return>:',1)[1].split('rfi',1)[0],
            'no SRR0 increment in return')
    print('ELF vectors/real frame/host offsetof PASS',flush=True)


def symbols(elf):
    return {p[2]:int(p[0],16) for line in command([str(CROSS)+'nm','-n',elf]).splitlines()
            if len(p:=line.split())==3}


def observe(out, elf):
    syms=symbols(elf)
    cd=out/'cd'; cd.mkdir(exist_ok=True)
    shutil.copyfile(elf,cd/'VECTORS.ELF')
    iso=out/'fixture.iso'
    command(['genisoimage','-quiet','-hfs','-r','-part','-no-desktop','-o',iso,cd])
    a,b=out/'a.log',out/'b.log'
    args=['qemu-system-ppc','-M','mac99','-cpu','7450','-m','128M','-accel','tcg',
          '-bios',ROM,'-cdrom',iso,'-boot','d','-prom-env','boot-device=cd:,\\VECTORS.ELF',
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
            serial=a.read_bytes()
            if stable<1:
                # No terminal PC within the deadline: a hang is a test failure,
                # never a mutation RED (design §2).
                (out/'result.log').write_bytes(serial)
                raise TimeoutError('no terminal PC within 30s: '+regs+' '+repr(serial[-2000:]))
            (out/'result.log').write_bytes(serial)
            require(last==syms['fixture_done'] and stable>=1,
                    'runtime terminal/canary verdict: '+regs+' '+repr(serial[-2000:]))
            for marker in [b'RETRY-CANARY-PASS',b'DISPATCH-CANARY-PASS',
                           b'NESTED-SPILL-PASS',b'GUARD-PASS',b'VECTORS-PASS']:
                require(serial.count(marker)==1,'runtime '+repr(marker)+' '+repr(serial[-2000:]))
            require(b'FAIL' not in serial,'runtime FAIL')
            q.call('quit'); p.wait(timeout=2)
        finally:
            if q: q.close()
            if p.poll() is None:
                p.terminate()
                try: p.wait(timeout=2)
                except subprocess.TimeoutExpired: p.kill(); p.wait()
    print('QEMU canary/retry/dispatch/nested spill PASS',flush=True)


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--mutate',action='store_true')
    a=parser.parse_args()
    out=ROOT/'build/out/ppc/kboot-vectors'
    src=ROOT/'arch/ppc/kernel_vectors.S'
    elf=build(out/'normal',src)
    static(out/'normal',elf); observe(out/'normal',elf)
    if a.mutate:
        changes=[('r31-offset','lwz 31,PPC_CALL_SIZE+124(1)','lwz 31,PPC_CALL_SIZE+120(1)'),
                 ('early-dr','    mr 31,1','    mfmsr 30\n    ori 30,30,0x30\n    mtmsr 30\n    isync\n    mr 31,1'),
                 ('srr0-plus4','    mtsrr0 3','    addi 3,3,4\n    mtsrr0 3'),
                 ('spill-dispatch','    beq ppc_kernel_vector_return','    bne ppc_kernel_vector_return')]
        text=src.read_text()
        for name,old,new in changes:
            require(text.count(old)==1,'unique mutation anchor '+name)
            dest=out/name; dest.mkdir(parents=True,exist_ok=True)
            mutant=dest/'kernel_vectors.S'; mutant.write_text(text.replace(old,new))
            elf=build(dest,mutant)
            try: observe(dest,elf)
            except CriterionFailure as e:
                (dest/'RED.log').write_text(str(e)); print(name+': runtime RED',flush=True)
            else: raise CriterionFailure(name+': survived')


if __name__=='__main__': main()
