/* Boot-only module transactions. Internal exports remain separate from KAPI. */
#ifndef MODULE_RUNTIME_H
#define MODULE_RUNTIME_H

#include "module_image.h"

#define MODULE_DEPENDENCY_CAP 8U

enum module_state {
    MODULE_ABSENT, MODULE_HEADER_CHECKED, MODULE_OWNED_IMAGE,
    MODULE_VALIDATED, MODULE_RELOCATED, MODULE_STARTING, MODULE_RUNNING,
    MODULE_ROLLING_BACK, MODULE_QUARANTINED
};
enum module_result {
    MODULE_NOT_FOUND = 2, MODULE_NO_PAGES = 7, MODULE_SHORT_READ = 8,
    MODULE_INIT_FAILED = 10, MODULE_RECORD_FULL = 13,
    MODULE_BAD_DEPENDENCY, MODULE_BUSY, MODULE_ISOLATED
};
enum module_ref_kind {
    MODULE_REF_IRQ, MODULE_REF_TICK, MODULE_REF_CALLBACK, MODULE_REF_DMA,
    MODULE_REF_ACTIVE, MODULE_REF_REGISTRATION
};
struct module_handle { u32 slot, generation; };
struct module_record;
/* Resident adapters, copied into the record. start receives a private ops copy.
 * Cleanup hooks return 1 only after their operation has completed. detach must
 * remove callbacks/IRQ/tick and drain calls; stop_dma must confirm DMA stopped.
 * Missing hooks mean that the component creates no such resources. */
struct module_lifecycle {
    int (*start)(struct module_record *record, u32 entry, u32 *candidate);
    int (*quiesce)(struct module_record *record);
    int (*detach)(struct module_record *record);
    int (*stop_dma)(struct module_record *record);
};
struct module_request {
    const char *path, *name;
    struct module_image_policy policy;
    const u32 *imports; /* policy.import_count resolved addresses */
    const struct module_handle *dependencies;
    u32 dependency_count;
    struct module_lifecycle lifecycle;
};
struct module_record {
    u32 owner, generation, state, module_id, pair_crc;
    u32 backing, pages, prefix_pages, base, text, text_size, init;
    u32 exports[MODULE_RUNTIME_SLOTS + 1];
    struct module_handle dependencies[MODULE_DEPENDENCY_CAP];
    u32 dependency_count;
    u32 irq_refs, tick_refs, callback_refs, dma_refs, active_refs;
    u32 registration_refs;
    struct module_lifecycle lifecycle;
};
extern struct module_record module_records[MODULE_RECORD_CAP];

/* Normal boot context only. No SERVICES_READY unload/reload API.
 * load reserves a record before opening/allocating. After reservation *out is
 * set even on failure for diagnostics; an ABSENT record's owner is retired.
 * No request, name/table or prefix pointers survive load. */
int module_load(const struct module_request *request, struct module_handle *out);
struct module_record *module_record_get(struct module_handle handle);
const u32 *module_exports(struct module_handle handle);
/* Success means RUNNING; prefix-return failure is always nonzero. */
int module_start(struct module_handle handle);
int module_abort(struct module_handle handle);
/* Boot transaction caller supplies successful loads in load order. */
int module_abort_reverse(const struct module_handle *handles, u32 count);
/* Broker adapters use these before registration and after confirmed removal.
 * STARTING registrations work; handler must belong to this owner's text.
 * DMA/active/registration counts use handler=0. They do not replace hardware
 * stop checks. For FS/Device: acquire before the final init registration,
 * release on registration failure or after detach removes all its users.
 * A failed detach must retain the reference; abort then isolates the owner.
 * IRQ adapters may dispatch STARTING handlers after acquire, before init
 * completes; module_exports is deliberately not their dispatch gate. */
int module_ref_acquire(struct module_handle handle, u32 owner,
                       enum module_ref_kind kind, u32 handler);
int module_ref_release(struct module_handle handle, enum module_ref_kind kind);

#endif
