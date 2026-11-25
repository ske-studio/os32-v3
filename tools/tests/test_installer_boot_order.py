"""Exercise both installer mains with deferred boot on the host disk fixtures."""
import subprocess
import tempfile
from pathlib import Path

CHECK = dict(name='installer-boot-order-host', tier=1, needs=[], budget=10,
             covers=['tools/tests/test_installer_boot_order.py', 'tools/tests/cdinst_host.c',
                     'tools/tests/install_fresh_host.c', 'tools/tests/mtar_freestanding/**',
                     'tools/tests/host32.py', 'tools/tests/cc_cache.py', 'platform/pc98/**',
                     'userland/system/**', 'userland/lib/**', 'userland/shell/hdprep_plan.*',
                     'include/**', 'sdk/include/**', 'fs/pc98pt.*', 'fs/ext2/**'],
             basis=['docs/decisions/ppc/PPC_ROOT_MOUNT_ALIGN_X86_DESIGN.md §5'])

DIRECTORY_SOURCES = ['userland/system/inst_hdd.c', 'sys/platform/pc98/install_target.c',
                     'userland/system/inst_disk.c', 'sys/platform/pc98/install_disk.c',
                     'userland/shell/hdprep_plan.c', 'sys/platform/pc98/install_plan.c',
                     'sys/fs/pc98pt.c', 'sys/fs/ext2/ext2_layout.c']


def run(ctx):
    import host32
    with tempfile.TemporaryDirectory() as work:
        tmp = Path(work)
        # Use the disk fixture's geometry and media with PowerMac boot ordering.
        layout = tmp/'order.h'
        layout.write_text('#include "platform_install_layout.h"\n'
                          '#undef PLATFORM_BOOT_AFTER_FILES\n#define PLATFORM_BOOT_AFTER_FILES 1\n')
        flags = ['gcc', '-std=gnu11', '-D__cdecl=', '-include', layout, '-Iplatform/pc98',
                 '-I.', '-Iinclude', '-Isdk/include/os32', '-Iuserland/lib', '-Iuserland/system']
        builtin = subprocess.check_output(['gcc', '-print-file-name=include'], text=True).strip()
        for entry, fixture in [('install', 'install_fresh_host.c'), ('cdinst', 'cdinst_host.c')]:
            source = f'userland/system/{entry}.c'
            body = (ctx.root/source).read_text()
            installer = tmp/Path(source).name
            harness = tmp/fixture
            text = (ctx.root/'tools/tests'/fixture).read_text()
            harness.write_text(text.replace(f'"../../{source}"', f'"{installer}"')
                               .replace('"../../', f'"{ctx.root}/'))
            if entry == 'install':
                args = ['-D__OS32_USERLAND__', '-Wno-pointer-to-int-cast', *DIRECTORY_SOURCES]
            else:
                args = ['-m32', '-march=i386', '-nostdinc', '-isystem', builtin, '-nostdlib',
                        '-static', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                        '-fno-builtin', '-O1', '-Itools/tests/mtar_freestanding']
            for early in ([False, True] if ctx.mutate else [False]):
                if early:
                    anchor = 'if (!PLATFORM_BOOT_AFTER_FILES'
                    assert body.count(anchor) == 1
                    body = body.replace(anchor, 'if (1')
                installer.write_text(body)
                exe = tmp/'host'
                subprocess.run([*flags, *args, harness, '-o', exe], cwd=ctx.root, check=True)
                rc = host32.run([exe, 'boot-order']).returncode
                assert rc == (51 if early else 0), (entry, early, rc)
                print(entry, 'early deployment RED' if early else 'OK')
    return 0
