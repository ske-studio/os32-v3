#ifndef BOOT_DISPLAY_H
#define BOOT_DISPLAY_H
#include "types.h"
/* Preserve the existing status helpers' scratch capacities. */
#define BOOT_STATUS_NUM_BUFSIZE 16
#define BOOT_BYTE_HEX_BUFSIZE 8
void tvram_print(int x, int y, const char *str, u8 color);
char *tv_cat(char *dst, const char *src);
char *tv_cat_hex2(char *dst, u8 val);
char *tv_cat_dec(char *dst, int val);
#endif
