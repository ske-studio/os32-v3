/* ======================================================================== */
/*  KBD_STATUS.C — キーボード 8251 のステータス判定 (純粋部)                */
/*                                                                          */
/*  0043h READ のビット (docs/hw/undocumented/io_kb.md):                    */
/*    bit5 FE / bit4 OE / bit3 PE / bit1 RxRDY                              */
/*  判定の順序は RxRDY が先。受信データが無いのに 0041h を読んでも、得るの */
/*  は前回のバイトか不定値で、打鍵ではない (エラービットも見ない)。         */
/*                                                                          */
/*  PE / FE はそのバイトが化けている → 読み捨て (ERROR)。OE だけなら前の    */
/*  バイトを取りこぼしただけで、レジスタのバイトは正しい → 使う (OVERRUN)。*/
/*                                                                          */
/*  NP21/W の keyboard_i43 は `status | 0x85` を返し、IRQ1 を上げる前に     */
/*  RxRDY (status bit1) を立てる。エラービットは自前のバッファが溢れた      */
/*  ときの OE だけなので、通常の打鍵ではここは必ず KBD_ST_DATA になる。     */
/* ======================================================================== */

#include "kbd_status.h"

int kbd_status_classify(unsigned char st)
{
    if (!(st & KBD_STAT_RXRDY)) {
        return KBD_ST_EMPTY;
    }
    if (st & KBD_STAT_BADBYTE) {
        return KBD_ST_ERROR;
    }
    if (st & KBD_STAT_OE) {
        return KBD_ST_OVERRUN;
    }
    return KBD_ST_DATA;
}

int kbd_status_reflects(int kind)
{
    return (kind == KBD_ST_DATA || kind == KBD_ST_OVERRUN);
}
