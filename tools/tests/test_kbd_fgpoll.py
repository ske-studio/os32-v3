"""F2/F2i: real keyboard CUI foreground calls, ILP32 runtime mutants."""
CHECK = dict(name='kbd-fgpoll-host', tier=1, needs=[], budget=10.0,
             covers=['tools/tests/test_kbd_fgpoll.py', 'tools/tests/kbd_fgpoll_host.c',
                     'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
                     'tools/tests/host_arch/**', 'build/config.mk',
                     'drivers/input/kbd.c', 'drivers/input/kbd_fgpoll.inc',
                     'drivers/input/*.h', 'drivers/char/serial.h',
                     'drivers/video/geforce/geforce_module.h',
                     'kernel/module/*.h', 'kernel/v86/v86_bios.h',
                     'include/**'])

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
KBD = 'drivers/input/kbd.c'
ADAPTER = 'drivers/input/kbd_fgpoll.inc'
# Unique anchors; each mutant must compile, fail a runtime assertion and be
# followed by an unmodified GREEN control. One mutant for each connection.
MUTATIONS = [
    ('adapter-dispatch', ADAPTER, 'if (!kbd_gui_mode)', 'if (0 && !kbd_gui_mode)'),
    ('trychar-connection', KBD,
     'int kbd_trygetchar(void)\n{\n    u16 entry;\n\n    kbd_foreground_poll();',
     'int kbd_trygetchar(void)\n{\n    u16 entry;'),
    ('trykey-connection', KBD,
     'int kbd_trygetkey(void)\n{\n    u16 entry;\n\n    kbd_foreground_poll();',
     'int kbd_trygetkey(void)\n{\n    u16 entry;'),
    ('char-wait-connection', KBD,
     '            kbd_foreground_poll();\n            /* キーボードバッファ */',
     '            /* キーボードバッファ */'),
    ('key-wait-connection', KBD,
     '    while (kbd_count == 0) {\n        kbd_foreground_poll();',
     '    while (kbd_count == 0) {'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    originals = {name: (ROOT / name).read_text() for name in (KBD, ADAPTER)}
    with tempfile.TemporaryDirectory(prefix='kbd-fgpoll-') as directory:
        tmp = Path(directory)
        exe = tmp / 'host'
        command = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                   '-Wall', '-Wextra', '-Werror', '-Werror=vla', '-ffreestanding',
                   '-fno-pie', '-fno-stack-protector', '-D__KERNEL_BUILD__',
                   '-ffunction-sections', '-fdata-sections', '-nostdlib',
                   '-static', '-no-pie', '-Wl,--gc-sections', '-I' + str(tmp),
                   '-I' + str(ROOT / 'tools/tests/host_arch'), *include_flags(ROOT),
                   str(ROOT / 'tools/tests/kbd_fgpoll_host.c'), '-o', str(exe)]

        def build(inputs):
            for name, output in ((KBD, 'kbd_source.c'), (ADAPTER, 'kbd_fgpoll.inc')):
                def relocate(match):
                    if match[1] == 'kbd_fgpoll.inc':
                        return '#include "' + str(tmp / 'kbd_fgpoll.inc') + '"'
                    path = ROOT / Path(name).parent / match[1]
                    return '#include "' + str(path.resolve()) + '"' if path.is_file() else match[0]
                (tmp / output).write_text(re.sub(r'^#include "([^"]+)"', relocate,
                                               inputs[name], flags=re.M))
            subprocess.run(command, cwd=ROOT, check=True)

        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            def control():
                build(originals)
                host32.run([str(exe)], runner=runner, check=True, timeout=10)
            control()
            if args.mutate:
                for label, product, old, new in MUTATIONS:
                    assert originals[product].count(old) == 1, label
                    inputs = dict(originals)
                    inputs[product] = inputs[product].replace(old, new, 1)
                    build(inputs)
                    result = host32.run([str(exe)], runner=runner, capture_output=True,
                                        text=True, timeout=10)
                    host32.report_signal(result.returncode, runner)
                    assert result.returncode == 1 and 'FAIL ' in result.stdout, (
                        label, result.returncode, result.stdout, result.stderr)
                    print(f'MUTATE runtime RED {label}: {result.stdout.strip()}', flush=True)
                    control()
                    print(f'RESTORED GREEN {label}', flush=True)


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    command = [sys.executable, '-B', 'tools/tests/test_kbd_fgpoll.py']
    if ctx.mutate:
        command.append('--mutate')
    return subprocess.call(command, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
