"""Exercise synchronous child WM presents and the lifetime of the display grant."""
from pathlib import Path
import tempfile
import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ['tools/tests/gui_present_owner_host.c', 'kernel/gui.c', 'exec/appslot.c',
           'gfx/gfx_present.c']
CHECK = dict(name='gui-present-owner-host', tier=1, needs=[], budget=5,
             covers=['tools/tests/gui_present_owner_host.c', 'kernel/gui.c', 'exec/appslot.c',
                 'gfx/gfx_present.c', 'tools/tests/test_gui_present_owner.py', 'tools/tests/host32.py',
                 'tools/tests/mutpar.py', 'tools/tests/kernel_includes.py', 'tools/tests/host_arch/**',
                 'tools/tests/gfx2_bridge_fake.h', 'tools/tests/font_guard_inactive.h',
                 'exec/appslot*.inc', 'exec/appslot.h', 'exec/exec_fault.inc',
                 'kernel/gui.h', 'gfx/*.h', 'include/**', 'platform/pc98/platform_display_fallback.h'],
             basis=['docs/design/kernel/gui.md 同期 COMMIT の描画主体'])

MUTATIONS = [
    ('normal return', 'kernel/gui.c', '    appslot_gui_draw_leave(previous);\n    appslot_gui_op_leave();',
     '    (void)previous;\n    appslot_gui_op_leave();'),
    ('park', 'exec/appslot_transition.inc', 'appslot_gui_draw_leave(0);', ''),
    ('fault', 'exec/exec_fault.inc', 'appslot_gui_draw_leave(0);', ''),
    ('registration', 'exec/appslot.c', 'handler_generation[g_cur] == registered_generation',
     'handler_generation[g_cur] != 0'),
    ('child present', 'exec/appslot.c', 'return appslot_present_owner() == g_gfx_owner;', 'return 1;'),
]


def run(ctx):
    for mutant in [None, *(MUTATIONS if ctx.mutate else [])]:
        edits = {}
        if mutant:
            name, path, old, new = mutant
            source = (ROOT / path).read_text()
            assert source.count(old) == 1, name
            edits[path] = source.replace(old, new)
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            exe = work / 'present'
            command = ['gcc', '-m32', '-std=gnu11', '-Werror', '-ffreestanding',
                       '-fno-stack-protector', '-fno-pie', '-no-pie', '-nostdlib', '-static',
                       '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                       '-I' + str(ROOT / 'tools/tests/host_arch'), *include_flags(ROOT),
                       *(str(ROOT / path) for path in SOURCES), '-o', str(exe)]
            build_in_tree(ROOT, work, edits, [command])
            result = host32.run([str(exe)])
            assert (0 < result.returncode < 128 if mutant else result.returncode == 0), (mutant, result)
            print('runtime RED ' + name if mutant else 'PASS WM display ownership')
    return 0
