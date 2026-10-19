"""T7b-a2: real 387 instructions and exec/ISR paths; normal then runtime RED.
CPU privilege/IDT delivery and absent silicon detection require guest/real HW.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from test_exec_r1 import function

ROOT = Path(__file__).resolve().parents[2]
PATHS = ('kernel/font_guard.c', 'kernel/font_x87.asm', 'exec/exec.c', 'kernel/isr_handlers.c')
MUTATIONS = [
    ('restore-error', 0, '    font_x87_leave(font_x87_save);',
     '    if (result >= 0) font_x87_leave(font_x87_save);', 'full state restored'),
    ('rounding', 1, 'dw 0x037f', 'dw 0x077f', 'known CW'),
    ('mask', 1, 'dw 0x037f', 'dw 0x037e', 'known CW'),
    ('reentry', 0, 'font_guard_active || kctx_irq_depth', 'kctx_irq_depth', 'reentry'),
    ('STOP', 2, '    if (font_guard_active) return;', '', 'STOP after restore'),
    ('no-FPU', 0, 'if (!font_x87_present)', 'if (0)', 'no FPU KCG'),
    ('fault', 3, '    if (font_guard_active) font_guard_fault', '    if (0) font_guard_fault', 'font before app kill'),
]
for slot in range(8):
    MUTATIONS.append((f'value-{slot}', 1, '        frstor  [eax]',
                      f'        xor dword [eax + {28 + slot*10}], 1\n        frstor  [eax]', 'full state restored'))


def run(parts, work, name, expected=None):
    (work / 'guard.c').write_text(parts[0])
    # Execute the actual detection branches with only privileged state and
    # absent-device ESC behavior substituted. It does not prove absent silicon.
    asm = parts[1].replace('section .text', 'extern host_cr0, host_ac, host_no_fpu\nsection .text', 1)
    asm = asm.replace('        mov     edx, cr0', '        mov     edx, [host_cr0]')
    asm = asm.replace('        mov     cr0, edx', '        mov     [host_cr0], edx')
    asm = asm.replace('        xor     ecx, edx', '        xor     ecx, edx\n        and     ecx, [host_ac]')
    asm = asm.replace('        fninit\n        fnstsw',
                      '        cmp dword [host_no_fpu], 0\n        jne .no_esc\n        fninit\n        fnstsw', 1)
    asm = asm.replace('        fnstcw  [esp + 2]', '        fnstcw  [esp + 2]\n.no_esc:', 1)
    (work / 'fp.asm').write_text(asm)
    (work / 'exec.inc').write_text('\n'.join(function(parts[2], n) for n in (
        'ring3_abort_check', 'exec_park', 'exec_park_kbd', 'exec_park_poll',
        'exec_sys_yield', 'exec_park_stop')))
    handlers = ''.join(function(parts[3], n).split(marker)[0] + '}\n'
                       for n, marker in (
                           ('exception_handler', '    /* 画面上部クリア */'),
                           ('page_fault_handler', '    /* 画面上部をクリア')))
    (work / 'fault.inc').write_text(handlers.replace('    const char *name;', '    (void)error_code;\n    const char *name;'))
    subprocess.run(['nasm','-f','elf32',str(work/'fp.asm'),'-o',str(work/'fp.o')],check=True)
    exe=work/'test'
    inc=['-I'+str(ROOT/p) for p in ('tools/tests/host_arch','include','kernel','exec','fs','lib','gfx','drivers','arch/x86','platform/pc98','sdk/include/os32')]
    cmd=['gcc','-m32','-march=i386','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
         '-Wno-unused-variable','-Wno-unused-function','-ffreestanding','-fno-builtin',
         '-fno-pie','-fno-stack-protector','-nostdlib','-static','-no-pie',
         '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(work),*inc,
         str(ROOT/'tools/tests/font_guard_host.c'),str(work/'guard.c'),str(work/'fp.o'),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    result=host32.run([str(exe)],runner='qemu',capture_output=True,text=True,timeout=30)
    print(name+': '+result.stdout.strip(),flush=True)
    assert result.returncode==(1 if expected else 0), (name,result)
    if expected:
        assert 'FAIL '+expected in result.stdout,(name,result)
        print('RED runtime '+name+' (compiled)',flush=True)


def main():
    sources=[(ROOT/p).read_text() for p in PATHS]
    with tempfile.TemporaryDirectory(prefix='font-guard-') as td:
        run(sources,Path(td),'normal')
        if '--mutate' in sys.argv:
            for name,part,old,new,expected in MUTATIONS:
                assert old in sources[part],name
                changed=sources.copy()
                changed[part]=changed[part].replace(old,new)
                run(changed,Path(td),name,expected)
    return 0

if __name__=='__main__':
    sys.exit(main())
