#!/usr/bin/env python3
"""Exercise the LAN terminal with its wire codec in a freestanding host."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ppc-terminal', tier=1, budget=60, needs=[],
             covers=['platform/powermac/lan_terminal.c'],
             inputs=['tools/ppc/test_ppc_terminal.py', 'platform/powermac/lan_wire.c',
                     'platform/powermac/lan_boot.h', 'include/types.h',
                     'lib/kstring_c.c', 'lib/kstring.h'])
HARNESS = r'''
#include "lan_boot.h"
static int lost, fed;
void pm_terminal_feed(const u8 *p, u32 n) { (void)p; fed += n; }
void pm_terminal_line_lost(void) { ++lost; }
void pm_terminal_open(void) { ++lost; }
static void msg(const char *s, int n) { __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(s), "d"(n)); }
static void fail(int code) { if (code == 11) msg("gap\n", 4); else if (code == 12) msg("drop\n", 5); __asm__ volatile("int $0x80" : : "a"(1), "b"(code)); __builtin_unreachable(); }
void _start(void) {
    struct lan_message m = {0}, out, decoded; u8 mac[6] = {1}, buf[336], data[256] = {3}; u32 used; int i;
    if (pm_term_state(&out) || pm_term_tx_peek(&out)) fail(1);
    m.type = PLN1_TERM_OPEN; m.host_nonce[0] = 7; pm_term_receive(&m, mac);
    if (lost != 1 || pm_term_tx_peek(&out) || !pm_term_state(&out) || out.type != PLN1_TERM_STATE) fail(1);
    m.type = PLN1_TERM_IN; m.payload_bytes = 1;
    pm_term_receive(&m, mac); pm_term_receive(&m, mac); m.seq = 2; pm_term_receive(&m, mac);
    if (lost != 2 || fed != 2) fail(11);
    msg("gap-ok\n", 7);
    m.seq = 1; pm_term_receive(&m, mac); if (fed != 2) fail(2);
    for (i = 0; i < 17; ++i) pm_term_write(data, sizeof(data));
    if (!pm_term_tx_peek(&out) || out.type != PLN1_TERM_OUT || out.seq || out.drop_records != 1 || out.drop_bytes != 256) fail(3);
    if (pm_lan_encode(buf, sizeof(buf), &out, &used) || pm_lan_decode(buf, used, &decoded) || decoded.type != PLN1_TERM_OUT || decoded.payload[0] != 3) fail(4);
    for (i = 0; i < 16; ++i) { if (!pm_term_tx_peek(&out) || out.seq != (u64)i) fail(5); pm_term_tx_complete(); }
    if (pm_term_tx_peek(&out) || !pm_term_state(&out) || out.type != PLN1_TERM_STATE || out.seq != 17 || out.payload[7] != 3 || out.payload[15] != 1 || out.payload[16] || out.payload[17] || out.payload[18] || out.payload[19] || out.payload[20] || out.payload[21] || out.payload[22] || out.payload[23]) fail(12);
    m.type = PLN1_TERM_OPEN; m.payload_bytes = 0; m.seq = 0; pm_term_receive(&m, mac);
    for (i = 0; i < 20; ++i) pm_term_write(data, 1);
    if (!pm_term_tx_peek(&out) || out.payload_bytes != 20 || out.drop_records || out.drop_bytes) fail(6);
    for (i = 0; i < 20; ++i) if (out.payload[i] != 3) fail(6);
    pm_term_tx_complete();
    if (pm_term_tx_peek(&out)) fail(6);
    msg("drop-ok\n", 8); fail(0);
}
'''
MUTATIONS = [('gap', '        pm_terminal_line_lost();\n    }\n    in_next', '        /* lost event removed */\n    }\n    in_next', 11, 'gap\n', ''),
             ('drop', '            out_next++;\n            out_drop++;', '            /* sequence removed */\n            out_drop++;', 12, 'drop\n', 'gap-ok\n')]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    original = (ROOT / 'platform/powermac/lan_terminal.c').read_text()
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        d = Path(tmp)
        (d / 'harness.c').write_text(HARNESS)
        for name, source, expected, diagnostic, marker in [('normal', original, 0, '', 'drop-ok\n')] + (
                [(n, original.replace(old, new), rc, diag, mark) for n, old, new, rc, diag, mark in MUTATIONS] if args.mutate else []):
            if name != 'normal':
                old = next(x[1] for x in MUTATIONS if x[0] == name)
                if original.count(old) != 1:
                    raise RuntimeError('mutation anchor is not unique: ' + name)
            (d / 'terminal.c').write_text(source)
            cmd = ['gcc-13', '-m32', '-std=gnu11', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-fno-builtin', '-Wall', '-Wextra', '-Werror', '-I.', '-Iinclude', '-Iplatform/powermac', str(d / 'harness.c'), str(d / 'terminal.c'), 'platform/powermac/lan_wire.c', 'lib/kstring_c.c', '-o', str(d / 'test')]
            subprocess.run(cmd, cwd=ROOT, check=True, timeout=60)
            result = subprocess.run(['qemu-i386', str(d / 'test')], capture_output=True, text=True, timeout=60)
            if result.returncode != expected or diagnostic not in result.stderr or marker not in result.stderr:
                raise RuntimeError(f'{name}: unexpected exit {result.returncode}: {result.stderr}')
            print(('OK ' if name == 'normal' else 'RED ') + name, flush=True)


if __name__ == '__main__':
    main()
