#ifndef OS32_SURFACE_QUERY_H
#define OS32_SURFACE_QUERY_H
#include "lease.h"
#include "redir_access.h"

/* T2e e1: kernel-only staging of the future value ABI. No KAPI/SDK exposure
 * until e11. No physical address, kernel alias, owner or cache in the values. */
#define SURFACE_QUERY_MAX 4
#define SURFACE_ROLE_TVRAM 3
#define SURFACE_ROLE_UNICODE 4
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
int surface_query_authorize(const struct surface_query_source *source,
                            struct caller_access *caller);
int surface_query_refs(const struct surface_query_source *source,
                       const struct surface_ref *refs, u32 count, u32 access);
int surface_query(const struct surface_query_source *source,
                  struct surface_query_result *user_out);
int surface_query_error(int lease_rc);
#endif
