#ifndef BOOT_TOKEN_H
#define BOOT_TOKEN_H
#include "types.h"
void boot_token_init(void);
void boot_token_get(u8 out[16]);
void platform_boot_token(u8 out[16]);
#endif
