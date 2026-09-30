/* ======================================================================== */
/*  KBD_STATUS.H — キーボード 8251 のステータス判定 (純粋部)                */
/*                                                                          */
/*  I/O を持たない。drivers/kbd.c の IRQ1 ハンドラが 0043h を読んだ値を     */
/*  渡し、0041h を読むか・使うかをここで決める。ホスト試験                  */
/*  (tools/tests/test_kbd_status.py) が実物をそのまま #include して回す。    */
/*  記録: tools/tests/kbd_status_tdd.md、経緯: docs/POLICY_DEBUG.md §4-57   */
/* ======================================================================== */

#ifndef __KBD_STATUS_H
#define __KBD_STATUS_H

/* 0043h READ のビット (docs/hw/undocumented/io_kb.md) */
#define KBD_STAT_RXRDY   0x02   /* bit1 受信データあり (RxRDY) */
#define KBD_STAT_PE      0x08   /* bit3 パリティエラー */
#define KBD_STAT_OE      0x10   /* bit4 オーバーランエラー */
#define KBD_STAT_FE      0x20   /* bit5 フレーミングエラー */
/* 読み捨てるエラー。8251A の OE は「前のバイトを読む前に次が来た」で、
 * レジスタにある**新しい**バイトは正しいので含めない (OE は KBD_ST_OVERRUN)。
 * PE / FE はそのバイト自体が化けている。 */
#define KBD_STAT_BADBYTE (KBD_STAT_PE | KBD_STAT_FE)

/* kbd_status_classify の戻り値 */
#define KBD_ST_EMPTY  0   /* RxRDY = 0: 空 IRQ。0041h を読まない */
#define KBD_ST_DATA   1   /* RxRDY = 1、エラー無し: 0041h を読んで使う */
#define KBD_ST_ERROR  2   /* RxRDY = 1、PE/FE のどれか: 読み捨てて ER で解除 (OE が
                           * 一緒に立っていてもこちら) */
#define KBD_ST_OVERRUN 3  /* RxRDY = 1、OE だけ: 前のバイトを取りこぼしたが、
                           * 0041h のバイトは正しい。使って、ER で OE を解除する */

int kbd_status_classify(unsigned char st);

/* 分類の結果、IRQ1 を V86 ゲストへ反射 (INT 09h) してよいか。
 * **0041h から実データを読んだとき (DATA / OVERRUN) だけ** 1。
 * EMPTY / ERROR で反射すると、ゲストは空の仮想 FIFO から 1 バイト読み、
 * 以前はそれが 0x00 = ESC のメイクに化けていた (kernel/isr_stub.asm の
 * irq_stub_1 が戻り値を見る)。 */
int kbd_status_reflects(int kind);

#endif /* __KBD_STATUS_H */
