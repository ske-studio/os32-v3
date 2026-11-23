#include "install_management.h"
#include "install_access.h"
#include "exec.h"
#include "fd_redirect.h"
#include "io.h"
#include "kmalloc.h"
#include "kstring.h"
#include "vfs.h"

enum install_state { AVAILABLE, PREPARING, ACTIVE, FORMATTING, TABLE_READY, VIEW, CHECKING, RECOVERING, RESTORING, RAW_ONLY };
struct original_sector {
    u32 lba;
    u8 bytes[512];
};
struct reservation {
    enum install_state state;
    int owner;
    u32 handle;
    InstallPlanV1 plan;
    struct install_identity identity;
    struct original_sector *original;
    struct original_sector *restored;
    u32 restored_count;
    u32 count, in_flight;
    int table_dirty;
    u32 published_generation;
    u32 epoch;
    unsigned dirty_count;
};
static struct reservation units[VFS_DEV_UNIT_MAX];
static u32 last_handle;

/* At most one outstanding metadata writeback per mounted ext2 context. */
struct pending_metadata {
    void *ctx;
    u32 view_generation;
    struct install_access access;
};
static struct pending_metadata metadata[VFS_MAX_FS];

static int plan_valid(const InstallPlanV1 *p)
{
    const InstallPlanPartitionV1 *part[] = { &p->map, &p->boot_hfs, &p->root_ext2 };
    unsigned i, j;

    if (p->version != 1 || p->size != sizeof(*p) || p->sector_bytes != 512 ||
        p->raw_unit >= VFS_DEV_UNIT_MAX || p->original_table_sectors < 2 ||
        p->original_table_sectors > p->capacity || p->lba0_start || p->lba0_sectors > 1)
        return 0;
    for (i = 0; i < 10; i++) if (p->reserved[i]) return 0;
    for (i = 0; i < 3; i++) {
        const InstallPlanPartitionV1 *a = part[i];
        if (a->type != i + 1 || !a->entry || a->entry > p->map.sectors ||
            !a->start_lba || !a->sectors || a->start_lba > p->capacity ||
            a->sectors > p->capacity - a->start_lba)
            return 0;
        for (j = 0; j < i; j++) {
            const InstallPlanPartitionV1 *b = part[j];
            if (a->entry == b->entry || (a->start_lba < b->start_lba + b->sectors &&
                                         b->start_lba < a->start_lba + a->sectors))
                return 0;
        }
    }
    return p->map.start_lba == 1;
}

static int save_originals(struct reservation *r, const struct install_medium *m, u32 count)
{
    u32 i;
    if (count > r->plan.capacity || count > (u32)-1 / sizeof(*r->original))
        return OS32_ERR_INVAL;
    if (!count) return 0;
    r->original = kmalloc(count * sizeof(*r->original));
    if (!r->original) return OS32_ERR_NOSPC;
    for (i = 0; i < count; i++) {
        struct original_sector *s = &r->original[i];
        int rc = m->changed_lba(&r->plan, i, &s->lba);
        if (rc < 0) return rc;
        if ((!s->lba && !r->plan.lba0_sectors) || s->lba > r->plan.map.sectors)
            return OS32_ERR_INVAL;
        rc = m->read(r->plan.raw_unit, s->lba, s->bytes);
        if (rc < 0) return rc;
    }
    r->count = count;
    return 0;
}

static void release_originals(struct reservation *r)
{
    if (r->original) kfree(r->original);
    r->original = 0;
    r->count = 0;
}

int install_begin(const InstallPlanV1 *plan, u32 *handle)
{
    const struct install_medium *m = platform_install_medium();
    struct reservation *r;
    InstallPlanV1 copy, previous;
    struct install_identity identity;
    enum install_state prior;
    unsigned flags;
    u32 count = 0;
    int rc, retry;

    if (!exec_disk_write_allowed() || !plan || !handle || (u32)handle % 4) return OS32_ERR_INVAL;
    kmemcpy(&copy, plan, sizeof(copy));
    if (!m) return OS32_ERR_NOSYS;
    if (!plan_valid(&copy)) return OS32_ERR_INVAL;
    r = &units[copy.raw_unit];
    flags = irq_save();
    prior = r->state;
    if (prior != AVAILABLE && prior != RAW_ONLY) {
        irq_restore(flags);
        return OS32_ERR_BUSY;
    }
    if (last_handle == (u32)-1) {
        irq_restore(flags);
        return OS32_ERR_FULL;
    }
    previous = r->plan;
    retry = prior == RAW_ONLY && !memcmp(&previous, &copy, sizeof(copy));
    r->plan = copy;
    r->state = PREPARING;
    u32 next_handle = ++last_handle;
    r->owner = res_owner_get();
    irq_restore(flags);
    r->table_dirty = 0;
    rc = m->identify(copy.raw_unit, &identity);
    if (!rc && prior == RAW_ONLY && memcmp(&r->identity, &identity, sizeof(identity))) {
        if (r->restored) kfree(r->restored);
        r->restored = 0;
        r->restored_count = 0;
        prior = AVAILABLE;
        retry = 0;
    }
    if (!rc && (copy.serial_crc != identity.serial_crc || copy.capacity != identity.capacity ||
                copy.sector_bytes != identity.sector_bytes)) rc = OS32_ERR_INVAL;
    if (!rc) rc = vfs_install_precheck(copy.raw_unit);
    if (!rc && retry) {
        u8 bytes[512];
        for (u32 i = 0; i < r->restored_count; i++) {
            struct original_sector *sector = &r->restored[i];
            rc = m->read(copy.raw_unit, sector->lba, bytes);
            if (rc) break;
            if (memcmp(bytes, sector->bytes, sizeof(bytes))) {
                retry = 0;
                break;
            }
        }
    }
    if (!rc) rc = m->validate(&copy, retry, &count);
    if (!rc) rc = m->drain(copy.raw_unit);
    if (!rc && r->in_flight > r->dirty_count) rc = OS32_ERR_BUSY;
    if (!rc) rc = vfs_install_detach(copy.raw_unit);
    if (!rc && r->in_flight) rc = OS32_ERR_BUSY;
    if (!rc) rc = m->sync(copy.raw_unit);
    if (!rc) rc = vfs_install_precheck(copy.raw_unit);
    if (!rc) rc = m->validate(&copy, retry, &count);
    if (!rc) rc = save_originals(r, m, count);
    if (!r->owner) rc = OS32_ERR_STALE;
    if (rc) {
        release_originals(r);
        r->plan = previous;
        r->handle = 0;
        r->owner = 0;
        r->state = prior;
        return rc;
    }
    m->invalidate(copy.raw_unit);
    flags = irq_save();
    if (!r->owner) {
        irq_restore(flags);
        release_originals(r);
        r->plan = previous;
        r->state = prior;
        return OS32_ERR_STALE;
    }
    struct original_sector *old = r->restored;
    r->restored = 0;
    r->restored_count = 0;
    r->identity = identity;
    r->handle = next_handle;
    r->epoch = next_handle;
    r->published_generation = 0;
    r->state = ACTIVE;
    *handle = next_handle;
    irq_restore(flags);
    if (old) kfree(old);
    return 0;
}

static struct reservation *lookup(u32 handle)
{
    unsigned i;
    for (i = 0; i < VFS_DEV_UNIT_MAX; i++) {
        struct reservation *r = &units[i];
        if ((r->state == ACTIVE || r->state == FORMATTING || r->state == TABLE_READY || r->state == VIEW) &&
            r->handle == handle && r->owner == res_owner_get()) return r;
    }
    return 0;
}

static int restore(struct reservation *r)
{
    const struct install_medium *m = platform_install_medium();
    u32 unit = r->plan.raw_unit, i;
    u8 check[512];
    int rc;
    unsigned flags = irq_save();
    if (r->state == RESTORING) {
        irq_restore(flags);
        return OS32_ERR_BUSY;
    }
    r->state = RECOVERING;
    r->owner = 0;
    r->handle = 0;
    /* A sync worker can be between two transfers using this metadata record. */
    if (r->in_flight > r->dirty_count) {
        irq_restore(flags);
        return OS32_ERR_BUSY;
    }
    for (unsigned i = 0; i < VFS_MAX_FS; i++) {
        if (metadata[i].ctx && metadata[i].access.unit == unit)
            install_dirty_finish(metadata[i].ctx);
    }
    r->state = RESTORING;
    irq_restore(flags);
    vfs_install_discard(unit);
    m->invalidate(unit);
    struct install_identity current;
    rc = m->identify(unit, &current);
    if (rc) goto quarantine;
    if (memcmp(&current, &r->identity, sizeof(current))) {
        rc = OS32_ERR_STALE;
        goto quarantine;
    }
    for (i = 0; r->table_dirty && i < r->count; i++) {
        struct original_sector *s = &r->original[i];
        rc = m->write(unit, s->lba, 1, s->bytes);
        if (rc) goto quarantine;
    }
    rc = m->sync(unit);
    if (rc) goto quarantine;
    for (i = 0; i < r->count; i++) {
        struct original_sector *s = &r->original[i];
        rc = m->read(unit, s->lba, check);
        if (rc || memcmp(check, s->bytes, sizeof(check))) {
            rc = OS32_ERR_IO;
            goto quarantine;
        }
    }
    r->restored = r->original;
    r->restored_count = r->count;
    r->original = 0;
    r->count = 0;
    r->state = RAW_ONLY;
    return 0;
quarantine:
    r->state = RECOVERING;
    return rc;
}

void install_retry_recovery(void)
{
    for (unsigned i = 0; i < VFS_DEV_UNIT_MAX; i++)
        if (units[i].state == RECOVERING) (void)restore(&units[i]);
}

void install_reclaim_owner(int owner)
{
    unsigned i;
    for (i = 0; i < VFS_DEV_UNIT_MAX; i++) {
        struct reservation *r = &units[i];
        if (!owner || r->owner != owner) continue;
        if (r->state == PREPARING || r->state == CHECKING) {
            r->owner = 0; /* The management call owns the cleanup until its callback returns. */
        } else if (r->handle) {
            (void)restore(r);
        }
    }
}

static int inspect(u32 handle, int commit)
{
    const struct install_medium *m = platform_install_medium();
    unsigned flags = irq_save();
    struct reservation *r = lookup(handle);
    int rc;
    if (!r) rc = OS32_ERR_STALE;
    else if (r->state != VIEW && (commit || r->state != TABLE_READY)) rc = OS32_ERR_INVAL;
    else if (r->in_flight > r->dirty_count) rc = OS32_ERR_BUSY;
    else {
        r->state = CHECKING;
        rc = 0;
    }
    irq_restore(flags);
    if (rc) return rc;

    rc = m->sync(r->plan.raw_unit);
    if (!rc && commit) rc = vfs_install_detach(r->plan.raw_unit);
    if (!rc && commit) rc = m->sync(r->plan.raw_unit);
    if (!rc && commit && r->in_flight) rc = OS32_ERR_BUSY;
    if (!rc && r->owner)
        rc = commit ? m->verify(&r->plan) : m->rescan(&r->plan, handle);

    flags = irq_save();
    if (!r->owner) rc = OS32_ERR_STALE;
    if (rc) {
        irq_restore(flags);
        int recovery = restore(r);
        return recovery ? recovery : rc;
    }
    if (!commit) {
        r->state = VIEW;
        irq_restore(flags);
        return 0;
    }
    struct original_sector *old = r->original;
    r->original = 0;
    r->count = 0;
    r->published_generation = handle;
    m->publish(r->plan.raw_unit);
    r->owner = 0;
    r->handle = 0;
    r->state = AVAILABLE;
    irq_restore(flags);
    if (old) kfree(old);
    return 0;
}

int install_rescan(u32 handle)
{
    if (!exec_disk_write_allowed()) return OS32_ERR_INVAL;
    return inspect(handle, 0);
}

int install_end(u32 handle, u32 outcome)
{
    if (!exec_disk_write_allowed() ||
        (outcome != OS32_INSTALL_END_COMMIT && outcome != OS32_INSTALL_END_ABORT))
        return OS32_ERR_INVAL;
    if (outcome == OS32_INSTALL_END_COMMIT) return inspect(handle, 1);
    struct reservation *r = lookup(handle);
    return r ? restore(r) : OS32_ERR_STALE;
}

static int within(u32 start, u32 length, u32 lba, u32 count)
{
    return lba >= start && lba - start <= length && count <= length - (lba - start);
}

int install_admit(struct install_access *a, u32 unit, enum install_op op,
                  u32 lba, u32 count, const void *bytes)
{
    if (unit >= VFS_DEV_UNIT_MAX) return OS32_ERR_INVAL;
    struct reservation *r = &units[unit];
    unsigned flags = irq_save();
    int rc = OS32_ERR_BUSY;
    const struct install_medium *m = platform_install_medium();
    int raw_write = op == INSTALL_WRITE || op == INSTALL_CHS_WRITE;
    if ((r->state == AVAILABLE && (!raw_write || !m || !m->requires_raw_reservation)) ||
        (r->state == RAW_ONLY && op == INSTALL_READ))
        rc = 0;
    else if ((r->state == ACTIVE || r->state == FORMATTING || r->state == TABLE_READY || r->state == VIEW) && r->owner == res_owner_get()) {
        const InstallPlanV1 *p = &r->plan;
        if (op == INSTALL_MOUNT)
            rc = r->state == VIEW ? 0 : OS32_ERR_BUSY;
        else if ((op == INSTALL_VIEW_READ || op == INSTALL_VIEW_WRITE) &&
                 r->state == VIEW &&
                 within(p->root_ext2.start_lba, p->root_ext2.sectors, lba, count))
            rc = 0;
        else if (within(0, p->capacity, lba, count)) {
            if (op == INSTALL_READ) rc = 0;
            else if (op == INSTALL_WRITE && exec_disk_write_allowed()) {
                if (r->state == VIEW && within(p->boot_hfs.start_lba, p->boot_hfs.sectors, lba, count))
                    rc = 0;
                else if (r->state == TABLE_READY && bytes &&
                         (within(p->map.start_lba, p->map.sectors, lba, count) ||
                          within(0, p->lba0_sectors, lba, count)))
                {
                    rc = platform_install_medium()->check_table(p, lba, count, bytes);
                    for (u32 offset = 0; !rc && offset < count; offset++) {
                        u32 i = 0;
                        while (i < r->count && r->original[i].lba != lba + offset) i++;
                        if (i == r->count) rc = OS32_ERR_INVAL;
                    }
                    if (!rc) r->table_dirty = 1;
                }
            }
        }
    }
    if (!rc) {
        *a = (struct install_access){ .unit = unit, .generation = r->handle, .lba = lba,
                                     .count = count, .epoch = r->epoch, .phase = r->state, .op = op };
        r->in_flight++;
    }
    irq_restore(flags);
    return rc;
}

int install_transfer_allowed(const struct install_access *a, u32 lba, u32 count)
{
    if (!a || a->unit >= VFS_DEV_UNIT_MAX) return 0;
    struct reservation *r = &units[a->unit];
    int same_phase = r->state == a->phase ||
        (r->state == PREPARING && !a->generation) ||
        (r->state == CHECKING && a->phase == VIEW && a->op == INSTALL_VIEW_WRITE);
    return same_phase && r->in_flight &&
           r->epoch == a->epoch && r->handle == a->generation &&
           within(a->lba, a->count, lba, count);
}

void install_complete(struct install_access *a)
{
    unsigned flags = irq_save();
    units[a->unit].in_flight--;
    a->unit = VFS_DEV_UNIT_MAX;
    irq_restore(flags);
}

int install_mount_visible(u32 unit)
{
    if (unit >= VFS_DEV_UNIT_MAX) return 0;
    const struct reservation *r = &units[unit];
    return r->state == AVAILABLE || (r->state == VIEW && r->owner == res_owner_get());
}

int install_mount_lock(const struct install_access *a, unsigned *flags)
{
    *flags = irq_save();
    if (install_transfer_allowed(a, 0, 0)) return 0;
    irq_restore(*flags);
    return OS32_ERR_STALE;
}

void install_mount_unlock(unsigned flags)
{
    irq_restore(flags);
}

int install_sync_pin(struct _Device *dev, u32 start, struct install_access *pin)
{
    u32 unit, physical, generation;
    int rc = platform_install_medium()->resolve(dev, start, 1, &unit, &physical, &generation);
    if (rc != 1) return rc;
    if (unit >= VFS_DEV_UNIT_MAX) return OS32_ERR_INVAL;
    struct reservation *r = &units[unit];
    if (r->state == AVAILABLE) return 0;
    unsigned flags = irq_save();
    rc = 0;
    if (r->state == RECOVERING || r->state == RESTORING || r->state == RAW_ONLY)
        rc = OS32_ERR_STALE;
    else if (r->handle) {
        if (r->handle != generation) rc = OS32_ERR_STALE;
        else {
            pin->unit = unit;
            r->in_flight++;
            rc = 1;
        }
    }
    irq_restore(flags);
    return rc;
}

int install_format_begin(struct install_access *a, u32 unit, u32 start, u32 length)
{
    if (unit >= VFS_DEV_UNIT_MAX) return OS32_ERR_INVAL;
    struct reservation *r = &units[unit];
    unsigned flags = irq_save();
    if (platform_install_medium() || r->state != AVAILABLE) {
        if (r->state != ACTIVE || !exec_disk_write_allowed() ||
            r->owner != res_owner_get() || start != r->plan.root_ext2.start_lba ||
            length != r->plan.root_ext2.sectors || r->in_flight) {
            irq_restore(flags);
            return OS32_ERR_BUSY;
        }
        r->state = FORMATTING;
    }
    *a = (struct install_access){ .unit = unit, .generation = r->handle,
        .lba = start, .count = length, .epoch = r->epoch, .phase = r->state, .op = INSTALL_FORMAT };
    r->in_flight++;
    irq_restore(flags);
    return 0;
}

int install_format_finish(struct install_access *a, int rc)
{
    u32 unit = a->unit;
    struct reservation *r = &units[unit];
    install_complete(a);
    if (!a->generation) return rc;
    if (r->state != FORMATTING) return OS32_ERR_STALE;
    r->state = CHECKING;
    if (!rc) rc = platform_install_medium()->sync(unit);
    if (!r->owner) rc = OS32_ERR_STALE;
    if (rc) {
        int recovery = restore(r);
        return recovery ? recovery : rc;
    }
    r->state = TABLE_READY;
    return 0;
}

/* Worker and owner both consume kernel-held admission records. Backend sector
 * callbacks address the raw medium without entering public IDE dispatch again. */
static int transfer(const struct install_access *a, u32 physical, u32 count,
                    void *dst, const void *src)
{
    const struct install_medium *m = platform_install_medium();
    u8 *out = dst;
    const u8 *in = src;
    struct install_access issued = *a;
    if (in && issued.op != INSTALL_VIEW_WRITE && issued.op != INSTALL_FORMAT &&
        issued.op != INSTALL_WRITE) return OS32_ERR_INVAL;
    unsigned flags = irq_save();
    if (!install_transfer_allowed(&issued, physical, count)) {
        irq_restore(flags);
        return OS32_ERR_STALE;
    }
    units[issued.unit].in_flight++;
    irq_restore(flags);
    int rc = 0;
    if (in && count) {
        rc = m->write(issued.unit, physical, count, in);
    } else {
        for (u32 i = 0; i < count; i++) {
            if (!install_transfer_allowed(&issued, physical + i, 1)) {
                install_complete(&issued);
                return OS32_ERR_STALE;
            }
            rc = m->read(issued.unit, physical + i, out);
            if (rc) break;
            out += 512;
        }
    }
    if (rc && issued.phase == VIEW &&
        (issued.op == INSTALL_VIEW_READ || issued.op == INSTALL_VIEW_WRITE)) {
        struct reservation *r = &units[issued.unit];
        flags = irq_save();
        if (r->state == VIEW && r->handle == issued.generation) {
            /* Idle completes recovery after all callers drop their pins. */
            r->state = RECOVERING;
            r->owner = 0;
            r->handle = 0;
        }
        irq_restore(flags);
    }
    install_complete(&issued);
    return rc;
}

/* Admission pins the unit even when an ordinary unreserved request falls
 * through to its original driver. Freeze proposed table bytes before checking
 * them so the validated payload is also the payload submitted to the medium. */
int install_raw_io(struct install_access *a, u32 unit, u32 lba, u32 count,
                   void *dst, const void *src, int writing, int *handled)
{
    void *copy = 0;
    int rc;
    *handled = 1;
    if (unit >= VFS_DEV_UNIT_MAX) return OS32_ERR_INVAL;
    struct reservation *r = &units[unit];
    if (writing && r->state == TABLE_READY) {
        if (!exec_disk_write_allowed() || r->owner != res_owner_get())
            return OS32_ERR_BUSY;
        if (!src || count > r->count || count > (u32)-1 / 512)
            return OS32_ERR_INVAL;
        if (count) {
            copy = kmalloc(count * 512);
            if (!copy) return OS32_ERR_NOSPC;
            kmemcpy(copy, src, count * 512);
            src = copy;
        }
    }
    rc = install_admit(a, unit, writing ? INSTALL_WRITE : INSTALL_READ,
                       lba, count, copy);
    if (!rc) {
        if (a->generation) {
            if (writing && !src && count) rc = OS32_ERR_INVAL;
            else rc = transfer(a, lba, count, dst, src);
            install_complete(a);
            if (rc && r->state != RECOVERING && r->state != RESTORING) {
                int recovery = restore(r);
                if (recovery) rc = recovery;
            }
        } else *handled = 0;
    }
    if (copy) kfree(copy);
    return rc;
}

int install_format_transfer(const struct install_access *a, u32 physical, u32 count,
                            void *dst, const void *src)
{
    if (a->op != INSTALL_FORMAT) return OS32_ERR_INVAL;
    return transfer(a, physical, count, dst, src);
}

int install_view_io(struct install_access *a, u32 unit, u32 generation,
                    u32 physical, u32 count, void *dst, const void *src, int *handled)
{
    *handled = 1;
    if (unit >= VFS_DEV_UNIT_MAX) return OS32_ERR_INVAL;
    struct reservation *r = &units[unit];
    int rc = install_admit(a, unit, src ? INSTALL_VIEW_WRITE : INSTALL_VIEW_READ,
                           physical, count, src);
    if (rc) return rc;
    if (!a->generation && (!r->epoch || generation == r->published_generation)) {
        *handled = 0;
        return 0;
    }
    if (generation != a->generation) rc = OS32_ERR_STALE;
    else rc = transfer(a, physical, count, dst, src);
    install_complete(a);
    return rc;
}

void install_dirty_begin(void *ctx, struct _Device *dev, u32 start, u32 sectors)
{
    u32 unit, physical, generation;
    const struct install_medium *m = platform_install_medium();
    if (m->resolve(dev, start, sectors, &unit, &physical, &generation) != 1 ||
        unit >= VFS_DEV_UNIT_MAX) return;
    struct reservation *r = &units[unit];
    unsigned flags = irq_save();
    struct pending_metadata *slot = 0;
    for (unsigned i = 0; i < VFS_MAX_FS; i++) {
        if (metadata[i].ctx == ctx) {
            irq_restore(flags);
            return;
        }
        if (!metadata[i].ctx) slot = &metadata[i];
    }
    int current = r->state == VIEW ? r->handle == generation :
        r->state == AVAILABLE && (!r->epoch || generation == r->published_generation);
    if (slot && current &&
        !install_admit(&slot->access, unit, INSTALL_VIEW_WRITE, physical, sectors, 0)) {
        slot->ctx = ctx;
        slot->view_generation = generation;
        r->dirty_count++;
    }
    irq_restore(flags);
}

void install_dirty_finish(void *ctx)
{
    unsigned flags = irq_save();
    for (unsigned i = 0; i < VFS_MAX_FS; i++) {
        struct pending_metadata *p = &metadata[i];
        if (p->ctx != ctx) continue;
        p->ctx = 0;
        units[p->access.unit].dirty_count--;
        install_complete(&p->access);
        break;
    }
    irq_restore(flags);
}

int install_dirty_transfer(void *ctx, struct _Device *dev, u32 lba, u32 count,
                           void *dst, const void *src, int *handled)
{
    const struct install_medium *m = platform_install_medium();
    u32 unit, physical, generation;
    int rc = m->resolve(dev, lba, count, &unit, &physical, &generation);
    *handled = rc < 0;
    if (rc != 1 || unit >= VFS_DEV_UNIT_MAX || units[unit].state == AVAILABLE) return rc;
    for (unsigned i = 0; i < VFS_MAX_FS; i++) {
        struct pending_metadata *p = &metadata[i];
        if (p->ctx != ctx) continue;
        *handled = 1;
        if (p->access.unit != unit || p->view_generation != generation) return OS32_ERR_STALE;
        return transfer(&p->access, physical, count, dst, src);
    }
    return 0;
}
