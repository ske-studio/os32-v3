#ifndef OS32_X87_H
#define OS32_X87_H

/* x87 の FNSAVE 域 (108 バイト) は include/exec_arch.h の struct arch_fp_context。
 * 保存・復元・初期化は arch_fp_save / arch_fp_restore / arch_fp_init (実体は x87.c)。 */
#include "exec_arch.h"

void x87_probe(void);
/* Internal boot result, valid after x87_probe; never a public KAPI. */
int arch_fp_present(void);
/* #NM only. Return 1 and replace *saved_eip only for CPL0, VM=0,
 * active boot probe and one of its three exact fault labels. Otherwise return
 * 0 without modifying *saved_eip. Active/labels stay private to x87.c. */
int x87_probe_nm(u32 saved_cs, u32 saved_eflags, u32 *saved_eip);

#endif
