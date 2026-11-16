#ifndef INPUT_HAL_KEYMAP_H
#define INPUT_HAL_KEYMAP_H

#include "types.h"

typedef u16 keysym_t;

typedef struct {
    keysym_t command;
    keysym_t group1[2];
    keysym_t group2[2];
} KmEntry;
STATIC_ASSERT(sizeof(KmEntry) == 10, km_entry_size);

typedef struct {
    u8 src, down, repeat;
    u16 generation;
    u16 keycode;
} KmKey;

typedef struct {
    u16 id, count;              /* 1: PC98, 2: HID */
    const KmEntry *entries;
    const u8 *compat_sc;        /* 0xff: no legacy projection */
    const u8 *norepeat;         /* HID: 256 bits */
    const void *extensions;     /* typed descriptor, e.g. PC98 overrides */
} KmBackend;

/* H0b: internal contracts, not the public policy event ABI. */
enum { KM_MAP_PC98 = 1, KM_MAP_HID = 2 };
enum {
    KM_CHAR = 0, KM_BSD_SPECIAL = 1, KM_LEGACY_CONTROL = 2,
    KM_CELL_INHERIT = 255 /* bank1 only; never an evaluated/wire kind */
};
typedef struct {
    keysym_t sym;
    u8 kind, op;
} KmExactCell;
STATIC_ASSERT(sizeof(KmExactCell) == 4, km_exact_cell_size);

typedef struct {
    u8 accchar, count;
    u8 pairs[52][2];
} KmAccent;
typedef struct {
    const KmExactCell (*bank0)[8]; /* count rows; mandatory */
    const KmExactCell (*bank1)[8]; /* NULL or count rows */
    const u8 *flags;              /* count bytes: O=0 C=1 N=2 B=3 */
    const KmAccent *accents;      /* NULL iff accent_count == 0 */
    u8 accent_count;              /* 0..15 */
} KmPc98Extensions;

/* Shared by registry/evaluator and the generated HID descriptor. */
typedef struct {
    const u8 *order; /* packed 2-bit groups, usage-low bits first */
    const u8 (*actions)[2]; /* {usage, action}: 1 IME_ON, 2 IME_OFF */
    u8 action_count;
    const u8 *no_keydata; /* 256 bits */
} KmHidExtensions;

/* S1pt/S1ht define these; S1x registers both, S1m registers HID only. */
extern const KmBackend pc98_jp_backend;
extern const KmBackend hid_jp_backend;

/* Held modifiers, lock actions and SHIFTA tag; see H0b for valid pairs. */
typedef u16 KmEffect;
enum {
    KM_MOD_SHIFT = 0x0001, KM_MOD_CTRL = 0x0002,
    KM_MOD_ALT = 0x0004, KM_MOD_MODE = 0x0008,
    KM_MOD_COMMAND = 0x0010, KM_MOD_META = 0x0020,
    KM_LOCK_CAPS = 0x0100, KM_LOCK_NUM = 0x0200,
    KM_LOCK_KANA = 0x0400, KM_LOCK_SCROLL = 0x0800,
    KM_LOCK_ALT = 0x1000,
    KM_EFFECT_SHIFTA = 0x2000 /* tap-release AltLock, not a lock bit */
};
typedef struct {
    u16 mods, locks;              /* KM_MOD_* / KM_LOCK_*; not SHIFT_* */
} KmState;
typedef struct {
    const KmBackend *map;
    u32 map_generation;          /* active table revision, not source generation */
} KmSource;
typedef struct {
    keysym_t sym, command;
    KmEffect effect;
    u8 kind, op;                 /* op: BSD ID or legacy byte, else zero */
    u8 compat_sc, is_modifier, no_keydata, action; /* action: 0/1/2 */
} KmEvalResult;

/* 0 success, OS32_ERR_INVAL on stale/unbound source or invalid arguments. */
int km_source_lookup(u8 src, u16 generation, KmSource *out);
/* S0a: invalidate binding only, exact generation match, otherwise no-op. */
void km_source_unbind(u8 src, u16 generation);
/* S0b: pure selection; no held/lock changes, allocation or delivery. */
int km_eval(const KmBackend *map, u16 keycode, const KmState *state,
            KmEvalResult *out);
/* S0c: old_down comes from S0d's sole held array. origin is INPUT_ORIGIN_*. */
int km_state_apply(const KmKey *key, u8 old_down, u8 origin, KmEffect effect);
void km_state_snapshot(KmState *out);
void km_state_release(u8 src, u16 generation);
/* S0d calls this before synthetic breaks, so unplug cannot toggle AltLock. */
void km_state_cancel_tap(u8 src, u16 generation);
/* S0d: release held keys/projection while binding is still available. */
void km_release_keys(u8 src, u16 generation);

/* H0b reservation. S0a replaces this with an external declaration and body;
 * S0c/S0d extend that body before capture is enabled. No weak fallback. */
static inline void km_source_release(u8 src, u16 generation)
{
    (void)src;
    (void)generation;
}

/* S0a release hook; S0d connects the reservation above. */
void km_registry_source_released(u8 src, u16 generation);

/* Descriptors have static lifetime. Duplicate IDs are rejected. */
int km_backend_register(const KmBackend *map);
int km_source_bind(u8 src, u16 generation, u16 map_id);
/* Validate generation and range; discard input from unregistered sources. */
int input_capture_key(const KmKey *key);

/* Only these keypad symbols can be converted to bytes with sym & 0x7f. */
static inline int is_keypad(keysym_t sym)
{
    return sym >= 0xf220 && sym <= 0xf27f;
}

#endif
