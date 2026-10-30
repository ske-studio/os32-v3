/* ======================================================================== */
/*  KBD_DLOG.C — キーボードの受信記録リング (純粋部、KAPI v67 kbd_diag_log)  */
/*                                                                          */
/*  票 docs/tasks/gui/TASK_KBD_NAV.md §3: 実機でカナ / CAPS が「ロックで     */
/*  make、解除で break」か「押すたびに make だけ」かを見るための記録。       */
/*  1 件 = 通し番号 seq・0041h の生の値・処理後の修飾・フラグ。              */
/*                                                                          */
/*  seq が「位置」を兼ねる: seq s のエントリは ent[(s - 1) % CAP] にある。    */
/*  保持しているのは (last_seq - CAP, last_seq] の範囲 (1 未満は切る)。       */
/*  読み手は「最後に読んだ seq」を渡すだけで、飛んだ分は seq の飛びで分かる。*/
/*                                                                          */
/*  排他は呼び手の責任 (IRQ1 の中の push は IF=0、copy は irq_save の間)。   */
/*  ホスト試験は tools/tests/test_kbd_dlog.py (記録 kbd_dlog_tdd.md)。       */
/* ======================================================================== */

#include "kbd_dlog.h"

void kbd_dlog_reset(KbdDlog *l)
{
    int i;
    for (i = 0; i < KBD_DLOG_CAP; i++) {
        l->ent[i].seq = 0;
        l->ent[i].code = 0;
        l->ent[i].mods = 0;
        l->ent[i].flags = 0;
        l->ent[i].reserved = 0;
    }
    l->last_seq = 0;
}

void kbd_dlog_push(KbdDlog *l, u8 code, u8 mods, u8 flags)
{
    KbdDiagLogEnt *e;
    u32 seq = l->last_seq + 1;

    e = &l->ent[(seq - 1) % KBD_DLOG_CAP];
    e->seq = seq;
    e->code = code;
    e->mods = mods;
    e->flags = flags;
    e->reserved = 0;
    l->last_seq = seq;
}

int kbd_dlog_copy(const KbdDlog *l, u32 after_seq, KbdDiagLogEnt *out, int max)
{
    u32 last = l->last_seq;
    u32 oldest;
    u32 s;
    int n = 0;

    if (max <= 0 || after_seq >= last) {
        return 0;
    }
    /* 保持している最古の seq (上書きで消えた分はここより前) */
    oldest = (last > (u32)KBD_DLOG_CAP) ? last - (u32)KBD_DLOG_CAP + 1 : 1;
    s = after_seq + 1;
    if (s < oldest) {
        s = oldest;
    }
    while (s <= last && n < max) {
        out[n] = l->ent[(s - 1) % KBD_DLOG_CAP];
        n++;
        s++;
    }
    return n;
}
