#!/usr/bin/env python3
"""S3 byte/provenance exit, including unmapped HID and non-byte boundaries."""
CHECK = dict(name='input-s3-host', tier=1, budget=10, needs=[], kind='contract',
    basis=['INPUT_KEYMAP_DESIGN §2 §4 §7 S3/S3d §8 K10', 'INPUT_BIND_DESIGN §2a'],
    covers=['input/hal/input_context.h', 'input/hal/legacy_keymap.c', 'input/hal/keymap_eval.c', 'input/hal/keymap.h',
            'input/hal/in2_int.h', 'input/hal/kbd_merge.c', 'input/hal/in2_dev.c', 'input/hal/keymap_registry.c', 'input/hal/keymap_state.c', 'tools/tests/input_s3_capture_host.c', 'input/keymaps/hid_jp.c', 'tools/tests/input_s3_host.c',
            'tools/tests/test_input_s3.py', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'tools/tests/host_arch/**', 'platform/pc98/platform_io.h', 'include/**', 'drivers/input/*.h', 'sdk/include/os32/**', 'build/config.mk'])
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
PATH = 'input/hal/legacy_keymap.c'
def run(ctx):
    source = (ctx.root/PATH).read_text()
    with tempfile.TemporaryDirectory(prefix='input-s3-') as td:
        tmp = Path(td)
        def build(code):
            (tmp/'exit.c').write_text(code)
            exe = tmp/'test'
            subprocess.run(['gcc', '-m32', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-ffunction-sections', '-fdata-sections', '-fno-pie', '-fno-stack-protector', '-nostdinc', '-nostdlib',
                '-static', '-Wl,--gc-sections', '-no-pie', '-I'+str(ctx.root/'input/hal'), *include_flags(ctx.root),
                str(tmp/'exit.c'), str(ctx.root/'input/hal/keymap_eval.c'),
                str(ctx.root/'input/keymaps/hid_jp.c'), str(ctx.root/'tools/tests/input_s3_host.c'),
                '-o', str(exe)], check=True)
            return host32.run([str(exe)], runner='qemu').returncode
        assert build(source) == 0
        if ctx.mutate:
            for anchor, replacement in [
                ('legacy_cooked_push(((u16)sc << 8) | byte)', 'legacy_cooked_push(byte)'),
                ('result->sym < 0x80', 'result->sym < 0x100'),
                ('byte = result->sym & 0x7f', 'byte = 0'),
                ('result->kind = KM_LEGACY_CONTROL;', 'result->kind = KM_CHAR;'),
                ('sc == KEY_STOP && input_key_pressed(KEY_CTRL)', 'sc == KEY_STOP && 0'),
                ('sc != 0xff && legacy_stop_filter', '0 && legacy_stop_filter'),
                ('if (sc == 0 && byte == 0) return;', 'if (sc == 0 && byte == 0) byte = 1;')]:
                assert source.count(anchor) == 1, anchor
                rc = build(source.replace(anchor, replacement))
                assert 0 < rc < 128, (anchor, rc)
                print('RED runtime:', anchor, flush=True)
            assert build(source) == 0
    with tempfile.TemporaryDirectory(prefix='input-s3-capture-') as td:
        tmp = Path(td)
        paths = ['input/hal/'+n+'.c' for n in
                 ('in2_dev', 'kbd_merge', 'legacy_keymap', 'keymap_registry', 'keymap_state', 'keymap_eval')]
        source = (ctx.root/'input/hal/kbd_merge.c').read_text()
        (tmp/'io.h').write_text('static inline unsigned irq_save(void) { return 0; }\nstatic inline void irq_restore(unsigned f) { (void)f; }\n')
        def capture(code):
            (tmp/'merge.c').write_text(code)
            files = [str(tmp/'merge.c') if p == 'input/hal/kbd_merge.c' else str(ctx.root/p) for p in paths]
            exe = tmp/'capture'
            subprocess.run(['gcc', '-m32', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-ffunction-sections', '-fdata-sections', '-fno-pie', '-fno-stack-protector', '-nostdinc', '-nostdlib',
                '-static', '-Wl,--gc-sections', '-no-pie', '-I'+str(tmp), '-I'+str(ctx.root/'input/hal'),
                *include_flags(ctx.root), *files, str(ctx.root/'tools/tests/input_s3_capture_host.c'),
                '-o', str(exe)], check=True)
            return host32.run([str(exe)], runner='qemu').returncode
        assert capture(source) == 0
        if ctx.mutate:
            anchor = '    km_legacy_compat(&result);'
            assert source.count(anchor) == 1
            rc = capture(source.replace(anchor, '    if (sc == 0xff) { irq_restore(flags); return 0; }\n'+anchor))
            assert 0 < rc < 128, rc
            print('RED runtime: capture drops unmapped character', flush=True)
            assert capture(source) == 0
    print('PASS S3: provenance, sc/byte, physical STOP, GUI filter, KP bounds, high bytes, Clear/F13, Alt/Command', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
