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
            "-I" + str(ROOT / "include"),
            str(ROOT / "tools/tests/recover_host.c"), "-o", str(exe)]


MUTATIONS = [
    ("snapshot_valid_early", "r->valid = false; r->failed = false;",
     "r->valid = true; r->failed = false;"),
    ("cleanup_removed", "bool safe = !r->access_blocked && quiesce(r, true);",
     "bool safe = false;"),
    ("w1c_allowed", """(d->kind != REC_RW && d->kind != REC_COLD) ||
            d->kind != p->targets[d->key].access""",
     "(d->kind != REC_RW && d->kind != REC_COLD && d->kind != REC_W1C)"),
    ("mask_all_bits", "(actual & ~d->write_mask) | (r->saved[i] & d->write_mask)",
     "r->saved[i]"),
    ("restore_order", "const struct recover_reg *d = &r->profile->regs[i];\n            if (d->kind == REC_COLD)",
     "const struct recover_reg *d = &r->profile->regs[r->saved_count - 1 - i];\n            if (d->kind == REC_COLD)"),
    ("deadline_inverted", "if (r->spent >= r->budget ||",
     "if (r->spent < r->budget ||"),
    ("stale_session", "t && t->session == r->token.session &&", "t &&"),
    ("verify_ok", "((actual ^ *value) & w->verify_mask)) {",
     "(((actual ^ *value) & w->verify_mask) && false)) {"),
    ("cold_replayed", """if (safe && !r->profile->cold_required) {
        for (u32 i = 0; i < r->saved_count; i++) {
            const struct recover_reg *d = &r->profile->regs[i];
            if (d->kind == REC_COLD) continue;""", """if (safe) {
        for (u32 i = 0; i < r->saved_count; i++) {
            const struct recover_reg *d = &r->profile->regs[i];
            if (d->kind != REC_COLD) continue;"""),
    ("receive_time_lost", "    if (check_deadline(r)) return -1;\n    if (abort)",
     "    if (abort)"),
    ("pause_time_lost", "    if (check_deadline(r)) return -1;\n    r->in_call = false;",
     "    r->in_call = false;"),
    ("local_cleanup_removed", "if (r->ops->local_cleanup(r->opaque)) {", "if (false) {"),
    ("local_cleanup_after_quiesce", """    if (r->ops->local_cleanup(r->opaque)) {
        fail(r, REC_BUS, REC_PHASE_RESTORE, 0); r->access_blocked = true;
    }
    state(r, REC_RESTORING);
    bool safe = !r->access_blocked && quiesce(r, true);""", """    state(r, REC_RESTORING);
    bool safe = !r->access_blocked && quiesce(r, true);
    if (r->ops->local_cleanup(r->opaque)) {
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


def one_mutation(item):
    name, old, new = item
    original = (ROOT / SOURCE).read_text()
    if old and original.count(old) != 1:
        return f"MUTATE {name}: missing/ambiguous anchor", 1
    with tempfile.TemporaryDirectory(prefix="recover-mut-") as td:
        exe = pathlib.Path(td) / "recover-host"
        try:
            tree = mutpar.build_in_tree(
                ROOT, td, {SOURCE: original.replace(old, new, 1) if old else original},
                [host_cmd(exe)], capture_output=True)
        except subprocess.CalledProcessError as error:
            return f"MUTATE {name}: COMPILE FAIL {error.stderr!r}", 1
        result = host32.run([str(exe)], cwd=str(tree), timeout=60, capture_output=True)
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
        print("TARGET i386-elf GNU11 compile PASS", flush=True)
        if "--mutate" in sys.argv:
            return int(bool(mutpar.run_with_control(
                one_mutation, MUTATIONS, ("control", "", ""))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
