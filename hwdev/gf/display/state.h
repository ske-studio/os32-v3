/* Shared main4 captures and C CHECK constants, docs/design/hwdev/gf/display/state.md. */
#ifndef GF_DISPLAY_STATE_H
#define GF_DISPLAY_STATE_H
#include "../../gf_load_state.h"
#define GF_STATE_OFF(f) __builtin_offsetof(struct gfinit_mode, f)
#define GF_DISPLAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define C_GPIO_EXT 0x60081cU
#define C_CRTC850 0x600850U
#define C_SEL_CLK 0x680524U
#define C_PLL_SELECT 0x68050cU
#define C_CR_RPC1_MASK 0xd7U /* CR1A bits 3/5 read back set on NV44A */
#define C_PLL_STATUS 0x680510U
#define C_CONFIG 0x600804U
#define C_START 0x600800U
#define C_DACCLK 0x68052cU
#define C_TEST 0x680608U
#define C_GENERAL 0x680600U
#define C_DACCLK_MASK 0x101U
#define C_CONFIG_MASK 3U
#define C_CR_LOCK 0x80U
#define C_CR_PITCH_EXTRA 0x47U
#define C_PLL_STAGE_WRITE 1U
#define C_PLL_COEFF_WRITE 2U
#define C_PLL_PRIMARY_WRITE 3U
struct c_saved_field { u32 offset, kind, reg, index, width, keep, add, bits, saved_offset; };
static const struct c_saved_field c_fields[] = {
    {GF_STATE_OFF(cr)+GF_MODE_CR_ENH,GF_C_CR,0,GF_MODE_CR_ENH,1,~GF_MODE_ENH_CLEAR,0,0,__builtin_offsetof(struct gfinit_mode_saved,cr1c)},
    {GF_STATE_OFF(cr)+GF_MODE_CR_LCD,GF_C_CR,0,GF_MODE_CR_LCD,1,~GF_MODE_LCD_CLEAR,0,0,__builtin_offsetof(struct gfinit_mode_saved,cr33)},
    {GF_STATE_OFF(cr)+GF_MODE_CR_SCRATCH4,GF_C_CR,0,GF_MODE_CR_SCRATCH4,1,~0U,0,0,__builtin_offsetof(struct gfinit_mode_saved,cr3c)},
    {GF_STATE_OFF(cr)+GF_MODE_CR_SCRATCH4B,GF_C_CR,0,GF_MODE_CR_SCRATCH4B,1,~0U,0,GF_MODE_HEAD_A_SCRATCH,__builtin_offsetof(struct gfinit_mode_saved,cr4b)},
    {GF_STATE_OFF(cr)+GF_MODE_CR_LATENCY,GF_C_CR,0,GF_MODE_CR_LATENCY,1,~0U,GF_MODE_LATENCY_ADD,0,__builtin_offsetof(struct gfinit_mode_saved,cr52)},
    {GF_STATE_OFF(gpio_ext),GF_C_MMIO,C_GPIO_EXT,0,4,~0U,0,0,__builtin_offsetof(struct gfinit_mode_saved,gpio_ext)},
    {GF_STATE_OFF(crtc850),GF_C_MMIO,C_CRTC850,0,4,~0U,0,0,__builtin_offsetof(struct gfinit_mode_saved,crtc850)},
    {GF_STATE_OFF(sel_clk),GF_C_MMIO,C_SEL_CLK,0,4,~GF_MODE_SEL_CLK_CLEAR,0,0,__builtin_offsetof(struct gfinit_mode_saved,sel_clk)}
};
#endif
