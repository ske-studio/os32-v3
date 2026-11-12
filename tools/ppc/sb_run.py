#!/usr/bin/env python3
"""S-B Q1/Q2 through the real CLI, with serial input substitution or LAN."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

from kboot_accept import build, mutant_settings
from kboot_run import ROOT, ROM, QMP, command, require, serial, validate, PREFIX
import shell_run

CHECK = dict(name='ppc-sb', tier=2, budget=900, needs=[],
             covers=['tools/ppc/sb_run.py', 'tools/ppc/shell_run.py',
                     'tools/mkroot_image.py', 'build/ppc-user.mk',
                     'userland/tests/ppc_fault.c'],
             inputs=['build/ppc-kboot.mk', 'platform/powermac/cli_boot.c',
                     'arch/ppc/kapi_call.c', 'arch/ppc/kernel_trap.c',
                     'tools/ppc/kboot_accept.py', 'tools/ppc/kboot_run.py',
                     'tools/ppc/run.py', 'userland/tests/ppc_hello.c'])

# Substitute only the input device; parsing, exec_run, output and reclamation
# are the product paths. READY gives the parent time to eject before execution.
INPUT = r'''
#include "kprintf.h"
#include "kernel_tick.h"
__attribute__((noinline, noreturn)) void sb_done(void)
{
    __asm__ volatile(".globl sb_stop\nsb_stop: b sb_stop");
    __builtin_unreachable();
}
static int sb_getkey(void)
{
    static const char commands[] = "/bin/hello.bin ok\n/bin/hello.bin ok\n/bin/fault.bin\n/bin/hello.bin ok\n";
    static unsigned int pos;
    if (!pos) {
        u32 start = tick_count;
        kprintf(7, "SB READY\n");
        while ((u32)(tick_count - start) < 200) pm_terminal_poll();
    }
    if (!commands[pos]) {
        kprintf(7, "SB DONE\n");
        sb_done();
    }
    return commands[pos++];
}
'''
HELLO = '/bin/hello.bin ok\nhello ok\nexit=7\nos32> '
FAULT = '/bin/fault.bin\nexit=-2\nos32> '


def serial_accept(elf, cpu, out, expected):
    out.mkdir()
    cd = out/'cd'; cd.mkdir()
    shutil.copyfile(elf, cd/'PPCKERN.ELF')
    iso = out/'boot.iso'
    command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', iso, cd])
    symbols = {p[2]: int(p[0], 16) for line in command([PREFIX+'nm', '-n', elf]).splitlines()
               if len(p := line.split()) == 3}
    a, b = out/'serial-a.log', out/'serial-b.log'
    args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', cpu, '-m', '128M', '-smp', '1',
            '-accel', 'tcg', '-bios', str(ROM), '-drive', f'file={iso},media=cdrom,id=bootcd',
            '-boot', 'd', '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF',
            '-prom-env', 'auto-boot?=true', '-nographic', '-serial', 'file:'+str(a),
            '-serial', 'file:'+str(b), '-monitor', 'none', '-qmp', 'stdio', '-no-reboot', '-nic', 'none']
    deadline = time.monotonic()+60
    q = None
    ejected = False
    with (out/'qemu.stderr').open('w') as err:
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, bufsize=0)
        try:
            q = QMP(p, deadline)
            while time.monotonic() < deadline:
                aa, bb = serial(a).replace('\r', ''), serial(b)
                validate(aa, bb)
                if not ejected and 'SB READY\n' in aa:
                    require('/bin/hello.bin ok' not in aa, 'execution before eject')
                    q.call('eject', {'device': 'bootcd', 'force': True})
                    ejected = True
                if 'SB DONE\n' in aa:
                    q.call('stop')
                    regs = q.call('human-monitor-command', {'command-line': 'info registers'})
                    (out/'registers.txt').write_text(regs)
                    pc = re.search(r'NIP\s+([0-9a-fA-F]+)', regs)
                    require(pc and int(pc[1], 16) == symbols['sb_stop'], 'unexpected stop PC')
                    validate(aa, bb, True)
                    require(ejected, 'CD not ejected')
                    actual = aa.split('SB READY\n', 1)[1]
                    require(actual == expected+'SB DONE\n', 'Q1/Q2 transcript: '+repr(actual))
                    print(cpu+' serial: '+repr(actual), flush=True)
                    q.call('quit'); p.wait(timeout=2)
                    require(p.returncode == 0, 'QEMU exit')
                    return
                time.sleep(0.01)
            raise RuntimeError('serial acceptance timeout: '+repr(serial(a)))
        finally:
            if q: q.close()
            if p.poll() is None:
                p.terminate()
                try: p.wait(timeout=2)
                except subprocess.TimeoutExpired: p.kill(); p.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--serial', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    cpus = ['7450', '7455'] if args.cpu == 'both' else [args.cpu]
    if not args.serial:
        require(not args.mutate, '--mutate requires --serial')
        skipped = [shell_run.accept(cpu, 60, sb=True) for cpu in cpus]
        if any(skipped):
            print('SKIP LAN socket rc=3'); return 3
        print('PASS S-B LAN Q1/Q2'); return 0
    base = Path(os.environ['TMPDIR'])/'sb-run'; base.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    source = 'platform/powermac/cli_boot.c'
    text = (ROOT/source).read_text()
    injected = work/'cli_boot.c'
    injected.write_text(text.replace('enum { CLI_CAT_BYTES', INPUT+'\nenum { CLI_CAT_BYTES')
                        .replace('.getkey = pm_terminal_getkey', '.getkey = sb_getkey'))
    settings = ['KBOOT_SHELL=1', 'KB_MUT_SRC='+source, 'KB_MUT_FILE='+str(injected)]
    elf = build(work/'normal', *settings)
    expected = HELLO+HELLO+FAULT+HELLO
    for cpu in cpus:
        serial_accept(elf, cpu, work/('normal-'+cpu), expected)
    if args.mutate:
        mutations = [
            ('exit-status', 'arch/ppc/kapi_call.c',
             'return ppc_kapi_invoke(&api, current_slot, values);',
             'struct kapi_values changed = *values;\n'
             '    if (current_slot == (__builtin_offsetof(KernelAPI, sys_exit) - 8) / 4) changed.value[0] = 0;\n'
             '    return ppc_kapi_invoke(&api, current_slot, &changed);',
             expected.replace('exit=7', 'exit=0')),
            ('fault-exit', 'arch/ppc/kernel_trap.c',
             '        ring3_fault_kill();',
             '        extern void kapi_sys_exit(int);\n        kapi_sys_exit(0);',
             expected.replace('exit=-2', 'exit=0'))]
        for name, src, before, after, transcript in mutations:
            extra = mutant_settings(work, name, src, before, after)
            extra = [s.replace('KB_MUT_', 'KB_MUT_EXTRA_') for s in extra]
            mutant = build(work/name, *settings, *extra)
            serial_accept(mutant, cpus[0], work/(name+'-guest'), transcript)
            require(transcript != expected, 'mutation did not change verdict')
            print('RED '+name+': exact transcript and sb_stop (QEMU rc=0)', flush=True)
    print('PASS S-B serial Q1/Q2 evidence='+str(work))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
