/* Layer 3 lifecycle owner. Canonical contract: GF_FEAT3_DESIGN.md,
 * C1/C2/C4 addendum and final decisions F1-F7 (2026-11-04).
 * C1 owns one initially zero static service. Cooperative foreground only;
 * no reentry from waits, heap, locks or reference counts.
 * Lifecycle model: nouveau (c03c1a1), drivers/gpu/drm/nouveau/
 * nvkm/engine/fifo/uchan.c:272-298 and nv04.c:41-99. */
#ifndef HWDEV_GF_ACCEL_SERVICE_H
#define HWDEV_GF_ACCEL_SERVICE_H
#include "../../include/types.h"
#include "accel.h"

struct gf_production;
struct gf3_service {
    struct gf_production *production;
    const struct gf3_test_ops **slot;
    struct gf_vram_reservation_node scan_node, test_node;
    struct gf3_surface scan, test;
    struct gf3_test_ops ops;
    /* F3: copied framebuffer geometry survives reservation turnover.
     * phys is physical, pitch is bytes, width/height are pixels. Recover
     * reuses this geometry and replaces reservation IDs/generations. */
    u32 scan_pitch, scan_width, scan_height, scan_phys;
};
/* production/slot are borrowed; each node owns itself by address. The only
 * ctx is production->accel, never copied. Required NULL: GF3_EINVAL;
 * different p/slot on rebind: GF3_ESTATE; same pair: unchanged success.
 * First bind saves the pair and clears *slot. */
i32 gf3_service_bind(struct gf_production *p, const struct gf3_test_ops **slot);
/* Only the bound production channel matches. NULL, diagnostic channels and
 * an unbound service return NULL. No production casts at call sites. */
struct gf3_service *gf3_service_get(struct gf2_chan *ch);
/* RUN required, already ready is ESTATE. Registry init, scan/test reserve,
 * gf3_init, ctx/scan bind, then publish &s->ops. Retain existing reservations
 * for retry without double reserve; ops borrows resident ctx/test_surface.
 * dl is one absolute deadline, shared with recover and never renewed. */
i32 gf3_service_start(struct gf3_service *s, u32 dl);
/* Clear slot, unbind scan, then gf_vram_views_close. Keep ready for fini.
 * Close failure retains the failed window, reservation and adapter. C2
 * records close failure but continues cleanup when production is revoked. */
i32 gf3_service_close(struct gf3_service *s);
/* Call gf3_fini only when ready, RUN, fault-free and epoch-matched; otherwise
 * return 0 for F2 cleanup. A failed clock forbids a new fini fence, but does
 * not prevent F2 stop. C2 attempts F2 stop even after fini failure. */
i32 gf3_service_fini(struct gf3_service *s, u32 dl);
/* Internal: only after old F2 is confirmed OFF. Zero old objects/notifier/
 * storage/off/epoch and ready; preserve ch and last_rc. No object_del/free
 * or zeroing of live inst nodes. NULL is a no-op. */
void gf3_service_invalidate(struct gf3_service *s);
/* Release live test then scan nodes after display use ends; zero each
 * released surface, retain node ID/generation history and failed ownership.
 * Unregistered nodes are no-ops. C2 owns display stop and this release;
 * cleanup entered with !engaged releases only if the channel is OFF. */
i32 gf3_service_release(struct gf3_service *s);
/* F1: close -> fini -> gf2_stop -> OFF check -> invalidate, retaining
 * reservations and display use. Normal video stop uses the C2 display
 * cleanup instead. OFF can accompany an old fault rc; preserve first rc. */
i32 gf3_service_stop(struct gf3_service *s);
/* Close, stop and confirm OFF, recover F2, invalidate old descriptors.
 * Only an engaged display updates reservations, initializes F3 and publishes
 * hook/slot; otherwise recover F2 alone. Stop failure before OFF retains all
 * ownership. F2 recover failure permits invalidate but keeps reservations.
 * F2: init/reservation-update failure leaves slot closed and ctx/reservations
 * retained for the next C2 stop. Never replay a partly submitted operation. */
i32 gf3_service_recover(struct gf3_service *s, u32 dl);
/* Except get/invalidate: NULL required arguments are GF3_EINVAL, invalid
 * RUN/epoch/ownership state is GF3_ESTATE, success is 0. Propagate lower
 * F1/F2/F3 negative rc unchanged and save failures in accel.last_rc.
 * Callers reacquire dispatch/ctx/surface loans for every synchronous call. */
#endif
