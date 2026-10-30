"""T1/T1b recovery contracts; real source + fake bus/clock, no hardware.

--mutate covers T1 plus ADDENDUM.r2 §3 T1b mutations; hardware PTE work is T2.
"""
import os
import pathlib
import subprocess
import sys
import tempfile

import host32
import mutpar

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = "kdebug/recover.c"
FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-m32", "-march=i386",
         "-ffreestanding", "-fno-pie", "-fno-stack-protector"]


def host_cmd(exe):
    return ["gcc", *FLAGS, "-O0", "-nostdlib", "-static", "-no-pie",
            "-I" + str(ROOT / "tools/tests/recover_hostshim"),
            "-I" + str(ROOT / "include"),
            str(ROOT / "tools/tests/recover_host.c"), "-o", str(exe)]


MUTATIONS = [
    ("route_unapproved_write", "if (!once && !r->route_write_approved)", "if (false)", "kdebug/recover_common/s0.inc"),
    ("route_zero_skipped", "if (once || (route & RECOVER_ROUTE_MASK))", "if (route & RECOVER_ROUTE_MASK)", "kdebug/recover_common/s0.inc"),
    ("route_readback_ignored", "logged_route(r, &route) || (route & RECOVER_ROUTE_MASK)", "logged_route(r, &route)", "kdebug/recover_common/s0.inc"),
    ("route_exit_approval_leaked", "    route_test_once = route_test_approved = false;\n    return rc;", "    return rc;"),
    ("route_consumption_removed", "if (once) route_test_once = route_test_approved = false;", "/* not consumed */", "kdebug/recover_common/s0.inc"),
    ("access_blocked_ignored", "    if (r->access_blocked) return -1;", "", "kdebug/recover_plan/access.inc"),
    ("idle_bar_mismatch_ignored", "if (bars[i] != r->cache.bar[i].raw)",
     "if (false && bars[i] != r->cache.bar[i].raw)", "kdebug/recover_plan/access_idle_read.inc"),
    ("idle_read_safe_ignored", "if (!t->read_safe ||", "if (false ||",
     "kdebug/recover_plan/access_idle_read.inc"),
    ("idle_result_ignored", "return access_idle_read(r, target, width, value);",
     "return (access_idle_read(r, target, width, value), 0);", "kdebug/recover_plan/access.inc"),
    ("idle_advances_accessed", "    a.key = target;", "    r->accessed = true;\n    a.key = target;",
     "kdebug/recover_plan/access_idle_read.inc"),
    ("local_selected_stale", "r->port_selected[r->index_port[target]] =\n            *value == t->index ? target : RECOVER_NONE;",
     "r->port_selected[r->index_port[target]] = target;", "kdebug/recover_plan/access.inc"),
    ("write_bus_contract", "fail(r, REC_BUS, access_phase(r), target);\n            return access_error",
     "fail(r, REC_CONTRACT, REC_PHASE_NONE, target);\n            return access_error", "kdebug/recover_plan/access.inc"),
    ("verify_phase_restore", "fail(r, REC_VERIFY, access_phase(r), target);",
     "fail(r, REC_VERIFY, REC_PHASE_RESTORE, target);", "kdebug/recover_plan/access.inc"),
    ("snapshot_valid_early", "r->valid = false; r->failed = false;",
     "r->valid = true; r->failed = false;", "kdebug/recover_common/session.inc"),
    ("cleanup_removed", "bool safe = !r->s0_only && !r->access_blocked && quiesce(r, true);",
     "bool safe = false;", "kdebug/recover_common/s0.inc"),
    ("w1c_allowed", """(d->kind != REC_RW && d->kind != REC_COLD) ||
            d->kind != p->targets[d->key].access""",
     "(d->kind != REC_RW && d->kind != REC_COLD && d->kind != REC_W1C)", "kdebug/recover_plan/profile.inc"),
    ("mask_all_bits", "(actual & ~d->write_mask) | (r->saved[i] & d->write_mask)",
     "r->saved[i]", "kdebug/recover_common/s0.inc"),
    ("restore_order", "const struct recover_reg *d = &r->profile->regs[i];\n            bool command",
     "const struct recover_reg *d = &r->profile->regs[r->saved_count - 1 - i];\n            bool command", "kdebug/recover_common/s0.inc"),
    ("deadline_inverted", "if (r->spent >= r->budget ||",
     "if (r->spent < r->budget ||", "kdebug/recover_common/session.inc"),
    ("stale_session", "t && t->session == r->token.session &&", "t &&"),
    ("verify_ok", "mismatch = ((actual ^ readback_value(t, dir, *value)) & w->verify_mask) != 0;",
     "mismatch = (((actual ^ readback_value(t, dir, *value)) & w->verify_mask) != 0) && false;", "kdebug/recover_plan/access.inc"),
    ("cold_replayed", "if (d->kind == REC_COLD || (r->profile->cold_required && !command)) continue;",
     "if (command) continue;", "kdebug/recover_common/s0.inc"),
    ("receive_time_lost", "    if (check_deadline(r)) return -1;\n    if (abort)",
     "    if (abort)", "kdebug/recover_common/session.inc"),
    ("pause_time_lost", "    if (check_deadline(r)) return -1;\n    r->in_call = false;",
     "    r->in_call = false;", "kdebug/recover_common/session.inc"),
    ("local_cleanup_removed", "if (recover_restore_point(r) || r->ops->local_cleanup(r->opaque)) {", "if (false) {", "kdebug/recover_common/s0.inc"),
    ("local_cleanup_after_quiesce", """    if (recover_restore_point(r) || r->ops->local_cleanup(r->opaque)) {
        fail(r, REC_BUS, REC_PHASE_RESTORE, 0); r->access_blocked = true;
    }
    bool safe = !r->s0_only && !r->access_blocked && quiesce(r, true);""", """    bool safe = !r->s0_only && !r->access_blocked && quiesce(r, true);
    if (recover_restore_point(r) || r->ops->local_cleanup(r->opaque)) {
        fail(r, REC_BUS, REC_PHASE_RESTORE, 0); r->access_blocked = true;
    }""", "kdebug/recover_common/s0.inc"),
    ("head_stride_removed", "t->head * RECOVER_HEAD_STRIDE + t->port", "0 * t->head + t->port", "kdebug/recover_plan/profile.inc"),
    ("prmcio_confused", "? RECOVER_PRMCIO : RECOVER_PRMVIO", "? RECOVER_PRMVIO : RECOVER_PRMVIO", "kdebug/recover_plan/profile.inc"),
    ("index_data_reversed", "t->space == REC_VGA_INDEXED && dir != REC_SELECT", "t->space == REC_VGA_INDEXED && dir == REC_SELECT", "kdebug/recover_plan/profile.inc"),
    ("select_write_unchecked", "if (dir == REC_READ) {", "if (dir == REC_READ || dir == REC_SELECT) {", "kdebug/recover_plan/access.inc"),
    ("phase_unchecked", "n->phase == w->phase && n->main_id == w->main_id", "n->main_id == w->main_id", "kdebug/recover_plan/bind.inc"),
    ("pc_unchecked", "n->pc == w->pc && n->substep == w->substep", "n->substep == w->substep", "kdebug/recover_plan/bind.inc"),
    ("value_mask_unchecked", "(value & v->mask) != v->bits", "((value & v->mask) != v->bits && false)", "kdebug/recover_plan/profile.inc"),
    ("value_set_unchecked", "if (v->mode == REC_SET) {", "if (v->mode == REC_SET) return true;\n    if (v->mode == REC_SET) {", "kdebug/recover_plan/profile.inc"),
    ("value_range_unchecked", "return value >= v->min && value <= v->max;", "return true;", "kdebug/recover_plan/profile.inc"),
    ("expr_unchecked", "            if (*value != expected) return access_error(r, target);", "            (void)expected;", "kdebug/recover_plan/access.inc"),
    ("dependency_order_reversed", "bool found = false;\n        for (u32 j = 0; j < node; j++) {", "bool found = false;\n        for (u32 j = node; j < plan->node_count; j++) {", "kdebug/recover_plan/bind.inc"),
    ("previous_iteration_reused", "q->ctx == n->ctx && q->phase == n->phase && q->main_id == n->main_id)))", "q->phase == n->phase && q->main_id == n->main_id)))", "kdebug/recover_plan/bind.inc"),
    ("n1_before_removed", "if (!guards(r, w, REC_BEFORE))", "if (false && !guards(r, w, REC_BEFORE))", "kdebug/recover_plan/access.inc"),
    ("n1_after_removed", "if (!guards(r, w, REC_AFTER))", "if (false && !guards(r, w, REC_AFTER))", "kdebug/recover_plan/access.inc"),
    ("n1_cfg_only", "if (!zero_target(t)) continue;", "if (!zero_target(t) || t->space != REC_PCI_CFG) continue;", "kdebug/recover_plan/access.inc"),
    ("n1_mmio_only", "if (!zero_target(t)) continue;", "if (!zero_target(t) || t->space != REC_BAR_MMIO) continue;", "kdebug/recover_plan/access.inc"),
    ("nonzero_hold_allowed", "if (zero_target(t) && args[1].value) return false;", "if (false && zero_target(t) && args[1].value) return false;", "kdebug/recover_plan/bind.inc"),
    ("crc_tables_removed", "c = crc_word(c, d->value.bits);", "/* value bits omitted */"),
    ("cold_saved_allowed", "if (v->mode == REC_SAVED && (", "if (false && v->mode == REC_SAVED && (", "kdebug/recover_plan/profile.inc"),
]

# Additional T2 mutations; every one must compile and fail at runtime.
MUTATIONS += [
    # gf4a S1 follow-up: raw VGA alias ports and indexed EXPR READ selection.
    ("indexed_expr_dependency_removed", "if (!target_read && arg->kind != REC_ARG_READ) continue;",
     "if (!target_read) continue;", "kdebug/recover_plan/bind.inc"),
    ("indexed_expr_selection_removed", "r->port_selected[r->index_port[a->value]] != a->value) || bus_read", "false) || bus_read", "kdebug/recover_plan/access.inc"),
    ("raw_alias_unchecked", "if (t->bar == 0) for (u32 head = 0; head < RECOVER_HEAD_COUNT; head++)",
     "if (false) for (u32 head = 0; head < RECOVER_HEAD_COUNT; head++)", "kdebug/recover_plan/profile.inc"),
    ("raw_alias_aperture", "/* Actual PRMVIO/PRMCIO ports, not the intervening PCRTC regs.",
     "if (off >= RECOVER_PRMCIO && off < RECOVER_PRMCIO + 2 * RECOVER_HEAD_STRIDE) return -1;\n            /* Actual PRMVIO/PRMCIO ports, not the intervening PCRTC regs.", "kdebug/recover_plan/profile.inc"),
    # T2.
    ("t2_event_cleanup_reentered", """    if (r->state == REC_RESTORING || r->state == REC_VERIFIED ||
        r->state == REC_FAILED) return -1;""", "/* repeated cleanup */", "kdebug/recover_common/s0.inc"),
    ("t2_event_cleanup_overwritten", "        if (r->state != (restoring ? REC_RESTORING : s)) return -1;", "/* completed cleanup ignored */"),
    ("t2_uart_running_only", "if (!r->in_call || (r->state != REC_SNAPSHOTTING && r->state != REC_ARMED &&\n        r->state != REC_RUNNING && r->state != REC_PAUSED)) return -1;",
     "if (!r->in_call || r->state != REC_RUNNING) return -1;", "kdebug/recover_common/session.inc"),
    ("t2_size_tail_no_poll", "!r->failed && !recover_restore_point(r) &&", "!r->failed &&", "kdebug/recover_common/s0.inc"),
    ("t2_size_tail_no_deadline", """bars) && restore_time(r) &&
            command == RECOVER_COMMAND_OVERRIDE;
        for (u32 i = 0; i < RECOVER_BAR_COUNT; i++)
            if (!r->measurement.bar[i].valid || bars[i] != r->measurement.bar[i].raw) complete = false;
        if (complete) r->cache = r->measurement;
        else r->cache = (struct recover_bar_measurement){0};
    }
    /* Finish the first allocation before releasing the reserved S0 budget. */
    restore_time(r);""", """bars) &&
            command == RECOVER_COMMAND_OVERRIDE;
        for (u32 i = 0; i < RECOVER_BAR_COUNT; i++)
            if (!r->measurement.bar[i].valid || bars[i] != r->measurement.bar[i].raw) complete = false;
        if (complete) r->cache = r->measurement;
        else r->cache = (struct recover_bar_measurement){0};
    }""", "kdebug/recover_common/s0.inc"),
    ("t2_s0_reserve_removed", "limit -= cb.s0_ticks;", "/* no reservation */", "kdebug/recover_common/clock.inc"),
    ("t2_poll_removed", "r->restore_polls++;\n    return 0;", "return 0;", "kdebug/recover_common/clock.inc"),
    ("t2_delay_truncated", "wait_us(opaque, RECOVER_US_PER_MS)", "wait_us(opaque, 999U)", "kdebug/recover_common/clock.inc"),
    ("t2_uart_1ms", "    for (;;) {", "    frame_us = 1000;\n    for (;;) {", "kdebug/recover_common/clock.inc"),
    ("t2_uart_double_poll", "        r->uart_polls++;\n        int ready = ops->ready(opaque);", "        r->uart_polls += 2;\n        int ready = ops->ready(opaque);", "kdebug/recover_common/clock.inc"),
    ("t2_trial_budget_reused", "u32 limit = cb.ticks;", "u32 limit = r->budget - r->spent;", "kdebug/recover_common/clock.inc"),
    ("t2_log_reserve_removed", "bytes -= cb.s0_bytes;", "bytes -= 0;", "kdebug/recover_common/clock.inc"),
    ("t2_uart_poll_removed", "        r->uart_polls++;\n        int ready = ops->ready(opaque);", "        /* omitted */\n        int ready = ops->ready(opaque);", "kdebug/recover_common/clock.inc"),
    ("t2_restore_reextended", "u32 limit = cb.ticks;", "r->restore_tick = r->ops->ticks(r->opaque); u32 limit = cb.ticks;", "kdebug/recover_common/clock.inc"),
    ("t2_failed_verified", "state(r, r->failed ? REC_FAILED : REC_VERIFIED);", "r->failed = false; state(r, REC_VERIFIED);", "kdebug/recover_common/s0.inc"),
    ("t2_restore_wrap_inverted", "r->restore_tick) < limit", "r->restore_tick) > limit", "kdebug/recover_common/clock.inc"),
    ("t2_pit_round_down", "+(scaled%PIT_PERIOD_SCALE!=0)", "", "kdebug/recover_profile.c"),
    ("t2_pit_irq_lost", "    irq_restore(flags);\n    return low", "    (void)flags;\n    return low", "kdebug/recover_profile.c"),
    ("t2_pte_restore_removed", "w->ops->unmap(w->opaque, w->va + i * MEM_PAGE_SIZE, w->raw[i])", "0", "kdebug/kdebug_window.c"),
    ("t2_irq_restore_removed", "w->ops->irq_restore(w->opaque, w->flags);", "(void)w;", "kdebug/kdebug_window.c"),
    ("t2_cell_restore_removed", "access(opaque, 5, true, &saved)", "0", "lib/pc98_cell.h"),
    ("t2_cell_verify_removed", "actual != saved", "false", "lib/pc98_cell.h"),
    ("t2_cell_cleanup_before_restore", "if (run && step>=3 && step<=5)", "if (false)", "kdebug/recover_profile.c"),
    ("t2_command_snapshot_replayed", "w.value = RECOVER_COMMAND_OVERRIDE;", "w.value = r->saved[i];", "kdebug/recover_common/s0.inc"),
    ("t2_index_order_unchecked", "(i && steps[i].role < steps[i-1].role)", "false", "kdebug/recover_profile.c"),
]


DIAG_MUTATIONS = [
    ("txdiag_disabled", "if (!txd.active || txd.valid) return;", "if (txd.active || txd.valid) return;"),
    ("txdiag_overwrite", "if (!txd.active || txd.valid) return;", "if (!txd.active) return;"),
    ("txdiag_pos", "tx_pos = i + 1; tx_sent = 0;", "tx_pos = i; tx_sent = 0;", "kdebug/recover_common/log.inc"),
    ("txdiag_raw_rc", "tx_fail(r, REC_TX_PUT, rc);", "tx_fail(r, REC_TX_PUT, -1);", "kdebug/recover_common/clock.inc"),
    ("txdiag_put_retry", "int rc = ops->put(opaque, byte);", "int rc = ops->put(opaque, byte); if (rc) rc = ops->put(opaque, byte);", "kdebug/recover_common/clock.inc"),
    ("txdiag_row_overflow", "if (b->overflow) {", "if (false && b->overflow) {", "kdebug/recover_common/log.inc"),
    ("txdiag_final_before_diag", '        transport_diag(r);\n        row_start(r, &b, "final", "END");', '        row_start(r, &b, "final", "END");\n        transport_diag(r);', "kdebug/recover_common/log.inc"),

]
MUTATIONS += DIAG_MUTATIONS

def one_mutation(item):
    name, old, new = item[:3]
    source = item[3] if len(item) == 4 else SOURCE
    original = (ROOT / source).read_text()
    if old and original.count(old) != 1:
        return f"MUTATE {name}: missing/ambiguous anchor", 1
    with tempfile.TemporaryDirectory(prefix="recover-mut-") as td:
        exe = pathlib.Path(td) / "recover-host"
        try:
            tree = mutpar.build_in_tree(
                ROOT, td, {source: original.replace(old, new, 1) if old else original},
                [host_cmd(exe)], capture_output=True)
        except subprocess.CalledProcessError as error:
            return f"MUTATE {name}: COMPILE FAIL {error.stderr!r}", 1
        try:
            result = host32.run([str(exe)], cwd=str(tree), timeout=60, capture_output=True)
        except subprocess.TimeoutExpired:
            return mutpar.timeout_red((f"MUTATE {name}: RED timeout", 0))
        if name == "cold_replayed":
            missing = [i for i in range(4)
                       if f"COLD replay exit {i}".encode() not in result.stdout]
            if missing:
                return f"MUTATE {name}: missing exit paths {missing}", 1
            return f"MUTATE {name}: RED all 4 exit paths rc={result.returncode}", 0
        if result.returncode == 0:
            return f"MUTATE {name}: GREEN", 1
        return f"MUTATE {name}: RED rc={result.returncode}", 0


def main():
    with tempfile.TemporaryDirectory(prefix="recover-host-") as td:
        exe = pathlib.Path(td) / "recover-host"
        subprocess.run(host_cmd(exe), cwd=ROOT, check=True)
        rc = host32.run([str(exe)], cwd=ROOT, timeout=60).returncode
        print(f"HOST recover rc={rc}", flush=True)
        if rc:
            return 1
        cross = pathlib.Path(os.environ["CROSS_DIR"]) / "bin/i386-elf-gcc"
        subprocess.run([str(cross), *FLAGS, "-O2", "-I" + str(ROOT / "include"),
                        "-c", str(ROOT / SOURCE), "-o", str(pathlib.Path(td) / "recover.o")],
                       cwd=ROOT, check=True)
        subprocess.run([str(cross), *FLAGS, "-O2", "-I" + str(ROOT / "include"),
                        "-I" + str(ROOT / "arch/x86"), "-I" + str(ROOT / "platform/pc98"),
                        "-c", str(ROOT / "kdebug/recover_profile.c"), "-o", str(pathlib.Path(td) / "profile.o")],
                       cwd=ROOT, check=True)
        print("TARGET i386-elf GNU11 core/profile compile PASS", flush=True)
        if "--mutate" in sys.argv:
            return int(bool(mutpar.run_with_control(
                one_mutation, MUTATIONS, ("control", "", ""))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
