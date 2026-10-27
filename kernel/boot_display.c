/* Shared boot status rendering, moved without changing layout. */
#include "boot_display.h"
#include "platform_emergency.h"
#include "kprintf.h"

void tvram_print(int x, int y, const char *str, u8 color)
{
    platform_boot_puts(x, y, str, color);
}

/* 文字列を連結して終端の次を返す (状態行を組み立てる小道具)。
 * [C2] libc は使わない。kstrncat は毎回長さを数え直すので、
 * 組み立てのあいだは末尾の位置を持ち回る。 */
char *tv_cat(char *dst, const char *src)
{
    while (*src) { *dst = *src; dst++; src++; }
    *dst = '\0';
    return dst;
}

/* 1 バイトを 2 桁の 16 進 (小文字) で連結する。kutoa_hex は桁を詰めるので
 * (0x0F → "f")、足りない分だけ '0' を先に置く。 */
char *tv_cat_hex2(char *dst, u8 val)
{
    char tmp[BOOT_BYTE_HEX_BUFSIZE];
    int len;

    len = kutoa_hex((u32)val, tmp, (int)sizeof(tmp), 0);
    if (len < 2) { *dst = '0'; dst++; *dst = '\0'; }
    return tv_cat(dst, tmp);
}

/* 符号付き 10 進を連結する。 */
char *tv_cat_dec(char *dst, int val)
{
    char tmp[BOOT_STATUS_NUM_BUFSIZE];

    if (val < 0) {
        *dst = '-'; dst++; *dst = '\0';
        /* 符号反転は u32 で行う — int で -INT_MIN を作ると未定義。 */
        kutoa_dec(0u - (u32)val, tmp, (int)sizeof(tmp));
    } else {
        kutoa_dec((u32)val, tmp, (int)sizeof(tmp));
    }
    return tv_cat(dst, tmp);
}

