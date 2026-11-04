/* kapi_osri.c — gfx_osri_show (KAPI v80). Registration stub; the OSRI show
 * stage owns the body (docs/decisions/gui/OSRI_VIEWER_DESIGN.md §3). */
#include "os32_kapi_shared.h"

int kapi_osri_show(const char *path, u32 duration_ms)
{
    (void)path; (void)duration_ms;
    return OS32_ERR_NOSYS;
}
