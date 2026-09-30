/* ======================================================================== */
/*  KSELFTEST.H — カーネル内プリミティブの自己診断                          */
/* ======================================================================== */

#ifndef __KSELFTEST_H
#define __KSELFTEST_H

/* カーネルが実際に使うプリミティブ (kstring_asm / kmalloc / kprintf) の
 * 境界ケースを実機上で検証する。ブート時に 1 回呼ぶ。
 * 戻り値: 失敗した項目数 (0 = 全て通過)。 */
int kselftest_run(void);

/* exec_init() の **後** でしか踏めない項目 (KAPI トランポリンページの
 * 写し場の番地とページ属性。票 T9 §12 R1)。kselftest_run() はローダー初期化
 * より前に走るので、ここだけ kernel.c が exec_init() の直後に呼ぶ。
 * 合否は同じ kselftest_pass / kselftest_fail に積む (PM が emu_read_mem で
 * 読むのはその 2 つなので、後から増えても「全部通ったか」の意味は変わらない)。
 * 戻り値: 失敗した項目数 (0 = 全て通過)。 */
int kselftest_run_post_exec(void);

#endif /* __KSELFTEST_H */
