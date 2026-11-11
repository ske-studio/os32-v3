#ifndef RESIDENT_CLI_H
#define RESIDENT_CLI_H

#include "types.h"

#define RESIDENT_CLI_KEY_CANCEL 0x100

struct resident_cli_ops {
    /* 0..255 = byte, RESIDENT_CLI_KEY_CANCEL = line cancel, negative = no input. */
    int (*getkey)(void);
    void (*write)(const void *, u32);
    void (*poll)(void);
    int (*cat)(const char *path);
    int (*put)(const char *path, const void *data, u32 size);
};
void resident_cli_run(const struct resident_cli_ops *ops);

#endif
