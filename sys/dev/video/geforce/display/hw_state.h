/* GF_STOP_REF_DESIGN §3-1: module-private display state, not gf4_state. */
#ifndef HWDEV_GF_DISPLAY_HW_STATE_H
#define HWDEV_GF_DISPLAY_HW_STATE_H
#include "api.h"
struct gf_io;

/* NV44A subset of nouveau dispnv04/disp.h:23-81. */
struct gf4_hw_pllvals { u32 refclk; u16 nm1,nm2; u8 log2p; };
struct gf4_hw_crtc_reg {
    u8 misc,cr[0xa0],cr58[16],sr[5],gr[9],ar[21],dac[768];
    u32 fb_start,crtc_cfg,cursor_cfg,gpio_ext,crtc_830,crtc_834,crtc_850,crtc_eng_ctrl;
    u32 nv10_cursync; struct gf4_hw_pllvals pllvals;
    u32 ramdac_gen_ctrl,ramdac_630,ramdac_634;
    u32 tv_setup,tv_vtotal,tv_vskew,tv_vsync_delay,tv_htotal,tv_hskew,tv_hsync_delay,tv_hsync_delay2;
    u32 fp_horiz_regs[7],fp_vert_regs[7],dither,fp_control,dither_regs[6];
    u32 fp_debug_0,fp_debug_1,fp_debug_2,fp_margin_color,ramdac_8c0;
    u32 ramdac_a20,ramdac_a24,ramdac_a34,ctv_regs[38];
};
struct gf4_hw_output { u32 output; i32 head; struct gf4_tv_state tv; };
struct gf4_hw_saved {
    struct gf4_hw_crtc_reg crtc[GF4_HEADS]; u32 pllsel,sel_clk;
    struct gf4_hw_output output[32]; u32 nroutes;
    u32 *fonts; bool valid,fonts_valid;
};
/* S1 / S4a: save once, access for restore, reset on module unload. */
void gf4_hw_save(struct gf4_ctx *,struct gf_io *);
struct gf4_hw_saved *gf4_hw_saved_get(void);
void gf4_hw_state_reset(void);
int gf4_hw_restore_nodeadline(struct gf4_ctx *);
int gf4_hw_crtc_save(struct gf_io *,u8,u32,struct gf4_hw_saved *);
/* S1b: fonts_save allocates; fonts_reset releases the pages. */
int gf4_hw_fonts_save(struct gf_io *,struct gf4_hw_saved *);
int gf4_hw_fonts_restore(struct gf_io *,const struct gf4_hw_saved *);
void gf4_hw_fonts_reset(struct gf4_hw_saved *);
/* S2: load saved CRTC; S2b: program decoded VPLL. */
int gf4_hw_crtc_restore(struct gf_io *,u8,const struct gf4_hw_saved *);
int gf4_hw_pll_restore(struct gf_io *,u8,const struct gf4_hw_pllvals *);
/* S3: routes are the parsed DCB routes, available before svc_feature5. */
int gf4_hw_enc_save(struct gf_io *,const struct gf4_route *,u32,struct gf4_hw_saved *);
int gf4_hw_enc_restore(struct gf4_ctx *,struct gf_io *,const struct gf4_route *,u32,
                       const struct gf4_hw_saved *,bool,u32);
#endif
