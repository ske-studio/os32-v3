#!/usr/bin/env python3
"""Compile the standalone link platform contract for host and PowerPC."""
CHECK = dict(name='link-platform-host', tier=2, budget=10, needs=[], covers=[
    'net/link_platform.h', 'include/types.h', 'include/callconv.h',
    'tools/tests/test_link_platform.py'])
import os
from pathlib import Path
import subprocess
import tempfile


def run(ctx):
    header = (ctx.root / 'net/link_platform.h').read_text()
    # CI has no PPC toolchain (same rule as ppc-sc): host cc is required, PPC runs where present.
    ppc = str(Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
              / 'bin/powerpc-elf-gcc')
    compilers = ['cc'] + ([ppc] if Path(ppc).is_file() else [])
    if len(compilers) == 1:
        print('SKIP PowerPC half: toolchain unavailable: ' + ppc, flush=True)
    source = '#include "link_platform.h"\n'
    for name, signature in [('now', 'u32 (*)(void)'), ('lock', 'u32 (*)(void)'),
                            ('unlock', 'void (*)(u32)'), ('idle', 'void (*)(void)')]:
        source += ('_Static_assert(__builtin_types_compatible_p('
                   '__typeof__(&link_platform_' + name + '), ' + signature +
                   '), "' + name + '");\n')
    with tempfile.TemporaryDirectory(prefix='link-platform-') as td:
        work = Path(td)
        (work / 'probe.c').write_text(source)
        def compile_header(cc, text):
            (work / 'link_platform.h').write_text(text)
            try:
                return subprocess.run([cc, '-Wall', '-Wextra', '-Werror',
                    '-std=gnu11', '-I' + str(ctx.root / 'include'), '-c',
                    str(work / 'probe.c'), '-o', str(work / 'probe.o')],
                    capture_output=True, text=True)
            except FileNotFoundError:
                print('FAIL compiler missing: ' + cc, flush=True)
                return None
        failed = False
        for cc in compilers:
            result = compile_header(cc, header)
            if result is None or result.returncode:
                if result is not None:
                    print(result.stderr, flush=True)
                failed = True
                continue
            print('GREEN standalone declarations: ' + cc, flush=True)
            if ctx.mutate:
                anchor = 'u32 link_platform_now(void);'
                assert header.count(anchor) == 1
                result = compile_header(cc, header.replace(anchor,
                                            'void link_platform_now(void);'))
                assert result is not None and result.returncode != 0
                assert 'static assertion failed' in result.stderr
                print('RED compile: now return type: ' + cc, flush=True)
        return int(failed)
