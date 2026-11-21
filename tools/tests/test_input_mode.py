#!/usr/bin/env python3
"""Compare mode transitions against the pre-extraction implementation."""
CHECK = dict(name='input-mode-host', tier=1, budget=3, needs=[], kind='characterization',
    covers=['input/hal/legacy_mode.c', 'input/hal/legacy_ring.c', 'input/hal/in2_int.h',
            'drivers/input/kbd.h', 'include/**', 'sdk/include/os32/**',
            'tools/tests/host_arch/**', 'platform/pc98/platform_io.h', 'build/config.mk',
            'tools/tests/kernel_includes.py', 'tools/tests/host32.py',
            'tools/tests/input_mode_host.c', 'tools/tests/test_input_mode.py'])
from pathlib import Path
import subprocess, sys, tempfile
from types import SimpleNamespace
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
def run(ctx):
    source = (ctx.root/'input/hal/legacy_mode.c').read_text()
    with tempfile.TemporaryDirectory(prefix='input-mode-') as directory:
        tmp = Path(directory)
        def execute(code):
            (tmp/'mode_source.inc').write_text(code)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdinc', '-nostdlib',
                '-static', '-no-pie', '-I'+str(tmp), '-I'+str(ctx.root/'tools/tests/host_arch'),
                '-I'+str(ctx.root/'input/hal'), *include_flags(ctx.root),
                str(ctx.root/'tools/tests/input_mode_host.c'), '-o', str(tmp/'test')], check=True)
            return host32.run([str(tmp/'test')], runner='qemu', timeout=10).returncode
        assert execute(source) == 0
        if ctx.mutate:
            for old,new in [('    legacy_cooked_flush();\n    /* raw', '    /* raw'),
                            ('        legacy_cooked_flush();', ''),
                            ('        kbd_raw_purge_keep();', ''),
                            ('        kbd_raw_purge_keep();', '        legacy_raw_flush();'), ('legacy_raw_flush();', ';'),
                            ('legacy_cooked_reset_dropped();', ';'), ('legacy_raw_reset_dropped();', ';'),
                            ('if (next == kbd_gui_mode) return;', 'if (0) return;'),
                            ('kbd_gui_mode = next;\n    kbd_stop_suppressed = 0;', 'kbd_gui_mode = next;'),
                            ('flags = irq_save();\n    kbd_gui_mode = next;', 'flags = irq_save(); irq_restore(flags);\n    kbd_gui_mode = next;')]:
                assert source.count(old) == 1, old
                assert execute(source.replace(old,new,1)) == 1, old
                print('RED runtime: '+old, flush=True)
        print('PASS mode transitions and dropped counts', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
