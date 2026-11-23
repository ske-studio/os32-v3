"""PC-98 boot probe, OHCI takeover, shared IRQ ownership and root exploration."""
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

SOURCE = 'platform/pc98/usb_resident.c'
PLATFORM = 'platform/pc98/usb_platform.c'
BODY = 'platform/pc98/usb_module.c'
HAL = ['in2_dev', 'in2_state', 'in2_ring', 'kbd_merge', 'pointer_state', 'mouse_ext',
       'legacy_keymap', 'keymap_registry', 'keymap_eval', 'keymap_state']
SUPPORT = ['input/keymaps/hid_jp.c', 'kernel/module/module_runtime.c', 'lib/kstring_c.c',
           *[f'input/hal/{name}.c' for name in HAL]]
HOST = 'tools/tests/pc98_usb_resident_host.c'
CHECK = dict(name='pc98-usb-resident-host', tier=1, needs=[], budget=16,
             covers=['input/keymaps/hid_jp.c', 'kernel/module/module_runtime.c', 'lib/kstring_c.c', 'input/hal/*.c', 'tools/tests/kernel_includes.py', 'tools/tests/host32.py', 'tools/tests/kbd_hostshim/io.h', 'lib/*.h', 'mm/*.h', 'fs/*.h', 'platform/pc98/usb_module.c', 'input/hal/*.h', 'platform/pc98/usb_resident.c', 'tools/tests/pc98_usb_resident_host.c', 'platform/pc98/usb_platform.c', 'platform/pc98/usb*.h',
                     'platform/pc98/tick_order.h', 'platform/pc98/platform_ver.h', 'kernel/ver_info.h', 'kernel/sqlite_engine.h', 'kernel/idle_work.h', 'kernel/irq/*.h', 'kernel/module/*.h',
                     'drivers/pci/*.h', 'drivers/usb/*.h', 'include/*.h', 'arch/x86/*.h',
                     'sdk/include/os32/*.h', 'tools/tests/test_pc98_usb_resident.py'],
             basis=['docs/decisions/usb/USB_DESIGN.md §4-1, §7-3'])
MUTANTS = [('if (active) core->service()', 'core->service()', 1),
           ('OHCI_COMMAND_STATUS, OHCI_CMD_OCR', 'OHCI_COMMAND_STATUS, 0', 2),
           ('dispatch, slot, IRQ_F_SHARED', 'dispatch, slot, 0', 2),
           ('? IRQ_HANDLED : IRQ_NONE', '? IRQ_NONE : IRQ_HANDLED', 3),
           ('? IRQ_HANDLED : IRQ_NONE', '? IRQ_HANDLED : IRQ_HANDLED', 10),
           ('& PCI_CMD_BUS_MASTER) &&', '& 0) &&', 11),
           ('core->dead(hc, USB_WHY_START);', '(void)hc;', 6)]


def run(ctx):
    root = Path(ctx.root)
    paths = (SOURCE, PLATFORM, BODY, 'input/hal/usb_input.c')
    originals = [(root / path).read_text() for path in paths]
    with tempfile.TemporaryDirectory() as tmp:
        sources = [Path(tmp) / Path(path).name for path in paths]
        exe = Path(tmp) / 'host'
        cases = [(mode, '', '', 0) for mode in (b'r', b'm', b'f', b'q', b't')]
        if ctx.mutate:
            cases.extend([
                (b'm', 'firmware[i] = hc.firmware;', 'firmware[i].valid = 0;', 28),
                (b'm', '{tick_reach, tick_deny, tick_runs, idle_runs}', '{tick_runs, tick_deny, tick_runs, idle_runs}', 47),
                (b'm', 'scan_pending = 1;', 'scan_pending = 0;', 42),
                (b'm', 'idle_runs++;\n        run_scan();', 'idle_runs++; irq_restore(flags); run_scan(); flags = irq_save();', 42),
                (b'm', 'hc->isr_pending |= pending;', 'hc->isr_pending |= pending & ~OHCI_INTR_WDH;', 43),
                (b't', 'kprintf(KLOG_ATTR, "[usb] tick register failed', 'if (0) kprintf(KLOG_ATTR, "[usb] tick register failed', 41),
                (b'm', 'if (!found) kprintf', 'if (0) kprintf', 37),
                (b'm', 'stage = "BAR map"', 'stage = "unknown"', 38),
                (b'r', 'if (!hc->has_irq)\n            kprintf', 'if (0)\n            kprintf', 40),
                (b'f', '    tick_target();',
                 '    tick_target(); if (!foreground_log) log_drain_one();', 39),
                (b'm', 'pc98_usb_service_log("[usb] first tick", NULL);', '(void)0;', 39),
            ])
            cases.extend((b'r', *mutation) for mutation in MUTANTS)
            cases.extend((b'm', *mutation) for mutation in [
                ('    tick_target();', '        (void)tick_target;', 34),
                ('tick_budget_cnt(IRQ0_SLOT_BUDGET_US), scan_reserve_cnt', 'tick_budget_cnt(IRQ0_SLOT_BUDGET_US), 0', 35),
                ('active < 0 ? active : 0', 'active', 22),
                ('found && !active ? -USB_E_HC : active', '(void)found, active', 27),
                ('input_src_release(s->src, s->generation);', '(void)s;', 29),
                ('isr_pending |= pending', 'isr_pending = pending', 23),
                ('return pending ?', 'return hc->isr_pending ?', 24),
                ('*s = (struct usb_source){0};', '*s = (struct usb_source){0}; i16 x, y; mouse_ext_read_legacy(&x, &y);', 36),
                ('e.buttons = buttons', 'e.buttons = 0 * buttons', 26)])
        for mode, old, new, expected in cases:
            if old:
                assert sum(source.count(old) for source in originals) == 1, old
            for src, original in zip(sources, originals):
                src.write_text(original.replace(old, new) if old else original)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-ffunction-sections', '-fdata-sections',
                            '-fno-pie', '-no-pie', '-nostdlib', '-static', '-Wl,--gc-sections',
                            '-Iinput/hal', '-Itools/tests/kbd_hostshim', '-I.', *include_flags(root),
                            *sources, *SUPPORT, HOST, '-o', exe], cwd=root, check=True)
            result = host32.run([str(exe)], input=mode).returncode
            assert result == expected, (old, result, expected)
            print('RED runtime' if old else 'PASS', mode.decode(), old, result)
    return 0
