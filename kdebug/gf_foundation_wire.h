/* C89 request shared with the shell; kernel owner types stay private. */
#ifndef KDEBUG_GF_FOUNDATION_WIRE_H
#define KDEBUG_GF_FOUNDATION_WIRE_H
#include "types.h"

#define GF_OP_FOUNDATION 16U
enum gf1_case { GF1_STATUS, GF1_INSTMEM, GF1_VBLANK, GF1_CYCLE };
struct gf1_request { u32 size, test, session; int rc; };
#endif
