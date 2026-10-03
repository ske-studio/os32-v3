"""Bounds through the real three-backend fixture; no hardware access.
Record: tools/tests/kapi_bounds_tdd.md.
"""
import argparse
from pathlib import Path
import test_gfx_kernel_fb as base
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
 ('pegc', 'gfx/backend_pegc.c',
  '    if (!gfx_clip_screen(&x, &y, &w, &h, PEGC_WIDTH, PEGC_HEIGHT_480)) return;',
  '''    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > PEGC_WIDTH) w = PEGC_WIDTH - x;
    if (y + h > PEGC_HEIGHT_480) h = PEGC_HEIGHT_480 - y;
    if (w <= 0 || h <= 0) return;'''),
 ('cirrus', 'gfx/backend_cirrus.c',
  '    return gfx_clip_screen(x, y, w, h, CIRRUS_WIDTH, CIRRUS_HEIGHT);',
  '''    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > CIRRUS_WIDTH) *w = CIRRUS_WIDTH - *x;
    if (*y + *h > CIRRUS_HEIGHT) *h = CIRRUS_HEIGHT - *y;
    return *w > 0 && *h > 0;'''),
 ('scroll', 'gfx/gfx_scroll.c', 'vram_scroll_y += lines % gfx_current_height;',
  'vram_scroll_y += lines;'),
]

def run(runner, mutation=None):
    body = (ROOT/'tools/tests/gfx_kernel_fb_host.c').read_text()
    body = body.replace('static void run(void)\n{',
                        '#include "gfx_bounds_cases.inc"\nstatic void run(void)\n{')
    body = body.replace('        display_check(selected);',
                        '        bounds_test(selected);\n        display_check(selected);')
    # Actual Cirrus command arguments must stay within the card window.
    old='(void)g;(void)d;(void)s;(void)dp;(void)sp;(void)w;(void)h;return 0;'
    body=body.replace(old,'CHECK(w>0 && w<=GFX_WIDTH && h>0 && h<=(int)MEM_GFX_BB8_HEIGHT);'+old)
    p=base.run_case(None, runner, extra_changes=([mutation[1:]] if mutation else ()),
                    fixture_body='unsigned int host_arch_if = 0x202U;\n'+body)
    if mutation:
        expected = {'pegc': (-11, ''), 'cirrus': (1, 'FAIL w>0'),
                    'scroll': (1, 'FAIL vram_scroll_y ==')}[mutation[0]]
        assert p.returncode == expected[0] and expected[1] in p.stdout, (mutation[0], p.returncode, p.stdout, p.stderr)
        print('RED', mutation[0], p.returncode, p.stdout.strip())
    else:
        assert p.returncode==0, (p.returncode,p.stdout,p.stderr)
        print(p.stdout.strip())

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--runner', choices=('native','qemu'))
    parser.add_argument('--mutate', action='store_true')
    args=parser.parse_args()
    run(args.runner)
    if args.mutate:
        for m in MUTATIONS: run(args.runner,m)
        print('PASS 3/3 runtime mutations')
