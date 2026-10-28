#ifndef KDEBUG_GFINIT_MODE_H
#define KDEBUG_GFINIT_MODE_H
#include "../kdebug/gfinit_pll.h"
/* Fixed diagnostic mode, GF6200_4C_DESIGN §§1,3. DCB facts are decoded
 * data from the PM's confirmation, never bytes from the private ROM. */
#define GF_MODE_WIDTH 640U
#define GF_MODE_HEIGHT 480U
#define GF_MODE_HSYNC_START 656U
#define GF_MODE_HSYNC_END 752U
#define GF_MODE_HTOTAL 800U
#define GF_MODE_VSYNC_START 490U
#define GF_MODE_VSYNC_END 492U
#define GF_MODE_VTOTAL 525U
#define GF_MODE_BPP 8U
#define GF_MODE_PITCH GF_MODE_WIDTH
#define GF_MODE_CLOCK_KHZ 25175U
#define GF_MODE_CRTC_COUNT 0xa0U
#define GF_MODE_SR_COUNT 5U
#define GF_MODE_GR_COUNT 9U
#define GF_MODE_AR_COUNT 21U
#define GF_MODE_PALETTE_SIZE 256U
#define GF_MODE_PALETTE_COLORS 8U
#define GF_MODE_RGB 3U
#define GF_MODE_BYTE_MASK 0xffU
#define GF_MODE_CHAR_SHIFT 3U
#define GF_MODE_DCB_ENTRY 0U
#define GF_MODE_DCB_ANALOG 0U
#define GF_MODE_DCB_HEADS 3U
#define GF_MODE_DCB_OR 0U
#define GF_MODE_DCB_CONNECTOR 0U
#define GF_MODE_CONNECTOR_VGA 0U
#define GF_MODE_HEAD 0U
#define GF_MODE_SEL_CLK_CLEAR 0x00050000U
#define GF_MODE_PLLSEL_VPLL_A (1U << 28)
#define GF_MODE_PLLSEL_USE_VPLL2 (1U << 2)
#define GF_MODE_PLLSEL_SOURCE (1U << 9)
#define GF_MODE_PLL_SELECT (GF_MODE_PLLSEL_VPLL_A | GF_MODE_PLLSEL_USE_VPLL2 | GF_MODE_PLLSEL_SOURCE)
/* VGA/NV04 timing indices: nouveau dispnv04/nvreg.h, crtc.c:350-413. */
enum gfinit_mode_cr_index {
    GF_MODE_CR_HT=0x00, GF_MODE_CR_HD=0x01, GF_MODE_CR_HBS=0x02,
    GF_MODE_CR_HBE=0x03, GF_MODE_CR_HS=0x04, GF_MODE_CR_HE=0x05,
    GF_MODE_CR_VT=0x06, GF_MODE_CR_OVERFLOW=0x07, GF_MODE_CR_CELL=0x09,
    GF_MODE_CR_VS=0x10, GF_MODE_CR_VE=0x11, GF_MODE_CR_VD=0x12,
    GF_MODE_CR_PITCH=0x13, GF_MODE_CR_VBS=0x15, GF_MODE_CR_VBE=0x16,
    GF_MODE_CR_MODE=0x17, GF_MODE_CR_LINE_COMPARE=0x18,
    GF_MODE_CR_PITCH_HI=0x19, GF_MODE_CR_RPC1=0x1a, GF_MODE_CR_BURST=0x1b,
    GF_MODE_CR_ENH=0x1c, GF_MODE_CR_LWM=0x20, GF_MODE_CR_LSR=0x25,
    GF_MODE_CR_PIXEL=0x28, GF_MODE_CR_HEB=0x2d, GF_MODE_CR_CURSOR=0x30,
    GF_MODE_CR_LCD=0x33, GF_MODE_CR_INTERLACE=0x39, GF_MODE_CR_SCRATCH3=0x3b,
    GF_MODE_CR_SCRATCH4=0x3c, GF_MODE_CR_EBR=0x41, GF_MODE_CR_PITCH11=0x42,
    GF_MODE_CR_SCRATCH4B=0x4b, GF_MODE_CR_LATENCY=0x52,
    GF_MODE_CR_85=0x85, GF_MODE_CR_86=0x86, GF_MODE_CR_9F=0x9f
};
#define GF_MODE_MISC_NEG_HV 0xe3U
#define GF_MODE_SR_BLANKED 0x21U
#define GF_MODE_PLANES 0x0fU
#define GF_MODE_SR_LINEAR 0x0eU
#define GF_MODE_HBE_ENABLE 0x80U
#define GF_MODE_END_MASK 0x1fU
#define GF_MODE_OVL_LINE_COMPARE 0x10U
#define GF_MODE_CELL_LINE_COMPARE 0x40U
#define GF_MODE_VRE_ENABLE 0x20U
#define GF_MODE_VRE_MASK 0x0fU
#define GF_MODE_CRTC_MODE 0x43U
#define GF_MODE_RPC1_LARGE 0x04U
#define GF_MODE_ENH_CLEAR 0x20U
#define GF_MODE_LCD_CLEAR 0x3bU
#define GF_MODE_ANALOG_SCRATCH 0x22U
#define GF_MODE_HEAD_A_SCRATCH 0x80U
#define GF_MODE_LATENCY_ADD 4U
#define GF_MODE_CURSOR_HIDDEN 0x80U
#define GF_MODE_FIFO_BURST 4U
#define GF_MODE_FIFO_LWM 0xc0U
#define GF_MODE_ENGINE_I2C 0x10U
#define GF_MODE_CURSOR_CONFIG 0x04011100U
#define GF_MODE_CONFIG_HSYNC 2U
#define GF_MODE_CURSYNC (1U << 25)
#define GF_MODE_GENERAL 0x20100130U
#define GF_MODE_FP_OFF 0x20000022U
#define GF_MODE_RAMDAC8C0 0x100U
#define GF_MODE_RAMDACA24 0x000fffffU
#define GF_MODE_DAC_ENABLE 1U
#define GF_MODE_TEST_CONTROL 0x00100000U
#define GF_MODE_GR_INDEXED 0x40U
#define GF_MODE_GR_MAP 0x05U
struct gfinit_mode_saved {
    u8 cr1c, cr33, cr3c, cr4b, cr52;
    u32 gpio_ext, crtc850, sel_clk;
};
struct gfinit_mode_timing {
    u32 hd, hs, he, ht, hbs, hbe, vd, vs, ve, vt, vbs, vbe;
};
struct gfinit_mode {
    struct gfinit_mode_timing timing;
    u8 cr[GF_MODE_CRTC_COUNT], sr[GF_MODE_SR_COUNT];
    u8 gr[GF_MODE_GR_COUNT], ar[GF_MODE_AR_COUNT], misc;
    u32 pllsel, sel_clk, cursync, general, ramdac630, ramdac634;
    u32 tv_setup, fp_control, ramdac8c0, ramdaca20, ramdaca24, ramdaca34;
    u32 engine_ctrl, cursor_cfg, crtc830, crtc834, gpio_ext, crtc850;
    u32 config, start, dacclk, test_control, single;
    struct gfinit_pll_limits limits;
    struct gfinit_pll_solution pll;
    struct gfinit_pll_sequence sequence;
};
/* Pure state calculation; caller supplies VIDEO limits and pre-Q captures.
 * Does not read hardware or bind/execute a recover plan. */
int gfinit_mode_calc(const struct gfinit_pll_limits *limits,
                     const struct gfinit_mode_saved *saved, struct gfinit_mode *out);
void gfinit_mode_palette(u32 index, u8 rgb[GF_MODE_RGB]);
#endif
