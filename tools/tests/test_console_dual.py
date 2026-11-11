"""Run CONSOLE_DUAL acceptance and single, source-anchored runtime mutants."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCES = {
    'console_dual.c': 'kernel/console/console_dual.c',
    'display_dual.c': 'platform/pc98/display_dual.c',
    'console.c': 'kernel/console/console.c',
    'route.c': 'drivers/video/pc98/pc98_route.c',
    'kernel.c': 'init/kernel.c',
}
# Each anchor must occur exactly once in the real source. Compile failures do
# not count as RED; every mutant must reach a named runtime assertion.
MUTANTS = [
    ('render gate', 'console.c', '|| con_dual_active()', '', 'A3'),
    ('ACTIVE key', 'display_dual.c',
     '    k = k * 31 + a->index;\n    k = k * 31 + a->gen;\n    k = k * 31 + a->state;\n    k = k * 31 + a->flags;', '', 'A1'),
    ('head key', 'display_dual.c',
     '        k = k * 31 + v.attached;\n        k = k * 31 + v.connector_mask;\n        k = k * 31 + v.mode_id;\n        k = k * 31 + v.scanout_sid;', '', 'A1g'),
    ('late legacy', 'console_dual.c',
     '    if (!cfg || !con_sink_is_enabled() || legacy_active()) return 0;',
     '    if (!cfg || !con_sink_is_enabled()) return 0;\n'
     '    if (legacy_active()) { struct con_out probe[CON_OUT_MAX];\n'
     '        display_dual_outputs(probe, CON_OUT_MAX); return 0; }', 'A1'),
    ('non GF allowed', 'display_dual.c',
     'if (!a || a->kind != GFX2_KIND_GEFORCE)', 'if (!a)', 'A1'),
    ('connector from head', 'display_dual.c',
     'u8 role = a->state == GFX2_AD_OK && v.attached ? CON_OUT_DESK : CON_OUT_NONE;',
     'conn = h;\n        u8 role = a->state == GFX2_AD_OK && v.attached ? CON_OUT_DESK : CON_OUT_NONE;', 'A1g'),
    ('decision DDC', 'display_dual.c', '    u32 k = 0;',
     '    GFX2_ConnectorInfo probe = {0};\n'
     '    if (a) gfx2_connector_info(a->index, 0, &probe);\n    u32 k = 0;', 'A1'),
    ('omit stopped head', 'display_dual.c', '        u8 conn = 0xff;',
     '        if (!v.attached) continue;\n        u8 conn = 0xff;', 'A1g'),
    ('LOST DESK', 'display_dual.c',
     'a->state == GFX2_AD_OK && v.attached ? CON_OUT_DESK : CON_OUT_NONE',
     'v.attached ? CON_OUT_DESK : CON_OUT_NONE', 'A1g'),
    ('text height', 'display_dual.c', '    outp(GDC_GFX_CMD, GDC_CMD_STOP);',
     '    gfx_current_height = 400;\n    outp(GDC_GFX_CMD, GDC_CMD_STOP);', 'A2'),
    ('CUI clear', 'console.c', 'was_gui && !con_dual_was_live()', 'was_gui', 'A3'),
    ('loading sticky', 'kernel.c', 'con_dual_active() || con_dual_was_live()', 'con_dual_active()', 'A3'),
    ('session CLEAR', 'console.c',
     '    if (con_dual_was_live()) {\n        con_sink_push_clear();\n        return;',
     '    if (con_dual_was_live()) {\n        return;', 'A3'),
    ('dual route', 'route.c',
     '    if (con_dual_cfg()) {\n        pc98_console_set_safe(pc98_route_safe());\n        return 0;\n    }', '', 'A4'),
]


def function(source, signature):
    """Extract a complete real function, rejecting missing/ambiguous anchors."""
    assert source.count(signature) == 1, signature
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def build_run(tmp, sources, runner):
    for name in ('console_dual.c', 'display_dual.c', 'console.c'):
        (tmp / name).write_text(sources[name])
    (tmp / 'route.inc').write_text(
        function(sources['route.c'], 'static int route_set(u8 value)') +
        function(sources['route.c'], 'int pc98_route_to_gf(void)'))
    pattern = (r'if \(con_dual_active\(\)(?: \|\| con_dual_was_live\(\))?\)\s*'
               r'shell_print\("Loading gshell\.\.\.\\n", TATTR_GRAY\);\s*else\s*'
               r'tvram_print\(0, 0, "Loading gshell\.\.\.", TATTR_GRAY\);')
    matches = re.findall(pattern, sources['kernel.c'])
    assert len(matches) == 1, 'kernel loading branch anchor'
    (tmp / 'loading.inc').write_text(matches[0] + '\n')
    fixture = ROOT / 'tools/tests/console_dual_host.c'
    exe = tmp / 'console-dual'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O0', '-ffreestanding',
                    '-fno-pie', '-fno-stack-protector', '-ffunction-sections',
                    '-fdata-sections', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-variable', '-Wno-sign-compare',
                    '-D__KERNEL_BUILD__', '-I' + str(tmp), '-I' + str(ROOT),
                    '-I' + str(ROOT / 'tools/tests'), *include_flags(ROOT),
                    '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                    str(fixture), '-o', str(exe)], cwd=ROOT, check=True)
    return host32.run([str(exe)], runner=runner, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--runner', choices=('native', 'qemu'))
    args = parser.parse_args()
    sources = {name: (ROOT / path).read_text() for name, path in SOURCES.items()}
    with tempfile.TemporaryDirectory(prefix='console-dual-') as directory:
        tmp = Path(directory)
        result = build_run(tmp, sources, args.runner)
        print(result.stdout, end='', flush=True)
        assert result.returncode == 0, result.stderr
        if args.mutate:
            for label, name, old, new, acceptance in MUTANTS:
                assert sources[name].count(old) == 1, (label, 'anchor missing/ambiguous')
                mutated = dict(sources, **{name: sources[name].replace(old, new)})
                result = build_run(tmp, mutated, args.runner)
                assert result.returncode == 1 and 'FAIL ' + acceptance in result.stdout, (
                    label, result.returncode, result.stdout, result.stderr)
                print('RED runtime: ' + label + ': ' + result.stdout.strip(), flush=True)
    print('EXIT console_dual_host=0')


if __name__ == '__main__':
    main()
