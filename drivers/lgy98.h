/* ======================================================================== */
/*  LGY98.H — メルコ LGY-98 (NE2000 互換 C バス LAN) カード固有定義           */
/*                                                                          */
/*  制御方式は DP8390 (drivers/ne2000_regs.h) と同じだが、ポート配置は        */
/*  PC/AT の NE2000 と違う。計画: docs/tasks/network/PLAN.md §2 / §8。       */
/*  出典: NP21/W LGY-98 解析資料 (simk98)、ローカル ai-debug fork の         */
/*  src/network/lgy98.c (lgy98_bind)。実カードでの確定は M0 の表に従う。     */
/* ======================================================================== */

#ifndef LGY98_H
#define LGY98_H

#include "types.h"

/* ---- BASE からのオフセット ---- */
#define LGY98_OFS_REGS      0x000   /* DP8390 レジスタ 0x00〜0x0F (8bit) */
#define LGY98_OFS_RESET     0x018   /* 読み出しでカードリセット (書き込みは無視) */
#define LGY98_OFS_DATA      0x200   /* Remote DMA データポート (16bit)。+0x201 は触らない */
#define LGY98_OFS_CFG       0x300   /* 独自設定 (EEPROM 相当) 0x300〜0x30F。初版は未使用 */

/* ---- BASE の候補: 0x00D0 + n × 0x1000 (n = 0..7) ---- */
#define LGY98_BASE_LOW      0x00D0
#define LGY98_BASE_STEP     0x1000
#define LGY98_BASE_HIGH     0x70D0
#define LGY98_BASE_LOWMASK  0x0FFF  /* 下位 12bit は常に 0x0D0 */
#define LGY98_BASE_DEFAULT  0x10D0  /* NP21/W の既定値 (ユーザー環境の設定値ではない) */

/* ---- カードの INT ジャンパ → PIC IRQ ----
 * INT0 → IRQ3, INT1 → IRQ5, INT2 → IRQ6, INT5 → IRQ12。
 * 初版は IRQ 3/5/6 だけ受け付ける (IRQ12 は音源 / V86 と排他が未決)。 */
#define LGY98_INT0_IRQ      3
#define LGY98_INT1_IRQ      5
#define LGY98_INT2_IRQ      6
#define LGY98_INT5_IRQ      12

/* ---- NIC RAM ----
 * 先頭ページ 0x40 (バイトアドレス 0x4000)。容量は 16KB (実カードの想定) か
 * 32KB (NP21/W) で、初期化時の probe で決める。 */
#define LGY98_RAM_PAGE_FIRST 0x40
#define LGY98_RAM_PAGES_16K  0x40
#define LGY98_RAM_PAGES_32K  0x80

/* ---- 起動設定 (include/config.h の CONFIG_LGY98_*) に対する flags ---- */
#define LGY98_FLAG_DIAG      0x01   /* 初期化時に RAM 全域試験と Remote DMA 往復試験を行う */
#define LGY98_FLAG_LOOPBACK  0x02   /* 内部ループバックで起動 (M2 試験用) */
#define LGY98_FLAG_REFLECT   0x04   /* 反射モード: 100Hz tick で poll し、受信フレームの MAC を入れ替えて送り返す (M2 試験用) */
#define LGY98_FLAG_LINKTEST  0x08   /* attach 後にリンク層 L0 自己試験 (HELLO + PING/PONG)。Host Agent が要る */

/* 設定検証。0 = 妥当, 負 = NE2K_ERR_* */
int lgy98_validate_base(unsigned int base);
int lgy98_validate_irq(unsigned int irq);

/* 起動時入口 (kernel.c)。CONFIG_LGY98_BASE が 0 なら何もせず 0 を返す。
 * カード未装着・応答不良でも負値を返して OS 起動は続く。 */
int lgy98_init(void);

/* 明示的に接続する (base / irq / LGY98_FLAG_*)。lgy98_init が使う。 */
int lgy98_attach(unsigned int base, unsigned int irq, unsigned int flags);

/* 100Hz タイマから呼ぶ (kernel/isr_handlers.c timer_handler)。反射モードのときだけ
 * NIC を poll して受信フレームを送り返す。それ以外は即座に戻る。
 * M3 で IRQ 駆動になるまでの M2 試験経路であり、完成ドライバの受信保証ではない。 */
void lgy98_tick(void);

/* 反射した / 反射に失敗したフレーム数 (ホストから kernel.map 経由で観測する) */
extern u32 lgy98_reflected;
extern u32 lgy98_reflect_fail;

#endif /* LGY98_H */
