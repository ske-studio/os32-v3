/* 診断の出力先 (LAN のログ + 画面)。console_log.c が持ち、log_printf.c が使う。 */
#ifndef PM_CONSOLE_LOG_H
#define PM_CONSOLE_LOG_H

#include "types.h"

void pm_diag_write(const char *bytes, u32 n, u8 color);
void pm_diag_print(const char *s, u8 color);
void pm_diag_print_utf8(const char *s, u8 color);

#endif /* PM_CONSOLE_LOG_H */
