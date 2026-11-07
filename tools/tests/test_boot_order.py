"""Execute real boot callers/backends; replace only privileged CR0 transfers."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PATHS = ['init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc',
         'arch/x86/kernel/boot_cpu.c', 'arch/x86/kernel/boot_after_paging.c',
         'platform/pc98/boot_irq.c']
CHECK = dict(name='boot-order-host', tier=1, covers=['init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc',
    'arch/x86/kernel/boot_cpu.c', 'arch/x86/kernel/boot_after_paging.c',
    'platform/pc98/boot_irq.c',
    'tools/tests/test_boot_order.py',
    'include/boot_cpu.h', 'include/platform_boot.h', 'include/fault_fixture.h',
    'arch/x86/kernel/fault_fixture.c'], needs=[], budget=5.0)

PRELUDE = r'''
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "boot_cpu.h"
#include "platform_boot.h"
typedef unsigned int u32;
#define MEM_KSTACK_TOP 0x12340000u
#define CR0_EM 4u
#define CR0_TS 8u
#define KIB 1024
#define BOOT_STATUS_NUM_BUFSIZE 16
#define TATTR_CYAN 1
#define TATTR_GREEN 2
#define TATTR_WHITE 3
#define BOOT_DMA 4
#define BOOT_FONT 5
#define PIT_HZ 100
#define ICU_SLAVEID 7
struct boot_context { int unused; };
static unsigned flags, expected_flags, cr0, original;
static char trace[64];
static int count;
#define CHECK(x) do { if (!(x)) { puts("FAIL " #x); exit(1); } } while (0)
static void event(char c) { trace[count++]=c; trace[count]=0; }
static void gdt_init(void) { CHECK(flags==expected_flags); event('G'); }
static void tss_init(unsigned top) { CHECK(top==MEM_KSTACK_TOP && flags==expected_flags); event('T'); }
static void double_fault_init(void) { CHECK(flags==expected_flags); event('F'); }
static void idt_init(void) { CHECK(flags==expected_flags); event('I'); }
static void pic_init(void) { CHECK(flags==expected_flags); event('P'); }
void irq_topology_check(void) { CHECK(flags==expected_flags); event('Q'); }
static void sysclk_detect(void) { CHECK(flags==expected_flags); event('C'); }
static void irq_enable(unsigned irq) { CHECK(flags==expected_flags); CHECK(irq==0 || irq==7); event(irq ? '7' : '0'); }
static void pit_init(unsigned hz) { CHECK(hz==100 && flags==expected_flags); event('H'); }
static void _enable(void) { flags=1; event('E'); }
static void cpu_calibrate(void) { CHECK(flags==1); event('A'); }
static unsigned cpu_loops_per_tick(void) { return 1000; }
static int sysclk_is_8mhz(void) { return 1; }
static void tvram_clear(void) {}
static void console_hw_cursor_enable(void) {}
static void tvram_print(int x,int y,const char *s,int attr) { (void)x;(void)y;(void)s;(void)attr; }
static void boot_platform_run(int stage,struct boot_context *b) { (void)b; event(stage==BOOT_DMA ? 'D' : 'N'); }
static void paging_init(unsigned kb) { (void)kb; event('p'); }
static void paging_reclaim_conventional(void) { event('r'); }
static void x87_probe(void) { CHECK(cr0==(original & ~12u)); event('X'); }
'''


def probe(parts):
    source = PRELUDE
    for path in PATHS[2:]:
        body = re.sub(r'^#include[^\n]*\n', '', parts[path], flags=re.M)
        if path.endswith('boot_after_paging.c'):
            for old, new in [
                ('__asm__ volatile("mov %%cr0, %0" : "=r"(cr0_val));', 'cr0_val = cr0;'),
                ('__asm__ volatile("mov %0, %%cr0" : : "r"(cr0_val) : "memory");', 'cr0 = cr0_val;')]:
                assert body.count(old)==1
                body=body.replace(old,new)
        source += body
    source += parts[PATHS[0]]
    memory = 'static void kernel_boot_memory' + parts[PATHS[1]].split('static void kernel_boot_memory', 1)[1]
    memory = memory[:memory.index('    /* 物理ページ')] + '}\n'
    source += memory
    source += r'''
int main(void) {
    struct boot_context boot={0};
    kernel_boot_cpu(16384,&boot);
    CHECK(!strcmp(trace,"GTFIPQCDH07EA"));
    for (unsigned state=0;state<2;state++) {
        flags=expected_flags=state; count=0;
        arch_boot_segments(); arch_boot_exception_stack(); arch_boot_vectors();
        platform_boot_irq_controller(); platform_boot_clock_detect(); platform_boot_irq_start();
        CHECK(flags==state && !strcmp(trace,"GTFIPQC07"));
        count=0;
        original=cr0=0x8001003fu;
        kernel_boot_memory(16384,&boot);
        CHECK(!strcmp(trace,"prNX") && flags==state && cr0==(original & ~12u));
        count=0;
        arch_boot_after_paging();
        CHECK(flags==state && cr0==(original & ~12u));
    }
    puts("PASS boot order, PC98 cascade, IF preservation and CR0 mask");
}
'''
    with tempfile.TemporaryDirectory(prefix='boot-order-') as td:
        c=Path(td)/'host.c'; exe=Path(td)/'host'; c.write_text(source)
        subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(c),'-o',str(exe)],check=True)
        return subprocess.run([str(exe)]).returncode


def main():
    parts={p:(ROOT/p).read_text() for p in PATHS}
    assert probe(parts)==0
    if '--mutate' in sys.argv:
        for name,path,old,new in [
            ('TSS/IDT reversal',PATHS[0],'arch_boot_exception_stack();','arch_boot_vectors();'),
            ('topology check omitted',PATHS[4],' irq_topology_check();',''),
            ('EM retained',PATHS[3],'CR0_EM | CR0_TS','CR0_TS'),
            ('TS retained',PATHS[3],'CR0_EM | CR0_TS','CR0_EM'),
            ('IF inverted',PATHS[0],'    _enable();','    _enable(); flags ^= 1;'),
        ]:
            assert parts[path].count(old)==1
            changed=dict(parts)
            if name == 'TSS/IDT reversal':
                changed[path]=parts[path].replace(new, 'SWAP_STAGE();').replace(old,new).replace('SWAP_STAGE();',old)
            else:
                changed[path]=parts[path].replace(old,new)
            assert probe(changed)==1,name
            print('RED runtime '+name)
        assert probe(parts)==0

def run(ctx) -> int:
    return subprocess.call([sys.executable, '-B', str(ROOT / 'tools/tests/test_boot_order.py'),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__=='__main__': main()
