#!/usr/bin/env python3
"""INPUT_KEYMAP_DESIGN §7 H-usb・H-hid: declaration type contracts only."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='input-keymap-decl', tier=1, needs=[], budget=10,
    covers=['tools/tests/test_input_keymap_decl.py', 'sys/include/usb_platform.h',
            'sys/dev/usb/hid_boot.h', 'sys/dev/usb/usb_core.h',
            'sys/dev/usb/usb_desc.h', 'sys/include/types.h', 'sys/include/callconv.h',
            'sys/include/endian_le.h'],
    basis=['INPUT_KEYMAP_DESIGN §7 H-usb・H-hid'])

SOURCE = '''/* INPUT_KEYMAP_DESIGN §7 H-usb・H-hid */
#include "sys/dev/usb/hid_boot.h"
#define TYPE(fn, type) _Static_assert(\\
    __builtin_types_compatible_p(__typeof__(&(fn)), type), #fn)
TYPE(usbp_input_usage, void (*)(u8, u8, int));
TYPE(usbp_input_usage_repeat, void (*)(u8, u8));
TYPE(hid_kbd_sc, u8 (*)(u8));
TYPE(hid_kbd_norepeat, u8 (*)(u8));
TYPE(hid_kbd_order, u8 (*)(u8));
'''


def check(mutate):
    with tempfile.TemporaryDirectory(prefix='keymap-decl-') as directory:
        tmp = Path(directory)
        probe = tmp / 'probe.c'
        probe.write_text(SOURCE)
        command = ['gcc', '-m32', '-std=gnu11', '-ffreestanding', '-fno-builtin',
                   '-nostdinc', '-nostdlib', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                   '-I'+str(tmp), '-Iinclude', '-I.', '-fsyntax-only', str(probe)]
        subprocess.run(command, cwd=ROOT, check=True)
        print('GREEN declaration types (no libc, no symbol references)')
        if mutate:
            header = (ROOT / 'sys/include/usb_platform.h').read_text()
            anchor = 'void usbp_input_usage(u8 src, u8 usage, int down);'
            assert header.count(anchor) == 1, anchor
            (tmp / 'usb_platform.h').write_text(header.replace(
                anchor, 'void usbp_input_usage(u8 src, u16 usage, int down);'))
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            assert result.returncode != 0 and 'static assertion failed' in result.stderr
            assert 'usbp_input_usage' in result.stderr
            print('RED compile: usbp_input_usage usage u8 -> u16')


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])],
                           cwd=ctx.root, env=os.environ.copy())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    check(parser.parse_args().mutate)
