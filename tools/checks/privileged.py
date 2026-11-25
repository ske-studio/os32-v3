#!/usr/bin/env python3
CHECK = dict(name='privileged', tier=1,
    covers=['sys/include/callconv.h', 'sys/include/abi.h', 'sys/arch/x86/arch_abi.h', 'sys/include/config.h', 'sys/include/memmap.h', 'sys/dev/video/geforce/diag/gf_command_wire.h', 'sys/dev/video/geforce/diag/gf_foundation_wire.h', 'sys/dev/video/geforce/diag/gfinit_wire.h', 'sys/kern/kdebug/recover_wire.h', 'lib/zlib/zconf.h', 'lib/zlib/zlib.h', 'sdk/allocator/nano_adapter.h', 'sdk/include/os32/help.h', 'sdk/include/os32/ls.h', 'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_gui_shared.h', 'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_kapi_slots.h', 'sdk/include/os32/os32_ls.h', 'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_unit_stamp.h', 'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32api.h', 'sys/dev/pci/pci_decode.c', 'sys/dev/pci/pci_decode.h', 'sys/include/pci_dev_contract.h', 'sys/fs/pc98pt.c', 'sys/fs/pc98pt.h', 'sys/fs/ext2/ext2_layout.c', 'sys/fs/ext2/ext2_layout.h', 'sys/fs/ext2/ext2.h', '.env.sample', 'sys/kern/kstring_asm.asm', 'sys/kern/kstring_c.c', 'sys/kern/kstring.h', 'lib/crc32_core.inc', 'sys/include/types.h', 'lib/utf8.h', 'lib/lz4.h', 'lib/microtar/microtar.h', 'lib/sqlite3/sqlite3.c', 'lib/sqlite3/sqlite3.h', 'lib/sqlite3/os32_sqlite_config.h', 'userland/**/*.[ch]', 'userland/**/*.inc', 'userland/**/*.rs', 'userland/**/*.asm', 'tools/check_privileged.py', 'tools/check_privileged_allow.txt', 'tools/tests/test_check_privileged.py'],
    needs=[], budget=7.9,
    basis=['[C1]', 'ARCH_COMPAT_CONTRACT §4.1'],
)


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
