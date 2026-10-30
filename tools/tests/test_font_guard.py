"""T7b-a2 guard section and exec/ISR paths; normal then runtime RED.
CPU privilege/IDT delivery requires guest/real HW.
TASK_FONTPERF F6: the x87 save/restore (font_x87.asm) and the no-FPU NOSYS are
gone (integer raster); the section, reentry/IRQ/exception BUSY, STOP/park and
fault attribution stay. The compiled guard is checked to hold no x87
instruction (counted by mnemonic in `objdump -d`, design G-4).
"""
from kernel_includes import include_flags
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import host32
from test_exec_r1 import function

ROOT = Path(__file__).resolve().parents[2]
PATHS = ('kernel/console/font_guard.c', 'exec/exec.c', 'arch/x86/kernel/isr_handlers.c')
MUTATIONS = [
    ('reentry', 0, 'font_guard_active || kctx_irq_depth', 'kctx_irq_depth', 'reentry'),
    ('active-kept', 0, '    font_guard_active = 0;\n', '', 'active cleared'),
    ('STOP', 1, '    if (font_guard_active) return;', '', 'STOP after leave'),
    ('fault', 2, '    if (font_guard_active) font_guard_fault', '    if (0) font_guard_fault', 'font before app kill'),
    ('x87-back', 0, '    font_guard_active = 1;\n', '    font_guard_active = 1;\n    __asm__ volatile("fninit");\n',
     'no x87 in the guard'),
]
# x87 mnemonics (Intel SDM vol. 2: every x87 instruction starts with "f", the
# 387 save/restore family included; FWAIT may print as "wait"). The mnemonic is
# the column after the instruction bytes in `objdump -d`.
X87 = re.compile(r'^\s*[0-9a-f]+:\t(?:[0-9a-f]{2} )+\s*\t(f[a-z0-9]*|wait)\b', re.M)


def run(parts, work, name, expected=None):
    (work / 'guard.c').write_text(parts[0])
    (work / 'exec.inc').write_text('\n'.join(function(parts[1], n) for n in (
        'ring3_abort_check', 'exec_park', 'exec_park_kbd', 'exec_park_poll',
        'exec_sys_yield', 'exec_park_stop')))
    handlers = ''.join(function(parts[2], n).split(marker)[0] + '}\n'
                       for n, marker in (
                           ('exception_handler', '    /* 画面上部クリア */'),
                           ('page_fault_handler', '    /* 画面上部をクリア')))
    (work / 'fault.inc').write_text('#include "idt.h"\n' + handlers.replace('    const char *name;', '    (void)error_code;\n    const char *name;'))
    exe=work/'test'
    inc=['-I' + str(ROOT / 'tools/tests/host_arch'), *include_flags(ROOT)]
    cmd=['gcc','-m32','-march=i386','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
         '-Wno-unused-variable','-Wno-unused-function','-ffreestanding','-fno-builtin',
         '-fno-pie','-fno-stack-protector','-nostdlib','-static','-no-pie',
         '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(work),*inc,
         str(ROOT/'tools/tests/font_guard_host.c'),str(work/'guard.c'),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    # G-4: the guard object itself holds no x87 instruction (by mnemonic).
    obj=work/'guard.o'
    subprocess.run(['gcc','-m32','-march=i386','-std=gnu11','-O2','-ffreestanding','-fno-builtin',
                    '-fno-pie','-fno-stack-protector','-c',*inc,str(work/'guard.c'),'-o',str(obj)],check=True)
    dis=subprocess.run(['objdump','-d',str(obj)],text=True,capture_output=True,check=True).stdout
    found=sorted(set(X87.findall(dis)))
    if found:
        print(name+': FAIL no x87 in the guard '+' '.join(found),flush=True)
        assert expected=='no x87 in the guard',(name,found)
        print('RED static '+name+' (compiled): x87 '+' '.join(found),flush=True)
        return
    assert expected!='no x87 in the guard',(name,'x87 not seen')
    print(name+': guard object x87 instructions 0 (objdump -d, by mnemonic)',flush=True)
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
