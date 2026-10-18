"""T5-0b #DF descriptor/diagnostic contract; NOT hardware exception delivery."""
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
    for name in ('gdt', 'tss', 'idt', 'double_fault'):
        source = (ROOT / 'kernel' / (name + '.c')).read_text()
        if mutant == 'interrupt gate' and name == 'idt':
            source = source.replace('idt[8].type_attr = IDT_ATTR_TASK_GATE;',
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
    stubs = re.findall(r'extern void (\w+)\(void\);', (ROOT / 'kernel/idt.c').read_text())
    (folder / 'stubs.c').write_text('\n'.join('void '+n+'(void) {}' for n in stubs))
    cpu = (ROOT / 'arch/x86/arch_cpu.h').read_text()
    cpu = cpu.replace('mov %1, %%cr3', 'mov %1, host_observed_cr3')
    (folder / 'cpu_shadow.h').write_text(cpu)
    exe = folder / 'df'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Os', '-ffreestanding',
           '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
           '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-DOS32_DF_GUARD_TEST=0']
    cmd += ['-I'+str(p) for p in (folder, ROOT/'include', ROOT/'kernel', ROOT/'lib', ROOT/'drivers')]
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    elf = Elf((ROOT / 'build/out/kernel.elf').read_bytes())
    end = elf.symbol('__kernel_resident_end')['value']
    symbols = {s['name']: s for table in elf.symtabs.values() for s in table}
    for name in ('gdt', 'idt', 'kernel_tss', 'df_tss', 'df_stack',
                 'double_fault_entry', 'double_fault_halt', 'serial_puts_polled'):
        symbol = symbols[name]
        assert 0x100000 <= symbol['value'] and symbol['value'] + symbol['size'] <= end < 0x3e0000, name
    print('PASS product #DF code/GDT/IDT/TSS/stack in shared resident core')
    with tempfile.TemporaryDirectory(prefix='double-fault-') as tmp:
        folder = pathlib.Path(tmp)
        run(folder)
        if args.mutate:
            for mutant in ('interrupt gate', 'wrong CR3'):
                run(folder, mutant)


if __name__ == '__main__':
    main()
