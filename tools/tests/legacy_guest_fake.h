/* Shared optional-service fake. Opt in per owned entry; hooks may call the
 * genuine backend when a fixture tests session restoration or VM decoding.
 * State and ordered history are shared across selected entries. */
#ifndef OS32_LEGACY_GUEST_FAKE_H
#define OS32_LEGACY_GUEST_FAKE_H
#include "legacy_guest.h"
struct legacy_fake_state {
    int active, pinned, session_open, closing, lowmap, init_result, dispatch_result, kbd_result;
    unsigned count, history[128];
};
static struct legacy_fake_state legacy_fake __attribute__((unused));
static void __attribute__((unused)) legacy_fake_note(unsigned event)
{
    if (legacy_fake.count < 128) legacy_fake.history[legacy_fake.count++] = event;
}
#endif /* shared state */
#ifdef LEGACY_FAKE_INIT_ENABLE
#ifndef LEGACY_FAKE_INIT
#define LEGACY_FAKE_INIT legacy_fake.init_result
#endif
int legacy_init(void)
{ legacy_fake_note(1); return LEGACY_FAKE_INIT; }
#undef LEGACY_FAKE_INIT_ENABLE
#endif
#ifdef LEGACY_FAKE_ACTIVE_ENABLE
#ifndef LEGACY_FAKE_ACTIVE
#define LEGACY_FAKE_ACTIVE legacy_fake.active
#endif
int legacy_active(void)
{ legacy_fake_note(2); return LEGACY_FAKE_ACTIVE; }
#undef LEGACY_FAKE_ACTIVE_ENABLE
#endif
#ifdef LEGACY_FAKE_PINNED_ENABLE
#ifndef LEGACY_FAKE_PINNED
#define LEGACY_FAKE_PINNED legacy_fake.pinned
#endif
int legacy_pinned(void)
{ legacy_fake_note(3); return LEGACY_FAKE_PINNED; }
#undef LEGACY_FAKE_PINNED_ENABLE
#endif
#ifdef LEGACY_FAKE_SESSION_OPEN_ENABLE
#ifndef LEGACY_FAKE_SESSION_OPEN
#define LEGACY_FAKE_SESSION_OPEN legacy_fake.session_open
#endif
int legacy_session_open(void)
{ legacy_fake_note(13); return LEGACY_FAKE_SESSION_OPEN; }
#undef LEGACY_FAKE_SESSION_OPEN_ENABLE
#endif
#ifdef LEGACY_FAKE_CLOSING_ENABLE
#ifndef LEGACY_FAKE_CLOSING
#define LEGACY_FAKE_CLOSING legacy_fake.closing
#endif
int legacy_closing(void)
{ legacy_fake_note(4); return LEGACY_FAKE_CLOSING; }
#undef LEGACY_FAKE_CLOSING_ENABLE
#endif
#ifdef LEGACY_FAKE_LOWMAP_ENABLE
#ifndef LEGACY_FAKE_LOWMAP
#define LEGACY_FAKE_LOWMAP legacy_fake.lowmap
#endif
int legacy_lowmap_busy(void)
{ legacy_fake_note(5); return LEGACY_FAKE_LOWMAP; }
#undef LEGACY_FAKE_LOWMAP_ENABLE
#endif
#ifdef LEGACY_FAKE_ABORT_ENABLE
#ifndef LEGACY_FAKE_ABORT
#define LEGACY_FAKE_ABORT (void)0
#endif
void legacy_abort_begin(void)
{ legacy_fake_note(6); LEGACY_FAKE_ABORT; }
#undef LEGACY_FAKE_ABORT_ENABLE
#endif
#ifdef LEGACY_FAKE_END_ENABLE
#ifndef LEGACY_FAKE_END
#define LEGACY_FAKE_END (void)0
#endif
void legacy_end(void)
{ legacy_fake_note(7); LEGACY_FAKE_END; }
#undef LEGACY_FAKE_END_ENABLE
#endif
#ifdef LEGACY_FAKE_CLEANUP_ENABLE
#ifndef LEGACY_FAKE_CLEANUP
#define LEGACY_FAKE_CLEANUP (void)0
#endif
void legacy_cleanup(void)
{ legacy_fake_note(8); LEGACY_FAKE_CLEANUP; }
#undef LEGACY_FAKE_CLEANUP_ENABLE
#endif
#ifdef LEGACY_FAKE_EXIT_ENABLE
#ifndef LEGACY_FAKE_EXIT
#define LEGACY_FAKE_EXIT (void)0
#endif
void legacy_request_exit(void)
{ legacy_fake_note(9); LEGACY_FAKE_EXIT; }
#undef LEGACY_FAKE_EXIT_ENABLE
#endif
#ifdef LEGACY_FAKE_KBD_ENABLE
#ifndef LEGACY_FAKE_KBD
#define LEGACY_FAKE_KBD legacy_fake.kbd_result
#endif
int legacy_kbd_push(u8 scancode)
{ legacy_fake_note(10); (void)scancode; return LEGACY_FAKE_KBD; }
#undef LEGACY_FAKE_KBD_ENABLE
#endif
#ifdef LEGACY_FAKE_TRAP_ENABLE
#ifndef LEGACY_FAKE_TRAP
#define LEGACY_FAKE_TRAP 0
#endif
int legacy_trap(void *frame)
{ legacy_fake_note(11); (void)frame; return LEGACY_FAKE_TRAP; }
#undef LEGACY_FAKE_TRAP_ENABLE
#endif
#ifdef LEGACY_FAKE_DISPATCH_ENABLE
#ifndef LEGACY_FAKE_DISPATCH
#define LEGACY_FAKE_DISPATCH legacy_fake.dispatch_result
#endif
int legacy_call_dispatch(u32 slot, void *fn, struct kapi_arg_source *src, unsigned count, u32 *result)
{ legacy_fake_note(12); (void)slot; (void)fn; (void)src; (void)count; (void)result; return LEGACY_FAKE_DISPATCH; }
#undef LEGACY_FAKE_DISPATCH_ENABLE
#endif
