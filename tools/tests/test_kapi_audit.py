"""Real source ILP32 tests for cursor, FM/SSG, input STOP and rshell authority."""
from kernel_includes import include_flags
import argparse
import pathlib
import re
import subprocess
import tempfile
import host32
from test_exec_r1 import function

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = ('kernel/console/console.c', 'drivers/sound/fm/fm.c', 'drivers/char/serial.c', 'kapi/kapi_generated.c', 'drivers/input/kbd.c', 'kernel/ime/ime.c', 'exec/exec.c', 'kernel/ime/ime_dict.c')
MUTATIONS = [
    ('cursor x low', 0, 'if (x < 0) x = 0;', ''),
    ('cursor x high', 0, 'if (x >= TVRAM_COLS) x = TVRAM_COLS - 1;', ''),
    ('cursor y low', 0, 'if (y < 0) y = 0;', ''),
    ('cursor y high', 0, 'if (y >= TVRAM_ROWS) y = TVRAM_ROWS - 1;', ''),
]
for func in ('fm_set_tone', 'fm_note_on', 'fm_note_off', 'ssg_tone', 'ssg_volume'):
    for bound in ('ch < 0', 'ch >= OPN_CHANNEL_COUNT'):
        MUTATIONS.append((func + ' ' + bound, 1, func, bound))
MUTATIONS += [
    ('tone low', 1, 'tone_num >= 0 && ', ''),
    ('tone high', 1, 'tone_num < NUM_TONES', '1'),
    ('mml wait probe', 1, 'if (interruptible && ring3_wait_pending()) return 0;', ''),
    ('mml scan probe', 1, 'if (interruptible && ring3_wait_pending()) break;', ''),
    ('mml key off', 1, 'fm_play_mml_wait', 'keyoff'),
    ('serial receive probe', 2, 'if (interruptible && ring3_wait_pending())', 'if (0)'),
    ('serial puts probe', 2, 'serial_puts_kapi', 'probe'),
    ('serial char probe', 2, 'serial_putchar_kapi', 'probe'),
    ('shared TX abort regression', 2, 'static int ser_tx_byte(char c)\n{', 'static int ser_tx_byte(char c)\n{\n    if (_irq_enabled() && ring3_wait_pending()) ring3_abort_check();'),
    ('shared puts abort regression', 2, 'void serial_puts(const char *str)\n{', 'void serial_puts(const char *str)\n{\n    if (_irq_enabled() && ring3_wait_pending()) ring3_abort_check();'),
    ('extended log early abort', 7, '        serial_puts(".");',
     '        if (ring3_wait_pending()) ring3_abort_check();\n        serial_puts(".");'),
    ('extended log drops STOP', 7, '        serial_puts(".");',
     '        appslot_get(appslot_cur())->abort_req = 0;\n        serial_puts(".");'),
    ('kbd char wait', 4, '            if (interruptible && ring3_wait_pending()) return -1;', ''),
    ('kbd key wait', 4, '    while (kbd_count == 0) {\n        if (interruptible && ring3_wait_pending()) return -1;', '    while (kbd_count == 0) {'),
    ('kbd gui wait', 4, '        if (interruptible && ring3_wait_pending()) return -1; /* GUI fallback */', ''),
    ('ime char cleanup', 5, 'ime_getchar_kapi', 'cleanup'),
    ('ime key cleanup', 5, 'ime_getkey_kapi', 'cleanup'),
    ('wait user guard', 6, '!ring3_call_from_user() || ', ''),
    ('wait gui guard', 6, 'a->gui && ', ''),
    ('wait abort', 6, 'a->abort_req || ', ''),
    ('wait stop', 6, 'appslot_stop_pending()', '0'),
    ('ime owner guard', 5, 'owner != g_ime_reader_owner', '0'),
    ('ime owner cleanup', 5, 'ime_owner_exit', 'cleanup'),
    ('ime OFF row guard', 5, 'if (g_ime.mode != IME_MODE_OFF) preedit_draw();', 'preedit_draw();'),
    ('ime ON mode redraw', 5, 'if (g_ime.mode != IME_MODE_OFF) preedit_draw();', 'if (g_ime.mode != IME_MODE_OFF) preedit_clear();'),
    ('rshell user', 3, 'if (ring3_call_from_user()) return;', ''),
]

# Public wrapper names are stable; provider names need not match them.
for old, new in (('port_putchar_kapi', 'serial_putchar'),
                 ('port_puts_kapi', 'serial_puts'),
                 ('port_getchar_kapi', 'serial_getchar'),
                 ('input_getchar_kapi', 'input_getchar'),
                 ('input_getkey_kapi', 'input_getkey'),
                 ('ime_getchar_kapi', 'ime_getchar'),
                 ('ime_getkey_kapi', 'ime_getkey'),
                 ('fm_play_mml_kapi', 'fm_play_mml')):
    MUTATIONS.append((old + ' public target', 3, old + '(', new + '('))

def run(runner, mutation=None, case=0, expected=None):
    sources = [(ROOT / p).read_text() for p in SOURCES]
    names = ('rshell_set_active', 'serial_putchar', 'serial_puts', 'serial_getchar',
             'kbd_getchar', 'kbd_getkey', 'ime_getchar', 'ime_getkey', 'fm_play_mml')
    wrappers = []
    for name in names:
        match = re.search(r'(?:void|int) __cdecl wrap_' + name + r'\([^\n]*\)\n\{.*?\n\}', sources[3], re.S)
        assert match, name
        wrappers.append(match[0])
    sources[3] = '\n'.join(wrappers)
    start = sources[6].index('int ring3_wait_pending(void)')
    sources[6] = sources[6][start:sources[6].index('\n}', start)+2]
    if mutation:
        name, idx, old, new = mutation
        if new in ('ch < 0', 'ch >= OPN_CHANNEL_COUNT', 'abort', 'user-guard', 'probe', 'cleanup', 'keyoff'):
            start = sources[idx].index(old + '(')
            end = sources[idx].index('\n}', start)
            body = sources[idx][start:end]
            if new == 'keyoff':
                old_text, replacement = '    fm_note_off(0);', ''
                body = body[::-1].replace(old_text[::-1], replacement, 1)[::-1]
                old_text, replacement = 'fm_play_mml_wait', 'fm_play_mml_wait'
            elif new == 'probe':
                old_text, replacement = 'ring3_wait_pending()', '0'
            elif new == 'cleanup':
                old_text, replacement = 'if (!con_sink_is_enabled()) ime_cancel_input();', ''
            elif new == 'abort':
                old_text, replacement = 'ring3_abort_check();', ''
            elif new == 'user-guard':
                old_text, replacement = 'ring3_call_from_user()', '1'
            elif new == 'ch < 0':
                old_text, replacement = 'ch < 0 || ', ''
            else:
                old_text, replacement = 'ch >= OPN_CHANNEL_COUNT', '0'
            assert old_text in body, name
            sources[idx] = sources[idx][:start] + body.replace(old_text, replacement, 1) + sources[idx][end:]
        else:
            assert old in sources[idx], name
            sources[idx] = sources[idx].replace(old, new, 1)
    with tempfile.TemporaryDirectory(prefix='kapi-audit-') as directory:
        work = pathlib.Path(directory)
        sources[7] = function(sources[7], 'ime_dict_log_probe')
        for inc, source in zip(('console_source.inc', 'fm_source.inc', 'serial_source.inc', 'rshell_source.inc', 'kbd_source.inc', 'ime_source.inc', 'wait_source.inc', 'ime_log_source.inc'), sources):
            (work / inc).write_text(source)
        exe = work / 'fixture'
        cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Werror', '-Wno-unused-variable', '-ffreestanding',
               '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
               '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-D__KERNEL_BUILD__',
               '-DOS32_RAW_WAIT_LOG_TICKS=32', '-DAUDIT_CASE=' + str(case), '-I' + str(work)]
        cmd += ['-I' + str(ROOT / 'tools/tests/host_arch'), *include_flags(ROOT)]
        p = subprocess.run(cmd + [str(ROOT / 'tools/tests/kapi_audit_host.c'), '-o', str(exe)], capture_output=True, text=True)
        assert p.returncode == 0, p.stderr
        p = host32.run([str(exe)], runner=runner, capture_output=True, text=True, timeout=30)
        if mutation:
            # A missing tone-number guard can dereference a bogus table
            # pointer before a CHECK. Count the runtime fault, never a build failure.
            tone_fault = mutation and mutation[0] in ('tone low', 'tone high') and p.returncode in (-11, 139)
            assert tone_fault or (p.returncode == 1 and 'FAIL ' in p.stdout), (p.returncode, p.stdout, p.stderr)
            if expected:
                assert expected in p.stdout, (mutation[0], expected, p.stdout, p.stderr)
            print('RED', mutation[0] if mutation else 'item ' + str(case),
                  'runtime SIGSEGV' if tone_fault else p.stdout.strip())
        else:
            assert p.returncode == 0, (p.returncode, p.stdout, p.stderr)
            print(p.stdout.strip())

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', choices=('native', 'qemu'), default='qemu')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    run(args.runner)
    if args.mutate:
        for mutation in MUTATIONS: run(args.runner, mutation)
        print(f'PASS mutations {len(MUTATIONS)}/{len(MUTATIONS)}')
