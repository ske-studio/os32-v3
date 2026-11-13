#!/usr/bin/env python3
"""KCG packing and EBDT/manifest acceptance through the CHECK runner."""
CHECK = dict(name='kcg-lz4-host', tier=1,
    covers=['tools/gen_font16.py', 'tools/gen_g16.py', 'tools/mkroot_image.py',
     'build/ram-root.mk', 'build/ppc-kboot.mk', 'build/assets.mk', 'build/font-fetch.mk',
     'assets/fonts/**', 'tools/fetch_fonts.py', 'tools/gen_unicode.c',
     'lib/unicode_jis_table.h', 'kernel/console/unicode_file.h', 'include/endian_le.h',
     'include/types.h', 'include/callconv.h', 'build/ppc-ram-root.json', 'include/config.h',
     'tools/tests/test_kcg_ebdt.py', 'tools/tests/test_mkroot_image.py', 'build/core.yaml',
     'build/packages.yaml', 'userland/deploy.yaml', 'tools/deploy_manifests.py',
     'tools/mkpkg.py', 'tools/mkfat12.py', 'lib/lz4.c', 'lib/lz4.h',
     'tools/tests/test_kcg_lz4.py', 'requirements.txt'],
    needs=['make:assets/fonts/shino16.kcgfont', 'make:unicode_bin'], budget=60)


def run(ctx) -> int:
    import importlib
    import subprocess
    import sys

    # The standalone LZ4 test returns zero on SKIP; reject missing prerequisites
    # here so the CHECK runner cannot count that case as a passing check.
    for module in ('lz4.block', 'PIL.ImageFont', 'fontTools.ttLib'):
        try:
            importlib.import_module(module)
        except ImportError as exc:
            print(f'SKIP kcg-lz4-host: {module} unavailable ({exc}); not PASS', flush=True)
            return 1
    font = ctx.root / 'assets/fonts/JF-Dot-Shinonome16.ttf'
    if not font.is_file():
        print(f'SKIP kcg-lz4-host: missing {font}; not PASS', flush=True)
        return 1
    for script in ('tools/tests/test_kcg_lz4.py', 'tools/tests/test_kcg_ebdt.py'):
        argv = [sys.executable, '-B', script]
        if ctx.mutate:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, close_fds=False)
        if rc:
            return rc
    return 0
