/* Pointer-free private kdebug request; no KernelAPI change. */
#ifndef PEGC_COMMAND_WIRE_H
#define PEGC_COMMAND_WIRE_H
#include "types.h"
#define PEGC_OP_COMMAND 21U
#define PEGC_COMMAND_VERSION 1U
#define PEGC_TEST_PRESENT 0U
#define PEGC_TEST_GAP_SWEEP 1U
struct pegc_request {
    u32 version, size, test, start_ns, end_ns, step_ns;
    i32 rc;
    u32 result_count;
};
int pegc_command_call(void *buf, u32 size);
#endif
