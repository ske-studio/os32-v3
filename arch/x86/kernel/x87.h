#ifndef OS32_X87_H
#define OS32_X87_H

/* x87 の FNSAVE 域 (108 バイト) は include/exec_arch.h の struct arch_fp_context。
 * 保存・復元・初期化は arch_fp_save / arch_fp_restore / arch_fp_init (実体は x87.c)。 */
#include "exec_arch.h"

void x87_probe(void);

#endif
