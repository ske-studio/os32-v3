"""Exercise the real reservation and formatter with a disposable RAM disk."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='ppc-install-host', tier=1, needs=[], budget=35,
             covers=['tools/tests/test_ppc_install.py', 'tools/tests/ppc_install_host.c',
                     'tools/tests/host32.py', 'tools/tests/cc_cache.py', 'platform/powermac/install*',
                     'platform/powermac/partition_view.c', 'platform/powermac/apm.h',
                     'platform/powermac/platform_install_layout.h', 'userland/system/inst_*.h',
                     'userland/shell/hdprep_plan.h',
                     'platform/powermac/platform_ext2.h', 'platform/powermac/platform_install_management.h',
                     'kernel/dev/**', 'fs/ext2/**', 'include/**', 'lib/kstring*', 'lib/crc32*',
                     'drivers/ata/ide.h', 'drivers/block/disk.h', 'drivers/block/fdc.h', 'drivers/base/dev.c', 'mm/kmalloc.h', 'sdk/include/**', 'fs/*.h'],
             basis=['docs/decisions/ppc/PPC_ROOT_MOUNT_ALIGN_X86_DESIGN.md §5–6'])


def run(ctx):
    import host32
    sources = ['platform/powermac/install_backend.c', 'platform/powermac/partition_view.c',
               'kernel/dev/install_management.c', 'fs/ext2/ext2_fmt.c', 'fs/ext2/ext2_layout.c',
               'tools/tests/ppc_install_host.c', 'lib/kstring_c.c', 'lib/crc32.c', 'drivers/base/dev.c',
               'fs/ext2/ext2_super.c', 'fs/ext2/ext2_file.c', 'fs/ext2/ext2_inode.c',
               'fs/ext2/ext2_dir.c', 'platform/powermac/install_target.c']
    cases = [('normal', None, None, 0)]
    if ctx.mutate:
        cases += [('raw', '!raw_write || !m || !m->requires_raw_reservation', '1', 28),
                  ('view-auth', 'op == INSTALL_VIEW_READ || exec_disk_write_allowed()', '1', 49),
                  ('unchecked-unmount', ' && verified_generation[unit] == pin.generation', '', 51),
                  ('early-boot', '!PLATFORM_INSTALL_STAGED_BOOT || boot', '1', 48),
                  ('hfs', 'p->boot_hfs.sectors != PM_HFS_SECTORS', '0', 29),
                  ('preserved', 'i < tables[p->raw_unit].count', 'i+1 < tables[p->raw_unit].count', 26),
                  ('nonempty', 'if (block[j*512+k])', 'if (0)', 2),
                  ('deploy-length', 'sizes[0]/512 != p->boot_hfs.sectors', '0', 40),
                  ('deploy-readback', '!same(check, sector, sizeof(check))', '0', 42),
                  ('path-probe', 'if (!rc && api->ide_write_sectors(g.drive, 0, 0, 0))', 'if (0)', 39),
                  ('overlap', 'start < seen[j].start + seen[j].sectors', '0', 33),
                  ('hfs-end', 'count <= length - (lba - start)', 'count <= length - (lba - start) + 1', 36),
                  ('retry', 'if (retry && ((lba', 'if (0 && ((lba', 35),
                  ('reservation', 'if (platform_install_medium() && (unsigned)ide_drive < VFS_DEV_UNIT_MAX) {\n        struct install_access',
                   'if (0) {\n        struct install_access', 9)]
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        shims = {'io.h': 'static inline unsigned irq_save(void){return 0;}\nstatic inline void irq_restore(unsigned f){(void)f;}',
                 'exec.h': 'int exec_disk_write_allowed(void);',
                 'fd_redirect.h': 'int res_owner_get(void);',
                 'vfs.h': '#define VFS_DEV_UNIT_MAX 4\n#define VFS_MAX_FS 8\nint vfs_install_precheck(u32); int vfs_install_detach(u32); void vfs_install_discard(u32);'}
        for name, body in shims.items():
            (tmp/name).write_text(body+'\n')
        for name, old, new, expected in cases:
            files = sources.copy()
            if old:
                index = {'unchecked-unmount': 2, 'reservation': 3, 'raw': 2, 'early-boot': 2, 'view-auth': 2, 'hfs-end': 2, 'overlap': -1, 'path-probe': -1,
                         'deploy-length': -1, 'deploy-readback': -1}.get(name, 0)
                src = ctx.root/files[index]
                body = src.read_text()
                assert body.count(old) == 1
                files[index] = tmp/src.name
                files[index].write_text(body.replace(old,new))
            exe = tmp/'host'
            subprocess.run(['cc', '-O2', '-std=gnu11', '-m32', '-nostdinc', '-nostdlib', '-static',
                            '-ffreestanding', '-fno-pie', '-fno-builtin', '-ffunction-sections',
                            '-fdata-sections', '-Wl,--gc-sections', '-D__cdecl=', '-DOS32_FEATURE_IDE=1',
                            '-I'+directory, '-I.', '-Iinclude', '-Iplatform/powermac', '-Ikernel/dev',
                            '-Isdk/include/os32', '-Ifs/ext2', '-Ifs', '-Imm', '-Ilib',
                            *files, '-o', exe], cwd=ctx.root, check=True)
            rc = host32.run([exe]).returncode
            if rc != expected:
                raise RuntimeError(f'{name}: exit {rc}, expected {expected}')
            print(name, 'RED' if old else 'OK', rc)
    return 0
