#!/usr/bin/env python3
"""Check CUI IME reservation and candidate restoration on host TVRAM."""
CHECK = dict(name='ime-display-host', tier=1,
    covers=['drivers/video/pc98/pc98_route.h', 'tools/tests/ime_display_host.c', 'tools/tests/con_sink_host.c', 'tools/tests/host32.py', 'kernel/console/con_sink.c', 'kernel/port_service.c', 'kernel/ime/ime.c', 'kernel/ime/ime_render_tvram.c', 'kernel/console/console.c', 'platform/pc98/console.c', 'build/checks.d/check-ime-display-host.mk', 'tools/check_map.d/check-ime-display-host.yaml', 'tools/tests/kernel_includes.py'],
    needs=[], budget=60.0)

from kernel_includes import include_flags
import argparse
from pathlib import Path
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
OLD = '        cursor_y--;\n'
NEW = '        cursor_y -= 0;\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--runner', choices=('native', 'qemu'))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ime-display-') as directory:
        tmp = Path(directory)
        fixture = (ROOT / 'tools/tests/con_sink_host.c').read_text()
        fixture = fixture.replace('../../kernel/console/console.c', str(tmp / 'console.c'))
        fixture = fixture.replace('../../', str(ROOT) + '/')
        (tmp / 'con_sink_host.c').write_text(fixture)
        source = (ROOT / 'tools/tests/ime_display_host.c').read_text()
        source = source.replace('../../', str(ROOT) + '/')
        (tmp / 'ime_display_host.c').write_text(source)
        original = (ROOT / 'kernel/console/console.c').read_text()
        assert original.count(OLD) == 1, 'mutation target must match once'
        for modified in (False, True) if args.mutate else (False,):
            (tmp / 'console.c').write_text(original.replace(OLD, NEW) if modified else original)
            exe = tmp / ('mutant' if modified else 'normal')
            host32.build(['gcc', '-std=gnu11', '-m32', '-march=i386', '-ffreestanding',
                          '-fno-pie', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror',
                          '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare',
                          '-Wno-type-limits', '-Wno-missing-field-initializers',
                          '-DCON_SINK_NO_IRQ_LOCK', '-D__KERNEL_BUILD__', '-D__cdecl=',
                          '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                          *include_flags(ROOT),
                          '-nostdlib', '-static', '-no-pie', str(tmp / 'ime_display_host.c'),
                          '-o', str(exe)], check=True)
            result = host32.run([str(exe)], runner=args.runner, capture_output=True, text=True, timeout=30)
            print(result.stdout + result.stderr, end='', flush=True)
            if modified:
                assert result.returncode == 1 and 'IME reservation preserves cursor' in result.stdout
                print('RED runtime cursor reservation (compiled with -Werror)', flush=True)
            else:
                assert result.returncode == 0, result.returncode


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_ime_display.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
