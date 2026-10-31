"""T4b-5/X-11: real C services/keyboard queues, deterministic ILP32 controls.

Dictionary storage, display and CPU boundaries are explicit in the C fixture.
Park/resume uses production exec/AppSlot functions and setjmp.asm, sharing the
function-extraction tool with test_exec_r1. IME state is the real resident state.
Mutations must compile and fail at their named runtime assertion.
"""
from exec_source import read_text, exec_source
from kernel_includes import include_flags
import argparse
import os
import pathlib
import re
import subprocess
import tempfile

import host32
from test_exec_r1 import function

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = {
    'kbd_source.inc': 'drivers/input/kbd.c',
    'ime_source.inc': 'kernel/ime/ime.c',
    'ime_delivery.inc': 'kernel/ime/ime_delivery.inc',
    'ime_convert.inc': 'kernel/ime/ime_convert.inc',
    'ime_render.inc': 'kernel/ime/ime_render.inc',
    'ime_dict_api.inc': 'kernel/ime/ime_dict_api.inc',
    'gui_source.inc': 'kernel/gui.c',
    'sink_source.inc': 'kernel/console/con_sink.c',
    'inject_source.inc': 'kernel/kbd_inject.c',
    'exec_source.inc': 'exec/exec.c',
    'slot_source.inc': 'exec/appslot.c',
}


def exec_sources(sources):
    source = sources['exec_source.inc']
    context = source[source.index('typedef struct {\n    CallerAccessFrame caller;'):
                     source.index('} Ring3CallContext;') + len('} Ring3CallContext;')]
    constants = '\n'.join(line for line in source.splitlines() if
                          line.startswith(('#define EXEC_LJ_', 'static volatile int g_longjmp_')))
    sources['exec_source.inc'] = constants + '\n' + context + '\n' + '\n'.join(
        function(source, name) for name in (
            'ring3_context_save', 'ring3_context_clear', 'ring3_context_restore',
            'exec_restore_context', 'exec_park_transfer', 'exec_park', 'exec_resume'))
    sources['slot_source.inc'] = '\n'.join(function(sources['slot_source.inc'], name)
        for name in ('appslot_park_check', 'appslot_park_finish', 'appslot_park_commit',
                     'appslot_resume_check',
                     'appslot_resume_source', 'appslot_resume_commit', 'appslot_mark_scheduled'))


def run(mutation=None):
    sources = {name: read_text(ROOT / path) for name, path in SOURCES.items()}
    if mutation:
        name, source, old, new, expected = mutation
        assert sources[source].count(old) == 1, (name, sources[source].count(old))
        sources[source] = sources[source].replace(old, new)
    sources['slot_source.inc'] += (ROOT / 'exec/appslot_transition.inc').read_text()
    exec_sources(sources)
    with tempfile.TemporaryDirectory(prefix='x11-shared-') as directory:
        work = pathlib.Path(directory)
        for include_name, source in sources.items():
            (work / include_name).write_text(source)
        source = exec_source(ROOT)
        getter = re.search(r'^int exec_last_result\([^)]*\)\n\{.*?^\}', source, re.M | re.S)
        assert getter
        (work / 'exec_result.inc').write_text(
            'static int g_last_kind, g_last_code;\n' + getter[0])
        includes = ['-I' + str(work)] + ['-I' + str(ROOT / 'tools/tests/host_arch'), *include_flags(ROOT)]
        exe = work / 'test'
        subprocess.run(['nasm', '-f', 'elf32', str(ROOT / 'arch/x86/kernel/setjmp.asm'),
                        '-o', str(work / 'setjmp.o')], check=True)
        subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                        '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable',
                        '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                        '-ffunction-sections', '-fdata-sections', '-nostdlib',
                        '-static', '-no-pie', '-Wl,--gc-sections',
                        *includes, str(ROOT / 'tools/tests/x11_shared_host.c'),
                        str(work / 'setjmp.o'),
                        *[str(ROOT / p) for p in ('lib/kstring.c', 'lib/kstring_c.c',
                           'lib/utf8.c', 'kernel/ime/ime_romkana.c')],
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
    ('IMEDICT-stale-loaded', 'ime_dict_api.inc',
     '    g_ime.dict_loaded = g_ime.dict.db != NULL;', '',
     'FAIL IMEDICT ON retries dictionary after failed switch'),
    ('IMEBS-lost-n-wait', 'ime_convert.inc',
     '                g_ime.rk.n_wait = (kstrcmp(g_ime.rk.preedit, "n") == 0);',
     '                g_ime.rk.n_wait = 0;', 'FAIL IMEBS restored n commits once'),
    ('IMECAP-dropped-last-byte', 'ime_convert.inc',
     '    if (g_ime.kana_len + olen <= (int)sizeof(g_ime.kana_buf) - 1) {',
     '    if (g_ime.kana_len + olen < (int)sizeof(g_ime.kana_buf) - 1) {',
     'FAIL IMECAP uses all 127 bytes and preserves terminator'),
    ('IMESPACE-lone-n-not-flushed', 'ime_convert.inc',
     '    if (scancode == KEY_SPACE && (g_ime.kana_len > 0 || g_ime.rk.n_wait)) {',
     '    if (scancode == KEY_SPACE && g_ime.kana_len > 0) {',
     'FAIL IMESPACE lone n searched without raw Space or pending n'),
    ('IMEGUI-list-not-erased', 'ime_render.inc',
     '                g_ime.render->clear_row(y, ATTR_WHITE);',
     '                (void)y;', 'FAIL IMEGUI closed list leaves empty rows'),
    ('IMETAB-no-pending-key', 'ime_delivery.inc',
     '        pending_keydata = keydata;', '        (void)keydata;',
     'FAIL IMETAB original transparent key once with scan'),
    ('IMETAB-key-before-commit', 'ime_delivery.inc',
     '    int result = ime_process_key(keydata);',
     '    int result = ime_process_key(keydata);\n    if (result == -1) return keydata;',
     'FAIL IMETAB candidate before transparent key'),
    ('IMETAB-scan-lost', 'ime_delivery.inc',
     '        pending_keydata = keydata;', '        pending_keydata = keydata & 0xFF;',
     'FAIL IMETAB original transparent key once with scan'),
    ('IMETAB-pending-delivered-twice', 'ime_delivery.inc',
     '    int keydata = pending_keydata;\n    pending_keydata = -1;',
     '    int keydata = pending_keydata;',
     'FAIL IMETAB next a becomes new preedit'),
    ('IMETAB-cancel-keeps-key', 'ime_source.inc',
     '    g_ime.commit_len = g_ime.commit_pos = 0;\n    ime_delivery_clear();',
     '    g_ime.commit_len = g_ime.commit_pos = 0;',
     'FAIL IMETAB cancellation clears pending key and commit together'),
    ('IMETAB-init-keeps-key', 'ime_source.inc',
     '    kmemset(&g_ime, 0, sizeof(IME_State));\n    ime_delivery_clear();',
     '    kmemset(&g_ime, 0, sizeof(IME_State));',
     'FAIL IMETAB cancellation clears pending key and commit together'),
    ('IMETAB-park-clears-key', 'exec_source.inc',
     '    appslot_park_commit();      /* PARKED + 印 + owner 1 へ */',
     '    ime_delivery_clear();\n    appslot_park_commit();',
     'FAIL IMETAB real resume delivers remaining candidate then pending Tab once'),
    ('IMEORDER-key-before-candidate', 'ime_convert.inc',
     '    if (ascii >= ASCII_PRINT_FIRST && ascii <= ASCII_PRINT_LAST) {',
     '    if (0) {', 'FAIL IMEORDER candidate before typed ASCII'),
    ('MINIMAL-scan-only-poll', 'ime_source.inc',
     'int ime_trygetchar(void)\n{',
     'int ime_trygetchar(void)\n{\n    if (!services_public) return kbd_trygetchar();',
     'FAIL MINIMAL scan-only poll returns no character'),
    ('MINIMAL-scan-only-wait', 'ime_source.inc',
     'static int ime_getchar_wait(int interruptible)\n{',
     'static int ime_getchar_wait(int interruptible)\n{\n'
     '    if (!services_public) return interruptible ? kbd_getchar_kapi() : kbd_getchar();',
     'FAIL MINIMAL wait skips scan-only key'),
    ('IMEIND-RETURN-clear-only', 'ime_convert.inc',
     '            preedit_draw(); /* Keep the ON indicator even when nothing was committed. */',
     '            preedit_clear();',
     'FAIL IMEIND discarded RETURN keeps visible ON indicator'),
    ('IMEIND-RETURN-stale-romaji', 'ime_convert.inc',
     '            ime_rk_init(&g_ime.rk); /* Discard incomplete romaji after flushing n. */',
     '', 'FAIL IMEIND discarded reading is no longer visible'),
    ('IMECAND-no-immediate-ON-redraw', 'ime_source.inc',
     '    /* Restore now and again at the first read after the shell reload clear. */\n'
     '    if (ime_is_active()) preedit_draw();', '',
     'FAIL IMECAND GUI entry ON indicator drawn synchronously'),
    ('IMECAND-no-cancel-ON-redraw', 'ime_source.inc',
     '    if (g_ime.mode != IME_MODE_OFF) preedit_draw();', '',
     'FAIL IMECAND cancellation keeps visible ON indicator'),
    ('IMECAND-CUI-mode-invisible', 'ime_source.inc',
     '    g_ime_cui_redraw = table == NULL;', '',
     'FAIL IMECAND CUI mode visible after reload clear'),
    ('IMECAND-no-STOP-cancel', 'ime_source.inc',
     '        ime_cancel_input();\n        return -1;',
     '        return -1;', 'FAIL IMECAND input lifetime cancels all transient state'),
    ('IMECAND-no-renderer-cancel', 'ime_source.inc',
     '        ime_cancel_input(); /* GUI/CUI input lifetime ends with its renderer. */',
     '', 'FAIL IMECAND input lifetime cancels all transient state'),

    ('park-clears-candidates', 'exec_source.inc',
     '    exec_park_transfer(id, a, g_cur_frame, appslot_park_commit);',
     '    ime_init();\n    exec_park_transfer(id, a, g_cur_frame, appslot_park_commit);',
     'FAIL real park preserves candidate and dictionary'),
    ('resume-clears-output', 'exec_source.inc',
     '    appslot_resume_commit((int)app_id);',
     '    appslot_resume_commit((int)app_id);\n'
     '    while (ime_feed_key(-1) >= 0) {} /* discard pending output */',
     'FAIL real resume preserves unread commit and dictionary'),
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
        selected = [m for m in MUTATIONS if not os.environ.get("MUTATION_FILTER")
                    or os.environ["MUTATION_FILTER"] in m[0]]
        assert selected, "no mutations selected"
        for mutation in selected:
            run(mutation)
        print(f'PASS {len(selected)} mutations: runtime RED; compile failures 0')
