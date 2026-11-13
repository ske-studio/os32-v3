"""Freeze x86 TVRAM routing with the real console and PC98 backend."""
import argparse
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCES = {
    'console_source.inc': 'kernel/console/console.c',
    'console_dual_source.inc': 'kernel/console/console_dual.c',
    'display_dual_source.inc': 'platform/pc98/display_dual.c',
    'pc98_console_source.inc': 'platform/pc98/console.c',
}
MUTANTS = [
    ('x86 default mirror enabled', 'console_source.inc',
     '#define CONSOLE_PUTCHAR_MIRROR 0',
     '#define CONSOLE_PUTCHAR_MIRROR 1', 'character mirror policy'),
    ('character mirror missing', 'console_source.inc',
     'if (CONSOLE_PUTCHAR_MIRROR && (rshell_active || boot_serial_mirror)) con_output_putchar(ch);',
     '(void)ch;', 'character mirror policy'),
    ('character mirror doubled', 'console_source.inc',
     'if (CONSOLE_PUTCHAR_MIRROR && (rshell_active || boot_serial_mirror)) con_output_putchar(ch);',
     'if (CONSOLE_PUTCHAR_MIRROR && (rshell_active || boot_serial_mirror)) { con_output_putchar(ch); con_output_putchar(ch); }', 'character mirror policy'),
    ('string mirror missing', 'console_source.inc',
     'if (rshell_active || boot_serial_mirror) con_output_putchar(*str);',
     '(void)str;', 'mirror exactly once'),
    ('write mirror missing', 'console_source.inc',
     'for (i = 0; i < size; i++) con_output_putchar(buf[i]);',
     'for (i = size; i < size; i++) con_output_putchar(buf[i]);', 'mirror exactly once'),
    ('shell requires sink', 'console_source.inc',
     'void shell_putchar(char ch, u8 color)\n{',
     'void shell_putchar(char ch, u8 color)\n{\n'
     '    if (!con_sink_is_enabled()) return;', 'CUI shell character'),
    ('safe requires cached TEXT', 'pc98_console_source.inc',
     '    return !pc98_console_safe || pc98_console_safe();',
     '    int n; const struct con_out *o = con_dual_outputs(&n);\n'
     '    for (int i = 0; i < n; i++)\n'
     '        if (o[i].role == CON_OUT_TEXT && o[i].live) return 1;\n'
     '    return 0;', 'CUI boot log'),
    ('render sink inverted', 'console_source.inc',
     '!legacy_active() && (!con_sink_is_enabled() || con_dual_active())',
     '!legacy_active() && (con_sink_is_enabled() || con_dual_active())', 'CUI boot log'),
    ('render dual inverted', 'console_source.inc',
     '!legacy_active() && (!con_sink_is_enabled() || con_dual_active())',
     '!legacy_active() && (!con_sink_is_enabled() || !con_dual_active())', 'GUI frozen'),
    ('cursor dual inverted', 'console_source.inc',
     'if (con_sink_is_enabled() && !con_dual_active()) return;',
     'if (con_sink_is_enabled() && con_dual_active()) return;', 'GUI frozen'),
    ('GUI clear inverted', 'console_source.inc',
     'if (con_sink_is_enabled()) return;',
     'if (!con_sink_is_enabled()) return;', 'GUI frozen'),
    ('return clear dual inverted', 'console_source.inc',
     'if (was_gui && !con_dual_was_live()) tvram_clear();',
     'if (was_gui && con_dual_was_live()) tvram_clear();', 'GUI ordinary return clear'),
]


def build_run(tmp, sources, ppc):
    for name, source in sources.items():
        (tmp / name).write_text(source)
    exe = tmp / 'console-cui-route'
    subprocess.run([
        'gcc', '-m32', '-std=gnu11', '-O0', '-ffreestanding', '-fno-builtin',
        '-fno-pie', '-no-pie', '-fno-stack-protector', '-nostdlib', '-static',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
        '-DEXPECT_PUTCHAR_MIRROR=' + str(ppc),
        *(['-DCONSOLE_PUTCHAR_MIRROR=1'] if ppc else []),
        '-D__KERNEL_BUILD__', '-I' + str(tmp), '-I' + str(ROOT),
        '-I' + str(ROOT / 'tools/tests'), *include_flags(ROOT),
        str(ROOT / 'tools/tests/console_cui_route_host.c'),
        str(ROOT / 'lib/utf8.c'), str(ROOT / 'lib/kprintf.c'), str(ROOT / 'lib/kprintf_attr.c'),
        str(ROOT / 'platform/pc98/console_attr.c'), '-o', str(exe),
    ], cwd=ROOT, check=True)
    return host32.run([str(exe)], runner='qemu', capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    sources = {name: (ROOT / path).read_text() for name, path in SOURCES.items()}
    with tempfile.TemporaryDirectory(prefix='console-cui-route-') as directory:
        tmp = Path(directory)
        for ppc in (0, 1):
            print('configuration: ' + ('PowerPC' if ppc else 'x86'), flush=True)
            result = build_run(tmp, sources, ppc)
            print(result.stdout, end='', flush=True)
            assert result.returncode == 0, (result.returncode, result.stderr)
            if args.mutate:
                for label, name, old, new, assertion in MUTANTS:
                    if label.startswith('x86') and ppc:
                        continue
                    if label.startswith('character mirror') and not ppc:
                        continue
                    assert sources[name].count(old) == 1, (label, 'anchor missing/ambiguous')
                    mutated = dict(sources)
                    mutated[name] = sources[name].replace(old, new)
                    result = build_run(tmp, mutated, ppc)
                    assert result.returncode == 1 and 'FAIL ' + assertion in result.stdout, (
                        label, result.returncode, result.stdout, result.stderr)
                    print('RED runtime: ' + label + ': ' + result.stdout.strip(), flush=True)
    print('EXIT console_cui_route_host=0')


if __name__ == '__main__':
    main()
