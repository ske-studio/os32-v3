"""D3c B/C: real static table/dispatcher/wrappers and kernel boundary order."""
from kernel_boot_source import kernel_parts, kernel_source
from kernel_includes import include_flags
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('platform/pc98/boot.c', 'init/boot_platform.c', 'init/boot_display.c', 'platform/pc98/emergency.c')
HEADERS = ('include/ide_contract.h', 'init/boot_platform.h', 'init/boot_display.h', 'include/platform_emergency.h')


def kernel_order(kernel=None, overrides=None):
    if kernel is None:
        kernel = kernel_source(ROOT, overrides)
    else:
        kernel = kernel_source(ROOT, {'init/kernel.c': kernel})
    lan = re.search(r'^static void kernel_boot_lan\(struct boot_context \*boot\)\n\{\n(.*?)^}\n',
                    kernel, re.M | re.S)
    assert lan, 'shared LAN boot helper'
    lan_markers = ['boot_platform_run(BOOT_PCI_RESOURCES, boot);',
                   'lan_module_load();', 'lan_module_pci_bind_fallback();',
                   'kernel_boot_hostsvc(boot);']
    positions = [lan[1].index(m) for m in lan_markers]
    assert positions == sorted(positions), 'LAN helper order'
    for m in lan_markers:
        assert kernel.count(m) == 1, m
    assert re.search(r'if \(memory_boot_dma_available\(\)\)\s*\{\s*lan_module_load\(\);\s*\}', lan[1])
    kernel = kernel[:lan.start()] + kernel[lan.end():]
    hostsvc = re.search(r'^static void kernel_boot_hostsvc\(struct boot_context \*boot\)\n\{\n(.*?)^}\n', kernel, re.M | re.S)
    assert hostsvc, 'configured host services boot helper'
    assert hostsvc[1].index('boot_platform_run(BOOT_NIC_LEGACY, boot);') < hostsvc[1].index('net_start(') < hostsvc[1].index('sfs_persistent_start(')
    assert 'boot_legacy_started = 1;' in hostsvc[1]
    kernel = kernel[:hostsvc.start()] + kernel[hostsvc.end():]
    assert re.search(r'if \(!boot_legacy_started\)\s*\{\s*boot_platform_run\(BOOT_NIC_LEGACY, &boot\);', kernel)
    early = re.search(r'    if \(netcd_boot\) \{\n(.*?)^    }\n', kernel, re.M | re.S)
    assert early, 'NETCD boot branch'
    assert early[1].index('kernel_boot_lan(&boot);') < early[1].index('lan_module_netcd_register(netcd_alias)')
    assert kernel.index('kdebug.ops.selftest_run();') < early.start() < early.end() < kernel.index('cd_module_load();')
    assert re.search(r'if \(!netcd_boot\)\s*\{\s*kernel_boot_lan\(&boot\);\s*\}', kernel)
    assert kernel.count('kernel_boot_lan(&boot);') == 2
    assert kernel.count('lan_module_netcd_register(netcd_alias)') == 1
    # Check the retained path after checking the early NETCD path separately.
    kernel = kernel[:early.start()] + kernel[early.end():]
    # Include the intervening operations: slot ordering alone is insufficient.
    markers = [
        'kstack_hw_init();', 'pc98_boot_bind(&boot, boot_drive);',
        'bootinfo_capture();', 'v86_bios_save_real();',
        'mem_kb = memory_boot_detect(mem_kb);', 'platform_boot_clock_detect();',
        'boot_platform_run(BOOT_DMA, &boot);', 'pit_init(PIT_HZ);',
        'platform_boot_irq_start();',
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
        'arch_boot_after_paging()', 'memory_boot_init(mem_kb)', 'shm_init();',
        'paging_boot_user_shared(exec_tramp_page_addr())', 'dma_pool_init();',
        'v86_module_load();', 'kdebug_load();', 'kdebug.ops.selftest_run();',
        'cd_module_load();',
        'kcg_module_load();', 'kernel_mount_children(boot.root_dev);',
        'services_stage = SERVICES_ROOT_READY;', 'unicode_file_load();',
        'kernel_boot_lan(&boot);', 'pcm_module_load();',
        'fd_redirect_init();', 'pipe_buffer_init();',
        '(void)sysconfig_get_str(SYS_SYSTEM_CFG, "GFX", gfmode, sizeof(gfmode));',
        'gfx_boot_reserve();',
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
    for rel in (*SOURCES, *HEADERS, 'init/kernel.c', *kernel_parts(ROOT)):
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
    kernel = (ROOT / 'init/kernel.c').read_text()
    # Definitions can move without changing execution; follow the call sites.
    includes = [f'#include "{Path(p).name}"' for p in kernel_parts(ROOT)]
    assert len(includes) == 3
    reordered = kernel
    for i, include in enumerate(includes):
        assert reordered.count(include) == 1
        reordered = reordered.replace(include, f'BOOT_INCLUDE_{i}')
    for i, include in enumerate(reversed(includes)):
        reordered = reordered.replace(f'BOOT_INCLUDE_{i}', include)
    kernel_order(reordered)
    with tempfile.TemporaryDirectory(prefix='dep-dir-d3-boot-') as td:
        subprocess.run([str(build(Path(td)))], check=True)
    if '--mutate' in sys.argv:
        memory_path = 'init/kernel_boot_memory.inc'
        memory = (ROOT / memory_path).read_text()
        early_order = ('        kernel_boot_lan(boot);\n'
                       '        int rc = lan_module_netcd_register(netcd_alias);')
        assert memory.count(early_order) == 1
        changed = memory.replace(early_order,
                                 '        int rc = lan_module_netcd_register(netcd_alias);\n'
                                 '        kernel_boot_lan(boot);')
        try:
            kernel_order(overrides={memory_path: changed})
        except AssertionError:
            print('RED NETCD LAN/register boundary swap')
        else:
            raise AssertionError('NETCD LAN/register swap escaped order check')
        for label, a, b in (
            ('NETCD/CD', '    if (netcd_boot) {', '    cd_module_load();'),
            ('Unicode/LAN', '    unicode_file_load();', '    if (!netcd_boot) {'),
            ('LAN/PCM', '    if (!netcd_boot) {\n        kernel_boot_lan(boot);\n    }',
             '    if (memory_boot_dma_available()) {\n        pcm_module_load();\n    }'),
            ('PCI/LAN', '    boot_platform_run(BOOT_PCI_RESOURCES, boot);', '        lan_module_load();'),
            ('LAN/fallback', '        lan_module_load();', '    lan_module_pci_bind_fallback();'),
        ):
            assert memory.count(a) == memory.count(b) == 1, label
            changed = memory.replace(a, 'SWAP_BOOT_POINT').replace(b, a).replace('SWAP_BOOT_POINT', b)
            try:
                kernel_order(overrides={memory_path: changed})
            except (AssertionError, ValueError):
                print('RED', label, 'LAN boundary swap')
            else:
                raise AssertionError(label + ': LAN boundary swap escaped order check')

        for label, a, b in (
            ('PCI_ENUM/DISK', 'BOOT_PCI_ENUM, &boot', 'BOOT_DISK, &boot'),
            ('CPU/input', 'kernel_boot_cpu(mem_kb, &boot);',
             'boot_platform_run(BOOT_INPUT, &boot);'),
            ('memory/root-clock', 'kernel_boot_memory(mem_kb, &boot);',
             'boot_platform_run(BOOT_CLOCK, &boot);'),
            ('GFX config/reservation', 'kernel_gfx_config();', 'gfx_boot_reserve();'),
        ):
            assert kernel.count(a) == kernel.count(b) == 1
            changed = kernel.replace(a, 'SWAP_BOOT_POINT').replace(b, a).replace('SWAP_BOOT_POINT', b)
            try:
                kernel_order(changed)
            except AssertionError:
                print('RED', label, 'boundary swap')
            else:
                raise AssertionError(label + ': boot boundary swap escaped order check')

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
