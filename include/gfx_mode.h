/* Internal mode data; hardware timing sequences remain in each backend. */
#ifndef GFX_MODE_H
#define GFX_MODE_H

#include "gfx_format.h"

typedef struct GfxMode {
    u16 width, height;
    u8 format, dac_bits, hw_mode, reserved;
    u32 pitch;                 /* Bytes per row of one plane or packed row. */
} GfxMode;

static inline int gfx_mode_layout(const GfxMode *m, u32 *plane_bytes,
                                  u32 *total_bytes)
{
    GfxFormatDesc desc;
    u32 row, plane;
    if (!m || !m->height || !gfx_format_desc(m->format, &desc) ||
        !gfx_format_row(m->format, m->width, &row) || m->pitch < row ||
        m->pitch > U32_MAX / m->height) return 0;
    plane = m->pitch * m->height;
    if (plane > U32_MAX / desc.planes) return 0;
    *plane_bytes = plane;
    *total_bytes = plane * desc.planes;
    return 1;
}

#endif /* GFX_MODE_H */
