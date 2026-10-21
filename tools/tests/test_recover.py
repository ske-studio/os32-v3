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
    ("access_blocked_ignored", "    if (r->access_blocked) return -1;", ""),
    ("local_selected_stale", "r->selected = *value == t->index ? target : RECOVER_NONE;",
     "r->selected = target;"),
    ("write_bus_contract", "fail(r, REC_BUS, access_phase(r), target);\n            return access_error",
     "fail(r, REC_CONTRACT, REC_PHASE_NONE, target);\n            return access_error"),
    ("verify_phase_restore", "fail(r, REC_VERIFY, access_phase(r), target);",
     "fail(r, REC_VERIFY, REC_PHASE_RESTORE, target);"),
    ("snapshot_valid_early", "r->valid = false; r->failed = false;",
     "r->valid = true; r->failed = false;"),
    ("cleanup_removed", "bool safe = !r->access_blocked && quiesce(r, true);",
     "bool safe = false;"),
    ("w1c_allowed", """(d->kind != REC_RW && d->kind != REC_COLD) ||
            d->kind != p->targets[d->key].access""",
     "(d->kind != REC_RW && d->kind != REC_COLD && d->kind != REC_W1C)"),
    ("mask_all_bits", "(actual & ~d->write_mask) | (r->saved[i] & d->write_mask)",
     "r->saved[i]"),
    ("restore_order", "const struct recover_reg *d = &r->profile->regs[i];\n            bool command",
     "const struct recover_reg *d = &r->profile->regs[r->saved_count - 1 - i];\n            bool command"),
    ("deadline_inverted", "if (r->spent >= r->budget ||",
     "if (r->spent < r->budget ||"),
    ("stale_session", "t && t->session == r->token.session &&", "t &&"),
    ("verify_ok", "((actual ^ *value) & w->verify_mask)) {",
     "(((actual ^ *value) & w->verify_mask) && false)) {"),
    ("cold_replayed", "if (d->kind == REC_COLD || (r->profile->cold_required && !command)) continue;",
     "if (command) continue;"),
    ("receive_time_lost", "    if (check_deadline(r)) return -1;\n    if (abort)",
     "    if (abort)"),
    ("pause_time_lost", "    if (check_deadline(r)) return -1;\n    r->in_call = false;",
     "    r->in_call = false;"),
    ("local_cleanup_removed", "if (recover_restore_point(r) || r->ops->local_cleanup(r->opaque)) {", "if (false) {"),
    ("local_cleanup_after_quiesce", """    if (recover_restore_point(r) || r->ops->local_cleanup(r->opaque)) {
        fail(r, REC_BUS, REC_PHASE_RESTORE, 0); r->access_blocked = true;
    }
    state(r, REC_RESTORING);
    bool safe = !r->access_blocked && quiesce(r, true);""", """    state(r, REC_RESTORING);
    bool safe = !r->access_blocked && quiesce(r, true);
    if (recover_restore_point(r) || r->ops->local_cleanup(r->opaque)) {
        fail(r, REC_BUS, REC_PHASE_RESTORE, 0); r->access_blocked = true;
    }"""),
    ("head_stride_removed", "t->head * RECOVER_HEAD_STRIDE + t->port", "0 * t->head + t->port"),
    ("prmcio_confused", "? RECOVER_PRMCIO : RECOVER_PRMVIO", "? RECOVER_PRMVIO : RECOVER_PRMVIO"),
    ("index_data_reversed", "t->space == REC_VGA_INDEXED && dir != REC_SELECT", "t->space == REC_VGA_INDEXED && dir == REC_SELECT"),
    ("select_write_unchecked", "if (dir == REC_READ) {", "if (dir == REC_READ || dir == REC_SELECT) {"),
    ("phase_unchecked", "n->phase == w->phase && n->main_id == w->main_id", "n->main_id == w->main_id"),
    ("pc_unchecked", "n->pc == w->pc && n->substep == w->substep", "n->substep == w->substep"),
    ("value_mask_unchecked", "(value & v->mask) != v->bits", "((value & v->mask) != v->bits && false)"),
    ("value_set_unchecked", "if (v->mode == REC_SET) {", "if (v->mode == REC_SET) return true;\n    if (v->mode == REC_SET) {"),
    ("value_range_unchecked", "return value >= v->min && value <= v->max;", "return true;"),
    ("expr_unchecked", "!evaluate(r, n, w, old, &expected) || *value != expected", "!evaluate(r, n, w, old, &expected)"),
    ("dependency_order_reversed", "bool found = false;\n        for (u32 j = 0; j < node; j++) {", "bool found = false;\n        for (u32 j = node; j < plan->node_count; j++) {"),
    ("previous_iteration_reused", "q->ctx == n->ctx && q->phase == n->phase && q->main_id == n->main_id)))", "q->phase == n->phase && q->main_id == n->main_id)))"),
    ("n1_before_removed", "if (!guards(r, w, REC_BEFORE))", "if (false && !guards(r, w, REC_BEFORE))"),
    ("n1_after_removed", "if (!guards(r, w, REC_AFTER))", "if (false && !guards(r, w, REC_AFTER))"),
    ("n1_cfg_only", "if (!zero_target(t)) continue;", "if (!zero_target(t) || t->space != REC_PCI_CFG) continue;"),
    ("n1_mmio_only", "if (!zero_target(t)) continue;", "if (!zero_target(t) || t->space != REC_BAR_MMIO) continue;"),
    ("nonzero_hold_allowed", "if (zero_target(t) && args[1].value) return false;", "if (false && zero_target(t) && args[1].value) return false;"),
    ("crc_tables_removed", "c = crc_word(c, d->value.bits);", "/* value bits omitted */"),
    ("cold_saved_allowed", "if (v->mode == REC_SAVED && (", "if (false && v->mode == REC_SAVED && ("),
]

# Additional T2 mutations; every one must compile and fail at runtime.
MUTATIONS += [
    ("indexed_expr_dependency_removed", "if (!target_read && arg->kind != REC_ARG_READ) continue;",
     "if (!target_read) continue;"),
    ("indexed_expr_selection_removed", "r->selected != a->value) || bus_read", "false) || bus_read"),
    ("raw_alias_unchecked", "if (t->bar == 0) for (u32 head = 0; head < 2; head++)",
     "if (false) for (u32 head = 0; head < 2; head++)"),
    ("raw_alias_aperture", "/* Actual PRMVIO/PRMCIO ports, not the intervening PCRTC regs.",
     "if (off >= RECOVER_PRMCIO && off < RECOVER_PRMCIO + 2 * RECOVER_HEAD_STRIDE) return -1;\n            /* Actual PRMVIO/PRMCIO ports, not the intervening PCRTC regs."),
    ("t2_event_cleanup_reentered", """    if (r->state == REC_RESTORING || r->state == REC_VERIFIED ||
        r->state == REC_FAILED) return -1;""", "/* repeated cleanup */"),
    ("t2_event_cleanup_overwritten", "        if (r->state != (restoring ? REC_RESTORING : s)) return -1;", "/* completed cleanup ignored */"),
    ("t2_uart_running_only", "if (!r->in_call || (r->state != REC_SNAPSHOTTING && r->state != REC_ARMED &&\n        r->state != REC_RUNNING && r->state != REC_PAUSED)) return -1;",
     "if (!r->in_call || r->state != REC_RUNNING) return -1;"),
    ("t2_size_tail_no_poll", "!r->failed && !recover_restore_point(r) &&", "!r->failed &&"),
    ("t2_size_tail_no_deadline", """bars) && restore_time(r) &&
            command == RECOVER_COMMAND_OVERRIDE;
        for (u32 i = 0; i < 3; i++)
            if (!r->measurement.bar[i].valid || bars[i] != r->measurement.bar[i].raw) complete = false;
        if (complete) r->cache = r->measurement;
        else r->cache = (struct recover_bar_measurement){0};
    }
    /* Finish the first allocation before releasing the reserved S0 budget. */
    restore_time(r);""", """bars) &&
            command == RECOVER_COMMAND_OVERRIDE;
        for (u32 i = 0; i < 3; i++)
            if (!r->measurement.bar[i].valid || bars[i] != r->measurement.bar[i].raw) complete = false;
        if (complete) r->cache = r->measurement;
        else r->cache = (struct recover_bar_measurement){0};
    }"""),
    ("t2_s0_reserve_removed", "limit -= r->profile->s0_reserve_ticks;", "/* no reservation */"),
    ("t2_poll_removed", "r->restore_polls++;\n    return 0;", "return 0;"),
    ("t2_delay_truncated", "wait_us(opaque, 1000U)", "wait_us(opaque, 999U)"),
    ("t2_uart_1ms", "    for (;;) {", "    frame_us = 1000;\n    for (;;) {"),
    ("t2_uart_double_poll", "        r->uart_polls++;", "        r->uart_polls += 2;"),
    ("t2_trial_budget_reused", "u32 limit = r->profile->restore_limit;", "u32 limit = r->budget - r->spent;"),
    ("t2_log_reserve_removed", "bytes -= r->profile->s0_reserve_bytes;", "bytes -= 0;"),
    ("t2_uart_poll_removed", "        r->uart_polls++;", "        /* omitted */"),
    ("t2_restore_reextended", "u32 limit = r->profile->restore_limit;", "r->restore_tick = r->ops->ticks(r->opaque); u32 limit = r->profile->restore_limit;"),
    ("t2_failed_verified", "state(r, r->failed ? REC_FAILED : REC_VERIFIED);", "r->failed = false; state(r, REC_VERIFIED);"),
    ("t2_restore_wrap_inverted", "r->restore_tick) < limit", "r->restore_tick) > limit"),
    ("t2_pit_round_down", "+(scaled%PIT_PERIOD_SCALE!=0)", "", "kdebug/recover_profile.c"),
    ("t2_pit_irq_lost", "    irq_restore(flags);\n    return low", "    (void)flags;\n    return low", "kdebug/recover_profile.c"),
    ("t2_pte_restore_removed", "int rc = w->ops->unmap(w->opaque, w->va, w->raw);", "int rc = 0;", "kdebug/recover_profile.c"),
    ("t2_irq_restore_removed", "w->ops->irq_restore(w->opaque, w->flags);", "(void)w;", "kdebug/recover_profile.c"),
    ("t2_cell_restore_removed", "ops->write_cell(opaque, saved)", "0", "kdebug/recover_profile.c"),
    ("t2_cell_verify_removed", "actual != saved", "false", "kdebug/recover_profile.c"),
    ("t2_command_snapshot_replayed", "w.value = RECOVER_COMMAND_OVERRIDE;", "w.value = r->saved[i];"),
    ("t2_index_order_unchecked", "(i && steps[i].role < steps[i-1].role)", "false", "kdebug/recover_profile.c"),
]


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
        result = host32.run([str(exe)], cwd=str(tree), timeout=60, capture_output=True)
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
