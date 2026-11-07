/* x86 V86 service adapter; native frames and session state stay here. */
#include "legacy_guest.h"
#include "v86.h"
#include "v86_mem.h"
#include "v86_module.h"
#include "v86_kbd.h"
#include "v86_call.h"
#include "x86_flags.h"

int legacy_init(void) { return v86_module_load(); }
int legacy_active(void) { return v86_is_active(); }
int legacy_pinned(void) { return v86_call_pinned(); }
int legacy_session_open(void) { return v86_session.open; }
int legacy_closing(void) { return v86_session.closing; }
int legacy_lowmap_busy(void) { return paging_v86_session_open(); }
void legacy_abort_begin(void) { v86_session.aborting = 1; }
void legacy_end(void) { v86_session_end(); }
void legacy_cleanup(void) { v86_call_cleanup(); }
void legacy_request_exit(void) { v86_request_exit(); }
int legacy_kbd_push(u8 scancode) { return v86_kbd_push(scancode); }
int legacy_trap(void *native_frame)
{
    u32 *frame = native_frame;
    if (!(frame[V86I_EFLAGS] & EFLAGS_VM)) return 0;
    v86_int80(frame);
    return 1;
}
int legacy_call_dispatch(u32 slot, void *fn, const void *args, u32 bytes,
                         u32 *result)
{
    return v86_call_dispatch(slot, fn, args, bytes, result);
}
