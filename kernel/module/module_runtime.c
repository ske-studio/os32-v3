#include "le_internal.h"
#include "module_runtime.h"
#include "../../mm/pgalloc.h"
#include "memmap.h"
#include "vfs.h"
#include "kstring.h"

STATIC_ASSERT(MODULE_PAGE_SIZE == PAGE_SIZE, module_runtime_page_size);
struct module_record module_records[MODULE_RECORD_CAP];


struct module_record *module_record_get(struct module_handle h)
{
    if (h.slot >= MODULE_RECORD_CAP || !h.generation ||
        module_records[h.slot].generation != h.generation) return 0;
    return &module_records[h.slot];
}

const u32 *module_exports(struct module_handle h)
{
    struct module_record *r = module_record_get(h);
    return r && r->state == MODULE_RUNNING ? r->exports : 0;
}

static int dependencies_ready(const struct module_handle *deps, u32 n)
{
    if (n > MODULE_DEPENDENCY_CAP || (n && !deps)) return 0;
    for (u32 i = 0; i < n; i++)
        if (!module_exports(deps[i])) return 0;
    return 1;
}

int module_load(const struct module_request *q, struct module_handle *out)
{
    u8 header[MODULE_HEADER_SIZE];
    struct module_image_plan plan;
    OS32_Stat st;
    struct module_record *r = 0;
    struct module_handle handle = {0, 0};
    if (!dependencies_ready(q->dependencies, q->dependency_count))
        return MODULE_BAD_DEPENDENCY;
    if (!q->lifecycle.start || (q->policy.import_count && !q->imports))
        return MODULE_BAD_HEADER;
    for (u32 i = 0; i < MODULE_RECORD_CAP; i++) {
        if (module_records[i].state != MODULE_ABSENT) continue;
        r = &module_records[i];
        handle.slot = i;
        handle.generation = r->generation + 1;
        break;
    }
    if (!r) return MODULE_RECORD_FULL;
    kmemset(r, 0, sizeof(*r));
    r->generation = handle.generation;
    r->module_id = q->policy.module_id;
    r->lifecycle = q->lifecycle;
    r->dependency_count = q->dependency_count;
    for (u32 i = 0; i < r->dependency_count; i++)
        r->dependencies[i] = q->dependencies[i];
    *out = handle;
    int fd = vfs_open(q->path, O_RDONLY);
    int rc = MODULE_NOT_FOUND;
    if (fd < 0) goto rejected;
    rc = MODULE_SHORT_READ;
    if (vfs_fstat(fd, &st) < 0 ||
        vfs_read_fd(fd, header, sizeof(header)) != (int)sizeof(header)) goto rejected;
    r->pair_crc = le_read32(header + MI_PAIR_CRC * MODULE_WORD_BYTES);
    rc = module_image_header(header, sizeof(header), st.st_size, &q->policy, &plan);
    if (rc) goto rejected;
    r->state = MODULE_HEADER_CHECKED;
    const u32 *h = plan.word;
    rc = MODULE_NO_PAGES;
    if (!ledger_owner_new(LEDGER_KIND_MODULE, r->module_id, q->name, &r->owner))
        goto rejected;
    r->backing = pgalloc_alloc_phys(r->owner, plan.load_span / PAGE_SIZE);
    if (!r->backing) goto rejected;
    r->pages = plan.load_span / PAGE_SIZE;
    r->state = MODULE_OWNED_IMAGE;
    u8 *image = P2V(r->backing);
    kmemcpy(image, header, sizeof(header));
    rc = MODULE_SHORT_READ;
    if (vfs_read_fd(fd, image + sizeof(header), st.st_size - sizeof(header)) !=
        (int)(st.st_size - sizeof(header))) goto rejected;
    vfs_close(fd);
    fd = -1;
    u8 *body = image + h[MI_PAYLOAD_OFF];
    r->base = (u32)(uptr)body; /* kernel VA from P2V */
    rc = module_image_relocate(image, st.st_size, &q->policy, r->base, &plan);
    if (rc) goto rejected;
    r->state = MODULE_VALIDATED;
    /* relocate validates export count/unique slots against generated policy. */
    for (u32 i = 0; i < h[MI_IMPORT_COUNT]; i++) {
        u32 off = le_read32(image + h[MI_IMPORT_OFF] + i * MODULE_RECORD_SIZE + MODULE_IMPORT_SLOT_OFF);
        kmemcpy(body + off, &q->imports[i], sizeof(u32));
    }
    r->exports[0] = h[MI_OPS_VERSION];
    for (u32 i = 0; i < h[MI_EXPORT_COUNT]; i++) {
        const u8 *rec = image + h[MI_EXPORT_OFF] + i * MODULE_RECORD_SIZE;
        u32 slot = (u32)rec[MODULE_EXPORT_SLOT_OFF] | ((u32)rec[MODULE_EXPORT_SLOT_OFF + 1] << 8);
        r->exports[slot] = r->base + le_read32(rec + MODULE_EXPORT_ADDR_OFF);
    }
    r->prefix_pages = h[MI_PAYLOAD_OFF] / PAGE_SIZE;
    r->text = r->base + h[MI_TEXT_OFF];
    r->text_size = h[MI_TEXT_SIZE];
    r->ro = r->base + h[MI_RO_OFF];
    r->ro_size = h[MI_RO_SIZE];
    r->image_crc = h[MI_IMAGE_CRC];
    r->init = r->base + h[MI_INIT];
    r->state = MODULE_RELOCATED;
    return 0;

rejected:
    if (fd >= 0) vfs_close(fd);
    module_abort(handle);
    return rc;
}

int module_start(struct module_handle h)
{
    struct module_record *r = module_record_get(h);
    u32 candidate[MODULE_RUNTIME_SLOTS + 1];
    if (!r || r->state != MODULE_RELOCATED) return MODULE_BUSY;
    if (!dependencies_ready(r->dependencies, r->dependency_count)) {
        module_abort(h);
        return MODULE_BAD_DEPENDENCY;
    }
    kmemcpy(candidate, r->exports, sizeof(candidate));
    r->state = MODULE_STARTING;
    if (r->lifecycle.start(r, r->init, candidate) ||
        memcmp(candidate, r->exports, sizeof(candidate))) {
        module_abort(h);
        return MODULE_INIT_FAILED;
    }
    if (!pgalloc_free_n_owner(r->owner, r->backing / PAGE_SIZE, r->prefix_pages)) {
        int rc = module_abort(h);
        return rc ? rc : MODULE_INIT_FAILED;
    }
    r->backing += r->prefix_pages * PAGE_SIZE;
    r->pages -= r->prefix_pages;
    r->prefix_pages = 0;
    r->state = MODULE_RUNNING;
    return 0;
}

/* A dependent which has not returned to ABSENT may still hold raw entrypoints.
 * Keep its dependency body even if its own public exports are now stubbed. */
static int has_dependents(struct module_handle h)
{
    for (u32 i = 0; i < MODULE_RECORD_CAP; i++) {
        const struct module_record *r = &module_records[i];
        if (r->state == MODULE_ABSENT) continue;
        for (u32 j = 0; j < r->dependency_count; j++)
            if (r->dependencies[j].slot == h.slot &&
                r->dependencies[j].generation == h.generation) return 1;
    }
    return 0;
}

int module_abort(struct module_handle h)
{
    struct module_record *r = module_record_get(h);
    if (!r || r->state == MODULE_ABSENT) return 0;
    if (r->state == MODULE_QUARANTINED) return MODULE_ISOLATED;
    if (r->state == MODULE_ROLLING_BACK) return MODULE_BUSY;
    u32 started = r->state == MODULE_STARTING || r->state == MODULE_RUNNING;
    r->state = MODULE_ROLLING_BACK; /* unpublish before any cleanup */
    if (has_dependents(h)) goto quarantined;
    int stopped = 1;
    if (started) {
        if (r->lifecycle.quiesce && !r->lifecycle.quiesce(r)) stopped = 0;
        if (r->lifecycle.detach && !r->lifecycle.detach(r)) stopped = 0;
        if (r->lifecycle.stop_dma && !r->lifecycle.stop_dma(r)) stopped = 0;
    }
    if (!stopped || r->irq_refs || r->tick_refs || r->callback_refs ||
        r->dma_refs || r->active_refs || r->registration_refs) goto quarantined;
    if (r->owner) {
        u32 reclaimed;
        if (!ledger_reclaim_owner(r->owner, &reclaimed)) goto quarantined;
        r->backing = r->pages = r->prefix_pages = 0;
        if (!ledger_owner_retire(r->owner)) goto quarantined;
    }
    r->state = MODULE_ABSENT;
    return 0;
quarantined:
    r->state = MODULE_QUARANTINED;
    return MODULE_ISOLATED;
}

int module_abort_reverse(const struct module_handle *handles, u32 count)
{
    int rc = 0;
    while (count) {
        int result = module_abort(handles[--count]);
        if (result) rc = result;
    }
    return rc;
}

static u32 *ref_count(struct module_record *r, enum module_ref_kind kind)
{
    switch (kind) {
    case MODULE_REF_IRQ: return &r->irq_refs;
    case MODULE_REF_TICK: return &r->tick_refs;
    case MODULE_REF_CALLBACK: return &r->callback_refs;
    case MODULE_REF_DMA: return &r->dma_refs;
    case MODULE_REF_ACTIVE: return &r->active_refs;
    case MODULE_REF_REGISTRATION: return &r->registration_refs;
    }
    return 0;
}

int module_ref_acquire(struct module_handle h, u32 owner,
                       enum module_ref_kind kind, u32 handler)
{
    struct module_record *r = module_record_get(h);
    if (!r || r->owner != owner ||
        (r->state != MODULE_STARTING && r->state != MODULE_RUNNING)) return 0;
    if (kind <= MODULE_REF_CALLBACK && !module_text_contains(r, handler)) return 0;
    u32 *count = ref_count(r, kind);
    if (!count) return 0;
    ++*count;
    return 1;
}

int module_ref_release(struct module_handle h, enum module_ref_kind kind)
{
    struct module_record *r = module_record_get(h);
    if (!r) return 0;
    u32 *count = ref_count(r, kind);
    if (!count || !*count) return 0;
    --*count;
    return 1;
}
