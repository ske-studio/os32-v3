/* PC-98 standard planar graphics: mode entry and display-memory clearing.
 * (第 5 段 B3: gfx/gfx_core.c・gfx_core_init.inc から移した。値と順は不変。)
 * UNDOCUMENTED io_disp.md: GDC CSRFORM、モード F/F1・F/F2、A4h/A6h のページ。 */
#include "gfx_internal.h"
#include "display_restore.h"
#include "kstring.h"
#include "memmap.h"

/* テキスト VRAM を見えなくする (文字 0・属性 0)。console_clear とは別の口。 */
void display_text_hide(void)
{
    int i;
    volatile u16 *tvram_char = (volatile u16 *)P2V_IO(TVRAM_CHAR_BASE);
    volatile u8  *tvram_attr = (volatile u8  *)P2V_IO(TVRAM_ATTR_BASE);

    for (i = 0; i < TVRAM_COLS * TVRAM_ROWS; i++) {
        tvram_char[i] = 0x0000;
        tvram_attr[i * 2] = 0x00;
    }
}

void display_planar16(void)
{
    _out(MODE_FF2_PORT, MFF2_16COLOR);
}

/* GDC CSRFORM: L/R=1 のとき各ラインを2倍表示する。 */
void display_planar_start(int legacy200)
{
    _out(GDC_GFX_CMD, GDC_GFX_400LINE);
    _out(GDC_GFX_PARAM, legacy200 ? 0x01 : 0x00);
    _out(MODE_FF1_PORT, legacy200 ? MFF1_200LINE : MFF1_HIRES);

    _out(GDC_GFX_CMD, GDC_CMD_START);
}

/* 表示ページ → CPU の書込先ページの順 (A4h、A6h)。 */
void display_flip_pages(int display, int access)
{
    _out(GDC_DISP_PAGE, display ? GDC_PAGE_1 : GDC_PAGE_0);
    _out(GDC_ACCESS_PAGE, access ? GDC_PAGE_1 : GDC_PAGE_0);
}

/* テキスト GDC の状態の VSYNC が立つまで回る (第 5 段 B6: gfx/gfx_vram.c から)。 */
void display_wait_vsync(void)
{
    while ((_in(GDC_TEXT_STAT) & GDC_STAT_VSYNC) == 0) { }
}

/* 表示メモリの 4 プレーン (B・R・G・I)。A6h で選んだ書込先ページに届く。 */
u8 *display_planar_plane(unsigned index)
{
    static const u32 planes[GVRAM_PLANES] = {
        VRAM_PLANE_B, VRAM_PLANE_R, VRAM_PLANE_G, VRAM_PLANE_I
    };
    return (u8 *)P2V(planes[index]);
}

/* 標準 planar モード専用。CPU 直書きで両ページの表示領域を消す [HW1]。
 * UNDOCUMENTED io_disp.md「VRAMプレーン切り換え」: A6h が CPU の書込先。
 * 戻りは描画ページ1。呼び手は初期化を続けるか直ちに shutdown する。 */
void gfx_clear_planar_pages(u32 plane_size)
{
    _out(GDC_ACCESS_PAGE, GDC_PAGE_0);
    kmemset((u8 *)P2V(VRAM_PLANE_B), 0, plane_size);
    kmemset((u8 *)P2V(VRAM_PLANE_R), 0, plane_size);
    kmemset((u8 *)P2V(VRAM_PLANE_G), 0, plane_size);
    kmemset((u8 *)P2V(VRAM_PLANE_I), 0, plane_size);

    _out(GDC_ACCESS_PAGE, GDC_PAGE_1);
    kmemset((u8 *)P2V(VRAM_PLANE_B), 0, plane_size);
    kmemset((u8 *)P2V(VRAM_PLANE_R), 0, plane_size);
    kmemset((u8 *)P2V(VRAM_PLANE_G), 0, plane_size);
    kmemset((u8 *)P2V(VRAM_PLANE_I), 0, plane_size);
}
