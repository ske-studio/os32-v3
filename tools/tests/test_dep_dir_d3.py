"""D3a A/F: execute real VFS/name table; check the temporary boot connection."""
from pathlib import Path
import subprocess
import tempfile
from test_vfs_mount_dev import FLAGS, INCLUDES

ROOT = Path(__file__).resolve().parents[2]


def main():
    kernel = (ROOT / 'init/kernel.c').read_text()
    assert kernel.index('    pc98_boot_bind(&boot, boot_drive);') < kernel.index('vfs_mount("/",')
    vfs = (ROOT / 'fs/vfs.c').read_text()
    assert 'ide_limits.h' not in vfs and 'IDE_MAX_DRIVES' not in vfs
    assert 'dev_find(' not in vfs
    assert vfs.count('void vfs_device_names_register(') == 1
    assert 'input_owner_exit' not in (ROOT / 'include/input_service.h').read_text()
    assert 'input_owner_exit' not in (ROOT / 'kernel/input_service.c').read_text()
    formatter = (ROOT / 'kernel/ver_format.c').read_text()
    assert 'drivers/input/kbd.h' not in formatter and 'KBD_IRQ' not in formatter
    with tempfile.TemporaryDirectory(prefix='dep-dir-d3-') as td:
        exe = Path(td) / 'names'
        subprocess.run(['gcc', *FLAGS, *INCLUDES,
                        str(ROOT / 'tools/tests/dep_dir_d3_host.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print('PASS D3a direction and pre-mount registration')


if __name__ == '__main__':
    main()
