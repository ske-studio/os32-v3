#!/usr/bin/env python3
"""P1: real PEGC tables and lifecycle, fake C1/C11/Y4, runtime mutants."""
import argparse
from pathlib import Path
import tempfile
import host32
import mutpar
import test_pegc_mode as mode

ROOT = Path(__file__).resolve().parents[2]
V2 = 'drivers/video/pc98/pegc_v2.c'
BACKEND = mode.MUT_TARGET
RESTORE = mode.RESTORE_TARGET
MUTATIONS = [
    ('software-only', V2, 'out->caps2d_sw = ops;', 'out->caps2d_sw = 0;'),
    ('scanout-format', V2, 'out->fmt_scanout = 1u << GFX2_FMT_INDEX8;', 'out->fmt_scanout = 1u << GFX2_FMT_RGB565;'),
    ('empty-heap', V2, '*base = *size = *phys = 0;', '*base = *size = *phys = 1;'),
    ('kva', V2, '*va = s->kva;', '*va = NULL;'),
    ('completion', V2, '*done = 0;', '*done = 1;'),
    ('palette', V2, '(xrgb[i] >> 20) & 15', '(xrgb[i] >> 16) & 15'),
    ('publish-kind', BACKEND, 'gfx2_sync_publish(GFX2_KIND_PEGC, 0, s_sync);', 'gfx2_sync_publish(GFX2_KIND_PC98, 0, s_sync);'),
    ('bind-fallback', BACKEND, 's_probe_ok = 0;\n        return;', 's_probe_ok = 1;\n        return;'),
    ('fixed-shadow', BACKEND, 'gfxsync_mode(s_sync) != GFXSYNC_SHADOW', 'gfxsync_mode(s_sync) == GFXSYNC_SHADOW'),
    ('rebind-unpublish', BACKEND, 'gfx2_sync_publish(GFX2_KIND_PEGC, 0, NULL);', '(void)0;'),
    ('shutdown-unbind', RESTORE, '    pegc_sync_drop();\n    if (!s_active) return;', '    (void)0;\n    if (!s_active) return;'),
    ('text-restore-unbind', RESTORE, 'void pegc_restore_text_sync(int hsync31)\n{\n    pegc_sync_drop();', 'void pegc_restore_text_sync(int hsync31)\n{\n    (void)0;'),
    ('publish-rollback', BACKEND, 'rc = gfx2_sync_publish(GFX2_KIND_PEGC, 0, s_sync);\n    if (rc)', 'rc = gfx2_sync_publish(GFX2_KIND_PEGC, 0, s_sync);\n    if (!rc)'),
]


def observe(changes):
    with tempfile.TemporaryDirectory(prefix='pegc-v2-') as td:
        exe = Path(td) / 'pegc-v2'
        tree = mutpar.build_in_tree(ROOT, td, changes, [mode.host_cmd(exe)],
                                    capture_output=True)
        result = host32.run([str(exe)], cwd=tree, timeout=30, capture_output=True)
        return result.returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    assert observe({}) == 0
    print('GREEN P1 all hooks, capabilities, bind/rebind/unbind and failure fallback', flush=True)
    if args.mutate:
        for name, path, anchor, replacement in MUTATIONS:
            source = (ROOT / path).read_text()
            assert source.count(anchor) == 1, name
            rc = observe({path: source.replace(anchor, replacement)})
            assert 0 < rc < 128, (name, rc)
            print(f'RED {name}: runtime rc={rc}', flush=True)
        assert observe({}) == 0
        print('GREEN restored control', flush=True)


if __name__ == '__main__':
    main()
