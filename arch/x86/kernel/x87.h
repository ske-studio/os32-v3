#ifndef OS32_X87_H
#define OS32_X87_H

/* x87 の FNSAVE 域 (108 バイト) は include/exec_arch.h の struct arch_fp_context。
 * 保存・復元・初期化は arch_fp_save / arch_fp_restore / arch_fp_init (実体は x87.c)。 */
#include "exec_arch.h"
#include "cpu.h"

void x87_probe(void);
/* int arch_fp_present(void) は契約 include/cpu.h (起動の結果。x87_probe の後で有効)。 */
/* #NM only. Return 1 and replace *saved_eip only for CPL0, VM=0,
 * active boot probe and one of its three exact fault labels. Otherwise return
 * 0 without modifying *saved_eip. Active/labels stay private to x87.c. */
int x87_probe_nm(u32 saved_cs, u32 saved_eflags, u32 *saved_eip);

struct x87_emu_trap;
int x87_emulate_nm(struct x87_emu_trap *trap);

#endif
