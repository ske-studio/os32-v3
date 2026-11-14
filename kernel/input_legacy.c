/* Nonblocking character-terminal selection; providers remain resident. */
#include "../input/hal/in2_int.h"

static const struct InputLegacyProvider *legacy;

void input_legacy_register(const struct InputLegacyProvider *provider)
{
    legacy = provider;
}

static int legacy_read(int mode, int peek)
{
    const struct InputLegacyProvider *p = legacy;
    int (*read_key)(void);
    int key;

    if (!p) return -1;
    if (p->gui_active && p->gui_active()) {
        if (mode == IL_LOCAL_CHAR) return -1;
        read_key = peek ? p->inject_peek : p->inject_try;
        key = read_key ? read_key() : -1;
        if (key < 0 && !peek && p->park_poll) p->park_poll();
        return key;
    }
    if (mode != IL_LOCAL_CHAR && p->terminal_enabled && p->terminal_enabled()) {
        read_key = peek ? p->terminal_peek : p->terminal_try;
        key = read_key ? read_key() : -1;
        if (key >= 0) return key;
    }
    read_key = peek ? p->peek_key : p->try_key;
    key = read_key ? read_key() : -1;
    return key < 0 || mode == IL_KEY ? key : key & 0xff;
}

int input_legacy_try(int mode) { return legacy_read(mode, 0); }
int input_legacy_peek(int mode) { return legacy_read(mode, 1); }
