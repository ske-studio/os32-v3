#ifndef POWERMAC_CLI_BOOT_H
#define POWERMAC_CLI_BOOT_H

#include "types.h"

void pm_cli_boot(void);
/* 常駐 CLI の出力 (console_stream.c)。 */
void pm_cli_write(const void *buf, u32 size);

#endif
