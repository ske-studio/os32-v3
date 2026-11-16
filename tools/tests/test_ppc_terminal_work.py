#!/usr/bin/env python3
"""期待の出所: AC-CTX-1 と P3 確定設計 v2 §2 の PLN1 work・入口/endpoint の別計数。"""
CHECK = dict(name='ppc-terminal-work', env=['ppc-cross'], tier=1, needs=[], budget=30,
    covers=['tools/tests/idle_io_sources.py', 'kernel/io_work.[ch]',
            'kernel/io_clock.h', 'kernel/io_policy.[ch]', 'exec/appslot.h',
            'tools/tests/test_ppc_terminal_work.py', 'tools/tests/host32.py',
            'platform/powermac/terminal_input.c',
            'platform/powermac/lan_boot.h', 'platform/powermac/usb_boot.h',
            'platform/powermac/usb_boot.c', 'platform/powermac/lan_service.c',
            'init/ppc_kernel.c', 'build/ppc-kboot.mk', 'net/nic.c', 'net/link.c', 'kernel/idle_work.c',
            'kernel/idle_work.h', 'net/nic.h', 'kernel/cli/resident_cli.h', 'include/types.h',
            'include/callconv.h', 'sdk/include/os32/os32_kapi_shared.h',
            'arch/ppc/kernel_boot.h', 'arch/ppc/exec_kapi.c', 'arch/ppc/exec_kapi.h',
            'platform/powermac/kapi_table.h', 'exec/exec_syscall.inc'], basis=['AC-CTX-1'])
from idle_io_sources import idle_io_sources
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]


def exercise(tmp, mutation=None):
    terminal = (ROOT / 'platform/powermac/terminal_input.c').read_text()
    if mutation:
        anchor, replacement = mutation
        assert terminal.count(anchor) == 1, anchor
        terminal = terminal.replace(anchor, replacement)
    # Compile the actual idle-work implementation against observable IRQ state.
    (tmp / 'io.h').write_text('extern int enabled; void _idle(void);\n#define _irq_enabled() (enabled)\n')
    (tmp / 'irq.h').write_text('extern unsigned int kctx_irq_depth;\n')
    idle = (ROOT / 'kernel/idle_work.c').read_text()
    (tmp / 'idle.c').write_text(idle)
    # The production USB poll body is tested with a fake clock and service sink.
    usb = (ROOT / 'platform/powermac/usb_boot.c').read_text()
    usb = usb[usb.index('void pm_usb_poll(void)'):].replace('void pm_usb_poll(', 'static void usb_poll_impl(')
    # Exercise the production LAN admission gate. The state-machine body is a
    # step counter here: this test measures admission, not MMIO/state transitions.
    lan = (ROOT / 'platform/powermac/lan_service.c').read_text()
    lan = lan[lan.index('void pm_lan_poll(u64 now)'):]
    lan = lan[:lan.index('    switch (s.state)')] + '    ++lan_steps;\n}\n'
    entry = (ROOT / 'exec/exec_syscall.inc').read_text()
    assert entry.count('    idle_work_run();') == 1
    entry = next(line for line in entry.splitlines() if line.strip() == 'idle_work_run();')
    harness = r'''
#include "lan_boot.h"
#include "idle_work.h"
int net_rx_recent(unsigned int ms) { (void)ms; return 0; }
void _idle(void) {}
int enabled = 1;
unsigned int kctx_irq_depth;
static int usb_calls, usb_steps, lan_steps, irq_errors, nested;
static u32 ms, active = 1, last_service_ms;
static u64 now = 10000;
static void (*callback)(void);
static u32 usbp_ms(void) { return ms; }
static u32 usbp_irq_save(void) { u32 old = enabled; enabled = 0; return old; }
static void usbp_irq_restore(u32 old) { enabled = old; }
static void service(void) { if (enabled) ++irq_errors; last_service_ms = ms; ++usb_steps; }
USB
void pm_usb_poll(void) {
    ++usb_calls;
    if (nested == 1) { nested = 2; callback(); nested = 0; }
    usb_poll_impl();
}
/* Only the LAN hardware step is substituted; the admission prefix is product code. */
enum service_state { OFF, RUNNING, QUARANTINED };
struct gem_dma { int unused; };
enum { GEM_AGAIN = 1 };
static struct { enum service_state state; u64 poll_at; struct { struct { u32 tb_hz; } io; } g; } s;
LAN
u64 pm_terminal_tb(void) { return now; }
TERMINAL
/* Capture the actual registered function, retaining the real register checks. */
static int register_work(void) {
    int rc = pm_terminal_work_register();
    callback = terminal_service_once;
    return rc;
}
static void syscall_entry(void) {
ENTRY
}
static int run(void) {
    s.state = RUNNING; s.g.io.tb_hz = 1000000;
    if (register_work()) return 1;
    if (register_work() != -19) return 2; /* OS32_ERR_EXIST */
    ms = 10;
    syscall_entry();
    if (usb_calls != 1 || usb_steps != 1 || lan_steps != 1) return 3;
    if (ppc_kapi_wait_once() || usb_calls != 2 || usb_steps != 1 || lan_steps != 1) return 4;
    /* Existing direct polls in input/output can follow both idle-work passes. */
    pm_terminal_poll();
    if (usb_calls != 3 || usb_steps != 1 || lan_steps != 1) return 5;
    ms = 20; now += 1000;
    if (ppc_kapi_wait_once() || usb_calls != 4 || usb_steps != 2 || lan_steps != 2) return 6;
    ms = 30; now += 1000; nested = 1;
    callback(); /* Bypass idle_work's guard to test the wrapper's own guard. */
    if (usb_calls != 5 || usb_steps != 3 || lan_steps != 3 || nested) return 7;
    enabled = 0; usb_poll_impl();
    if (enabled || irq_errors) return 8;
    ppc_kapi_wait_once();
    if (usb_calls != 5) return 9;
    enabled = 1; kctx_irq_depth = 1; ppc_kapi_wait_once();
    if (usb_calls != 5) return 10;
    kctx_irq_depth = 0; ppc_kapi_wait_once();
    if (usb_calls != 6 || !enabled) return 11;
    return 0;
}
void _start(void) { int rc = run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); for (;;) {} }
'''
    # Use the actual shared error value rather than a duplicated numeric ABI.
    harness = harness.replace('!= -19', '!= OS32_ERR_EXIST')
    source = '#include "os32_kapi_shared.h"\n' + (ROOT / 'arch/ppc/exec_kapi.c').read_text()
    for key, value in [('USB', usb), ('LAN', lan), ('TERMINAL', terminal), ('ENTRY', entry)]:
        harness = harness.replace('\n' + key + '\n', '\n' + value + '\n')
    (tmp / 'test.c').write_text(source + harness)
    # -Wno-unused-variable: the extracted pm_lan_poll prefix leaves the state machine's locals unused.
    flags = ['-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable', '-DKBOOT_SH=1', '-DPM_TERMINAL_TEST', '-D__cdecl=',
             '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie', '-nostdlib', '-static']
    includes = ['.', 'include', 'kernel', 'platform/powermac', 'arch/ppc', 'sdk/include/os32']
    subprocess.run(['gcc', *flags, *['-I' + str(ROOT / p) for p in includes],
                    str(tmp / 'test.c'), str(tmp / 'idle.c'), *idle_io_sources(ROOT, tmp), '-o', str(tmp / 'test')], check=True)
    return host32.run([str(tmp / 'test')], runner='qemu', timeout=10).returncode


def main():
    cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
    missing = [str(cc)] if not cc.is_file() else []
    missing += [p for p in ('gcc', 'qemu-i386', 'make') if not shutil.which(p)]
    if missing:
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP') + ' ppc-terminal-work: unavailable: ' + ', '.join(missing))
        return int(required)
    # Read the actual image selection without building prerequisites.
    for image, expected in [(['KBOOT_SH=1', 'KBOOT_LINK=0'], True), (['KBOOT_LINK=1'], False)]:
        manifest = subprocess.check_output(['make', '-s', '-f', 'build/ppc-kboot.mk', *image,
            '--eval=work-manifest:;@echo $(KB_C)', 'work-manifest'], cwd=ROOT, text=True).split()
        assert ('platform/powermac/terminal_input.c' in manifest) == expected
        assert 'net/nic.c' in manifest  # idle_wait_step uses the common RX predicate.
        assert ('net/link.c' in manifest) != expected
        assert ('platform/powermac/usb_boot.c' in manifest) == expected
    boot = (ROOT / 'init/ppc_kernel.c').read_text()
    # Select the PLN1 branch, then match the complete ordered startup block.
    pln = boot.replace('#if !KBOOT_LINK\n', '#if 1\n')
    pln = pln.replace('#if KBOOT_LINK\n        int work_rc = idle_work_register(pm_link_service_once);\n#else\n', '')
    pln = pln.replace('#endif\n', '').replace('#if 1\n', '')
    assert ('        (void)pm_hostfs_boot(info->tb_hz);\n'
            '        pm_usb_boot(info->tb_hz);\n'
            '        int work_rc = pm_terminal_work_register();\n'
            '        if (work_rc) kprintf(KBOOT_ATTR, "TERMINAL WORK error rc=%d\\r\\n", work_rc);\n'
            '        pm_input_boot();') in pln
    assert '#if KBOOT_LINK\n        int work_rc = idle_work_register(pm_link_service_once);' in boot
    with tempfile.TemporaryDirectory(prefix='ppc-work-') as directory:
        tmp = Path(directory)
        assert exercise(tmp) == 0
        if '--mutate' in sys.argv:
            for label, anchor, replacement in [
                ('registration', 'return idle_work_register(terminal_service_once);', 'return 0;'),
                ('USB poll', '    pm_usb_poll();', '    ;'),
                ('reentry', '    if (running) return;', '    (void)running;')]:
                rc = exercise(tmp, (anchor, replacement))
                assert rc > 0, (label, rc)
                print('RED runtime', label, 'rc=', rc)
    print('PASS PLN1 work, image selection, entry/endpoint, reentry and duplicate poll gates')
    return 0


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
