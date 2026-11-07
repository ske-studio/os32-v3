/* KDBG_PROBE_DESIGN §3: private, pointer-free shell/kdebug wire. */
#ifndef KDEBUG_PROBE_WIRE_H
#define KDEBUG_PROBE_WIRE_H
#include "types.h"

#define PROBE_OP 20U
#define PROBE_VERSION 1U
#define PROBE_WIRE_BYTES 128U
#define PROBE_SERIAL 1U
enum probe_command {
    PROBE_CMD_LIST = 1, PROBE_CMD_ADD, PROBE_CMD_ARM, PROBE_CMD_STOP,
    PROBE_CMD_RESET, PROBE_CMD_DRAIN, PROBE_CMD_STATUS
};
enum probe_kind { PROBE_IO = 1, PROBE_MMIO, PROBE_IDXIO, PROBE_IDXMMIO };
enum probe_status {
    PROBE_OK = 0, PROBE_ARG, PROBE_NOSYS, PROBE_WINDOW, PROBE_IO_ERROR,
    PROBE_LIMIT, PROBE_BUSY, PROBE_OVERFLOW, PROBE_SKIP
};
struct probe_wire {
    u32 version, size, action, flags;
    char name[32];
    u32 id, kind, width, address, data_address, index;
    u32 hit, item, value;
    i32 status, restore_rc;
    u32 captured, dropped, overflow, more, reserved[5];
};
STATIC_ASSERT(sizeof(struct probe_wire) == 128, probe_wire_size);
/* Input: all unused words/bytes zero. LIST uses id as the ordinal cursor,
 * returns one registered name/id, flags=enabled, more=remaining entries.
 * ADD uses name and the five descriptor words. ARM accepts flags bit0 only.
 * STATUS flags=armed, status=latched error; DRAIN returns one item and more,
 * consuming it. Empty DRAIN has hit=0. Counters persist until RESET.
 * Nonzero call return invalidates all output. Busy commands return BUSY in
 * status. A registry epoch change latches ARG and discards settings, keeping
 * old snapshots drainable; ADD may immediately start a fresh configuration. */
#ifdef __KERNEL_BUILD__
/* Internal descriptor, also copied into each snapshot item. */
struct probe_item { u32 kind, width, address, data_address, index; };
int probe_item_valid(const struct probe_item *item);
i32 probe_index_read(const struct probe_item *item, u32 *value, i32 *restore_rc);
i32 probe_call(void *buf, u32 size);
#endif
#endif
