"""T5-0b #DF descriptor/diagnostic contract; NOT hardware exception delivery."""
from kernel_includes import include_flags
import argparse
import pathlib
import re
import struct
import sys
import subprocess
import tempfile
import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from mkmod import Elf


def run(folder, mutant=None):
    (folder / 'platform_emergency.h').write_text((ROOT / 'include/platform_emergency.h').read_text())
    (folder / 'emergency.c').write_text((ROOT / 'platform/pc98/emergency.c').read_text())
    for name in ('gdt', 'tss', 'idt', 'double_fault'):
        source = (ROOT / 'arch/x86/kernel' / (name + '.c')).read_text()
        if mutant == 'interrupt gate' and name == 'idt':
            source = source.replace('idt[EXC_DOUBLE_FAULT].type_attr = IDT_ATTR_TASK_GATE;',
                                    'idt_set_gate(8, isr_stub_8, IDT_ATTR_INT_GATE32);')
        if mutant == 'wrong CR3' and name == 'double_fault':
            source = source.replace('df_tss.cr3 = MEM_FIXED_PD_BASE;', 'df_tss.cr3 = 0;')
        (folder / (name + '.c')).write_text(source)
    (folder / 'x86_desc.h').write_text(
        '#pragma once\nstatic void x86_load_gdt(u32 p) {(void)p;}\n'
        'static void x86_load_tr(u16 s) {(void)s;}\n')
    (folder / 'io.h').write_text('#ifndef HOST_DF_IO\n#define HOST_DF_IO\n'
        'void host_stop(void);\nstatic void _disable(void) {}\n'
        'static void _stop(void) {host_stop();}\nstatic void _lidt(void *p) {(void)p;}\n'
        'static u8 inp(u16 p) {(void)p; return 0;}\n'
        'static void outp(u16 p, u8 v) {(void)p;(void)v;}\n'
        'static u32 irq_save(void) {return 0;}\nstatic void irq_restore(u32 f) {(void)f;}\n#endif\n')
    stubs = re.findall(r'extern void (\w+)\(void\);', (ROOT / 'arch/x86/kernel/idt.c').read_text())
    (folder / 'stubs.c').write_text('\n'.join('void '+n+'(void) {}' for n in stubs))
    cpu = (ROOT / 'arch/x86/arch_cpu.h').read_text()
    cpu = cpu.replace('mov %1, %%cr3', 'mov %1, host_observed_cr3')
    (folder / 'cpu_shadow.h').write_text(cpu)
    (folder / 'arch_cpu.h').write_text(cpu)
    exe = folder / 'df'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Os', '-ffreestanding',
           '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
           '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-DOS32_DF_GUARD_TEST=0']
    cmd += ['-I' + str(folder), *include_flags(ROOT)]
    cmd += [str(ROOT/'tools/tests/double_fault_host.c'), str(folder/'stubs.c'), '-o', str(exe)]
    host32.build(cmd, check=True)
    for mode in range(3):
        result = host32.run([str(exe)], input=struct.pack('<I', mode), runner='qemu', capture_output=True)
        if mutant:
            assert result.returncode == 1 and b'FAIL ' in result.stdout, (mutant, result)
        else:
            assert result.returncode == 0, result
            print(result.stdout.decode(), end='')
    if mutant:
        print('RED (runtime descriptor assertion): '+mutant)


def check_asm_constants(folder):
    values = {name: int(value, 0) for name, value in re.findall(
        r'^%define (\w+) (0x[0-9a-fA-F]+|[0-9]+)$',
        (ROOT / 'arch/x86/kernel/cpu_flags.inc').read_text(), re.M)}
    source = ('#define __ASSEMBLER__ 1\n#include "cpu.h"\n#include "types.h"\n'
              '#include "tss.h"\n#include "ksetjmp.h"\n#include <stddef.h>\n')
    for name in ('EFLAGS_IF', 'EFLAGS_VM', 'EFLAGS_RESERVED1', 'ABI_STACK_ALIGN'):
        source += f'STATIC_ASSERT({name} == {values[name]}, asm_{name});\n'
    source += (f'STATIC_ASSERT(offsetof(struct tss_entry, cr3) == '
               f'{values["TSS_OFF_CR3"]}, asm_tss_cr3);\n')
    for index, name in enumerate(('ESP', 'EBP', 'EBX', 'ESI', 'EDI', 'EIP',
                                  'IRQ_DEPTH', 'EXC_DEPTH')):
        source += (f'STATIC_ASSERT({values["JB_" + name]} == '
                   f'{index} * sizeof(u32), asm_jb_{name});\n')
    source += (f'STATIC_ASSERT({values["JB_EXC_DEPTH"]} + sizeof(u32) == '
               'KSETJMP_BUF_LEN * sizeof(u32), asm_jb_size);\n')
    path = folder / 'asm_constants.c'
    path.write_text(source)
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding',
                     *include_flags(ROOT),
                    '-c', str(path), '-o', str(folder / 'asm_constants.o')], check=True)
    print('PASS NASM/C EFLAGS/ABI, TSS.CR3 and setjmp offsets/size')


def check_entry_layout(folder):
    # Assemble expressions with NASM itself; compile assertions against the real
    # C headers with i386 sizes. No host pointer-size assumptions or C replicas.
    expressions = {
        'ABI_WORD_BYTES': 'ABI_WORD_BYTES',
        'GDT_KERNEL_DS': 'GDT_KERNEL_DS',
        'USER_DS': 'USER_DS',
        'X86_SEL_RPL_MASK': 'X86_SEL_RPL_MASK',
        'APP_FRAME_WORDS': 'APP_FRAME_WORDS',
        'TVRAM_CHAR_BASE': 'TVRAM_CHAR_BASE',
        'TVRAM_ATTR_BASE': 'TVRAM_ATTR_BASE',
        'PIC1_CMD': 'PIC1_CMD',
        'PIC2_CMD': 'PIC2_CMD',
        'OCW2_EOI': 'OCW2_EOI',
        'OCW3_ISR': 'OCW3_ISR',
        'OCW3_IRR': 'OCW3_IRR',
        'PIC_LAST_ISR_BIT': 'PIC_LAST_ISR_BIT',
        'TSS_OFF_ESP0': 'offsetof(struct tss_entry, esp0)',
        'PUSHAD_BYTES': '(ISR_REG_EAX + 1) * sizeof(u32)',
        'IRET_CS_OFF': '(V86I_CS - V86I_EIP) * sizeof(u32)',
        'IRET_EFLAGS_OFF': '(V86I_EFLAGS - V86I_EIP) * sizeof(u32)',
        'ISR_ERROR_BYTES': '(V86F_EIP - V86F_ERRCODE) * sizeof(u32)',
        'ISR_ERROR_EFLAGS_OFF': '(V86F_EFLAGS - V86F_ERRCODE) * sizeof(u32)',
        'ISR_COMMON_VECTOR_OFF': '(ISR_COMMON_CS - 3) * sizeof(u32)',
        'ISR_COMMON_ERROR_OFF': '(ISR_COMMON_CS - 2) * sizeof(u32)',
        'ISR_COMMON_EIP_OFF': '(ISR_COMMON_CS - 1) * sizeof(u32)',
        'ISR_COMMON_SKIP_BYTES': '(ISR_COMMON_CS - ISR_PF_CS + 1) * sizeof(u32)',
        'ISR_PF_ERROR_OFF': '(ISR_PF_CS - 2) * sizeof(u32)',
        'ISR_PF_EIP_OFF': '(ISR_PF_CS - 1) * sizeof(u32)',
        'ISR_IRQ_CS_OFF': 'V86I_CS * sizeof(u32)',
        'ISR_IRQ_EFLAGS_OFF': 'V86I_EFLAGS * sizeof(u32)',
        'V86_CTX_EIP': 'offsetof(struct v86_context, eip)',
        'V86_CTX_CS': 'offsetof(struct v86_context, cs)',
        'V86_CTX_EFLAGS': 'offsetof(struct v86_context, eflags)',
        'V86_CTX_ESP': 'offsetof(struct v86_context, esp)',
        'V86_CTX_SS': 'offsetof(struct v86_context, ss)',
        'V86_CTX_ES': 'offsetof(struct v86_context, es)',
        'V86_CTX_DS': 'offsetof(struct v86_context, ds)',
        'V86_CTX_FS': 'offsetof(struct v86_context, fs)',
        'V86_CTX_GS': 'offsetof(struct v86_context, gs)',
    }
    definitions = set(re.findall(r'^%define (\w+) .+$',
        (ROOT / 'arch/x86/kernel/entry_layout.inc').read_text(), re.M))
    assert definitions == set(expressions), (definitions ^ set(expressions))
    asm = folder / 'entry_layout.asm'
    asm.write_text('%include "arch/x86/kernel/entry_layout.inc"\n' +
                   ''.join('dd ' + name + '\n' for name in expressions))
    binary = folder / 'entry_layout.bin'
    subprocess.run(['nasm', '-f', 'bin', str(asm), '-o', str(binary)],
                   cwd=ROOT, check=True)
    values = struct.unpack('<' + 'I' * len(expressions), binary.read_bytes())
    source = ('#include "cpu.h"\n#include "idt.h"\n#include "tss.h"\n'
              '#include "v86.h"\n#include "exec/appslot.h"\n#include <stddef.h>\n')
    for (name, expression), value in zip(expressions.items(), values):
        source += f'STATIC_ASSERT(({expression}) == {value}, entry_{name});\n'
    path = folder / 'entry_layout.c'
    path.write_text(source)
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-ffreestanding',
                    *include_flags(ROOT),
                    '-c', str(path), '-o', str(folder / 'entry_layout.o')], check=True)
    print('PASS NASM/C entry constants, ISR frames, TSS and V86Context layout')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    elf = Elf((ROOT / 'build/out/kernel.elf').read_bytes())
    end = elf.symbol('__kernel_resident_end')['value']
    symbols = {s['name']: s for table in elf.symtabs.values() for s in table}
    for name in ('gdt', 'idt', 'kernel_tss', 'df_tss', 'df_stack',
                 'double_fault_entry', 'double_fault_halt', 'serial_puts_polled',
                 'platform_boot_puts', 'platform_fault_cell'):
        symbol = symbols[name]
        assert 0x100000 <= symbol['value'] and symbol['value'] + symbol['size'] <= end < 0x3e0000, name
    print('PASS product #DF code/GDT/IDT/TSS/stack in shared resident core')
    with tempfile.TemporaryDirectory(prefix='double-fault-') as tmp:
        folder = pathlib.Path(tmp)
        check_asm_constants(folder)
        check_entry_layout(folder)
        run(folder)
        if args.mutate:
            for mutant in ('interrupt gate', 'wrong CR3'):
                run(folder, mutant)


if __name__ == '__main__':
    main()
