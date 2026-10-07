#ifndef OS32_LIBGFX_ATTACH_H
#define OS32_LIBGFX_ATTACH_H
#include "libos32gfx.h"

/* Private testable port, not the public KAPI ABI. v70 binds the generated
 * query/lease/unlease slots; NULL supports pre-v70 hosts.
 * One CLIENT per library instance. No callbacks/scheduling within an attach.
 * query describes registered storage (400 planar lines even in 200 mode).
 * lease publishes a view only on success; unlease(INVAL) also covers revoke.
 * Set the port only before attaching, or after libos32gfx_detach(). */
struct gfx_attach_ref { u32 sid, generation; };
struct gfx_attach_desc {
    struct gfx_attach_ref ref;
    u32 format, width, height, pitch, planes, plane_offset[4], bytes;
};
struct gfx_attach_view { u32 token, base, bytes, planes[4]; };
struct gfx_attach_port {
    int (*query)(struct gfx_attach_desc *out);
    int (*lease)(const struct gfx_attach_ref *ref, struct gfx_attach_view *out);
    int (*unlease)(u32 token);
};
extern const struct gfx_attach_port *gfx_attach_port;
/* Independent RO Unicode port, bound with CLIENT for v70. No public ABI/CRT dependency.
 * Both C instances own one token; teardown's revoke_all releases it.
 * Set only before the first acquisition. query/lease have no callbacks. */
extern const struct gfx_attach_port *gfx_unicode_port;
void libos32gfx_unicode_init(void);
/* Detach affects this instance's CLIENT only. */
void libos32gfx_detach(void);
#endif
