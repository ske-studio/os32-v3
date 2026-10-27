/* ======================================================================== */
/*  IDT.H — 割り込みディスクリプタテーブル / PIC / PIT 定義                 */
/*                                                                          */
/*  PC-98固有のポートアドレスを使用 (FreeBSD pc98 atpic.c 参照)             */
/* ======================================================================== */

#ifndef __IDT_H
#define __IDT_H

#define RING3_SYSCALL_VECTOR 0x80

#define EXC_DOUBLE_FAULT 8
#define EXC_PAGE_FAULT 14

#ifndef __ASSEMBLER__
/* ======== 基本型 ======== */
#include "types.h"
#include "gdt.h"
#include "pc98.h"
#include "irq_math.h"
#include "pit_math.h"   /* struct pit_setup / pit_compute */

/* ======================================================================== */
/*  IDT ゲートディスクリプタ (8バイト)                                      */
/* ======================================================================== */
struct idt_entry {
    u16 offset_low;     /* ハンドラアドレス下位16ビット */
    u16 selector;       /* コードセグメントセレクタ (0x08) */
    u8  zero;           /* 予約 (0) */
    u8  type_attr;      /* タイプ/属性 */
    u16 offset_high;    /* ハンドラアドレス上位16ビット */
} __attribute__((packed));

/* IDTR レジスタ用構造体 (パック) */
struct idt_ptr {
    u16 limit;          /* IDTサイズ - 1 (= 256*8 - 1 = 2047) */
    u32 base;           /* IDTベースアドレス */
} __attribute__((packed));

/* IDTタイプ/属性ビット */
#define IDT_ATTR_TASK_GATE     0x85  /* P=1, DPL=0, type=5 */
#define IDT_ATTR_INT_GATE32  0x8E   /* P=1, DPL=0, 32ビット割込みゲート */
#define IDT_ATTR_TRAP_GATE32 0x8F   /* P=1, DPL=0, 32ビットトラップゲート */
#define IDT_ATTR_INT_GATE32_DPL3 0xEE /* P=1, DPL=3, 32ビット割込みゲート
                                       * (CPL=3 から int 0x80 を呼ぶ用, v2 M2/C4) */

/* GDTコードセグメントセレクタ */
#define KERNEL_CS  GDT_KERNEL_CS

/* IDTエントリ数 */
#define IDT_ENTRIES  256
#define EXC_VECTOR_COUNT 32

/* PUSHAD words shared by the common exception and dedicated #PF paths. */
#define ISR_REG_EDI 0
#define ISR_REG_ESI 1
#define ISR_REG_EBP 2
#define ISR_REG_ESP 3
#define ISR_REG_EBX 4
#define ISR_REG_EDX 5
#define ISR_REG_ECX 6
#define ISR_REG_EAX 7
/* Common: vector, error, EIP, CS. Dedicated #PF: error, EIP, CS. */
#define ISR_COMMON_CS 11
#define ISR_PF_CS     10

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

/* ======================================================================== */
/*  PIT (8254) — PC-98固有ポートアドレス                                    */
/*  出典: FreeBSD sys/pc98/include/timerreg.h, PC9800Bible §2-3            */
/* ======================================================================== */

#define PIT_CNTR0    PIT_CNT0  /* カウンタ#0 (インターバルタイマ) */
#define PIT_CNTR1    PIT_CNT1_BEEP /* カウンタ#1 (スピーカー) — 特殊アドレス */
#define PIT_CNTR2    PIT_CNT2  /* カウンタ#2 (RS-232C) */
/* PIT_MODE is defined in pc98.h. */

/* システムクロックは機種で 2 通り (1.9968MHz / 2.4576MHz)。**決め打ちしない** */
/* — 判定は kernel/sysclk.c、分周の算数は kernel/pit_math.c。               */
/* 直す前はここに PIT_CLOCK = 1996800 があり、2.4576MHz 系では 100Hz のつもり */
/* の tick が 123Hz (8.125ms) になっていた (docs/POLICY_DEBUG.md §4-54)。    */

/* PITモードバイト: カウンタ#0, LSB/MSB, モード2(レートジェネレータ), バイナリ */
#define PIT_MODE_TIMER0  0x34  /* 00 11 010 0 */

/* カウンタラッチコマンド (SC=00 カウンタ#0, RW=00 = ラッチ)。
 * 走っているカウンタを止めずに現在値を凍らせ、下位 → 上位の順に読む。
 * µs 時計 (kernel/ktime.c) が使う。 */
#define PIT_LATCH_TIMER0 0x00  /* 00 00 000 0 */

/* ======================================================================== */
/*  公開API                                                                */
/* ======================================================================== */

void idt_init(void);
void pic_init(void);

/* カウンタ#0 を hz で回す。戻り 0 = 頼まれたとおり / 負 = 出せないので
 * **既定 PIT_HZ を積んだ** (タイマ無しで起動を続けない)。いまは PIT_HZ
 * 以外を受けない (票 TASK_HAL_WIRING §1-0)。 */
int pit_init(unsigned int hz);

/* kernel/pit.c が所有する、直前の pit_init が積んだ設定。valid = 0 なら一度も積んでいない。 */
const struct pit_setup *pit_get_setup(void);

void idt_register_irq(unsigned int irq, void (*stub)(void));
void irq_enable(unsigned int irq);
void irq_disable(unsigned int irq);
void pic_eoi(unsigned int irq);

/* フレームカウンタ (タイマ割り込みで更新) */
extern volatile u32 tick_count;

#endif /* !__ASSEMBLER__ */
#endif /* __IDT_H */
