#ifndef PLATFORM_PIC_H
#define PLATFORM_PIC_H
/* 割り込みコントローラの配線と初期化の値 (契約の固定名。arch の割り込みの入口と PIC の自己試験が読む)。 */

/* ======================================================================== */
/*  PIC (8259A) — PC-98固有ポートアドレス                                   */
/*  出典: FreeBSD sys/x86/isa/atpic.c, PC9800Bible §1-4                    */
/* ======================================================================== */

/* マスタPIC (IO_ICU1 = 0x00) */
/* PIC1_CMD / PIC1_DATA are defined in pc98.h. */

/* スレーブPIC (IO_ICU2 = 0x08) */
/* PIC2_CMD / PIC2_DATA are defined in pc98.h. */

/* PC-98: カスケードはIR7 (PC/ATはIR2) */
#define ICU_SLAVEID  7

/* ICW1 */
#define ICW1_INIT    0x11  /* IC4=1, カスケード, エッジトリガ */

/* ICW3 */
#define ICW3_MASTER  0x80  /* IR7にスレーブ接続 (1<<7) */
#define ICW3_SLAVE   0x07  /* マスタのIR7に接続 (ID=7) */

/* ICW4 — PC-98固有 (FreeBSD実装より) */
/*   PC-98: バッファモード + SFNM, Auto EOI不可 */
#define ICW4_MASTER  0x1D  /* SFNM=1, BUF=1, M/S=1, AEOI=0, 8086=1 */
#define ICW4_SLAVE   0x09  /* SFNM=0, BUF=1, M/S=0, AEOI=0, 8086=1 */

/* OCW2 (EOI) */
#define OCW2_EOI     0x20  /* 非特定EOI */

/* OCW3 */
#define OCW3_IRR     0x0A  /* IRR読み出し指定 */
#define OCW3_ISR     0x0B  /* ISR読み出し指定 */

/* 再マッピング後のベクタ番号 */
#define PIC_MASTER_OFFSET  0x20   /* IRQ0-7 → INT 0x20-0x27 */
#define PIC_SLAVE_OFFSET   0x28   /* IRQ8-15 → INT 0x28-0x2F */

/* IRQ番号 → INTベクタ */
#define IRQ_TO_INT(irq)  ((irq) < PIC_IRQS ? PIC_MASTER_OFFSET + (irq) \
                                     : PIC_SLAVE_OFFSET + (irq) - PIC_IRQS)

#endif
