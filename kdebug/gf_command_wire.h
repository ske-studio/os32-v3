/* C89 wire shared with the shell; channel and module types stay private. */
#ifndef KDEBUG_GF_COMMAND_WIRE_H
#define KDEBUG_GF_COMMAND_WIRE_H
#include "types.h"
#define GF_OP_COMMAND 17U
enum gf2_case {
    GF2_STATUS, GF2_START, GF2_REFERENCE, GF2_STOP, GF2_RECOVER,
    GF2_CYCLE, GF2_BAD_HANDLE, GF2_BAD_METHOD, GF2_VBLANK
};
struct gf2_request { u32 size, test, session; int rc; };
#endif
