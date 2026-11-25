/* ======================================================================== */
/*  PLATFORM_BOOT_LAYOUT.H — PC-98 の起動と低位メモリの番地                  */
/*                                                                          */
/*  リアルモードのローダの置き場・起動時のスタック・BIOS の作業域の保護範囲・ */
/*  自動プレイの mailbox。PC-98 の機械と起動の手順で決まる番地で、共通の       */
/*  ソースと別の機種の像は使わない。OS の配置 (帯・鎖・池) は sys/include/memmap.h。 */
/*                                                                          */
/*  ここも番地の正典 ([C4])。地図 (build/out/MEMMAP.md) と重なりの検査、       */
/*  生成ヘッダ memmap_layout.h の LAYOUT_ の名前は tools/gen_memmap.py が      */
/*  sys/include/memmap.h と合わせて読む。NASM の写しは boot/memmap_defs.inc         */
/*  (一致は tools/tests/test_boot_named_constants.py)。                        */
/* ======================================================================== */

#ifndef PC98_PLATFORM_BOOT_LAYOUT_H
#define PC98_PLATFORM_BOOT_LAYOUT_H

#include "memmap.h"

/* ====================================================================== */
/*  ページング保護範囲                                                      */
/* ====================================================================== */

/* IVT/BIOSデータ領域: Read-Only (ブート後に再利用されるまで) */
/* ページ0 (0x0-0xFFF) はブート後に NOT PRESENT (NULLポインタ検出) */
#define MEM_IVT_PROT_START    0x01000UL
#define MEM_IVT_PROT_END      0x05FFFUL

/* loader.bin (使用済み): Read-Only → ブート後に再利用 
 * (注: ブートローダーが 0x804E に作成した GDT は
 *  カーネル初期化時 (gdt_init) に安全な上位メモリへ退避されます) */
#define MEM_LOADER_START      0x08000UL
#define MEM_LOADER_END        0x08FFFUL

/* ====================================================================== */
/*  ローダの低位の番地                                                      */
/* ====================================================================== */

/* ブート時スタック (ローダー専用)。
 *
 * **ここは低位のままでなければならない。** ローダーは PM に入った後も
 * ディスク BIOS (INT 1Bh) を呼ぶためにリアルモードへ戻るので、SS:SP で
 * 届く 1MB 未満にスタックが要る。3 つのローダーの `mov esp, 0009FFFCh` が
 * この値。カーネルは kentry.asm で MEM_KSTACK_TOP に張り替えるため、
 * この領域はカーネルが動き出した時点で用済みになる
 * (V86 セッションはブートの遥か後なので競合しない)。 */
#define MEM_BOOT_STACK_TOP    0x9FFFCUL

/* ローダ専用の低位番地。NASM の写しへの適用は N12。地図として数値を保つ。 */
#define MEM_BOOT_RM_STACK_TOP  0x07C00UL
#define MEM_BOOT_FAT_BUF       0x06000UL
#define MEM_BOOT_COMPRESSED_BASE 0x10000UL
#define MEM_BOOT_BIOS_PARAM_BASE 0x07F00UL
#define MEM_HDD_IPL_BASE       0x1FC00UL
#define MEM_BOOT_PROBE_END     0x1000000UL /* 現行 RAM probe の上限 (排他) */

/* ====================================================================== */
/*  低位メモリの受け渡し                                                    */
/* ====================================================================== */

/* 自動プレイ mailbox: game/app の実装と game/tools/autoplay/driver.py の対。
 * 配置や大きさを変えるときは game 側も同じコミットで直す (CLAUDE.md)。 */
#define MEM_AUTOPLAY_MAILBOX_BASE 0x90000UL
#define MEM_AUTOPLAY_MAILBOX_SIZE 868UL

#endif /* PC98_PLATFORM_BOOT_LAYOUT_H */
