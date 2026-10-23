/* Device-free trial; only the borrowed S0 callbacks touch the display.
 * recovery_call has already validated the wire form and the case. */
#include "recover_virtual.h"

#define VIRTUAL_UART_POLLS 4096U
#define VIRTUAL_FRAME_BIT_US 10000000U
#define VIRTUAL_FRAME_BITS 10U
#define VIRTUAL_WAIT_MS 2000U
#define VIRTUAL_POINTS 256U
#define VIRTUAL_LOG_BYTES 4096U
#define VIRTUAL_ID 1U /* fixed within this image/boot; never persisted */
/* Decimal u32 maxima; sizeof includes one spare byte per row. S0 has
 * route/decode/screen plus eight nested screen BEGIN/END pairs. */
#define VIRTUAL_ROW "REC sid=4294967295 seq=4294967295 phase="
#define VIRTUAL_STATE_BYTES sizeof(VIRTUAL_ROW "state event=SNAPSHOTTING version=4294967295 profile_crc=4294967295 plan_hash=4294967295 snapshot_crc=4294967295\n")
#define VIRTUAL_BEGIN_BYTES sizeof(VIRTUAL_ROW "screen event=BEGIN step=4294967295 addr=4294967295\n")
#define VIRTUAL_END_BYTES sizeof(VIRTUAL_ROW "screen event=END rc=-2147483648 elapsed=4294967295\n")
#define VIRTUAL_FINAL_BYTES sizeof(VIRTUAL_ROW "final event=END result=PARTIAL target=S0 verified=1 unknown=1 override=4294967295 saved=4294967295 actual=4294967295 trial=4294967295 lost_lines=4294967295 discarded_bytes=4294967295\n")
#define VIRTUAL_DISCARD_BYTES sizeof(VIRTUAL_ROW "receive event=DISCARD discarded_bytes=4294967295\n")
#define VIRTUAL_RESTORE_BYTES (11U * (VIRTUAL_BEGIN_BYTES + VIRTUAL_END_BYTES) + VIRTUAL_STATE_BYTES + VIRTUAL_FINAL_BYTES + VIRTUAL_DISCARD_BYTES)
#define VIRTUAL_TRIAL_BYTES (6U * VIRTUAL_STATE_BYTES + VIRTUAL_DISCARD_BYTES + VIRTUAL_BEGIN_BYTES)
/* One ready poll per byte and one empty poll per row at minimum. */
STATIC_ASSERT(VIRTUAL_TRIAL_BYTES + 8U < VIRTUAL_UART_POLLS, virtual_trial_polls);
STATIC_ASSERT(VIRTUAL_RESTORE_BYTES <= VIRTUAL_LOG_BYTES, virtual_restore_log);
STATIC_ASSERT(VIRTUAL_RESTORE_BYTES + 25U < VIRTUAL_UART_POLLS, virtual_restore_polls);

static bool borrowed;
static u32 active_case;
static const struct recover_transport *saved_transport;
static struct recover_transport transport;
static struct recover_ops ops;
static const struct recover_ops *saved_ops;
static struct recover_profile profile;
static struct recover_token token;
static u32 flow[RECOVER_BITS_WORDS], valid[RECOVER_BITS_WORDS];
static const struct recover_target target = {
    .space = REC_PCI_CFG, .index = RECOVER_NONE, .width = 4, .access = REC_RW, .read_safe = 1
};
static const struct recover_opcode_layout layout = {.dir_substep = RECOVER_NONE};
static const struct recover_plan_node node = {
    .phase = REC_PREPARE, .main_id = RECOVER_NONE, .pc = RECOVER_NONE,
    .rule_id = RECOVER_NONE, .expr_id = RECOVER_NONE, .next = {RECOVER_NONE, RECOVER_NONE}
};
static const struct recover_plan_entry entry = {REC_PREPARE, RECOVER_NONE, 0};
static struct recover_plan plan;

static int start_ready(void *opaque)
{
    u32 route;
    return saved_ops->start_ready(opaque) || saved_ops->route_read(opaque, &route) ||
           (route & RECOVER_ROUTE_MASK) ? -1 : 0;
}

static int identity(void *opaque, struct recover_identity *out)
{
    (void)opaque;
    *out = (struct recover_identity){.boot = VIRTUAL_ID, .image_version = VIRTUAL_ID,
                                     .kdebug_version = VIRTUAL_ID};
    return 0;
}

static int raw_config(void *opaque, u32 *command, u32 bars[3])
{
    (void)opaque;
    *command = RECOVER_COMMAND_MEM;
    for (u32 i = 0; i < 3; i++) bars[i] = 0;
    return 0;
}

static int deny_read(void *opaque, const struct recover_bus_access *a, u32 *value)
{
    (void)opaque; (void)a; (void)value;
    return -1;
}

static int deny_write(void *opaque, const struct recover_bus_access *a, u32 value)
{
    (void)opaque; (void)a; (void)value;
    return -1;
}

void recover_virtual_detach(void)
{
    struct recover *r = recover_owner();
    if (borrowed && (r->state == REC_IDLE || r->state == REC_VERIFIED || r->state == REC_FAILED)) {
        r->transport = saved_transport;
        recover_owner_configure(0, 0, 0, false);
        borrowed = false;
    }
}

static int virtual_operation(struct recover *r, void *arg)
{
    (void)arg;
    if (recover_plan_enter(r, 0)) return -1;
    if (active_case == RECOVER_VIRTUAL_FAULT) {
        if (recover_record(r, "inject", "BEGIN", active_case, 0, 0, 0)) return -1;
        return -1; /* Fault callback; trial_step owns failure and cleanup. */
    }
    return recover_plan_leave(r, 0);
}

static int virtual_begin(struct recover *r, u32 hz, u32 case_id)
{
    if (borrowed || (r->state != REC_IDLE && r->state != REC_VERIFIED) ||
        r->failed || r->cold_latched) return -1;
    profile = (struct recover_profile){.version = RECOVER_PROFILE_VERSION,
        .targets = &target, .target_count = 1, .layouts = &layout, .layout_count = 1};
    /* S0 restoration only; trial work is bounded by trial_limit. */
    const struct recover_static_work work = {VIRTUAL_LOG_BYTES, VIRTUAL_WAIT_MS, VIRTUAL_POINTS,
                                             VIRTUAL_LOG_BYTES, VIRTUAL_WAIT_MS, VIRTUAL_POINTS};
    if (!r->transport || !r->transport->frame_us ||
        recover_budget_apply(&profile, REC_BUDGET_S0, hz,
            VIRTUAL_FRAME_BIT_US / r->transport->frame_us, VIRTUAL_FRAME_BITS, &work)) return -1;
    saved_ops = r->ops;
    ops = *saved_ops;
    ops.start_ready = start_ready;
    ops.identity = identity;
    ops.raw_config = raw_config;
    ops.read = deny_read;
    ops.write = deny_write;
    ops.route_write = 0;
    saved_transport = r->transport;
    transport = *saved_transport;
    transport.max_polls = VIRTUAL_UART_POLLS;
    if (recover_owner_configure(&ops, r->opaque, r->s0_profile, false)) return -1;
    active_case = case_id;
    borrowed = true;
    r->transport = &transport;
    plan = (struct recover_plan){.version = 1, .profile_crc = profile.crc,
        .nodes = &node, .node_count = 1, .ctx_count = 1, .entries = &entry, .entry_count = 1};
    struct recover_plan_buffers buffers = {flow, valid};
    u32 hash;
    int rc = recover_bind_plan(r, &profile, &plan, &buffers, &hash);
    if (!rc) rc = trial_begin(r, &profile, hash, profile.trial_limit);
    token = r->token;
    return rc;
}

bool recover_virtual_allowed(const KdebugRecoverVirtual *q)
{
    struct recover *r = recover_owner();
    return borrowed && r->ops == &ops && q->session == token.session &&
        (q->action == RECOVER_VIRTUAL_STEP || q->action == RECOVER_VIRTUAL_END);
}

int recover_virtual_call(const KdebugRecoverVirtual *q, u32 hz)
{
    struct recover *r = recover_owner();
    int rc;
    if (q->action == RECOVER_VIRTUAL_BEGIN) {
        if (q->session) return -1;
        rc = virtual_begin(r, hz, q->case_id);
    } else {
        if (!recover_virtual_allowed(q)) return -1;
        rc = q->action == RECOVER_VIRTUAL_STEP ? trial_step(r, &token, true, virtual_operation, 0) :
                                                 trial_end(r, &token, REC_NORMAL);
    }
    recover_virtual_detach();
    return rc;
}
