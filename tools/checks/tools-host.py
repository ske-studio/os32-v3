#!/usr/bin/env python3
CHECK = dict(name='tools-host', tier=1,
    covers=['tools/np21w_mcp/gui.py', 'assets/fonts/IPA_Font_License_Agreement_v1.0.txt', 'assets/settings/defaults.tsv', 'build/core.yaml', 'build/packages.yaml', 'build/assets.mk', 'fs/vfs.c', 'lib/microtar/microtar.c', 'tools/deploy_manifests.py', 'tools/deploy_protect.py', 'tools/emu_agent/playbook.py', 'tools/fetch_fonts.py', 'userland/lib/font/**', 'userland/rust/font_test/Cargo.toml', 'userland/rust/font_test/src/lib.rs', 'assets/fonts/README.OS32', 'tools/mk_settings_db.py', 'tools/mkpkg.py', 'tools/nhd_deploy.py', 'tools/np21w_ctl.py', 'tools/np21w_ini.py', 'tools/np21w_ini_live.py', 'tools/np21w_mcp/np21w_client.py', 'tools/np21w_trial.py', 'tools/pc98pt.py', 'sdk/rust/os32api/src/ls.rs', 'tools/tests/os32api_host.py', 'tools/tests/stat_cmd_host.c', 'tools/tests/mutpar.py', 'tools/tests/test_about_info.py', 'tools/tests/test_emu_playbook.py', 'tools/tests/test_fetch_fonts.py', 'tools/tests/test_filer_copy_abort.py', 'tools/tests/test_filer_normalize.py', 'tools/tests/test_gui_button_dispatch.py', 'tools/tests/test_mk_blank_nhd.py', 'tools/tests/test_mk_settings_db.py', 'tools/tests/test_nhd_deploy_failure.py', 'tools/tests/test_np21w_ini.py', 'tools/tests/test_np21w_ini_live.py', 'tools/tests/test_np21w_transport.py', 'tools/tests/test_np21w_trial.py', 'tools/tests/test_stat_cmd.py', 'tools/tests/test_tar_cmd.py', 'tools/tests/ver_about_host.c', 'userland/cmds/stat.c', 'userland/cmds/tar.c', 'userland/deploy.yaml', 'userland/rust/about/src/info.rs', 'userland/rust/about/src/lib.rs', 'userland/shell/cmd_base.c', 'userland/gshell/src/wm.rs', 'userland/rust/filer/host/model_tests.rs', 'userland/rust/filer/src/model.rs', 'userland/rust/libos32gui/src/app.rs', 'userland/rust/libos32gui/src/layout.rs', 'userland/rust/libos32gui/src/widget.rs', 'sdk/rust/os32api/src/**', 'userland/gshell/src/taskbar.rs', 'userland/gshell/src/startmenu.rs', 'tools/nhd_profile.py', 'tools/gen_deploy_set.py', 'build/os32.ld', 'sdk/gen_kapi.py', 'kernel/module/module.c', 'kernel/ver_format.c', 'lib/kstring_asm.asm', 'tools/tests/host32.py', 'tools/tests/ver_test_utils.py', 'tools/tests/kernel_includes.py'],
    needs=['make:all'], budget=40.702598)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', '-m', 'unittest', 'discover', '-s', 'tools/tests', '-p', 'test_np21w_ini.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', '-m', 'unittest', 'discover', '-s', 'tools/tests', '-p', 'test_np21w_transport.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', '-m', 'unittest', 'discover', '-s', 'tools/tests', '-p', 'test_np21w_trial.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_nhd_deploy_failure.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_filer_normalize.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_filer_copy_abort.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_about_info.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_gui_button_dispatch.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    env['PYTHONPATH'] = '.'
    argv = ['python3', '-B', 'tools/tests/test_emu_playbook.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_mk_settings_db.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_fetch_fonts.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_mk_blank_nhd.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_stat_cmd.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_tar_cmd.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
