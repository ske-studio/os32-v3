#!/usr/bin/env python3
"""Exercise terminal frames and loss recovery with the production input ring."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ppc-terminal-input', tier=1, budget=60, needs=[],
             covers=['platform/powermac/terminal_input.c', 'platform/powermac/console_stream.c'],
             inputs=['tools/ppc/test_ppc_terminal_input.py', 'platform/powermac/lan_terminal.c',
                     'platform/powermac/lan_boot.h', 'kernel/cli/resident_cli.h',
                     'include/types.h', 'lib/kstring_c.c', 'lib/kstring.h'])
HARNESS = r'''
#include "lan_boot.h"
#include "kernel/cli/resident_cli.h"
u64 pm_terminal_tb(void) { return 1; }
void pm_lan_poll(u64 now) { (void)now; }
static void msg(const char *s, int n) { __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(s), "d"(n)); }
static void end(int code) { if (code == 11) msg("discard-fail\n", 13); __asm__ volatile("int $0x80" : : "a"(1), "b"(code)); __builtin_unreachable(); }
void _start(void) {
    struct lan_message m = {0}; u8 mac[6] = {1};
    m.type = PLN1_TERM_OPEN; pm_term_receive(&m, mac);
    if (pm_terminal_trygetkey() != RESIDENT_CLI_KEY_CANCEL) end(1);
    m.type = PLN1_TERM_IN; m.payload_bytes = 2; m.payload[0] = 'a'; m.payload[1] = 'b';
    pm_term_receive(&m, mac);
    if (pm_terminal_trygetkey() != 'a' || pm_terminal_trygetkey() != 'b' || pm_terminal_trygetkey() >= 0) end(2);
    msg("input-ok\n", 9);
    m.seq = 2; m.payload_bytes = 4; m.payload[0] = 'x'; m.payload[1] = '\n'; m.payload[2] = 'z'; m.payload[3] = '\n';
    pm_term_receive(&m, mac);
    if (pm_terminal_trygetkey() != RESIDENT_CLI_KEY_CANCEL || pm_terminal_trygetkey() != 'z' || pm_terminal_trygetkey() != '\n' || pm_terminal_trygetkey() >= 0) end(11);
    msg("gap-ok\n", 7);
    for (int i = 0; i < 5; ++i) { m.seq++; m.payload_bytes = 256; for (int j = 0; j < 256; ++j) m.payload[j] = 'q'; pm_term_receive(&m, mac); }
    if (pm_terminal_trygetkey() != RESIDENT_CLI_KEY_CANCEL || pm_terminal_trygetkey() >= 0) end(3);
    m.seq++; m.payload_bytes = 3; m.payload[0] = '\n'; m.payload[1] = 'r'; m.payload[2] = '\n'; pm_term_receive(&m, mac);
    if (pm_terminal_trygetkey() != 'r' || pm_terminal_trygetkey() != '\n' || pm_terminal_trygetkey() >= 0) end(4);
    msg("full-ok\n", 8); end(0);
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'platform/powermac/terminal_input.c').read_text()
    old = "        if (discard) {\n            if (c == '\\n') discard = 0;\n            continue;\n        }"
    if source.count(old) != 1:
        raise RuntimeError('mutation anchor is not unique')
    cases = [('normal', source, 0, 'full-ok\n')]
    if args.mutate:
        cases.append(('discard', source.replace(old, ''), 11, 'input-ok\ndiscard-fail\n'))
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        d = Path(tmp)
        (d / 'harness.c').write_text(HARNESS)
        for name, code, expected, marker in cases:
            (d / 'input.c').write_text(code)
            subprocess.run(['gcc-13', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-DPM_TERMINAL_TEST', '-I.', '-Iinclude', '-Iplatform/powermac', str(d / 'harness.c'), str(d / 'input.c'), 'platform/powermac/lan_terminal.c', 'lib/kstring_c.c', '-o', str(d / 'test')], cwd=ROOT, check=True, timeout=60)
            result = subprocess.run(['qemu-i386', str(d / 'test')], capture_output=True, text=True, timeout=60)
            if result.returncode != expected or marker not in result.stderr:
                raise RuntimeError(f'{name}: unexpected exit {result.returncode}: {result.stderr}')
            print(('OK ' if name == 'normal' else 'RED ') + name, flush=True)


if __name__ == '__main__':
    main()
