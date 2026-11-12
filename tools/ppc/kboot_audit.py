#!/usr/bin/env python3
"""Link-time audits of the PowerPC core-ready kernel (PPC_KBOOT_DESIGN H3, R7).

  1. no undefined symbol;
  2. integer-only: every decoded instruction of the ELF is checked against the
     floating-point and AltiVec mnemonic families of the 7450/7455;
  3. no translation unit *uses* PIT_HZ, directly or through another macro
     (e.g. TICK_MS): the kernel ticks at 1 kHz, PIT_HZ is the 100 Hz PC-98 rate.

Called by build/ppc-kboot.mk. Any tool failure is a failed audit.
"""
import argparse
import os
import re
import subprocess
import sys
sys.dont_write_bytecode = True

# dssall stops data streams before HID0 changes (MPC7450UM table 2-6 note 2).
# It is in the AltiVec book but touches no vector state and takes no
# AltiVec-unavailable exception, so the integer kernel may issue it.
ALLOWED = {'dssall'}
# Everything starting with 'f' is floating point in the 32-bit PowerPC set.
FLOAT = re.compile(r'^(f[a-z0-9.]+|l(fs|fd)u?x?|lfiwax|stf(s|d)u?x?|stfiwx|'
                   r'mffs\.?|mtfsf[i]?\.?|mtfsb[01]\.?|mcrfs)$')
ALTIVEC = re.compile(r'^(v[a-z0-9.]+|lv(e[bhw])?xl?|lvs[lr]|stv(e[bhw])?xl?|'
                     r'mfvscr|mtvscr|dstt?|dststt?|dss|dssall|mfvrsave|mtvrsave)$')
POISON = ('PIT_HZ',)


def run(args):
    result = subprocess.run(args, text=True, capture_output=True)
    if result.returncode:
        raise SystemExit('audit tool failed: '+' '.join(args[:3])+'\n'+result.stderr[-1500:])
    return result.stdout


def instructions(objdump, elf):
    for line in run([objdump, '-d', '--no-show-raw-insn', elf]).splitlines():
        match = re.match(r'^\s*[0-9a-f]+:\s+([a-z][a-z0-9.+-]*)', line)
        if match:
            yield match[1], line.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--elf', required=True)
    parser.add_argument('--nm', required=True)
    parser.add_argument('--objdump', required=True)
    parser.add_argument('--sources', nargs='+', required=True)
    parser.add_argument('--mutant', nargs=2, action='append', metavar=('SOURCE', 'FILE'),
                        help='audit FILE in place of product SOURCE (mutation builds)')
    parser.add_argument('cc', nargs=argparse.REMAINDER,
                        help='-- compiler and the exact flags used for kernel TUs')
    args = parser.parse_args()
    args.cc = [a for a in args.cc if a != '--']
    if not args.cc:
        parser.error('compiler command required after --')
    problems = []
    undefined = run([args.nm, '-u', args.elf]).split()
    if undefined:
        problems.append('undefined symbols: '+' '.join(undefined))
    count = 0
    for mnemonic, line in instructions(args.objdump, args.elf):
        count += 1
        if mnemonic not in ALLOWED and (FLOAT.match(mnemonic) or ALTIVEC.match(mnemonic)):
            problems.append('floating-point/AltiVec instruction: '+line)
    if count < 1000:
        problems.append(f'disassembly too small to be the kernel ({count} instructions)')
    # -dU prints a #define only for macros that were expanded or tested, so a use
    # through another macro (TICK_MS -> PIT_HZ) is reported like a direct one.
    for source in args.sources:
        extra = []
        for original, replacement in args.mutant or []:
            if source == original:
                extra = ['-I'+os.path.dirname(source)]
                source = replacement
                break
        used = run([*args.cc, *extra, '-E', '-dU', '-MF', '/dev/null', source, '-o', '-'])
        for name in POISON:
            if re.search(r'^#define '+name+r'\b', used, re.M):
                problems.append(f'{name} user in the 1 kHz closure: {source}')
    if problems:
        print('kboot audit FAILED:\n  '+'\n  '.join(problems))
        return 1
    print(f'kboot audit OK: {args.elf} ({count} instructions, {len(args.sources)} sources)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
