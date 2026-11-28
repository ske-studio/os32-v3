#ifndef OS32_GFX_MEASURE_WIRE_H
#define OS32_GFX_MEASURE_WIRE_H

#include "types.h"

#define GFX_G5M_RUN       0x47354d31U
#define GFX_TRACE_CONTROL 0x47585443U
#define GFX_TRACE_BOUNDARY 0x47585442U
#define GFX_TRACE_POLICY     0x47585446U
#define GFX_TRACE_BEGIN    0x80000000U
#define GFX_TRACE_EVENT   0x47585445U

enum gfx_trace_policy { GFX_TRACE_NORMAL, GFX_TRACE_REDRAW, GFX_TRACE_FAIL_ONCE };

enum gfx_trace_kind {
    GFX_TRACE_FRAME, GFX_TRACE_PRESENT, GFX_TRACE_PACKED,
    GFX_TRACE_PLANE, GFX_TRACE_PREVIOUS, GFX_TRACE_PUT, GFX_TRACE_GET,
    GFX_TRACE_FLIP, GFX_TRACE_FAILED
};
struct gfx_trace_event {
    u32 kind, frame;
    int x, y, w, h;
    u32 bytes, detail;
};
#endif
