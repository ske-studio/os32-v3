#ifndef INPUT_HAL_IN2_INT_H
#define INPUT_HAL_IN2_INT_H

#include "types.h"

#define IN2_MAX_DEV 16

/* Internal native-endian event. Producers zero fields unused by the type. */
typedef struct InputEvent {
    u32 time_ms;
    u16 generation;
    u8 src, type;
    i16 x, y;
    u8 sc, down, buttons, flags;
} InputEvent;

/* 16B layout is a target contract (u32 = 4 bytes, include/type_contract.h).
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
/* Internal helpers: caller holds irq_save across validation and state update. */
int input_src_info(u8 src, u16 generation, u8 *kind, u8 *origin);
void input_state_release(u8 src, u16 generation);
void input_merge_key(InputEvent *event, u8 old, u8 origin);
void input_emit_in2(const InputEvent *event);
/* Stage 3a ports remain optional until that stage lands. */
void input_emit_legacy_key(u8 sc, u8 down, u8 mods) __attribute__((weak));
void input_emit_legacy_repeat(u8 sc, u8 mods) __attribute__((weak));

/* Registered at startup and kept resident. A NULL provider or callback means
 * that source/service is absent; callers must not call a NULL pointer. Empty
 * or absent input is -1. peek does not consume a completed key. */
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
};

void input_legacy_register(const struct InputLegacyProvider *provider);
const struct InputLegacyProvider *input_legacy_provider(void);

enum { IL_CHAR = 0, IL_KEY = 1, IL_LOCAL_CHAR = 2 };
int input_legacy_peek(int mode);
int input_legacy_try(int mode);
/* Each wait measures its own 30-second capture deadline from entry. FREEZE
 * stays closed; after expiry character waits return CR and key waits ESC. */
int input_legacy_wait(int mode, int interruptible);

#endif
