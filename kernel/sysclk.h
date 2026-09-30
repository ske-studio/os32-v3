/* ======================================================================== */
/*  SYSCLK.H — 機械のシステムクロック (1.9968MHz / 2.4576MHz) の判定        */
/*                                                                          */
/*  PC-98 の 8253 TCU に入るクロックは機種で 2 通りあり、BIOS ワークエリア  */
/*  `0000:0501h` の bit7 で分かる (1 = 8MHz 系 → 1.9968MHz / 0 = 5･10MHz 系 */
/*  → 2.4576MHz)。**PIT のリロード値もシリアルの分周もこれに従う**。         */
/*                                                                          */
/*  直す前は `kernel/idt.c` が 1.9968MHz 決め打ちで分周していたので、        */
/*  2.4576MHz 系 (実機 PC-9821Ra266) では 100Hz のつもりの tick が 123Hz     */
/*  (8.125ms) になり、tick を使う待ち・番犬・CPU 校正が**すべて 23% 速い**   */
/*  状態だった。NP21/W は 1.9968MHz 設定なので**エミュレータでは踏めない**。 */
/*    経緯: docs/POLICY_DEBUG.md §4-54                                      */
/*    票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-0                           */
/*                                                                          */
/*  `sysclk_detect()` は **`paging_init()` より前** (PG=0、物理番地がその     */
/*  まま見える) に `kernel.c` から 1 回だけ呼ぶ。以後は保存値だけを読む      */
/*  ので、CPL=3 のアプリ文脈から呼ばれる経路 (KAPI 越しの `serial_init`)     */
/*  でも低位物理を触らない。                                                */
/* ======================================================================== */

#ifndef __SYSCLK_H
#define __SYSCLK_H

#include "types.h"

/* BIOS ワークエリアを 1 回だけ読んで保存する。2 回目以降は何もしない。
 * **paging_init より前に呼ぶこと** (PG=0 のあいだだけが安全)。 */
void sysclk_detect(void);

/* 保存したクロック [Hz] (SYSCLK_1997 / SYSCLK_2458)。
 * まだ判定していなければ従来の既定 SYSCLK_1997 を返す。 */
unsigned long sysclk_hz(void);

/* 8MHz 系 (0501h bit7 = 1) なら 1。未判定は 0。 */
int sysclk_is_8mhz(void);

/* 判定済みなら 1。未判定なら 0 (= sysclk_hz() は既定値)。 */
int sysclk_detected(void);

#endif /* __SYSCLK_H */
