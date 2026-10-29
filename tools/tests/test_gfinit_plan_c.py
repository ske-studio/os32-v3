"""4c composition: legacy trace, VRAM/AR/DAC and 2c1 CHECK/wait evidence."""
import pathlib
import subprocess
import sys
import tempfile

import host32
import mutpar
import test_gfinit_mode
import test_gfinit_plan

ROOT = pathlib.Path(__file__).resolve().parents[2]


def command(exe):
    return [s.replace('gfinit_plan_host.c', 'gfinit_plan_c_host.c')
            for s in test_gfinit_plan.command(exe)]


def check_output(text, verbose=True):
    assert 'FAIL' not in text and 'C PASS' in text, text
    assert 'A2 -> main0..4 -> baseline required/record/A4 stable/changing GPIO PASS' in text
    assert 'VERIFY default hash/CRC, required targets, explicit override, unmatched selector and mismatch execution PASS' in text
    assert 'VRAM fixed range, required execution, full row order/compare, first mismatch, bus failure and safe-point abort PASS' in text
    assert 'T2 same-session and C-only share ordered writes, waits, sections and verification masks PASS' in text
    assert 'T1 baseline once, three E gates, cold latch, CR52/C040, restore and S0 failure PASS' in text
    assert 'T1 record entry/route/cell required, exit TARGET/VRAM evidence, later bus failure PASS' in text
    assert 'T1 CHECK then S0 local/quiesce/first-send/final-send failure revokes init PASS' in text
    assert 'T1 C-only each lock write/abort restores per-call CR1F/CR11/CR44 originals PASS' in text
    assert 'COLOR order values log and bus failure cleanup PASS' in text
    assert 'CHECK entry/exit match, each mismatch, masks, retained values, REC and failure exit PASS' in text
    assert 'WAIT lengths positions, conditional FP, REC, callback failure and abort PASS' in text
    assert 'ROUTE fixed 01 mask03 match/mismatch/bus failures, exclusive operation and slots94/95 PASS' in text
    assert 'CELL RUN match/mismatch/test-write failure restore once, no HAL restore/redraw PASS' in text
    prefix = next(line for line in text.splitlines() if line.startswith('prefix transport trace events '))
    if verbose:
        print('PASS MODE_C == legacy ' + prefix)
        print('PASS A2/main0..4/baseline: required/record/A4, stable/changing GPIO')
        for line in text.splitlines():
            if line.startswith(('VRAM ', 'COLOR ', 'CHECK ', 'WAIT ', 'ROUTE ', 'CELL ', 'VERIFY ', 'default_', 'REC sid=')):
                print(line)
    groups = text.split('CHECK entry case=')[0].split('variant=')[1:]
    assert len(groups) == 3
    for section, (variant, fp_on) in zip(groups, [(0, True), (1, True), (1, False)]):
        rows, fields = [], {}
        assert int(section.splitlines()[0]) == variant
        for line in section.splitlines()[1:]:
            if '=' not in line:
                continue
            name, values = line.split('=', 1)
            values = list(map(int, values.split()))
            if name == 'row':
                rows.append(values)
            else:
                fields[name] = values
        expected = test_gfinit_mode.expected_trace(test_gfinit_mode.expected_state(variant), fields['pll'])
        # This execution fixture keeps the CR11 just loaded before locking it;
        # the stage-1 expression-only fixture supplies LIVE separately per row.
        lock = max(i for i, row in enumerate(expected) if row[0] == 1 and row[2] == 0x11)
        kind, reg, index, width, _, count = expected[lock]
        expected[lock] = (kind, reg, index, width, 0xab, count)
        assert len(expected) == 206 and len(rows) == 206 - (not fp_on)
        for row, (ordinal, want) in zip(rows, enumerate(expected) if fp_on else
                                      [(i, v) for i, v in enumerate(expected) if i != 1]):
            assert tuple(row[:-1]) == (ordinal, *want), (ordinal, row, want)
            assert row[-1] == 1
        assert fields['entries'] == [7] and fields['captures'] == [39]
        assert fields['pending'] == [0]
        assert fields['nodes'][0] <= 2048 and fields['args'][0] <= 6144
        assert fields['targets'][0] <= 608 and fields['rules'][0] <= 1280
        assert fields['layouts'][0] <= 1280
        if verbose:
            print(f'PASS C bind/bus variant={variant} FP_ON={int(fp_on)}: '
                  f'{len(rows)} rows agree with stage-1 independent oracle; route/cell wired')
            print(' '.join(f'{k}={v[0]}' for k, v in fields.items() if k != 'pll'))


MUTATIONS = [
    ('kdebug/recover.c', 'rmw_destination_sample',
     'if (a->value == w->key) *old = values[i];', '(void)old;'),
    ('kdebug/gf_stage_state.c', 'baseline_once',
     'if (s->initialized) return 0;', 'if (false) return 0;'),
    ('kdebug/gf_stage_state.c', 'entry_pmc',
     'value != s->values[GF_STAGE_PMC]', 'false'),
    ('kdebug/gf_stage_state.h', 'entry_last_s0',
     'r->gf_stage->s0_ok && r->s0_verified', 'r->gf_stage->s0_ok'),
    ('kdebug/recover.c', 'record_cell_required',
     'source != REC_CHECK_TARGET;', 'false;'),
    ('kdebug/recover.c', 'cold_retained',
     'r->cold_latched = r->cold_latched || p->cold_required;', 'r->cold_latched = false;'),

    ('kdebug/recover.c', 'route_readback_mask',
     '((actual ^ RECOVER_ROUTE_RGB_IN) & RECOVER_ROUTE_MASK)',
     '((actual ^ RECOVER_ROUTE_RGB_IN) & 0U)'),
    ('kdebug/recover_profile.c', 'cell_skip_restore',
     'cell_access(r,ops,opaque,run,5,true,&saved)',
     'cell_access(r,ops,opaque,run,5,true,&test)'),
    ('kdebug/recover.c', 'route_callback_scope',
     'r->node_layout[r->cursor]->flags==REC_LAYOUT_MODE_ROUTE &&',
     'r->node_layout[r->cursor]->flags!=REC_LAYOUT_MODE_ROUTE &&'),
    ('hwdev/gf/display/checks.inc', 'check_pll_mask',
     'CHECK_MMIO(GF_PLL_VIDEO,~GF_PLL_VIDEO_KEEP,q[C_PLL_PRIMARY_WRITE].value);',
     'CHECK_MMIO(GF_PLL_VIDEO,0,q[C_PLL_PRIMARY_WRITE].value);'),
    ('hwdev/gf/display/checks.inc', 'check_test_sense',
     'CHECK_MMIO(C_TEST,GF_MODE_TEST_CONTROL,m->test_control);',
     'CHECK_MMIO(C_TEST,~0U,m->test_control);'),
    ('kdebug/recover.c', 'wait_short',
     'recover_delay_ms(r,ms,r->ops->mode_wait,r->opaque);',
     'recover_delay_ms(r,ms-1,r->ops->mode_wait,r->opaque);'),
    ('kdebug/gfinit_bus.c', 'ar_reset',
     'mode_byte(b,base,GF_MODE_AR_STATUS,false,&ignored)', 'mode_byte(b,base,GF_MODE_AR_PORT,false,&ignored)'),
    ('kdebug/gfinit_bus.c', 'dac_palette',
     'gfinit_mode_palette(i,rgb);', 'gfinit_mode_palette(0,rgb);'),
    ('kdebug/recover.c', 'run_uses_c_read_safe',
     't->read_safe && !(w->flags & REC_RUN_NO_PREREAD) && logged_read(r, &a, &old)',
     't->read_safe && logged_read(r, &a, &old)'),
    ('kdebug/gfinit_plan_c.c', 'pll_restore_live',
     'expr=GF_C_EXPR_SAVED;producer=b->pll_first;', 'expr=GF_C_EXPR_LIVE;producer=b->pll_first;'),
    ('kdebug/gfinit_bus.c', 'misc_read_alias',
     'offset += RECOVER_MISC_READ - RECOVER_MISC_WRITE;', 'offset += 0;'),
    ('kdebug/gfinit_plan_c.c', 'vram_last_row',
     'y < GF_MODE_HEIGHT; y++', 'y + 1 < GF_MODE_HEIGHT; y++'),
    ('kdebug/gfinit_bus.c', 'vram_compare',
     '!write && value != wanted && *bad_offset == RECOVER_NONE',
     '!write && value != wanted && *bad_offset != RECOVER_NONE'),
]


def mutation(item):
    file, name, old, new = item
    source = (ROOT / file).read_text()
    if old and source.count(old) != 1:
        return name + ': ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gf-plan-c-mut-') as td:
        exe = pathlib.Path(td) / 'host'
        try:
            tree = mutpar.build_in_tree(ROOT, td, {file: source.replace(old, new, 1) if old else source},
                                       [command(exe)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return name + ': compile failed ' + str(exc.stderr), 1
        # gcc dependency closure must copy the new units, not follow source-tree symlinks.
        for dependency in ('kdebug/gfinit_plan_c.c', 'kdebug/gfinit_plan_c.h',
                           'hwdev/gfinit_mode.h', 'hwdev/generated/gf_mode_rows.inc'):
            assert (tree / dependency).is_file() and not (tree / dependency).is_symlink()
        result = host32.run([str(exe)], cwd=tree, timeout=60, capture_output=True)
        failed = result.returncode != 0
        if not failed:
            try:
                check_output(result.stdout.decode(), verbose=False)
            except AssertionError:
                failed = True
        return name + (': RED' if failed else ': GREEN'), int(not failed)


def main():
    with tempfile.TemporaryDirectory(prefix='gf-plan-c-') as td:
        exe = pathlib.Path(td) / 'host'
        subprocess.run(command(exe), check=True, cwd=ROOT)
        result = host32.run([str(exe)], cwd=ROOT, timeout=60, capture_output=True)
        if result.returncode:
            print(result.stdout.decode())
            return result.returncode
        check_output(result.stdout.decode())
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation, MUTATIONS,
                        ('kdebug/gfinit_plan_c.c', 'control', '', ''))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
