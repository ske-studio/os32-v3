#ifndef GFX_OSRI_VIEW_H
#define GFX_OSRI_VIEW_H
#include "types.h"
/* Kernel-owned call context. No USER pointers survive the entry copy. */
struct OsriViewRun { int caller; u32 start, duration_ms; };
int gfx_is_started(void);
int gfx_osri_view(const char *path, u32 duration_ms, int caller);
int gfx_osri_poll_abort(int caller);
#endif
