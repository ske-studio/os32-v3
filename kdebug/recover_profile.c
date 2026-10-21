/* T2 common injectable primitives. No GF ROM/opcode classification here.
 * GF targets/rules are the shared recover descriptor, supplied by S1/S2.
 * Existing HAL PIT mode 2 is sampled without reprogramming; IRQ-off spans
 * only one latch/read. Never use tick/PIC-derived time for UART frame waits.
 */
#include "recover.h"
#include "memmap.h"
#include "../kernel/idt.h"
#include "../kernel/paging.h"
#include "pc98.h"
#include "io.h"
#ifdef RECOVER_MODULE
#include "generated/kdebug_import_macros.h"
#endif

#define RECOVER_UART_US 1000000U
#define RECOVER_WAIT_SAMPLES 1048576U
#define RECOVER_BUDGET_LOG_S0 4096U
#define RECOVER_BUDGET_POINTS 1024U
#define RECOVER_BUDGET_TRIAL_SECONDS 60U
#define RECOVER_BUDGET_STEPS 4096U
#define RECOVER_BUDGET_POLLS 65536U
#define RECOVER_BUDGET_S0_POLLS 16384U

int recover_screen_98(struct recover *r, const struct recover_screen_ops *ops, void *opaque)
{
    u32 saved, actual;
    int screen_rc;
    /* Each synchronous screen action has a bounded BEGIN/END pair. Logging
     * cannot authorize an access after its allocation has expired. */
#define SCREEN_CALL(step, expression) \
    ((void)recover_record(r, "screen", "BEGIN", step, 0, 0, 0), \
     screen_rc = (r->transport && recover_restore_point(r)) ? -1 : (expression), \
     (void)recover_record(r, "screen", "END", step, 0, screen_rc, \
                         r->ops->ticks(r->opaque) - r->restore_tick), screen_rc)
    if (!ops || !ops->cui || !ops->safe || !ops->restore || !ops->read_cell ||
        !ops->write_cell || !ops->redraw || ops->cui(opaque)) return -1;
    if (recover_restore_point(r) || SCREEN_CALL(0, ops->safe(opaque)) ||
        recover_restore_point(r) || SCREEN_CALL(1, ops->restore(opaque)) ||
        recover_restore_point(r) || SCREEN_CALL(2, ops->read_cell(opaque, &saved))) return -1;
    u32 test = (saved ^ 0xffffU) & 0xffffU;
    if (recover_restore_point(r)) return -1;
    int rc = SCREEN_CALL(3, ops->write_cell(opaque, test));
    /* Even a write reporting failure may have changed VRAM. Restore once,
     * preserving the first error. No retry of the failed access. */
    if (!rc) {
        if (recover_restore_point(r) || SCREEN_CALL(4, ops->read_cell(opaque, &actual)) || actual != test) rc = -1;
    }
    if (recover_restore_point(r) || SCREEN_CALL(5, ops->write_cell(opaque, saved))) return -1;
    if (recover_restore_point(r) || SCREEN_CALL(4, ops->read_cell(opaque, &actual)) || actual != saved) return -1;
    if (rc || recover_restore_point(r) || SCREEN_CALL(6, ops->redraw(opaque))) return -1;
    return 0;
#undef SCREEN_CALL
}

static int seconds_ticks(u32 sec, u32 hz, u32 *out)
{
    unsigned long long ticks = (unsigned long long)sec * hz;
    if (!ticks || ticks >= RECOVER_HALF_TICKS) return -1;
    *out = (u32)ticks;
    return 0;
}

int recover_budget_apply(struct recover_profile *p, u32 kind, u32 hz,
                         u32 baud, u32 frame_bits, const struct recover_static_work *w)
{
    static const u32 seconds[] = {10, 18, 33};
    static const u32 bytes[] = {4096, 8192, 16384};
    static const u32 execution_ms[] = {2000, 4000, 6000};
    static const u32 polls[] = {16384, 32768, 65536};
    if (!p || !w || kind > REC_BUDGET_SIZE_BARS || !baud || !frame_bits) return -1;
    if (frame_bits > (~0U / RECOVER_UART_US)) return -1;
    u32 bit_us = frame_bits * RECOVER_UART_US;
    u32 frame = bit_us / baud + (bit_us % baud != 0);
    if (!frame || frame > seconds[kind] * RECOVER_UART_US / bytes[kind]) return -1;
    u32 tx_us = bytes[kind] * frame;
    u32 tx_seconds = tx_us / RECOVER_UART_US + (tx_us % RECOVER_UART_US != 0);
    u32 margin = (tx_seconds + 1) / 2;
    if (margin < 3) margin = 3;
    if (!frame || frame > RECOVER_UART_US || tx_seconds + execution_ms[kind] / 1000 + margin > seconds[kind]) return -1;
    unsigned long long needed = (unsigned long long)w->bytes + w->wait_ms + w->points;
    unsigned long long s0_needed = (unsigned long long)w->s0_bytes + w->s0_wait_ms + w->s0_points;
    if (w->bytes > bytes[kind] || w->wait_ms > execution_ms[kind] || w->points > RECOVER_BUDGET_POINTS ||
        w->s0_bytes > RECOVER_BUDGET_LOG_S0 || w->s0_wait_ms > 2000 ||
        w->s0_points > RECOVER_BUDGET_POINTS || w->s0_bytes > w->bytes ||
        w->s0_wait_ms > w->wait_ms || w->s0_points > w->points ||
        needed > polls[kind] || s0_needed > RECOVER_BUDGET_S0_POLLS ||
        (kind && (needed - s0_needed > polls[kind] - RECOVER_BUDGET_S0_POLLS ||
                  w->bytes - w->s0_bytes > bytes[kind] - RECOVER_BUDGET_LOG_S0))) return -1;
    struct recover_profile next = *p;
    if (seconds_ticks(seconds[kind], hz, &next.restore_limit) ||
        seconds_ticks(RECOVER_BUDGET_TRIAL_SECONDS, hz, &next.trial_limit)) return -1;
    next.opcode_limit = next.trial_limit;
    next.max_steps = RECOVER_BUDGET_STEPS; next.max_polls = RECOVER_BUDGET_POLLS;
    next.restore_log_bytes = bytes[kind]; next.s0_reserve_bytes = kind ? RECOVER_BUDGET_LOG_S0:0;
    next.restore_max_polls = polls[kind]; next.s0_reserve_ticks = 0; next.s0_reserve_polls = 0;
    if (kind) {
        if (seconds_ticks(seconds[REC_BUDGET_S0], hz, &next.s0_reserve_ticks)) return -1;
        next.s0_reserve_polls = RECOVER_BUDGET_S0_POLLS;
    }
    next.crc = recover_profile_crc(&next);
    *p = next;
    return 0;
}

/* PIT mode 2 is already running at 100Hz. Read only the latch, never
 * reprogram it or consult PIC/tick_count. The read sequence is identical to
 * kernel/ktime.c; the short modulo difference avoids its 10ms IRR/clamp
 * ambiguity. At both supported input clocks one count is <1us. */
extern const struct pit_setup *pit_get_setup(void);
static u32 recover_pit_count(void)
{
    unsigned int flags=irq_save();
    outp(PIT_MODE,PIT_LATCH_TIMER0);
    u32 low=inp(PIT_CNTR0), high=inp(PIT_CNTR0);
    irq_restore(flags);
    return low | (high << 8);
}
int recover_hal_wait_frame(void *opaque, u32 frame_us)
{
    (void)opaque;
    const struct pit_setup *ps=pit_get_setup();
    if (!ps || !ps->valid || ps->mode != PIT_MODE_TIMER0 || !ps->reload ||
        (ps->clk_hz != SYSCLK_1997 && ps->clk_hz != SYSCLK_2458) ||
        !frame_us || frame_us >= ps->period_us) return -1;
    /* Max accepted frame <10000us, clock <=2457600Hz. Split the
     * product to keep i386 arithmetic in u32 and round UP, never truncate. */
    if (ps->hz != PIT_HZ || ps->period_us != RECOVER_UART_US/PIT_HZ ||
        ps->reload != ps->clk_hz/PIT_HZ) return -1;
    u32 scaled=frame_us*(ps->clk_hz/PIT_CLOCK_DIV);
    u32 counts=scaled/PIT_PERIOD_SCALE+(scaled%PIT_PERIOD_SCALE!=0);
    if (!counts || counts >= ps->reload) return -1;
    u32 previous=recover_pit_count(), elapsed=0;
    if (previous > ps->reload) return -1;
    for (u32 i=0;i<RECOVER_WAIT_SAMPLES;i++) {
        u32 current=recover_pit_count();
        if (current > ps->reload) return -1;
        u32 delta=previous>=current ? previous-current:previous+ps->reload-current;
        if (delta >= counts-elapsed) return 0;
        elapsed+=delta; previous=current;
    }
    return -1; /* finite even if the PIT stops */
}

int recover_indexed_cleanup(struct recover *r, const struct recover_local_step *steps,
                            u32 count, struct recover_window *window)
{
    int rc = 0;
    if ((!steps && count) || count > RECOVER_MAX_REGS) rc = -1;
    for (u32 i = 0; !rc && i < count; i++) {
        if (steps[i].role > REC_LOCAL_INDEX || (i && steps[i].role < steps[i-1].role) ||
            (steps[i].role == REC_LOCAL_INDEX && steps[i].dir != REC_SELECT) ||
            (steps[i].dir != REC_SELECT && steps[i].dir != REC_DATA)) rc = -1;
    }
    for (u32 i = 0; !rc && i < count; i++) {
        u32 value = steps[i].value;
        if (recover_restore_point(r) || recover_access(r, RECOVER_NONE,
            steps[i].dir, steps[i].key, steps[i].width, &value)) rc = -1;
    }
    /* Always release the local window even after a failed indexed access. */
    if (window && recover_window_cleanup(window)) rc = -1;
    return rc;
}

/* Minimal GF 0:13.0 recovery model. ROM W rules are intentionally absent:
 * S1/S2 appends classified rules to the same descriptor, not another allowlist.
 * References: DESIGN.final §3/§5; gf6200/PLAN.final §1 PMC INTR_ENABLE 0140h;
 * envytools docs/hw/bus/{pci,pmc}.rst, MIT; xf86-video-nv nv_driver.c:2290.
 * This is not the 619-candidate classification or a real ROM implementation. */
#define RECOVER_GF_BDF (13U << 3)
#define RECOVER_GF_DEVICE 0x022110deU
#define RECOVER_GF_PMC_IRQ 0x0140U
#define RECOVER_GF_COMMAND_MASK 0x0107U
#define GF_CMD 0U
#define GF_OUT 1U
#define GF_LEGACY 2U
#define GF_IRQ 3U
#define GF_CFG50 4U
#define GF_CFG54 5U
#define GF_RESTORE_CMD 6U
static const struct recover_target gf_targets[] = {
    {REC_PCI_CFG,0,RECOVER_COMMAND,0,0,RECOVER_NONE,2,REC_RW,1},
    {REC_BAR_MMIO,0,RECOVER_PBUS1850,0,0,RECOVER_NONE,4,REC_RW,1},
    {REC_BAR_MMIO,0,RECOVER_PBUS1854,0,0,RECOVER_NONE,4,REC_RW,1},
    {REC_BAR_MMIO,0,RECOVER_GF_PMC_IRQ,0,0,RECOVER_NONE,4,REC_RW,1},
    {REC_PCI_CFG,0,RECOVER_CFG50,0,0,RECOVER_NONE,4,REC_RW,1},
    {REC_PCI_CFG,0,RECOVER_CFG54,0,0,RECOVER_NONE,4,REC_RW,1},
};
static const u32 gf_after_out[]={GF_OUT}, gf_after_legacy[]={GF_LEGACY};
static const u32 gf_after_irq[]={GF_IRQ}, gf_after_50[]={GF_CFG50}, gf_after_54[]={GF_CFG54};
#define GF_RULE(id_,key_,phase_,mask_,verify_,bits_,deps_,n_) \
    { .id=id_, .key=key_, .phase=phase_, .main_id=RECOVER_NONE, .pc=RECOVER_NONE, \
      .substep=REC_WRITE, .write_mask=mask_, .verify_mask=verify_, \
      .value={.mode=REC_LITERAL,.mask=mask_,.bits=bits_,.expr_id=RECOVER_NONE}, \
      .requires=deps_, .requires_count=n_ }
static const struct recover_rule gf_rules[]={
    GF_RULE(GF_OUT,GF_OUT,REC_QUIESCE,~0U,~0U,0,0,0),
    GF_RULE(GF_LEGACY,GF_LEGACY,REC_QUIESCE,~0U,~0U,0,gf_after_out,1),
    GF_RULE(GF_IRQ,GF_IRQ,REC_QUIESCE,~0U,~0U,0,gf_after_legacy,1),
    GF_RULE(GF_CFG50,GF_CFG50,REC_QUIESCE,~0U,~0U,0,gf_after_irq,1),
    GF_RULE(GF_CFG54,GF_CFG54,REC_QUIESCE,~0U,~0U,0,gf_after_50,1),
    GF_RULE(GF_CMD,GF_CMD,REC_QUIESCE,RECOVER_GF_COMMAND_MASK,0xffffU,RECOVER_COMMAND_OVERRIDE,gf_after_54,1),
    GF_RULE(GF_RESTORE_CMD,GF_CMD,REC_RESTORE,RECOVER_GF_COMMAND_MASK,0xffffU,RECOVER_COMMAND_OVERRIDE,0,0),
};
#undef GF_RULE
static const struct recover_reg gf_regs[]={
    {GF_CMD,2,RECOVER_GF_COMMAND_MASK,0xffffU,REC_RW,true,GF_RESTORE_CMD}
};
static const struct recover_write gf_quiet[]={
    {GF_OUT,4,0,~0U,GF_OUT}, {GF_LEGACY,4,0,~0U,GF_LEGACY},
    {GF_IRQ,4,0,~0U,GF_IRQ}, {GF_CFG50,4,0,~0U,GF_CFG50}, {GF_CFG54,4,0,~0U,GF_CFG54},
    {GF_CMD,2,RECOVER_COMMAND_OVERRIDE,0xffffU,GF_CMD}
};
int recover_gf_profile(struct recover_profile *p, bool cold, u32 kind, u32 hz)
{
    if (!p || (kind != REC_BUDGET_GF && kind != REC_BUDGET_SIZE_BARS)) return -1;
    *p=(struct recover_profile){.version=RECOVER_PROFILE_VERSION,
        .expected_bdf=RECOVER_GF_BDF,.expected_device=RECOVER_GF_DEVICE,
        .targets=gf_targets,.target_count=sizeof(gf_targets)/sizeof(*gf_targets),
        .rules=gf_rules,.rule_count=sizeof(gf_rules)/sizeof(*gf_rules),
        .regs=gf_regs,.count=1,.quiesce=gf_quiet,.quiesce_count=sizeof(gf_quiet)/sizeof(*gf_quiet),
        .cold_required=cold};
    /* Static upper envelope of DESIGN.final §5 REC rows, maximum decimal
     * widths including the failure and final override rows. T3's serializer
     * must fit this grammar; adding fields/operations requires revalidation.
     * Count the actual T2 sequence, not an average line length: snapshot,
     * local cleanup, six quiesce writes, Command override, verification,
     * terminal event; six extra map/unmap pairs for the three MMIO W/Rs.
     * SIZE_BARS adds three BAR restore/readback pairs plus its Command fence.
     * ②: route R/W/R, safe, display, cell R/W/R/W/R, redraw, final verify.
     * E is conservatively entirely counted as 1ms waits, per the addendum. */
    static const char begin_row[] =
        "REC sid=4294967295 seq=4294967295 phase=restore event=BEGIN step=4294967295 addr=4294967295\n";
    static const char end_row[] =
        "REC sid=4294967295 seq=4294967295 phase=restore event=END rc=-2147483648 elapsed=4294967295\n";
    static const char final_row[] =
        "REC sid=4294967295 seq=4294967295 phase=final event=END result=PARTIAL target=S0 verified=4294967295 unknown=4294967295 override=4294967295 saved=4294967295 actual=4294967295 trial=4294967295 lost_lines=4294967295 discarded_bytes=4294967295\n";
    static const char discard_row[] =
        "REC sid=4294967295 seq=4294967295 phase=receive event=DISCARD discarded_bytes=4294967295\n";
    static const char state_row[] =
        "REC sid=4294967295 seq=4294967295 phase=state event=RESTORING version=4294967295 profile_crc=4294967295 plan_hash=4294967295 snapshot_crc=4294967295\n";
    u32 pair_bytes=sizeof(begin_row)+sizeof(end_row)-2;
    u32 s0_bytes=14*pair_bytes+sizeof(final_row)+sizeof(discard_row)-2;
    u32 main_pairs=1+1+p->quiesce_count+1+1+1+6;
    if (kind==REC_BUDGET_SIZE_BARS) main_pairs+=7;
    struct recover_static_work work={
        main_pairs*pair_bytes+s0_bytes+sizeof(state_row)-1, kind==REC_BUDGET_SIZE_BARS ? 6000:4000,
        main_pairs*8+256, s0_bytes,2000,256};

    return recover_budget_apply(p,kind,hz,9600,10,&work);
}

/* Independent proof via PCI config mirrors; never MMIO when BAR/Mem failed.
 * A missing/failed proof returns before the common HAL/TVRAM path. This only
 * permits independent ②; it never clears a failure of ① or declares S1. */
int recover_gf_screen_safe(struct recover *r)
{
    const u32 keys[]={GF_CMD,GF_CFG50,GF_CFG54};
    for(u32 i=0;i<sizeof(keys)/sizeof(*keys);i++) {
        u32 value;
        struct recover_bus_access access;
        if (!r->profile || keys[i]>=r->profile->target_count) return -1;
        const struct recover_target *target=&r->profile->targets[keys[i]];
        /* access_blocked forbids suspect GPU access. These three safe PCI
         * reads are the independent ② proof, not a reopening of that gate. */
        if (target->space!=REC_PCI_CFG || !target->read_safe ||
            recover_target_address(target,REC_READ,&access) || recover_restore_point(r)) return -1;
        access.key=keys[i];
        if (r->ops->read(r->opaque,&access,&value)) return -1;
        if (i ? value!=0 : (value & (RECOVER_COMMAND_DMA_MEM & ~RECOVER_COMMAND_MEM))!=0) return -1;
    }
    return 0;
}
