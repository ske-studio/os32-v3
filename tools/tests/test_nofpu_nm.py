"""CR0/probe/#NM integration: ILP32 product code, libc-free, privileged hooks."""
CHECK = dict(name='nofpu-nm', tier=1, needs=[], budget=15,
    covers=['lib/softfloat/**', 'exec/appslot.h', 'include/app_ids.h',
            'include/appmem_types.h', 'include/ksetjmp.h', 'arch/x86/arch_jmp_storage.h',
            'include/memmap.h', 'include/pc98.h', 'sdk/include/os32/*.h',
            'arch/x86/kernel/x87*', 'arch/x86/kernel/isr_handlers.c',
            'arch/x86/kernel/isr_stub.asm', 'arch/x86/kernel/entry_layout.inc',
            'arch/x86/kernel/iret_user.inc', 'arch/x86/kernel/cpu_flags.inc',
            'arch/x86/arch_cpu.h', 'arch/x86/arch_abi.h', 'arch/x86/arch_exec_storage.h',
            'arch/x86/x86_flags.h', 'include/cpu.h', 'include/abi.h',
            'include/callconv.h', 'include/exec_arch.h', 'include/types.h',
            'tools/tests/nofpu_nm_host.c', 'tools/tests/test_nofpu_nm.py',
            'tools/tests/ver_test_utils.py', 'tools/tests/kernel_includes.py', 'tools/tests/host32.py', 'build/config.mk'],
    kind='contract')
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
from ver_test_utils import function
ROOT=Path(__file__).resolve().parents[2]
MUTATIONS=[
    ('no-MP','x87.c','u32 final = original | CR0_MP;', 'u32 final = original;'),
    ('no-TS','x87.c','else final |= CR0_EM | CR0_TS;', 'else final |= CR0_EM;'),
    ('no-NE','x87.c','if (x87_present) final |= CR0_NE;', 'if (x87_present) final |= 0;'),
    ('EM-retained','x87.c','final &= ~(u32)(CR0_EM | CR0_TS)', 'final &= ~(u32)CR0_TS'),
    ('TS-retained','x87.c','final &= ~(u32)(CR0_EM | CR0_TS)', 'final &= ~(u32)CR0_EM'),
    ('memory-probe-before-status','x87.c','testw %%ax, %%ax; jnz 1f', 'testw %%ax, %%ax; nop'),
    ('unrestricted-probe-EIP','x87.c','if (*saved_eip != (u32)x87_probe_init &&', 'if (0 && *saved_eip != (u32)x87_probe_init &&'),
    ('kernel-ESP-as-user','x87_nm.h','t->gpr[X87_ESP] = f->user_esp;', 't->gpr[X87_ESP] = f->pushad_esp;'),
]
def asm_fixture():
    source=(ROOT/'arch/x86/kernel/isr_stub.asm').read_text()
    body=source.split('isr_stub_7:\n',1)[1].split('.from_v86:',1)[0]
    body=body.replace('        cli','        nop')
    body=body.replace('        jnz     .from_v86','        jnz     bad')
    body=body.replace('        iretd', '        jmp finish')
    return r'''bits 32
%include "arch/x86/kernel/cpu_flags.inc"
%include "arch/x86/kernel/entry_layout.inc"
%macro EXC_ENTER 0
%endmacro
%macro EXC_LEAVE 0
%endmacro
%macro RESTORE_KSEG 0
    mov ax,ss
    mov ds,ax
    mov es,ax
    mov fs,ax
    mov gs,ax
%endmacro
section .text
extern nm_handler
global asm_nm_test
asm_nm_test:
    push ebp
    push esi
    push edi
    push ebx
    push ds
    push es
    push fs
    push gs
    xor eax,eax
    mov ds,ax
    mov gs,ax
    mov ax,0x23
    mov fs,ax
    push dword 0x2b
    push dword 0x2000
    push dword 0x602
    push dword 0x23
    push dword 0x1000
''' + body + r'''
finish:
    ; The actual POP segment / POPAD path above must preserve null DS/GS,
    ; readable code FS and the original ES. Check before restoring host state.
    mov ax,ds
    test ax,ax
    jnz bad
    mov ax,gs
    test ax,ax
    jnz bad
    mov ax,fs
    cmp ax,0x23
    jne bad
    add esp,20
    pop gs
    pop fs
    pop es
    pop ds
    pop ebx
    pop edi
    pop esi
    pop ebp
    ret
bad:
    mov eax,1
    mov ebx,41
    int 0x80
'''

def execute(base, status, control, linked=False, forced=False):
    with tempfile.TemporaryDirectory(prefix='nofpu-nm-') as d:
        tmp=Path(d)
        source=base['x87.c']
        # Substitute only the three FPU operations: no-FPU FNSTSW AX leaves
        # 0xffff; the real asm labels/branch/AX constraint and C policy execute.
        source=source.replace('x87_probe_init: fninit','x87_probe_init: nop')
        source=source.replace('x87_probe_status: fnstsw %%ax',f'x87_probe_status: movw ${status}, %%ax')
        source=source.replace('x87_probe_control: fnstcw %1',f'x87_probe_control: movw ${control}, %1; incl control_reads')
        (tmp/'x87_source.c').write_text(source)
        (tmp/'x87_nm.h').write_text(base['x87_nm.h'])
        (tmp/'nm_handler.inc').write_text(base['handler'])
        (tmp/'entry.asm').write_text(asm_fixture())
        subprocess.run(['nasm','-f','elf32',str(tmp/'entry.asm'),'-o',str(tmp/'entry.o')],cwd=ROOT,check=True)
        exe=tmp/'fixture'
        cmd=['gcc','-m32','-march=i386','-std=gnu11','-Os','-Wall','-Wextra','-Werror',
             '-DX87_HOST_TEST',f'-DEXPECT_PRESENT={int(not forced and status==0 and control&0x103f==0x3f)}',
             f'-DPROBE_STATUS={status}','-ffreestanding','-fno-pie','-fno-stack-protector',
             '-nostdlib','-static','-no-pie','-I'+str(tmp),*include_flags(ROOT),
             str(tmp/'entry.o'), str(ROOT/'tools/tests/nofpu_nm_host.c'),str(ROOT/'arch/x86/kernel/x87_emu_decode.c'),'-o',str(exe)]
        if linked:
            cmd += ['-DOS32_X87_EMU_LINKED',str(ROOT/'arch/x86/kernel/x87_emu_state.c')]
        if forced: cmd += ['-DOS32_TEST_NOFPU']
        subprocess.run(cmd,check=True)
        return host32.run([str(exe)],timeout=30).returncode

def main():
    base={n:(ROOT/'arch/x86/kernel'/n).read_text() for n in ('x87.c','x87_nm.h')}
    base['handler']=function((ROOT/'arch/x86/kernel/isr_handlers.c').read_text(),'void nm_handler(')
    cases=[(0,0x37f),(0xffff,0xffff),(1,0x37f),(0,0xffff)]
    for status,control in cases: assert execute(base,status,control)==0,(status,control)
    assert execute(base,0xffff,0xffff,linked=True)==0
    assert execute(base,0,0x37f,linked=True,forced=True)==0
    print('GREEN CR0 four states, AX-first probe, restricted recovery, user ESP/SS and handler ordering')
    if '--mutate' in sys.argv:
        for name,file,old,new in MUTATIONS:
            assert base[file].count(old)==1,name
            changed=dict(base);changed[file]=base[file].replace(old,new)
            results=[execute(changed,*case) for case in cases]
            assert any(results),(name,results)
            print(f'{name}: runtime RED rc={results}')

def run(ctx):
    import os
    return subprocess.call([sys.executable,'-B',__file__,*(['--mutate'] if ctx.mutate else [])],
        cwd=ctx.root,env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners)))
if __name__=='__main__': main()
