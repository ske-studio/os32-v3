#!/usr/bin/env python3
"""Link-time audits of the PowerPC core-ready kernel (PPC_KBOOT_DESIGN H3, R7).

  1. no undefined symbol;
  2. integer-only outside the dedicated FP save/restore leaves: check decoded
     instructions in the ELF and objects against FP/AltiVec families;
  3. no translation unit *uses* PIT_HZ, directly or through another macro
     (e.g. TICK_MS): PIT_HZ is a PC-98 name; common code uses OS32_TICK_HZ.

Called by build/targets/ppc-powermac.mk. Any tool failure is a failed audit.
"""
from bisect import bisect_right
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


def decoded(objdump, elf):
    function, start = '', 0
    for line in run([objdump, '-d', '--no-show-raw-insn', elf]).splitlines():
        label = re.match(r'^([0-9a-f]+) <([^>]+)>:', line)
        if label:
            function, start = label[2], int(label[1], 16)
        match = re.match(r'^\s*([0-9a-f]+):\s+([a-z][a-z0-9.+-]*)', line)
        if match:
            yield function, int(match[1], 16) - start, match[2], line.strip()


def instructions(objdump, elf):
    # Preserve the existing public iterator used by acceptance scripts.
    for _, _, mnemonic, line in decoded(objdump, elf):
        yield mnemonic, line


def fp_permissions(objdump, obj):
    # Only the dedicated file's two leaf bodies may contain these G3 opcodes.
    allowed = set()
    for function, offset, mnemonic, _ in decoded(objdump, obj):
        if FLOAT.match(mnemonic):
            if function not in ('arch_fp_save', 'arch_fp_restore') or mnemonic not in (
                    'stfd', 'lfd', 'mffs', 'mtfsf'):
                raise SystemExit('invalid FP context object: ' + function + ' ' + mnemonic)
            allowed.add((function, offset, mnemonic))
    return allowed


def text_ranges(map_path, obj):
    with open(map_path) as stream:
        linked = stream.read().split('Linker script and memory map', 1)[1]
    pattern = (r'^ [.]text(?:[.]\S+)?\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+) '
               + re.escape(obj) + r'$')
    ranges = sorted((int(addr, 16), int(size, 16)) for addr, size in
                    re.findall(pattern, linked, re.M) if int(size, 16))
    if not ranges:
        raise SystemExit('no linked SQLite text in map: ' + obj)
    return ranges


def forbidden(objdump, elf, fp_object=None, sqlite_raw=False, ranges=()):
    allowed = fp_permissions(objdump, fp_object) if fp_object else set()
    starts = [start for start, _ in ranges]
    count, problems = 0, []
    for function, offset, mnemonic, line in decoded(objdump, elf):
        count += 1
        if mnemonic in ALLOWED:
            continue
        if FLOAT.match(mnemonic):
            addr = int(line.split(':', 1)[0], 16)
            index = bisect_right(starts, addr) - 1
            if (sqlite_raw or (function, offset, mnemonic) in allowed or
                    index >= 0 and addr < starts[index] + ranges[index][1]):
                continue
        if ALTIVEC.match(mnemonic) or FLOAT.match(mnemonic):
            problems.append('floating-point/AltiVec instruction: ' + line)
    return count, problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--elf', required=True)
    parser.add_argument('--nm', required=True)
    parser.add_argument('--objdump', required=True)
    parser.add_argument('--objects', nargs='*', default=[], help='audit unused object code too')
    parser.add_argument('--map', help='link map must not contain libc/libgcc')
    parser.add_argument('--sqlite-group')
    parser.add_argument('--sqlite-inputs', nargs='+', default=[])
    parser.add_argument('--fp-object', help='dedicated arch/ppc/fp_context.o only')
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
    if args.sqlite_group:
        exports = {line.split()[-1] for line in
                   run([args.nm, '-g', '--defined-only', args.sqlite_group]).splitlines()}
        if exports != {'sqlite_module_init'}:
            problems.append('unexpected SQLite exports: ' + str(sorted(exports)))
        undefined = set(run([args.nm, '-u', args.sqlite_group]).split()) - {'U'}
        relocations = run([args.objdump, '-r', args.sqlite_group])
        live = undefined.intersection(re.findall(r'^\w+\s+R_PPC_\w+\s+(\w+)', relocations, re.M))
        imports = set('''__ashldi3 __divdi3 __lshrdi3 __moddi3 __udivdi3 __umoddi3
            free kmemcpy kmemset kprintf kstrncmp malloc memcmp memcpy memmove memset realloc
            softfloat_detectTininess softfloat_exceptionFlags softfloat_raiseFlags softfloat_roundingMode
            strchr strcmp strcspn strlen strncmp strspn sys_time tick_count vfs_close vfs_close_sqlite
            vfs_count_sqlite vfs_fd_is_stale vfs_get_size vfs_open_sqlite vfs_quarantine_sqlite
            vfs_read_fd vfs_resize_fd vfs_resolve_path vfs_rm vfs_seek vfs_sqlite_backend
            vfs_sqlite_claim_main vfs_sqlite_fd_identity vfs_sqlite_writable vfs_stat vfs_sync
            vfs_sync_fd vfs_validate_sqlite vfs_write_fd'''.split())
        if live - imports:
            problems.append('unexpected SQLite imports: ' + str(sorted(live - imports)))
        print('SQLite live imports:', ' '.join(sorted(live)))
    ranges = text_ranges(args.map, args.sqlite_group) if args.sqlite_group else ()
    count, bad = forbidden(args.objdump, args.elf, args.fp_object, ranges=ranges)
    problems.extend(bad)
    for obj in args.objects + args.sqlite_inputs:
        permitted = args.fp_object if args.fp_object and os.path.realpath(obj) == os.path.realpath(args.fp_object) else None
        _, bad = forbidden(args.objdump, obj, permitted,
                           obj == args.sqlite_group or obj in args.sqlite_inputs)
        problems.extend(obj + ': ' + problem for problem in bad)
    if args.map:
        with open(args.map) as stream:
            if re.search(r'\b(?:libgcc|libc)\.a\b', stream.read()):
                problems.append('libc/libgcc in kernel link map')
    if count < 1000:
        problems.append(f'disassembly too small to be the kernel ({count} instructions)')
    # -dU prints a #define only for macros that were expanded or tested, so a use
    # through another macro (TICK_MS -> PIT_HZ) is reported like a direct one.
    for source in args.sources:
        extra = []
        if args.sqlite_group and source == "lib/sqlite3/sqlite3.c":
            extra += ["-include", "lib/sqlite3/os32_sqlite_config.h"]
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
