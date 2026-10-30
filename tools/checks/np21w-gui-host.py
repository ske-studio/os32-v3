#!/usr/bin/env python3
CHECK = dict(name='np21w-gui-host', tier=1,
    covers=['tools/np21w_mcp/gui.py', 'tools/np21w_mcp/gui_desc.py', 'tools/tests/test_gui_desc.py', 'exec/shell_service.h', 'sdk/include/os32/os32_kapi_shared.h', 'userland/lib/font/**', 'tools/tests/os32api_host.py', 'userland/gshell/**', 'sdk/rust/os32api/**', 'tools/np21w_mcp/server.py', 'tools/gui_gate.py', 'tools/h3_park_resume.py', 'tools/tests/test_np21w_gui.py', 'tools/tests/test_np21w_gui_mcp.py', 'tools/tests/fixtures/gui_about.png', 'tools/tests/fixtures/np21w_schema_before.json', 'tools/gen_font16.py', 'tools/gen_kcg_font.py', 'include/memmap.h', 'include/con_sink.h', 'exec/appslot.h', 'kernel/module/module_runtime.h', 'kernel/module/module_schema.h', 'kernel/ime/ime.h', 'kernel/ime/ime.c', 'kernel/ime/fep_module.c', 'build/fep-module.ld', 'userland/gshell/src/startmenu.rs', '.claude/skills/run-os32/SKILL.md', 'tools/np21w_mcp/np21w_client.py', 'tools/np21w_mcp/symbols.py', 'drivers/video/kcg/kcg_body.c', 'tools/h3_layout.c', 'tools/tests/kernel_includes.py'],
    needs=['make:assets/fonts/ipaexg16.kcgfont'], budget=12.205944)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_np21w_gui.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_np21w_gui_mcp.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
