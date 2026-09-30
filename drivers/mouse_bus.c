/* ======================================================================== */
/*  MOUSE_BUS.C — バスマウスドライバ (IRQ13 + 8255 PPI)                     */
/*                                                                          */
/*  PC-9801標準バスマウスインターフェースを制御する。                        */
/*  IRQ13 (スレーブPIC IR5, INT 0x2D) でカウンタを定期的にラッチし、         */
/*  相対移動量を累積する。                                                  */
/*                                                                          */
/*  出典: PC9800Bible §2-11, UNDOCUMENTED io_mouse.md,                      */
/*        NP21/W io/mouseif.c                                               */
/* ======================================================================== */

#include "mouse.h"
#include "pc98.h"
#include "io.h"

/* ====================================================================== */
/*  8255 ポートC 制御ビット                                                */
/* ====================================================================== */
/* bit7: HC    — 0->1でラッチ＆カウンタクリア                               */
/* bit6: SXY   — 0=X軸, 1=Y軸                                              */
/* bit5: SHL   — 0=下位4bit, 1=上位4bit                                     */
/* bit4: -INT  — 0=割り込み許可, 1=割り込み禁止                             */
#define PORTC_HC    0x80
#define PORTC_SXY   0x40
#define PORTC_SHL   0x20
#define PORTC_INT   0x10   /* 1=割り込み禁止 */

/* ====================================================================== */
/*  内部状態 (volatile — IRQハンドラから更新)                               */
/* ====================================================================== */
static volatile i16 bus_accum_dx;   /* 累積X差分 */
static volatile i16 bus_accum_dy;   /* 累積Y差分 */
static volatile u8  bus_btn_raw;    /* 最新のボタン生データ */

/* ====================================================================== */
/*  mouse_read_buttons — ポートAからボタン状態を読取                        */
/*                                                                          */
/*  PC9800Bible §2-11 / UNDOCUMENTED:                                       */
/*    D7=LEFT(0=押下), D6=MIDDLE(0=押下), D5=RIGHT(0=押下)                  */
/*  → ビット反転して MOUSE_BTN_xxx マスクに変換                            */
/* ====================================================================== */
u8 mouse_read_buttons(void)
{
    u8 raw;
    u8 btn = 0;
    raw = (u8)inp(MOUSE_DATA);
    /* ボタンは負論理 (0=押下) → 反転してマスク */
    if (!(raw & MOUSE_LEFT))   btn |= MOUSE_BTN_LEFT;
    if (!(raw & MOUSE_RIGHT))  btn |= MOUSE_BTN_RIGHT;
    if (!(raw & MOUSE_MIDDLE)) btn |= MOUSE_BTN_MIDDLE;
    return btn;
}

/* ====================================================================== */
/*  mouse_irq_handler — IRQ13 割り込みハンドラ                              */
/*                                                                          */
/*  isr_stub.asm の irq_stub_13 から呼ばれる。                              */
/*  カウンタをラッチし、4nibble読取で8bit符号付き差分を取得。               */
/* ====================================================================== */
void mouse_irq_handler(void)
{
    u8 data;
    u8 xl, xh, yl, yh;
    i8 dx, dy;

    /* HC=0->1: カウンタをラッチ＆クリア、割り込み許可維持 */
    outp(MOUSE_CTRL, PORTC_HC);

    /* X軸 下位4bit (SXY=0, SHL=0) */
    outp(MOUSE_CTRL, PORTC_HC | 0x00);
    data = (u8)inp(MOUSE_DATA);
    xl = data & 0x0F;
    /* ボタンも同時に読取 */
    bus_btn_raw = data & 0xE0;

    /* X軸 上位4bit (SXY=0, SHL=1) */
    outp(MOUSE_CTRL, PORTC_HC | PORTC_SHL);
    data = (u8)inp(MOUSE_DATA);
    xh = data & 0x0F;

    /* Y軸 下位4bit (SXY=1, SHL=0) */
    outp(MOUSE_CTRL, PORTC_HC | PORTC_SXY);
    data = (u8)inp(MOUSE_DATA);
    yl = data & 0x0F;

    /* Y軸 上位4bit (SXY=1, SHL=1) */
    outp(MOUSE_CTRL, PORTC_HC | PORTC_SXY | PORTC_SHL);
    data = (u8)inp(MOUSE_DATA);
    yh = data & 0x0F;

    /* 8bit符号付き差分に合成 */
    dx = (i8)((xh << 4) | xl);
    dy = (i8)((yh << 4) | yl);

    /* 累積に加算 */
    bus_accum_dx += dx;
    bus_accum_dy += dy;
}

/* ====================================================================== */
/*  bus_mouse_init — バスマウス初期化                                       */
/*                                                                          */
/*  8255をモード0入力に設定し、IRQ13を有効化する。                          */
/* ====================================================================== */
int bus_mouse_init(void)
{
    /* 累積値クリア */
    bus_accum_dx = 0;
    bus_accum_dy = 0;
    bus_btn_raw = 0xE0; /* 全ボタン解放 (負論理) */

    /* 8255 モード設定: モード0, PortA入力, PortB入力, PortCH出力, PortCL入力 */
    outp(MOUSE_MODE, 0x93);

    /* 割り込み周期: 120Hz (T1:T0 = 00) — UNDOCUMENTED: リセット時初期値 */
    outp(MOUSE_INTERVAL, 0x00);

    /* 割り込み許可 (PortC bit4 = 0) */
    outp(MOUSE_CTRL, 0x00);  /* HC=0, SXY=0, SHL=0, INT=0(許可) */

    /* IRQ13のPIC有効化は呼び出し元 (mouse.c) で行う */

    return 1;
}

/* ====================================================================== */
/*  bus_mouse_get — バスマウスの累積差分を取得しリセット                    */
/* ====================================================================== */
void bus_mouse_get(i16 *dx, i16 *dy, u8 *buttons)
{
    unsigned int flags;

    /* 割り込み禁止区間で累積値を読取・リセット */
    flags = irq_save();
    *dx = bus_accum_dx;
    *dy = bus_accum_dy;
    bus_accum_dx = 0;
    bus_accum_dy = 0;
    irq_restore(flags);

    /* ボタン: 負論理→正論理変換 */
    *buttons = 0;
    if (!(bus_btn_raw & MOUSE_LEFT))   *buttons |= MOUSE_BTN_LEFT;
    if (!(bus_btn_raw & MOUSE_RIGHT))  *buttons |= MOUSE_BTN_RIGHT;
    if (!(bus_btn_raw & MOUSE_MIDDLE)) *buttons |= MOUSE_BTN_MIDDLE;
}
