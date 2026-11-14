#!/usr/bin/env python3
"""AC-CTX common observation columns; x86 execution remains explicitly pending.

Expectation origin: docs/decisions/ARCH_CTX_DESIGN.md AC-CTX-1..7 and the W4e row of
its acceptance table (contract, not a record of current behaviour).
"""
import argparse
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

COLUMNS = {
    'irq': r'pr=0 ee=1 depth=1/0 sentinel=1 dec=\d+ ipi=\d+ poll=\d+',
    'exit': r'boundaries=3 restored=1',
    'lifecycle': r'normal=1 exit=1 kill=1 park=1 resume=1 tick=\d+',
    'stack': r'used=\d+ size=32768 guard=1',
}
X86_EXPECTED = dict(COLUMNS, irq=r'cpl=0 if=1 depth=1/0 sentinel=1 irq=\d+ poll=\d+', stack=r'used=\d+ size=\d+ guard=1')
USER = r'''
#include "os32api.h"
int main(int argc, char **argv, KernelAPI *api)
{
    volatile unsigned sentinel = 0x13579bdf;
    unsigned expected = api->sys_get_mem_kb();
    if (argc > 1) {
        if (api->sys_yield() || sentinel != 0x13579bdf) return 90;
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
    command([prefix+'gcc', '-include', 'sdk/include/os32/os32_unit_stamp.h', '-std=gnu11', '-O2', '-m32', '-mcpu=7450', '-mbig-endian',
             '-msoft-float', '-mno-altivec', '-msdata=none', '-mcall-sysv', '-mno-eabi',
             '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-Wno-attributes',
             '-Isdk/include/os32', '-c', src, '-o', work/'user.o'])
    command(['python3', 'sdk/link_guard.py', '--target', 'ppc-powermac', prefix+'ld',
             '-m', 'elf32ppc', '--gc-sections', '-T', 'sdk/link/ppc/app.ld',
             '-o', work/'ctx.elf', 'build/out/ppc-user/crt0.o',
             'build/out/ppc-user/crt0_c.o', work/'user.o'])
    command([prefix+'objcopy', '-O', 'binary', work/'ctx.elf', work/'ctx.raw'])
    command(['python3', 'sdk/mkos32x.py', work/'ctx.raw', work/'ctx.bin',
             '--target', 'ppc-powermac', '--elf', work/'ctx.elf'])
    trap = (ROOT/'arch/ppc/kernel_trap.c').read_text()
    trap = '#include "ppc_trap.h"\n#include "exec_arch.h"\nvoid ctx_irq(struct ppc_frame *, void (*)(struct ppc_frame *));\nvoid ctx_syscall(struct arch_syscall *);\nvoid ctx_exit_arm(void);\nextern volatile u32 ctx_started;\n'+trap
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
    shell = work/'boot.c'; shell.write_text('#include "'+str(ROOT/'tools/accept/arch_ctx_probe.c')+'"\n')
    t = work/'trap.c'; t.write_text(trap)
    vectors_file = work/'vectors.S'; vectors_file.write_text(vectors)
    opts = ['KBOOT_SH=1',
                 'KB_ROOT_BINS='+str(work/'ctx.bin')+' build/out/ppc-user/fault.bin',
                 'KB_MUT_SRC=platform/powermac/shell_boot.c', 'KB_MUT_FILE='+str(shell),
                 'KB_MUT_EXTRA_SRC=arch/ppc/kernel_trap.c', 'KB_MUT_EXTRA_FILE='+str(t),
                 'KB_MUT_THIRD_SRC=arch/ppc/kernel_vectors.S', 'KB_MUT_THIRD_FILE='+str(vectors_file)]
    elf = build(work/'image', *opts)
    changes = {
        'dec-rearm': ('arch/ppc/kernel_dec.c', '    ppc_write_dec((u32)remaining);', '    extern volatile u32 ctx_started;\n    if (!ctx_started) ppc_write_dec((u32)remaining);'),
        'halt-ee': ('kernel/sys.c', '    idle_work_run();\n    _halt();',
                    '    idle_work_run();\n    _disable();\n    _halt();'),
        'resume-result': ('exec/exec.c', '#include "exec_schedule.inc"', None),
    }
    if mutation in changes:
        source, old, new = changes[mutation]
        text = (ROOT/source).read_text()
        if new is None:
            schedule = work/'schedule.inc'
            schedule.write_text(once((ROOT/'exec/exec_schedule.inc').read_text(),
                                      'arch_user_result(&a->frame, 0);',
                                      'arch_user_result(&a->frame, 1);'))
            new = '#include "'+str(schedule)+'"'
        altered = work/'product.c'; altered.write_text(once(text, old, new))
        # Recompile exactly one object using make's recorded flags, then let
        # make relink/audit it. No source or object in a default image changes.
        log = (work/'image.build.log').read_text().splitlines()
        cc = [shlex.split(line) for line in log if ' -c '+source+' -o ' in line]
        require(len(cc) == 1, 'one product compile '+source)
        cc = cc[0]; cc[cc.index('-c')+1] = str(altered)
        command(cc)
        elf = build(work/'image', *opts)
    return elf

def observe(elf, cpu, out):
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
    done = next(int(l.split()[0], 16) for l in command([prefix+'nm', '-n', elf]).splitlines()
                if l.endswith(' ctx_done'))
    deadline = time.monotonic()+45
    q = None
    with (out/'stderr').open('w') as err:
        p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, bufsize=0)
        try:
            q = QMP(p, deadline)
            while time.monotonic() < deadline:
                text = serial(a).replace('\r', '')
                if 'CTX FAIL' in text or 'FATAL' in text:
                    require('CTX BEGIN' in text, 'failure before probe entry')
                    return 'state mismatch'
                if 'CTX PASS\n' in text:
                    q.call('stop')
                    regs = q.call('human-monitor-command', {'command-line':'info registers'})
                    pc = re.search(r'NIP\s+([0-9a-fA-F]+)', regs)
                    if not pc or int(pc[1], 16) != done:
                        q.call('cont'); continue
                    for col, expected in COLUMNS.items():
                        require(re.search(r'^CTX '+col+' '+expected+r'$', text, re.M), 'missing '+col)
                    stack = re.search(r'CTX stack used=(\d+) size=(\d+) guard=1', text)
                    require(stack and 0 < int(stack[1]) < int(stack[2]) == 32768, 'stack bounds')
                    q.call('quit'); p.wait(timeout=3)
                    require(p.returncode == 0, 'QEMU exit code')
                    return None
                if p.poll() is not None: raise RuntimeError('QEMU exited '+str(p.returncode))
                time.sleep(.02)
            require('CTX BEGIN' in serial(a), 'watchdog before probe entry')
            return 'watchdog expired'
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

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    args = parser.parse_args()
    os.environ.setdefault('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')
    missing = [t for t in (str(Path(os.environ['PPC_CROSS_DIR'])/'bin/powerpc-elf-gcc'),
                          'qemu-system-ppc', 'genisoimage') if not shutil.which(t)]
    if missing or not ROM.is_file():
        print('SKIP arch-ctx-guest: toolchain/QEMU/OpenBIOS unavailable')
        return int(os.environ.get('OS32_REQUIRE_PPC') == '1')
    work = Path(tempfile.mkdtemp(prefix='arch-ctx-', dir=os.environ['TMPDIR']))
    (work/'expected.json').write_text(json.dumps(dict(ppc=COLUMNS, x86=X86_EXPECTED), indent=2))
    print('evidence='+str(work), flush=True)
    cpus = ['7450', '7455'] if args.cpu == 'both' else [args.cpu]
    v.CROSS = Path(os.environ['PPC_CROSS_DIR'])/'bin/powerpc-elf-'
    v.observe_slots(work, cpus, args.mutate)
    for mode in ['normal'] + (['enable', 'depth', 'exit-ee', 'srr', 'pr-frame', 'dec-rearm', 'halt-ee', 'resume-result'] if args.mutate else []):
        elf = image(work/mode, mode)
        for cpu in cpus:
            while os.getloadavg()[0] >= 4: time.sleep(5)
            verdict = observe(elf, cpu, work/(mode+'-'+cpu))
            require((verdict is None) == (mode == 'normal'), mode+': '+str(verdict))
            print(('GREEN' if verdict is None else 'RED')+' '+mode+' '+cpu+': '+str(verdict), flush=True)
    print('x86 '+', '.join(col+'=未' for col in COLUMNS)+' (合格に数えない)', flush=True)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
