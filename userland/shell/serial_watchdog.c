/* ======================================================================== */
/*  SERIAL_WATCHDOG.C — 速度を上げたあと会話が続いているかの判定            */
/*                                                                          */
/*  ここには副作用のある行を 1 つも置かない (KAPI も呼ばない)。切り出して   */
/*  あるのは、**実機でしか起きない失敗**をホストで試験するため。            */
/*    試験: tools/tests/test_serial_vfast.py (ケース watchdog)              */
/*    記録: tools/tests/serial_vfast_tdd.md                                 */
/* ======================================================================== */

#include "serial_watchdog.h"

int serial_watchdog_decide(unsigned long elapsed_ticks, int acked)
{
    /* **ack を期限より先に見る。** 期限ちょうどに届いた ack を「無音だった」
     * と読み替えて戻すと、せっかく揃った足並みを自分で壊す。
     *
     * ⚠ 見るのは `serial ack` という **明示の合図** だけ。受信バイト数でも
     * 往復した行数でもない (往復 1〜3 の設計はそこで穴が尽きなかった)。 */
    if (acked) {
        return SER_WD_LINKED;
    }
    /* 期限を過ぎても ack が来ない = ホストは別の速度で喋っている
     * (こちらの応答が届いていない)。元へ戻すしかない。 */
    if (elapsed_ticks >= (unsigned long)SER_SWITCH_WATCHDOG_TICKS) {
        return SER_WD_REVERT;
    }
    return SER_WD_WAIT;
}

void serial_watchdog_arm(struct serial_watchdog *w, unsigned long tick,
                         unsigned long prev_mode, unsigned long prev_baud)
{
    if (!w) return;
    w->armed = 1;
    w->acked = 0;
    w->start_tick = tick;
    w->prev_mode = prev_mode;
    w->prev_baud = prev_baud;
}

void serial_watchdog_ack(struct serial_watchdog *w)
{
    /* **仕掛かっていないときは印を立てない。** 立てると、次の切替で arm する
     * 前の古い ack が残って即 LINKED になる (arm が 0 に戻すので実害は無いが、
     * 意味の無い代入はしない)。`serial ack` の応答自体は呼び手が冪等に返す。 */
    if (!w || !w->armed) return;
    w->acked = 1;
}

int serial_watchdog_poll(struct serial_watchdog *w, unsigned long tick)
{
    int d;

    if (!w || !w->armed) return SER_WD_WAIT;
    d = serial_watchdog_decide(tick - w->start_tick, w->acked);
    if (d == SER_WD_WAIT) return SER_WD_WAIT;
    /* 答えが出たら下ろす。**2 度は返らない** — REVERT を 2 回返すと
     * 戻したあとにもう一度 serial_init を呼んでしまう。 */
    w->armed = 0;
    return d;
}

int serial_watchdog_leave(struct serial_watchdog *w)
{
    if (!w || !w->armed) return SER_WD_WAIT;

    /* **抜けたあとは poll する者が居ない** (往復 4 B4)。番犬が仕掛かったまま
     * 忘れられると、確認の取れていない速度のまま会話が死ぬ。期限を待たずに
     * その場で戻す — 待っても誰も見に来ないのだから、待つ意味が無い。 */
    w->armed = 0;
    if (w->acked) {
        return SER_WD_WAIT;   /* 確認済み = そのままでよい */
    }
    return SER_WD_REVERT;
}

int rsh_esc_classify(int ch, int from_serial, int at_line_start, int followed)
{
    if (ch != 0x1B) return RSH_ESC_NONE;
    if (!from_serial) return RSH_ESC_EXIT;          /* 本体の ESC */
    if (at_line_start && !followed) return RSH_ESC_EXIT;
    return RSH_ESC_JUNK;
}

static int rsh_is_space(char c)
{
    return c == ' ' || c == '\t';
}

const char *rsh_sfs_child(const char *line)
{
    const char *p = line;

    if (!p) return (const char *)0;
    while (rsh_is_space(*p)) p++;
    if (p[0] != 's' || p[1] != 'f' || p[2] != 's' || !rsh_is_space(p[3]))
        return (const char *)0;
    p += 3;
    while (rsh_is_space(*p)) p++;
    if (p[0] != 'r' || p[1] != 'u' || p[2] != 'n' || !rsh_is_space(p[3]))
        return (const char *)0;
    p += 3;
    while (rsh_is_space(*p)) p++;
    if (*p == '\0') return (const char *)0;
    return p;
}

void rsh_line_begin(struct rsh_line *l, char *buf, int cap)
{
    l->buf = buf;
    l->cap = cap;
    l->pos = 0;
    l->overflow = 0;
    l->junk = 0;
    l->local = 0;
    l->bytes = 0;
    if (cap > 0) buf[0] = '\0';
}

int rsh_line_feed(struct rsh_line *l, int ch, int from_serial, int at_start,
                  int followed)
{
    int cls;

    if (ch == '\n' || ch == '\r') return RSH_LINE_DONE;
    cls = rsh_esc_classify(ch, from_serial, at_start, followed);
    /* 閉じる ESC は数えない (行頭の単独 ESC に EOT を返さないため) */
    if (cls == RSH_ESC_EXIT) return RSH_LINE_EXIT;
    l->bytes++;
    if (!from_serial) l->local = 1;
    if (cls == RSH_ESC_JUNK) {
        l->junk = 1;
        return RSH_LINE_MORE;
    }
    if (l->pos >= l->cap - 2) {
        l->overflow = 1;
    } else {
        l->buf[l->pos++] = (char)ch;
        l->buf[l->pos] = '\0';
    }
    return RSH_LINE_MORE;
}

int rsh_line_idle(const struct rsh_line *l)
{
    if (!l->junk) return RSH_LINE_DONE;
    /* 拒否した行は沈黙では閉じない。行末 (\n / \r) が来るまで待つ */
    return RSH_LINE_MORE;
}

int rsh_line_rest(struct rsh_line *l, int r, int ch, int fs,
                  struct serial_watchdog *w, const struct rsh_io *io)
{
    while (r == RSH_LINE_MORE) {
        /* **拒否した行を待つ間も番犬を見る** (Codex P2)。バイトが来続けても
         * 見るよう、空回りの後だけでなく毎周見る。拒否していない行は短い
         * 空回りで終わり、行頭の待ちが番犬を見るのでここでは触らない
         * (ack の行を受けている途中で期限を切らない)。 */
        if (l->junk && w &&
            serial_watchdog_poll(w, io->tick(io->ctx)) == SER_WD_REVERT) {
            /* 速度が変わった — 受けたバイト列には意味が無いので捨てる */
            rsh_line_begin(l, l->buf, l->cap);
            return RSH_LINE_REVERT;
        }
        if (ch < 0) {
            int t = 0;
            while (t < RSH_REST_SPIN) {
                ch = io->getch(io->ctx, &fs);
                if (ch >= 0) break;
                t++;
            }
        }
        if (ch < 0) {
            r = rsh_line_idle(l);
            if (r == RSH_LINE_MORE) io->idle(io->ctx);
            continue;
        }
        r = rsh_line_feed(l, ch, fs, 0, 1);
        ch = -1;
    }
    return r;
}
