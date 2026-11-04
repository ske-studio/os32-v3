/* Display contract: GF_FEAT4_DESIGN §2 and G2. */
#ifndef HWDEV_GF_DISPLAY_API_H
#define HWDEV_GF_DISPLAY_API_H
#include "data.h"
#include "../accel.h"
#include "../../gfinit_mode.h"

#define GF4_HEADS 2U
#define GF4_MODES 96U
#define GF4_CURSOR_BYTES 16384U
#define GF4_CURSOR_ALIGN 4096U
#define GF4_STATE_VERSION 1U
#define GF4_SAVED_VERSION 1U

enum gf4_rc { GF4_OK=0, GF4_EINVAL=-401, GF4_ERANGE=-402,
 GF4_EFORMAT=-403, GF4_EPLL=-404, GF4_EDDC=-405, GF4_EEDID=-406,
 GF4_ENOSPC=-407, GF4_ESTATE=-408, GF4_ETIME=-409,
 GF4_EUNSUPPORTED=-410, GF4_EBUSY=-411, GF4_ENODEV=-412 };
enum gf4_dpms { GF4_ON, GF4_STANDBY, GF4_SUSPEND, GF4_OFF };
struct gf4_mode { u32 id; struct gf4_timing t; enum gf3_fmt fmt; u32 pitch; };
struct gf4_cursor { const u32 *argb_pm; u32 bytes,pitch,w,h; i32 hot_x,hot_y; };
struct gf4_head_image {
    bool enabled,blank,cursor_visible,lut_dirty;
    struct gf4_mode mode; struct gf4_route route;
    struct gf3_surface front,back; struct gf_vram_reservation cursor;
    i32 x,y,hot_x,hot_y; u32 cursor_w,cursor_h,fp_on;
    enum gf4_dpms dpms; u8 palette[768]; u16 gamma[3][256];
};
struct gf4_flip_hold {
    bool pending,base_written; struct gf3_surface old_front,new_front; u32 after;
};
struct gf4_saved {
    u32 version,size,wanted_fifo,wanted_graph,wanted_heads;
    struct gf4_head_image head[GF4_HEADS]; struct gf4_flip_hold hold[GF4_HEADS];
};
struct gf4_state {
    u32 version,size,nmodes; struct gf4_mode modes[GF4_MODES];
    u8 mode_connector[GF4_MODES]; struct gf4_edid_state edid;
    struct gf4_head_image head[GF4_HEADS]; struct gf4_tv_state tv[GF4_HEADS];
    struct gfinit_mode work[GF4_HEADS]; struct gf4_saved rollback;
};
struct gf4_exec_ops {
    i32 (*tmds_script)(void *,const struct gf4_route *,u32 khz,u32 dl);
    i32 (*external_mode)(void *,const struct gf4_route *,const struct gf4_mode *,u32 dl);
    void *owner;
};
struct gf4_ctx {
    struct gf2_chan *ch; struct gf3_ctx *accel; struct gf4_state *state;
    const struct gf4_data_ops *data; const struct gf4_exec_ops *exec;
    const struct gf4_route *routes; u32 nroutes,epoch,deadline; i32 last_rc;
    bool ready,accepting,reusable;
    struct gf_vram_reservation_node scan[GF4_HEADS][2],cursor[GF4_HEADS],test;
    struct gf_vram_view cursor_view[GF4_HEADS]; struct gf4_flip_hold hold[GF4_HEADS];
};
int gf4_modes(struct gf4_ctx *,u8,const struct gf4_mode **,u32 *);
int gf4_set_mode(struct gf4_ctx *,const struct gf4_route *,u32,const struct gf3_surface *,u32);
int gf4_ddc_read(struct gf4_ctx *,u8,u8,u8 out[128],u32);
int gf4_dpms_set(struct gf4_ctx *,u8,enum gf4_dpms,u32);
int gf4_cursor_image(struct gf4_ctx *,u8,const struct gf4_cursor *,u32);
int gf4_cursor_move(struct gf4_ctx *,u8,i32,i32);
int gf4_cursor_show(struct gf4_ctx *,u8,bool);
int gf4_vblank_wait(struct gf4_ctx *,u8,u32,u32,u32 *);
int gf4_flip(struct gf4_ctx *,u8,const struct gf3_surface *,bool,u32);
int gf4_gamma(struct gf4_ctx *,u8,const u16 rgb[3][256]);
int gf4_mode_calc(const struct gf4_mode *,const struct gf4_route *,const struct gfinit_mode_saved *,const struct gfinit_pll_limits *,const struct gf3_surface *,struct gfinit_mode *);
int gf4_mode_load(struct gf4_ctx *,u8,const struct gfinit_mode *,u32);
struct gf4_test_ops {
    struct gf4_ctx *ctx; __typeof__(&gf4_set_mode) set_mode;
    __typeof__(&gf4_ddc_read) ddc_read; __typeof__(&gf4_flip) flip;
    int (*last_rc)(struct gf4_ctx *);
};
struct gf5_test_ops;
i32 gf4_display_save(struct gf4_ctx *,struct gf4_saved *,u32);
i32 gf4_display_restore(struct gf4_ctx *,const struct gf4_saved *,u32);
i32 gf4_surface_check(struct gf4_ctx *,const struct gf3_surface *,const struct gf4_mode *);
i32 gf4_route_check(struct gf4_ctx *,const struct gf4_route *);
i32 gf4_lut_load(struct gf4_ctx *,u8);
i32 gf4_heads_apply(struct gf4_ctx *,u32);
i32 gf4_timing_calc(const struct gf4_mode *,struct gfinit_mode *);
i32 gf4_format_calc(const struct gf4_mode *,const struct gf3_surface *,struct gfinit_mode *);
void gf4_fifo_calc(struct gfinit_mode *);
i32 gf4_pll_calc(const struct gf4_mode *,const struct gf4_route *,const struct gfinit_mode_saved *,const struct gfinit_pll_limits *,struct gfinit_mode *);
i32 gf4_analog_load(struct gf4_ctx *,const struct gf4_route *,const struct gfinit_mode *,u32);
i32 gf4_tmds_load(struct gf4_ctx *,const struct gf4_route *,const struct gfinit_mode *,u32);
i32 gf4_tv_load(struct gf4_ctx *,const struct gf4_route *,const struct gfinit_mode *,u32);
i32 gf4_analog_dpms(struct gf4_ctx *,const struct gf4_route *,enum gf4_dpms,u32);
i32 gf4_tmds_dpms(struct gf4_ctx *,const struct gf4_route *,enum gf4_dpms,u32);
i32 gf4_tv_dpms(struct gf4_ctx *,const struct gf4_route *,enum gf4_dpms,u32);
#endif
