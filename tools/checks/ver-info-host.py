#!/usr/bin/env python3
CHECK = dict(name='ver-info-host', tier=1,
    covers=['.claude/skills/os32-realhw-update/SKILL.md', 'docs/decisions/VERSIONS.md', 'include/input_service.h', 'kernel/input_service.c', 'platform/pc98/d2_bind.c', 'drivers/sound/cs4231/pcm_cs4231.h', 'drivers/char/serial.h', 'exec/exec.c', 'exec/exec_image.inc', 'exec/exec_argv.inc', 'exec/exec_kapi_init.inc', 'gfx/gfx_core.c', 'include/bootinfo.h', 'include/config.h', 'include/gfx_format.h', 'include/gfx_hal.h', 'include/gfx_mode.h', 'include/types.h', 'kapi/kapi_generated.c', 'kapi/kapi_sys.c', 'kdebug/kdebug_window.c', 'kdebug/kdebug_window.h', 'kdebug/kpeek.c', 'kernel/time/cpu_calibrate.c', 'kernel/time/cpu_calibrate.h', 'kernel/time/cpu_calibrate_math.c', 'kernel/time/cpu_calibrate_math.h', 'arch/x86/kernel/idt.h', 'kernel/module/module.c', 'kernel/module/module.h', 'kernel/time/pit_math.h', 'kernel/sqlite_engine.c', 'kernel/sqlite_engine.h', 'kernel/ver_collect.c', 'kernel/ver_format.c', 'kernel/ver_info.h', 'lib/kstring.h', 'lib/kstring_asm.asm', 'sdk/gen_kapi.py', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_kapi_slots.h', 'sdk/include/os32/os32_ver_info.h', 'sdk/kapi.json', 'sdk/kapi_rust_gen.py', 'sdk/os32_generations.py', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/generations.rs', 'sdk/rust/os32api/src/kapi_generated.rs', 'tools/lan_bridge.py', 'tools/lanfs_host.py', 'tools/rshell_peek.py', 'tools/rshell_recover.py', 'tools/rshell_serial.py', 'tools/serialfs_host.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/test_about_info.py', 'tools/tests/test_np21w_gui.py', 'tools/tests/test_rshell_peek.py', 'tools/tests/test_rshell_recover.py', 'tools/tests/test_ver_accept.py', 'tools/tests/test_ver_api.py', 'tools/tests/test_ver_collect.py', 'tools/tests/test_ver_format.py', 'tools/tests/test_ver_gfx.py', 'tools/tests/test_ver_layout.py', 'tools/tests/test_ver_services.py', 'tools/tests/test_ver_shell.py', 'tools/tests/test_ver_state_name.py', 'tools/tests/test_ver_version.py', 'tools/tests/ver_about_host.c', 'tools/tests/ver_collect_host.c', 'tools/tests/ver_format_host.c', 'tools/tests/ver_layout_host.c', 'tools/tests/ver_services_host.c', 'tools/tests/ver_test_utils.py', 'userland/deploy.yaml', 'userland/gshell/src/wm.rs', 'userland/rust/about/src/info.rs', 'userland/rust/about/src/lib.rs', 'userland/rust/libos32gui/src/layout.rs', 'userland/rust/libos32gui/src/widget.rs', 'userland/shell/cmd_base.c', 'tools/tests/kernel_includes.py', 'lib/crc32_core.inc', 'tools/gui_gate.py', 'tools/np21w_mcp/gui.py', 'tools/np21w_mcp/np21w_client.py', 'tools/tests/fixtures/gui_about.png', 'userland/gshell/src/startmenu.rs', 'userland/gshell/src/taskbar.rs'],
    needs=['check:ver-version-host', 'check:ver-layout-host', 'check:ver-state-name-host', 'check:ver-services-host', 'check:ver-gfx-host', 'check:ver-collect-host', 'check:ver-format-host', 'check:ver-api-host', 'check:ver-shell-host', 'check:rshell-peek-host', 'check:rshell-recover-host'], budget=60.0)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_about_info.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_ver_accept.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
