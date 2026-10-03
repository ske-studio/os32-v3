#ifndef OS32_SYSTEM_SURFACE_H
#define OS32_SYSTEM_SURFACE_H
#include "surface_query.h"
int system_unicode_register(void);
/* No callbacks/scheduling between source construction and acquisition. */
int system_surface_source(u32 role, struct surface_query_source *out);
#endif
