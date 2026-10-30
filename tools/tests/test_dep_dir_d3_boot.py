"""D3c B/C: real static table/dispatcher/wrappers and kernel boundary order."""
from kernel_includes import include_flags
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('platform/pc98/boot.c', 'init/boot_platform.c', 'init/boot_display.c', 'platform/pc98/emergency.c')
HEADERS = ('include/ide_contract.h', 'init/boot_platform.h', 'init/boot_display.h', 'include/platform_emergency.h')


def kernel_order(kernel=None):
    if kernel is None:
        kernel = (ROOT / 'init/kernel.c').read_text()
    # Include the intervening operations: slot ordering alone is insufficient.
    markers = [
        'kstack_hw_init();', 'pc98_boot_bind(&boot, boot_drive);',
        'bootinfo_capture();', 'sysclk_detect();',
        'boot_platform_run(BOOT_DMA, &boot);', 'pit_init(PIT_HZ);',
        'cpu_calibrate();', 'boot_platform_run(BOOT_INPUT, &boot);',
        'boot_platform_run(BOOT_FLOPPY, &boot);',
        'boot_platform_run(BOOT_DEV_FS, &boot);', 'palette_init();',
        'boot_platform_run(BOOT_PCI_ENUM, &boot);',
        'boot_platform_run(BOOT_DISK, &boot);', 'bootinfo_report();',
        'bootinfo_report_image();', '"HEAP..."', 'kmalloc_init(', '"MOUNT..."',
        'rc = vfs_mount("/", boot.root_dev, boot.root_fs);', '"root OK"',
        'boot_platform_run(BOOT_AUTO_MOUNT, &boot);',
        'boot_platform_run(BOOT_CLOCK, &boot);', 'paging_init(mem_kb);',
        'paging_reclaim_conventional();', 'boot_platform_run(BOOT_FONT, &boot);',
        '"fninit"', 'memory_boot_init(mem_kb)', 'cd_module_load();',
        'kcg_module_load();', 'kernel_mount_children(boot.root_dev);',
        'services_stage = SERVICES_ROOT_READY;', 'unicode_file_load();',
        'boot_platform_run(BOOT_PCI_RESOURCES, &boot);', 'lan_module_load();',
        'lan_module_pci_bind_fallback();', 'pcm_module_load();',
        '"[BOOT] snd module rc=%d\\n"',
        'boot_platform_run(BOOT_NIC_LEGACY, &boot);', '"IRQ_EN"', 'services_boot();',
    ]
    positions = [kernel.index(m) for m in markers]
    assert positions == sorted(positions)
    for m in markers:
        assert kernel.count(m) == 1, m
    assert 'pc98_d2_bind' not in kernel
    assert not re.search(r'#include.*(?:kbd|mouse|fdc|rtc|kcg|pci|pci_bind|dma8237|ide|lgy98|e82557)\.h', kernel)
    print('PASS kernel boundaries and retained operations in original order')


def build(work, mutation=None):
    # Positive and mutation copies use the same headers and include paths.
    for rel in (*SOURCES, *HEADERS, 'init/kernel.c'):
        text = (ROOT / rel).read_text()
        if mutation and rel == mutation[0]:
            _, old, new = mutation
            assert text.count(old) == 1
            text = text.replace(old, new, 1)
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text)
    for rel, out in zip(SOURCES, ('pc98_boot_source.inc', 'boot_platform_source.inc', 'boot_display_source.inc', 'emergency_source.inc')):
        text = (work / rel).read_text()
        (work / out).write_text(re.sub(r'^#include[^\n]*\n', '', text, flags=re.M))
    kernel = (work / 'init/kernel.c').read_text()
    start = kernel.index('    {\n        int rc;', kernel.index('"MOUNT..."'))
    end = kernel.index('    boot_platform_run(BOOT_CLOCK, &boot);', start)
    child = re.search(r'static inline void kernel_mount_children\(const char \*root_dev\)\n\{.*?\n}\n', kernel, re.S)
    assert child
    (work / 'kernel_mount_source.inc').write_text(
        'static void host_root_mount(struct boot_context boot) {\n' +
        kernel[start:end] + '}\n' + child.group())
    exe = work / 'boot'
    cmd = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-D__cdecl=',
           '-I' + str(work), '-I' + str(work / 'include'), *include_flags(work)]
    cmd += include_flags(ROOT)
    subprocess.run([*cmd, str(ROOT / 'tools/tests/dep_dir_d3_boot_host.c'), '-o', str(exe)], check=True)
    return exe


def main():
    kernel_order()
    with tempfile.TemporaryDirectory(prefix='dep-dir-d3-boot-') as td:
        subprocess.run([str(build(Path(td)))], check=True)
    if '--mutate' in sys.argv:
        kernel = (ROOT / 'init/kernel.c').read_text()
        a, b = 'BOOT_PCI_ENUM, &boot', 'BOOT_DISK, &boot'
        assert kernel.count(a) == kernel.count(b) == 1
        changed = kernel.replace(a, 'SWAP_BOOT_POINT').replace(b, a).replace('SWAP_BOOT_POINT', b)
        try:
            kernel_order(changed)
        except AssertionError:
            print('RED PCI_ENUM/DISK boundary swap')
        else:
            raise AssertionError('boot boundary swap escaped order check')

        for mutation in (
            ('platform/pc98/boot.c',
             'drive == BOOT_DRIVE_FDD || drive == BOOT_DRIVE_FDD_144',
             '(drive & BIOS_DA_TYPE_MASK) == BOOT_DRIVE_FDD || (drive & BIOS_DA_TYPE_MASK) == BOOT_DRIVE_FDD_144'),
            ('init/kernel.c', 'if (match) continue;', 'if (match && 0) continue;'),
        ):
            with tempfile.TemporaryDirectory(prefix='dep-dir-d3-c-mut-') as td:
                rc = subprocess.run([str(build(Path(td), mutation))]).returncode
                assert rc != 0, mutation
                print('RED', mutation[1])


if __name__ == '__main__':
    main()
