"""T3ab-B direct product entry/stack fixture, invoked by VK32 tests."""
from kernel_includes import include_flags
import pathlib
import re

import host32
from test_vmkernel_layout import ROOT, command, fixture
import gen_memmap as gen


def run_case(work):
    work = pathlib.Path(work) / 'stack-entry'
    elf = fixture(work, core_bss=35)
    _, syms = gen.read_elf(elf)
    m = gen.Macros(gen.read_defines(ROOT / 'include/memmap.h'), syms)
    guards = ['MEM_STACK_GUARD', 'MEM_SHELL_GUARD', 'MEM_SHM_GUARD_LO', 'MEM_SHM_GUARD_HI']
    guards += ['MEM_FIXED_PAGING_GUARD_BASE']
    header = ['static const unsigned long guard_ranges[][2] = {']
    header += ['{%d, %d},' % (m.get(n), m.get(n) + m.get('MEM_GUARD_SIZE')) for n in guards]
    header += ['};', 'static void boot_tss(void);', 'static int boot_sqlite(void);',
               'static void check_classification(void);', 'static void check_fault_return(void);']
    for name, symbol in [('ZERO_CORE_START', '__bss_start'), ('ZERO_CORE_END', '__bss_end')]:
        header += ['#define %s %d' % (name, syms[symbol])]
    (work / 'fixture_values.h').write_text('\n'.join(header))
    (work / 'x86_desc.h').write_text('static void x86_load_tr(unsigned int sel) {(void)sel;}\n')
    entry = (ROOT / 'arch/x86/kernel/kentry.asm').read_text()
    # Retain the complete BSS/ESP/argument path (the F6-removed x87 probe is gone).
    entry = entry.replace('__bss_start', 'fixture_bss_start')
    entry = entry.replace('extern kernel_main', 'extern host_kernel_main\n%define kernel_main entry_probe')
    entry += '''
entry_probe:
    mov [entry_sp], esp
    mov [entry_bp], ebp
    jmp host_kernel_main
global enter_kernel
enter_kernel:
    push dword 0x80
    push dword 65536
    push dword 0
    cld
    jmp kentry
section .data
global entry_sp, entry_bp
entry_sp: dd 0
entry_bp: dd 0
'''
    (work / 'entry.asm').write_text(gen.preprocess_layout(ROOT, entry, True))
    command(['nasm', '-f', 'elf32', work / 'entry.asm', '-o', work / 'entry.o'])
    command(['nasm', '-f', 'elf32', ROOT / 'lib/sqlite3/sqlite_stack.asm', '-o', work / 'sqlite_stack.o'])
    command(['nasm', '-f', 'elf32', ROOT / 'arch/x86/kernel/setjmp.asm', '-o', work / 'setjmp.o'])
    boot = (ROOT / 'init/kernel.c').read_text()
    tss = re.search(r'tss_init\([^;]+;', boot).group()
    calls = ('\nstatic void boot_tss(void) { ' + tss + ' }\nstatic int boot_sqlite(void) { '
             'int result=0; sqlite_call_on_alt_stack(sqlite_body, test_stack_top, &result); return result; }\n')
    # Execute each product diagnostic classification block with boundary EIPs.
    isr = (ROOT / 'arch/x86/kernel/isr_handlers.c').read_text()
    # The extracted recovery block also uses this product-local row constant.
    classify = re.search(r'^#define EXC_SCREEN_STATUS_ROW[^\n]+$', isr, re.M).group() + '\n'
    classify += '''
static const char *label;
static void tvram_puts_at(int row, int col, const char *s, int attr)
{ (void)row; (void)col; (void)attr; label = s; }
static void sputs(const char *s) { (void)s; }
'''
    begin = isr.index('static int sqlite_fault_address(')
    end = isr.index('\n/* exec フォルト復帰用', begin)
    classify += isr[begin:end]
    classify += '''
static u32 jumpbuf[KSETJMP_BUF_LEN];
u32 kctx_irq_depth, kctx_exc_depth;
static int exec_nest_level = 1;
/* Model exec's teardown at its boundary; execute the product longjmp itself. */
static void exec_fault_recover(void) { exec_longjmp(jumpbuf); }
'''
    for i, name in enumerate(('exception_handler', 'page_fault_handler')):
        start = isr.index('        if (sqlite_fault_address(fault_eip))', isr.index('void ' + name))
        end = isr.index('\n    }', start)
        classify += 'static void classify%d(u32 fault_eip) { int row = 0;\n%s\n}\n' % (i, isr[start:end])
        start = isr.index('    if (exec_nest_level > 0)', end)
        end = isr.index('\n    }', start) + len('\n    }')
        classify += 'static void recover%d(void *arg) { (void)arg; kctx_exc_depth = 1;\n%s\n}\n' % (i, isr[start:end])
    classify += '''
static void check_fault_return(void)
{
    volatile u32 marker = 0x12345678;
    for (volatile int i = 0; i < 2; i++) {
        if (!exec_setjmp(jumpbuf)) {
            sqlite_call_on_alt_stack(i ? recover1 : recover0, test_stack_top, 0);
            finish(32);
        }
        CHECK(marker == 0x12345678 && (u32)&marker >= MEM_KSTACK_BASE &&
              (u32)&marker < MEM_KSTACK_TOP && kctx_exc_depth == 0, 33);
    }
}
'''
    classify += 'static void check_classification(void) {\n'
    classify += 'module_records[0].module_id = MODULE_ID_SQLITE; module_records[0].state = MODULE_RUNNING; module_records[0].text = 0x200000; module_records[0].text_size = 4096;\n'
    for i in range(2):
        for addr, expected in [(m.get('KERNEL_LOAD_ADDR') - 1, 'O'),
                               (m.get('KERNEL_LOAD_ADDR'), '.'),
                               (syms['__kernel_resident_end'] - 1, '.'),
                               (syms['__kernel_resident_end'], 'O'),
                               (0x200000, 's'), (0x201000, 'O')]:
            classify += "classify%d(%d); CHECK(label[1] == '%s', 30);\n" % (i, addr, expected)
    classify += '}\n'
    source = (ROOT / 'tools/tests/stack_entry_host.c').read_text()
    source = source.replace('void kernel_main(', 'void host_kernel_main(')
    (work / 'host.c').write_text(source + calls + classify)
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O0', '-ffreestanding', '-fno-builtin',
           '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
           '-Wall', '-Wextra', '-Werror',
           *['-I' + str(work), *include_flags(ROOT)],
           work / 'host.c', work / 'entry.o', work / 'sqlite_stack.o', work / 'setjmp.o', '-o', work / 'host']
    for name in ('__bss_start', '__bss_end', '__kernel_resident_end'):
        host_name = 'fixture_bss_start' if name == '__bss_start' else name
        cmd += ['-Wl,--defsym,%s=%d' % (host_name, syms[name])]
    command(cmd)
    result = host32.run([str(work / 'host')], capture_output=True)
    assert result.returncode == 0, 'entry/stack observation rc=%d %s' % (result.returncode, result.stderr)
    return '[entry] product ASM BSS/guards/ESP/args, C TSS/watermarks, SQLite/shell return, fault classification/recovery boundary PASS'
