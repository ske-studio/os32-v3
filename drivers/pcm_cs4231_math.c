/* ======================================================================== */
/*  PCM_CS4231_MATH.C — CS4231 再生ドライバの**純粋部** (I/O を 1 つも出さない) */
/*                                                                          */
/*  ここに在るのは「装置をどう見るか」「次に何をするか」の判定だけで、       */
/*  ポートも割り込み禁止も触らない。ホスト試験が実物のまま回せるのはこの     */
/*  ためで、NP21/W では踏めない分岐 (1 周回った観測・補充の余裕・drain の    */
/*  段階・停止列の順序) をここで固定する。                                   */
/*                                                                          */
/*  票  : docs/tasks/v3/TASK_PCM_CS4231.md §2-1 / §2-3                      */
/*  試験: tools/tests/test_pcm_cs4231.py / 記録 tools/tests/pcm_cs4231_tdd.md */
/*  [C1] C89 (GNU89)。宣言はブロック先頭、`//` は使わない。                  */
/* ======================================================================== */

#include "pcm_cs4231.h"

/* ======================================================================== */
/*  レート                                                                  */
/* ======================================================================== */
int pcm_fmt_for_rate(u32 rate, u8 *fmt)
{
    if (!fmt) return OS32_ERR_INVAL;
    if (rate == PCM_RATE_44100) { *fmt = PCM_FMT_44100; return 0; }
    if (rate == PCM_RATE_22050) { *fmt = PCM_FMT_22050; return 0; }
    return OS32_ERR_INVAL;
}

/* 半周期 (2048 frame) のミリ秒。44.1k = 46ms、22.05k = 92ms (切り捨て)。 */
u32 pcm_half_ms(u32 rate)
{
    if (rate == 0) return 0;
    return (PCM_HALF_FRAMES * PCM_MS_PER_SEC) / rate;
}

/* 番犬の期間 = 2 半周期のマイクロ秒。44.1k = 92879µs、22.05k = 185759µs。
 * 時計は長い IF=0 で**過小評価する側**なので、番犬は遅れて発火することは
 * あっても早く発火することはない (安全側。票 §2-1 の 5)。 */
u32 pcm_watchdog_us(u32 rate)
{
    if (rate == 0) return 0;
    return (PCM_HALF_FRAMES * 2U * PCM_US_PER_SEC) / rate;
}

/* ======================================================================== */
/*  close の期限 (票 往復 4 R5)                                             */
/*                                                                          */
/*  残りを写し切る半分の数 ceil(staged / 2048) + 現在の半分の残り 1 +        */
/*  データの半分の通過 1 + 0 の半分の通過 1 → (ceil + 3) 個の半分。          */
/*  それを tick に切り上げ、停止確認の 3 tick を足す。                       */
/*  staged = 0 なら 44.1k で 17 tick、22.05k で 31 tick。                    */
/* ======================================================================== */
u32 pcm_close_ticks(u32 staged_frames, u32 rate)
{
    u32 halves, num, den;

    if (rate == 0) return PCM_STOP_TICKS;
    halves = (staged_frames + PCM_HALF_FRAMES - 1U) / PCM_HALF_FRAMES;
    num = (halves + 3U) * PCM_HALF_FRAMES * PCM_MS_PER_SEC;
    den = rate * PCM_MS_PER_TICK;
    return ((num + den - 1U) / den) + PCM_STOP_TICKS;
}

/* ======================================================================== */
/*  音量 — percent を 6 ビットの減衰へ**線形に**写す                        */
/*                                                                          */
/*  100 = 0dB (減衰 0)、1 = 最大減衰 (63 × 1.5dB)、0 = D7 のミュート。       */
/*  101 以上は OS32_ERR_INVAL (出力は触らない)。                            */
/* ======================================================================== */
int pcm_vol_reg(u32 percent, u8 *reg)
{
    u32 att;

    if (!reg) return OS32_ERR_INVAL;
    if (percent > PCM_VOL_MAX) return OS32_ERR_INVAL;
    if (percent == 0) {
        *reg = (u8)(PCM_DA_MUTE | PCM_DA_ATT_MAX);
        return 0;
    }
    att = ((PCM_VOL_MAX - percent) * PCM_DA_ATT_MAX) / (PCM_VOL_MAX - 1U);
    *reg = (u8)(att & PCM_DA_ATT_MASK);
    return 0;
}

/* ======================================================================== */
/*  位置と連続性                                                            */
/* ======================================================================== */

/* 残バイト数 → リング全体の frame 番号 (0..4095)。 */
u32 pcm_pos_frames(u32 bytes_left)
{
    u32 pos;

    pos = (PCM_RING_BYTES - (bytes_left % PCM_RING_BYTES)) % PCM_RING_BYTES;
    return pos / PCM_FRAME_BYTES;
}

/* ------------------------------------------------------------------------ */
/*  連続性の判定は**位置だけ**で行う (票 往復 8)。                          */
/*                                                                          */
/*  PI は回数を持たないレベルのフラグなので、古い PI が残ったまま次の境界が  */
/*  来ても同じ 1 にしか見えない = 合流する。だから受理の判定にだけ使う。     */
/*                                                                          */
/*  **保証の範囲**: 正しいのは成功した位置取得どうしの間隔が 1 半周期        */
/*  (46.4ms @44.1k) 未満のとき。2 半周期以上の空白では 0 回と 2 回、         */
/*  1 回と 3 回が区別できない (未補充の半分を再読しても p1 >= p0 なら        */
/*  見逃す) — これは**保証外**で、救済は番犬と p1 < p0 の場合だけ。          */
/* ------------------------------------------------------------------------ */
int pcm_cont(u32 h0, u32 p0, u32 h1, u32 p1)
{
    if (h0 != h1) return PCM_CONT_SWITCH;
    if (p1 >= p0) return PCM_CONT_SAME;
    return PCM_CONT_LOST;          /* 同じ半分で戻った = 1 周 (2 境界) */
}

/* 現在位置からその半分の末尾まで REFILL_MARGIN 以上あるか。
 * 足りなければ写さない — 書いている最中に装置が追い越せば、消したはずの
 * 古い音が鳴る (既に鳴ったデータは取り消せない)。 */
int pcm_refill_ok(u32 p1)
{
    u32 end;

    end = ((p1 / PCM_HALF_FRAMES) + 1U) * PCM_HALF_FRAMES;
    return (end - p1) >= PCM_REFILL_MARGIN;
}

/* drain の段階を**1 回の観測で 1 段だけ**進める (切り替えのときだけ呼ぶ)。 */
int pcm_drain_next(int stage, u32 h1, u32 last_data_half)
{
    if (stage == PCM_DRAIN_WAIT)
        return (h1 == last_data_half) ? PCM_DRAIN_IN : PCM_DRAIN_WAIT;
    if (stage == PCM_DRAIN_IN)
        return (h1 != last_data_half) ? PCM_DRAIN_SIL : PCM_DRAIN_IN;
    return PCM_DRAIN_OUT;          /* 無音の半分から出た / もう出ている */
}

/* ======================================================================== */
/*  pcm_start の配り方                                                      */
/*                                                                          */
/*  半分 0 へ min(2048, staged)、残りがあれば半分 1 へ。**最後に正の frame を */
/*  置いた半分**が last_data_half で、drain の初期段階はそこから決まる       */
/*  (往復 6 R3: 半分 0 だけの短いストリームを「待つ」から始めると 1 周遅れる)。*/
/* ======================================================================== */
void pcm_start_plan(u32 staged, u32 *n0, u32 *n1, u32 *last_half,
                    int *drain_stage)
{
    u32 a, b;

    a = (staged < PCM_HALF_FRAMES) ? staged : PCM_HALF_FRAMES;
    b = staged - a;
    if (b > PCM_HALF_FRAMES) b = PCM_HALF_FRAMES;
    *n0 = a;
    *n1 = b;
    *last_half = (b > 0U) ? 1U : 0U;
    *drain_stage = (b > 0U) ? PCM_DRAIN_WAIT : PCM_DRAIN_IN;
}

/* ======================================================================== */
/*  ステージング (DMA しない 16KB)。frame 単位。                             */
/*                                                                          */
/*  空きは staged から出す — w == r を「空」と「満杯」の両方に使うと         */
/*  4096 frame 消費した直後の空きが 0 に見える。                            */
/* ======================================================================== */
u32 pcm_stg_free(const struct pcm_stg *s)
{
    return PCM_STG_FRAMES - s->staged;
}

u32 pcm_stg_reserve(const struct pcm_stg *s, u32 frames, u32 *off)
{
    u32 room;

    room = pcm_stg_free(s);
    if (frames > room) frames = room;
    *off = s->w;
    return frames;
}

void pcm_stg_publish(struct pcm_stg *s, u32 frames)
{
    s->w = (s->w + frames) % PCM_STG_FRAMES;
    s->staged += frames;
}

u32 pcm_stg_consume(struct pcm_stg *s, u32 frames, u32 *off)
{
    if (frames > s->staged) frames = s->staged;
    *off = s->r;
    s->r = (s->r + frames) % PCM_STG_FRAMES;
    s->staged -= frames;
    return frames;
}

/* 物理末尾を跨ぐときの 2 分割 (往復 4 R6)。n1 + n2 == n。 */
void pcm_split(u32 off, u32 n, u32 *n1, u32 *n2)
{
    u32 head;

    head = PCM_STG_FRAMES - off;
    if (n < head) head = n;
    *n1 = head;
    *n2 = n - head;
}

/* ======================================================================== */
/*  カウンタ 3 本を 1 つの u32 へ (8 / 8 / 16 ビット、飽和)                  */
/* ======================================================================== */
u32 pcm_counters_pack(u32 underruns, u32 repeats, u32 resyncs)
{
    if (underruns > PCM_CNT_U8_MAX)  underruns = PCM_CNT_U8_MAX;
    if (repeats   > PCM_CNT_U8_MAX)  repeats   = PCM_CNT_U8_MAX;
    if (resyncs   > PCM_CNT_U16_MAX) resyncs   = PCM_CNT_U16_MAX;
    return (underruns << PCM_CNT_UND_SHIFT) | (repeats << PCM_CNT_REP_SHIFT) |
           resyncs;
}

/* ======================================================================== */
/*  pcm_obs — 1 回の観測 (票 §2-1 の 1〜5)                                   */
/*                                                                          */
/*  RUNNING / DRAINING でだけ呼ぶ。装置には触らないので、呼び手が            */
/*  「位置が取れたか (have_pos)」「いまの時刻 (now)」を与える。              */
/* ======================================================================== */
static void pcm_fail(struct pcm_core *c, struct pcm_act *act)
{
    if (c->state == PCM_ST_RUNNING) {
        c->state = PCM_ST_RS_STOP;
    } else if (c->state == PCM_ST_DRAINING) {
        c->drain_failed = 1;          /* drain 失敗を記録して止める */
        c->state = PCM_ST_STOP_REQ;
    } else {
        return;
    }
    act->entered = 1;
}

void pcm_obs(struct pcm_core *c, int have_pos, u32 p1, u32 now,
             struct pcm_act *act)
{
    u32 h1, old;
    int changed, progressed;

    act->refill = 0;
    act->to_half = 0;
    act->entered = 0;
    act->pad = 0;
    act->src_off = 0;
    act->frames = 0;

    if (!have_pos) {
        /* -EAGAIN: pos / half / filled は更新せず、番犬だけ見る。 */
        c->now = now;
    } else {
        h1 = p1 / PCM_HALF_FRAMES;
        if (pcm_cont(c->half, c->pos, h1, p1) == PCM_CONT_LOST) {
            c->repeats++;
            c->now = now;
            pcm_fail(c, act);
            return;
        }
        /* **changed / old / progressed を先に確定してから**更新する
         * (往復 6 R6、往復 7 B2: 更新後の位置で数えると境界を取り逃がす)。 */
        changed = (h1 != (u32)c->half);
        old = c->half;
        progressed = (changed || p1 != c->pos);
        c->half = (u8)h1;
        c->pos = p1;
        if (progressed) c->last_progress = now;
        c->now = now;

        if (changed) {
            c->gen++;
            /* **消す前に**数える。DRAINING の末尾の 0 埋めは数えない。 */
            if (c->state == PCM_ST_RUNNING &&
                c->filled[h1] < PCM_HALF_FRAMES) {
                c->underruns++;
            }
            if (!pcm_refill_ok(p1)) {
                c->repeats++;
                pcm_fail(c, act);
                return;
            }
            act->refill = 1;
            act->to_half = (u8)old;
            act->frames = pcm_stg_consume(&c->stg, PCM_HALF_FRAMES,
                                          &act->src_off);
            c->filled[old] = act->frames;
            /* **正の frame を写したときだけ** last_data_half を動かす
             * (0 frame の補充で動かすと drain が終わらない — 往復 4 R4)。 */
            if (act->frames > 0U) {
                c->last_data_half = (u8)old;
                c->drain = PCM_DRAIN_WAIT;
            }
            c->drain = (u8)pcm_drain_next((int)c->drain, h1,
                                          c->last_data_half);
            if (c->state == PCM_ST_DRAINING &&
                c->drain == PCM_DRAIN_OUT && c->stg.staged == 0U) {
                c->state = PCM_ST_STOP_REQ;   /* drain 完了 */
                act->entered = 1;
                return;
            }
        }
    }

    /* 番犬: 位置が進んだ最後の時刻からの経過で見る (往復 6 R5)。 */
    if ((u32)(now - c->last_progress) > pcm_watchdog_us(c->rate)) {
        pcm_fail(c, act);
    }
}

/* ======================================================================== */
/*  ポート書きの列                                                          */
/*                                                                          */
/*  順序そのものが仕様なので**表で持ち**、ホスト試験が配列として突き合わせる  */
/*  (I15 → I14、MCE 付きの I9、MODE2 の後の I24、DRS を待ってからの mask、    */
/*  期限は入口の列にだけ)。表にしてあるのはカーネルの予算のためでもある —    */
/*  組み立てるコードより .rodata 3 バイト/op のほうがずっと小さい。          */
/* ======================================================================== */
#define OP(k, a, b)  { (u8)(k), (u8)(a), (u8)(b) }

/* open の (2)〜(4): 先に新しい経路を結び、BIOS の旧設定の要因を止める。
 * **detach 状態 (0F40h = 0) で装置レジスタを書かない**ので経路が先。 */
static const struct pcm_op seq_prologue[] = {
    OP(PCM_OP_DMA_MASK, 0, 0),
    OP(PCM_OP_PORT, PCM_POFS_ROUTE, PCM_ROUTE_INT41_DMA1),
    OP(PCM_OP_PORT, PCM_POFS_R2, 0),
    OP(PCM_OP_REG, PCM_I_IFACE, PCM_IFACE_CAL1),      /* PEN=0 */
    OP(PCM_OP_REG, PCM_I_PINCTL, 0)                   /* IEN=0 */
};

/* open の (8)〜(10): 初期化列 + DMA の積み + Playback Base。
 * I8 の値だけ呼び出しごとに差し替える (SEQ_INIT_FMT_AT)。 */
#define SEQ_INIT_FMT_AT  3
static const struct pcm_op seq_init[] = {
    OP(PCM_OP_WAIT_INIT, 0, 0),
    OP(PCM_OP_REG, PCM_I_MODEID, PCM_MODE2),
    OP(PCM_OP_CHK_MODE2, 0, 0),
    OP(PCM_OP_REG, PCM_R0_MCE | PCM_I_FMT, PCM_FMT_44100),         /* ← 差し替え */
    OP(PCM_OP_WAIT_INIT, 0, 0),                       /* クロックの再同期 */
    OP(PCM_OP_REG, PCM_R0_MCE | PCM_I_IFACE, PCM_IFACE_CAL1),
    OP(PCM_OP_REG, PCM_R0_MCE | PCM_I_ALTFEAT1, PCM_ALT1_DACZ),    /* DACZ=1、SPE=0 */
    OP(PCM_OP_PORT, PCM_POFS_R0, PCM_I_IFACE),
    OP(PCM_OP_WAIT_INIT, 0, 0),                       /* MCE を落とした後 */
    OP(PCM_OP_WAIT_ACI, 0, 0),
    OP(PCM_OP_REG, PCM_I_LDA, 0),
    OP(PCM_OP_REG, PCM_I_RDA, 0),
    OP(PCM_OP_REG, PCM_I_ALTSTAT, 0),
    OP(PCM_OP_REG, PCM_I_PINCTL, PCM_PINCTL_IEN),
    OP(PCM_OP_DMA_SETUP, 0, 0),
    OP(PCM_OP_REG, PCM_I_BASE_LO, PCM_BASE_LO_2048),
    OP(PCM_OP_REG, PCM_I_BASE_HI, PCM_BASE_HI_2048)
};

/* pcm_start (OPEN → RUNNING)。装置は停止中で、リングは積んだまま。 */
static const struct pcm_op seq_start[] = {
    OP(PCM_OP_RING_FILL, 0, 0),
    OP(PCM_OP_ACK_TC, 0, 0),
    OP(PCM_OP_REG, PCM_I_ALTSTAT, 0),
    OP(PCM_OP_TIMEBASE, 0, 0),
    OP(PCM_OP_DMA_UNMASK, 0, 0),
    OP(PCM_OP_REG, PCM_I_IFACE, PCM_IFACE_CAL1 | PCM_IFACE_PEN)
};

/* RS_STOP / STOP_REQ の**入口 (1 回だけ)**。期限はここでだけ設定する。 */
static const struct pcm_op seq_stop_entry[] = {
    OP(PCM_OP_REG, PCM_I_PINCTL, 0),                  /* IEN=0 */
    OP(PCM_OP_REG, PCM_I_IFACE, PCM_IFACE_CAL1),      /* PEN=0 */
    OP(PCM_OP_PORT, PCM_POFS_R2, 0),
    OP(PCM_OP_REG, PCM_I_ALTSTAT, 0),
    OP(PCM_OP_DEADLINE, 0, PCM_STOP_TICKS)
};

/* 後続の tick の確認。**DRS が落ちてから** mask する (往復 3 B6):
 * PEN=0 は発行済みの DMA 要求の最後のサンプル転送が終わってから効く。 */
static const struct pcm_op seq_stop_tail[] = {
    OP(PCM_OP_WAIT_DRS, 0, 0),
    OP(PCM_OP_DMA_MASK, 0, 0)
};

/* RS_RESTART: mask 状態で組み直してから unmask → PEN=1 → IEN=1。 */
static const struct pcm_op seq_restart[] = {
    OP(PCM_OP_DMA_MASK, 0, 0),
    OP(PCM_OP_RING_CLEAR, 0, 0),
    OP(PCM_OP_DMA_SETUP, 0, 0),
    OP(PCM_OP_REG, PCM_I_BASE_LO, PCM_BASE_LO_2048),
    OP(PCM_OP_REG, PCM_I_BASE_HI, PCM_BASE_HI_2048),
    OP(PCM_OP_RING_FILL, 0, 0),
    OP(PCM_OP_ACK_TC, 0, 0),
    OP(PCM_OP_REG, PCM_I_ALTSTAT, 0),
    OP(PCM_OP_TIMEBASE, 0, 0),
    OP(PCM_OP_DMA_UNMASK, 0, 0),
    OP(PCM_OP_REG, PCM_I_IFACE, PCM_IFACE_CAL1 | PCM_IFACE_PEN),
    OP(PCM_OP_REG, PCM_I_PINCTL, PCM_PINCTL_IEN)
};

/* reclaim の**待たない abort**。捨てるストリームなので DRS は待たない。 */
static const struct pcm_op seq_abort[] = {
    OP(PCM_OP_REG, PCM_I_PINCTL, 0),
    OP(PCM_OP_REG, PCM_I_IFACE, PCM_IFACE_CAL1),
    OP(PCM_OP_PORT, PCM_POFS_R2, 0),
    OP(PCM_OP_REG, PCM_I_ALTSTAT, 0),
    OP(PCM_OP_DMA_MASK, 0, 0)
};

#define SEQ_N(t)  ((int)(sizeof(t) / sizeof((t)[0])))

int pcm_seq_prologue(const struct pcm_op **out)
{
    *out = seq_prologue;
    return SEQ_N(seq_prologue);
}

int pcm_seq_start(const struct pcm_op **out)
{
    *out = seq_start;
    return SEQ_N(seq_start);
}

int pcm_seq_stop_entry(const struct pcm_op **out)
{
    *out = seq_stop_entry;
    return SEQ_N(seq_stop_entry);
}

int pcm_seq_stop_tail(const struct pcm_op **out)
{
    *out = seq_stop_tail;
    return SEQ_N(seq_stop_tail);
}

int pcm_seq_restart(const struct pcm_op **out)
{
    *out = seq_restart;
    return SEQ_N(seq_restart);
}

int pcm_seq_abort(const struct pcm_op **out)
{
    *out = seq_abort;
    return SEQ_N(seq_abort);
}

/* I8 の値だけ呼び出しごとに変わるので、ここだけ写して差し替える。 */
int pcm_seq_init(u8 fmt, struct pcm_op *out)
{
    int i;

    for (i = 0; i < SEQ_N(seq_init); i++) out[i] = seq_init[i];
    out[SEQ_INIT_FMT_AT].b = fmt;
    return SEQ_N(seq_init);
}
