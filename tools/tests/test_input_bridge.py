#!/usr/bin/env python3
"""Provider declaration, ILP32 layout and absent-source contract."""
CHECK = dict(name='input-bridge-host', tier=1, budget=5, needs=[], covers=[
    'input/hal/in2_int.h', 'include/types.h', 'tools/tests/test_input_bridge.py',
    'tools/tests/host32.py'], basis=['INPUT_HAL_DESIGN §3'])

import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = r'''
#include "in2_int.h"
#ifdef PPC_COMPILE
_Static_assert(sizeof(u32) == 4 && sizeof(void *) == 4, "PPC ILP32");
_Static_assert(sizeof(struct InputLegacyProvider) == 40, "provider size");
_Static_assert(__builtin_offsetof(struct InputLegacyProvider, park_kbd) == 36, "provider offset");
#endif
static int absent(const struct InputLegacyProvider *p) {
    return p && p->try_key ? p->try_key() : -1;
}
static int probe(void) {
    struct InputLegacyProvider p = {0};
    if (sizeof(p) != 10 * sizeof(void (*)(void))) return 1;
    if (IN2_MAX_DEV != 16 || IL_CHAR != 0 || IL_KEY != 1 || IL_LOCAL_CHAR != 2) return 2;
    if (absent(0) != -1 || absent(&p) != -1) return 3;
    return 0;
}
#ifndef PPC_COMPILE
void _start(void) {
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#else
int fixture(void) { return probe(); }
#endif
'''


def run(ctx):
    header = (ctx.root / 'input/hal/in2_int.h').read_text()
    cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
    with tempfile.TemporaryDirectory(prefix='input-bridge-') as td:
        tmp = Path(td)
        (tmp / 'fixture.c').write_text(FIXTURE)
        def build(source):
            (tmp / 'in2_int.h').write_text(source)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
                '-static', '-no-pie', '-I' + str(tmp), '-I' + str(ctx.root / 'include'),
                str(tmp / 'fixture.c'), '-o', str(tmp / 'test')], check=True)
            return host32.run([str(tmp / 'test')], runner='qemu').returncode
        if build(header) != 0:
            return 1
        print('PASS x86 libc-free provider contract', flush=True)
        if cc.is_file() and os.access(cc, os.X_OK):
            subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-DPPC_COMPILE', '-I' + str(tmp),
                '-I' + str(ctx.root / 'include'), '-c', str(tmp / 'fixture.c'),
                '-o', str(tmp / 'ppc.o')], check=True)
            print('PASS PowerPC cross compile', flush=True)
        else:
            print('SKIP PowerPC cross compile: toolchain unavailable', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1':
                print('FAIL PowerPC toolchain required', flush=True)
                return 1
        if ctx.mutate:
            for old, new, expected in [
                ('    int (*terminal_enabled)(void);\n', '', 1),
                ('#define IN2_MAX_DEV 16', '#define IN2_MAX_DEV 8', 2)]:
                assert header.count(old) == 1
                rc = build(header.replace(old, new, 1))
                if rc != expected:
                    raise RuntimeError(f'mutation expected runtime {expected}, got {rc}')
                print('RED runtime: ' + old.strip(), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
