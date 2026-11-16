#!/usr/bin/env python3
"""期待の出所: INPUT_KEYMAP_DESIGN §7 S1m・S2a、K5."""
CHECK = dict(name='input-s2a', tier=1, budget=10, needs=[], covers=[
    'input/hal/usb_input.[ch]', 'input/hal/keymap.h', 'input/hal/in2_int.h',
    'input/hal/in2_dev.c', 'input/hal/keymap_registry.c', 'input/keymaps/hid_jp.c',
    'platform/powermac/input_keymap_backend.c', 'platform/powermac/input_boot.[ch]',
    'include/types.h', 'include/io.h', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/input_s2a_host.c', 'tools/tests/test_input_s2a.py', 'tools/tests/host32.py'],
    basis=['INPUT_KEYMAP_DESIGN §7 S1m・S2a、K5'], kind='contract')
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32
ROOT = Path(__file__).resolve().parents[2]


def run(ctx):
    source = (ctx.root/'input/hal/usb_input.c').read_text()
    with tempfile.TemporaryDirectory(prefix='input-s2a-') as td:
        tmp = Path(td)
        for name in ('keymap.h', 'in2_int.h', 'usb_input.h'):
            (tmp/name).write_text((ctx.root/'input/hal'/name).read_text())
        for name in ('in2_dev.c', 'keymap_registry.c'):
            (tmp/name).write_text((ctx.root/'input/hal'/name).read_text())
        (tmp/'io.h').write_text('static inline unsigned irq_save(void) { return 0; }\nstatic inline void irq_restore(unsigned f) { (void)f; }\n')
        files = [tmp/'usb_input.c', tmp/'in2_dev.c', tmp/'keymap_registry.c',
                 ctx.root/'input/keymaps/hid_jp.c', ctx.root/'platform/powermac/input_keymap_backend.c',
                 ctx.root/'tools/tests/input_s2a_host.c']
        def build(s):
            (tmp/'usb_input.c').write_text(s)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                            '-I'+str(tmp), '-I'+str(ctx.root), '-I'+str(ctx.root/'include'),
                            '-I'+str(ctx.root/'sdk/include/os32'), *map(str, files), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu').returncode
        assert build(source) == 0
        print('PASS S1m registration and S2a source/bind/usage/repeat/release/generation', flush=True)
        if ctx.mutate:
            for name, old, new in [
                ('omit map2 bind', 'rc = km_source_bind((u8)src, generation, KM_MAP_HID);', 'rc = 0;'),
                ('omit source release', 'input_src_release(usb_sources[usb_src].src, usb_sources[usb_src].generation);', '(void)0;'),
                ('ignore stored generation', 'input_src_info(usb_sources[usb_src].src,\n                       usb_sources[usb_src].generation, &kind, &origin)', '(kind = INPUT_KIND_KBD, origin = INPUT_ORIGIN_USB, 1)')]:
                assert source.count(old) == 1
                assert build(source.replace(old, new)) != 0
                print('RED runtime: '+name, flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
