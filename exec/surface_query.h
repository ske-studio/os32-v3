#ifndef OS32_SURFACE_QUERY_H
#define OS32_SURFACE_QUERY_H
#include "lease.h"
#include "redir_access.h"

/* Kernel representation of the v70 public value ABI; layouts asserted in C.
 * No physical address, kernel alias, owner or cache in the values. */
#define SURFACE_QUERY_MAX 4
struct surface_desc {
    struct surface_ref ref;
    u32 role, backend, format, width, height, pitch, planes;
    u32 plane_offset[SURFACE_QUERY_MAX];
    u32 bytes, access_max;
};
struct surface_query_result {
    u32 count;
    struct surface_desc desc[SURFACE_QUERY_MAX];
};
/* Trusted kernel input, never copied from an app. The future backend/boot
 * publisher supplies selected, initialized refs in plane order. ready also
 * covers Unicode table readiness. e1 installs no publisher or active path.
 * System roles use backend 0; CLIENT/DISPLAY use the selected backend. */
struct surface_query_source {
    u32 role, backend, ready, count;
    struct surface_ref refs[SURFACE_QUERY_MAX];
};
/* Normal context only; no scheduling/callback between source construction,
 * authorization and use. Failures leave outputs unchanged, preserve IF/CR3.
 * authorize/refs return a kernel value snapshot. query does B1 copyout.
 * refs are kernel staging (e2 must B1-copy user input before calling). */
/* authorize/refs require an IRQ-saved interval for the value snapshot.
 * The caller may be used afterward in normal context with no scheduling or
 * callbacks: AS reclamation is restricted to safe points, and each B1 copy
 * helper saves IRQs and rechecks live identity. ABORT_PENDING refuses copyout
 * (surface_lease then rolls back its unpublished lease). */
int surface_query_authorize(const struct surface_query_source *source,
                            struct caller_access *caller);
int surface_query_refs(const struct surface_query_source *source,
                       const struct surface_ref *refs, u32 count, u32 access);
int surface_query(const struct surface_query_source *source,
                  struct surface_query_result *user_out);
/* Single-surface B1 entry. No planar DISPLAY single-face acquisition. */
int surface_lease(const struct surface_query_source *source,
                  const struct surface_ref *user_ref, u32 access,
                  struct lease_view *user_out);
/* PC98 DISPLAY bundle only. Trusted source is the selected backend snapshot. */
struct surface_lease_result {
    u32 count;
    struct lease_view views[SURFACE_QUERY_MAX];
};
int surface_lease_bundle(const struct surface_query_source *source,
                      const struct surface_ref *user_refs, u32 count, u32 access,
                      struct surface_lease_result *user_out);
/* Public entry bodies. Publisher and caller identity are never user input. */
int surface_api_query(u32 role, struct surface_query_result *out);
int surface_api_lease(u32 role, const struct surface_ref *ref, u32 access,
                      struct lease_view *out);
int surface_api_bundle(u32 role, const struct surface_ref *refs, u32 count,
                       u32 access, struct surface_lease_result *out);
int surface_api_unlease(u32 token);
int surface_query_error(int lease_rc);
#endif
