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


def build_run(tmp, sources):
    for name, source in sources.items():
        (tmp / name).write_text(source)
    exe = tmp / 'console-cui-route'
    subprocess.run([
        'gcc', '-m32', '-std=gnu11', '-O0', '-ffreestanding', '-fno-builtin',
        '-fno-pie', '-no-pie', '-fno-stack-protector', '-nostdlib', '-static',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
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
        result = build_run(tmp, sources)
        print(result.stdout, end='', flush=True)
        assert result.returncode == 0, (result.returncode, result.stderr)
        if args.mutate:
            for label, name, old, new, assertion in MUTANTS:
                assert sources[name].count(old) == 1, (label, 'anchor missing/ambiguous')
                mutated = dict(sources)
                mutated[name] = sources[name].replace(old, new)
                result = build_run(tmp, mutated)
                assert result.returncode == 1 and 'FAIL ' + assertion in result.stdout, (
                    label, result.returncode, result.stdout, result.stderr)
                print('RED runtime: ' + label + ': ' + result.stdout.strip(), flush=True)
    print('EXIT console_cui_route_host=0')


if __name__ == '__main__':
    main()
