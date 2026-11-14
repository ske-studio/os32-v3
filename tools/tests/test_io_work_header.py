#!/usr/bin/env python3
"""A1 declaration/layout evidence only; no dispatcher implementation or stub."""
CHECK = dict(name='io-work-header-host', tier=1, budget=10, needs=[], covers=[
    'tools/tests/test_io_work_header.py', 'tools/tests/host32.py',
    'kernel/io_work.h', 'include/types.h', 'include/callconv.h'])
from pathlib import Path
import subprocess
import sys
import tempfile
import host32

SOURCE = r'''
#include "io_work.h"
#include "io_work.h"
#define TYPE(expr, type) STATIC_ASSERT(\
    __builtin_types_compatible_p(__typeof__(expr), type), signature)
TYPE(&io_register, int (*)(const struct io_desc *));
TYPE(&io_unregister, int (*)(u32));
TYPE(&io_request, void (*)(u32));
TYPE(&io_clock_us, int (*)(u32 *, u32 *));
TYPE(&io_slice_begin, int (*)(struct io_slice *, u32, u32));
TYPE(&io_slice_take, int (*)(struct io_slice *));
TYPE(&io_dispatch, int (*)(u32));
TYPE(((struct io_desc *)0)->step, int (*)(void *, struct io_slice *));
void _start(void)
{
    int rc = sizeof(struct io_slice) != 24 || sizeof(struct io_desc) != 32 ||
        __builtin_offsetof(struct io_desc, step) != 24 ||
        __builtin_offsetof(struct io_desc, state) != 28 ||
        IO_DONE != 0 || IO_MORE != 1 || IO_WAIT != 2;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
'''


def exercise(root, work, header):
    (work / 'io_work.h').write_text(header)
    (work / 'host.c').write_text(SOURCE)
    exe = work / 'host'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra',
        '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
        '-nostdlib', '-static', '-no-pie', '-I' + str(root / 'include'),
        str(work / 'host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)], runner='qemu').returncode


def run(ctx):
    header = (ctx.root / 'kernel/io_work.h').read_text()
    with tempfile.TemporaryDirectory(prefix='io-work-header-') as td:
        work = Path(td)
        assert exercise(ctx.root, work, header) == 0
        print('PASS io_work declarations/layout (ILP32, no libc)', flush=True)
        if ctx.mutate:
            for old, new in [('reserved[3]', 'reserved[7]'),
                             ('clock_source, clock_generation', 'clock_source'),
                             ('IO_WAIT = 2', 'IO_WAIT = 3')]:
                assert header.count(old) == 1, old
                assert exercise(ctx.root, work, header.replace(old, new, 1)) == 1
                print('RED runtime: ' + old, flush=True)
    return 0


if __name__ == '__main__':
    from types import SimpleNamespace
    sys.exit(run(SimpleNamespace(root=Path(__file__).resolve().parents[2],
                                mutate='--mutate' in sys.argv)))
