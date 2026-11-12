/* Terminal byte stream to the cooked keyboard representation. */
#include "input_boot.h"
#include "kernel/cli/resident_cli.h"

enum { ESC = 27, ESC_TIMEOUT_US = 50000 };

int pm_input_decode(PmInputDecoder *d, int byte, u32 now)
{
    if (d->state && (u32)(now - d->started) >= ESC_TIMEOUT_US) {
        d->state = 0;
        d->pending = byte;
        return ESC;
    }
    if (byte == RESIDENT_CLI_KEY_CANCEL || byte == 3) {
        d->state = d->after_cr = 0;
        return ESC;
    }
    if (byte < 0) return -1;
    if (d->state) {
        if (d->state == 1 && byte == '[') {
            d->state = 2;
            return -1;
        }
        if (d->state == 2 && (byte == 'A' || byte == 'B')) {
            d->state = 0;
            return byte == 'A' ? 0x3A00 : 0x3D00;
        }
        d->state = 0;
        d->pending = byte;
        return ESC;
    }
    if (d->after_cr) {
        d->after_cr = 0;
        if (byte == '\n') return -1;
    }
    if (byte == ESC) {
        d->state = 1;
        d->started = now;
        return -1;
    }
    if (byte == '\r') d->after_cr = 1;
    if (byte == '\n') return '\r';
    if (byte == 127) return '\b';
    return byte;
}
