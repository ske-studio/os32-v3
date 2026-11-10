"""Host dispatch regression: no emulator, shared build, or device access."""
CHECK = dict(name='boot-splash-host', tier=1,
    covers=['gfx/gfx_emergency.c', 'gfx/gfx_emergency.h', 'drivers/video/pc98/pc98_route.h', 'gfx/gfx_vram.c', 'drivers/video/pc98/backend_pc98.c', 'gfx/gfx_core.c', 'platform/pc98/gfx_init.c', 'init/boot_splash.c', 'tools/tests/boot_splash_native_host.c', 'tools/tests/serial_diag_host.h', 'lib/serial_diag.h', 'include/platform_splash.h', 'platform/pc98/splash.c', 'tools/tests/kernel_includes.py', 'tools/tests/paging_source.py',
            'drivers/video/pc98/splash_raster.c', 'drivers/video/pc98/splash_raster.h', 'gfx/gfx_osri.h', 'gfx/gfx_core_*.inc'],
    needs=[], budget=4.0)

from kernel_includes import include_flags
from paging_source import paging_source
import argparse
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


MUTATIONS = (
    ('omit player', 'gfx/gfx_vram.c',
     '    (void)pc98_splash_raster_table(pc98_splash_raster_state(), table);',
     '    (void)table;', 'public raster uses common player'),
    ('relative 120us returns', 'gfx/gfx_vram.c',
     '    (void)pc98_splash_raster_table(pc98_splash_raster_state(), table);',
     '    cpu_delay_us(120);\n    (void)pc98_splash_raster_table(pc98_splash_raster_state(), table);',
     'relative raster delay must not return'),
    ('omit nonflip transfer', 'gfx/gfx_vram.c',
     '    } else {\n        _flush_dirty_queue();\n    }\n\n    (void)pc98_splash_raster_table',
     '    } else {\n        dirty_queue.count = 0;\n    }\n\n    (void)pc98_splash_raster_table',
     'dirty bytes include initial transfer'),
    ('omit raster flip', 'gfx/gfx_vram.c',
     '            prev_dirty = snapshot;\n            _flip_page();',
     '            prev_dirty = snapshot;', 'flip only when a page was transferred'),
    ('omit init reset', 'gfx/gfx_core.c',
     '    pc98_splash_raster_reset();\n    gfx_init_gen++;',
     '    gfx_init_gen++;', 'init200 invalidates timing'),
    ('omit shutdown reset', 'gfx/gfx_core.c',
     '    pc98_splash_raster_reset();\n    gfx_started = 0;',
     '    gfx_started = 0;', 'shutdown invalidates timing'),
)


def build_binary(directory, mutation=None):
    work = Path(directory)
    fixture = (ROOT / 'tools/tests/boot_splash_native_host.c').read_text()
    for rel in ('gfx/gfx_core.c', 'gfx/gfx_vram.c'):
        source = paging_source(ROOT / rel)
        if mutation and mutation[1] == rel:
            _, _, old, new, _ = mutation
            assert source.count(old) == 1, f'ambiguous/missing anchor: {mutation[0]}'
            source = source.replace(old, new, 1)
        source = re.sub(r'#include "(\.\./[^"\n]+)"',
                        lambda m: '#include "' + str((ROOT / rel).parent / m[1]) + '"', source)
        staged = work / Path(rel).name
        staged.write_text(source)
        fixture = fixture.replace('"../../' + rel + '"', '"' + str(staged) + '"')
    fixture = re.sub(r'#include "\.\./\.\./([^"\n]+)"',
                     lambda m: '#include "' + str(ROOT / m[1]) + '"', fixture)
    (work / 'fixture.c').write_text(fixture)
    binary = work / 'boot-native'
    command = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-function', '-D__KERNEL_BUILD__', '-Wl,--gc-sections',
               '-I' + str(ROOT / 'tools/tests'),
               '-I' + str(ROOT / 'drivers/video/pc98'), *include_flags(ROOT),
               str(work / 'fixture.c'), '-o', str(binary)]
    subprocess.run(command, cwd=ROOT, check=True)
    return binary


class NativeBootTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="os32-boot-native-")
        cls.binary = build_binary(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_native_boot_preserves_each_gui_preference(self):
        for pref in range(4):
            with self.subTest(preference=pref):
                result = subprocess.run([str(self.binary), str(pref)],
                                        capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_invalid_native_state_cleans_up_and_can_retry(self):
        for pref in range(4):
            with self.subTest(preference=pref):
                result = subprocess.run([str(self.binary), str(pref), "fault"],
                                        capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_public_raster_transfer_timing_and_lifecycle(self):
        result = subprocess.run([str(self.binary), '1', 'raster'],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(NativeBootTest)
    assert unittest.TextTestRunner().run(suite).wasSuccessful()
    print('GREEN native boot and public raster integration', flush=True)
    if args.mutate:
        for mutation in MUTATIONS:
            with tempfile.TemporaryDirectory(prefix='os32-raster-mut-') as td:
                binary = build_binary(td, mutation)
                result = subprocess.run([str(binary), '1', 'raster'],
                                        capture_output=True, text=True, timeout=60)
                assert result.returncode == 1 and mutation[4] in result.stderr, (
                    mutation[0], result.returncode, result.stdout, result.stderr)
                print(f'RED {mutation[0]}: compiled, runtime rc={result.returncode}', flush=True)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_boot_splash_native.py',
            *(['--mutate'] if ctx.mutate else [])]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0


if __name__ == "__main__":
    main()
