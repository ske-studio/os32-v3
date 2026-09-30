/* ======================================================================== */
/*  V86_PIC.C — ゲスト用の仮想 8259A (PIC) ×2                               */
/* ======================================================================== */

#include "v86_pic.h"

struct pic {
    u8 imr;         /* OCW1 — ゲストのマスク (1 = 塞ぐ) */
    u8 irr;         /* 要求中 */
    u8 isr;         /* サービス中 */
    u8 base;        /* ICW2 — ゲストが決めるベクタベース */
    u8 icw_step;    /* 0=通常 1=ICW2待ち 2=ICW3待ち 3=ICW4待ち */
    u8 icw4_needed;
    u8 single;      /* ICW1 の SNGL。立っていると ICW3 を要求しない */
    u8 read_isr;    /* OCW3 の RR/RIS。0=IRR を読む 1=ISR を読む */
};

/* #GP ハンドラ (ゲスト OUT) と IRQ スタブ (反射ゲート) の両方の割り込み
 * 文脈から触られるため volatile。どちらも IF=0 で走るので直列化自体は
 * ゲートが保証するが、コンパイラのキャッシュ/並べ替えはこれで防ぐ。 */
static volatile struct pic pic_m;
static volatile struct pic pic_s;

/* 実測用。ゲストが何を書いたかは v86_iolog にも残るが、
 * 「最終的にどういう状態になったか」はこちらで見る。 */
u32 v86_pic_ocw2_n = 0;         /* EOI を受けた回数 */
u32 v86_pic_icw1_n = 0;         /* 初期化された回数 */
u32 v86_pic_masked_n = 0;       /* IMR で落とした IRQ の数 */
u32 v86_pic_inject_n = 0;       /* 通した IRQ の数 */

int v86_pic_is_port(u16 port)
{
    return (port == V86_PIC_M_CMD  || port == V86_PIC_M_DATA ||
            port == V86_PIC_S_CMD  || port == V86_PIC_S_DATA);
}

static void pic_init_one(volatile struct pic *p, u8 imr, u8 base)
{
    /* 既定の IMR は**素の NP21/W で実測した値**。推測ではない。
     *
     * Ys は ICW1 も OCW1 も一度も出さない (v86_pic_icw1_n = 0)。
     * BIOS が IPL 直前に設定した状態を丸ごと前提にしているので、
     * こちらが実機と同じ初期マスクを用意する義務がある。
     * グラフィックとまったく同じ形の問題 (v86_io.c の
     * gfx_state_for_guest() を参照)。
     *
     * 素の NP21/W で Ys.D88 を起動して `emu_pic` を読むと:
     *
     *   master IMR = 0x7D   IRQ0 (タイマ) **マスク** / IRQ1 (キー) 許可
     *                       IRQ2-6 マスク / IRQ7 (スレーブカスケード) 許可
     *   slave  IMR = 0x61   IRQ9-12 許可 / IRQ8,13,14 マスク / IRQ15 許可
     *
     * **IRQ0 がマスクされているのが要点。** 以前は 0x00 (全許可) にして
     * いたので、実機なら絶対に来ないタイマ割り込みをゲストに毎秒 100 回
     * 注入していた。ゲストは INT 08h を差し替えていないので BIOS ROM の
     * タイマハンドラが走り、毎回 PIT を再設定し (FD80:00AD の OUT 71h)、
     * タイトル画面から先へ進まなくなっていた。
     * 素の NP21/W の I/O プロファイルに 0x71 が一切現れないのが決め手。 */
    p->imr = imr;
    p->irr = 0;
    p->isr = 0;
    p->base = base;
    p->icw_step = 0;
    p->icw4_needed = 0;
    p->single = 0;
    p->read_isr = 0;
}

void v86_pic_reset(void)
{
    pic_init_one(&pic_m, V86_PIC_M_IMR_INIT, V86_PIC_M_BASE);
    pic_init_one(&pic_s, V86_PIC_S_IMR_INIT, V86_PIC_S_BASE);
    v86_pic_ocw2_n = 0;
    v86_pic_icw1_n = 0;
    v86_pic_masked_n = 0;
    v86_pic_inject_n = 0;
}

void v86_pic_set_imr(int slave, u8 imr)
{
    (slave ? &pic_s : &pic_m)->imr = imr;
}

u32 v86_pic_state(int slave)
{
    const volatile struct pic *p = slave ? &pic_s : &pic_m;
    return ((u32)p->isr << 16) | ((u32)p->irr << 8) | p->imr;
}

/* 最も優先度の高い (= ビット番号の小さい) セット済みビットを落とす。 */
static void clear_highest(volatile u8 *reg)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (*reg & (1 << i)) {
            *reg &= (u8)~(1 << i);
            return;
        }
    }
}

/* ------------------------------------------------------------------------ */
/*  コマンドポートへの書き込み                                              */
/*                                                                          */
/*  8259A の分岐規則:                                                       */
/*    bit4 = 1            → ICW1 (初期化開始)                               */
/*    bit4 = 0, bit3 = 1  → OCW3 (読み出すレジスタの選択 / ポーリング)      */
/*    bit4 = 0, bit3 = 0  → OCW2 (EOI 系)                                   */
/* ------------------------------------------------------------------------ */
static void pic_write_cmd(volatile struct pic *p, u8 v)
{
    if (v & 0x10) {
        /* ICW1 */
        p->icw_step = 1;
        p->icw4_needed = (u8)(v & 0x01);
        p->single = (u8)((v & 0x02) ? 1 : 0);
        p->isr = 0;
        p->irr = 0;
        p->read_isr = 0;
        /* ICW1 は IMR を消さない (実機と同じ)。ゲストは必ず OCW1 を書く。 */
        v86_pic_icw1_n++;
        return;
    }

    if (v & 0x08) {
        /* OCW3。bit1(RR) が立っているときだけ bit0(RIS) が意味を持つ。
         * ポーリングモード (bit2) は Ys では出てこないので未対応。 */
        if (v & 0x02) {
            p->read_isr = (u8)(v & 0x01);
        }
        return;
    }

    /* OCW2 — EOI。
     *   0x20 非特定 EOI      → 優先度最上位の ISR ビットを落とす
     *   0x60|n 特定 EOI      → ビット n を落とす
     *   0xA0 ローテート付き  → 落とすところは同じ (優先度回転は未実装) */
    v86_pic_ocw2_n++;
    if ((v & 0x60) == 0x60) {
        p->isr &= (u8)~(1 << (v & 0x07));
    } else if (v & 0x20) {
        clear_highest(&p->isr);
    }
}

static void pic_write_data(volatile struct pic *p, u8 v)
{
    switch (p->icw_step) {
    case 1:                     /* ICW2 — ベクタベース */
        p->base = (u8)(v & 0xF8);
        p->icw_step = p->single ? (p->icw4_needed ? 3 : 0) : 2;
        return;

    case 2:                     /* ICW3 — カスケード構成。値は使わない */
        p->icw_step = p->icw4_needed ? 3 : 0;
        return;

    case 3:                     /* ICW4 — 8086 モード等。値は使わない */
        p->icw_step = 0;
        return;

    default:                    /* OCW1 — IMR */
        p->imr = v;
        return;
    }
}

u32 v86_pic_in(u16 port)
{
    switch (port) {
    case V86_PIC_M_CMD:  return pic_m.read_isr ? pic_m.isr : pic_m.irr;
    case V86_PIC_M_DATA: return pic_m.imr;
    case V86_PIC_S_CMD:  return pic_s.read_isr ? pic_s.isr : pic_s.irr;
    case V86_PIC_S_DATA: return pic_s.imr;
    default:             return 0xFF;
    }
}

void v86_pic_out(u16 port, u32 value)
{
    u8 v = (u8)(value & 0xFF);

    switch (port) {
    case V86_PIC_M_CMD:  pic_write_cmd(&pic_m, v);  break;
    case V86_PIC_M_DATA: pic_write_data(&pic_m, v); break;
    case V86_PIC_S_CMD:  pic_write_cmd(&pic_s, v);  break;
    case V86_PIC_S_DATA: pic_write_data(&pic_s, v); break;
    default: break;
    }
}

/* ------------------------------------------------------------------------ */
/*  割り込み反射のゲート                                                    */
/*                                                                          */
/*  実 IRQ が来たとき、ゲストに見せてよいかをここで決める。                 */
/*  IMR で塞がれていたら IRR に立てるだけで注入しない。                     */
/*                                                                          */
/*  カスケードは各チップ独立に扱う。厳密にはスレーブの割り込みで            */
/*  マスタの IR7 の ISR も立つが、ゲストは EOI を両方に出す作法なので       */
/*  独立でも破綻しない。破綻したら実測で分かる。                            */
/* ------------------------------------------------------------------------ */
int v86_pic_should_inject(u32 irq, u32 *out_vector)
{
    volatile struct pic *p;
    u8 bit;

    if (irq >= 16) {
        return 0;
    }
    p = (irq < 8) ? &pic_m : &pic_s;
    bit = (u8)(1 << (irq & 7));

    p->irr |= bit;

    if (p->imr & bit) {
        v86_pic_masked_n++;
        return 0;               /* ゲストが塞いでいる。IRR は立てたまま */
    }

    p->irr &= (u8)~bit;
    p->isr |= bit;
    v86_pic_inject_n++;

    if (out_vector) {
        *out_vector = (u32)p->base + (irq & 7);
    }
    return 1;
}
