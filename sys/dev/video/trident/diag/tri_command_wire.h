/* C89 wire shared with userland; module/device types remain private. */
#ifndef KDEBUG_TRI_COMMAND_WIRE_H
#define KDEBUG_TRI_COMMAND_WIRE_H
#include "types.h"
#define TRI_OP_COMMAND 19U
#define TRI_COMMAND_VERSION 2U
/* Fixed case numbers for the shell and kdebug dispatch. */
enum tri_command_case {
    TRI_TEST_ALL = 0,
    TRI_TEST_MODES = 1,
    TRI_TEST_MODE = 2,
    TRI_TEST_DPMS = 3,
    TRI_TEST_CURSOR = 4,
    TRI_TEST_CPU32 = 5,
    TRI_TEST_POLL = 6,
    TRI_TEST_PRESENT = 7,
    TRI_TEST_GAP = 8, TRI_TEST_GAP_SET = 9, TRI_TEST_GAP_SWEEP = 10
};
/* Input: version, sizeof(struct tri_request), test, arg. MODE uses arg (F3 mode ID); GAP_SET uses arg (ns),
 * GAP_SWEEP uses arg/end_ns/step_ns; other cases require zero. rc/result_count must start at zero.
 * Output: rc matches the call result; result_count counts emitted phase lines
 * including unavailable/precheck/end, excluding detail and mismatch lines.
 * NULL buffer/incorrect outer size: INVAL without a reply. Invalid request fields:
 * INVAL in rc, result_count=0, no device operation. Serial gate: BUSY, no lines.
 * ALL runs MODES (mode/depth + CPU32 + cursor), DPMS and POLL. Optional NOSYS
 * phases remain visible while grouped checks continue; an absent whole case
 * returns NOSYS. DPMS also continues across absent individual power states.
 * MODES restores the initial mode; MODE leaves the selected mode active.
 * POLL is one tick; caller repeats it from the foreground for video timers.
 * No pointers, module state, or retained session cross this boundary. */
struct tri_request {
    u32 version, size, test, arg;
    i32 rc;
    u32 result_count;
    u32 end_ns, step_ns; /* GAP_SWEEP: arg=start, inclusive end, positive step */
};
#endif
