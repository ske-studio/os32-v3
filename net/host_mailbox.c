/* Host Services notification storage: NET_HOSTSVC_DESIGN §5, #4 and #5. */
#include "host_mailbox.h"
#include "io.h"
#include "kmalloc.h"
#include "kstring.h"
#include "os32_kapi_shared.h"

#define MAILBOX_SLOTS 64
#define MAILBOX_ALL 65535u

static HostNotice *slots;
/* Arrival order contains slot indices, so filtered removal moves only indices. */
static u8 order[MAILBOX_SLOTS], used[MAILBOX_SLOTS];
static unsigned int count;
static u32 generation_now, serial_now, dropped;
static int initialized, generation_valid, latest_valid;
static HostNotice latest;
static struct HostAlive alive_state;

int host_mailbox_init(void)
{
    if (!initialized) {
        initialized = 1;
        slots = kmalloc(MAILBOX_SLOTS * sizeof(*slots));
    }
    return slots ? 0 : OS32_ERR_NOSYS;
}

static void remove_at(unsigned int pos)
{
    used[order[pos]] = 0;
    for (unsigned int i = pos + 1; i < count; i++)
        order[i - 1] = order[i];
    count--;
}

int host_mailbox_put(u32 generation, u16 service, u16 kind,
                     const void *data, u16 len, int alive)
{
    if (!service || service == MAILBOX_ALL || len > sizeof(latest.data) ||
        (len && !data))
        return OS32_ERR_INVAL;
    unsigned int flags = irq_save();
    int same_alive = alive_state.valid &&
                     alive_state.host_generation == generation;
    if (alive) {
        alive_state.host_generation = generation;
        alive_state.observation++;
        alive_state.valid = 1;
    }
    if (!slots) {
        irq_restore(flags);
        return OS32_ERR_NOSYS;
    }
    if (!generation_valid || generation_now != generation) {
        dropped += count;
        /* The latest ALIVE may outlive its ring entry. Count it only once. */
        int in_ring = 0;
        for (unsigned int i = 0; i < count; i++) {
            if (latest_valid && slots[order[i]].serial == latest.serial)
                in_ring = 1;
            used[order[i]] = 0;
        }
        if (latest_valid && !in_ring) dropped++;
        count = 0;
        latest_valid = 0;
        generation_now = generation;
        generation_valid = 1;
    }
    HostNotice notice;
    kmemset(&notice, 0, sizeof(notice));
    notice.serial = ++serial_now;
    notice.host_generation = generation;
    notice.service = service;
    notice.kind = kind;
    notice.length = len;
    if (len) kmemcpy(notice.data, data, len);
    if (alive && same_alive) {
        /* Refresh an unconsumed first ALIVE without occupying another slot. */
        for (unsigned int i = 0; i < count; i++)
            if (latest_valid && slots[order[i]].serial == latest.serial)
                slots[order[i]] = notice;
    } else {
        if (count == MAILBOX_SLOTS) {
            remove_at(0);
            dropped++;
        }
        unsigned int slot = 0;
        while (used[slot]) slot++;
        slots[slot] = notice;
        used[slot] = 1;
        order[count++] = slot;
    }
    if (alive) {
        latest = notice;
        latest_valid = 1;
    }
    irq_restore(flags);
    return 0;
}

int host_mailbox_stage(u32 generation, u32 service, HostNotice *kernel_out)
{
    if (!kernel_out || !service || service > MAILBOX_ALL ||
        (!generation && service != MAILBOX_ALL))
        return OS32_ERR_INVAL;
    unsigned int flags = irq_save();
    int rc = OS32_ERR_AGAIN;
    if (!slots) rc = OS32_ERR_NOSYS;
    else if (generation && generation_valid && generation != generation_now)
        rc = OS32_ERR_STALE;
    else {
        const HostNotice *found = 0;
        for (unsigned int i = 0; i < count; i++) {
            const HostNotice *n = &slots[order[i]];
            if (service == MAILBOX_ALL || service == n->service) {
                found = n;
                break;
            }
        }
        if (!found && latest_valid &&
            (service == MAILBOX_ALL || service == latest.service))
            found = &latest;
        if (found) {
            *kernel_out = *found;
            kernel_out->dropped = dropped;
            rc = 0;
        }
    }
    irq_restore(flags);
    return rc;
}

void host_mailbox_commit(u32 generation, u32 serial)
{
    unsigned int flags = irq_save();
    if (generation_valid && generation == generation_now) {
        for (unsigned int i = 0; i < count; i++)
            if (slots[order[i]].serial == serial) {
                remove_at(i);
                break;
            }
        if (latest_valid && latest.serial == serial)
            latest_valid = 0;
    }
    irq_restore(flags);
}

u32 host_mailbox_latest_serial(void)
{
    unsigned int flags = irq_save();
    u32 serial = serial_now;
    irq_restore(flags);
    return serial;
}

void host_alive_snapshot(struct HostAlive *out)
{
    unsigned int flags = irq_save();
    *out = alive_state;
    irq_restore(flags);
}
