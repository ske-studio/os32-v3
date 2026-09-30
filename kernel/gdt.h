/* ======================================================================== */
/*  GDT.H — グローバルディスクリプタテーブルのセレクタ定数と API            */
/*                                                                          */
/*  セレクタ値は CONTRACTS.md C1 で凍結されている。**勝手に変えないこと** —  */
/*  値を変えると他レーン (トランポリン/exec の iret フレーム) が黙って壊れる。*/
/* ======================================================================== */

#ifndef __GDT_H
#define __GDT_H

#include "types.h"

/* GDT エントリ数とインデックス (CONTRACTS C1)。
 * TSS は idx3 固定 — user code/data は TSS の後ろ (idx4/5) に足す。
 * 末尾に足すと TSS 番地が動き、tss.c / V86 復帰が壊れる。 */
#define GDT_ENTRIES         6
#define GDT_NULL_IDX        0
#define GDT_KERNEL_CS_IDX   1
#define GDT_KERNEL_DS_IDX   2
#define GDT_TSS_IDX         3
#define GDT_USER_CS_IDX     4
#define GDT_USER_DS_IDX     5

/* セレクタ値 (= idx<<3 | RPL)。カーネルは RPL=0、ユーザは RPL=3。 */
#define GDT_KERNEL_CS       0x08    /* idx1, DPL=0  (= idt.h の KERNEL_CS) */
#define GDT_KERNEL_DS       0x10    /* idx2, DPL=0 */
#define GDT_TSS_SEL         0x18    /* idx3 */
#define USER_CS             0x23    /* idx4 (0x20 | RPL3), DPL=3 */
#define USER_DS             0x2B    /* idx5 (0x28 | RPL3), DPL=3 */

/* ディスクリプタ access バイト (CONTRACTS C1) */
#define GDT_ACCESS_KCODE    0x9A    /* P=1,DPL=0,S=1, code exec/read */
#define GDT_ACCESS_KDATA    0x92    /* P=1,DPL=0,S=1, data read/write */
#define GDT_ACCESS_UCODE    0xFA    /* P=1,DPL=3,S=1, code exec/read */
#define GDT_ACCESS_UDATA    0xF2    /* P=1,DPL=3,S=1, data read/write */
#define GDT_ACCESS_TSS      0x89    /* P=1,DPL=0,S=0, 32bit TSS (avail) */

/* 粒度: 4KB 粒度 + 32bit + リミット上位。フラット 4GB。 */
#define GDT_GRAN_FLAT       0xCF

/* ======== API ======== */
void gdt_init(void);
void gdt_set_tss(u32 base, u32 limit);

#endif /* __GDT_H */
