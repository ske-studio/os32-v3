"""C/Python bit agreement for both-sided planning, timing and signed FS."""
CHECK = dict(name='osri-enc2-core', tier=1, needs=['make:osri-enc2-core'], budget=180,
             covers=['tools/osri_enc2_core.c', 'tools/osri_enc2_native.py',
                     'build/assets.mk', 'tools/osri_enc2_spec.py',
                     'tools/osri_enc2_plan.py', 'tools/osri_enc2_master.py',
                     'tools/osri_enc2_tree.py', 'tools/osri_enc2_row.py',
                     'tools/osri_enc2_order.py', 'tools/osri_enc2_timing.py',
                     'tools/osri_enc2_dither.py', 'tools/osri_enc2_pack.py'])

import argparse
from dataclasses import replace
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from osri_enc2_spec import BUDGETS, Config, components, allowed
from osri_enc2_plan import prepare, plan, objective
from osri_enc2_dither import quantize
from osri_enc2_pack import pack
from osri_enc2_native import plan as c_plan, quantize as c_quantize


def check():
    if os.environ.get('ENC2_SELFTEST'):
        result = subprocess.run([os.environ['ENC2_SELFTEST']], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
    # Broad colors, duplicated/empty centers, short lookahead and both neighbors.
    pixels = np.array([[[((x * 47 + y * 89 + c * 71) % 256) for c in range(3)]
                        for x in range(12)] for y in range(5)], dtype=np.int32)
    cfg = Config(master_iters=2, row_moves=2, row_iters=2, round_trips=2,
                 start_delay_ns=7000, pixel0_offset_ns=-900)
    prepared = prepare(pixels, cfg)
    for budget in BUDGETS:
        py_p, py_o, py_s = plan(pixels, budget, cfg, prepared)
        c_p, c_o, c_s = c_plan(pixels, budget, cfg, prepared)
        assert np.array_equal(py_p, c_p), ('palettes', budget, py_p, c_p)
        assert py_o == c_o, ('orders', budget, py_o, c_o)
        assert py_s['j_trace'] == c_s['j_trace'], ('J', budget)
        assert py_s['visits'] == c_s['visits'], ('visits', budget)
        assert py_s['evaluations'] == c_s['evaluations'], ('cache', budget)
        assert all(a >= b for a, b in zip(c_s['j_trace'], c_s['j_trace'][1:]))
        assert all(allowed(a, b, budget) for a, b in zip(c_p, c_p[1:]))
        for timed in (False, True):
            for dither in (False, True):
                a = quantize(pixels, py_p, py_o, cfg, timed=timed, dither=dither)
                b = c_quantize(pixels, c_p, c_o, cfg, timed=timed, dither=dither)
                assert np.array_equal(a, b), ('FS', budget, timed, dither)
        # Repeat the complete search, preserving all physical slots and ties.
        p2, o2, s2 = c_plan(pixels, budget, cfg, prepared)
        assert np.array_equal(c_p, p2) and c_o == o2 and c_s['visits'] == s2['visits']

    # A deliberately weak root seed and one move per visit force real backward
    # and forward updates, rather than testing an already stationary solution.
    changing = np.array([[[x * 34] * 3 for x in range(8)]
                         for _ in range(4)], dtype=np.int32)
    config = Config(row_iters=0, row_moves=1, round_trips=2, lookahead_rows=1)
    colors, roots, stats = prepare(changing, config)
    weak_roots = []
    for i, node in enumerate(roots):
        leaf = replace(node, id=2*i, color=np.full(3, 2*(i % 8), dtype=np.int32), children=())
        weak_roots.append(replace(node, color=np.zeros(3, dtype=np.int32),
                                  children=(leaf, replace(leaf, id=2*i+1))))
    prepared = colors, weak_roots, stats
    ap, ao, ast = plan(changing, 15, config, prepared)
    bp, bo, bst = c_plan(changing, 15, config, prepared)
    assert ast['visits'] and ast['j_trace'][0] > ast['j_trace'][-1]
    assert np.array_equal(ap, bp) and ao == bo
    assert ast['j_trace'] == bst['j_trace'] and ast['visits'] == bst['visits']

    # Default settings on a 400-row fixture, with the S1 active-row mechanism.
    # The other 398 rows and both adjacent boundary constraints remain present.
    y, x, c = np.indices((400, 8, 3))
    full = (((x + y // 100 + c) % 3) * 93).astype(np.int32)
    default = Config()
    prepared = prepare(full, default)
    a, ao, ast = plan(full, 31, default, prepared, {198})
    b, bo, bst = c_plan(full, 31, default, prepared, {198})
    assert np.array_equal(a, b) and ao == bo
    assert ast['j_trace'] == bst['j_trace'] and ast['visits'] == bst['visits']
    assert objective(full, b, bo) == bst['j_trace'][-1]
    # Serialize the entire 640x400 image on both sides through Python's v2 packer.
    wide = np.repeat(full, 80, axis=1)
    ai = quantize(wide, a, ao)
    bi = c_quantize(wide, b, bo)
    assert np.array_equal(ai, bi)
    assert pack(a, ao, ai, 31) == pack(b, bo, bi, 31)

    # Edge widths, equality at a completion time, signed truncation, slot ties,
    # deliberately nonascending orders, missing downwards error and ideal mode.
    q = np.zeros((4, 16, 3), dtype=np.int32)
    q[0, 1] = 15
    q[1:] = q[0]
    q[1:, 9] = [8, 15, 3]; q[1:, 2] = [15, 1, 13]
    q[2:, 5] = [2, 8, 15]
    orders = [(), (9, 2), (5,), ()]
    for width in (1, 2, 8, 40, 640):
        yy, xx, cc = np.indices((4, width, 3))
        rgb = ((xx * 43 + yy * 71 + cc * 97) % 256).astype(np.int32)
        for delay, offset in ((575, 0), (5250, 0), (5901, -700), (12000, 900)):
            config = replace(cfg, start_delay_ns=delay, pixel0_offset_ns=offset)
            for timed in (False, True):
                for dither in (False, True):
                    expected = quantize(rgb, q, orders, config, timed=timed, dither=dither)
                    actual = c_quantize(rgb, q, orders, config, timed=timed, dither=dither)
                    assert np.array_equal(actual, expected), ('edge FS', width, delay, offset, timed, dither)
    black = np.zeros((1, 1, 3), dtype=np.int32)
    duplicate = np.zeros((1, 16, 3), dtype=np.int32)
    assert c_quantize(black, duplicate, [()])[0, 0] == 0
    # One row, no moves/iterations/trips, no following row, zero OUT budget.
    for source in (black, pixels[:1]):
        config = Config(row_iters=0, row_moves=0, round_trips=0, lookahead_rows=0)
        prepared = prepare(source, config)
        a, ao, _ = plan(source, 0, config, prepared)
        b, bo, _ = c_plan(source, 0, config, prepared)
        assert np.array_equal(a, b) and ao == bo
    print('enc2 C/Python: GREEN', flush=True)


MUTATIONS = [
    ('component-out', 'return components + (slots ? slots-1 : 0);', 'return slots;'),
    ('next-delta', 'value.score+=SPILL_WEIGHT*(next-s->next_ideal);', 'value.score+=0*(next-s->next_ideal);'),
    ('partial-mask', '(mask&(1<<c))?candidates[i]:current', 'candidates[i]'),
    ('runner-up', 'n->nearest[t]==slot?n->second[t]:n->first[t]', 'n->first[t]'),
    ('pixel-ceil', 'if(numerator>0 && numerator%ACTIVE_NS) x++;', 'if(0) x++;'),
    ('order-timing', 'int slot=im->orders[y].slot[pos];if(pos) j++;', 'int slot=pos;if(pos) j++;'),
    ('signed-error', 'error[x+2][c]+=e*7/16;', 'error[x+2][c]+=(e*7-15*(e<0))/16;'),
    ('down-error', 'memcpy(error,nxt,sizeof(error));', 'memset(error,0,sizeof(error));'),
    ('index-tie', 'if(d<best) {best=d;id=i;}\n            }', 'if(d<=best) {best=d;id=i;}\n            }'),
]


# Directly exercise the S1 next-row delta counterexample through the C function,
# without adding a production protocol/API just for unit tests.
HARNESS = r'''#define main enc2_program_main
#include "core.c"
#undef main
#include <assert.h>
int main(void) {
    Image *im=allocate(sizeof(*im));
    im->cfg=(Config){.h=3,.w=16,.budget=47,.lookahead=3,.passes=2,.delay=6000};
    for(int j=0;j<68;j++) im->cfg.cuts[j]=cut(&im->cfg,j);
    im->pixels=allocate(3*16*3);
    memset(im->pixels+2*16*3,255,16*3);
    Palette p={{0}},following={{0}};
    for(int i=0;i<12;i++) following.q[i]=0xfff;
    Order reversed={.n=12};for(int i=0;i<12;i++) reversed.slot[i]=11-i;
    Order empty={0};
    Search *s=search_new(im,1,&p,&following,reversed,NULL,empty);
    Value v=evaluate(s,&p,NULL,NULL);
    /* F=4*16*3*255^2. For the next row slot 0 wins the white tie.
     * Count its still-black components at every pixel independently. */
    int boundaries[3]={0},j=0;
    for(int pos=0;pos<v.next.n;pos++) {
        int slot=v.next.slot[pos];if(pos) j++;
        for(int k=0;k<3;k++) {int c=component_order[k];j++;
            if(slot==0) boundaries[c]=cut(&im->cfg,j);
        }
    }
    int64_t actual=0;
    for(int x=0;x<16;x++) for(int c=0;c<3;c++) if(x<boundaries[c]) actual+=255*255;
    assert(actual>0);
    assert(v.score==INT64_C(4)*16*3*255*255+8*actual);
    search_free(s);free(im->pixels);free(im);
    return 0;
}
'''


def compile_core(source, binary):
    subprocess.run(['gcc', '-std=c11', '-O3', '-Wall', '-Wextra', '-Werror', '-Wvla',
                    str(source), '-o', str(binary)], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    if os.environ.get('ENC2_CORE'):
        check()
        return
    with tempfile.TemporaryDirectory(prefix='enc2-core-test-') as name:
        directory = Path(name)
        source, binary = directory / 'core.c', directory / 'core'
        harness = directory / 'selftest.c'
        harness.write_text(HARNESS)
        selftest = directory / 'selftest'
        original = (ROOT / 'tools/osri_enc2_core.c').read_text()
        source.write_text(original)
        compile_core(source, binary)
        compile_core(harness, selftest)
        env = dict(os.environ, ENC2_CORE=str(binary), ENC2_SELFTEST=str(selftest))
        command = [sys.executable, '-B', __file__]
        subprocess.run(command, env=env, check=True)
        if args.mutate:
            for label, old, new in MUTATIONS:
                assert original.count(old) == 1, label
                source.write_text(original.replace(old, new))
                compile_core(source, binary)  # A compile failure never counts as RED.
                compile_core(harness, selftest)
                result = subprocess.run(command, env=env, capture_output=True, text=True)
                assert result.returncode and 'AssertionError' in result.stderr, (label, result.stderr)
                print(label, 'runtime RED', result.stderr.splitlines()[-1][:180], flush=True)
            source.write_text(original)
            compile_core(source, binary)
            compile_core(harness, selftest)
            subprocess.run(command, env=env, check=True)


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_osri_enc2_core.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)
