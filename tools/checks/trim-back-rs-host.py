#!/usr/bin/env python3
CHECK = dict(name='trim-back-rs-host', tier=1,
    covers=['tools/tests/font_guard_inactive.h', 'arch/x86/arch_cpu.h', 'arch/x86/x86_flags.h', 'arch/x86/arch_io.h', 'drivers/input/kbd.h', 'exec/access_walk.c', 'exec/appmem.c', 'exec/appmem.h', 'exec/appmem_map.c', 'exec/appmem_unmap.c', 'exec/appslot.c', 'exec/appslot.h', 'exec/exec.c', 'exec/exec.h', 'exec/exec_heap.c', 'exec/exec_heap.h', 'exec/lease.c', 'exec/redir_access.c', 'exec/ring3_str.c', 'fs/fd_redirect.h', 'include/appmem_types.h', 'include/cpu.h', 'include/io.h', 'include/memmap.h', 'include/pc98.h', 'include/redir_access.h', 'include/sys.h', 'include/tvram.h', 'include/types.h', 'kapi/kapi_generated.c', 'kapi/kapi_sys.c', 'kernel/sqlite_engine.c', 'arch/x86/kernel/gdt.h', 'kernel/gui.c', 'kernel/gui.h', 'kernel/ime/ime.h', 'mm/kmalloc.c', 'mm/kmalloc.h', 'kdebug/kselftest.c', 'mm/paging.c', 'mm/paging.h', 'mm/paging_app.c', 'mm/paging_app.h', 'mm/pgalloc.c', 'mm/pgalloc.h', 'mm/physmem.c', 'mm/physmem.h', 'arch/x86/kernel/tss.h', 'lib/kstring.h', 'platform/pc98/platform_io.h', 'sdk/allocator/**', 'sdk/include/os32/os32_gui_shared.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32api.h', 'sdk/kapi.json', 'tools/check_nano_inputs.py', 'tools/ci/build_cross.sh', 'tools/tests/appmem_map_host.c', 'tools/tests/exec_heap_host.h', 'tools/tests/host32.py', 'tools/tests/host_arch/arch_cpu.h', 'tools/tests/host_arch/arch_io.h', 'tools/tests/mem_stat_host.c', 'tools/tests/multiapp_model_host.c', 'tools/tests/mutpar.py', 'tools/tests/pgalloc_host_fixture.h', 'tools/tests/test_appmem_map.py', 'tools/tests/test_mem_stat.py', 'tools/tests/test_trim_flow.py', 'tools/tests/test_trim_kernel.py', 'tools/tests/trim_flow_host.c', 'tools/tests/trim_kernel_host.c', 'userland/gshell/src/trim.rs', 'userland/shell/cmd_sys.c', 'tools/tests/test_trim_back_rs.py', 'tools/tests/trim_back_rs_host.rs', 'tools/tests/trim_stop_host.c', 'userland/rust/trim_back_rs/**', 'userland/rust/libos32gui/src/app.rs', 'userland/rust/libos32gui_stub/src/lib.rs', 'sdk/rust/os32api/**', 'sdk/rust/i686-os32-none.json', 'rust-toolchain.toml', 'tools/tests/trim_focus_host.rs', 'init/kernel.c', 'kernel/kstack_hw.c', 'tools/tests/kdebug_cli_host.c', 'userland/shell/cmd_base.c', 'init/sysconfig.c', 'tools/tests/kernel_layout.py', 'init/kaudit_boot.c', 'mm/memory_boot.c', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'tools/tests/kernel_includes.py', 'tools/fetch_fonts.py', 'tools/g16/gen_preload16.py', 'tools/gen_g16.py', 'tools/tests/g16_fixture.py', 'tools/tests/g3fix_wm_probe.py', 'tools/tests/os32api_host.py', 'tools/tests/test_trim_fixtures.py', 'tools/tests/trim_fixtures_host.c', 'userland/gshell/build.rs', 'userland/gshell/host/integration.py', 'userland/tests/trim_back.c', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py', 'tools/tests/paging_source.py'],
    needs=['make:all'], budget=55.65845)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_trim_back_rs.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
