/* C2 uses exactly the same UNIT=12 + real F1/F2/B/V0/C0/C1/C2 fixture.
 * Build as described in gf_accel_c1_host.c, without separately linking that
 * test or quiesce.c. GF3_QUIESCE_SOURCE selects a product mutation.
 * Coverage: each stop boundary, partial-close retry, OFF versus ESTOP,
 * restore/each quiesce row retry, first error and revoked cleanup. */
#define GF3_CINT_C2
#include "gf_accel_c1_host.c"

static void c2_normal_and_repeated_stop(void)
{
    cint_reset(); cint_start(); cint_open_windows();
    struct gf_vram_reservation scan = cint_service->scan_node.value;
    struct gf_vram_reservation test = cint_service->test_node.value;
    cint_trace_clear(); cint_stop_display();
    assert(cint_close_calls[0] == 1 && cint_close_calls[1] == 1);
    assert(cint_restore_calls == 1 && cint_owner_stops == 1 && !cint_prod.display.engaged);
    assert(cint_service->scan_node.value.reservation_id == scan.reservation_id);
    assert(cint_service->scan_node.value.generation == scan.generation);
    assert(cint_service->test_node.value.reservation_id == test.reservation_id);
    struct gf_vram_reservation key;
    assert(gf_vram_match(&cint_prod.chan, scan.owner, scan.reservation_id,
        scan.generation, scan.off, scan.bytes, &key) == GF3_ESTATE);
    assert(!cint_service->scan.bytes && !cint_service->test.bytes);
    u32 writes = cint_host.calls[H_WRITE];
    assert(!gf_display_stop(&cint_prod.display));
    assert(cint_host.calls[H_WRITE] == writes && cint_restore_calls == 1);
    cint_no_resources();
}
static void c2_partial_window_close_retry(void)
{
    cint_reset(); cint_start(); cint_open_windows();
    struct gf_inst *head = cint_prod.foundation.imem.used;
    u32 count = cint_used_count(), epoch = cint_prod.chan.epoch;
    struct gf_vram_reservation scan = cint_service->scan_node.value;
    cint_trace_clear(); cint_expect_closed = true;
    /* List is newest first: view1 closes, then view0 fails. */
    cint_unmap_fail = CINT_MAP_VIEW0;
    assert(gf_display_stop(&cint_prod.display) == -1);
    assert(cint_prod.display.last_rc == GF2_EMAP && cint_prod.display.engaged);
    cint_closed(); cint_owned_reservations();
    assert(cint_prod.chan.state == GF2_RUN && cint_prod.accel.ready);
    assert(cint_prod.chan.epoch == epoch && cint_prod.foundation.imem.used == head && cint_used_count() == count);
    assert(cint_prod.chan.mapped && cint_prod.foundation.mapped && cint_dma_live);
    assert(cint_service->scan_node.value.reservation_id == scan.reservation_id);
    assert(cint_view[0].mapped && cint_loan[0] && !cint_view[1].mapped && !cint_loan[1]);
    assert(cint_view[0].map_ops == &cint_map_ops && cint_view[0].map_owner == &cint_prod);
    assert(cint_view[0].close_owner == &cint_loan[0] && cint_view[0].close == cint_close_window);
    assert(cint_close_calls[0] == 1 && cint_close_calls[1] == 1);
    assert(!cint_host.calls[H_WRITE] && !cint_host.calls[H_CLOCK] && !cint_restore_calls);
    cint_unmap_fail = -1;
    cint_stop_display();
    assert(cint_close_calls[0] == 2 && cint_close_calls[1] == 1);
}
static void c2_fini_failures_and_first_rc(void)
{
    /* One-shot failure at each MMIO/clock in real fini. Even if fini fails,
     * C2 must stop F2, invalidate, restore and release after successful OFF.
     * Build the fault schedule from real fini, not an invented call sequence. */
    cint_reset(); cint_start();
    cint_expect_closed = true;
    assert(!gf3_service_close(cint_service));
    cint_trace_clear();
    assert(!gf3_service_fini(cint_service, cint_deadline()));
    u32 counts[] = {cint_host.calls[H_READ], cint_host.calls[H_WRITE], cint_host.calls[H_CLOCK]};
    cint_stop_display();
    const enum gf3_host_op ops[] = {H_READ, H_WRITE, H_CLOCK};
    for (unsigned k = 0; k < sizeof(ops) / sizeof(ops[0]); ++k)
        for (u32 n = 1; n <= counts[k]; ++n) {
            cint_reset(); cint_start(); cint_open_windows();
            cint_trace_clear(); cint_expect_closed = true;
            /* C2 takes one clock before calling fini. */
            gf3_host_fail_at(&cint_host, ops[k], n + (ops[k] == H_CLOCK ? 1U : 0U), CINT_IO_FAILURE);
            assert(gf_display_stop(&cint_prod.display) == -1);
            assert(cint_prod.display.last_rc == GF2_EIO);
            assert(cint_restore_calls == 1 && !cint_prod.display.engaged);
            cint_no_resources();
        }
    cint_reset(); cint_start(); cint_trace_clear(); cint_expect_closed = true;
    gf3_host_fail_at(&cint_host, H_CLOCK, 1, CINT_IO_FAILURE);
    /* Fini issues no fence when C2 cannot obtain its deadline; F2 still drains
     * and stops with its own clock. Its resources are released by real F2. */
    u32 seq = cint_prod.chan.seq;
    assert(gf_display_stop(&cint_prod.display) == -1 && cint_prod.display.last_rc == GF2_EIO);
    assert(cint_prod.chan.seq == seq + 1U && cint_restore_calls == 1);
    cint_no_resources();
    /* An error in restoration cannot overwrite the earlier fini error. */
    cint_reset(); cint_start(); cint_trace_clear(); cint_expect_closed = true;
    gf3_host_fail_at(&cint_host, H_CLOCK, 1, CINT_IO_FAILURE);
    cint_restore_rc = CINT_RESTORE_FAILURE;
    assert(gf_display_stop(&cint_prod.display) == -1 && cint_prod.display.last_rc == GF2_EIO);
    assert(cint_prod.display.engaged); cint_owned_reservations();
    cint_restore_rc = 0; cint_trace_clear(); cint_stop_display();
}
static void c2_fault_off_and_estop(void)
{
    cint_reset(); cint_start(); cint_open_windows();
    ++cint_prod.foundation.stats.fifo;
    assert(gf2_fault_check(&cint_prod.chan) == GF2_EFAULT);
    cint_trace_clear(); cint_expect_closed = true;
    assert(gf_display_stop(&cint_prod.display) == -1);
    assert(cint_prod.display.last_rc == GF2_EFAULT && cint_restore_calls == 1);
    cint_no_resources(); /* OFF with a diagnostic error still permits release */

    cint_reset(); cint_start(); cint_open_windows();
    /* Force a real F2 engine-stop failure after fini. A fault latch makes
     * fini skip deletion, so all F3 instances must remain linked. */
    ++cint_prod.foundation.stats.graph;
    assert(gf2_fault_check(&cint_prod.chan) == GF2_EFAULT);
    struct gf_inst *head = cint_prod.foundation.imem.used;
    u32 count = cint_used_count();
    cint_trace_clear(); cint_expect_closed = true;
    /* First F2 stop write is close_gates(CACHES), no reference on FAULT. */
    gf3_host_fail_at(&cint_host, H_WRITE, 1, CINT_IO_FAILURE);
    assert(gf_display_stop(&cint_prod.display) == -1);
    assert(cint_prod.display.last_rc == GF2_ESTOP && cint_prod.chan.state == GF2_FAULT);
    cint_closed(); cint_owned_reservations();
    assert(cint_prod.accel.ready && cint_prod.accel.epoch && cint_used_count() == count);
    assert(cint_prod.foundation.imem.used == head && cint_prod.foundation.mapped && cint_prod.chan.mapped);
    assert(cint_dma_live && cint_prod.display.engaged && !cint_restore_calls && !views);
    cint_trace_clear();
    assert(gf_display_stop(&cint_prod.display) == -1); /* old fault rc, now OFF */
    assert(cint_prod.display.last_rc == GF2_EFAULT && cint_restore_calls == 1);
    cint_no_resources();
}
static void c2_lower_unmap_failures(void)
{
    for (int which = CINT_MAP_IMEM; which <= CINT_MAP_PUSH; ++which) {
        cint_reset(); cint_start(); cint_open_windows();
        cint_trace_clear(); cint_expect_closed = true; cint_unmap_fail = which;
        assert(gf_display_stop(&cint_prod.display) == -1);
        assert(cint_prod.display.last_rc == GF2_ESTOP && cint_prod.chan.state == GF2_FAULT);
        cint_closed(); cint_owned_reservations();
        assert(cint_prod.display.engaged && !cint_restore_calls && !views);
        assert(!cint_prod.foundation.imem.used); /* deletion succeeded before unmap */
        assert(cint_prod.chan.mapped);
        if (which == CINT_MAP_IMEM) {
            assert(cint_prod.foundation.mapped && cint_dma_live);
        } else {
            assert(!cint_prod.foundation.mapped && !cint_dma_live);
        }
        cint_unmap_fail = -1; cint_trace_clear();
        assert(gf_display_stop(&cint_prod.display) == -1); /* FAULT diagnostic survives */
        assert(cint_prod.display.last_rc == GF2_EFAULT);
        cint_no_resources();
    }
}
static void c2_f2_stop_stage_failures(void)
{
    cint_reset(); cint_start(); cint_trace_clear(); cint_stop_display();
    u32 nth[3] = {0}, irq_masks = 0;
    for (u32 i = 0; i < cint_host.nevents; ++i) {
        const struct gf3_host_event *e = &cint_events[i];
        if (e->op != H_WRITE) continue;
        if (e->a[1] == GF_IRQ_PMC_ENABLE && e->a[3] == 0) {
            ++irq_masks;
            if (irq_masks == 1) nth[0] = e->nth; /* command IRQ disable */
            if (irq_masks == 2) nth[2] = e->nth; /* F1 source mask after inst free */
        }
        if (!nth[1] && e->a[1] == RECOVER_PMC_ENABLE) nth[1] = e->nth; /* engine stop */
    }
    for (unsigned stage = 0; stage < sizeof(nth) / sizeof(nth[0]); ++stage) {
        assert(nth[stage]);
        cint_reset(); cint_start(); cint_open_windows();
        cint_trace_clear(); cint_expect_closed = true;
        gf3_host_fail_at(&cint_host, H_WRITE, nth[stage], CINT_IO_FAILURE);
        assert(gf_display_stop(&cint_prod.display) == -1);
        assert(cint_prod.display.last_rc == GF2_ESTOP && cint_prod.chan.state == GF2_FAULT);
        cint_closed(); cint_owned_reservations();
        assert(cint_prod.display.engaged && !cint_restore_calls && !views);
        assert(cint_prod.foundation.mapped && cint_prod.chan.mapped && cint_dma_live);
        /* Fini released F3. F2 variable inst survive IRQ/engine failure;
         * after release_instances only F1's four fixed regions remain. */
        assert(cint_used_count() == (stage == 2 ? 4U : 8U));
        assert(!cint_prod.accel.ready);
        cint_trace_clear();
        assert(gf_display_stop(&cint_prod.display) == -1);
        assert(cint_prod.display.last_rc == GF2_EFAULT);
        cint_no_resources();
    }
}
static void c2_restore_and_each_row_retry(void)
{
    /* Independent row list is the existing PC98 coexistence contract.
     * A per-row injection must prove all THREE rows are attempted again. */
    static const u32 rows[] = {RECOVER_PBUS1850, RECOVER_PBUS1854, GF_PMC_IRQ};
    cint_reset(); cint_start(); cint_trace_clear(); cint_stop_display();
    u32 nth[sizeof(rows) / sizeof(rows[0])] = {0};
    for (unsigned k = 0; k < sizeof(rows) / sizeof(rows[0]); ++k)
        for (u32 i = 0; i < cint_host.nevents; ++i)
            if (cint_events[i].op == H_WRITE && cint_events[i].a[1] == rows[k])
                nth[k] = cint_events[i].nth; /* final write is the C2 row */
    for (unsigned stage = 0; stage <= sizeof(rows) / sizeof(rows[0]); ++stage) {
        cint_reset(); cint_start(); cint_open_windows();
        struct gf_vram_reservation scan = cint_service->scan_node.value;
        cint_trace_clear(); cint_expect_closed = true;
        if (!stage) cint_restore_rc = CINT_RESTORE_FAILURE;
        else {
            assert(nth[stage - 1U]);
            gf3_host_fail_at(&cint_host, H_WRITE, nth[stage - 1U], CINT_IO_FAILURE);
        }
        assert(gf_display_stop(&cint_prod.display) == -1);
        assert(cint_prod.display.last_rc == (stage ? CINT_IO_FAILURE : CINT_RESTORE_FAILURE));
        assert(cint_prod.display.engaged && !cint_prod.display.active);
        cint_closed(); cint_owned_reservations();
        assert(cint_service->scan_node.value.reservation_id == scan.reservation_id);
        assert(cint_prod.chan.state == GF2_OFF && !cint_prod.accel.ready);
        assert(!cint_prod.foundation.imem.used && !cint_prod.foundation.mapped && !cint_prod.chan.mapped);
        assert(!cint_dma_live && !views && cint_restore_calls == 1);
        /* Count attempted rows (including the injected failing write) after
         * restore, rather than h.writes which records only successful MMIO. */
        assert(cint_host.nevents >= 3);
        for (unsigned k = 0; k < sizeof(rows) / sizeof(rows[0]); ++k) {
            const struct gf3_host_event *e = &cint_events[cint_host.nevents - 3U + k];
            assert(e->op == H_WRITE && e->a[1] == rows[k] && e->a[3] == 0);
        }
        cint_restore_rc = 0; cint_trace_clear(); cint_stop_display();
        assert(cint_restore_calls == 1 && !cint_prod.display.engaged);
        for (unsigned k = 0; k < sizeof(rows) / sizeof(rows[0]); ++k) {
            const struct gf3_host_event *e = &cint_events[cint_host.nevents - 3U + k];
            assert(e->op == H_WRITE && e->a[1] == rows[k] && !e->rc);
        }
        assert(cint_close_calls[0] == 1 && cint_close_calls[1] == 1);
    }
}
static void c2_owner_foundation_stop_failure(void)
{
    cint_reset(); cint_start(); cint_trace_clear(); cint_stop_display();
    /* Find the second source-mask write to PMC IRQ: the owner calls real F1
     * again AFTER real gf2_stop has successfully stopped F1 and reached OFF. */
    u32 nth = 0;
    for (u32 i = cint_host.nevents; i; --i) {
        const struct gf3_host_event *e = &cint_events[i - 1U];
        if (e->op == H_WRITE && e->a[1] == GF1_PMC_IRQ_ENABLE && e->a[3] == 0) {
            if (nth) { nth = e->nth; break; } /* skip final C2 quiesce row */
            nth = e->nth;
        }
    }
    assert(nth);
    cint_reset(); cint_start(); cint_trace_clear(); cint_expect_closed = true;
    gf3_host_fail_at(&cint_host, H_WRITE, nth, CINT_IO_FAILURE);
    assert(gf_display_stop(&cint_prod.display) == -1);
    assert(cint_prod.display.last_rc == GF1_EIO && cint_prod.display.engaged);
    assert(cint_prod.chan.state == GF2_OFF && cint_owner_stops == 1 && !cint_restore_calls);
    cint_owned_reservations();
    cint_trace_clear(); cint_stop_display();
}
static void c2_revoked_close_failure(void)
{
    cint_reset(); cint_start(); cint_open_windows();
    struct gf_inst *head = cint_prod.foundation.imem.used;
    cint_trace_clear(); cint_expect_closed = true;
    gf_foundation_revoke(&cint_prod.foundation);
    /* A revoked production bus refuses MMIO (GF_FEAT1_DESIGN C1), while
     * CPU-only mapper cleanup remains callable. Use A1's failures directly. */
    gf3_host_fail_at(&cint_host, H_READ, 0, CINT_IO_FAILURE);
    gf3_host_fail_at(&cint_host, H_WRITE, 0, CINT_IO_FAILURE);
    cint_unmap_fail = CINT_MAP_VIEW0;
    assert(gf_display_stop(&cint_prod.display) == -1);
    /* Revoked F1 cannot prove hardware stop: real gf2_stop returns ESTOP.
     * C2 must still reach the owner cleanup (F7), preserving the close rc. */
    assert(cint_host.calls[H_WRITE] && cint_prod.chan.state == GF2_FAULT);
    assert(cint_owner_stops == 1);
    assert(cint_prod.display.last_rc == GF2_EMAP && cint_prod.display.engaged);
    cint_closed(); cint_owned_reservations();
    assert(cint_view[0].mapped && cint_loan[0] && !cint_view[1].mapped && !cint_loan[1]);
    assert(cint_prod.foundation.imem.used == head && !cint_prod.foundation.mapped);
    assert(cint_inst_live(&cint_prod.accel.obj[0].inst));
    assert(cint_prod.chan.mapped && cint_dma_live && cint_prod.foundation.revoked);
    /* F7 / GF_FEAT2_DESIGN:319: attempt local restore and ALL quiesce rows
     * despite revocation, without claiming OFF or releasing reservations.
     * The supplied quiesce.c:63-65 returns early here: this is an intentional
     * contract regression assertion, reported without changing the product. */
    assert(cint_restore_calls == 1);
    static const u32 rows[] = {RECOVER_PBUS1850, RECOVER_PBUS1854, GF_PMC_IRQ};
    for (unsigned k = 0; k < sizeof(rows) / sizeof(rows[0]); ++k) {
        const struct gf3_host_event *e = &cint_events[cint_host.nevents - 3U + k];
        assert(e->op == H_WRITE && e->a[1] == rows[k] && e->a[3] == 0 && e->rc);
    }
    /* This is the last case: no fake repair of sticky revoked or forced
     * release of device-owned allocations just to make fixture teardown pass. */
}
void _start(void)
{
    c2_normal_and_repeated_stop();
    c2_partial_window_close_retry();
    c2_fini_failures_and_first_rc();
    c2_fault_off_and_estop();
    c2_f2_stop_stage_failures();
    c2_lower_unmap_failures();
    c2_restore_and_each_row_retry();
    c2_owner_foundation_stop_failure();
    c2_revoked_close_failure();
    gf2_host_exit(0);
}
