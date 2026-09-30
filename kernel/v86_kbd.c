/* ======================================================================== */
/*  V86_KBD.C — ゲスト用の仮想 8251A (キーボード)                           */
/*  設計と経路は v86_kbd.h を参照                                           */
/* ======================================================================== */

#include "v86_kbd.h"
#include "kbd.h"

/* ------------------------------------------------------------------------ */
/*  スキャンコード FIFO                                                     */
/*                                                                          */
/*  実機のキーボードにはバッファが無く、取りこぼしはそのまま消える。        */
/*  ここも同じ扱いにして、溢れたら**古い方を残して新しい方を捨てる**。      */
/*  ゲームはキーの押しっぱなしをブレイクコードで判定するので、              */
/*  順番が入れ替わる方が「離した」を落とすより害が大きい。                  */
/* ------------------------------------------------------------------------ */
#define V86_KBD_FIFO_SZ  32

static u8  fifo[V86_KBD_FIFO_SZ];
static u32 fifo_r;
static u32 fifo_w;

/* ゲストが 0x41 から最後に読んだバイト。空の FIFO を読まれたらこれを返す
 * (実チップの 8251A もデータレジスタに前回のバイトが残る)。以前は 0x00 を
 * 返していて、それは **ESC のメイク**だった — IRQ1 の偽反射 (空 IRQ・
 * エラー) と組んで、ゲストに押していない ESC が届いた (POLICY_DEBUG §4-57)。
 * 反射は実データのときだけに絞ったので、これは二重の防御。
 * セッション開始時の初期値は V86_KBD_IDLE (未割当キー 0x7F のブレイク) —
 * 前回のバイトが無い状態で何か返すなら、押されていないキーの「離した」が
 * いちばん害が無い。 */
#define V86_KBD_IDLE  0xFF
static u8  last_data = V86_KBD_IDLE;

/* 診断用カウンタ。ホストから emu_read_mem addr=v86_kbd_n_push len=12 で
 * 3 本まとめて読めるように並べて置く (ディスク側のカウンタと同じ作法)。
 * static にすると最適化で消えるので必ず外部リンケージにする (04 §4-11)。 */
u32 v86_kbd_n_push;
u32 v86_kbd_n_read;
u32 v86_kbd_n_drop;

/* 脱出ホットキー (CTRL + GRPH + DEL) 判定用の修飾キー状態。
 * OS32 側の kbd_shift_state はセッション中、カナ・CAPS (ロック) しか更新しない
 * (drivers/kbd.c kbd_lock_apply) ので、CTRL / GRPH は自前で持つ。 */
static int mod_ctrl;
static int mod_grph;

void v86_kbd_reset(void)
{
    fifo_r = 0;
    fifo_w = 0;
    last_data = V86_KBD_IDLE;
    v86_kbd_n_push = 0;
    v86_kbd_n_read = 0;
    v86_kbd_n_drop = 0;
    mod_ctrl = 0;
    mod_grph = 0;
}

int v86_kbd_is_port(u16 port)
{
    return (port == V86_KBD_DATA || port == V86_KBD_CMD);
}

int v86_kbd_pending(void)
{
    return (fifo_r != fifo_w);
}

int v86_kbd_push(u8 scancode)
{
    u32 next;
    u8  key = (u8)(scancode & SCANCODE_KEY);
    int is_break = (scancode & SCANCODE_BREAK) ? 1 : 0;

    if (key == KEY_CTRL) {
        mod_ctrl = !is_break;
    } else if (key == KEY_GRPH) {
        mod_grph = !is_break;
    }

    /* 脱出ホットキー。ゲストには渡さない。
     * タイムアウトと #GP ウォッチドッグに加えて、人間が明示的に抜ける口を
     * 1 本用意しておく。キーがゲストに全部流れる以上これが無いと戻れない。
     *
     * 主は **CTRL + STOP**。PC-98 で「止める」といえばこれで、2 キーなので
     * ゲームを操作しながらでも確実に押せる。3 キーの CTRL+GRPH+DEL は
     * 咄嗟に押しにくかったので、互換のために残すだけの位置づけにする。 */
    if (!is_break && mod_ctrl &&
        (key == KEY_STOP || (key == KEY_DEL && mod_grph))) {
        return 1;
    }

    next = (fifo_w + 1) % V86_KBD_FIFO_SZ;
    if (next == fifo_r) {
        v86_kbd_n_drop++;               /* 満杯: 新しい方を捨てる */
        return 0;
    }
    fifo[fifo_w] = scancode;
    fifo_w = next;
    v86_kbd_n_push++;
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  8251A のレジスタ                                                        */
/*                                                                          */
/*  0x41 リード = 受信データ。読むと 1 バイト消える。                       */
/*  0x43 リード = ステータス。ゲストは RxRDY (bit1) を見てポーリングする     */
/*                こともあるので、FIFO の有無をそのまま反映させる。         */
/*                TxRDY (bit0) と TxEMPTY (bit2) は常に立てておく           */
/*                — ゲストがキーボードへコマンドを送るとき待たされない。    */
/*  0x43 ライト = モード/コマンド。LED やリピート速度の設定なので捨てる。   */
/*                実チップに転送しない: OS32 が使っている設定を壊すため。   */
/* ------------------------------------------------------------------------ */
#define ST_TXRDY    0x01
#define ST_RXRDY    0x02
#define ST_TXEMPTY  0x04

u32 v86_kbd_in(u16 port)
{
    u8 v;

    if (port == V86_KBD_DATA) {
        if (fifo_r == fifo_w) {
            return last_data;   /* 空読み。実機と同じく前回のバイト */
        }
        v = fifo[fifo_r];
        fifo_r = (fifo_r + 1) % V86_KBD_FIFO_SZ;
        last_data = v;
        v86_kbd_n_read++;
        return v;
    }

    /* 0x43: ステータス */
    v = ST_TXRDY | ST_TXEMPTY;
    if (fifo_r != fifo_w) {
        v |= ST_RXRDY;
    }
    return v;
}

void v86_kbd_out(u16 port, u32 value)
{
    (void)port;
    (void)value;
}
