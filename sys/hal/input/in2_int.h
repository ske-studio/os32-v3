#ifndef INPUT_HAL_IN2_INT_H
#define INPUT_HAL_IN2_INT_H

#include "types.h"

/* Foreground boundary helpers run with interrupts masked. */
int in2_excl_acquire(int owner, u32 flags);
int in2_excl_unacquire(int owner);
u32 in2_focus_generation(void);
void in2_reclaim_generation(void);
int in2_excl_acquired(int owner);
int in2_excl_keyboard(void);
int in2_excl_drop_raw(u8 sc, int is_break);
u32 input_key_release_gen(u8 sc);
int in2_excl_release(int owner);
void in2_excl_focus(int old);
void in2_ring_clear(void);
void in2_ring_lost(u8 reason);
void kbd_raw_purge_keep(void);
void mouse_ext_wheel(i16 delta);
i32 mouse_ext_read_wheel(void);
void in2_on_focus(int old, int owner);

#define IN2_MAX_DEV 16

/* Internal native-endian event. Producers zero fields unused by the type. */
typedef struct InputEvent {
    u32 time_ms;
    u16 generation;
    u8 src, type;
    i16 x, y;
    u8 sc, down, buttons, flags;
} InputEvent;

/* 16B layout is a target contract (u32 = 4 bytes, sys/include/type_contract.h).
 * LP64 host shims that compile kbd.c for other tests skip it. */
#if __SIZEOF_LONG__ == 4
#define IN2_OFFSET(field, value) STATIC_ASSERT(__builtin_offsetof(InputEvent, field) == value, input_event_##field)
STATIC_ASSERT(sizeof(InputEvent) == 16, input_event_size);
IN2_OFFSET(time_ms, 0); IN2_OFFSET(generation, 4);
IN2_OFFSET(src, 6); IN2_OFFSET(type, 7);
IN2_OFFSET(x, 8); IN2_OFFSET(y, 10);
IN2_OFFSET(sc, 12); IN2_OFFSET(down, 13);
IN2_OFFSET(buttons, 14); IN2_OFFSET(flags, 15);
#undef IN2_OFFSET
#define IN2_FIELD_SIZE(field, value) STATIC_ASSERT(sizeof(((InputEvent *)0)->field) == value, input_event_size_##field)
IN2_FIELD_SIZE(time_ms, 4); IN2_FIELD_SIZE(generation, 2);
IN2_FIELD_SIZE(src, 1); IN2_FIELD_SIZE(type, 1);
IN2_FIELD_SIZE(x, 2); IN2_FIELD_SIZE(y, 2);
IN2_FIELD_SIZE(sc, 1); IN2_FIELD_SIZE(down, 1);
IN2_FIELD_SIZE(buttons, 1); IN2_FIELD_SIZE(flags, 1);
#undef IN2_FIELD_SIZE
#endif

enum { INPUT_EVENT_KEY = 1, INPUT_EVENT_MOVE = 2, INPUT_EVENT_BUTTON = 3,
       INPUT_EVENT_WHEEL = 4, INPUT_EVENT_PADBTN = 5, INPUT_EVENT_ABS = 6,
       INPUT_EVENT_LOST = 7 };
enum { INPUT_EVENT_SEAMLESS = 0x01 };
#define IN2_RING_CAPACITY 128
#define INPUT_LOST_OVERFLOW 1
/* LOST: sc=reason, x/y hold the low/high halves of the u32 count. */
int in2_ring_push(const InputEvent *event);
int in2_ring_pop(InputEvent *event, u16 *seq);
u32 in2_ring_dropped(void);
u32 in2_time_ms(void);

int input_src_register(u8 kind, u8 origin, u16 *generation);
void input_src_release(u8 src, u16 generation);

/* Native mechanical locks vs shared USB toggle locks (internal origin). */
enum { INPUT_ORIGIN_NATIVE = 1, INPUT_ORIGIN_USB = 2 };
enum { INPUT_KIND_KBD = 1, INPUT_KIND_MOUSE = 2, INPUT_KIND_PAD = 3 };
int input_capture(const InputEvent *event);
void input_repeat(u8 src, u16 generation, u8 sc);
int input_key_pressed(u8 sc);
u8 input_key_mods(void);
u8 input_buttons(void);
void mouse_ext_mode(int enabled);
int mouse_ext_sample(u8 src, u16 generation, i16 x, i16 y, u32 time_ms);
void mouse_ext_read(i32 *x, i32 *y);
void mouse_ext_read_legacy(i16 *x, i16 *y);
void mouse_legacy_snapshot(i16 *dx, i16 *dy, u8 *buttons, i16 *x, i16 *y);
void pointer_bounds(i16 x0, i16 y0, i16 x1, i16 y1);
void pointer_position(i16 *x, i16 *y);
void pointer_get_bounds(i16 *x0, i16 *y0, i16 *x1, i16 *y1);
/* Capture/sample helpers below require IRQs masked and validated input. */
u8 pointer_buttons_locked(void);
void pointer_position_locked(i16 *x, i16 *y);
void mouse_ext_move(i16 x, i16 y, int marked);
int pointer_seamless(void);
void pointer_absolute_reset(void);
int pointer_absolute_delta(i16 x, i16 y, i16 *dx, i16 *dy);
void pointer_move(i16 x, i16 y);
void pointer_absolute(i16 x, i16 y);
u8 pointer_source_buttons(u8 src);
int pointer_button(InputEvent *event);
/* Stage 5c supplies the legacy aggregate button transition consumer. */
void pointer_legacy_buttons(u8 buttons, u8 changed);
/* Internal helpers: caller holds irq_save across validation and state update. */
int input_src_info(u8 src, u16 generation, u8 *kind, u8 *origin);
void input_state_release(u8 src, u16 generation);
u8 input_held_key(u8 src, u16 keycode); /* caller validates source/range */
void input_merge_key(InputEvent *event, u8 old, u8 origin);
void input_emit_in2(const InputEvent *event);
/* Native boot locks seed the existing mechanical bits with IRQs masked.
 * src must be a registered native keyboard; no synthetic key is published. */
void input_native_locks(u8 src, u8 mods);
void kbd_hal_init(u8 locks);
void kbd_hal_capture(u8 scancode);
extern volatile u8 kbd_shift_state;
void input_emit_legacy_event(const InputEvent *event, u8 old, u8 raw_due);

extern volatile int kbd_gui_mode;
extern volatile int kbd_stop_suppressed;
void appslot_poll_yield_reset(void);
int legacy_stop_filter(int keycode, int is_break, int mods);

/* Cooked ring operations; producers and flush callers already hold IRQ exclusion. */
u32 legacy_cooked_pushed(void);
u32 legacy_cooked_popped(void);
void legacy_cooked_push(u16 entry);
void legacy_cooked_push_mods(u16 entry, u8 mods);
int legacy_cooked_take_mods(u8 *mods);
int legacy_cooked_take(void);
int legacy_cooked_peek(void);
int legacy_cooked_pending(void); /* Number of queued entries. */
void legacy_cooked_flush(void);
void legacy_cooked_reset_dropped(void);
u32 legacy_cooked_dropped(void);

/* Raw operations use the caller's IRQ exclusion. */
void legacy_raw_discard_stop(void);
void legacy_raw_flush(void);
void legacy_raw_reset_dropped(void);
u32 legacy_raw_dropped(void);
void input_gui_key_lost(void);
void legacy_raw_push(int keycode, int is_break, int mods);
int legacy_raw_take(void);

/* Registered at startup and kept resident. A NULL provider or callback means
 * that source/service is absent; callers must not call a NULL pointer. Empty
 * or absent input is -1. peek does not consume a completed key. */
enum InputReadStep { IR_EMPTY, IR_KEY, IR_RECHECK, IR_INTERRUPTED };
struct KmKey;
struct KmEvalResult;
struct InputLegacyProvider {
    int (*terminal_try)(void);
    int (*inject_try)(void);
    int (*terminal_peek)(void);
    int (*inject_peek)(void);
    int (*try_key)(void);
    int (*peek_key)(void);
    int (*gui_active)(void);
    int (*terminal_enabled)(void);
    void (*park_poll)(void);
    void (*park_kbd)(int interruptible);
    u32 (*wait_ticks)(void);
    u32 (*wait_timeout_ticks)(void); /* 30 seconds in wait_ticks units */
    int (*capture_active)(void);
    int (*wait_pending)(void);
    void (*wait_step)(void); /* foreground work and idle */
    void (*wait_prepare)(void); /* Optional CUI pre-read work; never called in GUI. */
    /* Optional ordered IME delivery, installed by the resident provider. */
    int (*capture_ime)(const struct KmKey *, u8, const struct KmEvalResult *);
    void (*flush_actions)(void);
    enum InputReadStep (*read_cooked)(int cancel, int *key);
};

void input_legacy_register(const struct InputLegacyProvider *provider);
const struct InputLegacyProvider *input_legacy_provider(void);

/* Images without the resident FEP keep the usage classifier only. */
static inline const struct InputLegacyProvider *input_ime_provider(void)
{
#if OS32_HOOK_IME
    return input_legacy_provider();
#else
    return 0;
#endif
}

enum { IL_CHAR = 0, IL_KEY = 1, IL_LOCAL_CHAR = 2 };
enum InputReadStep input_read_cooked(int cancel, int *key);
int fep_read_try(int *key);
int fep_read_wait(int interruptible, int *key, u32 *start_tick, int resume);
int input_legacy_peek(int mode);
int input_legacy_try(int mode);
/* Each wait measures its own 30-second capture deadline from entry. FREEZE
 * stays closed; after expiry character waits return CR and key waits ESC. */
int input_legacy_wait(int mode, int interruptible);

#endif
