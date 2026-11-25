#ifndef OS32_LEGACY_GUEST_H
#define OS32_LEGACY_GUEST_H
#include "types.h"
struct kapi_arg_source;
/* Link exactly one provider. State queries are independent: a pinned caller
 * need not have opened low mappings yet. End precedes normal-stack cleanup. */
int legacy_init(void);
int legacy_active(void);
int legacy_pinned(void);
int legacy_session_open(void);
int legacy_closing(void);
int legacy_lowmap_busy(void);
void legacy_abort_begin(void);
void legacy_end(void);
void legacy_cleanup(void);
/* Only the native provider may inspect a native frame. Consumed=1. */
int legacy_trap(void *native_frame);
void legacy_request_exit(void);
int legacy_kbd_push(u8 scancode);
/* Other slot=0, handled=1, bad caller=-1 (caller performs fault kill). */
int legacy_call_dispatch(u32 slot, void *fn, struct kapi_arg_source *src, unsigned count,
                         u32 *result);
#endif
