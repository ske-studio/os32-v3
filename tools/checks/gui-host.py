#!/usr/bin/env python3
CHECK = dict(name='gui-host', tier=1,
    covers=['tools/tests/test_gui_trim.py', 'tools/tests/gui_trim_host.rs', 'tools/tests/gui_font_host.rs', 'userland/lib/font/**', 'tools/tests/test_font_reader.py', 'tools/fetch_fonts.py', 'tools/tests/test_font_outline.py', 'tools/tests/rust_alloc_host.rs', 'userland/rust/libos32gui_stub/src/lib.rs', 'tools/tests/mutpar.py', 'tools/tests/test_gui_reattach.py', 'tools/tests/gui_reattach_host.rs', 'sdk/rust/os32api/src/ls.rs', 'tools/tests/os32api_host.py', 'sdk/rust/os32api/src/**', 'userland/rust/gdi_test/src/lib.rs', 'userland/rust/gdi_test/src/corpus.rs', 'userland/rust/gdi_test/src/fontbench.rs', 'userland/rust/libos32gui/src/**', 'userland/rust/libos32gui/host_tests/**', 'sdk/rust/os32api/src/cfg.rs', 'sdk/rust/os32api/src/host.rs', 'userland/rust/libos32gui/src/cfgro.rs', 'userland/rust/libos32gui/src/hostsvc.rs', 'userland/rust/libos32gui/src/utf8core.rs', 'tools/tests/g16_fixture.py', 'tools/gen_g16.py', 'tools/g16/**', 'assets/ipadic/*.csv'],
    needs=[], budget=11.40838)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_gui_reattach.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['cargo', 'test', '--manifest-path', 'userland/rust/libos32gui/host_tests/Cargo.toml', '--target', 'x86_64-unknown-linux-gnu', '--offline']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
