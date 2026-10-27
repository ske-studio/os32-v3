#include "display_restore.h"
#include "platform_display.h"
#include "display_private.h"
#include "io.h"
#include "gfx_internal.h"
#include "v86_bios.h"
#include "palette.h"
#include "pegc.h"
#include "con_sink.h"
#include "console.h"

/* UNDOCUMENTED io_disp.md: I/O 00A2h / モードF/F1 DISP ENABLE。
 * CUI の表示順序を通常の V86 と ROM 採取の出口で共有する。 */
/* Core display return path also serves kdebug with v86.mod absent. */
/* Shared with recover: always linked into the kernel, no v86.mod entry. */
static void display_out(unsigned int port, unsigned int value)
{
    outp(port, value);
    io_wait();
}

void display_text_start(void)
{
    gfx_current_height = GFX_HEIGHT;
    gfx_flip_enabled = 0;
    outp(GDC_GFX_CMD, GDC_CMD_STOP);
    io_wait();
    outp(MODE_FF1_PORT, MFF1_DISP_ON);
    io_wait();
    outp(GDC_TEXT_CMD, GDC_CMD_START);
    io_wait();
}

void display_restore_98(void)
{
    u32 i;
    /* gfx_hardware_scroll(0) が 400 ライン・スクロール無しで出す値
     * (SAD=0 / SL=400 の 1 分割)。実測した OS32 側の PRAM と一致する。 */
    static const u8 pram_400[8] = { 0x00, 0x00, 0x00, 0x19,
                                    0x00, 0x00, 0x00, 0x00 };

    display_out(GDC_GFX_CMD, GDC_CMD_STOP);

    /* Saved boot flags, never the guest's BDA. UNDOCUMENTED io_disp 006Ah:
     * EGC→GRCG requires 04A0h=FFF0h; 20h/68h exist only on extended gfx.
     * FreeBSD 10.4 sys/pc98/cbus/gdc.c:1095-1107 gates the latter too. */
    const u8 *bios = v86_bios_real_lowmem();
    if (bios[BIOS_GRAPH_FLAGS] & BIOS_GRAPH_EGC) {
        outpw(EGC_ACCESS_PORT, EGC_ACCESS_RESET);
        io_wait();
        display_out(MODE_FF2_PORT, MFF2_UNLOCK);
        display_out(MODE_FF2_PORT, MFF2_GRCG_MODE);
        display_out(MODE_FF2_PORT, MFF2_LOCK);
    }
    if (bios[BIOS_CRT_FLAGS] & BIOS_CRT_GRCG)
        display_out(GRCG_MODE_PORT, GRCG_MODE_OFF);
    if (bios[PEGC_BIOS_ARCH_FLAG] & PEGC_BIOS_ARCH_EXTGFX) {
        display_out(MODE_FF2_PORT, PEGC_FF2_UNLOCK);
        display_out(MODE_FF2_PORT, PEGC_FF2_STD_GFX);
        display_out(MODE_FF2_PORT, PEGC_FF2_VRAM_400L);
        display_out(MODE_FF2_PORT, PEGC_FF2_LOCK);
    }
    display_out(MODE_FF2_PORT, MFF2_16COLOR);
    display_out(MODE_FF1_PORT, MFF1_HIRES);

    display_out(GDC_GFX_CMD, GDC_GFX_400LINE);
    display_out(GDC_GFX_PARAM, 0x00);
    display_out(GDC_GFX_PARAM, 0x00);
    display_out(GDC_GFX_PARAM, 0x00);

    display_out(GDC_GFX_CMD, GDC_CMD_SCROLL);
    for (i = 0; i < sizeof(pram_400); i++) {
        display_out(GDC_GFX_PARAM, pram_400[i]);
    }

    display_text_start();
    display_out(GDC_DISP_PAGE, GDC_PAGE_0);
    display_out(GDC_ACCESS_PAGE, GDC_PAGE_0);

    /* 16 色に戻した後でないとアナログパレットとして書き込まれない */
    palette_init();

    /* テキスト GDC のカーソル形状/位置もゲストが書き換えている
     * (DOS は DC=1 のブロックカーソルを最後の位置に残す)。OS32 の形に戻す。 */
    console_hw_cursor_enable();
}

int display_recover_ready(void)
{
    if (con_sink_is_enabled() || gfx_current_height != GFX_HEIGHT || gfx_flip_enabled) return -1;
    return (inp(PEGC_HSYNC_PORT) & PEGC_HSYNC_MASK) == PEGC_HSYNC_31KHZ ? 0 : -1;
}

int display_recover_cell_read(u32 *value)
{
    if (!value || display_recover_ready()) return -1;
    *value = *(volatile u16 *)P2V_IO(DISPLAY_RECOVER_CELL);
    return 0;
}

int display_recover_cell_write(u32 value)
{
    if (value > 0xffffU || display_recover_ready()) return -1;
    *(volatile u16 *)P2V_IO(DISPLAY_RECOVER_CELL) = (u16)value;
    return 0;
}

/* Bible §2-6-5 / UNDOCUMENTED io_disp 0068h KAC Mode. */
void platform_display_kcg_code(void)
{
    outp(MODE_FF1_PORT, MFF1_KCG_CODE);
    io_wait();
}
