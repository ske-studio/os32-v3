"""PowerMac USB cooked input: real product state/map/provider, ILP32/QEMU."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

CHECK = dict(name='ppc-input-usb-host', tier=1, budget=60, needs=[],
    covers=['platform/powermac/input_usb.[ch]', 'platform/powermac/input_keymap_backend.c', 'platform/powermac/usb_key_report.c', 'input/hal/*.c', 'input/hal/*.h', 'input/keymaps/*.c', 'include/**', 'sdk/include/os32/**', 'tools/tests/host_arch/**', 'platform/powermac/platform_io.h',
            'platform/powermac/input_boot.c', 'tools/tests/ppc_input_usb_host.c',
            'tools/checks/ver-ppc-input-usb.py','platform/powermac/input_boot.h', 'platform/powermac/input_decode.c',
            'include/types.h', 'include/kbd.h', 'include/input_service.h', 'include/clock_source.h',
            'platform/powermac/lan_boot.h', 'kernel/cli/resident_cli.h',
            'tools/tests/host32.py', 'include/io.h', 'arch/ppc/arch_io.h',
            'kernel/input_legacy.c', 'kernel/input_wait.c', 'input/hal/in2_int.h'],
    basis=['PPC_USB_KBD_DESIGN §2', 'PPC_USB_KBD_DESIGN §5', 'PPC_P3_DESIGN §5 ③b-2'],
    env_limit='製品USB入力Cの修飾・release・FIFO・LAN優先を確認するが、実USB/HCとLANのIRQ競合は再現しない。',
)
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/tests'))
import host32

MUTATIONS = [
    ('rollback', 'usb_input.c', 'input_src_release(src, gen);', '(void)gen;', 10, 'START\n'),
    ('move-sign', 'usb_input.c', '.x = dx, .y = dy', '.x = (u8)dx, .y = dy', 10, 'START\n'),
    ('live-mods', 'legacy_ring.c', '*mods = cooked_mods[kbd_head]', '*mods = input_key_mods()', 12, 'START\nMAP\n'),
    ('abort', 'usb_input.c', ')) ring3_abort_request();', ')) (void)0;', -1, 'START\nMAP\nSTATE\nFIFO\n'),
    ('peek-consumes', 'input_boot.c', 'if (lan_lookahead >= 0) return lan_lookahead;', 'if (lan_lookahead >= 0) { int key = lan_lookahead; lan_lookahead = -1; return key; }', 15, 'START\nMAP\nSTATE\nFIFO\n'),
    ('lan-first', 'input_boot.c', 'int key = lan_peek();\n    if (key >= 0)', 'int usb = pm_input_usb_take(); if (usb >= 0) return usb;\n    int key = lan_peek();\n    if (key >= 0)', 15, 'START\nMAP\nSTATE\nFIFO\n'),
    ('no-usb', 'input_boot.c', 'return pm_input_usb_take();', 'return -1;', 15, 'START\nMAP\nSTATE\nFIFO\n'),
    ('no-lan', 'input_boot.c', 'lan_lookahead = lan_key();', 'lan_lookahead = 0 ? lan_key() : -1;', 15, 'START\nMAP\nSTATE\nFIFO\n'),
    # peek_key became multi-line with #if !KBOOT_LINK in 585dbbdb (P3 ⑤).
    ('peek-no-progress', 'input_boot.c', '#if !KBOOT_LINK\n    idle_work_run();\n#endif\n    return input_legacy_peek(IL_KEY);', '    return input_legacy_peek(IL_KEY);', 15, 'START\nMAP\nSTATE\nFIFO\n'),
    ('peek-usb-first', 'input_boot.c', 'return key >= 0 ? key : pm_input_usb_peek();', 'int usb = pm_input_usb_peek(); return usb >= 0 ? usb : key;', 15, 'START\nMAP\nSTATE\nFIFO\n'),
    ('peek-pumps', 'input_boot.c', '    int key = lan_peek();\n    return key >= 0 ? key : pm_input_usb_peek();', '    pm_terminal_poll();\n    int key = lan_peek();\n    return key >= 0 ? key : pm_input_usb_peek();', 15, 'START\nMAP\nSTATE\nFIFO\n'),
    ('local-char-mode', 'input_boot.c', 'return input_legacy_try(IL_LOCAL_CHAR);', 'return input_legacy_try(IL_CHAR);', 15, 'START\nMAP\nSTATE\nFIFO\n'),
]
HAL = ['usb_input', 'in2_dev', 'in2_state', 'in2_ring', 'keymap_registry', 'keymap_eval',
       'keymap_state', 'kbd_merge', 'legacy_keymap', 'legacy_ring', 'legacy_mode',
       'pointer_state', 'mouse_ext']
PLATFORM = ['input_boot', 'input_usb', 'usb_key_report', 'input_keymap_backend']

def main(mutate=False):
    paths = ['input/hal/' + n + '.c' for n in HAL] + [
        'platform/powermac/' + n + '.c' for n in PLATFORM]
    sources = {Path(p).name: (ROOT/p).read_text() for p in paths}
    with tempfile.TemporaryDirectory(prefix='ppc-input-usb-', dir=os.environ.get('TMPDIR')) as td:
        work = Path(td)
        # Replace only the IRQ hardware boundary, not product functions.
        cases = [('normal', None, None, None, 0, 'START\nMAP\nSTATE\nFIFO\nWAIT\nPROVIDER\n')]
        if mutate: cases += MUTATIONS
        for name, file, old, new, expected, reached in cases:
            for n, text in sources.items():
                if n == file:
                    if text.count(old) != 1: raise RuntimeError(name+': nonunique anchor')
                    text = text.replace(old, new)
                (work/n).write_text(text)
            exe = work/'test'
            subprocess.run(['gcc','-m32','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
                '-nostdinc','-ffreestanding','-fno-builtin','-fno-pie','-no-pie','-nostdlib','-static',
                '-DHOST_ARCH_SHARED_IF','-DINPUT_COOKED_MODS=1','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Itools/tests/host_arch','-Iinput/hal','-Iinclude','-Iarch/ppc','-Iplatform/powermac','-Isdk/include/os32','-Idrivers/input','-I.',
                str(ROOT/'tools/tests/ppc_input_usb_host.c'),*[str(work/n) for n in sources],
                'platform/powermac/input_decode.c','kernel/input_legacy.c','kernel/input_wait.c','input/keymaps/hid_jp.c','input/keymaps/pc98_jp.c','-o',str(exe)], cwd=ROOT, check=True)
            try:
                result = host32.run([str(exe)], runner='qemu', capture_output=True, text=True, timeout=10)
            except subprocess.TimeoutExpired as error:
                if name != 'abort' or error.output != b'START\nMAP\nSTATE\nFIFO\nWAIT\n': raise
                print('RED runtime: interruptible wait timed out', flush=True)
                continue
            marker = reached + ('FAIL\n' if expected else '')
            if result.returncode != expected or result.stdout != marker:
                raise RuntimeError(f'{name}: rc={result.returncode} output={result.stdout!r}, expected={expected}/{marker!r}')
            print(('GREEN' if name=='normal' else 'RED')+f' {name}: runtime rc={expected} reached={reached.splitlines()[-1]}', flush=True)
    return 0

def run(ctx):
    return main(ctx.mutate)

if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))
