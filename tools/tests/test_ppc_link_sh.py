#!/usr/bin/env python3
"""期待の出所: PPC_P3_DESIGN v2 §2/§3, appendix A-1 E5/E6/E7, A-3/A-4.
LAN_OPERATOR_DESIGN §5/§8 P4c restores HostFS over link.
Build manifests prove TU selection; freestanding host execution proves boot,
provider, foreground reentry and capture routing without a hardware oracle.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ppc-link-sh', env=['ppc-cross'], tier=1, needs=[], budget=60,
    covers=['tools/tests/test_ppc_link_sh.py', 'tools/tests/host32.py',
            'build/targets/ppc-powermac.mk', 'build/ppc-user.mk', 'sys/platform/powermac/ppc_kernel.c',
            'sys/platform/powermac/hostfs_boot.c', 'sys/platform/powermac/hostfs_link_io.c',
            'sys/platform/powermac/hostfs_link_peer.c',
            'sys/net/host_mailbox.c', 'sys/net/link.c', 'sys/net/nic.c',
            'sys/kern/dev/device_host.c', 'sys/platform/powermac/usb_boot.c',
            'sys/platform/powermac/terminal_fifo.inc', 'sys/kern/cli/resident_cli.h',
            'sys/net/host_mailbox.h', 'sys/net/link.h', 'sys/net/nic.h',
            'sys/platform/powermac/link_platform.c', 'sys/platform/powermac/link_platform.h',
            'sys/platform/powermac/input_boot.c', 'sys/platform/powermac/console_stream.c',
            'sys/platform/powermac/log_printf.c', 'sys/platform/powermac/shell_loop_ops.c',
            'platform/powermac/*.h', 'include/*.h', 'kernel/console/*.h',
            'sys/kern/init/shell_loop.c', 'sys/kern/kstring_c.c', 'sys/kern/kprintf.c', 'sys/kern/kprintf_format.inc', 'sys/hal/input/in2_int.h', 'sys/kern/exec/exec.h', 'sys/kern/idle_work.h',
            'arch/ppc/*.h', 'sdk/include/os32/*.h', 'sys/hal/net/link_platform.h'],
    basis=['LAN_OPERATOR_DESIGN §5/§8 P4c', 'PPC_P3_DESIGN v2 §2/§3; appendix A-1 E5/E6/E7, A-3/A-4'])

PREFIX = r'''
#include "types.h"
#include "sys/hal/net/link_platform.h"
#include "os32_kapi_shared.h"
static void finish(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); for (;;) {} }
#define CHECK(x) do { if (!(x)) finish(1); } while (0)
'''
WORK = r'''
static int usb, lan, recurse, active, stops, queued;
static HostNotice incoming;
u32 link_platform_lock(void) { return 0; }
void link_platform_unlock(u32 saved) { (void)saved; }
u32 link_hello_ok = 1;
void host_alive_snapshot(struct HostAlive *out)
{
    *out = (struct HostAlive){.valid=1, .host_generation=17};
}
int host_mailbox_stage(u32 generation, u32 service, HostNotice *out)
{
    CHECK(service == REMOTE_SVC_PPC_DIAG);
    if (generation != 17) return OS32_ERR_STALE;
    if (!queued) return OS32_ERR_AGAIN;
    *out = incoming;
    return 0;
}
void host_mailbox_commit(u32 generation, u32 serial)
{
    CHECK(queued && generation == 17 && serial == incoming.serial);
    queued = 0;
}
int resident_cui_hw_active(void) { return active; }
void ring3_abort_request(void) { ++stops; }
void pm_usb_poll(void) { usb++; if (!recurse) { recurse=1; pm_link_service_once(); } }
void pm_lan_poll(u64 now) { CHECK(now == 0x100000002ULL); lan++; }
void _start(void) {
    incoming = (HostNotice){.kind=REMOTE_NOTIFY_HW_STOP, .length=1, .serial=1, .data={3}};
    active = queued = 1;
    link_platform_session(18);
    terminal_receive();
    CHECK(!stops && queued);
    link_platform_session(17);
    CHECK(!queued && !stops);
    active = 0;
    queued = 1;
    pm_link_service_once();
    CHECK(usb == 1 && lan == 1 && !stops && !queued);
    active = queued = 1;
    ++incoming.serial;
    pm_link_service_once();
    pm_link_service_once();
    CHECK(usb == 3 && lan == 3 && stops == 1 && !queued);
    CHECK(pm_terminal_trygetkey() < 0);
    finish(0);
}
'''
PROVIDER = r'''
static const struct InputLegacyProvider *registered;
static const InputOps *registered_ops;
void input_legacy_register(const struct InputLegacyProvider *p) { registered=p; }
void input_register(const InputOps *p) { registered_ops=p; }
int input_legacy_wait(int mode, int interruptible) { (void)mode; (void)interruptible; return -1; }
int input_legacy_try(int mode) { (void)mode; return -1; }
int input_legacy_peek(int mode) { (void)mode; return -1; }
u8 platform_fep_key_shift(void) { return 0; }
int pm_input_usb_take(void) { return 42; }
int pm_input_usb_peek(void) { return 43; }
volatile int kbd_gui_mode;
int kbd_inject_peek(u8 *out) { (void)out; return 0; }
int kbd_inject_take(u8 *out) { (void)out; return 0; }
void kbd_set_gui_mode(int on) { kbd_gui_mode = on; }
void kbd_discard_stop(void) {}
int legacy_raw_take(void) { return -1; }
int input_key_pressed(u8 sc) { (void)sc; return 0; }
u8 input_key_mods(void) { return 0; }
void pm_input_usb_note_remote(void) {}
u32 pm_input_usb_dropped(void) { return 0; }
int platform_clock_read_us(u32 *lo,u32 *hi) { *lo=7; *hi=0; return 0; }
int con_sink_capture_active(void) { return 1; }
int ring3_wait_pending(void) { return 0; }
void idle_work_run(void) {}
void arch_cpu_idle(void) {}
void idle_wait_step(void) { idle_work_run(); arch_cpu_idle(); }
void _start(void) {
    pm_input_boot(); CHECK(registered && registered_ops);
    CHECK(!registered->terminal_try && !registered->terminal_peek && !registered->terminal_enabled);
    CHECK(registered->inject_try && registered->inject_peek);
    CHECK(registered->try_key == pm_input_usb_take && registered->peek_key == pm_input_usb_peek);
    CHECK(registered->try_key()==42 && registered->peek_key()==43);
    CHECK(registered->wait_timeout_ticks()==30000000u && registered->capture_active()); finish(0);
}
'''
BOOT = r'''
int rshell_active;
static int calls, waits, failures;
int gui_take_next_shell(char *path, int cap) { (void)path; (void)cap; return 0; }
int exec_run(const char *cmd)
{
    CHECK(!kstrcmp(cmd, "/sys/shell.bin") && ++calls <= 3);
    rshell_active = 1;
    return calls == 1 ? 0 : calls == 2 ? EXEC_ERR_FAULT : -1;
}
void shell_print(const char *s, u8 c) { (void)c; CHECK(calls == 3 && s[0]); failures++; }
void idle_wait_step(void)
{
    CHECK(++waits == calls && !rshell_active);
    if (calls == 3) finish(failures != 1);
}
void kprintf(u8 c, const char *fmt, ...) { (void)c; (void)fmt; }
void _start(void) { shell_loop_run(&pm_shell_loop_ops); finish(1); }
'''
OUTPUT = r'''
u8 kprintf_attr_to_pc98(u8 attr) { return attr; }
static int writes, logs, capture=1, clears;
int boot_serial_mirror,rshell_active=1;
void con_output_register(const ConOutputOps *ops) { CHECK(ops->putchar('x')==0); }
void console_write(const char *s,u32 n,u8 c) { (void)c; CHECK(s && n); writes++; }
void pm_log_write(const char *s,u32 n) { (void)s;(void)n; logs++; }
int con_sink_capture_active(void) { return capture; }
void tvram_clear(void) { clears++; }
void _start(void) {
    pm_console_stream_init();
    console_stream_write("a",1); CHECK(writes==1);
    pm_fd1_write("b",1,2); CHECK(writes==2);
    kprintf(1, "log"); CHECK(logs==1 && writes==2);
    capture=0; kprintf(1, "log"); CHECK(logs==2 && writes==2 && rshell_active==1);
    pm_console_clear(); CHECK(clears==1 && writes==2); finish(0);
}
'''

def execute(tmp, source, harness):
    (tmp/'test.c').write_text(PREFIX+source+harness)
    builtin_headers = subprocess.check_output(['gcc', '-print-file-name=include'], text=True).strip()
    formatter = ['sys/kern/kprintf.c'] if harness == OUTPUT else []
    subprocess.run(['gcc','-m32', '-nostdinc','-isystem',builtin_headers, '-std=gnu11','-D__cdecl=','-DKBOOT_SH=1','-DKBOOT_LINK=1',
        '-ffreestanding','-fno-builtin','-fno-pie','-no-pie','-nostdlib','-static',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-I.','-Iinclude','-Ikernel','-Ikernel/console','-Iplatform/powermac','-Iarch/ppc',
        '-Isdk/include/os32','-Iexec','-Ilib',str(tmp/'test.c'),'sys/kern/kstring_c.c',*formatter,'-o',str(tmp/'test')],cwd=ROOT,check=True)
    return host32.run([str(tmp/'test')],runner='qemu',timeout=10).returncode


def manifest(makefile, flags, fields):
    rule='p3-manifest:;@echo "'+ ' END '.join('$('+f+')' for f in fields)+'"'
    return subprocess.check_output(['make','-s','ARCH=ppc','PLATFORM=powermac','TARGET_MK='+str(makefile),*flags,'--eval='+rule,'p3-manifest'],cwd=ROOT,text=True).strip().split(' END ')


def selection(tmp, mutate):
    original=(ROOT/'build/targets/ppc-powermac.mk').read_text()
    def check(text):
        path=tmp/'image.mk'; path.write_text(text)
        fields = ['KB_OUT', 'KB_SH_OUT']
        assert manifest(path, [], fields) == ['build/out/ppc-powermac', 'build/out/ppc-powermac/user']
        assert manifest(path, ['KBOOT_SH=1'], fields) == ['build/out/ppc-kboot/link-sh', 'build/out/ppc-user/link-sh']
        assert manifest(path, ['KBOOT_SH=1', 'KB_OUT=private'], fields) == ['private', 'private/user']
        link=manifest(path,['KBOOT_SH=1','KBOOT_LINK=1'],['KB_OUT','ROOT_BINS','KB_C','KB_ROOT_SHELL'])
        assert link[0]=='build/out/ppc-kboot/link-sh'
        assert not link[1].strip() and link[3] == 'build/out/ppc-user/link-sh/shell.bin'
        absent=['hostfs_io','hostfs_peer','lan_wire','lan_terminal','terminal_input']
        assert all('platform/powermac/'+name+'.c' not in link[2].split() for name in absent)
        assert all(name in link[2].split() for name in ['sys/net/link.c','sys/net/host_mailbox.c','sys/net/nic.c','sys/kern/dev/device_host.c','sys/platform/powermac/usb_boot.c',
            'sys/platform/powermac/hostfs_link_io.c','sys/platform/powermac/hostfs_link_peer.c','sys/platform/powermac/hostfs_boot.c'])
        for flags,out in [(['KBOOT_SH=1','KBOOT_LINK=0'],'link-sh'),(['KBOOT_LINK=1'],'link'),(['KBOOT_SHELL=1','KBOOT_LINK=0'],'link-sh')]:
            assert manifest(path,flags,['KB_OUT'])[0]=='build/out/ppc-kboot/'+out
        rejected=subprocess.run(['make','-s','ARCH=ppc','PLATFORM=powermac','TARGET_MK='+str(path),'KBOOT_SHELL=1','KBOOT_LINK=1','-n'],cwd=ROOT,capture_output=True)
        assert rejected.returncode!=0
        # The flags are the only image selector here: command-line features or IMAGE must not disagree with KB_OUT.
        for override in ('FEATURE_LINK=0','IMAGE=kboot-core'):
            assert subprocess.run(['make','-s','ARCH=ppc','PLATFORM=powermac','TARGET_MK='+str(path),'KBOOT_SH=1',override,'-n'],cwd=ROOT,capture_output=True).returncode!=0
    check(original)
    if mutate:
        for old,new in [('KB_OUT ?= build/out/ppc-powermac','KB_OUT ?= build/out/ppc-kboot/link-sh'),('KB_SH_OUT ?= $(KB_OUT)/user','KB_SH_OUT ?= build/out/ppc-user/link-sh'),('KB_C := $(C_KERNEL)','KB_C := $(filter-out sys/platform/powermac/hostfs_link_io.c,$(C_KERNEL)) sys/platform/powermac/hostfs_io.c'),('KB_ROOT_SHELL = $(KB_SH_OUT)/shell.bin','KB_ROOT_SHELL = $(KB_SH_OUT)/sh.bin')]:
            assert original.count(old)==1
            try: check(original.replace(old,new))
            except AssertionError: print('RED selection',old)
            else: raise AssertionError(old)


def main():
    cc=Path(os.environ.get('PPC_CROSS_DIR','/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-gcc'
    if not cc.exists() or any(not shutil.which(p) for p in ['gcc','qemu-i386','make']):
        required=os.environ.get('OS32_REQUIRE_PPC')=='1'
        print(('FAIL' if required else 'SKIP')+' ppc-link-sh: PPC toolchain or host runner unavailable')
        return int(required)
    mutate='--mutate' in sys.argv
    work=(ROOT/'sys/platform/powermac/link_platform.c').read_text()
    work=work[work.index('#if KBOOT_SH'):]
    start=work.index('    do {'); end=work.index('    pm_lan_poll',start)
    work=work[:start]+'    hi=1; lo=2; again=hi; (void)again;\n'+work[end:]
    boot=(ROOT/'sys/platform/powermac/ppc_kernel.c').read_text()
    registration=boot[boot.index('#if KBOOT_LINK\n        int work_rc'):]
    registration=registration[:registration.index('        if (work_rc)')]
    work += '\n#include "sys/kern/idle_work.h"\nint idle_work_register(void (*fn)(void)) { CHECK(fn==pm_link_service_once); return 0; }\nstatic void registration(void) {\n'+registration+'CHECK(work_rc==0); }\n'
    cases=[('work','#define PM_LOAD_KDEBUG 1\n'+work,WORK.replace('void _start(void) {','void _start(void) { registration();'),[
        ('    if (running) return;','    (void)running;'),
        ('    pm_usb_poll();','    ;'),
        ('    terminal_receive();','    ;'),
        ('alive.host_generation != terminal_generation ||',''),
        ('while (generation &&','while (0 && generation &&'),
        ('    pm_lan_poll(((u64)hi << 32) | lo);','    (void)lo;'),
        ('int work_rc = idle_work_register(pm_link_service_once);','int work_rc = -1;')]),
        ('provider',(ROOT/'sys/platform/powermac/input_boot.c').read_text(),PROVIDER,[
            ('.try_key = pm_input_usb_take, .peek_key = pm_input_usb_peek,','.try_key = pm_input_usb_peek, .peek_key = pm_input_usb_take,')]),
        ('boot',(ROOT/'sys/platform/powermac/shell_loop_ops.c').read_text()+(ROOT/'sys/kern/init/shell_loop.c').read_text(),BOOT,[
            ('    return gui ? 2 : 0;', '    return gui ? 2 : 1;'),
            ('    shell_print("Cannot load', '    if (0) shell_print("Cannot load'),
            ('rshell_active = 0;', 'rshell_active = 1;'),
            ('rc != EXEC_ERR_FAULT','rc == EXEC_ERR_FAULT')]),
        ('output',(ROOT/'sys/platform/powermac/console_stream.c').read_text()+(ROOT/'sys/platform/powermac/log_printf.c').read_text(),OUTPUT,[
            ('    pm_log_write(bytes, n);','    pm_log_write(bytes, n); console_write(bytes, n, color);'),
            ('    console_write(bytes, len, 1);','    console_write(bytes, len, 1); console_write(bytes, len, 1);')])]
    with tempfile.TemporaryDirectory(prefix='ppc-link-sh-') as d:
        tmp=Path(d); selection(tmp,mutate)
        for name,source,harness,mutations in cases:
            assert execute(tmp,source,harness)==0,name
            print('PASS',name)
            if mutate:
                for old,new in mutations:
                    assert source.count(old)==1,old
                    assert execute(tmp,source.replace(old,new),harness)==1,(name,old)
                    print('RED runtime',name,old)
    return 0


def run(ctx):
    return subprocess.call([sys.executable,'-B',__file__,*(['--mutate'] if ctx.mutate else [])],cwd=ctx.root)

if __name__=='__main__': sys.exit(main())
