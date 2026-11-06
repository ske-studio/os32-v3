"""Runner/diagnostic adapter versus frozen pre-P6 traces and bounded controls."""
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
import mutpar

ROOT = Path(__file__).resolve().parents[2]


def command(exe, focused=False, script=False):
    return [
        'gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Werror=vla',
        '-m32', '-march=i386', '-O2', '-ffreestanding', '-fno-pie',
        '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
        *(['-DGF_RUNNER_TRACE_ONLY'] if focused else []),
        *['-I' + str(ROOT / d) for d in ('.', 'include', 'sdk/include/os32')],
        str(ROOT / ('tools/tests/gf4_c2linit_host.c' if script else 'tools/tests/gf_runner_host.c')),
        *([str(ROOT / p) for p in ('hwdev/gf/runner.c', 'hwdev/gf/save.c',
                                   'hwdev/gf/pll.c', 'kdebug/gfinit_decode.c',
                                   'kdebug/gfinit_pll.c')] if script else []),
        *map(str, sorted((ROOT / 'hwdev/gf/op').glob('*.c'))), '-o', str(exe)
    ]


MUTATIONS = [
    ('diagnostic_a2_skipped', 'gf_init_a2(&e->init,runner_decode_safe)', '((void)runner_decode_safe,0)'),
    ('diagnostic_runner_skipped', 'int rc = gf_init_run_one(&e->init,e->program);', 'int rc = 0;'),
    ('diagnostic_restore_omitted', 'return e && e->recover ? gf_init_restore(&e->init) : 0;', '(void)e; return 0;'),
]


SCRIPT_MUTATIONS = [
    ('script_wait_budget_eio', 'hwdev/gf/runner.c',
     'if (us > GF_INIT_WAIT_MAX - (ctx->opcode_timing ? 0 : ctx->wait_us)) {\n'
     '        if (ctx->display_script) ctx->last_rc = GF_SCRIPT_ETIME;',
     'if (us > GF_INIT_WAIT_MAX - (ctx->opcode_timing ? 0 : ctx->wait_us)) {\n'
     '        if (ctx->display_script) ctx->last_rc = GF_SCRIPT_EIO;'),
    ('script_step_budget_eio', 'hwdev/gf/runner.c',
     'if (ctx->steps == GF_INIT_SCHEDULE) {\n'
     '            if (ctx->display_script) ctx->last_rc = GF_SCRIPT_ETIME;',
     'if (ctx->steps == GF_INIT_SCHEDULE) {\n'
     '            if (ctx->display_script) ctx->last_rc = GF_SCRIPT_EIO;'),
    ('script_head0', 'hwdev/gf/runner.c', 'ctx->head = head; ctx->pc = pc;',
     '(void)head; ctx->head = 0; ctx->pc = pc;'),
    ('script_or1', 'hwdev/gf/op/tmds_reg.c', 'offsets[ctx->or_mask]', 'offsets[1]'),
    ('script_deadline_shift', 'hwdev/gf/runner.c', 'ctx->deadline = dl;',
     'ctx->deadline = dl + GF_INIT_WAIT_CHUNK_US;'),
    ('script_cleanup_omitted', 'hwdev/gf/runner.c',
     '(void)ctx->io.restore(ctx->io.opaque, ctx);', '(void)ctx;'),
    ('script_group_order', 'hwdev/gf/op/zm_tmds_group.c',
     'base + GF_TMDS_DATA, 4, pair[1]) ||\n            ctx->io.write(ctx->io.opaque, 0, base, 4, pair[0])',
     'base, 4, pair[0]) ||\n            ctx->io.write(ctx->io.opaque, 0, base + GF_TMDS_DATA, 4, pair[1])'),
    ('script_zero_sub_call', 'hwdev/gf/op/sub.c',
     'ctx->skip || (ctx->display_script && !insn->ref)', 'ctx->skip'),
    ('script_other_bank', 'hwdev/gf/op/tmds_reg.c',
     'offset ^= GF_TMDS_OTHER_BANK;', 'offset |= GF_TMDS_OTHER_BANK;'),
]


# name, product source, unique anchor, replacement, runtime RED case.
SPLIT_MUTATIONS = [
    ('split_prepare_executes', 'hwdev/gf/runner.c',
     'if (init_point(ctx)) goto fail;\n    /* last_rc stays EIO',
     'if (init_point(ctx) || init_execute(ctx, out)) goto fail;\n    /* last_rc stays EIO',
     'split_cases: prepare has no I/O/steps/wait'),
    ('split_execute_steps_reset', 'hwdev/gf/runner.c',
     'if (init_execute(ctx, program)) return init_script_failure(ctx);',
     'ctx->steps = 0; if (init_execute(ctx, program)) return init_script_failure(ctx);',
     'split_cases: exhausted outer step budget'),
    ('split_execute_wait_reset', 'hwdev/gf/runner.c',
     'if (init_execute(ctx, program)) return init_script_failure(ctx);',
     'ctx->wait_us = 0; if (init_execute(ctx, program)) return init_script_failure(ctx);',
     'split_cases: accumulated wait budget'),
    ('split_execute_elapsed_reset', 'hwdev/gf/runner.c',
     'if (init_execute(ctx, program)) return init_script_failure(ctx);',
     'ctx->elapsed_us = 0; if (init_execute(ctx, program)) return init_script_failure(ctx);',
     'split_cases: clock failure preserves elapsed'),
    ('split_execute_stale_state', 'hwdev/gf/runner.c',
     'init_script_context(ctx, pc, head, or_mask, dl);\n    /* The owner supplies',
     '(void)head; (void)or_mask; (void)dl;\n    /* The owner supplies',
     'split_cases: stale calls/repeats/skip and head/OR/deadline'),
    ('split_execute_redecode', 'hwdev/gf/runner.c',
     'if (init_execute(ctx, program)) return init_script_failure(ctx);',
     'if (gf_init_script_prepare(ctx, (struct gf_init_program *)program, pc, head, or_mask, dl) || '
     'init_execute(ctx, program)) return init_script_failure(ctx);',
     'split_cases: outer budget remains charged'),
    ('split_wrapper_double_restore', 'hwdev/gf/runner.c',
     'if (rc) return rc;\n    return gf_init_script_execute',
     'if (rc) { (void)ctx->io.restore(ctx->io.opaque, ctx); return rc; }\n'
     '    return gf_init_script_execute',
     'split_cases: wrapper preparation failure restores once'),
    ('split_cleanup_replaces_rc', 'hwdev/gf/runner.c',
     'ctx->last_rc = rc;\n    return rc;',
     '(void)rc; return ctx->last_rc;',
     'split_cases: timeout survives failing cleanup'),
]


def mutation(item):
    split = len(item) == 5
    script = len(item) == 4
    if split:
        name, path, old, new, _case = item
    elif script:
        name, path, old, new = item
    else:
        name, old, new = item
        path = 'kdebug/gfinit_exec.c'
    source = (ROOT / path).read_text()
    if old and source.count(old) != 1:
        return name + ': ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gf-runner-mut-') as td:
        exe = Path(td) / 'host'
        try:
            tree = mutpar.build_in_tree(ROOT, td, {path: source.replace(old, new, 1) if old else source},
                                       [command(exe, focused=not (script or split), script=script)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return name + ': compile failed ' + str(exc.stderr), 1
        result = host32.run([str(exe)], cwd=tree, timeout=120, capture_output=True)
        return name + (': RED' if result.returncode else ': GREEN'), int(not result.returncode)


def main():
    with tempfile.TemporaryDirectory(prefix='gf-runner-') as td:
        exe = Path(td) / 'host'
        subprocess.run(command(exe), cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=120)
        if result.returncode:
            return result.returncode
        subprocess.run(command(exe, script=True), cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=120)
        if result.returncode:
            return result.returncode
    if '--mutate' in sys.argv:
        errors = mutpar.run_with_control(mutation, MUTATIONS, ('control', '', ''))
        errors += mutpar.run_with_control(mutation, SCRIPT_MUTATIONS,
                                          ('script-control', 'hwdev/gf/runner.c', '', ''))
        errors += mutpar.run_with_control(mutation, SPLIT_MUTATIONS,
                                          ('split-control', 'hwdev/gf/runner.c', '', '', 'control'))
        return int(bool(errors))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
