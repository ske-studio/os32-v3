"""P3 calc versus base source, executed rows versus C plan, real service faults."""
CHECK = dict(name='gf-display-host', tier=1,
    covers=['kdebug/gfinit_decode.c', 'kdebug/gfinit_decode.h', 'kdebug/gfinit_decode/*.inc', 'kdebug/gfinit_op/*.inc', 'kdebug/gfinit/*.inc', 'kdebug/gfinit_plan_c/*.inc', 'kdebug/gfinit_pll/*.inc', 'kdebug/gfinit_resources/*.inc', 'kdebug/recover.c', 'kdebug/recover_common/*.inc', 'kdebug/recover_plan/*.inc', 'kdebug/recover_gf_stage/*.inc', 'kdebug/recover.h', 'kdebug/recover_gf.c', 'kdebug/recover_gf_expand.h', 'kdebug/recover_gf.h', 'tools/tests/gfinit_host.c', 'tools/tests/gfinit_fixture.h', 'tools/tests/test_gfinit.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py', 'include/types.h', 'tools/tests/recover_gf_host.c', 'tools/tests/recover_gf_inventory.h', 'kdebug/gfinit_pll.c', 'kdebug/gfinit_pll.h', 'include/memmap.h', 'kdebug/gfinit_exec.c', 'kdebug/gfinit_exec.h', 'tools/tests/gfinit_exec_host.c', 'tools/tests/test_gfinit_exec.py', 'tools/tests/gfinit_pll_host.c', 'tools/tests/test_gfinit_pll.py', 'kdebug/gfinit_bus.c', 'kdebug/gfinit_bus.h', 'kdebug/kdebug_window.c', 'kdebug/kdebug_window.h', 'tools/tests/gfinit_bus_host.c', 'tools/tests/test_gfinit_bus.py', 'tools/tests/recover_hostshim/io.h', 'kdebug/gfinit_resources.c', 'kdebug/gfinit_resources.h', 'drivers/pci/pci.h', 'drivers/pci/pci_decode.h', 'sdk/include/os32/os32_kapi_shared.h', 'include/kprintf.h', 'tools/tests/gfinit_resources_ra266.h', 'hwdev/owner.h', 'kdebug/gfinit.c', 'kdebug/gfinit.h', 'kdebug/gfinit_plan.h', 'kdebug/kpeek.h', 'kdebug/kpeek_allow.h', 'mm/pgalloc.h', 'mm/paging.h', 'mm/physmem.h', 'lib/kstring.h', 'arch/x86/kernel/idt.h', 'kernel/module/module.h', 'drivers/char/serial.h', 'include/bootinfo.h', 'include/sys.h', 'kdebug/generated/kdebug_import_macros.h', 'kdebug/generated/kdebug_imports.h', 'tools/tests/gfinit_owner_host.c', 'tools/tests/test_gfinit_owner.py', 'include/hwdev_private.h', 'kdebug/gfinit_wire.h', 'userland/shell/cmd_gfinit.c', 'userland/shell/shell.h', 'userland/shell/cmd_sys.c', 'build/programs.mk', 'tools/tests/gfinit_output_host.h', 'lib/kprintf.c', 'lib/kprintf_attr.c', 'include/console_attr.h', 'platform/pc98/console_attr.c', 'kernel/console/console.c', 'kernel/module/kdebug_call.c', 'hwdev/hwdev_imports.def', 'kdebug/gfinit_plan.c', 'tools/tests/gfinit_plan_host.c', 'tools/tests/test_gfinit_plan.py', 'kdebug/recover_profile.c', 'tools/run_host_group.py', 'tools/tests/gfinit_romfile_host.c', 'tools/gfinit_rom_decode.py', 'include/port_service.h', 'include/console_output.h', 'kernel/port_service.c', 'kdebug/recover_cleanup.h', 'kdebug/recover_diag.h', 'include/pci_dev_contract.h', 'kernel/console/console_backend.h', 'platform/pc98/console.c', 'kdebug/gfinit_mode.c', 'kdebug/gfinit_plan_c.c', 'kdebug/gfinit_plan_c.h', 'kdebug/recover_gf_c.h', 'hwdev/gfinit_mode.h', 'tools/tests/test_gfinit_plan_c.py', 'tools/tests/gfinit_plan_c_host.c', 'tools/tests/test_gfinit_mode.py', 'tools/tests/gfinit_mode_host.c', 'tools/tests/test_gfinit_budget.py', 'tools/tests/gfinit_budget_host.c', 'tools/rshell_recover.py', 'tools/lan_bridge.py', 'tools/lanfs_host.py', 'tools/rshell_peek.py', 'tools/rshell_serial.py', 'tools/serialfs_host.py', 'kdebug/gf_stage_state.c', 'kdebug/gf_stage_state.h', 'hwdev/gf_load_state.c', 'hwdev/gf_load_state.h', 'tools/tests/test_gf_runner.py', 'tools/tests/gf_runner_host.c', 'hwdev/gf/internal.h', 'hwdev/gf/runner.c', 'hwdev/gf/save.c', 'hwdev/gf/pll.c', 'hwdev/gf/op/*.c', 'include/video_service.h', 'hwdev/gf/scripts.h', 'hwdev/gf/display/calc.h', 'hwdev/gf/display/calc.c', 'hwdev/gf/display/timing.c', 'hwdev/gf/display/format.c', 'hwdev/gf/display/pitch.c', 'hwdev/gf/display/start.c', 'hwdev/gf/display/dac_format.c', 'hwdev/gf/display/output.c', 'hwdev/gf/display/cursor.c', 'hwdev/gf/display/fifo.c', 'hwdev/gf/display/extended.c', 'hwdev/gf/display/pll_program.c', 'hwdev/gf/display/state.h', 'hwdev/gf/quiesce_rows.h', 'hwdev/gf/display/*.c', 'hwdev/gf/display/*.h', 'hwdev/gf/service.c', 'tools/tests/gf_display_host.c', 'tools/tests/gf_display_palette_host.c', 'tools/tests/gf_display_blank_host.c', 'tools/tests/gf_display_modes_host.c', 'include/gf_production.h', 'tools/tests/gfinit_c_only_host.c', 'tools/tests/gfinit_init_host.c', 'tools/tests/gfinit_s0_host.c', 'tools/tests/gfinit_stages_host.c', 'tools/tests/test_gfinit_stages.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=60.0)

from pathlib import Path
import subprocess
import tempfile
import host32
import test_gfinit_plan_c

ROOT = Path(__file__).resolve().parents[2]
BASE = '13bc5619'


def previous_calc():
    def source(name):
        return subprocess.check_output(['git', 'show', f'{BASE}:{name}'], cwd=ROOT, text=True)
    text = source('kdebug/gfinit_mode.c').split('void gfinit_mode_palette')[0]
    for name in ('output', 'pll_program'):
        text = text.replace(f'#include "gfinit_mode/{name}.inc"', source(f'kdebug/gfinit_mode/{name}.inc'))
    return text.replace('../hwdev/gfinit_mode.h', str(ROOT / 'hwdev/gfinit_mode.h')).replace(
        'gfinit_mode_calc', 'gf_previous_calc').replace('mode_bit', 'previous_mode_bit').replace(
        'mode_dcb_head_a_or0', 'previous_mode_dcb_head_a_or0')


def check_rows(text):
    rows, actual, delays, resets = {}, [], {}, []
    colors = bytes.fromhex('000000 ff0000 00ff00 0000ff ffffff 00ffff ff00ff ffff00') + bytes(744)
    groups = 0
    collecting = False
    for line in text.splitlines():
        if line.startswith('variant='):
            rows = {}
        elif line.startswith('row=') and not collecting:
            ordinal, kind, reg, index, width, value, count, bound = map(int, line[4:].split())
            assert bound == 1
            if kind != 10:  # Upper layer route belongs to P5.
                rows[ordinal] = kind, reg, index, width, value, count
        elif line.startswith('adapter variant='):
            collecting = True
            actual, delays, resets = [], {}, []
        elif line.startswith('bus='):
            ordinal, write, reg, width, value = map(int, line[4:].split())
            if write:
                actual.append((ordinal, reg, width, value))
            else:
                assert reg not in (0x6013c0, 0x6813c9)
                if reg == 0x6013da:
                    resets.append(ordinal)
        elif line.startswith('delay='):
            ordinal, us = map(int, line[6:].split())
            delays[ordinal] = delays.get(ordinal, 0) + us
        elif line == 'adapter end':
            expected = []
            for ordinal, (kind, reg, index, width, value, count) in rows.items():
                if kind in (1, 2, 3):
                    expected += [(ordinal, reg, 1, index), (ordinal, reg+1, width, value)]
                elif kind == 4:
                    expected += [(ordinal, reg, 1, index), (ordinal, reg, 1, value)]
                elif kind == 9:
                    assert count == len(colors)
                    expected += [(ordinal, reg, 1, v) for v in colors]
                else:
                    expected.append((ordinal, reg, width, value))
            assert len(actual) == len(expected), (len(actual), len(expected))
            for i, (got, want) in enumerate(zip(actual, expected)):
                assert got == want, (groups, i, got, want)
            assert resets == [i for i, r in rows.items() if r[0] in (4, 6)]
            assert delays == ({1: 50000, 5: 10000, 200: 10000} if 1 in rows else {5: 10000, 200: 10000})
            collecting = False
            groups += 1
    assert groups == 3
    assert 'DISPLAY PASS' in text and 'FAIL' not in text
    print('PASS adapter: all writes match executed C plan (3 cases), AR/DAC aliases and FP/DPMS waits')
    for line in text.splitlines():
        if line.startswith('PASS '):
            print(line)


def main():
    with tempfile.TemporaryDirectory(prefix='gf-display-') as tmp:
        tmp = Path(tmp)
        (tmp / 'gf_previous_calc.inc').write_text(previous_calc())
        exe = tmp / 'host'
        cmd = [s.replace('gfinit_plan_c_host.c', 'gf_display_host.c')
               for s in test_gfinit_plan_c.command(exe)]
        sources = [ROOT / 'hwdev/gf/service.c',
                   *[p for p in sorted((ROOT / 'hwdev/gf/display').glob('*.c'))
                     if p.stem in ('baseline', 'checks', 'load_row', 'modeset', 'quiesce', 'palette', 'blank', 'modes')]]
        cmd += ['-I'+str(ROOT), '-I'+str(tmp), *map(str, sources)]
        subprocess.run(cmd, cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=180, capture_output=True, text=True)
        if result.returncode:
            print('\n'.join([line for line in result.stdout.splitlines() if 'FAIL' in line][:30]))
            return result.returncode
        check_rows(result.stdout)
    # Retain terra's focused tests: they cover partial palette updates and
    # every blank I/O failure more precisely than the integration fixture.
    with tempfile.TemporaryDirectory(prefix='gf-components-') as tmp:
        for name in ('palette', 'blank', 'modes'):
            exe = Path(tmp) / name
            cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                   '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                   '-nostdlib', '-static', '-no-pie',
                   *['-I'+str(ROOT / d) for d in ('.', 'include', 'sdk/include/os32')],
                   str(ROOT / f'tools/tests/gf_display_{name}_host.c')]
            if name in ('blank', 'modes'):
                cmd.append(str(ROOT / f'hwdev/gf/display/{name}.c'))
            subprocess.run([*cmd, '-o', str(exe)], cwd=ROOT, check=True)
            result = host32.run([str(exe)], cwd=ROOT, timeout=60)
            if result.returncode:
                return result.returncode
            print(f'PASS component {name}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_gf_display.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
