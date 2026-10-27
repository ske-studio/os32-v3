"""D3b geometry/ABI: compile real runtime and adapter as ILP32; IDE is fake."""
from pathlib import Path
import subprocess
import tempfile
import sys
import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('kernel/bootinfo.c', 'kernel/bootinfo_check.c',
           'platform/pc98/boot_disk.c')
HEADERS = ('include/ide_contract.h', 'platform/pc98/boot_disk.h', 'kernel/boot_disk.h')


def build(tmp, mutation=None):
    # The same copied tree is used for positive and future mutation runs.
    for rel in (*SOURCES, *HEADERS):
        text = (ROOT / rel).read_text()
        if mutation and mutation[0] == rel:
            _, old, new = mutation
            assert old in text
            text = text.replace(old, new, 1)
        dest = tmp / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text.replace('"../../drivers/', '"'))
    src = tmp / 'fixture.c'
    src.write_text((ROOT / 'tools/tests/dep_dir_d3_disk_host.c').read_text()
                   .replace('"../../', '"'))
    exe = tmp / 'disk'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
           '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
           '-static', '-no-pie', '-ffunction-sections', '-Wl,--gc-sections',
           '-D__cdecl=', '-I' + str(tmp), '-I' + str(tmp / 'include'), '-I' + str(ROOT)]
    cmd += ['-I' + str(ROOT / p) for p in ('include', 'kernel', 'lib', 'drivers',
                                         'sdk/include/os32')]
    subprocess.run([*cmd, str(src), '-o', str(exe)], check=True)
    return exe


def main():
    kernel = (ROOT / 'kernel/kernel.c').read_text()
    assert kernel.index('pc98_boot_bind(&boot, boot_drive);') < kernel.index('bootinfo_report();')
    for rel in ('kernel/bootinfo.c', 'kernel/kernel.c'):
        text = (ROOT / rel).read_text()
        assert '#include "ide.h"' not in text
        assert 'boot_devices.h' not in text
    with tempfile.TemporaryDirectory(prefix='dep-dir-d3-disk-') as td:
        exe = build(Path(td))
        result = host32.run([str(exe)], check=True)
        assert result.returncode == 0
    if '--mutate' in sys.argv:
        mutation = ('platform/pc98/boot_disk.c',
                    '    return 0;\n}\n\nstatic int pc98_disk_report_ata',
                    '    return BOOTINFO_DA_HDD0 + ide_drive;\n}\n\nstatic int pc98_disk_report_ata')
        with tempfile.TemporaryDirectory(prefix='dep-dir-d3-disk-mut-') as td:
            exe = build(Path(td), mutation)
            result = host32.run([str(exe)])
            assert result.returncode > 0, 'bank1 BIOS DA mutation was not detected'
        print('RED bank1 BIOS DA mutation')
    print('PASS D3b ILP32 geometry matrix, all HddGeom fields/offsets, report order')


if __name__ == '__main__':
    main()
