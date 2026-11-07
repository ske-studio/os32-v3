/* Private, resident probe broker. Normal cooperative context only; no yield. */
#ifndef DEBUG_PROBE_H
#define DEBUG_PROBE_H

#include "types.h"
#include "module_runtime.h"
#include "os32_kapi_shared.h"

#define PROBE_POINT_MAX 32U
#define PROBE_ACTIVE_MAX 8U
#define PROBE_ITEM_MAX 16U
#define PROBE_SNAPSHOT_MAX 64U
#define PROBE_NAME_SIZE 32U

typedef void (*probe_hit_fn)(u32 id);
struct probe_slot { probe_hit_fn hit; u32 id; };
struct probe_decl { const char *name; struct probe_slot *slot; };
#define PROBE_HIT(s) do { if ((s).hit) (s).hit((s).id); } while (0)
typedef void (*probe_callback_fn)(u32 id);
typedef i32 (*probe_get_decls_fn)(const struct probe_decl **table, u32 *count);

#define PROBE_LIST 1U
#define PROBE_ATTACH 2U
#define PROBE_DETACH 3U
/* Broker results: ARG/BUSY/LIMIT use the existing kernel error numbers. */
#define PROBE_ERR_ARG OS32_ERR_INVAL
#define PROBE_ERR_BUSY OS32_ERR_BUSY
#define PROBE_ERR_LIMIT OS32_ERR_FULL

struct probe_attach {
    struct module_handle owner;
    probe_callback_fn callback;
    u32 count;
    u32 ids[PROBE_ACTIVE_MAX];
};
struct probe_info {
    char name[PROBE_NAME_SIZE];
    struct module_handle owner;
    u32 id, enabled;
};
/* LIST copies RUNNING entries only, in id order; count is output. */
struct probe_list { u32 count; struct probe_info points[PROBE_POINT_MAX]; };

/* register is atomic; STARTING entries stay invisible until RUNNING.
 * One registration reference per point protects borrowed slots. Zero count is
 * a valid empty declaration table. Loaders call unregister before freeing an
 * owner, including startup rollback.
 * unregister stops the entire attachment before removing/reusing any ids.
 * Consumers must discard settings for removed registrations before rearming.
 * DETACH takes the callback owner's handle and is idempotent when stopped.
 * Control buffers are trusted kernel buffers, never userspace wire payloads. */
i32 debug_probe_register(struct module_handle owner,
                         const struct probe_decl *table, u32 count);
i32 debug_probe_unregister(struct module_handle owner);
i32 debug_probe_control(u32 action, void *arg, u32 size);

#endif
