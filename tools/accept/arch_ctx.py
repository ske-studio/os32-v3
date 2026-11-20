#!/usr/bin/env python3
"""AC-CTX common columns; x86 requires an independently observed NP21/W image.

Expectation origin: docs/decisions/ARCH_CTX_DESIGN.md AC-CTX-1..7 and the W4e row of
its acceptance table. Resident entry, fault retirement and FP follow TASK_PPC_ALIGN_X86 B03/B09/B15 and the stage B entry design §5.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess
import sys
import tempfile
import time
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/ppc'))
import kboot_vectors as v
from kboot_accept import build
from kboot_run import ROM, QMP, command, require, serial
from run import CriterionFailure

COLUMNS = {
    'irq': r'pr=0 ee=1 depth=1/0 sentinel=1 dec=\d+ ipi=\d+ poll=\d+',
    'exit': r'boundaries=3 restored=1',
    'lifecycle': r'normal=1 exit=1 kill=1 park=1 resume=1 tick=\d+',
    'stack': r'used=\d+ size=32768 guard=1',
}
X86_EXPECTED = dict(COLUMNS, irq=r'cpl=0 if=1 depth=1/0 sentinel=1 irq=\d+ poll=\d+', stack=r'used=\d+ size=\d+ guard=1')
USER = r'''
#include "os32api.h"
unsigned initial[9];
__asm__(".section .text.startup,\"ax\"\n.globl _start\n_start:\n"
        "lis 7,initial@ha\naddi 7,7,initial@l\nstw 1,0(7)\nstw 3,4(7)\n"
        "stw 4,8(7)\nstw 5,12(7)\nstw 6,16(7)\n"
        ".irp off,0,4,8,12\nlwz 8,\\off(1)\nstw 8,20+\\off(7)\n.endr\nb ctx_crt_entry\n.previous");
int main(int argc, char **argv, KernelAPI *api)
{
    if ((initial[0] & 15) || initial[1] != (unsigned)argc ||
        initial[2] != (unsigned)argv || initial[3] != (unsigned)api || initial[4] ||
        initial[5] || initial[6] || initial[7] || initial[8])
        return 93;
    volatile unsigned sentinel = 0x13579bdf;
    /* Leave a distinct nonvolatile FP register and rounding mode at syscall exit. */
    __asm__ volatile("lfd 31,0(%0); mtfsfi 7,2" :: "r"(&(const double){9.0}) : "memory");
    unsigned expected = api->sys_get_mem_kb();
    if (argc > 1 && argv[1][0] == 'f') __asm__ volatile(".globl ctx_user_trap\nctx_user_trap: trap" ::: "memory");
    if (argc > 1) {
        int result = api->sys_yield();
        if (result == 1) return 90;
        if (result || sentinel != 0x13579bdf) return 92;
        return 9;
    }
    (void)argv;
    for (unsigned n=0; n<2000; ++n) {
        api->sys_halt();
        if (api->sys_yield() || api->sys_get_mem_kb() != expected ||
            sentinel != 0x13579bdf) return 91;
    }
    return sentinel == 0x13579bdf ? 7 : 95;
}
'''

def once(text, old, new):
    require(text.count(old) == 1, 'unique anchor '+old)
    return text.replace(old, new, 1)

def image(work, mutation):
    work.mkdir()
    command(['make', '--no-print-directory', 'ARCH=ppc', 'PLATFORM=powermac',
             'SDK_TARGET=ppc-powermac', 'hello', 'ppc-fault'])
    prefix = str(Path(os.environ['PPC_CROSS_DIR'])/'bin/powerpc-elf-')
    src = work/'user.c'; src.write_text(USER)
    # Same USER ABI and CRT/newlib closure as build/ppc-user.mk (PPC_USER_ABI,
    # PPC_USER_CRT, PPC_USER_LIBS): start.c calls _init and the CRT help.
    abi = ['-mcpu=750', '-mhard-float', '-mcall-sysv', '-mno-eabi']
    command([prefix+'gcc', '-include', 'sdk/include/os32/os32_unit_stamp.h', '-std=gnu11', '-O2', '-m32', '-mbig-endian',
             *abi, '-mno-altivec', '-msdata=none',
             '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-Wno-attributes',
             '-Isdk/include/os32', '-c', src, '-o', work/'user.o'])
    libc = command([prefix+'gcc', *abi, '-print-file-name=libc.a']).strip()
    libgcc = command([prefix+'gcc', *abi, '-print-libgcc-file-name']).strip()
    out = 'build/out/ppc-user/'
    command([prefix+'objcopy', '--redefine-sym', '_start=ctx_crt_entry', out+'crt0.o', work/'crt0.o'])
    command(['python3', 'sdk/link_guard.py', '--target', 'ppc-powermac', '--ar', prefix+'ar', prefix+'ld',
             '-m', 'elf32ppc', '--gc-sections', '-z', 'noexecstack', '-T', 'sdk/link/ppc/app.ld',
             '-u', '_printf_float', '-o', work/'ctx.elf', work/'crt0.o', out+'crt0_c.o',
             out+'syscalls.o', out+'help.o', work/'user.o',
             '-L'+str(Path(libc).parent), '-L'+str(Path(libgcc).parent),
             out+'nano/libos32nano.a', '-lc', '-lgcc'])
    command([prefix+'objcopy', '-O', 'binary', work/'ctx.elf', work/'ctx.raw'])
    command(['python3', 'sdk/mkos32x.py', work/'ctx.raw', work/'ctx.bin',
             '--target', 'ppc-powermac', '--elf', work/'ctx.elf'])
    resident_ld = work/'app.ld'
    resident_ld.write_text(once((ROOT/'sdk/link/ppc/app.ld').read_text(),
                                '0x80100000', '0x400000'))
    residents = []
    for gen in range(1, 5):
        base = work/f'r{gen}'
        command([prefix+'gcc', *abi, '-msdata=none', '-ffreestanding', '-fno-pie',
                 '-fno-stack-protector', '-O2', '-Wno-attributes', '-Isdk/include/os32', '-I.', '-Iinclude',
                 '-include', 'sdk/include/os32/os32_unit_stamp.h', f'-DGENERATION={gen}',
                 '-c', 'tools/accept/arch_ctx_resident.c', '-o', base.with_suffix('.o')])
        inputs = [base.with_suffix('.o')]
        if gen == 4:
            inputs += [work/'crt0.o', out+'crt0_c.o', out+'help.o', out+'syscalls.o',
                       '-L'+str(Path(libc).parent), '-L'+str(Path(libgcc).parent),
                       out+'nano/libos32nano.a', '-lc', '-lgcc']
        command(['python3', 'sdk/link_guard.py', '--target', 'ppc-powermac', '--ar',
                 prefix+'ar', prefix+'ld', '--gc-sections', '--no-undefined', '-T', resident_ld, *inputs,
                 '-o', base.with_suffix('.elf')])
        command([prefix+'objcopy', '-O', 'binary', base.with_suffix('.elf'), base.with_suffix('.raw')])
        command(['python3', 'sdk/mkos32x.py', base.with_suffix('.raw'), base.with_suffix('.bin'),
                 '--elf', base.with_suffix('.elf'), '--target', 'ppc-powermac'])
        residents.append(str(base.with_suffix('.bin')))
    trap = (ROOT/'arch/ppc/kernel_trap.c').read_text()
    trap = '#include "ppc_trap.h"\n#include "exec_arch.h"\nvoid ctx_irq(struct ppc_frame *, void (*)(struct ppc_frame *));\nvoid ctx_syscall(struct arch_syscall *);\nvoid ctx_exit_arm(void);\nextern volatile u32 ctx_started;\n'+trap
    trap = 'struct ppc_frame; void ctx_fault(struct ppc_frame *);\n'+trap
    trap = once(trap, '        ring3_fault_kill();',
                '        extern void ctx_user_fault(struct ppc_frame *);\n'
                '        ctx_user_fault(f); ring3_fault_kill();')
    trap = once(trap, '        resident_fault(f);', '        { ctx_fault(f); resident_fault(f); }')
    trap = once(trap, '            handler(f);', '            ctx_irq(f, handler);')
    trap = once(trap, '            exec_syscall_dispatch(&call);', '            ctx_syscall(&call);')
    trap = once(trap, '            (void)ppc_irq_save();\n            return;',
                '            (void)ppc_irq_save();\n            ctx_exit_arm();\n            return;')
    vectors = (ROOT/'arch/ppc/kernel_vectors.S').read_text()
    head, vectors = vectors.split('ppc_kernel_vector_return:\n', 1)
    vectors = v.user_exit_enable(vectors)
    # Each boundary uses scratch r3/r4 before register/SRR restoration.
    sites = ('    lwz 3,PPC_CALL_SIZE+PPC_FRAME_SRR1(1)\n    andi. 3,3,PPC_MSR_PR',
             '.Lrestore_user:\n',
             '    lwz 3,PPC_CALL_SIZE+PPC_FRAME_LR(1)\n    mtlr 3\n    lwz 3,PPC_CALL_SIZE+PPC_FRAME_CTR(1)')
    for n, site in enumerate(sites, 1):
        code = f'''
    lwz 4,PPC_CALL_SIZE+PPC_FRAME_SRR1(1)
    andi. 4,4,PPC_MSR_PR
    beq 91f
    lis 3,ctx_exit_request@ha
    lwz 4,ctx_exit_request@l(3)
    cmpwi 4,{n}
    bne 91f
    li 4,0
    stw 4,ctx_exit_request@l(3)
    lis 3,ctx_exit_phase@ha
    li 4,{n}
    stw 4,ctx_exit_phase@l(3)
    li 3,-1
    mtdec 3
    li 4,20000
90: addic. 4,4,-1
    bne 90b
91:
'''
        if n == 3:
            pos = vectors.index('.Lrestore_user:')
            vectors = vectors[:pos] + once(vectors[pos:], site, code+site)
        else:
            vectors = once(vectors, site, site+code if n == 2 else code+site)
    vectors = head + 'ppc_kernel_vector_return:\n' + vectors
    mutations = {
        'enable': ('trap', '            ppc_enable();', ''),
        'pr-frame': ('vectors', '    bne .Luser_frame', '    nop'),
        'depth': ('trap', '            --kctx_irq_depth;', '            if (!ctx_started) --kctx_irq_depth;'),
        'exit-ee': ('vectors', 'ori 4,4,(PPC_MSR_EE|PPC_MSR_IR|PPC_MSR_DR|PPC_MSR_RI)',
                    'ori 4,4,(PPC_MSR_IR|PPC_MSR_DR|PPC_MSR_RI)'),
        'srr': ('vectors', '    mtsrr1 1\n    mfsprg1 0', '    mtsrr1 0\n    mfsprg1 0'),
    }
    if mutation in mutations:
        target, old, new = mutations[mutation]
        if target == 'trap': trap = once(trap, old, new)
        else: vectors = once(vectors, old, new)
    trap = once(trap, '            return;\n        }\n        reason = PPC_TRAP_UNREGISTERED;',
                '            if (ctx_started && kctx_irq_depth == 1) {\n'
                '                extern void ctx_depth_mismatch(void); ctx_depth_mismatch();\n'
                '            }\n            return;\n        }\n        reason = PPC_TRAP_UNREGISTERED;')
    # r30 still contains saved SRR1[PR] at the supervisor fall-through.
    vectors = once(vectors, '    mfsprg3 30\n    cmpwi 30,PPC_VECTOR_DSI',
                   '    cmpwi 30,0\n    bne ctx_bad_pr_frame\n'
                   '    mfsprg3 30\n    cmpwi 30,PPC_VECTOR_DSI')
    vectors = once(vectors, '    mfsprg1 0\n    mfsprg2 1\n    rfi', """
    mfcr 0
    mtsprg3 0
    mfsrr1 0
    cmpw 0,1
    bne ctx_bad_srr
    mfsprg3 0
    mtcr 0
    mfsprg1 0
    mfsprg2 1
    rfi""")
    outside = {'outside-dsi': 2, 'outside-program': 3}.get(mutation, 0)
    user_trap = next(int(line.split()[0], 16) for line in
                     command([prefix+'nm', work/'ctx.elf']).splitlines()
                     if line.endswith(' ctx_user_trap'))
    shell = work/'boot.c'
    shell.write_text(f'#define CTX_USER_TRAP {user_trap:#x}U\n#define CTX_OUTSIDE {outside}\n#include "'+str(ROOT/'tools/accept/arch_ctx_probe.c')+'"\n')
    t = work/'trap.c'; t.write_text(trap)
    # Link the same measured entry and longjmp bodies in every fixture.
    entry = (ROOT/'arch/ppc/exec_enter.S').read_text()
    jump = (ROOT/'arch/ppc/setjmp.S').read_text()
    jump = jump[jump.index('    .globl exec_longjmp'):]
    entry = once(entry, '    mr 30,3',
                 '    cmpwi 4,0\n    beq ctx_bad_root_branch\n    mr 30,3')
    entry = once(entry, '    bctrl', """
    lis 8,.Lparent_sp@ha
    lwz 8,.Lparent_sp@l(8)
    lwz 9,0(31)
    cmpw 9,8
    bne ctx_bad_parent_link
    lwz 9,4(8)
    lis 10,.Lreturned@ha
    addi 10,10,.Lreturned@l
    cmpw 9,10
    bne ctx_bad_parent_link
    bctrl""")
    jump = once(jump, '.Lland:\n', """.Lland:
    mflr 4
    cmpw 4,0
    bne ctx_bad_child_return
""")
    if mutation == 'root-branch':
        entry = once(entry, '    beq .Lresident', '    bne .Lresident')
    if mutation == 'resident-aux':
        entry = once(entry, '    lwz 6,20(7)', '    li 6,0')
    if mutation == 'child-return':
        jump = once(jump, '    mtlr 0', '    mtctr 0')
    wrapper = (ROOT/'tools/accept/arch_ctx_parent.S').read_text()
    vectors_file = work/'vectors.S'
    vectors_file.write_text(vectors + '\n' + wrapper +
        '\n#define arch_entry_enter ctx_measured_entry\n' + entry +
        '\n#include "arch_jmp_storage.h"\n#define exec_longjmp __wrap_exec_longjmp\n' + jump)
    opts = ['-j6', 'KB_KEEP_RESET=-Wl,--wrap=arch_entry_enter,--wrap=exec_longjmp,'
                    '--wrap=arch_cpu_halt,--wrap=ppc_write_dec', 'KBOOT_SH=1', 'KBOOT_LINK=0',
                 'KB_ROOT_BINS='+' '.join(residents+[str(work/'ctx.bin')]),
                 'KB_MUT_SRC=platform/powermac/shell_loop_ops.c', 'KB_MUT_FILE='+str(shell),
                 'KB_MUT_EXTRA_SRC=arch/ppc/kernel_trap.c', 'KB_MUT_EXTRA_FILE='+str(t),
                 'KB_MUT_THIRD_SRC=arch/ppc/kernel_vectors.S', 'KB_MUT_THIRD_FILE='+str(vectors_file)]
    elf = build(work/'image', *opts)
    changes = {
        'parent-root': ('exec/exec.c', '#include "exec_finish.inc"', None),
        'dec-rearm': ('arch/ppc/kernel_dec.c', '    ppc_write_dec((u32)remaining);', '    extern volatile u32 ctx_started;\n    if (!ctx_started) ppc_write_dec((u32)remaining);'),
        'halt-ee': ('kernel/idle_work.c', '    idle_work_run();\n    if (!net_rx_recent(NET_RX_ACTIVE_MS)) _idle();',
                    '    idle_work_run();\n    if (!net_rx_recent(NET_RX_ACTIVE_MS)) { _disable(); for (;;) _halt(); }'),
        'resume-result': ('exec/exec.c', '#include "exec_schedule.inc"', None),
    }
    if mutation in changes:
        source, old, new = changes[mutation]
        text = (ROOT/source).read_text()
        if mutation == 'parent-root':
            inc = work/'finish.inc'
            inc.write_text(once((ROOT/'exec/exec_finish.inc').read_text(),
                                '        g_cur_app = 0;\n        paging_load_cr3(paging_kernel_pd_phys());',
                                '        g_cur_app = 0;\n'
                                '        AppSlot *child = appslot_at(APP_ID_SHELL + 1);\n'
                                '        paging_load_cr3(child->as ? child->as->pd_phys : paging_kernel_pd_phys());'))
            new = '#include "'+str(inc)+'"'
        if mutation == 'resume-result':
            schedule = work/'schedule.inc'
            schedule.write_text(once((ROOT/'exec/exec_schedule.inc').read_text(),
                                      'arch_user_result(&a->frame, 0);',
                                      'arch_user_result(&a->frame, 1);'))
            new = '#include "'+str(schedule)+'"'
        altered = work/('product'+Path(source).suffix); altered.write_text(once(text, old, new))
        # Recompile exactly one object using make's recorded flags, then let
        # make relink/audit it. No source or object in a default image changes.
        log = (work/'image.build.log').read_text().splitlines()
        cc = [shlex.split(line) for line in log if ' -c '+source+' -o ' in line]
        require(len(cc) == 1, 'one product compile '+source)
        cc = cc[0]; cc[cc.index('-c')+1] = str(altered)
        command(cc)
        elf = build(work/'image', *opts)
    return elf

# Serial witnesses are complete lines. Assembly witnesses stop at a symbol
# reached only by the corresponding register comparison.
SERIAL_RED = {
    'resident-aux': 'resident-aux gen=1 aux=0',
    'parent-root': 'parent-root parked=2 root=child',
    'enable': 'enable syscall ee=0',
    'depth': 'depth irq-return depth=1',
    'resume-result': 'resume-result yield=1',
}
STOP_RED = {
    'root-branch': ('ctx_bad_root_branch', 'CTX BEGIN'),
    'child-return': ('ctx_bad_child_return', 'CTX irq '),
    'srr': ('ctx_bad_srr', 'CTX entry gen=1 '),
    'pr-frame': ('ctx_bad_pr_frame', 'CTX entry gen=1 '),
    'dec-rearm': ('ctx_bad_dec_rearm', 'CTX BEGIN'),
    'halt-ee': ('ctx_bad_halt_ee', 'CTX entry gen=1 '),
    'exit-ee': ('ppc_kernel_vector_stop', 'CTX irq '),
}

def observe(elf, cpu, out, mode):
    out.mkdir()
    cd = out/'cd'; cd.mkdir(); shutil.copyfile(elf, cd/'PPCKERN.ELF')
    iso = out/'boot.iso'
    command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', iso, cd])
    a, b = out/'serial-a.log', out/'serial-b.log'
    argv = ['qemu-system-ppc', '-M', 'mac99', '-cpu', cpu, '-m', '128M', '-smp', '1',
            '-accel', 'tcg', '-bios', str(ROM), '-cdrom', str(iso), '-boot', 'd',
            '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF', '-prom-env', 'auto-boot?=true',
            '-nographic', '-serial', 'file:'+str(a), '-serial', 'file:'+str(b),
            '-monitor', 'none', '-qmp', 'stdio', '-no-reboot', '-nic', 'none']
    (out/'argv.json').write_text(json.dumps(argv))
    prefix = str(Path(os.environ['PPC_CROSS_DIR'])/'bin/powerpc-elf-')
    symbols = {parts[-1]: int(parts[0], 16)
               for line in command([prefix+'nm', '-n', elf]).splitlines()
               if len(parts := line.split()) == 3}
    done, stop = symbols['ctx_done'], symbols['ppc_trap_stop']
    deadline = time.monotonic()+45
    q = None
    with (out/'stderr').open('w') as err:
        p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, bufsize=0)
        try:
            q = QMP(p, deadline)
            next_sample = 0
            while time.monotonic() < deadline:
                text = serial(a).replace('\r', '')
                if mode.startswith('outside-') and 'FATAL' in text:
                    vector = '300' if mode == 'outside-dsi' else '700'
                    require('CTX outside '+vector in text and 'FATAL trap vector=0x'+vector in text,
                            'unexpected outside fault')
                    q.call('stop')
                    regs = q.call('human-monitor-command', {'command-line':'info registers'})
                    pc = re.search(r'NIP\s+([0-9a-fA-F]+)', regs)
                    if pc and int(pc[1], 16) == stop: return None
                    q.call('cont')
                    continue
                if 'CTX mismatch ' in text:
                    reports = re.findall(r'^CTX mismatch (.*)\n', text, re.M)
                    if reports:
                        require('CTX BEGIN\n' in text and 'FATAL' not in text and
                                'CTX FAIL' not in text.split('CTX mismatch ', 1)[0] and
                                reports == [SERIAL_RED.get(mode)], 'unexpected mismatch: '+str(reports))
                        return reports[0]
                require('CTX FAIL' not in text and 'FATAL' not in text,
                        'unexpected guest failure in '+mode)
                candidates = STOP_RED if mode == 'normal' else (mode,) if mode in STOP_RED else ()
                if candidates and 'CTX BEGIN\n' in text and time.monotonic() >= next_sample:
                    q.call('stop')
                    regs = q.call('human-monitor-command', {'command-line':'info registers'})
                    pc = re.search(r'NIP\s+([0-9a-fA-F]+)', regs)
                    msr = re.search(r'MSR\s+([0-9a-fA-F]+)', regs)
                    stopped_text = serial(a).replace('\r', '')
                    for candidate in candidates:
                        symbol, marker = STOP_RED[candidate]
                        if not pc or int(pc[1], 16) != symbols[symbol]:
                            continue
                        require(marker in stopped_text, 'missing stop context for '+candidate)
                        if candidate == 'exit-ee':
                            lines = re.findall(r'^CTX [^\n]*', stopped_text, re.M)
                            require(lines and re.fullmatch('CTX irq '+COLUMNS['irq'], lines[-1])
                                    and msr and int(msr[1], 16) == 0x1000,
                                    'unexpected exit-ee vector stop')
                        require(mode == candidate, 'unexpected stop in '+mode+' at '+symbol)
                        return symbol
                    q.call('cont')
                    next_sample = time.monotonic() + .25
                if 'CTX PASS\n' in text:
                    require(not mode.startswith('outside-'), 'outside fault did not stop')
                    q.call('stop')
                    regs = q.call('human-monitor-command', {'command-line':'info registers'})
                    pc = re.search(r'NIP\s+([0-9a-fA-F]+)', regs)
                    if not pc or int(pc[1], 16) != done:
                        q.call('cont'); continue
                    for gen in range(1, 5):
                        require(f'CTX resident generation {gen}\n' in text, 'variadic real table')
                        require(f'CTX retired gen={gen} ' in text, 'resident retirement')
                    for col, expected in COLUMNS.items():
                        require(re.search(r'^CTX '+col+' '+expected+r'$', text, re.M), 'missing '+col)
                    stack = re.search(r'CTX stack used=(\d+) size=(\d+) guard=1', text)
                    require(stack and 0 < int(stack[1]) < int(stack[2]) == 32768, 'stack bounds')
                    q.call('quit'); p.wait(timeout=3)
                    require(p.returncode == 0, 'QEMU exit code')
                    return None
                if p.poll() is not None: raise RuntimeError('QEMU exited '+str(p.returncode))
                time.sleep(.02)
            raise CriterionFailure('watchdog expired in '+mode)
        finally:
            if q and p.poll() is None:
                q.deadline = time.monotonic()+3
                try:
                    q.call('stop')
                    (out/'registers').write_text(q.call('human-monitor-command', {'command-line':'info registers'}))
                finally: q.close()
            elif q: q.close()
            if p.poll() is None: p.terminate()
            try: p.wait(timeout=3)
            except subprocess.TimeoutExpired: p.kill(); p.wait()

# x86 images are private snapshots; no source selection or product object changes.
X86_USER = r'''
#include "ring3_marker.h"
OS32_KAPI_LAYOUT_STAMP();
void _start(int argc, char **argv, void *api) __attribute__((section(".text.startup"), used, noreturn));
void _start(int argc, char **argv, void *api)
{
    (void)argv; (void)api;
    volatile unsigned sentinel = 0x13579bdf;
    if (argc > 1) {
        if (r3_call(KAPI_SLOT_SYS_YIELD, 0) || sentinel != 0x13579bdf) r3_exit(90);
        r3_exit(9);
    }
    volatile unsigned long *mark = r3_marker(0x4354);
    while (!mark[6]) __asm__ volatile("" ::: "memory");
    for (unsigned n = 0; n < 40; ++n) {
        r3_call(KAPI_SLOT_SYS_HALT, 0);
        if (r3_call(KAPI_SLOT_SYS_YIELD, 0) || sentinel != 0x13579bdf) r3_exit(91);
        unsigned flags; __asm__ volatile("pushfl; popl %0" : "=r"(flags));
        if (!(flags & 0x200)) r3_exit(92);
    }
    r3_exit(7);
}
'''

def x86_image(work, mutation='normal'):
    sys.path.insert(0, str(ROOT/'tools'))
    from build_compare import snapshot
    tree = work/'image'
    snapshot(ROOT, 'HEAD', tree)
    for font in (ROOT/'assets/fonts').glob('*'):
        if font.is_file(): shutil.copy2(font, tree/'assets/fonts'/font.name)
    def patch(name, old, new):
        path = tree/name; path.write_text(once(path.read_text(), old, new))
    patch('arch/x86/kernel/syscall_entry.c', 'extern void ring3_fault_kill(void);',
          '#include "arch/x86/arch_ctx_probe.c"\nextern void ring3_fault_kill(void);')
    patch('arch/x86/kernel/syscall_entry.c', '    exec_syscall_dispatch(&call);', '    ctx_syscall(&call);')
    patch('kernel/idle_work.c', '    running = 1;',
          '    extern void ctx_shell_poll(void); ctx_shell_poll();\n    running = 1;')
    patch('kernel/irq/tick.c', '            slots[slot].fn();',
          '            extern void ctx_tick_slot(unsigned); ctx_tick_slot(slot);\n            slots[slot].fn();')
    patch('kernel/idle_work.c', '        work[i]();',
          '        { extern void ctx_foreground(void); ctx_foreground(); work[i](); }')
    asm = tree/'arch/x86/kernel/isr_stub.asm'
    text = asm.read_text().replace('%macro IRQ_ENTER 0', '%macro IRQ_ENTER 1')
    lines, irq = [], None
    for line in text.splitlines(True):
        m = re.match(r'irq_stub_(common_%1|\d+):', line)
        if m: irq = '%1' if m[1].startswith('common') else m[1]
        if line.strip() == 'IRQ_ENTER': line = '        IRQ_ENTER '+irq+'\n'
        lines.append(line)
    text = ''.join(lines)
    text = once(text, 'extern kctx_irq_depth', 'extern ctx_irq, ctx_irq_leave, ctx_record\nextern kctx_irq_depth')
    text = once(text, '        inc     dword [ss:kctx_irq_depth]', '''        inc     dword [ss:kctx_irq_depth]
        pushad
        RESTORE_KSEG
        cld
        mov eax, esp
        push dword %1
        push eax
        call ctx_irq
        add esp, 8
        popad''')
    text = once(text, '        dec     dword [ss:kctx_irq_depth]', '''        dec     dword [ss:kctx_irq_depth]
        pushad
        RESTORE_KSEG
        cld
        call ctx_irq_leave
        popad''')
    asm.write_text(text)
    patch('arch/x86/kernel/ring3_entry.asm', 'extern ring3_syscall_dispatch', 'extern ctx_boundary\nextern ring3_syscall_dispatch')
    for old, offset in [('        popad                           ;; eax = 戻り値 (dispatcher が frame[7] に格納)', 0),
                        ('        IRETD_USER', 4)]:
        patch('arch/x86/kernel/ring3_entry.asm', old,
              f'        pushfd\n        pop dword [ss:ctx_boundary+{offset}]\n'+old)
    patch('arch/x86/kernel/iret_user.inc', '        iretd',
          '        pushfd\n        pop dword [ss:ctx_boundary+8]\n        iretd')
    # Every translation unit expanding IRETD_USER needs this diagnostic symbol.
    p = tree/'arch/x86/kernel/iret_user.inc'
    p.write_text('extern ctx_boundary\n'+p.read_text())
    mutations = {
        'sti': ('arch/x86/kernel/ring3_entry.asm', '        sti\n', '        nop\n'),
        'depth': ('arch/x86/kernel/isr_stub.asm', '        inc     dword [ss:kctx_irq_depth]',
                  '        cmp dword [ss:ctx_record+4], 1\n        je %%ctx_skip_depth\n'
                  '        inc     dword [ss:kctx_irq_depth]\n%%ctx_skip_depth:'),
        'sentinel': ('arch/x86/arch_ctx_probe.c', '        kmemcpy(ctx_saved, f, sizeof(ctx_saved));',
                     '        kmemcpy(ctx_saved, f, sizeof(ctx_saved));\n        ctx_saved[0] ^= 1;'),
    }
    if mutation != 'normal': patch(*mutations[mutation])
    (tree/'userland/tests/ring3_hello.c').write_text(X86_USER)
    env = dict(os.environ, OS32_BUILD_COMMIT='ctx-x86-'+mutation)
    with (work/'build.log').open('w') as log:
        subprocess.run(['make', 'kernel', 'ring3_hello'], cwd=tree, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    shutil.copy2(tree/'userland/tests/ring3_hello.bin', work/'ctx.bin')
    print('diagnostic='+str(tree/'build/out')+' user='+str(work/'ctx.bin'), flush=True)
    return tree

def x86_columns(r):
    require(len(r) == 20 and r[0] == 0x43545838 and r[1] == 2, 'probe incomplete/failed')
    require(r[2] > 2 and r[3] and r[4] and r[5] >= 20, 'timer/device IRQ progress')
    require(all(f & 2 and not f & 0x200 for f in r[6:9]), 'exit boundaries')
    require(0 < r[9] < r[10] and r[11] == 1, 'stack bounds/guard')
    require(r[13] == 31 and r[12] > 3000 and r[14] > 40 and r[15] >= 3000, 'lifecycle/progress')
    require(r[16] and r[17] > r[18], 'LAN rescue/cooperative foreground')
    return dict(irq=f'cpl=0 if=1 depth=1/0 sentinel=1 irq={r[2]} poll={r[5]}',
                exit='boundaries=3 restored=1',
                lifecycle=f'normal=1 exit=1 kill=1 park=1 resume=1 tick={r[12]}',
                stack=f'used={r[9]} size={r[10]} guard=1')

def x86_host(mutate):
    sys.path.insert(0, str(ROOT/'tools/tests'))
    from kernel_includes import include_flags
    import host32
    with tempfile.TemporaryDirectory(prefix='ctx-x86-host-') as td:
        work = Path(td)
        for name, args in [('normal', '512,0,0,1,0'), ('normal-irq', '0,0,1,1,1'), ('sti', '0,0,0,1,0'),
                           ('depth', '0,0,0,1,1'), ('depth-leave', '512,0,1,1,0'),
                           ('sentinel', '0,0,1,0,1')][:6 if mutate else 2]:
            source = '#define CTX_HOST\n#include "'+str(ROOT/'arch/x86/arch_ctx_probe.c')+'"\n'
            source += 'void _start(void) { int rc = !ctx_valid('+args+'); __asm__ volatile("int $0x80" :: "a"(1), "b"(rc)); }\n'
            (work/'test.c').write_text(source)
            command(['gcc', '-m32', '-std=gnu11', '-O2', '-ffreestanding', '-nostdlib', '-static', '-no-pie',
                     *include_flags(ROOT), work/'test.c', '-o', work/'test'])
            rc = host32.run([str(work/'test')], runner='qemu').returncode
            require(rc == (0 if name.startswith('normal') else 1), 'host predicate '+name)
            print(('GREEN ' if not rc else 'RED ')+name+' runtime predicate (guest assembly untested)', flush=True)
        good = [0x43545838,2,24,23,1,20,2,2,2,4096,16384,1,3100,31,90,3000,3000,90,2,0]
        for col, value in x86_columns(good).items():
            require(re.fullmatch(X86_EXPECTED[col], value), 'column '+col)
        for field in (1,2,3,4,5,6,9,10,11,12,13,14,15,16,17):
            bad = good.copy(); bad[field] = 0
            try: x86_columns(bad)
            except CriterionFailure: continue
            raise RuntimeError('accepted bad column '+str(field))
        gate = (ROOT/'arch/x86/arch_ctx_probe.c').read_text()
        for altered in range(2 if mutate else 1):
            source = once(gate, 'depth == 1', 'depth >= 0') if altered else gate
            (work/'gate.c').write_text(source)
            (work/'test.c').write_text('#define CTX_HOST\n#include "gate.c"\n'
                'void _start(void) { int rc = 0;\n'
                'for (int state=0; state<4; ++state) for (int depth=0; depth<3; ++depth)\n'
                'for (int sink=0; sink<2; ++sink)\n'
                'rc |= ctx_can_start(state,depth,sink) != (!state && depth==1 && !sink);\n'
                '__asm__ volatile("int $0x80" :: "a"(1), "b"(rc)); }\n')
            command(['gcc', '-m32', '-O2', '-ffreestanding', '-nostdlib', '-static', '-no-pie',
                     *include_flags(ROOT), work/'test.c', '-o', work/'test'])
            require(host32.run([str(work/'test')], runner='qemu').returncode == altered,
                    'shell launch gate')
            print(('RED' if altered else 'GREEN')+' shell launch gate runtime', flush=True)
    print('x86 irq=未, exit=未, lifecycle=未, stack=未 (host は合格に数えない)')
    return 0

def x86_np21w(work, mutation):
    import struct
    sys.path.insert(0, str(ROOT/'tools/np21w_mcp'))
    import np21w_client as emu
    from symbols import VerifiedSymbols
    def read(address, length):
        return bytes.fromhex(json.loads(emu.get(f'/api/mem?addr=0x{address:x}&len={length}&space=phys'))['hex'])
    symbols = VerifiedSymbols(read, explicit=work/'image/build/out/kernel.map')
    address = symbols.resolve('ctx_record')  # Includes guest image identity verification.
    require(symbols.info['commit'] == 'ctx-x86-'+mutation, 'wrong diagnostic mutation image')
    deadline = time.monotonic()+90
    emu.post('/api/key', 'seq=SPACE', timeout=15)
    while time.monotonic() < deadline:
        raw = read(address, 80)
        r = struct.unpack('<20I', raw)
        (work/'np21w.json').write_text(json.dumps(dict(record=r, identity=symbols.info)))
        if r[1] == 3:
            require(mutation != 'normal', 'CTX FAIL; x86 columns remain 未')
            print('RED '+mutation+' NP21/W runtime'); return 0
        if r[1] == 2:
            require(mutation == 'normal', 'mutant unexpectedly passed')
            for col, value in x86_columns(r).items(): print('CTX '+col+' '+value)
            print(f'CTX no-syscall tick={r[15]} lan-rescue={r[16]} foreground=0')
            print('CTX PASS (existing CUI/GUI/input/V86 regression remains 未)')
            return 0
        emu.post('/api/key', 'seq=SPACE', timeout=15)
        time.sleep(.25)
    require(mutation != 'normal' and r[1] == 1, 'NP21/W watchdog expired; x86 columns remain 未')
    print('RED '+mutation+' NP21/W watchdog after probe entry'); return 0

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--arch', choices=['ppc', 'x86'], default='ppc')
    parser.add_argument('--host', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--np21w', action='store_true')
    parser.add_argument('--work', type=Path)
    parser.add_argument('--mutation', choices=['normal', 'sti', 'depth', 'sentinel'], default='normal')
    args = parser.parse_args()
    if args.arch == 'x86':
        if args.host: return x86_host(args.mutate)
        work = args.work or Path(os.environ['TMPDIR'])/'arch-ctx-x86'
        if args.build:
            work.mkdir(parents=True, exist_ok=True)
            x86_image(work, args.mutation)
        if args.np21w: return x86_np21w(work, args.mutation)
        require(args.build, 'x86 needs --host, --build or --np21w')
        return 0
    os.environ.setdefault('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')
    missing = [t for t in (str(Path(os.environ['PPC_CROSS_DIR'])/'bin/powerpc-elf-gcc'),
                          'qemu-system-ppc', 'genisoimage') if not shutil.which(t)]
    if not args.build_only and (missing or not ROM.is_file()):
        print('SKIP arch-ctx-guest: toolchain/QEMU/OpenBIOS unavailable')
        return int(os.environ.get('OS32_REQUIRE_PPC') == '1')
    work = Path(tempfile.mkdtemp(prefix='arch-ctx-', dir=os.environ['TMPDIR']))
    (work/'expected.json').write_text(json.dumps(dict(ppc=COLUMNS, x86=X86_EXPECTED), indent=2))
    print('evidence='+str(work), flush=True)
    cpus = ['7450', '7455'] if args.cpu == 'both' else [args.cpu]
    v.CROSS = Path(os.environ['PPC_CROSS_DIR'])/'bin/powerpc-elf-'
    if not args.build_only:
        v.observe_slots(work, cpus, args.mutate)
    hashes = set()
    modes = ['normal', 'outside-dsi', 'outside-program']
    if args.mutate:
        modes += ['root-branch', 'resident-aux', 'child-return', 'parent-root']
        modes += ['enable', 'depth', 'exit-ee', 'srr', 'pr-frame', 'dec-rearm',
                      'halt-ee', 'resume-result']
    for mode in modes:
        elf = image(work/mode, mode)
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        require(digest not in hashes, mode+" duplicate image")
        hashes.add(digest)
        print(mode+" sha256="+digest, flush=True)
        if args.build_only: continue
        for cpu in cpus:
            while os.getloadavg()[0] >= 4: time.sleep(5)
            verdict = observe(elf, cpu, work/(mode+'-'+cpu), mode)
            require((verdict is None) == (mode in ('normal', 'outside-dsi', 'outside-program')), mode+': '+str(verdict))
            print(('GREEN' if verdict is None else 'RED')+' '+mode+' '+cpu+': '+str(verdict), flush=True)
    if args.build_only:
        print(f'PASS distinct images={len(hashes)}')
        return 0
    print('x86 '+', '.join(col+'=未' for col in COLUMNS)+' (合格に数えない)', flush=True)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
