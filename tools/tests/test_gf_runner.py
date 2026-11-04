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
     'if (us > GF_INIT_WAIT_MAX - ctx->wait_us) {\n'
     '        if (ctx->display_script) ctx->last_rc = GF_SCRIPT_ETIME;',
     'if (us > GF_INIT_WAIT_MAX - ctx->wait_us) {\n'
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


def mutation(item):
    script = len(item) == 4
    if script:
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
                                       [command(exe, focused=not script, script=script)], capture_output=True)
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
        return int(bool(errors))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
