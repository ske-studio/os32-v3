/* IO_TIER_DESIGN §4/§6, AC-IO-2: shared LE policy and owner restoration. */
#include "io_policy.h"
#include "io.h"
#include "os32_kapi_shared.h"
#include "appslot.h"

#define POLICY_SERVICES 4
#define POLICY_OWNER_COUNT (APP_ID_MAX + 1)
#define POLICY_HEADER_SIZE 16u
#define POLICY_ENTRY_SIZE 16u
#define POLICY_WIRE_MAX 16u
#define POLICY_WIRE_SIZE (POLICY_HEADER_SIZE + POLICY_SERVICES * POLICY_ENTRY_SIZE)

struct policy_value { u32 budget; u8 tier; };
struct policy_table { struct policy_value row[POLICY_SERVICES]; };
struct policy_rule { u32 id, budget, min, max; u8 tier; };
static const struct policy_rule rules[POLICY_SERVICES] = {
    /* [推] Existing NIC demux 120us (IO_TIER §3); range within §5's 500us. */
    { IO_SVC_NE2K_BH, 120, 1, 500, 2 },
    /* [推] RX shares that 120us ceiling (§3); min/max within the FG1 round. */
    { IO_SVC_NET_RX, 120, 1, 500, 2 },
    /* [推] Finite calibration (§3): 120us start, 1..500us within §5's round. */
    { IO_SVC_TSC_CALIBRATE, 120, 1, 500, 3 },
    /* LAN_BULK_DESIGN §4: 440us / 4 units (units owned by LAN's slice).
     * [推] 1..500us range bounded by IO_TIER §5; not a unit-time guarantee. */
    { IO_SVC_LAN_BULK, 440, 1, 500, 2 }
};
static struct policy_table tables[2];
/* NULL denotes the OS standard; no lazy initialization or extra generation. */
static const struct policy_table *active;
static u32 generation;
static struct {
    struct policy_table table;
    u32 gen;
    int valid;
} saved[POLICY_OWNER_COUNT];

static u32 read_le(const u8 *p, unsigned int n)
{
    u32 value = 0;
    for (unsigned int i = 0; i < n; ++i)
        value |= (u32)p[i] << (8 * i); /* POLICY_LE_READ */
    return value;
}

static void write_le(u8 *p, u32 value, unsigned int n)
{
    for (unsigned int i = 0; i < n; ++i)
        p[i] = (u8)(value >> (8 * i));
}

static int index_of(u32 id)
{
    for (int i = 0; i < POLICY_SERVICES; ++i)
        if (rules[i].id == id) return i;
    return -1;
}

static void defaults(struct policy_table *table)
{
    for (int i = 0; i < POLICY_SERVICES; ++i) {
        table->row[i].budget = rules[i].budget;
        table->row[i].tier = rules[i].tier;
    }
}

static void snapshot(struct policy_table *table)
{
    if (active) *table = *active;
    else defaults(table);
}

int io_policy_lookup(u32 id, u8 *tier, u32 *budget_us)
{
    int i = index_of(id);
    if (i < 0) return OS32_ERR_NOTFOUND;
    if (tier) *tier = active ? active->row[i].tier : rules[i].tier;
    if (budget_us) *budget_us = active ? active->row[i].budget : rules[i].budget;
    return 0;
}

int io_policy_limits(u32 id, u8 *default_tier, u32 *default_budget_us,
                     u32 *budget_min_us, u32 *budget_max_us)
{
    int i = index_of(id);
    if (i < 0) return OS32_ERR_NOTFOUND;
    if (default_tier) *default_tier = rules[i].tier;
    if (default_budget_us) *default_budget_us = rules[i].budget;
    if (budget_min_us) *budget_min_us = rules[i].min;
    if (budget_max_us) *budget_max_us = rules[i].max;
    return 0;
}

u32 io_policy_generation(void) { return generation; }

/* Single-task FG1 caller: validation and local construction never yield.
 * Only the bounded save/copy/pointer swap is inside the IRQ exclusion. */
static int commit(const struct policy_table *table, u32 owner, u32 gen)
{
    if (owner >= POLICY_OWNER_COUNT) return OS32_ERR_INVAL;
    unsigned int flags = irq_save();
    if (io_dispatch_running()) { /* POLICY_BUSY */
        irq_restore(flags);
        return OS32_ERR_BUSY;
    }
    if (owner && (!saved[owner].valid || saved[owner].gen != gen)) { /* POLICY_SAVE_ONCE */
        snapshot(&saved[owner].table);
        saved[owner].gen = gen;
        saved[owner].valid = 1;
    }
    if (table) {
        struct policy_table *next = active == &tables[0] ? &tables[1] : &tables[0];
        *next = *table;
        active = next;
    } else {
        active = NULL;
    }
    ++generation;
    irq_restore(flags);
    return 0;
}

int io_policy_set(const void *bytes, unsigned int len, u32 owner, u32 gen)
{
    const u8 *p = bytes;
    struct policy_table next;
    u32 seen = 0, count;
    if (!p || len < POLICY_HEADER_SIZE) return OS32_ERR_INVAL;
    if (p[0] != 'I' || p[1] != 'O' || p[2] != 'T' || p[3] != '1' ||
        read_le(p + 4, 2) != 1 || read_le(p + 6, 2) != POLICY_ENTRY_SIZE ||
        read_le(p + 12, 4) != 0) return OS32_ERR_INVAL;
    count = read_le(p + 8, 4);
    if (count > POLICY_WIRE_MAX || len != POLICY_HEADER_SIZE + count * POLICY_ENTRY_SIZE)
        return OS32_ERR_INVAL;
    defaults(&next); /* POLICY_BEFORE_VALIDATION */
    for (u32 n = 0; n < count; ++n) {
        const u8 *row = p + POLICY_HEADER_SIZE + n * POLICY_ENTRY_SIZE;
        int i = index_of(read_le(row, 4));
        u32 budget = read_le(row + 8, 4);
        if (i < 0 || (seen & (1u << i)) || row[4] < 1 || row[4] > 3 ||
            row[5] || row[6] || row[7] || read_le(row + 12, 4) ||
            budget < rules[i].min || budget > rules[i].max)
            return OS32_ERR_INVAL;
        seen |= 1u << i;
        next.row[i].tier = row[4];
        next.row[i].budget = budget; /* POLICY_ROW_VALIDATED */
    }
    return commit(&next, owner, gen);
}

int io_policy_get(void *bytes, unsigned int cap, unsigned int *needed)
{
    u8 *p = bytes;
    struct policy_table table;
    if (!needed) return OS32_ERR_INVAL;
    *needed = POLICY_WIRE_SIZE;
    if (cap < POLICY_WIRE_SIZE) return OS32_ERR_FULL;
    if (!p) return OS32_ERR_INVAL;
    snapshot(&table);
    p[0] = 'I'; p[1] = 'O'; p[2] = 'T'; p[3] = '1';
    write_le(p + 4, 1, 2);
    write_le(p + 6, POLICY_ENTRY_SIZE, 2);
    write_le(p + 8, POLICY_SERVICES, 4);
    write_le(p + 12, 0, 4);
    for (unsigned int i = 0; i < POLICY_SERVICES; ++i) {
        u8 *row = p + POLICY_HEADER_SIZE + i * POLICY_ENTRY_SIZE;
        write_le(row, rules[i].id, 4);
        row[4] = table.row[i].tier;
        row[5] = row[6] = row[7] = 0;
        write_le(row + 8, table.row[i].budget, 4);
        write_le(row + 12, 0, 4);
    }
    return 0;
}

int io_policy_reset(u32 owner, u32 gen) { return commit(NULL, owner, gen); }

void io_policy_owner_exit(u32 owner, u32 gen)
{
    if (!owner || owner >= POLICY_OWNER_COUNT) return;
    unsigned int flags = irq_save();
    if (saved[owner].valid && saved[owner].gen == gen) {
        struct policy_table *next = active == &tables[0] ? &tables[1] : &tables[0];
        *next = saved[owner].table;
        active = next; /* POLICY_RESTORE */
        saved[owner].valid = 0;
        ++generation;
    }
    /* P3: restore even during dispatch; its snapshot lasts until round end. */
    irq_restore(flags);
}
