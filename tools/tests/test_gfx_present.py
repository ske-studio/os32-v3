"""Execute the shared present entries with each machine's width and permission macros."""
from pathlib import Path
import subprocess
import tempfile
from kernel_includes import include_flags

CHECK = dict(name='gfx-present-host', tier=1, needs=[], budget=5,
    covers=['gfx/gfx_present.c', 'gfx/gfx_dirty.c', 'gfx/gfx_dirty.h', 'gfx/gfx_internal.h',
            'gfx/gfx.h', 'gfx/v2/gfx2_int.h', 'include/**', 'platform/*/platform_display_fallback.h',
            'tools/tests/gfx_present_host.c', 'tools/tests/gfx2_bridge_fake.h',
            'tools/tests/test_gfx_present.py', 'tools/tests/kernel_includes.py', 'build/config.mk'],
    basis=['docs/decisions/ARCH_COMPAT_CONTRACT.md AC-DISPLAY-2'])

MUTATIONS = [
    ('commits', 'gfx_counters.commits = commits_before + 1;', '(void)commits_before;', 1),
    ('rect without buffer', 'if (!bb_b) goto diag;\n    if (!DISPLAY_V1_PRESENT_OK()) return;\n    if (g_backend',
     'if (!DISPLAY_V1_PRESENT_OK()) return;\n    if (g_backend', 2),
    ('packed raster', 'if (present_dirty_packed()) goto diag;', '(void)0;', 3),
]


def run(ctx):
    root = Path(ctx.root)
    original = (root / 'gfx/gfx_present.c').read_text()
    with tempfile.TemporaryDirectory() as folder:
        source = Path(folder) / 'gfx_present.c'
        exe = Path(folder) / 'present'
        for machine, flags in [('pc98', ['-DTEST_WIDTH=640']),
                               ('powermac', ['-DTEST_WIDTH=800', '-DTEST_POWERMAC'])]:
            fallback = Path(folder) / "platform_display_fallback.h"
            fallback.unlink(missing_ok=True)
            fallback.symlink_to(root / "platform" / machine / fallback.name)
            for name, anchor, replacement, expected in [('control', '', '', 0),
                                                        *(MUTATIONS if ctx.mutate else [])]:
                if anchor:
                    assert original.count(anchor) == 1, name
                source.write_text(original.replace(anchor, replacement) if anchor else original)
                subprocess.run(['gcc', '-std=gnu11', '-D__KERNEL_BUILD__', *flags,
                                '-I' + folder, *include_flags(root),
                                str(source), str(root / 'gfx/gfx_dirty.c'),
                                str(root / 'tools/tests/gfx_present_host.c'), '-o', str(exe)], check=True)
                rc = subprocess.run([exe]).returncode
                assert rc == expected, (machine, name, rc)
                print(machine, 'runtime RED' if expected else 'PASS', name)
    return 0
