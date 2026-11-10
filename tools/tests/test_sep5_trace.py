#!/usr/bin/env python3
"""Real GNU89 SQLite TRACE, x86 object parity and provider runtime mutations."""
import sqlite_cc
import argparse
import os
import re
from pathlib import Path
import subprocess
import tempfile

import host32
import test_sqlite_memsys5 as memsys

ROOT = Path(__file__).resolve().parents[2]
VARS = ('_ndb_esp', '_bt_esp', '_esp', '_t_esp')
PREFIX = '#ifdef OS32_SQLITE_TRACE\n#include "diag_stack.h"\n#endif\n#line 1\n'
HARNESS = r'''
#define main memsys_main
#include "sqlite_memsys5_host.c"
#undef main
#include <stdarg.h>
static unsigned seen;
int memsys5_check_canary(void) { return 0; }
void kprintf(unsigned char attr, const char *fmt, ...)
{
    const char *p;
    va_list ap;
    unsigned bit = 0;
    (void)attr;
    if (strstr(fmt, "pre-PagerWrite esp=")) bit = 1;
    if (strstr(fmt, "pre-PagerBegin esp=")) bit = 2;
    if (strstr(fmt, "p3=%d esp=")) bit = 4;
    if (strstr(fmt, "ENTER OP_Transaction esp=")) bit = 8;
    va_start(ap, fmt);
    for (p = fmt; *p; ++p) {
        if (*p == '%') {
            unsigned value = va_arg(ap, unsigned);
            if (p >= fmt + 4 && !memcmp(p - 4, "esp=", 4)) {
#ifdef FAKE_SP
                CHECK(value == 0x13579bdfU);
#else
                CHECK(value != 0);
#endif
                seen |= bit;
            }
        }
    }
    va_end(ap);
}
int main(int argc, char **argv)
{
    int rc = memsys_main(argc, argv);
    CHECK(seen == 15);
    puts("PASS four TRACE stack sites and real SQL");
    return rc;
}
'''


def compile_sql(tmp, source, provider, trace=True, assembly=False):
    tmp.mkdir(parents=True, exist_ok=True)
    src = tmp / 'sqlite3.c'
    src.write_text(source)
    obj = tmp / ('sqlite.s' if assembly else 'sqlite.o')
    cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
    # Keep variant names relative to one temporary root so base_dir removes
    # its random component across runs, including the fake provider include.
    sqlite_cc.build([cc, '-m32', '-std=gnu89', '-Os', '-fno-pie',
        '-include', str(ROOT / 'lib/sqlite3/os32_sqlite_config.h'),
        '-I' + str(ROOT / 'lib/sqlite3'), '-I' + str(ROOT / 'include'),
        '-I' + str(provider), *(['-DOS32_SQLITE_TRACE'] if trace else []),
        '-S' if assembly else '-c', str(src), '-o', str(obj)], cwd=tmp.parent, check=True)
    return obj


def exercise(tmp, source, provider, fake=False):
    obj = compile_sql(tmp, source, provider)
    harness = tmp / 'harness.c'
    harness.write_text(HARNESS)
    cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
    exe = tmp / 'trace'
    host32.build([cc, '-m32', '-std=gnu11', '-Os', '-fno-pie', '-no-pie',
        '-static', '-nostartfiles', '-Wl,-e,_start',
        *(['-DFAKE_SP'] if fake else []), '-I' + str(ROOT / 'tools/tests'),
        '-I' + str(ROOT / 'lib/sqlite3'), str(harness), str(memsys.PROTO),
        str(ROOT / 'tools/tests/sqlite_memsys5_start.S'),
        str(ROOT / 'tools/tests/sqlite_memsys5_linux.c'), str(obj), '-o', str(exe)], check=True)
    return host32.run([str(exe), 'sql', '1'], capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'lib/sqlite3/sqlite3.c').read_text()
    baseline = subprocess.check_output(['git', 'show', '3ebbd208:lib/sqlite3/sqlite3.c'], cwd=ROOT).decode()
    with tempfile.TemporaryDirectory(prefix='sep5-trace-') as directory:
        tmp = Path(directory)
        x86 = ROOT / 'arch/x86'
        # Off is byte identical. On preserves inline instructions and log strings.
        for trace in (False, True):
            a = compile_sql(tmp / ('old' + str(trace)), baseline, x86, trace)
            b = compile_sql(tmp / ('new' + str(trace)), source, x86, trace)
            if trace:
                old_asm = compile_sql(tmp / 'old-asm', baseline, x86, True, True).read_text()
                new_asm = compile_sql(tmp / 'new-asm', source, x86, True, True).read_text()
                def inline_sp(asm):
                    blocks = re.findall(r'[/#]APP(.*?)[/#]NO_APP', asm, re.S)
                    return [re.sub(r'%e[a-z]+', '%reg', line.strip())
                            for block in blocks for line in block.splitlines()
                            if line.strip().startswith('mov %esp,')]
                assert inline_sp(old_asm) == inline_sp(new_asm) == ['mov %reg, %reg'] * 4
                def messages(obj):
                    return sorted(line for line in subprocess.check_output(
                        ['strings', str(obj)], text=True).splitlines()
                        if line.startswith(('[NDB]', '[BT]', '[VDBE]')))
                assert messages(a) == messages(b) and messages(a)
                nm = subprocess.check_output([str(Path(os.environ['CROSS_DIR']) /
                    'bin/i386-elf-nm'), str(b)], text=True)
                assert 'arch_diag_sp' not in nm
                print('GREEN TRACE on: four original inline instructions and identical log strings', flush=True)
            else:
                assert a.read_bytes() == b.read_bytes(), 'TRACE off object mismatch'
                print('GREEN TRACE off: full object parity', flush=True)
        result = exercise(tmp / 'real', source, x86)
        assert result.returncode == 0, result.stdout + result.stderr
        print(result.stdout, end='', flush=True)
        fake = tmp / 'fake'
        fake.mkdir()
        (fake / 'arch_diag_stack.h').write_text('static __inline__ __attribute__((always_inline)) unsigned long arch_diag_sp(void) { return 0x13579bdfUL; }\n')
        result = exercise(tmp / 'sentinel', source, fake, True)
        assert result.returncode == 0, result.stdout
        # PPC provider compile-only on host ISA: no PPC toolchain needed in CI.
        ppc = compile_sql(tmp / 'ppc-provider', source, ROOT / 'arch/ppc')
        nm = subprocess.check_output([str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-nm'), str(ppc)], text=True)
        assert 'arch_diag_sp' not in nm
        assert '%%esp' not in source
        print('GREEN PPC provider TRACE compile-only; zero/unavailable SP, no ESP asm')
        if args.mutate:
            for var in VARS:
                anchor = ' ' + var + ' = arch_diag_sp();'
                assert source.count(anchor) == 1
                result = exercise(tmp / var, source.replace(anchor, ' ' + var + ' = 0;'), fake, True)
                assert result.returncode == 1 and 'value == 0x13579bdfU' in result.stdout, result
                print('RED runtime sentinel: ' + var, flush=True)
            # An off-build provider reference changes the measured object.
            anchor = 'SQLITE_API int sqlite3_initialize(void){'
            assert source.count(anchor) == 1
            polluted = source.replace(PREFIX, '#include "diag_stack.h"\n#line 1\n', 1).replace(
                anchor, anchor + ' (void)arch_diag_sp();', 1)
            bad = compile_sql(tmp / 'off-leak', polluted, x86, False)
            good = compile_sql(tmp / 'off-control', source, x86, False)
            assert bad.read_bytes() != good.read_bytes()
            print('RED off object parity: leaked provider instruction')
    print('PASS sep5 trace')


if __name__ == '__main__':
    main()
