#!/usr/bin/env python3
"""Exercise PC-98 and PowerMac display policy against the real display TU."""
from pathlib import Path
import argparse
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]


def observe(work, source, machine="pc98"):
    path = work / 'display.c'
    path.write_text(source)
    exe = work / 'display'
    flags = ['-I' + str(ROOT / 'platform' / machine)]
    if machine == 'powermac':
        # DISPLAY_V2_SHADOW_PUSH comes from 5c-A2 (platform_display_fallback.h);
        # no fallback here, so a missing A2 fails the build (B4 review R2).
        flags += ['-DTEST_POWERMAC']
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-O0', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        *flags, '-I' + str(ROOT / 'gfx'), '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'gfx/v2'),
        '-I' + str(ROOT / 'mm'), '-I' + str(ROOT / 'lib'),
        '-I' + str(ROOT / 'arch/x86'), '-I' + str(ROOT / 'sdk/include/os32'),
        str(path), str(ROOT / 'tools/tests/gfx2_display_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/display.c').read_text()
    mutations = [
        ('pending reference', 'gfx2_scan_ref_get(s);\n    a->head[head].pend_sid = sid;', 'a->head[head].pend_sid = sid;'),
        ('palette bounds', 'count > 256 - first', 'count > 257 - first'),
        ('dirty retained on failure', 'if (rc) return OS32_ERR_IO;\n        s->dirty', 'if (rc) { s->dirty.w = 0; return OS32_ERR_IO; }\n        s->dirty'),
    ]
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        rc = observe(work, source)
        assert rc == 0, f'baseline rc={rc}'
        print('GREEN PC-98 display')
        assert observe(work, source, 'powermac') == 0
        print('GREEN PowerMac display gate and dirty transfer')
        if args.mutate:
            gate = '    if (!DISPLAY_V1_PRESENT_OK()) return OS32_ERR_BUSY;\n'
            present = gate + '    for (u32 i'
            empty = '    if (r <= l || b <= t) return 0;\n'
            assert source.count(present) == source.count(empty) == 1
            late = source.replace(present, '    for (u32 i').replace(empty, empty + gate)
            assert observe(work, late, 'powermac') == 10
            print('RED late present gate: runtime rc=10')
            after_sync = '    if (rc) return rc;\n    l = x > s->dirty.x'
            assert source.count(after_sync) == 1
            late = source.replace(present, '    for (u32 i').replace(
                after_sync, '    if (rc) return rc;\n' + gate + '    l = x > s->dirty.x')
            assert observe(work, late, 'powermac') == 16
            print('RED present gate after sync lookup: runtime rc=16')
            for name, anchor, replacement, rc in [
                ('flip gate', gate + '    gfx2_flip_poll(a);', '    gfx2_flip_poll(a);', 14),
                ('provider fallback', 'if (rc == OS32_ERR_NOSYS) {', 'if (1) {', 11),
                ('partial dirty', 'if (l == s->dirty.x && t == s->dirty.y &&\n'
                 '        r == s->dirty.x + s->dirty.w && b == s->dirty.y + s->dirty.h)', '', 12),
            ]:
                assert source.count(anchor) == 1, name
                assert observe(work, source.replace(anchor, replacement), 'powermac') == rc, name
                print(f'RED {name}: runtime rc={rc}')
            for name, anchor, replacement in mutations:
                assert source.count(anchor) == 1, (name, source.count(anchor))
                mutated = source.replace(anchor, replacement)
                rc = observe(work, mutated)
                assert rc > 0, f'{name}: expected runtime RED, rc={rc}'
                print(f'RED {name}: runtime rc={rc}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
