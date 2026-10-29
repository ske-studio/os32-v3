"""S3 complete mock traces, bounded execution and fail-stop; no external ROM."""
import pathlib
import subprocess
import sys
import tempfile
import host32
import mutpar

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-m32', '-march=i386',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector']


def command(exe, probe=False):
    return ['gcc', *FLAGS, *(['-DGF_VPLL_PROBE=1'] if probe else []), '-O2', '-nostdlib', '-static', '-no-pie',
            '-I'+str(ROOT/'include'), str(ROOT/'tools/tests/gfinit_exec_host.c'), '-o', str(exe)]


MUTATIONS = [
    ('gf_pre_begin_ignored', 'if (recover_gf_pre(r, n, "BEGIN", n->substep < GF_INIT_K, 0)) return -1;', '(void)recover_gf_pre(r, n, "BEGIN", n->substep < GF_INIT_K, 0);'),
    ('gf_pre_end_omitted', 'int log = recover_gf_pre(r, n, "END", n->substep < GF_INIT_K, rc);', 'int log = 0;'),
    ('call_return', 'e->call[e->calls-1].pc != n->pc || e->call[e->calls-1].ctx != n->ctx', 'false'),
    ('repeat_return', 'n->ctx != f->ctx || a[0].value != f->count', 'false'),
    ('main_frames', 'return e->calls || e->repeats ? -1 : 0;', 'return 0;'),
    ('unresolved_exec', '!r->plan || r->plan->unresolved_count ||', '!r->plan ||'),
    ('steps', 'cost > GF_INIT_SCHEDULE - e->steps', 'false'),
    ('wait_budget', 'us > GF_INIT_WAIT_MAX - e->wait_us', 'false'),
    ('exec_timeout_reason', 'r->reason = REC_TIMEOUT;', 'r->reason = REC_OPERATION;'),
    ('exec_deadline', 'e->elapsed_us >= GF_INIT_EXEC_US', 'false'),
    ('call_depth', 'e->calls == GF_INIT_CALL_DEPTH', 'e->calls > GF_INIT_CALL_DEPTH'),
    ('repeat_depth', 'e->repeats == GF_INIT_REPEAT_DEPTH', 'e->repeats > GF_INIT_REPEAT_DEPTH'),
    ('condition_mask', '(e->read_value & a[1].value) == a[2].value', '(e->read_value & a[1].value) != a[2].value'),
    ('selection', '>> a[1].value) == a[2].value;', '>> a[1].value) != a[2].value;'),
    ('io_flag', '& a[4].value) == a[5].value', '& a[4].value) != a[5].value'),
    ('skip_time', 'if (n->substep >= GF_INIT_K) return 0;', 'if (false && n->substep >= GF_INIT_K) return 0;'),
    ('wait_failure', 'e->ops->wait_us(e->opaque, part) ||', '(e->ops->wait_us(e->opaque, part) && false) ||'),
]
PLANNED = '        if (planned) *value = expected;\n'
PRECHECK = """        if (!w || t->access == REC_UNCLASSIFIED || (n && !node_rule(n, w)) ||
            (dir == REC_DATA && r->port_selected[r->index_port[target]] != target))
            return access_error(r, target);
"""
SHARED_MUTATIONS = [
    ('kdebug/recover_plan/access.inc', 'vpll_read', '|| (r->profile->version == RECOVER_PROFILE_VPLL_VERSION && n &&', '|| (false && r->profile->version == RECOVER_PROFILE_VPLL_VERSION && n &&'),
    ('kdebug/recover_common/session.inc', 'timeout_reason', 'r->reason == REC_TIMEOUT ? REC_TIMEOUT : REC_OPERATION', 'REC_OPERATION'),
    ('kdebug/recover_plan/access.inc', 'pre_read_order', PRECHECK + "        if (w->write_mask", "        if (!w) return access_error(r, target);\n        if (w->write_mask"),
    ('kdebug/recover_plan/bind.inc', 'fixed_head', 'if (w->value.mask != width_mask(t->width)) return -1;', 'if (false && w->value.mask != width_mask(t->width)) return -1;'),
    ('kdebug/recover_plan/access.inc', 'planned_value', 'if (planned) *value = expected;', 'if (planned) *value = 0;'),
]


def mutation(item):
    file, name, old, new = item
    source = (ROOT/file).read_text()
    if old and source.count(old) != 1:
        return f'{name}: ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='gfinit-mut-') as td:
        exe = pathlib.Path(td)/'host'
        try:
            mutated = source.replace(old, new, 1) if old else source
            if name == 'pre_read_order':
                mutated = mutated.replace(PLANNED, PLANNED + PRECHECK, 1)
            tree = mutpar.build_in_tree(ROOT, td, {file: mutated},
                                       [command(exe, name in ("vpll_read", "vpll_repeat_drop", "vpll_verify_zero"))], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return f'{name}: compile failed {exc.stderr!r}', 1
        result = host32.run([str(exe)], cwd=tree, timeout=60, capture_output=True)
        return f'{name}: {"GREEN" if result.returncode == 0 else "RED"} rc={result.returncode}', int(result.returncode == 0)


def main():
    with tempfile.TemporaryDirectory(prefix='gfinit-') as td:
        exe = pathlib.Path(td)/'host'
        subprocess.run(command(exe), cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=120)
        if result.returncode:
            return result.returncode
        subprocess.run(command(exe, True), cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=120)
        if result.returncode:
            return result.returncode
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(mutation,
            [('kdebug/gfinit_exec.c', *item) for item in MUTATIONS] +
            list(SHARED_MUTATIONS) +
            [('kdebug/gfinit_op/pll_expand.inc', 'vpll_repeat_drop', 'i < q.count; i++) {\n            const struct gfinit_pll_write *w = &q.writes[i];', 'i < q.count - 1; i++) {\n            const struct gfinit_pll_write *w = &q.writes[i];'),
             ('kdebug/recover_gf.c', 'vpll_verify_zero', '.write_mask = ~0U, .verify_mask = ~0U,', '.write_mask = ~0U, .verify_mask = 0,')],
            ('kdebug/gfinit_exec.c', 'control', '', ''))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
