/* GF4a owner. All trial writes go through recover's immutable plan. */
#include "gfinit.h"
#include "hwdev/owner.h"
#include "gfinit_bus.h"
#include "gfinit_exec.h"
#include "gfinit_plan.h"
#include "gfinit_plan_c.h"
#include "gfinit_resources.h"
#include "recover_gf_expand.h"
#include "kpeek.h"
#include "kpeek_allow.h"
#include "kernel/pgalloc.h"
#include "lib/kstring.h"
#include "kernel/idt.h"
#include "kernel/module.h"
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
    u8 *rom;
    bool inspected;
    u32 generation, prepare_flags;
};
static struct gfinit_owner control; /* small, stable callbacks survive terminal status */
static struct gfinit_owner *owner;
/* There is no production GF driver in v1. Only this fixed, stopped device is
 * classified; IDE, LAN/SerialFS, display and unknown inputs never claim. */
static struct hwdev_device_owner gf_device = {
    .bdf = RECOVER_GF_BDF, .generation = 1, .state = HWDEV_STOPPED_UNOWNED
};
static struct hwdev_lease gf_lease;
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
{ return gfinit_bus_local_cleanup(opaque, recover_owner()); }
static int gf_mode_vram(void *opaque, struct recover *r, bool write,
                         u32 *bad_offset, u32 *expected, u32 *actual)
{
    if (!gf_owned(r)) return -1;
    return gfinit_plan_c_vram(opaque,r,write,bad_offset,expected,actual);
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
/* BIT i version byte order: envytools nvbios/info.c (MIT), +2 is chip.
 * Expected NV44/NV44A version.chip: REALHW_RECOVER_ADDENDUM, S4. */
#define GF_VERSION_CHIP 0x44U
#define GF_CHIP_UNKNOWN 0x100U
#define GF_BIT_HEADER 12U
#define GF_BIT_ENTRY 6U
#define GF_BIT_CHIP_OFFSET 2U
#define GF_BIT_I_VERSION 2U /* envytools nvbios/bit.c:50 ({ 'i', 2, ... }) */
static u32 gf_rom_chip(const u8 *rom, u32 size)
{
    static const u8 signature[] = {0xff, 0xb8, 'B', 'I', 'T', 0, 0};
    for (u32 off = 0; off + GF_BIT_HEADER <= size; off++) {
        u32 i = 0;
        while (i < sizeof(signature) && rom[off+i] == signature[i]) i++;
        if (i != sizeof(signature)) continue;
        u32 head = rom[off+8], stride = rom[off+9], count = rom[off+10];
        if (head < GF_BIT_HEADER || stride < GF_BIT_ENTRY ||
            head + count * stride > size - off) return GF_CHIP_UNKNOWN;
        for (u32 n = 0; n < count; n++) {
            const u8 *entry = rom + off + head + n * stride;
            if (entry[0] != 'i' || entry[1] != GF_BIT_I_VERSION) continue;
            u32 len = (u32)entry[2] | (u32)entry[3] << 8;
            u32 data = (u32)entry[4] | (u32)entry[5] << 8;
            if (len <= GF_BIT_CHIP_OFFSET || data > size || len > size - data)
                return GF_CHIP_UNKNOWN;
            return rom[data + GF_BIT_CHIP_OFFSET];
        }
        return GF_CHIP_UNKNOWN;
    }
    return GF_CHIP_UNKNOWN;
}
static void gf_inspect_line(u32 sid, int rc, u32 len, u32 crc, u32 chip)
{
    kprintf(ATTR_WHITE, "GF inspect sid=%u rc=%d rom_len=", sid, rc);
    if (len) kprintf(ATTR_WHITE, "%u crc=%08X", len, crc);
    else kprintf(ATTR_WHITE, "unknown crc=unknown");
    if (chip == GF_CHIP_UNKNOWN)
        kprintf(ATTR_WHITE, " chip=unknown expect=%02X match=unknown\n", GF_VERSION_CHIP);
    else kprintf(ATTR_WHITE, " chip=%02X expect=%02X match=%u\n",
                 chip, GF_VERSION_CHIP, chip == GF_VERSION_CHIP);
}
static int gf_read_rom(struct gfinit_owner *g, u32 *len, u32 *crc, u32 *chip)
{
    KdebugPeek req;
    u32 raw, bars[RECOVER_BAR_COUNT];
    if (gfinit_bus_raw_config(&g->bus, &raw, bars) || !(raw & RECOVER_COMMAND_MEM) ||
        bars[0] > ~0U - KPEEK_NV_PROM_OFF - GF_ROM_MAX) return -1;
    g->rom = gf_alloc(GF_ROM_MAX);
    if (!g->rom) return -1;
    /* Read both copies through the existing read-only PROM descriptor/window. */
    for (u32 pass = 0; pass < 2; pass++) {
        for (u32 off = 0; off < GF_ROM_MAX; off += KDEBUG_PEEK_CHUNK) {
            req = (KdebugPeek){0};
            req.addr = bars[0] + KPEEK_NV_PROM_OFF + off;
            req.len = KDEBUG_PEEK_CHUNK; req.width = 1; req.space = KDEBUG_PEEK_SPACE_MMIO;
            if (kpeek_call(KDEBUG_OP_PEEK, &req, sizeof(req)) || req.status) return -1;
            for (u32 j = 0; j < req.len; j++) {
                if (pass && g->rom[off+j] != req.data[j]) return -1;
                g->rom[off+j] = req.data[j];
            }
        }
    }
    if (g->rom[0] != PCI_ROM_SIG0 || g->rom[1] != PCI_ROM_SIG1 || !g->rom[PCI_ROM_SIZE_OFF]) return -1;
    g->rom_size = (u32)g->rom[PCI_ROM_SIZE_OFF] * PCI_ROM_SIZE_UNIT;
    if (g->rom_size > GF_ROM_MAX) return -1;
    *len = g->rom_size;
    *crc = crc32_core_final(crc32_core_update(CRC32_INIT, g->rom, *len));
    *chip = gf_rom_chip(g->rom, *len);
    u32 pcir = (u32)g->rom[PCI_ROM_PCIR_PTR] | (u32)g->rom[PCI_ROM_PCIR_PTR + 1] << 8;
    if (g->rom_size < PCIR_HEADER_SIZE || pcir > g->rom_size - PCIR_HEADER_SIZE ||
        g->rom[pcir] != 'P' || g->rom[pcir+1] != 'C' ||
        g->rom[pcir+2] != 'I' || g->rom[pcir+3] != 'R' ||
        ((u32)g->rom[pcir+PCIR_VENDOR] | (u32)g->rom[pcir+PCIR_VENDOR+1] << 8) != PCI_VENDOR_NVIDIA ||
        ((u32)g->rom[pcir+PCIR_DEVICE] | (u32)g->rom[pcir+PCIR_DEVICE+1] << 8) != PCI_DEVICE_NV44A) return -1;
    u8 sum = 0;
    for (u32 i = 0; i < g->rom_size; i++) sum += g->rom[i];
    return sum || *chip != GF_VERSION_CHIP ? -1 : gfinit_decode_rom(g->rom, g->rom_size, &g->profile, g->decoded);
}
static int gf_release_rom(struct gfinit_owner *g)
{
    struct recover *r = recover_owner();
    /* Detach even a partially bound plan before returning its backing pages.
     * INSPECT has not installed callbacks and therefore needs no detachment. */
    if ((r->ops == &g->ops || r->profile == &g->profile ||
         r->bound_profile == &g->profile) &&
        recover_owner_configure(0, 0, 0, false)) return -1;
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
    if (owner && (r->state == REC_VERIFIED || r->state == REC_FAILED))
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
    g->ops.mode_vram = gf_mode_vram;
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
    u32 rows = 128;
    if (plan->node_count > (~0U - rows) / 12U) return -1;
    rows += plan->node_count * 12U;
    if (plan->arg_count > (~0U - rows) / 4U) return -1;
    rows += plan->arg_count * 4U;
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
static int gf_begin(struct recover *r, struct gfinit_owner *g,
                    struct recover_transport *transport, bool size, u32 flags)
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
                    r->cache.bar[1].length, g->rom, g->rom_size)) return -1;
        } else if (gfinit_plan_build(g->compiled, g->decoded, &g->profile,
                                     r->cache.bar[1].length)) return -1;
        plan = &g->compiled->plan;
    }
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
    /* Snapshot callbacks run after trial_begin advances the core session. */
    gf_lease.session = r->next_session + 1U;
    if (trial_begin(r, &g->profile, hash, g->profile.trial_limit)) return -1;
    gf_lease.session = r->token.session;
    if (trial_step(r, &r->token, true, gf_fixed, g)) return -1;
    if (size) return trial_end(r, &r->token, REC_NORMAL);
    return 0;
}
int gfinit_call(u32 op, void *buf, u32 size, const struct recover_ops *base,
                struct recover_transport *transport, const struct recover_profile *s0)
{
    if (!buf || size != sizeof(struct gfinit_request)) return -1;
    struct gfinit_request q = *(const struct gfinit_request *)buf;
    struct recover *r = recover_owner();
    if (q.version != GF_INIT_VERSION || q.size != sizeof(q) ||
        (q.flags & ~(GF_ALLOW_WRITE | (op == GF_OP_PREPARE ? GF_MODE_C : 0U))) || q.max_steps != GF_INIT_SCHEDULE ||
        q.status || q.fail_pc || q.fail_reg || q.steps ||
        (op != GF_OP_RUN_ONE && q.script_id) ||
        (op != GF_OP_INSPECT && !(q.flags & GF_ALLOW_WRITE))) return -1;
    if (owner && owner->inspected && !gf_owned(r)) {
        if (r->state == REC_IDLE || r->state == REC_VERIFIED) gf_release_rom(owner);
        return -1;
    }
    if (owner && q.session && q.session == r->token.session && owner->last_op == op &&
        owner->last_main == q.script_id && owner->last.flags == q.flags) {
        *(struct gfinit_request *)buf = owner->last; return owner->last.status ? -1 : 0;
    }
    if (q.session != r->token.session) return -1;
    int rc = -1;
    if (op == GF_OP_INSPECT) rc = gf_inspect(r, base, transport, s0);
    else if (op == GF_OP_SIZE_BARS || op == GF_OP_PREPARE)
        rc = gf_begin(r, owner, transport, op == GF_OP_SIZE_BARS, q.flags);
    else if (owner && owner->inspected && op == GF_OP_RUN_ONE &&
             q.script_id == owner->next_main && q.script_id < GF_INIT_MAINS) {
        struct gfinit_exec e = {.rom = owner->rom, .rom_size = owner->rom_size,
            .main_id = q.script_id, .ops = &gf_clock_ops, .opaque = r};
        rc = trial_step(r, &r->token, true, gfinit_exec_run, &e);
        q.steps = e.steps; q.fail_pc = e.fail_pc;
        if (!rc) owner->next_main++;
    } else if (owner && op == GF_OP_VERIFY && owner->next_main == GF_INIT_MAINS &&
               !(owner->prepare_flags & GF_MODE_C)) {
        rc = trial_step(r, &r->token, true, gfinit_plan_verify, owner->compiled);
        if (!rc) rc = trial_end(r, &r->token, REC_NORMAL);
    } else if (op == GF_OP_ABORT) {
        if (owner && owner->inspected && r->state == REC_IDLE) {
            gf_release_rom(owner); rc = 0;
        } else rc = trial_end(r, &r->token, REC_ABORT);
    }
    q.session = r->token.session; q.status = rc ? r->reason + 1U : 0;
    q.fail_reg = r->first_key;
    if (owner) {
        owner->last_op = op; owner->last_main = q.script_id; owner->last = q;
        if ((rc && (r->state == REC_IDLE || r->state == REC_VERIFIED)) ||
            ((r->state == REC_VERIFIED || r->state == REC_FAILED) &&
            op != GF_OP_INSPECT && (op != GF_OP_SIZE_BARS || r->state == REC_FAILED)))
            gf_release_rom(owner);
    }
    *(struct gfinit_request *)buf = q;
    return rc;
}
