/* Nonblocking character-terminal selection; providers remain resident. */
#include "../input/hal/in2_int.h"

static const struct InputLegacyProvider *legacy;

void input_legacy_register(const struct InputLegacyProvider *provider)
{
    legacy = provider;
}

const struct InputLegacyProvider *input_legacy_provider(void) { return legacy; }

static int legacy_read(int mode, int peek, int cancel, int *recheck)
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
    if (!peek && p->read_cooked) {
        enum InputReadStep step;
        do {
            step = p->read_cooked(cancel, &key);
        } while (step == IR_RECHECK && !cancel);
        if (step == IR_RECHECK) *recheck = 1;
        if (step != IR_KEY) return -1;
        return mode == IL_KEY ? key : key & 0xff;
    }
    read_key = peek ? p->peek_key : p->try_key;
    key = read_key ? read_key() : -1;
    return key < 0 || mode == IL_KEY ? key : key & 0xff;
}

int input_legacy_try(int mode) { return legacy_read(mode, 0, 0, 0); }
int input_legacy_peek(int mode) { return legacy_read(mode, 1, 0, 0); }

int fep_read_try(int *key)
{
    int recheck = 0;
    if (legacy && legacy->wait_prepare) legacy->wait_prepare();
    *key = legacy_read(IL_KEY, 0, 1, &recheck);
    return recheck ? IR_RECHECK : *key >= 0 ? IR_KEY : IR_EMPTY;
}
