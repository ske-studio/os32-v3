#!/usr/bin/env python3
CHECK = dict(name='ver-info-host', tier=1,
    covers=['sys/kern/exec/os32x_hdr.c', 'sys/kern/exec/os32x_hdr.h', 'sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'sys/platform/pc98/bootinfo_wire.h', 'sys/platform/pc98/bootinfo_native.h', 'sys/hal/time/clock_source.h', 'sys/platform/pc98/clock_source.c', 'sys/platform/pc98/pit_time.c', 'sys/platform/pc98/pit_time.h', 'sys/include/boot_diag.h', 'sys/platform/pc98/boot_diag.c', 'sys/platform/pc98/ver_backend.c', 'sys/platform/pc98/platform_ver.h', 'sys/platform/powermac/platform_ver.h', 'sys/include/ver_backend_contract.h', 'tools/np21w_mcp/symbols.py', '.claude/skills/os32-realhw-update/SKILL.md', 'docs/decisions/VERSIONS.md', 'sys/hal/input/input_service.h', 'sys/hal/input/input_service.c', 'sys/platform/pc98/d2_bind.c', 'sys/dev/sound/cs4231/pcm_cs4231.h', 'sys/dev/char/serial.h', 'sys/kern/exec/exec.c', 'sys/hal/gfx/gfx_core.c', 'sys/include/bootinfo.h', 'sys/include/config.h', 'sys/include/gfx_format.h', 'sys/include/gfx_hal.h', 'sys/include/gfx_mode.h', 'sys/include/types.h', 'sys/kapi/kapi_generated.c', 'sys/kapi/kapi_sys.c', 'sys/kapi/kapi_sysinfo.c', 'sys/kapi/kapi_sysinfo.h', 'sys/arch/x86/kdebug/kdebug_window.c', 'sys/arch/x86/kdebug/kdebug_window.h', 'sys/platform/pc98/kdebug/kpeek.c', 'sys/platform/pc98/time/cpu_calibrate.c', 'sys/hal/time/cpu_calibrate.h', 'sys/kern/time/cpu_calibrate_math.c', 'sys/kern/time/cpu_calibrate_math.h', 'sys/arch/x86/kernel/idt.h', 'sys/platform/pc98/pit.h', 'sys/include/kernel_tick.h', 'sys/kern/kdebug/kdebug_module.c', 'sys/kern/module/module.h', 'sys/platform/pc98/pit_math.h', 'sys/kern/sqlite_engine.c', 'sys/kern/sqlite_engine.h', 'sys/kern/sqlite_engine_body.h', 'sys/kern/ver_collect.c', 'sys/kern/ver_format.c', 'sys/kern/ver_info.h', 'sys/kern/kstring.h', 'sys/kern/kstring_asm.asm', 'sys/include/kapi_call_generated.h', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_kapi_slots.h', 'sdk/include/os32/os32_ver_info.h', 'sdk/kapi.json', 'sdk/kapi_rust_gen.py', 'sdk/os32_generations.py', 'sdk/os32x_hdr.py', 'sdk/rust/os32api/src/generations.rs', 'sdk/rust/os32api/src/kapi_generated.rs', 'tools/lan_bridge.py', 'tools/host_image.py', 'tools/lanfs_host.py', 'tools/rshell_peek.py', 'tools/rshell_hwb.py', 'tools/rshell_recover.py', 'tools/rshell_serial.py', 'tools/serialfs_host.py', 'tools/host_tree_manifest.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'tools/tests/test_about_info.py', 'tools/tests/test_np21w_gui.py', 'tools/module_schema.py', 'tools/tests/test_rshell_peek.py', 'tools/tests/test_rshell_recover.py', 'tools/tests/test_ver_accept.py', 'tools/tests/test_ver_api.py', 'tools/tests/test_ver_collect.py', 'tools/tests/test_ver_format.py', 'tools/tests/test_ver_gfx.py', 'tools/tests/test_ver_layout.py', 'tools/tests/test_ver_services.py', 'tools/tests/test_ver_shell.py', 'tools/tests/test_ver_state_name.py', 'tools/tests/test_ver_version.py', 'tools/tests/ver_about_host.c', 'tools/tests/ver_collect_host.c', 'tools/tests/ver_format_host.c', 'tools/tests/ver_layout_host.c', 'tools/tests/ver_services_host.c', 'tools/tests/ver_test_utils.py', 'userland/deploy.yaml', 'userland/gshell/src/wm.rs', 'userland/rust/about/src/info.rs', 'userland/rust/about/src/lib.rs', 'userland/rust/libos32gui/src/layout.rs', 'userland/rust/libos32gui/src/widget.rs', 'userland/shell/cmd_base.c', 'tools/tests/kernel_includes.py', 'tools/gui_gate.py', 'tools/np21w_mcp/gui.py', 'tools/np21w_mcp/np21w_client.py', 'tools/tests/fixtures/gui_about.png', 'userland/gshell/src/startmenu.rs', 'userland/gshell/src/taskbar.rs', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['check:ver-version-host', 'check:ver-layout-host', 'check:ver-state-name-host', 'check:ver-services-host', 'check:ver-gfx-host', 'check:ver-collect-host', 'check:ver-format-host', 'check:ver-api-host', 'check:ver-shell-host', 'check:rshell-peek-host', 'check:rshell-recover-host'], budget=5.3,
    kind='characterization',
)


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
