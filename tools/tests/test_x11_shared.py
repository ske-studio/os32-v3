"""T4b-5/X-11: real C services/keyboard queues, deterministic ILP32 controls.

Dictionary storage, display and scheduler boundaries are explicit in the C
fixture. test_exec_r1 covers real normal/fault/STOP/park landings and caller,
ESP0 and separate heap restoration; the getter here covers optional outputs.
Mutations must compile and fail at their named runtime assertion.
"""
import argparse
import pathlib
import re
import subprocess
import tempfile

import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = {
    'kbd_source.inc': 'drivers/kbd.c',
    'ime_source.inc': 'kernel/ime.c',
    'gui_source.inc': 'kernel/gui.c',
    'sink_source.inc': 'kernel/con_sink.c',
    'inject_source.inc': 'kernel/kbd_inject.c',
}


def run(mutation=None):
    sources = {name: (ROOT / path).read_text() for name, path in SOURCES.items()}
    if mutation:
        name, source, old, new, expected = mutation
        assert sources[source].count(old) == 1, (name, sources[source].count(old))
        sources[source] = sources[source].replace(old, new)
    with tempfile.TemporaryDirectory(prefix='x11-shared-') as directory:
        work = pathlib.Path(directory)
        for include_name, source in sources.items():
            (work / include_name).write_text(source)
        source = (ROOT / 'exec/exec.c').read_text()
        getter = re.search(r'^int exec_last_result\([^)]*\)\n\{.*?^\}', source, re.M | re.S)
        assert getter
        (work / 'exec_result.inc').write_text(
            'static int g_last_kind, g_last_code;\n' + getter[0])
        includes = ['-I' + str(work)] + ['-I' + str(ROOT / p) for p in (
            'tools/tests/host_arch', 'include', 'arch/x86', 'platform/pc98',
            'kernel', 'drivers', 'lib', 'sdk/include/os32', 'exec', 'fs')]
        exe = work / 'test'
        subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                        '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable',
                        '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                        '-ffunction-sections', '-fdata-sections', '-nostdlib',
                        '-static', '-no-pie', '-Wl,--gc-sections',
                        *includes, str(ROOT / 'tools/tests/x11_shared_host.c'),
                        *[str(ROOT / p) for p in ('lib/kstring.c', 'lib/kstring_c.c',
                           'lib/utf8.c', 'kernel/ime_romkana.c')],
                        '-o', str(exe)], check=True)
        result = host32.run([str(exe)], capture_output=True, text=True, timeout=30)
        print(result.stdout, end='', flush=True)
        if mutation:
            assert result.returncode == 1 and expected in result.stdout, (
                name, result.returncode, result.stdout, result.stderr)
            print('RED runtime:', name, flush=True)
        else:
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)


MUTATIONS = [
    ('invalid-mode-write', 'ime_source.inc',
     '    if (mode < IME_MODE_OFF || mode > IME_MODE_KATAKANA) return;', '',
     'FAIL invalid mode preserves complete state'),
    ('WM-tail-double-delivery', 'ime_source.inc',
     '    if (con_sink_is_enabled()) return kbd_trygetchar();', '',
     'FAIL GUI cooked readers cannot steal WM commit tail'),
    ('terminal-reconversion', 'ime_source.inc',
     '    if (con_sink_is_enabled()) return kbd_trygetkey();',
     '    if (con_sink_is_enabled() && g_ime.commit_pos < g_ime.commit_len) return kbd_trygetkey();',
     'FAIL GUI injected ASCII is not converted twice'),
    ('nonblocking-reader-not-tracked', 'ime_source.inc',
     '        g_ime_reader_owner = res_owner_get(); /* nonblocking CUI input */', '        (void)0;',
     'FAIL nonblocking reader exit cancels only input'),
    ('owner-substitution', 'gui_source.inc',
     'r = g_gui_handler(op, arg, res_owner_get());',
     'r = g_gui_handler(op, arg, GUI_SHELL_OWNER);', 'FAIL WM dispatch real owner'),
    ('internal-op-forwarded', 'gui_source.inc',
     '    if (op == GUI_OP_OWNER_EXIT) return OS32_ERR_INVAL;', '',
     'FAIL internal owner exit rejected'),
    ('reader-early-release', 'sink_source.inc',
     '    if (g_reader == id) g_reader = CON_SINK_NO_READER;',
     '    g_reader = CON_SINK_NO_READER;', 'FAIL reader retained across child exit'),
    ('GUI-cooked-double-enqueue', 'kbd_source.inc',
     '    if (kbd_gui_mode) return;\n\n    /* スキャンコード',
     '    /* スキャンコード', 'FAIL WM raw only'),
]


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    run()
    if args.mutate:
        for mutation in MUTATIONS:
            run(mutation)
        print(f'PASS {len(MUTATIONS)} mutations: runtime RED; compile failures 0')
