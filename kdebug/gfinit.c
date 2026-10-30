/* Diagnostic owner: bound stage approvals and the recorded common runner. */
#include "gfinit.h"
#include "gf_production.h"
#include "hwdev/owner.h"
#include "gfinit_bus.h"
#include "gfinit_exec.h"
#include "gfinit_plan.h"
#include "gfinit_plan_c.h"
#include "gf_stage_state.h"
#include "gfinit_verify.inc"
#include "gfinit_budget.inc"
#include "gfinit_resources.h"
#include "recover_gf_expand.h"
#include "kpeek.h"
#include "kpeek_allow.h"
#include "mm/pgalloc.h"
#include "lib/kstring.h"
#include "arch/x86/kernel/idt.h"
#include "kernel/module/module.h"
#include "drivers/pci.h"
#include "drivers/serial.h"
#include "include/bootinfo.h"
#include "include/hwdev_private.h"
#include "sys.h"
#include "kprintf.h"
#include "../lib/crc32_core.inc"
#ifdef HWDEV_MODULE
#include "hwdev/generated/hwdev_import_macros.h"
#else
#include "generated/kdebug_import_macros.h"
#endif

#define GF_US_PER_TICK (RECOVER_UART_US / PIT_HZ)
#define GF_ROM_MAX KPEEK_NV_PROM_LEN
/* Each allocation is rounded independently; the old size-plan flow is freed
 * before the maximum flow allocation. Static control is not trial RAM. */
#define GF_WORK_MAX_PAGES HWDEV_WORK_MAX_PAGES
STATIC_ASSERT(GF_WORK_MAX_PAGES == HWDEV_WORK_MAX_PAGES, gf_work_page_contract);
#define GF_FLOW_MAX_BYTES (RECOVER_NODE_MAX * RECOVER_BITS_WORDS * sizeof(u32))
STATIC_ASSERT(RECOVER_NODE_MAX == 2048U, gf_work_node_contract);
STATIC_ASSERT((PAGE_ALIGN_UP(GF_ROM_MAX) +
               PAGE_ALIGN_UP(sizeof(struct gfinit_decode)) +
               PAGE_ALIGN_UP(sizeof(struct gfinit_plan)) +
               PAGE_ALIGN_UP(GF_FLOW_MAX_BYTES)) / PAGE_SIZE <= GF_WORK_MAX_PAGES,
              gf_work_page_contract);
struct gfinit_owner {
    struct gfinit_bus bus;
    struct recover_ops ops;
    struct recover_profile profile;
    const struct recover_profile *s0;
    struct gfinit_decode *decoded;
    struct gfinit_plan *compiled;
    struct recover_plan size_plan;
    struct recover_plan_buffers buffers;
    u32 *flow;
    u32 flow_bytes;
    u32 valid[RECOVER_BITS_WORDS];
    struct recover_range exclude[RECOVER_RANGE_MAX];
    u32 exclude_count, rom_size, next_main, last_op, last_main;
    struct gfinit_request last;
    struct gfinit_exec *execution;
    u8 *rom;
    bool inspected;
    u32 generation, prepare_flags;
    u32 last_count;
    struct gfinit_no_verify last_regs[GF_NO_VERIFY_MAX];
};
static struct gfinit_owner control; /* small, stable callbacks survive terminal status */
static struct gfinit_owner *owner;
/* Diagnostics claim only this fixed, stopped device. Production selection
 * rejects this owner; IDE, LAN/SerialFS and unknown inputs never claim. */
static struct hwdev_device_owner gf_device = {
    .bdf = RECOVER_GF_BDF, .generation = 1, .state = HWDEV_STOPPED_UNOWNED
};
static struct hwdev_lease gf_lease;
static struct gf_stage_state stage;
static bool gf_owned(struct recover *r)
{
    return owner && hwdev_lease_matches(&gf_lease, &gf_device,
                                        owner->generation, r->token.session);
}

static void *gf_alloc(u32 bytes)
{
    u32 phys = pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, PAGE_ALIGN_UP(bytes) / PAGE_SIZE);
    if (!phys) return 0;
    void *p = P2V(phys);
    kmemset(p, 0, bytes);
    return p;
}
static void gf_free(void *p, u32 bytes)
{
    if (p) (void)pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, V2P(p) / PAGE_SIZE,
                                     PAGE_ALIGN_UP(bytes) / PAGE_SIZE);
}
static const struct gfinit_pci_ops gf_pci = {
    pci_cfg_read32, pci_cfg_read16, pci_cfg_write32, pci_cfg_write16
};

static int gf_read(void *opaque, const struct recover_bus_access *a, u32 *value)
{
    if (!gf_owned(recover_owner())) return -1;
    int rc = gfinit_bus_read(opaque, a, value);
    if (!rc && owner && owner->compiled &&
        recover_owner()->plan == &owner->compiled->plan)
        gfinit_plan_read(owner->compiled, recover_owner(), a, *value);
    return rc;
}
static int gf_write(void *opaque, const struct recover_bus_access *a, u32 value)
{
    if (!gf_owned(recover_owner())) return -1;
    return gfinit_bus_write(opaque, a, value);
}
static int gf_local(void *opaque)
{
    int rc = owner ? gfinit_exec_restore(owner->execution) : 0;
    int local = gfinit_bus_local_cleanup(opaque,recover_owner());
    return rc || local ? -1 : 0;
}
static int gf_mode_vram(void *opaque, struct recover *r, bool write,
                         u32 *bad_offset, u32 *expected, u32 *actual)
{
    if (!gf_owned(r)) return -1;
    return gfinit_plan_c_vram(opaque,r,write,bad_offset,expected,actual);
}
static int gf_mode_color(void *opaque, struct recover *r, u32 row, u32 value)
{
    if (!gf_owned(r)) return -1;
    return gfinit_plan_c_color(opaque,r,row,value);
}
static int gf_screen_safe(void *opaque)
{ (void)opaque; return recover_gf_screen_safe(recover_owner()); }
static int gf_clock(void *opaque, u32 *value)
{
    struct recover *r = opaque;
    if (r->spent > ~0U / GF_US_PER_TICK) return -1;
    *value = r->spent * GF_US_PER_TICK;
    return 0;
}
static const struct gfinit_exec_ops gf_clock_ops = {gf_clock, recover_hal_wait_frame};
#include "../hwdev/gf/rom.inc"
#include "gfinit/rom.inc"
static int gf_release_rom(struct gfinit_owner *g)
{
    struct recover *r = recover_owner();
    /* Detach even a partially bound plan before returning its backing pages.
     * INSPECT has not installed callbacks and therefore needs no detachment. */
    if ((r->ops == &g->ops || r->profile == &g->profile ||
         r->bound_profile == &g->profile) &&
        recover_owner_configure(0, 0, 0, false)) return -1;
    stage = (struct gf_stage_state){0};
    r->gf_stage = 0; r->c_only = false; r->verify_record = false;
    gf_free(g->rom, GF_ROM_MAX);
    gf_free(g->decoded, sizeof(*g->decoded));
    gf_free(g->compiled, sizeof(*g->compiled));
    gf_free(g->flow, g->flow_bytes);
    g->decoded = 0; g->compiled = 0; g->flow = 0; g->flow_bytes = 0;
    g->buffers = (struct recover_plan_buffers){0};
    (void)recover_gf_profile_expand(&g->profile, 0);
    g->last_op = RECOVER_NONE;
    g->prepare_flags = 0;
    g->rom = 0; g->inspected = false;
    gf_lease.active = false;
    return 0;
}
void gfinit_recover_end(void)
{
    struct recover *r = recover_owner();
    if (owner && !stage.initialized && (r->state == REC_VERIFIED || r->state == REC_FAILED))
        gf_release_rom(owner);
}
static int gf_inspect(struct recover *r, const struct recover_ops *base,
                       struct recover_transport *transport, const struct recover_profile *s0)
{
    if (r->state != REC_IDLE && r->state != REC_VERIFIED) return -1;
    if (r->failed || r->cold_latched) return -1;
    if (!owner) owner = &control;
    if (!owner) return -1;
    struct gfinit_owner *g = owner;
    if (gf_release_rom(g)) return -1;
    if (hwdev_claim(&gf_lease, &gf_device, r->token.session)) return -1;
    g->generation = gf_lease.generation;
    g->compiled = gf_alloc(sizeof(*g->compiled));
    if (!g->compiled || recover_gf_profile_expand(&g->profile, g->compiled)) {
        gf_release_rom(g); return -1;
    }
    g->profile.expected_bdf = RECOVER_GF_BDF; g->profile.expected_device = RECOVER_GF_DEVICE;
    gfinit_bus_init(&g->bus, &g->profile, &gf_pci);
    BootImageInfo boot;
    if (boot_image_info(&boot) || !boot.crc_valid || gfinit_bus_probe(&g->bus)) {
        gf_release_rom(g); return -1;
    }
    g->bus.identity.boot = 1; /* singleton lifetime is this boot, never persisted */
    g->bus.identity.image_version = boot.image_crc;
    g->bus.identity.kdebug_version = KDEBUG_OPS_VERSION;
    g->ops = *base;
    g->ops.identity = gfinit_bus_identity; g->ops.raw_config = gfinit_bus_raw_config;
    g->ops.read = gf_read; g->ops.write = gf_write;
    g->ops.mode_vram = gf_mode_vram; g->ops.mode_color = gf_mode_color;
    g->ops.mode_wait = recover_hal_wait_frame;
    g->ops.local_cleanup = gf_local; g->ops.screen_safe = gf_screen_safe;
    g->s0 = s0;
    r->transport = transport;
    g->decoded = gf_alloc(sizeof(*g->decoded));
    u32 len = 0, crc = 0, chip = GF_CHIP_UNKNOWN;
    int rc = g->decoded ? gf_read_rom(g, &len, &crc, &chip) : -1;
    gf_inspect_line(r->token.session, rc, len, crc, chip);
    if (rc) { gf_release_rom(g); return -1; }
    g->inspected = true; g->next_main = 0;
    return 0;
}
/* Static upper bound from every expanded node/READ, including both branch
 * arms. Each access can emit old/BEGIN/verify/END and each node PRE/END. */
static int gf_budget(struct gfinit_owner *g, struct recover *r,
                     struct recover_transport *transport, u32 kind,
                     const struct recover_plan *plan)
{
    const struct serial_setup *serial = serial_get_setup();
    const struct recover_static_work work = {
        kind == REC_BUDGET_SIZE_BARS ? RECOVER_BUDGET_BYTES_SIZE_BARS : RECOVER_BUDGET_BYTES_RUN,
        kind == REC_BUDGET_SIZE_BARS ? RECOVER_BUDGET_EXEC_MS_SIZE_BARS : RECOVER_BUDGET_EXEC_MS_RUN, RECOVER_BUDGET_POINTS,
        RECOVER_BUDGET_BYTES_S0, RECOVER_BUDGET_S0_WAIT_MS, RECOVER_BUDGET_POINTS
    };
    if (!serial || !serial->actual || recover_budget_apply(&g->profile, kind,
            PIT_HZ, serial->actual, RECOVER_UART_FRAME_BITS, &work)) return -1;
    u32 rows, first = RECOVER_NONE;
    if (kind == REC_BUDGET_GF && (g->compiled->mode_flags & GF_MODE_C))
        first = g->compiled->c_node;
    if (gfinit_log_rows(plan, first, &rows)) return -1;
#if GF_VPLL_PROBE
    if (kind == REC_BUDGET_GF) {
        if (rows > ~0U - RECOVER_VPLL_ROWS) return -1;
        rows += RECOVER_VPLL_ROWS;
    }
#endif
    if (rows > ~0U / (RECOVER_ROW_MAX + 1U)) return -1;
    u32 bytes = rows * (RECOVER_ROW_MAX + 1U);
    u32 seconds = bytes / serial->actual;
    if (seconds > (~0U - RECOVER_UART_FRAME_BITS) / RECOVER_UART_FRAME_BITS) return -1;
    seconds = seconds * RECOVER_UART_FRAME_BITS + (bytes % serial->actual * RECOVER_UART_FRAME_BITS + serial->actual - 1U) / serial->actual;
    if (seconds > (RECOVER_HALF_TICKS / PIT_HZ - RECOVER_BUDGET_MARGIN_S) / 2U ||
        bytes > (~0U - 1024U) / 2U) return -1;
    r->log_limit = (seconds * 2U + RECOVER_BUDGET_MARGIN_S) * PIT_HZ;
    transport->max_polls = bytes * 2U + 1024U;
    if (transport->max_polls < RECOVER_CLEANUP_TRANSPORT_POLLS)
        transport->max_polls = RECOVER_CLEANUP_TRANSPORT_POLLS;
    g->profile.crc = recover_profile_crc(&g->profile);
    return 0;
}
static int gf_fixed(struct recover *r, void *arg)
{
    struct gfinit_owner *g = arg;
    while (r->expected_node != RECOVER_NONE) {
        u32 id = r->expected_node;
        const struct recover_plan_node *n = &r->plan->nodes[id];
        if (n->phase == REC_RUN || n->phase == REC_VRAM_TEST) break;
        u32 value = 0;
        if (recover_safe_point(r) || recover_plan_enter(r, id) ||
            recover_plan_access(r, &value)) return -1;
        if (n->phase == REC_SIZE_BARS && r->node_layout[id]->dir_substep == REC_READ) {
            u32 key = r->plan->args[n->arg_first].value;
            const struct recover_target *t = &r->profile->targets[key];
            if (t->space != REC_PCI_CFG || t->offset < PCI_CFG_BAR0 || t->offset > PCI_CFG_BAR0 + PCI_CFG_BAR_STRIDE * (RECOVER_BAR_COUNT - 1)) return -1;
            u32 bar = (t->offset - PCI_CFG_BAR0) / PCI_CFG_BAR_STRIDE;
            if (recover_bar_probe(r, bar, g->bus.bars[bar], value,
                    g->exclude, g->exclude_count, 0, 0)) return -1;
        }
        if (recover_plan_leave(r, 0)) return -1;
    }
    return 0;
}
static int gf_modeset(struct recover *r, void *arg)
{
    struct gfinit_plan *plan = arg;
    while (r->expected_node != RECOVER_NONE) {
        u32 id = r->expected_node, value = 0, edge = 0;
        const struct recover_opcode_layout *layout = r->node_layout[id];
        if (recover_safe_point(r) || recover_plan_enter(r, id)) return -1;
        int rc = 0;
        if (layout->flags & (REC_LAYOUT_MODE_CHECK | REC_LAYOUT_MODE_WAIT | REC_LAYOUT_MODE_ROUTE))
            rc = gfinit_plan_c_control(plan, r);
        else if (layout->flags & REC_LAYOUT_CHECK_EXPR)
            rc = recover_plan_check(r, &edge);
        else if (layout->dir_substep != RECOVER_NONE ||
                 (layout->flags & (REC_LAYOUT_MODE_VRAM | REC_LAYOUT_MODE_COLOR)))
            rc = recover_plan_access(r, &value);
        if (rc || recover_plan_leave(r, edge)) return -1;
    }
    return 0;
}
static int gf_begin(struct recover *r, struct gfinit_owner *g,
                    struct recover_transport *transport, bool size, u32 flags,
                    const struct gfinit_no_verify *regs, u32 count)
{
    if (!g || !g->inspected || !gf_owned(r) || r->cold_latched ||
        (r->state != REC_IDLE && r->state != REC_VERIFIED)) return -1;
    if (!g->compiled || recover_gf_profile_expand(&g->profile, g->compiled)) return -1;
    g->profile.expected_bdf = RECOVER_GF_BDF; g->profile.expected_device = RECOVER_GF_DEVICE;
    const struct recover_plan *plan;
    if (size) {
        if (gfinit_resources(g->exclude, &g->exclude_count)) return -1;
        g->profile.cold_required = false;
        g->size_plan = recover_gf_size_bars_plan;
        plan = &g->size_plan;
    } else {
        if (!r->cache.bar[1].valid || gfinit_decode_rom(g->rom, g->rom_size,
                &g->profile, g->decoded)) return -1;
        g->prepare_flags = flags;
        if (flags & GF_MODE_C) {
            if (gfinit_plan_build_c(g->compiled, g->decoded, &g->profile,
                    r->cache.bar[1].length, g->rom, g->rom_size) ||
                gfinit_verify_policy(g->compiled, &g->profile, flags, regs, count)) return -1;
        } else if (gfinit_plan_build(g->compiled, g->decoded, &g->profile,
                                     r->cache.bar[1].length)) return -1;
        plan = &g->compiled->plan;
    }
    if (!size && gfinit_plan_baseline(g->compiled,&g->profile,&stage,g->rom,g->rom_size)) return -1;
    if (!size) r->gf_stage = &stage;
    r->c_only = false; r->verify_record = (flags & GF_VERIFY_RECORD) != 0;
    if (!plan->node_count || plan->node_count > RECOVER_NODE_MAX) return -1;
    if (gf_budget(g, r, transport, size ? REC_BUDGET_SIZE_BARS : REC_BUDGET_GF, plan)) return -1;
    if (size) g->size_plan.profile_crc = g->profile.crc;
    else g->compiled->plan.profile_crc = g->profile.crc;
    gf_free(g->flow, g->flow_bytes);
    g->flow_bytes = plan->node_count * RECOVER_BITS_WORDS * sizeof(u32);
    g->flow = gf_alloc(g->flow_bytes);
    if (!g->flow) return -1;
    g->buffers = (struct recover_plan_buffers){g->flow, g->valid};
    u32 hash;
    if (recover_owner_configure(&g->ops, &g->bus, g->s0, false) ||
        recover_bind_plan(r, &g->profile, plan, &g->buffers, &hash)) return -1;
    /* The approved fixed C route must also be returned to 98 by S0. */
    if (!size && (flags & GF_MODE_C)) r->route_write_approved = true;
    /* Snapshot callbacks run after trial_begin advances the core session. */
    gf_lease.session = r->next_session + 1U;
    if (trial_begin(r, &g->profile, hash, g->profile.trial_limit)) return -1;
    gf_lease.session = r->token.session;
    struct gfinit_exec e={.rom=g->rom,.rom_size=g->rom_size,.ops=&gf_clock_ops,.opaque=r};
    g->execution=size?0:&e;
    int rc=trial_step(r,&r->token,true,size?gf_fixed:gfinit_exec_prepare,size?(void *)g:(void *)&e);
    g->execution=0;
    if(rc)return -1;
    if (size) return trial_end(r, &r->token, REC_NORMAL);
    return 0;
}
static int gf_c_only(struct recover *r, struct recover_transport *transport,
                      u32 flags, const struct gfinit_no_verify *regs, u32 count)
{
    struct gfinit_owner *g=owner;
    if (!g || !gf_owned(r) || !stage.initialized ||
        (r->state != REC_VERIFIED && r->state != REC_FAILED)) return -1;
    if (gf_stage_entry(r)) { stage.initialized=false; return -1; }
    stage.entry_ok=true;
    r->c_only=true;
    if (!gf_stage_reentry(r) ||
        recover_gf_profile_expand(&g->profile,g->compiled)) goto invalid;
    g->profile.expected_bdf=RECOVER_GF_BDF;
    g->profile.expected_device=RECOVER_GF_DEVICE;
    if (gfinit_plan_rebuild_c(g->compiled,g->decoded,&g->profile,&stage) ||
        gfinit_verify_policy(g->compiled,&g->profile,flags,regs,count)) goto invalid;
    if (g->compiled->plan.node_count * RECOVER_BITS_WORDS * sizeof(u32) > g->flow_bytes) goto invalid;
    g->profile.trial_limit=GF_C_EXEC_SECONDS * PIT_HZ;
    g->profile.opcode_limit=g->profile.trial_limit;
    g->profile.max_steps=GF_INIT_SCHEDULE;
    g->profile.max_polls=GF_C_EXEC_POLLS;
    g->profile.restore_limit=GF_C_RESTORE_SECONDS * PIT_HZ;
    g->profile.restore_max_polls=GF_C_RESTORE_POLLS;
    g->profile.restore_log_bytes=GF_C_RESTORE_BYTES;
    g->profile.s0_reserve_ticks=GF_C_S0_SECONDS * PIT_HZ;
    g->profile.s0_reserve_polls=GF_C_S0_POLLS;
    g->profile.s0_reserve_bytes=GF_C_S0_BYTES;
    r->log_limit=GF_C_TX_SECONDS * PIT_HZ;
    transport->max_polls=GF_C_TX_POLLS;
    g->profile.crc=recover_profile_crc(&g->profile);
    g->compiled->plan.profile_crc=g->profile.crc;
    r->verify_record=(flags & GF_VERIFY_RECORD) != 0;
    u32 hash;
    if (recover_owner_configure(&g->ops,&g->bus,g->s0,false) ||
        recover_bind_plan(r,&g->profile,&g->compiled->plan,&g->buffers,&hash)) goto invalid;
    r->route_write_approved=true;
    gf_lease.session=r->next_session+1U;
    if (trial_begin(r,&g->profile,hash,g->profile.trial_limit)) {
        gf_lease.session=r->token.session;
        goto invalid;
    }
    gf_lease.session=r->token.session;
    stage.entry_ok=false;
    g->prepare_flags=flags;
    return trial_step(r,&r->token,true,gf_modeset,g->compiled);
invalid:
    stage.initialized=false;
    return -1;
}

int gfinit_call(u32 op, void *buf, u32 size, const struct recover_ops *base,
                struct recover_transport *transport, const struct recover_profile *s0)
{
    if (gf_production_selected()) return -1;
    if (!buf || size < sizeof(struct gfinit_request)) return -1;
    struct gfinit_request q = *(const struct gfinit_request *)buf;
    const struct gfinit_prepare_request *extra = buf;
    const struct gfinit_no_verify *regs = 0;
    u32 count = 0;
    if (q.flags & GF_NO_VERIFY_FILE) {
        if ((op != GF_OP_PREPARE && op != GF_OP_C_ONLY) || size != sizeof(*extra) ||
            extra->count > GF_NO_VERIFY_MAX) return -1;
        count = extra->count; regs = extra->regs;
    } else if (size != sizeof(q)) return -1;
    struct recover *r = recover_owner();
    if (q.version != GF_INIT_VERSION || q.size != size ||
        (q.flags & ~(GF_ALLOW_WRITE | ((op == GF_OP_PREPARE || op == GF_OP_C_ONLY) ? (GF_MODE_C | GF_VERIFY_REQUIRED | GF_VERIFY_RECORD | GF_NO_VERIFY_FILE) : 0U))) || q.max_steps != GF_INIT_SCHEDULE ||
        ((q.flags & (GF_VERIFY_REQUIRED | GF_VERIFY_RECORD | GF_NO_VERIFY_FILE)) && !(q.flags & GF_MODE_C)) ||
        ((q.flags & GF_VERIFY_REQUIRED) && (q.flags & GF_VERIFY_RECORD)) ||
        q.status || q.fail_pc || q.fail_reg || q.steps ||
        (op != GF_OP_RUN_ONE && q.script_id) ||
        (op != GF_OP_INSPECT && !(q.flags & GF_ALLOW_WRITE))) return -1;
    if (owner && owner->inspected && !gf_owned(r)) {
        if (r->state == REC_IDLE || r->state == REC_VERIFIED) gf_release_rom(owner);
        return -1;
    }
    bool same_regs = owner && owner->last_count == count;
    for (u32 i = 0; same_regs && i < count; i++)
        if (owner->last_regs[i].kind != regs[i].kind || owner->last_regs[i].reg != regs[i].reg)
            same_regs = false;
    if (same_regs && owner && q.session && q.session == r->token.session && owner->last_op == op &&
        owner->last_main == q.script_id && owner->last.flags == q.flags) {
        *(struct gfinit_request *)buf = owner->last; return owner->last.status ? -1 : 0;
    }
    if (op != GF_OP_C_ONLY && q.session != r->token.session) return -1;
    if (op == GF_OP_C_ONLY && (q.session || !(q.flags & GF_MODE_C))) return -1;
    int rc = -1;
    if (op == GF_OP_C_ONLY) rc = gf_c_only(r,transport,q.flags,regs,count);
    else if (op == GF_OP_INSPECT) rc = gf_inspect(r, base, transport, s0);
    else if (op == GF_OP_SIZE_BARS || op == GF_OP_PREPARE)
        rc = gf_begin(r, owner, transport, op == GF_OP_SIZE_BARS, q.flags, regs, count);
    else if (owner && owner->inspected && op == GF_OP_RUN_ONE &&
             q.script_id == owner->next_main && q.script_id < GF_INIT_MAINS) {
        struct gfinit_exec e = {.rom = owner->rom, .rom_size = owner->rom_size,
            .main_id = q.script_id, .ops = &gf_clock_ops, .opaque = r,
            .program = &owner->decoded->program};
        owner->execution = &e;
        rc = trial_step(r, &r->token, true, gfinit_exec_run, &e);
        owner->execution = 0;
        q.steps = e.steps; q.fail_pc = e.fail_pc;
        if (!rc) owner->next_main++;
#if !GF_VPLL_PROBE
        if (!rc && owner->next_main == GF_INIT_MAINS)
            rc = trial_step(r,&r->token,true,gf_stage_capture,&stage);
#endif
    } else if (owner && op == GF_OP_MODESET && (owner->prepare_flags & GF_MODE_C) &&
               owner->next_main == GF_INIT_MAINS && r->state == REC_PAUSED) {
        rc = trial_step(r, &r->token, true, gf_modeset, owner->compiled);
    } else if (owner && op == GF_OP_VERIFY && owner->next_main == GF_INIT_MAINS &&
               !(owner->prepare_flags & GF_MODE_C)) {
        rc = trial_step(r, &r->token, true, gfinit_plan_verify, owner->compiled);
        if (!rc) rc = trial_end(r, &r->token, REC_NORMAL);
    } else if (op == GF_OP_ABORT) {
        if (owner && owner->inspected && r->state == REC_IDLE) {
            gf_release_rom(owner); rc = 0;
        } else rc = trial_end(r, &r->token, REC_ABORT);
    }
    if (op == GF_OP_C_ONLY || op == GF_OP_MODESET)
        kprintf(ATTR_WHITE,"GF C verify=%s result=%s\n",
            (op == GF_OP_C_ONLY ? (q.flags & GF_VERIFY_RECORD) != 0 : r->verify_record)
                ? "record" : "required", rc ? "NG" : "OK");
    q.session = r->token.session; q.status = rc ? r->reason + 1U : 0;
    q.fail_reg = r->first_key;
    if (owner) {
        owner->last_op = op; owner->last_main = q.script_id; owner->last = q;
        owner->last_count = count;
        for (u32 i = 0; i < count; i++) owner->last_regs[i] = regs[i];
        if (!stage.initialized && ((rc && (r->state == REC_IDLE || r->state == REC_VERIFIED)) ||
            ((r->state == REC_VERIFIED || r->state == REC_FAILED) &&
            op != GF_OP_INSPECT && (op != GF_OP_SIZE_BARS || r->state == REC_FAILED))))
            gf_release_rom(owner);
    }
    *(struct gfinit_request *)buf = q;
    return rc;
}
