/* ======================================================================== */
/*  MOUSE.C — マウスドライバ統合モジュール                                  */
/*                                                                          */
/*  起動時にNP21/Wを検出し、シームレス/バスマウスを自動選択する。           */
/*  外部からは mouse_poll() で統一的にアクセスできる。                      */
/* ======================================================================== */

/* PC-98 acquisition and text-cell geometry. */
#include "irq_line.h"
#include "mouse.h"
#include "mouse_pointer_contract.h"
#include "../../../hal/input/in2_int.h"
#include "kcg_contract.h"
#include "../../../dev/char/np2sysp.h"
#include "pc98.h"
#include "kprintf.h"

static u8 mouse_mode_current = MOUSE_MODE_NONE;

/* ====================================================================== */
/*  mouse_init — マウスドライバ初期化                                      */
/* ====================================================================== */
void mouse_init(void)
{
    mouse_pointer_init_position();
    mouse_ext_mode(0);

    /* NP21/W検出 → シームレスモード試行 */
    if (np2_detect()) {
        if (seamless_mouse_init()) {
            mouse_hal_seamless_init();
            mouse_ext_mode(1);
            mouse_mode_current = MOUSE_MODE_SEAMLESS;
            kprintf(KLOG_LEGACY_GREEN, "[mouse] seamless mode (NP21/W)\n");
            return;
        }
    }

    /* 実機 or フォールバック → バスマウスモード */
    if (bus_mouse_init()) {
        irq_enable(PC98_IRQ_MOUSE);  /* IRQ13 (スレーブ IR5) をPICで許可 */
        mouse_mode_current = MOUSE_MODE_BUS;
        kprintf(KLOG_LEGACY_GREEN, "[mouse] bus mouse mode (IRQ13)\n");
        return;
    }

    mouse_mode_current = MOUSE_MODE_NONE;
    kprintf(KLOG_LEGACY_RED, "[mouse] no mouse available\n");
}

__attribute__((noipa))
static u8 mouse_pointer_snapshot(i16 *dx, i16 *dy, u8 *buttons, i16 *x, i16 *y)
{
    if (mouse_mode_current == MOUSE_MODE_SEAMLESS) mouse_hal_seamless_poll();
    if (mouse_mode_current != MOUSE_MODE_NONE) {
        mouse_legacy_snapshot(dx, dy, buttons, x, y);
    }
    return mouse_mode_current;
}

int mouse_available(void)
{
    return (mouse_mode_current != MOUSE_MODE_NONE);
}

__attribute__((noipa))
static void mouse_pointer_text_cell(i16 x, i16 y, int *cx, int *cy)
{
    *cx = x / KCG_ANK_W;
    *cy = y / KCG_ANK_H;
    if (*cx > TVRAM_COLS - 1) *cx = TVRAM_COLS - 1;
    if (*cy > TVRAM_ROWS - 1) *cy = TVRAM_ROWS - 1;
}

const MousePointerBackend pc98_mouse_backend = {
    .snapshot = mouse_pointer_snapshot,
    .text_cell = mouse_pointer_text_cell,
};
