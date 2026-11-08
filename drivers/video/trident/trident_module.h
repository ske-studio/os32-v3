/* Kernel/module loan contract, TRIDENT_S3_DESIGN §3 / TRIDENT_FULL_DESIGN §9. */
#ifndef TRIDENT_MODULE_H
#define TRIDENT_MODULE_H
#include "gfx_hal.h"
#include "trident.h"
#define TRIDENT_MODULE_OPS_VERSION 3u
#define TRIDENT_DIAGNOSTIC_OPS_VERSION 2u
/* F10 publishes this table and the production device through diagnostic_out.
 * The table is module resident and immutable; context borrows F10's device,
 * never a second diagnostic device. F13 validates the complete table and
 * context data ranges, version/size and every non-NULL callback's module text
 * address before retaining it. Publication requires successful prepare and a
 * pin; failed load/prepare and completed detach leave the core slot NULL.
 *
 * F17/H reread trident_diagnostic_ops() before dispatch. NULL table/optional
 * callback means unavailable, not a failure of mandatory display startup.
 * Primitive calls return TRI_* rc unchanged; optional features may return
 * TRI_E_NOSYS. PRESENT temporarily selects context.present_method and restores it. Surface arguments
 * obey trident.h's validation/clipping and allocation lifetime contracts.
 * Mode/DPMS/cursor use F10's HAL adapters (HAL rc/head0 contract); all other
 * callbacks have the exact corresponding tri_* signature from trident.h. */
struct TridentDiagnosticOps {
    u32 version, size;
    struct trident_device *context;
    int (*set_mode)(u32 id);
    int (*dpms)(u8 head, u32 state);
    int (*cursor_image)(u8 head, const struct GfxCursorImage *);
    int (*cursor_move)(u8 head, i32 x, i32 y);
    int (*cursor_show)(u8 head, int show);
    int (*sync)(struct trident_device *);
    int (*clear_sync)(struct trident_device *);
    int (*copy_direction)(struct trident_device *, int xdir, int ydir);
    int (*line_setup)(struct trident_device *, u32 fg, i32 bg, u8 rop,
                      u8 dash_length);
    int (*fill)(struct trident_device *, const struct tri_surface *,
                int x, int y, int w, int h, u32 color, u8 rop);
    int (*copy)(struct trident_device *, const struct tri_surface *src,
                const struct tri_surface *dst, int sx, int sy, int dx, int dy,
                int w, int h, u8 rop, int transparent, u32 key);
    int (*pattern)(struct trident_device *, const struct tri_surface *,
                   u32 pattern_off, int x, int y, int w, int h,
                   u32 fg, i32 bg, u8 rop);
    int (*line)(struct trident_device *, const struct tri_surface *, int x, int y,
                int dmaj, int dmin, int error, int len, u8 octant,
                u16 dash, u8 phase);
    int (*pattern_upload)(struct trident_device *, const u8 rows[8], u32 *off);
    int (*video_query)(struct tri_video_image *);
    int (*video_put)(struct trident_device *, const struct tri_video_image *,
                     const struct tri_rect *src, const struct tri_rect *dst,
                     u32 key);
    int (*video_set_key)(struct trident_device *, u32 key);
    int (*video_stop)(struct trident_device *, bool shutdown);
    /* Foreground cooperative loop only, never IRQ or present-driven. Call
     * after Put/Stop through OFF -> FREE -> release, even without drawing.
     * F4 reads fresh sys_time_now on every call; no stored caller deadline.
     * Return/record poll's rc (including video.last_rc), never discard it or
     * feed an optional video failure into mandatory present/initialization.
     * Leave/Close stop polling and use video_stop(shutdown=true). */
    int (*poll)(struct trident_device *);
    int (*video_surface_alloc)(struct trident_device *, struct tri_video_surface *,
                               u32 format, u32 width, u32 height);
    int (*video_surface_display)(struct trident_device *, struct tri_video_surface *,
                                 const struct tri_rect *src,
                                 const struct tri_rect *dst, u32 key);
    int (*video_surface_stop)(struct trident_device *, struct tri_video_surface *);
    int (*video_surface_free)(struct trident_device *, struct tri_video_surface *);
    int (*present)(struct trident_device *, int, int, int, int);
};
#define TRIDENT_DIAGNOSTIC_OPS_SIZE ((u32)sizeof(struct TridentDiagnosticOps))
/* All loans (including backend, counters and scalar addresses) are kernel
 * resident. resources is immutable after publication. display_owner calls appslot_gfx_owner()
 * for the current exec display owner, distinct from DEVICE resource owner.
 * Loader validates initialize/prepare/restore text and pins before arm.
 * prepare only inspects descriptors; it must not invoke Init or indexed I/O.
 * Optional GfxBackend slots are NULL or validated module text (F13).
 * The version-1 prefix is unchanged; version 2 appends the diagnostic output
 * slot and context size. Both sides require size == TRIDENT_MODULE_CONTEXT_SIZE
 * before using diagnostic_out. The module ops version gates this
 * larger context; initialize/prepare/restore retain their existing slots. */
typedef struct TridentModuleContext {
    GfxBackend *backend;
    GfxCounters *counters;
    const struct trident_resources *resources;
    int (*arm)(int (*restore)(void *), void *, u32 owner);
    int *height, *flip, *page;
    int (*display_owner)(void); /* current owner on every return, not arm owner */
    u32 size;
    const struct TridentDiagnosticOps **diagnostic_out; /* kernel static slot */
} TridentModuleContext;
#define TRIDENT_MODULE_CONTEXT_SIZE ((u32)sizeof(TridentModuleContext))
struct probe_decl;
struct TridentModuleOps {
    u32 version;
    int (*initialize)(GfxBackend *, const TridentModuleContext *);
    int (*prepare)(void); /* rc boundary behind HAL's void prepare */
    int (*restore)(void *);
    i32 (*get_probe_decls)(const struct probe_decl **, u32 *);
};
#define TRIDENT_MODULE_OPS_SIZE ((u32)sizeof(struct TridentModuleOps))
/* F13 core getter, also imported by F18b for F17. NULL until validated/pinned
 * prepare succeeds; do not retain the result across module detach. */
const struct TridentDiagnosticOps *trident_diagnostic_ops(void);
extern const TridentModuleContext *trident_context;
extern const struct TridentModuleOps trident_module_ops;
extern GfxBackend trident_backend_ops;
int trident_module_init(GfxBackend *out, const TridentModuleContext *context);
int trident_backend_prepare(void);
int trident_module_load(void);
int trident_module_abort(void); /* BUSY after arm, retains pin/maps/save */
#endif
