#ifndef OS32_UTF8_INTERNAL_H
#define OS32_UTF8_INTERNAL_H
#include "types.h"
/* Private to kernel/SDK implementation; never copied to SDK headers. */
#ifdef __KERNEL_BUILD__
int utf8_validate_jis_table(void);
int utf8_jis_table_ready(void);
#else
void utf8_set_jis_table(const u8 *table);
#endif
#endif
