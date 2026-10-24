"""T3 actual core fake-wire tests and focused mutations; no hardware."""
import pathlib
import subprocess
import sys
import tempfile

import host32
import mutpar
import test_recover as base

sys.path.insert(0, str(base.ROOT / "tools"))
import rshell_recover

MUTATIONS = [
    ('t3_bus_time_charged_tx', 'if (timed) account(r);\n    r->emitting = true;', 'r->emitting = true;'),
    ('t3_flush_tail_charged_execution', 'if (timed) account(r);\n    r->emitting = false;', 'r->emitting = false;'),
    ('t3_tx_execution_charged', 'if (r->log_limit && r->emitting) {', 'if (false && r->log_limit && r->emitting) {'),
    ('t3_tx_limit_ignored', 'if (r->log_limit && r->tx_ticks >= r->log_limit) {', 'if (false && r->log_limit && r->tx_ticks >= r->log_limit) {'),
    ('t3_flush_busy_ignored', 'int ready = empty(opaque);', 'int ready = 1; (void)empty(opaque);'),
    ('t3_flush_poll_limit', 'while (r->uart_polls < max_polls) {', 'while (r->uart_polls <= max_polls) {'),
    ('t3_row_acquire_ignored', 'int rc = t->begin ? t->begin(t->opaque) : 0;', 'int rc = 0;'),
    ('t3_row_release_omitted', 'if (owned && t->end && t->end(r, t->opaque, rc)) rc = -1;', '(void)owned;'),
    ('t3_row_flush_failure_ignored', 'if (owned && t->end && t->end(r, t->opaque, rc)) rc = -1;', 'if (owned && t->end) (void)t->end(r, t->opaque, rc);'),
    ('t3_gf_bus_end_omitted', 'if (r->state == REC_RUNNING && gf_bus_record(r, &a, "END", old, *value, actual, 0))', 'if (false)'),
    ('t3_read_begin_failed', 'if (log && r->state != REC_RESTORING) return -1;\n    if (r->state == REC_RESTORING && recover_restore_point(r)) return -1;\n    if (r->state != REC_RESTORING && call_point(r, false)) return -1;\n    int rc = r->ops->read', 'if (r->state == REC_RESTORING && recover_restore_point(r)) return -1;\n    if (r->state != REC_RESTORING && call_point(r, false)) return -1;\n    int rc = r->ops->read'),
    ('t3_write_begin_failed', 'if (log && r->state != REC_RESTORING) return -1;\n    if (r->state == REC_RESTORING && recover_restore_point(r)) return -1;\n    if (r->state != REC_RESTORING && call_point(r, false)) return -1;\n    int rc = r->ops->write', 'if (r->state == REC_RESTORING && recover_restore_point(r)) return -1;\n    if (r->state != REC_RESTORING && call_point(r, false)) return -1;\n    int rc = r->ops->write'),
    ('t3_deadline_reentry', 'fail(r, REC_TIMEOUT, REC_PHASE_NONE, 0);\n        if (!r->emitting) cleanup(r);', 'fail(r, REC_TIMEOUT, REC_PHASE_NONE, 0);\n        cleanup(r);'),
    ('t3_receive_reentry', 'if (r->emitting) return 0;', ''),
    ('t3_row_boundary', 'if (rc) r->log_partial = true;', ''),
    ('t3_access_after_terminal', 'static int access_error(struct recover *r, u32 key)\n{\n    if (r->state == REC_VERIFIED || r->state == REC_FAILED) return -1;', 'static int access_error(struct recover *r, u32 key)\n{'),
    ('t3_write_after_terminal', 'logged_write(r, &a, *value, old)) {\n            if (r->state == REC_VERIFIED || r->state == REC_FAILED) return -1;', 'logged_write(r, &a, *value, old)) {'),
    ('t3_verify_after_terminal', 'logged_read(r, &a, &actual)) {\n                if (r->state == REC_VERIFIED || r->state == REC_FAILED) return -1;', 'logged_read(r, &a, &actual)) {'),
    ('t3_old_session', 'rx_word(r->rx + RECOVER_ABORT_SESSION_OFF) == r->token.session && r->token.session &&', 'r->token.session &&'),
    ('t3_rx_cap', 'n < RECOVER_RX_LIMIT; n++', 'n < RECOVER_RX_LIMIT + 1; n++'),
    ('t3_crc', 'rx_word(r->rx + RECOVER_ABORT_CRC_OFF) == crc32_core_final(crc32_core_update(CRC32_INIT, r->rx, RECOVER_ABORT_CRC_OFF))', 'true'),
    ('t3_abort_ignored', 'bool abort = wire_abort || (r->ops->abort_requested &&', 'bool abort = (wire_abort && false) || (r->ops->abort_requested &&'),
    ('t3_lost_not_counted', 'r->lost_lines++; fail(r, REC_LOG, REC_PHASE_NONE, 0);', 'fail(r, REC_LOG, REC_PHASE_NONE, 0);'),
    ('t3_partial_retained', 'discard(r, r->rx_used); r->rx_used = 0;', 'discard(r, r->rx_used);'),
]


def one(item):
    name, before, after = item
    source = (base.ROOT / base.SOURCE).read_text()
    if before and source.count(before) != 1:
        return name + ': missing/ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix='recover-t3-mut-') as td:
        exe = pathlib.Path(td) / 'core'
        try:
            tree = mutpar.build_in_tree(base.ROOT, td,
                {base.SOURCE: source.replace(before, after, 1) if before else source},
                [base.host_cmd(exe)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return name + ': COMPILE FAIL ' + str(exc.stderr), 1
        try:
            result = host32.run([str(exe)], cwd=str(tree), timeout=30, capture_output=True)
        except subprocess.TimeoutExpired:
            return name + ': runtime boundedness failure', 0
        return name + (': GREEN' if result.returncode == 0 else ': RED rc=' + str(result.returncode)), int(result.returncode == 0)


def main():
    with tempfile.TemporaryDirectory(prefix='recover-t3-') as td:
        exe = pathlib.Path(td) / 'core'
        subprocess.run(base.host_cmd(exe) + ["-DRECOVER_T3_CAPTURE"], check=True, cwd=base.ROOT)
        result = host32.run([str(exe)], cwd=base.ROOT, timeout=30, capture_output=True)
        if result.returncode:
            print(result.stdout.decode(), end='')
            return result.returncode
        text = result.stdout.decode()
        transcript = text.split('T3 WIRE BEGIN\n', 1)[1].split('T3 WIRE END\n', 1)[0]
        records = rshell_recover.Records(1)
        for line in transcript.splitlines():
            records.line(line)
        assert records.success(), records.data
        assert records.final['discarded_bytes'] == '2'
        print('recover T3 core + host serializer contract: PASS')
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(one, MUTATIONS, ('control', '', ''))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
