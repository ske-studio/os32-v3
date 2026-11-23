"""Exercise install reservations against an in-memory medium provider."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='install-management-host', tier=1, needs=[], budget=4,
    covers=['kernel/dev/install_management.c', 'drivers/base/dev.c', 'fs/vfs.c', 'fs/ext2/ext2_super.c', 'fs/ext2/ext2_super_block_io.inc', 'fs/ext2/ext2_ctx.h', 'fs/ext2/ext2.h', 'fs/ext2/ext2_priv.h', 'drivers/ata/ide_request.inc',
            'drivers/ata/ide_addr.c', 'drivers/ata/ide_addr.h', 'drivers/ata/ide.h',
            'lib/crc32.c', 'lib/crc32.h', 'lib/crc32_core.inc', 'fs/vfs_device.h', 'fs/vfs.h', 'kernel/dev/dev.h', 'tools/tests/kernel_includes.py', 'include/install_access.h',
            'include/install_medium.h', 'include/install_management.h',
            'tools/tests/install_management_host.c', 'tools/tests/test_install_management.py',
            'sdk/include/os32/*.h', 'platform/*/platform_install_management.h'],
    basis=['docs/decisions/ppc/PPC_ROOT_MOUNT_ALIGN_X86_DESIGN.md §5–6'])

MUTATIONS = [
    ('if (platform_install_medium() || r->state != AVAILABLE) {',
     'if (r->state != AVAILABLE) {'),
    ('if (install_transfer_allowed(a, 0, 0)) return 0;', 'if (a) return 0;'),
    ('if (install_transfer_allowed(a, 0, 0)) return 0;',
     'if (install_transfer_allowed(a, 0, 0)) { irq_restore(*flags); return 0; }'),
    ('pin->unit = unit;\n            r->in_flight++;\n            rc = 1;', 'pin->unit = unit; rc = 0;'),
    ('if (issued.phase == VIEW &&', 'if (0 && issued.phase == VIEW &&'),
    ('r->owner == res_owner_get()) {\n        const InstallPlanV1', '1) {\n        const InstallPlanV1'),
    ('count <= length - (lba - start)', 'count <= length - (lba - start) + 1'),
    ('if (outcome == OS32_INSTALL_END_COMMIT) return inspect(handle, 1);',
     'if (outcome == OS32_INSTALL_END_ABORT) return inspect(handle, 1);'),
    ('if (slot && current &&', 'if (0 && slot && current &&'),
    ('memcmp(check, s->bytes, sizeof(check))', 'memcmp(check, s->bytes, sizeof(check)-1)'),
    ('r->state == VIEW && r->owner == res_owner_get()', 'r->state == VIEW'),
]


def run(ctx):
    from kernel_includes import include_flags
    headers = {
        'platform_install_management.h': '#include "install_medium.h"\nconst struct install_medium *platform_install_medium(void);',
        'exec.h': 'int exec_disk_write_allowed(void);',
        'fd_redirect.h': 'int res_owner_get(void);',
        'io.h': 'unsigned test_irq_save(void); void test_irq_restore(unsigned);\n#define irq_save test_irq_save\n#define irq_restore test_irq_restore',
        'kmalloc.h': 'void *kmalloc(u32); void kfree(void *);',
        'kstring.h': '#include <string.h>\n#define kmemcpy memcpy',
        'vfs.h': '#define VFS_DEV_UNIT_MAX 4\n#define VFS_MAX_FS 8\nvoid vfs_install_discard(u32); int vfs_install_precheck(u32); int vfs_install_detach(u32);',
    }
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        for name, content in headers.items():
            (tmp / name).write_text(content + '\n')
        provider = tmp / 'provider'
        provider.mkdir()
        (provider/'platform_install_management.h').write_text(headers['platform_install_management.h'])
        flags = ['gcc', '-std=gnu11', '-D__cdecl=', '-ffunction-sections', '-fdata-sections',
                 '-I', str(provider), *include_flags(ctx.root)]
        objects = []
        for source in ('drivers/base/dev.c', 'fs/vfs.c', 'fs/ext2/ext2_super.c', 'lib/crc32.c', 'drivers/ata/ide_addr.c', 'tools/tests/install_management_host.c'):
            obj = tmp / (Path(source).stem + '.o')
            subprocess.run([*flags, '-c', source, '-o', obj], cwd=ctx.root, check=True)
            objects.append(obj)
        src = (ctx.root / 'kernel/dev/install_management.c').read_text()
        for mutation in [None] + (MUTATIONS if ctx.mutate else []):
            code = src
            if mutation:
                old, new = mutation
                assert code.count(old) == 1, old
                code = code.replace(old, new)
            (tmp / 'management.c').write_text(code)
            exe = tmp / 'test'
            subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-D__cdecl=',
                            '-I', d, '-Iinclude', '-Isdk/include/os32', str(tmp/'management.c'),
                            '-Wl,--gc-sections', *objects, '-o', exe], cwd=ctx.root, check=True)
            media_cases = ('empty', 'existing') if mutation else ('empty', 'existing', 'swap')
            for media in media_cases:
                rc = subprocess.run([exe, media], cwd=tmp).returncode
                if (rc == 0) != (mutation is None):
                    return 1
            print('runtime RED ' + mutation[0] if mutation else 'install management PASS')
    return 0
