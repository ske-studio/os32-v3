#!/usr/bin/env python3
"""USB 1D boot HID: A5/A6, repeat, signed mouse and composite F11."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ['drivers/usb/hid_kbd.c', 'drivers/usb/hid_kbd_map.c', 'drivers/usb/hid_mouse.c']
CHECK = dict(name='usb-hid', tier=1, needs=[], budget=10, covers=[
    'drivers/usb/hid_kbd.c', 'drivers/usb/hid_kbd_map.c', 'drivers/usb/hid_mouse.c',
    'drivers/usb/hid_boot.h', 'drivers/usb/usb_core.h',
    'drivers/usb/usb_desc.h', 'include/usb_platform.h', 'include/types.h',
    'include/callconv.h', 'include/endian_le.h', 'tools/tests/usb_hid_host.c',
    'tools/tests/test_usb_hid.py', 'tools/tests/host32.py', 'tools/tests/cc_cache.py'])


def check(mutate):
    flags = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wvla', '-Iinclude', '-I.']
    with tempfile.TemporaryDirectory(prefix='usb-hid-') as directory:
        tmp = Path(directory)
        for prefix in ('/home/hight/opt/cross/bin/i386-elf-',
                       '/home/hight/opt/cross-ppc/bin/powerpc-elf-'):
            for src in SOURCES:
                subprocess.run([prefix+'gcc', *flags, '-ffreestanding', '-c', src,
                                '-o', str(tmp/'unit.o')], cwd=ROOT, check=True)
        for swap in ([], ['-DUSB_TEST_SWAP']):
            exe = tmp/'hid'
            subprocess.run(['gcc', '-m32', *flags, *swap, 'tools/tests/usb_hid_host.c',
                            *SOURCES, '-o', str(exe)], cwd=ROOT, check=True)
            result = host32.run([str(exe)])
            assert result.returncode == 0, result.returncode
            print('HID runtime OK', swap)
        if mutate:
            mutations = [
                ('drivers/usb/hid_kbd.c', '!!s->prev[i] != !!now[i]', 's->prev[i] != now[i]'),
                ('drivers/usb/hid_boot.h', 'usbp_input_release(b->kind, b->iface->src);', '(void)b;'),
                ('drivers/usb/hid_boot.h', 'usbp_input_release(b->kind, f->src);', '(void)b;'),
                ('drivers/usb/hid_kbd.c', 'usbp_input_key_repeat(f->src, s->repeat);', '(void)f;'),
                ('drivers/usb/hid_mouse.c', '(i8)r[1], (i8)r[2]', 'r[1], r[2]')]
            for rel, anchor, replacement in mutations:
                original = (ROOT/rel).read_text()
                assert original.count(anchor) == 1, anchor
                dest = tmp/rel
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_text(original.replace(anchor, replacement))
                srcs = [str(dest) if s == rel else s for s in SOURCES]
                subprocess.run(['gcc', '-m32', '-I'+str(tmp), *flags,
                                'tools/tests/usb_hid_host.c', *srcs, '-o', str(tmp/'bad')],
                               cwd=ROOT, check=True)
                result = host32.run([str(tmp/'bad')])
                assert result.returncode != 0, anchor
                print('RED runtime:', anchor, 'rc=', result.returncode)
                dest.unlink()


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__, *(['--mutate'] if ctx.mutate else [])],
                           cwd=ctx.root, env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    check(parser.parse_args().mutate)
