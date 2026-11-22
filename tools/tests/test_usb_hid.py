#!/usr/bin/env python3
"""USB 1D boot HID: A5/A6, repeat, signed mouse and composite F11."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ['input/keymaps/hid_jp.c', 'drivers/usb/hid_kbd.c', 'drivers/usb/hid_kbd_map.c', 'drivers/usb/hid_mouse.c']
CHECK = dict(env=['ppc-cross'], name='usb-hid', tier=1, needs=[], budget=10, covers=[
    'drivers/usb/hid_kbd.c', 'drivers/usb/hid_kbd_map.c', 'drivers/usb/hid_mouse.c',
    'input/keymaps/hid_jp.c', 'input/hal/keymap.h',
    'drivers/usb/hid_boot.h', 'drivers/usb/usb_core.h',
    'drivers/usb/usb_desc.h', 'include/usb_platform.h', 'include/types.h',
    'include/callconv.h', 'include/endian_le.h', 'tools/tests/usb_hid_host.c',
    'tools/tests/test_usb_hid.py', 'tools/tests/host32.py', 'tools/tests/cc_cache.py'],
    basis=['USB_DESIGN §5', 'USB_DESIGN §7'],
)


def check(mutate):
    flags = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wvla', '-Iinclude', '-I.']
    bare = ['-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie', '-nostdlib', '-static']
    with tempfile.TemporaryDirectory(prefix='usb-hid-') as directory:
        tmp = Path(directory)
        for env, target in (('CROSS_DIR', 'i386-elf-gcc'),
                            ('PPC_CROSS_DIR', 'powerpc-elf-gcc')):
            cross = os.environ.get(env)
            compiler = shutil.which(str(Path(cross)/'bin'/target) if cross else target)
            if not compiler:
                print('SKIP cross compile:', target)
                if env == 'PPC_CROSS_DIR' and os.environ.get('OS32_REQUIRE_PPC') == '1':
                    raise RuntimeError('FAIL required PowerPC compiler missing')
                continue
            for src in SOURCES:
                subprocess.run([compiler, *flags, '-ffreestanding', '-c', src,
                                '-o', str(tmp/'unit.o')], cwd=ROOT, check=True)
        host = ['-nostdinc', '-I'+str(tmp)]
        for swap in ([], ['-DUSB_TEST_SWAP']):
            exe = tmp/'hid'
            subprocess.run(['gcc', '-m32', *bare, *host, *flags, *swap, 'tools/tests/usb_hid_host.c',
                            *SOURCES, '-o', str(exe)], cwd=ROOT, check=True)
            result = host32.run([str(exe)], runner='qemu')
            assert result.returncode == 0, result.returncode
            print('HID runtime OK', swap)
        if mutate:
            mutations = [
                ('drivers/usb/hid_kbd_map.c', 'hid_kbd_sc(usage) < 128 && ', ''),
                ('drivers/usb/hid_kbd.c', 'hid_kbd_order(i) != order', 'hid_kbd_order(i) != 2 - order'),
                ('drivers/usb/hid_boot.h', 'b->x.short_ok = 1;', 'b->x.short_ok = 0;'),
                ('drivers/usb/hid_kbd_map.c', 'return hid_jp_backend.compat_sc[usage];',
                 'return usage == 0x14 ? 0x11 : hid_jp_backend.compat_sc[usage];'),
                ('drivers/usb/hid_kbd_map.c',
                 'return (bits[usage >> 3] >> (usage & 7)) & 1;', 'return 0 & bits[usage >> 3];'),
                ('drivers/usb/hid_kbd_map.c',
                 'return (ext->order[usage >> 2] >> ((usage & 3) * 2)) & 3;',
                 'return 0 & ext->order[usage >> 2];'),
                ('drivers/usb/hid_kbd.c', 's->boot.iface->src, i, down', 's->boot.iface->src, hid_kbd_sc(i), down'),
                ('drivers/usb/hid_boot.h', 'usbp_input_release(b->kind, b->iface->src);', '(void)b;'),
                ('drivers/usb/hid_boot.h', 'usbp_input_release(b->kind, f->src);', '(void)b;'),
                ('drivers/usb/hid_kbd.c', 'usbp_input_usage_repeat(f->src, s->repeat);', '(void)f;'),
                ('drivers/usb/hid_kbd.c', '(i32)(now - s->deadline) >= 0', '(i32)(now - s->deadline) > 0'),
                ('drivers/usb/hid_mouse.c', '(i8)r[1], (i8)r[2]', 'r[1], r[2]')]
            for rel, anchor, replacement in mutations:
                original = (ROOT/rel).read_text()
                assert original.count(anchor) == 1, anchor
                dest = tmp/rel
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_text(original.replace(anchor, replacement))
                srcs = [str(dest) if s == rel else s for s in SOURCES]
                subprocess.run(['gcc', '-m32', *bare, *host, *flags,
                                'tools/tests/usb_hid_host.c', *srcs, '-o', str(tmp/'bad')],
                               cwd=ROOT, check=True)
                result = host32.run([str(tmp/'bad')], runner='qemu')
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
