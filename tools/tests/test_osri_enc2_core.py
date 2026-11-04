"""C/Python bit agreement for both-sided planning, timing and signed FS."""
CHECK = dict(name='osri-enc2-core', tier=1, needs=['make:osri-enc2-core'], budget=180,
             covers=['tools/osri_enc2_core.c', 'tools/osri_enc2_native.py',
                     'build/assets.mk', 'tools/osri_enc2_spec.py',
                     'tools/osri_enc2_plan.py', 'tools/osri_enc2_master.py',
                     'tools/osri_enc2_tree.py', 'tools/osri_enc2_row.py',
                     'tools/osri_enc2_order.py', 'tools/osri_enc2_timing.py',
                     'tools/osri_enc2_dither.py', 'tools/osri_enc2_pack.py',
                     'tools/img2osri.py', 'tools/osri_enc2_compare.py',
                     'tools/osri_enc2_decode.py', 'tools/osri_enc2_metrics.py'])

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
from osri_enc2_row import RowSearch
from osri_enc2_plan import prepare, plan, objective
from osri_enc2_dither import quantize
from osri_enc2_pack import pack
from osri_enc2_native import plan as native_plan, quantize as c_quantize


def c_plan(*args, **kwargs):
    return native_plan(*args, threads=1, **kwargs)


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
        for threads in (2, 4, 8, 16):
            mp, mo, ms = native_plan(pixels, budget, cfg, prepared, threads=threads)
            assert np.array_equal(c_p, mp) and c_o == mo, ('parallel palette/order', budget, threads)
            for key in ('j_trace', 'visits', 'evaluations'):
                assert c_s[key] == ms[key], ('parallel', key, budget, threads)
            assert np.array_equal(c_quantize(pixels, c_p, c_o, cfg),
                                  c_quantize(pixels, mp, mo, cfg))

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

    # A retained changed slot needs Lloyd even when the tree has no improving
    # exchange. The tree offers only 111; the master/centroid can reach 222.
    source = np.full((2, 8, 3), 34, dtype=np.int32)
    old = np.zeros((16, 3), dtype=np.int32)
    seed = old.copy(); seed[0] = 1
    config = Config(row_iters=1, row_moves=1, lookahead_rows=0)
    search = RowSearch(source, 1, old, None, (), 3,
                       components([0x222]), components([0x111]), config)
    refined = search.search(seed)
    assert np.array_equal(refined[0], [2, 2, 2])
    assert np.array_equal(refined[1:], old[1:])

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

    with tempfile.TemporaryDirectory(prefix='enc2-trace-test-') as name:
        import json
        trace = Path(name) / 'rows.jsonl'
        a, ao, ast = c_plan(full, 31, default, prepared, {198}, trace_path=trace)
        assert np.array_equal(a, b) and ao == bo
        assert ast['j_trace'] == bst['j_trace']
        records = [json.loads(line) for line in trace.read_text().splitlines()]
        assert any(r['stage'] == 'forward' and r['y'] == 198 for r in records)
        assert any(r['stage'] == 'exchange-stationary' for r in records)
        assert all(r['OUT'] <= 31 and r['next_OUT'] <= 31 for r in records if 'OUT' in r)
        decisions = [r for r in records if r['stage'] == 'visit-decision']
        assert decisions and all(r['J_before'] >= r['J_after'] for r in decisions)
        assert sum(r['J_before'] - r['J_after'] for r in decisions) == bst['j_trace'][0] - bst['j_trace'][-1]
        assert ast['evaluations'] == bst['evaluations']

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
    # Exercise the public one-file CLI, including transparent black composition,
    # 640x400 padding, the fixed B=31 contract and empty parallel batches.
    with tempfile.TemporaryDirectory(prefix='enc2-cli-test-') as name:
        from PIL import Image
        directory = Path(name)
        source = directory / 'transparent.png'
        Image.new('RGBA', (1, 1), (255, 0, 0, 0)).save(source)
        expected = pack(np.zeros((400, 16, 3), dtype=np.int32), [()] * 400,
                        np.zeros((400, 640), dtype=np.uint8), 31)
        for threads in (1, 4):
            output = directory / f'output-{threads}.osri'
            result = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/osri_enc2_native.py'),
                                     str(source), str(output), '--threads', str(threads)],
                                    capture_output=True, text=True)
            assert result.returncode == 0, result.stderr
            assert output.read_bytes() == expected
        invalid = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/osri_enc2_native.py'),
                                  str(source), str(output), '--threads', '0'],
                                 capture_output=True, text=True)
        assert invalid.returncode and '--threads must be positive' in invalid.stderr
    print('enc2 C/Python: GREEN', flush=True)


MUTATIONS = [
    ('completion-order', 'int result_index=i;', 'int result_index=batch->ids[pool.completed];'),
    ('drop-candidate', 'pool_run(n,candidate_job,&batch);',
     'pool_run(n-1,candidate_job,&batch),memset(&values[ids[n-1]],0,sizeof(*values));'),
    ('component-out', 'return components + (slots ? slots-1 : 0);', 'return slots;'),
    ('next-delta', 'value.score+=SPILL_WEIGHT*(next-s->next_ideal);', 'value.score+=0*(next-s->next_ideal);'),
    ('partial-mask', '(mask&(1<<c))?candidates[i]:current', 'candidates[i]'),
    ('lloyd-after-exchanges', '*result=lloyd(s,p,movable,nm);', '*result=p; (void)movable; (void)nm;'),
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
    im=allocate(sizeof(*im));
    im->cfg=(Config){.h=2,.w=8,.budget=3,.moves=1,.iters=1};
    for(int j=0;j<68;j++) im->cfg.cuts[j]=cut(&im->cfg,j);
    im->pixels=allocate(2*8*3);memset(im->pixels,34,2*8*3);
    im->nm=1;im->master[0]=0x222;im->nc[1]=1;im->candidates[1][0]=0x111;
    Palette old={{0}},seed={{0x111}},result;
    s=search_new(im,1,&old,NULL,empty,NULL,empty);
    assert(search(s,&seed,0,&result));
    assert(result.q[0]==0x222);
    for(int i=1;i<SLOTS;i++) assert(!result.q[i]);
    search_free(s);free(im->pixels);free(im);
    /* Check every result, even a candidate which cannot win the search. */
    pool_init(4);
    im=allocate(sizeof(*im));im->cfg=(Config){.h=1,.w=8,.budget=47};
    im->pixels=allocate(8*3);
    for(int x=0;x<8;x++) for(int c=0;c<3;c++) im->pixels[3*x+c]=(x*47+c*71)%256;
    Palette base={{0}};s=search_new(im,0,NULL,NULL,empty,NULL,empty);
    Neighbors nb;neighbors(s,&base,&nb);
    Proposal prop[64];Value values[64];
    for(int i=0;i<64;i++) prop[i]=(Proposal){.q=i+1};
    evaluate_candidates(s,&base,0,prop,64,&nb,values);
    for(int i=0;i<64;i++) {
        Palette trial=base;trial.q[0]=prop[i].q;
        Value expected=evaluate_raw(s,&trial,NULL,NULL);
        assert(values[i].valid==expected.valid && values[i].score==expected.score);
        assert(!order_cmp(&values[i].order,&expected.order));
        assert(!order_cmp(&values[i].next,&expected.next));
    }
    assert(im->evaluations==64);
    /* One exchange stage retains candidate IDs across every physical slot
     * and mixes cached candidates with fresh evaluations. */
    Proposal stage_prop[16*64];Value stage_values[16*64];int slots[16*64];
    for(int slot=0;slot<SLOTS;slot++) for(int i=0;i<64;i++) {
        int index=slot*64+i;stage_prop[index]=prop[i];slots[index]=slot;
    }
    evaluate_stage(s,&base,slots,stage_prop,16*64,&nb,stage_values);
    for(int i=0;i<16*64;i++) {
        Palette trial=base;trial.q[slots[i]]=stage_prop[i].q;
        Value expected=evaluate_raw(s,&trial,NULL,NULL);
        assert(stage_values[i].valid==expected.valid && stage_values[i].score==expected.score);
        assert(!order_cmp(&stage_values[i].order,&expected.order));
        assert(!order_cmp(&stage_values[i].next,&expected.next));
    }
    assert(im->evaluations==16*64);
    /* Cached stage: no jobs, no new cache publications. */
    evaluate_stage(s,&base,slots,stage_prop,16*64,&nb,stage_values);
    assert(im->evaluations==16*64);
    /* Duplicated proposals still publish only one entry per palette. */
    stage_prop[0]=stage_prop[1];
    evaluate_stage(s,&base,slots,stage_prop,16*64,&nb,stage_values);
    assert(stage_values[0].score==stage_values[1].score);
    assert(im->evaluations==16*64);
    search_free(s);
    /* A wholly invalid exchange stage must neither wake workers nor scan
     * pixels; its invalid values still enter the cache deterministically. */
    im->cfg.budget=0;
    s=search_new(im,0,&base,NULL,empty,NULL,empty);
    evaluate_stage(s,&base,slots,stage_prop,16*64,&nb,stage_values);
    for(int i=0;i<16*64;i++) assert(!stage_values[i].valid);
    assert(im->evaluations==2*16*64-1);
    search_free(s);free(im->pixels);free(im);pool_free();
    return 0;
}
'''


def compile_core(source, binary):
    subprocess.run(['gcc', '-std=c11', '-O3', '-Wall', '-Wextra', '-Werror', '-Wvla', '-pthread',
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
