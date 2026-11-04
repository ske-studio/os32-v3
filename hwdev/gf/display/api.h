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
#define GF4_STATE_VERSION 2U
#define GF4_SAVED_VERSION 2U

struct gf_display_context;

enum gf4_dpms { GF4_ON, GF4_STANDBY, GF4_SUSPEND, GF4_OFF };
struct gf4_mode { u32 id; struct gf4_timing t; enum gf3_fmt fmt; u32 pitch; };
struct gf4_cursor { const u32 *argb_pm; u32 bytes,pitch,w,h; i32 hot_x,hot_y; };
struct gf4_head_image {
    bool enabled,blank,cursor_visible,lut_dirty;
    struct gf4_mode mode; struct gf4_route route;
    struct gf3_surface front,back; struct gf_vram_reservation cursor;
    i32 x,y,hot_x,hot_y; u32 cursor_w,cursor_h,fp_on;
    enum gf4_dpms dpms; u8 palette[768]; u16 gamma[3][256];
    bool cursor_valid;
};
struct gf4_flip_hold {
    bool pending,base_written; struct gf3_surface old_front,new_front; u32 after;
    bool base_verified;
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
    struct gf4_head_image next[GF4_HEADS];
    struct gfinit_mode committed[GF4_HEADS]; struct gf4_tv_state rollback_tv[GF4_HEADS];
};
struct gf4_exec_ops {
    i32 (*tmds_script)(void *,const struct gf4_route *,u32 khz,u32 dl);
    i32 (*external_mode)(void *,const struct gf4_route *,const struct gf4_mode *,u32 dl);
    void *owner;
    i32 (*external_dpms)(void *,const struct gf4_route *,enum gf4_dpms,u32 dl);
    i32 (*tv_gpio)(void *,bool on,u32 dl);
};
/* C2 owns these adapters; ctx borrows them. */
struct gf4_links {
    i32 (*i2c_port)(void *,u8 bus,struct gf4_i2c_port *);
    i32 (*admission)(void *,bool open);
    i32 (*failed)(void *,i32 cause,u32 dl);
    i32 (*scan_changed)(void *,u8 head,const struct gf3_surface *);
    void *owner;
};
struct gf4_ctx {
    struct gf2_chan *ch; struct gf3_ctx *accel; struct gf4_state *state;
    const struct gf4_data_ops *data; const struct gf4_exec_ops *exec;
    const struct gf4_route *routes; u32 nroutes,epoch,deadline; i32 last_rc;
    bool ready,accepting,reusable;
    struct gf_vram_reservation_node scan[GF4_HEADS][2],cursor[GF4_HEADS],test;
    struct gf_vram_view cursor_view[GF4_HEADS]; struct gf4_flip_hold hold[GF4_HEADS];
    const struct gf4_links *links; struct gf_display_context *legacy;
    struct gfinit_pll_limits limits[GF4_HEADS]; bool limits_valid[GF4_HEADS];
    /* F5 owns a closed save when saved_closed && diagnostic_saved == NULL. */
    bool changing,saved_closed; i32 recovery_rc; u32 next_mode_id;
    struct gf_vram_reservation_node scan_next[GF4_HEADS][2]; u8 scan_bank[GF4_HEADS];
    struct gf4_dcb_values dcb; const struct gf4_saved *diagnostic_saved;
    /* B11-MMIO (tv.c) owns: TV load-detect result, nouveau dispnv04/tvnv17.h:73-87
     * pin_mask/subconnector; valid cleared by service start (PM 2026-11-04). */
    u8 tv_pin_mask,tv_subconnector; bool tv_sense_valid;
    /* X2/N2/N3: C2-recover owns the value copy shared by recover and
     * diagnostic STOP->START. Capture head/hold/IRQ before suspend changes
     * DPMS; never use state.rollback or overwrite diagnostic_saved. */
    struct gf4_saved hold_saved;
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
i32 gf4_tv_sense(struct gf4_ctx *,const struct gf4_route *,u32 dl);
i32 gf4_analog_dpms(struct gf4_ctx *,const struct gf4_route *,enum gf4_dpms,u32);
i32 gf4_tmds_dpms(struct gf4_ctx *,const struct gf4_route *,enum gf4_dpms,u32);
i32 gf4_tv_dpms(struct gf4_ctx *,const struct gf4_route *,enum gf4_dpms,u32);
/* First-wave additions: GF_FEAT4_DESIGN, final 2026-11-04 addendum. */
i32 gf4_detect(struct gf4_ctx *,u32 dl);
i32 gf4_pllsel_calc(const struct gf4_head_image heads[GF4_HEADS],u32 *out);
i32 gf4_change_begin(struct gf4_ctx *,u32 dl);
i32 gf4_change_end(struct gf4_ctx *,i32 operation_rc);
i32 gf4_blank_set(struct gf4_ctx *,u8 head,bool blank,u32 dl);
i32 gf4_cursor_apply(struct gf4_ctx *,u8 head,bool visible);
i32 gf4_cursor_close(void *owner,struct gf_vram_view *);
i32 gf4_flip_complete(struct gf4_ctx *,u8 head,u32 dl);
i32 gf4_palette_set(struct gf4_ctx *,u8 head,u32 first,u32 count,const u8 *rgb);
i32 gf4_dac_set(struct gf4_ctx *,const struct gf4_route *,bool on);
i32 gf4_scan_prepare(struct gf4_ctx *,u8 head,u32 mode_id,struct gf3_surface out[2],u32 dl);
i32 gf4_scan_discard(struct gf4_ctx *,u8 head,u32 dl);
/* C2-scan owns: first reservation of scan/cursor/test at service start, and
 * return of every display reservation after the display stopped (X1/X3). */
i32 gf4_scan_reserve_initial(struct gf4_ctx *,u32 dl);
i32 gf4_scan_release_all(struct gf4_ctx *,u32 dl);
/* C2-service owns: the resident owner table lent to F3 until module exit;
 * C3 passes it to gf3_service_owner_bind (X3, PM 2026-11-04). */
struct gf3_service_owner;
const struct gf3_service_owner *gf4_service_owner_table(void);
i32 gf4_service_bind(struct gf4_ctx *,struct gf_display_context *,const struct gf4_links *);
i32 gf4_service_start(struct gf4_ctx *,u32 dl);
i32 gf4_service_stop(struct gf4_ctx *,u32 dl);
/* X2: suspend keeps reservations, state, snapshots and reciprocal loans;
 * close/fini(dl)/F2 stop/OFF precede invalidate and foundation cleanup.
 * Restart remains unpublished through objects_init and retained-surface
 * match/rebind; it changes neither saved_closed nor diagnostic_saved.
 * Required NULL/missing links: EINVAL; invalid state: ESTATE; changing:
 * EBUSY. Preserve lower rc, including OFF with a negative stop rc.
 * X4: pass the original dl; existing lower F2 deadlines stay unchanged. */
i32 gf4_service_suspend(struct gf4_ctx *,u32 dl);
i32 gf4_service_restart(struct gf4_ctx *,u32 dl);
i32 gf4_service_recover(struct gf4_ctx *,u32 dl);
i32 gf4_diagnostic_save(struct gf4_ctx *,struct gf4_saved *,u32 dl);
i32 gf4_diagnostic_restore(struct gf4_ctx *,const struct gf4_saved *,u32 dl);
i32 gf4_display_saved_release(struct gf4_ctx *,const struct gf4_saved *);
/* Kernel-only C4 binding; clear only after a complete stop. */
i32 gf4_data_bind(const struct gf4_data_ops **,struct gf4_state **);
struct gf4_restore_ops {
    struct gf4_ctx *ctx;
    i32 (*restore)(struct gf4_ctx *,const struct gf4_saved *,u32 dl);
    i32 (*last_rc)(struct gf4_ctx *);
    i32 (*saved_release)(struct gf4_ctx *,const struct gf4_saved *);
};
struct gf4_test_ops {
    struct gf4_ctx *ctx; __typeof__(&gf4_set_mode) set_mode;
    __typeof__(&gf4_ddc_read) ddc_read; __typeof__(&gf4_flip) flip;
    int (*last_rc)(struct gf4_ctx *);
    /* display_save binds diagnostic_save; restore uses the management slot. */
    __typeof__(&gf4_modes) modes;
    __typeof__(&gf4_detect) detect;
    __typeof__(&gf4_dpms_set) dpms_set;
    __typeof__(&gf4_cursor_image) cursor_image;
    __typeof__(&gf4_cursor_move) cursor_move;
    __typeof__(&gf4_cursor_show) cursor_show;
    __typeof__(&gf4_vblank_wait) vblank_wait;
    __typeof__(&gf4_gamma) gamma;
    __typeof__(&gf4_display_save) display_save;
    __typeof__(&gf4_palette_set) palette_set;
    __typeof__(&gf4_scan_prepare) scan_prepare;
    __typeof__(&gf4_scan_discard) scan_discard;
};
#endif
