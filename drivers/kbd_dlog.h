/* ======================================================================== */
/*  KBD_DLOG.H — キーボードの受信記録リング (純粋部、KAPI v67 kbd_diag_log)  */
/*                                                                          */
/*  I/O も割り込み禁止も持たない。drivers/kbd.c の IRQ1 ハンドラが           */
/*  kbd_dlog_push を呼び (IF=0 のまま、再入しない)、kbd_diag_log が          */
/*  irq_save の間に kbd_dlog_copy でローカルへ写す。ホスト試験               */
/*  (tools/tests/test_kbd_dlog.py) が実物をそのまま #include して回す。      */
/*  票: docs/tasks/gui/TASK_KBD_NAV.md §3、記録: tools/tests/kbd_dlog_tdd.md   */
/* ======================================================================== */

#ifndef __KBD_DLOG_H
#define __KBD_DLOG_H

#include "os32_kapi_shared.h"   /* KbdDiagLogEnt / KBD_DLOG_CAP */

typedef struct {
    KbdDiagLogEnt ent[KBD_DLOG_CAP];
    u32 last_seq;        /* 最後に積んだ seq (0 = まだ 1 件も無い) */
} KbdDlog;

/* 空にする (seq も 0 から) */
void kbd_dlog_reset(KbdDlog *l);

/* 1 件積む。seq は last_seq + 1。満杯なら最も古い 1 件を上書きする。
 * seq (u32) は折り返さない前提 (2^32 バイトの受信 = 押しっぱなしでも 4 年超)。 */
void kbd_dlog_push(KbdDlog *l, u8 code, u8 mods, u8 flags);

/* after_seq より新しいエントリを古い順に out へ最大 max 件写し、件数を返す。
 * 上書きで失われた分は飛ばす (写した先頭の seq > after_seq + 1 になる)。
 * after_seq >= last_seq なら 0。max <= 0 なら 0。 */
int  kbd_dlog_copy(const KbdDlog *l, u32 after_seq, KbdDiagLogEnt *out, int max);

#endif /* __KBD_DLOG_H */
