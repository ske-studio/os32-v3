#!/usr/bin/env python3
CHECK = dict(name='privileged', tier=1,
    covers=['include/config.h', 'include/memmap.h', 'kdebug/gf_command_wire.h', 'kdebug/gf_foundation_wire.h', 'kdebug/gfinit_wire.h', 'kdebug/recover_wire.h', 'lib/zlib/zconf.h', 'lib/zlib/zlib.h', 'sdk/allocator/nano_adapter.h', 'sdk/include/os32/help.h', 'sdk/include/os32/ls.h', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_gui_shared.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_kapi_slots.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_unit_stamp.h', 'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32api.h', 'drivers/pci/pci_decode.c', 'drivers/pci/pci_decode.h', 'include/pci_dev_contract.h', 'drivers/block/pc98pt.c', 'drivers/block/pc98pt.h', 'fs/ext2/ext2_layout.c', 'fs/ext2/ext2_layout.h', 'fs/ext2/ext2.h', '.env.sample', 'lib/kstring_asm.asm', 'lib/kstring_c.c', 'lib/kstring.h', 'lib/crc32_core.inc', 'include/types.h', 'lib/utf8.h', 'lib/lz4.h', 'lib/microtar/microtar.h', 'lib/sqlite3/sqlite3.c', 'lib/sqlite3/sqlite3.h', 'lib/sqlite3/os32_sqlite_config.h', 'userland/**/*.[ch]', 'userland/**/*.inc', 'userland/**/*.rs', 'userland/**/*.asm', 'tools/check_privileged.py', 'tools/check_privileged_allow.txt', 'tools/tests/test_check_privileged.py'],
    needs=[], budget=7.120772)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_check_privileged.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    env = dict(base_env)
    argv = ['python3', 'tools/check_privileged.py', '--strict']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
