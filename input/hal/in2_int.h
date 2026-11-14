#ifndef INPUT_HAL_IN2_INT_H
#define INPUT_HAL_IN2_INT_H

#include "types.h"

#define IN2_MAX_DEV 16

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
};

void input_legacy_register(const struct InputLegacyProvider *provider);

enum { IL_CHAR = 0, IL_KEY = 1, IL_LOCAL_CHAR = 2 };
int input_legacy_peek(int mode);
int input_legacy_try(int mode);
/* Each wait measures its own 30-second capture deadline from entry. FREEZE
 * stays closed; after expiry character waits return CR and key waits ESC. */
int input_legacy_wait(int mode, int interruptible);

#endif
