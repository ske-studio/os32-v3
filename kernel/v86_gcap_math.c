/* ======================================================================== */
/*  V86_GCAP_MATH.C — `v86 -g` の記録器と引数の判定 (ハードに触らない部分)   */
/*                                                                          */
/*  説明は v86_gcap_math.h。ホスト試験が実物のまま #include するので、       */
/*  カーネルのヘッダ (io.h 等) には依存しない。                              */
/* ======================================================================== */

#include "v86_gcap_math.h"

void v86g_reset(V86Gcap *g)
{
    unsigned char *p = (unsigned char *)g;
    unsigned int i;
    for (i = 0; i < (unsigned int)sizeof(*g); i++) {
        p[i] = 0;
    }
}

/* ------------------------------------------------------------------------ */
/*  実機へ通すポート                                                        */
/*                                                                          */
/*  票 §3 段 1 の表の 09A8h・6Ah・68h・60h/62h・A0h/A2h・A4h/A6h に、       */
/*    09A0h  — PEGC のモード読み戻し (選択番号を書いて同じ番地を読む)。      */
/*             ROM が今のモードを見て分岐するなら、通さないと 0xFF の偽の値で */
/*             別の道を通る                                                 */
/*    64h・6Ch・6Eh — テキスト GDC 帯の残り (通常の V86 でも素通し)          */
/*    70h〜7Ah の偶数 — テキスト CRTC (行の高さ・ラスタ)。480 ラインで何を   */
/*             入れるかは H5 (1 行の読み出し幅) の手掛かり                   */
/*  を足した。どれも通常の V86 の許可リスト (v86_io.c) で素通しにしている     */
/*  ポートか 09A0h/09A8h なので、通すこと自体は通常の V86 と変わらない。      */
/*  違うのは「捕まえて記録してから通す」ことだけ。                           */
/* ------------------------------------------------------------------------ */
int v86g_port_passed(unsigned int port)
{
    if (port == 0x09A8U || port == 0x09A0U) {
        return 1;
    }
    if (port >= 0x0060U && port <= 0x007AU && (port & 1U) == 0) {
        return 1;               /* テキスト GDC / モード F/F / CRTC */
    }
    if (port == 0x00A0U || port == 0x00A2U ||
        port == 0x00A4U || port == 0x00A6U) {
        return 1;               /* グラフィック GDC / 表示・描画ページ */
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  打ち切る命令                                                            */
/*                                                                          */
/*  INS/OUTS は #GP ハンドラが未対応 (v86.c の default)。66h 付きの          */
/*  IN EAX / OUT EAX は今のハンドラが幅 2 で処理してしまう。どちらも黙って   */
/*  別の幅で通すと記録が嘘になるので、採取を打ち切って失敗を出す。          */
/* ------------------------------------------------------------------------ */
int v86g_insn_check(int opsize16, unsigned int opcode)
{
    if (opcode >= 0x6CU && opcode <= 0x6FU) {
        return V86G_ABORT_INSOUTS;
    }
    if (!opsize16 && (opcode == 0xE5U || opcode == 0xE7U ||
                      opcode == 0xEDU || opcode == 0xEFU)) {
        return V86G_ABORT_IO32;
    }
    return V86G_ABORT_NONE;
}

static unsigned int width_mask(int size)
{
    return (size == 2) ? 0xFFFFU : 0xFFU;
}

void v86g_note_in(V86Gcap *g, unsigned int port, int size, unsigned int value,
                  int passed)
{
    unsigned int i;
    V86GcapIn *e;

    value &= width_mask(size);
    g->in_total++;
    g->seq_next++;

    for (i = 0; i < g->n_in; i++) {
        e = &g->in[i];
        if (e->port == (u16)port &&
            ((e->flags & V86G_F_PASSED) != 0) == (passed != 0)) {
            e->count++;
            e->widths = (u8)(e->widths | (u8)size);
            e->last = (u16)value;
            return;
        }
    }
    if (g->n_in >= V86G_IN_MAX) {
        g->in_other++;
        return;
    }
    e = &g->in[g->n_in];
    e->port = (u16)port;
    e->widths = (u8)size;
    e->flags = (u8)(passed ? V86G_F_PASSED : 0);
    e->count = 1;
    e->first = (u16)value;
    e->last = (u16)value;
    g->n_in++;
}

int v86g_note_out(V86Gcap *g, unsigned int port, int size, unsigned int value,
                  unsigned int cs, unsigned int ip, int passed,
                  unsigned int phase)
{
    V86GcapOut *e;
    u32 seq = g->seq_next;

    g->seq_next++;
    if (g->n_out >= V86G_OUT_MAX) {
        g->overflow = 1;
        return -1;
    }
    e = &g->out[g->n_out];
    e->seq = seq;
    e->port = (u16)port;
    e->value = (u16)(value & width_mask(size));
    e->cs = (u16)cs;
    e->ip = (u16)ip;
    e->width = (u8)size;
    e->flags = (u8)((passed ? V86G_F_PASSED : 0) |
                    ((phase & 0x0FU) << V86G_F_PHASE_SHIFT));
    e->reserved = 0;
    g->n_out++;
    return 0;
}

unsigned int v86g_pass_in(V86Gcap *g, const V86gIoOps *ops,
                          unsigned int port, int size)
{
    unsigned int v;

    if (size == 2) {
        v = ops->in16(port) & 0xFFFFU;
    } else {
        v = ops->in8(port) & 0xFFU;
    }
    v86g_note_in(g, port, size, v, 1);
    return v;
}

void v86g_pass_out(V86Gcap *g, const V86gIoOps *ops, unsigned int port,
                   int size, unsigned int value, unsigned int cs,
                   unsigned int ip, unsigned int phase)
{
    /* 記録してから通す。溢れても通すのはやめない — ROM のモード切り替えを
     * 途中で崩すと、戻しの AH=30h が前提にする状態が壊れる。溢れた採取は
     * 結果を出さない (V86G_ST_OVERFLOW) だけ。 */
    (void)v86g_note_out(g, port, size, value, cs, ip, 1, phase);
    if (size == 2) {
        ops->out16(port, value & 0xFFFFU);
    } else {
        ops->out8(port, value & 0xFFU);
    }
}

/* ------------------------------------------------------------------------ */
/*  AH=31h の値の並び                                                       */
/*                                                                          */
/*  bit2 並び (NP21/W bios18.c bios0x18_30 / 31al / 31bh):                   */
/*    AL = 08h | (31kHz ? 04h : 0)      (AL & F8h == 08h が必須)            */
/*    BH = 解像度 << 4 | 行数            解像度 0 = 200 LOWER / 1 = UPPER /  */
/*                                      2 = 400 / 3 = 480、                 */
/*                                      行数 0 = 20 / 1 = 25 / 2 = 30       */
/*  bit3 並び (Bible 3-2):                                                   */
/*    AL = (31kHz ? 08h : 0)                                                */
/*    BH = 行数 << 3 | 解像度 << 1       解像度 00b/01b/10b/11b は同じ順      */
/*  どちらも 30 行は 480 のときだけ、480 は 31kHz のときだけ指定できる。     */
/*                                                                          */
/*  **AL の bit0 はどちらの並びでも許し、30h へそのまま渡す** (実機 Ra266 の  */
/*  AH=31h が AX=310Dh BX=0100h を返した、2026-09-29。NP21/W は 3108h)。     */
/*  [US] memsys.md 0000:0459h bit3「INT 18h Function 30h,31h のモード設定    */
/*  情報 bit 0」= ラスタスキャンモード (BIOS とは値の意味が逆)。対象は        */
/*  PC-98GS・PC-9821 (ノーマル)・BA2 等。弾くと実機では並びが決まらない。     */
/*                                                                          */
/*  **解像度が 400 であることは決め手にしない**。NP21/W の AH=31h は解像度を */
/*  0000:0597h の bit1-0 から返し、そこは AH=42h / AH=30h を誰かが呼ぶまで   */
/*  0 (= 200 LOWER) のまま — OS32 の起動では誰も呼ばないので、画面は 400     */
/*  ラインでも BH は 00h / 01h で返る。実機の ROM も同じ作りかもしれない。   */
/*  決め手は「その並びとして正しい値か」だけにし、**ちょうど 1 つの並びで    */
/*  正しいときだけ**決める。両方で正しいのは AL=08h・BH=00h だけ (bit2 では  */
/*  200 LOWER・20 行、bit3 でも 200 LOWER・20 行) で、それは決めない。       */
/*  並びを取り違えても戻しの AH=30h は AH=31h の値をそのまま渡すので         */
/*  (ROM 自身の並び)、元のモードへの戻りは並びの判定に依らない。            */
/* ------------------------------------------------------------------------ */
#define V86G_AL_RASTER 0x01U   /* AL bit0: ラスタスキャン (上の注記) */

static int valid_mode(unsigned int res, unsigned int rows, int is31k)
{
    if (rows == 3U) return 0;
    if (rows == 2U && res != 3U) return 0;      /* 30 行は 480 だけ */
    if (res == 3U && !is31k) return 0;          /* 480 は 31kHz だけ */
    return 1;
}

static int is_bit2(unsigned int al, unsigned int bh)
{
    if ((al & ~(0x04U | V86G_AL_RASTER) & 0xFFU) != 0x08U) return 0;
    if ((bh & 0xCCU) != 0) return 0;
    return valid_mode((bh >> 4) & 3U, bh & 3U, (al & 0x04U) != 0);
}

static int is_bit3(unsigned int al, unsigned int bh)
{
    if ((al & ~(0x08U | V86G_AL_RASTER) & 0xFFU) != 0) return 0;
    if ((bh & 0xE1U) != 0) return 0;
    return valid_mode((bh >> 1) & 3U, (bh >> 3) & 3U, (al & 0x08U) != 0);
}

int v86g_decide(unsigned int al, unsigned int bh,
                unsigned int *al480, unsigned int *bh480)
{
    int b2, b3;

    al &= 0xFFU;
    bh &= 0xFFU;
    *al480 = 0;
    *bh480 = 0;
    if (al == V86G_SENTINEL_AL && bh == V86G_SENTINEL_BH) {
        return V86G_DEC_NO31;
    }
    b2 = is_bit2(al, bh);
    b3 = is_bit3(al, bh);
    if (b2 && !b3) {
        /* 31kHz / 640x480 / 30 行 (OS32 の PEGC と同じ 30 行) */
        *al480 = 0x08U | 0x04U | (al & V86G_AL_RASTER);
        *bh480 = (3U << 4) | 2U;
        return V86G_LAYOUT_BIT2;
    }
    if (b3 && !b2) {
        *al480 = 0x08U | (al & V86G_AL_RASTER);
        *bh480 = (2U << 3) | (3U << 1);
        return V86G_LAYOUT_BIT3;
    }
    return V86G_LAYOUT_NONE;
}

int v86g_need_restore(int set_ran, unsigned int set_ah)
{
    if (!set_ran) return 1;
    return ((set_ah & 0xFFU) == 0x05U) ? 1 : 0;
}

unsigned int v86g_restore_kind(int rst_ran, unsigned int rst_ah)
{
    if (rst_ran && (rst_ah & 0xFFU) == 0x05U) return V86G_RST_ROM;
    return V86G_RST_FALLBACK;
}

int v86g_mode_is_31k(int layout, unsigned int al)
{
    if (layout == V86G_LAYOUT_BIT2) return (al & 0x04U) ? 1 : 0;
    if (layout == V86G_LAYOUT_BIT3) return (al & 0x08U) ? 1 : 0;
    return 0;
}
