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
