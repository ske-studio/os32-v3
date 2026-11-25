"""期待の出所: PPC_RESET_DESIGN §3・§7、PPC_RESET_DESIGN 確定後の追記。
NetBSD sys/arch/macppc/dev/cuda.c:325–361,559–735,887–899,
pmu.c:475–545,620–624,775–784; OpenBSD pm_direct.c:294–322。
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import host32

CHECK = dict(name='powermac-reset', tier=1, needs=[], budget=40, env=['ppc-cross'], covers=[
    'sys/dev/macintosh/via.c', 'sys/dev/macintosh/via.h',
    'sys/include/types.h', 'sys/include/callconv.h',
    'tools/tests/test_powermac_reset.py', 'tools/tests/via_host.c',
    'tools/tests/host32.py', 'tools/ppc/kboot_capture.py',
    'tools/ppc/kboot_capture_fake.c', 'sys/platform/powermac/via_tree.c',
    'sys/platform/powermac/via_window.h', 'sys/platform/powermac/kboot_capture.c',
    'sys/platform/powermac/gmac_tree.c', 'sys/platform/powermac/kboot_info.h',
    'platform/powermac/*.h', 'arch/ppc/*.h', 'sys/include/memmap.h',
    'sys/kern/kstring.h', 'sys/kern/kstring_c.c', 'sys/kern/kprintf.c', 'lib/divdi3.c',
    'sys/arch/ppc/pd_walk.c', 'sys/platform/powermac/kernel_boot.c',
    'sys/platform/powermac/pci_config.c', 'sys/platform/powermac/pci_backend.c',
    'sys/platform/powermac/lan_gem_io.c', 'sys/platform/powermac/pci_hosts.inc',
    'sys/platform/powermac/usb_boot.c', 'sys/platform/powermac/usb_platform.c', 'sys/platform/powermac/usb_tree.c',
    'sys/platform/powermac/input_keymap_backend.c', 'sys/hal/input/keymap.h', 'sys/hal/input/usb_input.h',
    'tools/ppc/usb_via_overlap_host.c', 'sys/dev/usb/usb_core.h',
    'sys/dev/usb/ohci_regs.h', 'sys/include/endian_dma.h', 'sys/include/usb_platform.h',
    'sys/include/kprintf.h', 'sys/kern/mm/paging.h', 'sys/kern/mm/kmalloc.h', 'sys/kern/irq/tick.h',
], basis=['docs/decisions/ppc/PPC_RESET_DESIGN.md §2・§3・§7',
          'PPC_RESET_DESIGN 確定後の追記', 'docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md §5 clock'])

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / 'drivers/macintosh'
CASES = ('Cuda reset/wrap', 'PMU reset', 'Cuda pending', 'Cuda collision',
         'Cuda reply cap', 'Cuda stalled', 'PMU busy timeout', 'PMU idle timeout',
         'PMU free timeout', 'Cuda frame limit', 'Cuda overall deadline',
         'Cuda restart with unconsumed reply', 'Cuda fake start reply',
         'Cuda receive predicate', 'Cuda RTC', 'PMU RTC', 'Cuda RTC invalid header')
MUTATIONS = (
    ('cuda-pseudo', 'CUDA_PSEUDO = 0x01', 'CUDA_PSEUDO = 0x02', 15),
    ('cuda-reset', 'CUDA_RESET = 0x11', 'CUDA_RESET = 0x12', 15),
    ('pmu-reset', 'PMU_RESET_CPU = 0xd0', 'PMU_RESET_CPU = 0xd1', 25),
    ('cuda-rtc-opcode', 'CUDA_GET_TIME = 0x03', 'CUDA_GET_TIME = 0x38', 152),
    ('pmu-rtc-opcode', 'PMU_READ_RTC = 0x38', 'PMU_READ_RTC = 0x03', 163),
    ('cuda-rtc-status', 'reply[1] != 0', 'false', 172),
    ('tip-before-sr', 'write8(v, SR, out[0]);\n            update(v, PORT_B, TIP, PB4);',
     'update(v, PORT_B, TIP, PB4);\n            write8(v, SR, out[0]);', 13),
    ('missing-ack', 'write8(v, SR, out[i]);\n                cuda_toggle_ack(v);',
     'write8(v, SR, out[i]);\n                (void)0;', 12),
    ('pb3-polarity', '(read8(v, PORT_B) & PB3) == 0', '(read8(v, PORT_B) & PB3) != 0', 12),
    ('stale-ifr', 'if (!wait_bit(v, start, IFR, SR_DONE, true))\n                    break;',
     'if (false)\n                    break;', 13),
    ('fake-start', 'return 1; /* Fake start: no frame was received. */', 'return 0;', 134),
    ('null-reply', 'rc || !in || !cap', 'rc || !cap', 142),
    ('zero-cap', 'rc || !in || !cap', 'rc || !in', 143),
    ('prepare-delay', 'if (!delay(v, start, CUDA_SETTLE))\n        goto timeout;\n    update(v, PORT_B, TIP, 0);',
     'if (false)\n        goto timeout;\n    update(v, PORT_B, TIP, 0);', 11),
    ('collision-drop', 'rc = 1; /* Collision, go back to input before receiving. */',
     'rc = 0;', 44),
    ('edge-deadline', '|| current - begin >= ticks',
     '|| (ticks == 0 && current - begin >= ticks)', 67),
    ('overall-deadline', 'current - start >= v->io.tb_hz',
     'current - start >= 2ULL * v->io.tb_hz', 117),
)


def of_cases(folder, mutate):
    sys.path.insert(0, str(ROOT / 'tools/ppc'))
    from kboot_capture import exercise
    product = ROOT / 'platform/powermac'
    capture = product / 'kboot_capture.c'
    source = (product / 'via_tree.c').read_text()
    cases = range(26, 42)
    assert not exercise(folder, capture, cases=cases)
    print('GREEN: VIA OF PMU/Cuda, duplicate, missing, malformed reg, range, high cell, exact name, BAR tokens, child/peer failure after candidate')
    if mutate:
        mutations = (
            ('via-name-prefix', '(n == len || (n == len + 1 && !buf[len]))',
             '(n >= len)', 32),
            ('via-first-candidate', 'if (*found && *found != node) return PM_VIA_AMBIGUOUS;',
             'if (*found && *found != node) return 0;', 28),
            ('via-extra-reg-cell', '!= (ac + sc) * PM_CELL_BYTES)',
             '< (ac + sc) * PM_CELL_BYTES)', 29),
            ('via-range-length', '!contains(child, size, *a, bytes)',
             '!contains(child, size, *a, 1)', 30),
            ('via-wrong-bar-token', 'if (token && token != (hi & PCI_BAR_TOKEN)) return 0;',
             '', 40),
            ('via-child-peer-error', 'if (node == -1) return PM_VIA_BAD_PROPERTY;',
             'if (node == -1) return 0;', 34),
            ('via-address-high-cell', 'cells >= 2 && be32(p + (cells - 2) * PM_CELL_BYTES)',
             'cells >= 2 && be32(p + (cells - 2) * PM_CELL_BYTES) == ~0U', 31),
        )
        mutant = folder / 'via_tree.c'
        for name, old, new, case in mutations:
            assert source.count(old) == 1, f'{name}: mutation point changed'
            mutant.write_text(source.replace(old, new))
            exercise(folder, capture, (case, 44, b'reached=A1\nH:A1 via\n'),
                     cases=[case], via_source=mutant)
            print(f'RED {name} (OF case {case}, rc=44)')



USB_CASES = ('VIA first page', 'VIA last page', 'page before VIA', 'page after VIA',
             'unpublished VIA', 'config write cleanup', 'second root config address',
             'second root config data', 'HW_OP §5 6b saved USB root')
USB_MUTATIONS = (
    ('usb-swapped-OF-host', 'usb_tree.c', '.host_id = node',
     '.host_id = node == 2 ? 3 : 2', 8, 106),
    ('usb-ignore-root', 'usb_platform.c',
     'maps[slot].root == f->root && maps[slot].pa == pa', '1', 8, 106),
    ('usb-bus-instead-of-pa', 'usb_platform.c',
     'pm_via_phys_overlap(pa, PM_USB_PAGE)', 'pm_via_phys_overlap(bus, PM_USB_PAGE)', 0, 31),
    ('usb-miss-last-via-page', 'kernel_boot.c',
     'overlap(pa, bytes, via_alias.pa, via_alias.bytes)',
     'overlap(pa, bytes, via_alias.pa, via_alias.bytes - PAGE_SIZE)', 1, 31),
    ('usb-include-adjacent-page', 'kernel_boot.c',
     'a <= b ? b - a < n : a - b < m', 'a <= b ? b - a <= n : a - b <= m', 2, 33),
    ('via-ignore-later-usb-root', 'kernel_boot.c',
     'i < pm_usb_root_count; i++', 'i < pm_usb_root_count && i == 0; i++', 7, 32),
)


def usb_cases(folder, mutate):
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    cc = cross / 'bin/powerpc-elf-gcc'
    runner = shutil.which('qemu-ppc')
    if not cc.is_file() or not runner:
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP') + ': USB/VIA host cases need PPC GCC and qemu-ppc')
        return int(required)
    product = ROOT / 'platform/powermac'
    exe = folder / 'usb_via_overlap_host'

    def build(usb_platform):
        includes = ('.', 'include', 'arch/ppc', 'platform/powermac', 'sdk/include',
                    'sdk/include/os32', 'kernel', 'init', 'kernel/console',
                    'kernel/irq', 'kernel/time', 'mm', 'lib')
        sources = ('tools/ppc/usb_via_overlap_host.c', 'sys/platform/powermac/usb_boot.c',
                   'sys/platform/powermac/input_keymap_backend.c', 'sys/arch/ppc/pd_walk.c',
                   'sys/platform/powermac/pci_config.c', 'sys/platform/powermac/pci_backend.c',
                   'sys/platform/powermac/lan_gem_io.c',
                   'sys/kern/kstring_c.c', 'sys/kern/kprintf.c', 'lib/divdi3.c')
        subprocess.run(
            [str(cc), '-mcpu=7450', '-msdata=none', '-msoft-float', '-mno-altivec',
             '-std=gnu11', '-ffreestanding', '-fno-builtin', '-fno-pie',
             '-fno-stack-protector', '-nostdlib', '-static', '-O2', '-Wall', '-Wextra',
             '-Werror', '-ffunction-sections', '-fdata-sections', '-D__cdecl=',
             '-DPM_PCI_FAKE_LANE', '-DPM_PCI_FAKE_MMIO', '-D__KERNEL_BUILD__', '-DKBOOT_SH=1', '-DKBOOT_LINK=1', '-I' + str(folder),
             *['-I' + str(ROOT / p) for p in includes], str(usb_platform),
             *[str(ROOT / p) for p in sources],
             '-Wl,--gc-sections,--wrap=usbp_mmio_map,--wrap=usbp_cfg_read32', '-o', str(exe)],
            check=True, timeout=60, cwd=ROOT)

    def observe_usb(case):
        return subprocess.run([runner, str(exe), str(case)], timeout=10).returncode

    build(product / 'usb_platform.c')
    for case, description in enumerate(USB_CASES):
        rc = observe_usb(case)
        assert rc == 0, f'{description}: USB/VIA control failed (rc={rc})'
    print('GREEN USB/VIA ' + ', '.join(USB_CASES))
    if mutate:
        for name, file, old, new, case, expected in USB_MUTATIONS:
            source = (product / file).read_text()
            assert source.count(old) == 1, f'{name}: mutation point changed'
            mutant = folder / file
            mutant.write_text(source.replace(old, new))
            build(mutant if file == 'usb_platform.c' else product / 'usb_platform.c')
            mutant.unlink()
            rc = observe_usb(case)
            assert rc == expected, f'{name}: rc={rc}, expected runtime RED {expected}'
            print(f'RED {name} ({USB_CASES[case]}, rc={rc})')
    return 0


def observe(folder, source):
    (folder / 'via.c').write_text(source)
    exe = folder / 'via_host'
    builtin_headers = subprocess.check_output(['gcc', '-print-file-name=include'], text=True).strip()
    host32.build(['gcc', '-nostdinc', '-isystem', builtin_headers, '-m32', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                  '-ffreestanding', '-fno-pie', '-no-pie', '-fno-stack-protector',
                  '-nostdlib', '-static', '-I' + str(folder), '-I' + str(DRIVER),
                  '-I' + str(ROOT / 'include'), str(ROOT / 'tools/tests/via_host.c'),
                  '-o', str(exe)], check=True)
    return host32.run([str(exe)], timeout=5).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (DRIVER / 'via.c').read_text()
    with tempfile.TemporaryDirectory(prefix='via-host-') as temp:
        folder = Path(temp)
        rc = observe(folder, source)
        assert rc == 0, f'VIA control failed (rc={rc})'
        print('GREEN ' + ', '.join(CASES))
        if args.mutate:
            for name, old, new, expected in MUTATIONS:
                assert source.count(old) == 1, f'{name}: mutation point changed'
                rc = observe(folder, source.replace(old, new))
                assert rc == expected, f'{name}: rc={rc}, expected runtime RED {expected}'
                print(f'RED {name} ({CASES[rc // 10 - 1]}, rc={rc})')
        of_cases(folder, args.mutate)
        return usb_cases(folder, args.mutate)


def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    sys.exit(main())
