/* ======================================================================== */
/*  CPL0_PROBE.C — CPL=0 の子の受入用プローブ (票 TASK_T1_LEDGER §4-1、T1a)  */
/*                                                                          */
/*  `mkos32x --cpl0` で組む (build/programs.mk の明示ルール)。今のツリーに   */
/*  --cpl0 の本体バイナリは無い (`v86` は cui 宣言の CPL=3) ので、CPL=0 の   */
/*  子の claim (exec_child_claim) を実物で通すために足した試験バイナリ。     */
/*                                                                          */
/*  起動して自分のロード番地とスタックの位置を表示して終わるだけ。           */
/*  CPL=0 の子のスタックは sys_usable_mem_end() から下へ伸びる                */
/*  (exec.c: stack_top = mem_end) ので、main の ESP をページへ切り上げた     */
/*  値が sys_usable_mem_end() になる。8MB (FIXED 型) では 0x800000、          */
/*  PEGC の BB を取った構成ではその分 (300KB) 下がる。                        */
/*                                                                          */
/*  出力の 1 行目は機械で読む形 (`cpl0_probe: load=... usable_end=...`)。    */
/*  CPL は CS の下位 2 ビットで確かめる (--cpl0 が効いていなければ 3)。       */
/* ======================================================================== */

#include "os32api.h"

#define CPL0_PROBE_PAGE_MASK 0xFFFUL   /* 4KB ページの端数 (i386 のページ) */

void main(int argc, char **argv, KernelAPI *api)
{
    u32 esp, cs, usable_end;
    (void)argc; (void)argv;
    __asm__ volatile ("movl %%esp, %0" : "=r"(esp));
    __asm__ volatile ("movl %%cs, %0" : "=r"(cs));
    usable_end = (esp + CPL0_PROBE_PAGE_MASK) & ~CPL0_PROBE_PAGE_MASK;
    api->kprintf(0x07, "cpl0_probe: load=%x usable_end=%x cpl=%d mem_kb=%d\n",
                 (u32)main, usable_end, (int)(cs & 3),
                 (int)api->sys_get_mem_kb());
    if ((cs & 3) != 0)
        api->kprintf(0x41, "%s", "cpl0_probe: NOT CPL=0 (--cpl0 missing?)\n");
}
