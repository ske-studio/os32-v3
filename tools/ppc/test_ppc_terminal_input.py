#!/usr/bin/env python3
"""PowerMac HW_OP §5 5a and R3-P2: stop delivery, terminal frames and loss recovery."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include "lan_boot.h"
#include "sys/kern/cli/resident_cli.h"
u64 pm_terminal_tb(void) { return 1; }
void pm_usb_poll(void) {}
int idle_work_register(void (*f)(void)) { (void)f; return 0; }
void pm_lan_poll(u64 now) { (void)now; }
static void msg(const char *s, int n) { __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(s), "d"(n)); }
static void end(int code) { if (code == 11) msg("discard-fail\n", 13); __asm__ volatile("int $0x80" : : "a"(1), "b"(code)); __builtin_unreachable(); }
static int active, stops;
int resident_cui_hw_active(void) { return active; }
void ring3_abort_request(void) { ++stops; }
void _start(void) {
    u8 stop = 3, full[1024] = {0};
    active = 1;
    pm_terminal_open();
    if (stops || pm_terminal_trygetkey() != RESIDENT_CLI_KEY_CANCEL) end(20);
    pm_terminal_feed(full, sizeof(full));
    pm_terminal_feed(&stop, 1);
    if (stops != 1 || pm_terminal_trygetkey() != 0) end(21);
    pm_terminal_line_lost();
    pm_terminal_feed(&stop, 1);
    if (stops != 2) end(22);
    active = 0;
    pm_terminal_open();
    pm_terminal_feed(&stop, 1);
    if (pm_terminal_trygetkey() != RESIDENT_CLI_KEY_CANCEL ||
        pm_terminal_trygetkey() != 3 || stops != 2) end(23);
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
    source = (ROOT / 'sys/platform/powermac/terminal_fifo.inc').read_text()
    old = "        if (discard) {\n            if (c == '\\n') discard = 0;\n            continue;\n        }"
    if source.count(old) != 1:
        raise RuntimeError('mutation anchor is not unique')
    cases = [('normal', source, 0, 'full-ok\n')]
    if args.mutate:
        anchor = 'if (c == 3 && resident_cui_hw_active())'
        assert source.count(anchor) == 1
        cases.append(('lost-stop', source.replace(anchor, 'if (0)'), 21, ''))
        cases.append(('discard', source.replace(old, ''), 11, 'input-ok\ndiscard-fail\n'))
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        d = Path(tmp)
        (d / 'harness.c').write_text(HARNESS)
        for name, code, expected, marker in cases:
            (d / 'input.c').write_text(code)
            subprocess.run(['gcc-13', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-nostdinc', '-DPM_TERMINAL_TEST', '-DPM_LOAD_KDEBUG=1', '-DKBOOT_SH=1', '-I.', '-Iinclude', '-Iplatform/powermac', str(d / 'harness.c'), str(d / 'input.c'), 'sys/platform/powermac/lan_terminal.c', 'sys/kern/kstring_c.c', '-o', str(d / 'test')], cwd=ROOT, check=True, timeout=60)
            result = subprocess.run(['qemu-i386', str(d / 'test')], capture_output=True, text=True, timeout=60)
            if result.returncode != expected or marker not in result.stderr:
                raise RuntimeError(f'{name}: unexpected exit {result.returncode}: {result.stderr}')
            print(('OK ' if name == 'normal' else 'RED ') + name, flush=True)


if __name__ == '__main__':
    main()
