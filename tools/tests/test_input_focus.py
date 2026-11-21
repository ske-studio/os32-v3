"""Foreground ownership and IRQ raw delivery, using the product F1 and input HAL."""
CHECK = dict(name='input-focus-host', tier=1, needs=[], budget=15,
    covers=['tools/tests/test_input_focus.py', 'tools/tests/input_focus_host.c',
            'platform/pc98/platform_io.h', 'tools/tests/host_arch/**', 'include/**', 'exec/exec_gui.h', 'exec/exec_hooks.h',
            'kernel/av_focus.c', 'input/hal/*.[ch]', 'sdk/include/os32/**'],
    basis=['docs/decisions/sound/AV_HAL_DESIGN.md §5-3, §5-6'])
from pathlib import Path
import subprocess
import tempfile


def run(ctx):
    sources = ['kernel/av_focus.c'] + ['input/hal/' + name + '.c' for name in
        ('in2_excl', 'in2_reclaim', 'legacy_ring', 'mouse_ext', 'in2_ring')]
    cases = []
    if ctx.mutate:
        cases = [
            ('av_focus.c', 'in2_on_focus(old, owner);', 'in2_on_focus(owner, old);'),
            ('av_focus.c', 'in2_on_focus(old, owner);', 'in2_on_focus(old, old);'),
            ('in2_excl.c', '    acq_owner = 0;', ''),
            ('in2_excl.c', '    excl = 0;', ''),
            ('in2_excl.c', '    ++gen;', ''),
            ('in2_excl.c', '    kbd_raw_purge_keep();', ''),
            ('in2_excl.c', '    return !!(excl', '    ++focus_gen; return !!(excl'),
            ('in2_reclaim.c', '    in2_excl_release(owner);', ''),
            ('in2_reclaim.c', 'owner == av_focus_owner()', '1'),
            ('in2_reclaim.c', '    in2_ring_clear();', ''),
            ('in2_reclaim.c', '    mouse_ext_read(&x, &y);', '    (void)x; (void)y;'),
            ('in2_reclaim.c', '    mouse_ext_read_wheel();', ''),
            ('in2_reclaim.c', '        in2_reclaim_generation();', ''),
            ('in2_reclaim.c', '    in2_ring_lost(IN2_LOST_FOCUS);', ''),
            ('in2_excl.c', 'if (!in2_excl_release(old)) boundary();\n    ++focus_gen;',
             'if (!in2_excl_release(old)) boundary();'),
            ('legacy_ring.c', ' || sc == KEY_TAB', ''),
            ('in2_ring.c', 'head = count = 0;\n    pending = 0;', 'head = count = 0;'),
            ('in2_ring.c', '        --count;\n        ++dropped;', '        return;'),
        ]
    with tempfile.TemporaryDirectory() as td:
        folder = Path(td)
        for name, old, new in [('', '', ''), *cases]:
            units = []
            for path in sources:
                src = ctx.root / path
                if src.name == name:
                    text = src.read_text()
                    assert text.count(old) == 1, old
                    src = folder / name
                    src.write_text(text.replace(old, new))
                units.append(src)
            binary = folder / 'test'
            subprocess.run(['gcc', '-m32', '-ffreestanding', '-DOS32_HOOK_GUI=0', '-fno-pie', '-no-pie',
                '-nostdlib', '-static', '-ffunction-sections', '-Wl,--gc-sections', '-Wl,--wrap=in2_on_focus',
                '-Werror=implicit-function-declaration', '-Itools/tests/host_arch',
                '-Iinput/hal', '-Iinclude', '-Iexec', '-Iplatform/pc98', '-Ikernel', '-Isdk/include/os32',
                *units, 'tools/tests/input_focus_host.c', '-o', binary], cwd=ctx.root, check=True)
            rc = subprocess.run(['qemu-i386', binary]).returncode
            assert (0 < rc <= 19) if name else rc == 0, (name, old, rc)
            print('RED runtime' if name else 'PASS', name, old, flush=True)
    return 0
