"""PowerMac USB cooked input: real product state/map/provider, ILP32/QEMU."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

CHECK = dict(name='ppc-input-usb-host', tier=1, budget=60, needs=[],
    covers=['platform/powermac/input_usb.[ch]', 'platform/powermac/input_keymap.c',
            'platform/powermac/input_boot.c', 'tools/tests/ppc_input_usb_host.c',
            'tools/checks/ver-ppc-input-usb.py','platform/powermac/input_boot.h', 'platform/powermac/input_decode.c',
            'include/types.h', 'include/input_service.h', 'include/clock_source.h',
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
    ('caps-xor', 'input_keymap.c', '!!(state & PM_INPUT_SHIFT) ^ !!(state & PM_INPUT_CAPS)', '!!(state & PM_INPUT_SHIFT)', 10, 'START\n'),
    ('symbol-shift', 'input_keymap.c', '(state & PM_INPUT_SHIFT) ? shifted[sc] : plain[sc]', '(state & PM_INPUT_SHIFT) ? plain[sc] : shifted[sc]', 10, 'START\n'),
    ('backspace', 'input_keymap.c', 'case 0x0e: return 8;', 'case 0x0e: return 127;', 10, 'START\n'),
    ('ctrl-c', 'input_keymap.c', "c == 'c' ? 27 : c - 'a' + 1", "c - 'a' + 1", 10, 'START\n'),
    ('has-consumes', 'input_usb.c', 'int ready = count != 0;', 'int ready = count != 0; if (count) { tail = (tail + 1) % PM_INPUT_KEYS; count--; }', 12, 'START\nMAP\n'),
    ('usb-peek-consumes', 'input_usb.c', 'int key = count ? keys[tail] : -1;', 'int key = count ? keys[tail] : -1; if (count) { tail = (tail + 1) % PM_INPUT_KEYS; count--; }', 12, 'START\nMAP\n'),
    ('release', 'input_usb.c', 'down[src - 1][sc] = 0;', 'down[src - 1][sc] = down[src - 1][sc];', 12, 'START\nMAP\n'),
    ('other-source', 'input_usb.c', 'down[src - 1][sc] = 0;', '{ down[src - 1][sc] = 0; down[src % PM_INPUT_SOURCES][sc] = 0; }', 12, 'START\nMAP\n'),
    ('repeat-enqueue', 'input_usb.c', '    enqueue(sc); /* Repeat uses the current modifiers. */', '    /* omitted repeat */', 13, 'START\nMAP\n'),
    ('repeat-down', 'input_usb.c', 'if (!down[src - 1][sc]) return;', 'if (0) return;', 11, 'START\nMAP\n'),
    ('full-before-state', 'input_usb.c', '    if (!pressed) { *held = 0; return; }', '    if (count == PM_INPUT_KEYS) return;\n    if (!pressed) { *held = 0; return; }', 14, 'START\nMAP\nSTATE\n'),
    ('peek-consumes', 'input_boot.c', 'if (lan_lookahead >= 0) return lan_lookahead;', 'if (lan_lookahead >= 0) { int key = lan_lookahead; lan_lookahead = -1; return key; }', 15, 'START\nMAP\nSTATE\nFIFO\n'),
    ('lan-first', 'input_boot.c', 'return key >= 0 ? key : pm_input_usb_take();', 'int usb = pm_input_usb_take(); return usb >= 0 ? usb : key;', 15, 'START\nMAP\nSTATE\nFIFO\n'),
    ('no-usb', 'input_boot.c', 'return key >= 0 ? key : pm_input_usb_take();', 'return key >= 0 ? key : -1;', 15, 'START\nMAP\nSTATE\nFIFO\n'),
    ('no-lan', 'input_boot.c', 'lan_lookahead = lan_key();', 'lan_lookahead = 0 ? lan_key() : -1;', 15, 'START\nMAP\nSTATE\nFIFO\n'),
    ('local-char-mode', 'input_boot.c', 'return input_legacy_try(IL_LOCAL_CHAR);', 'return input_legacy_try(IL_CHAR);', 15, 'START\nMAP\nSTATE\nFIFO\n'),
]
# One line per special mapping: remove one case while retaining the others.
for sc in range(0x39, 0x3f):
    MUTATIONS.append(('special-%02x'%sc, 'input_keymap.c', 'case 0x%02x:'%sc, '/* omitted %02x */'%sc, 10, 'START\n'))

def main(mutate=False):
    sources = {n:(ROOT/'platform/powermac'/n).read_text() for n in ['input_usb.c','input_keymap.c','input_boot.c']}
    with tempfile.TemporaryDirectory(prefix='ppc-input-usb-', dir=os.environ.get('TMPDIR')) as td:
        work = Path(td)
        # Replace only the IRQ hardware boundary, not product functions.
        (work/'io.h').write_text('unsigned int irq_save(void);\nvoid irq_restore(unsigned int);\n')
        cases = [('normal', None, None, None, 0, 'START\nMAP\nSTATE\nFIFO\nPROVIDER\n')]
        if mutate: cases += MUTATIONS
        for name, file, old, new, expected, reached in cases:
            for n, text in sources.items():
                if n == file:
                    if text.count(old) != 1: raise RuntimeError(name+': nonunique anchor')
                    text = text.replace(old, new)
                (work/n).write_text(text)
            exe = work/'test'
            subprocess.run(['gcc','-m32','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
                '-ffreestanding','-fno-builtin','-fno-pie','-no-pie','-nostdlib','-static',
                '-I'+str(work),'-Iinclude','-Iarch/ppc','-Iplatform/powermac','-Isdk/include/os32','-Idrivers/input','-I.',
                str(ROOT/'tools/tests/ppc_input_usb_host.c'),*[str(work/n) for n in sources],
                'platform/powermac/input_decode.c','kernel/input_legacy.c','kernel/input_wait.c','-o',str(exe)], cwd=ROOT, check=True)
            result = host32.run([str(exe)], runner='qemu', capture_output=True, text=True, timeout=10)
            marker = reached + ('FAIL\n' if expected else '')
            if result.returncode != expected or result.stdout != marker:
                raise RuntimeError(f'{name}: rc={result.returncode} output={result.stdout!r}, expected={expected}/{marker!r}')
            print(('GREEN' if name=='normal' else 'RED')+f' {name}: runtime rc={expected} reached={reached.splitlines()[-1]}', flush=True)
    return 0

def run(ctx):
    return main(ctx.mutate)

if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))
