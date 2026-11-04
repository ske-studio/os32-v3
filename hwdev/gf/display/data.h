/* Pure display data contract: GF_FEAT4_DESIGN §2 and G2/N2/N3. */
#ifndef HWDEV_GF_DISPLAY_DATA_H
#define HWDEV_GF_DISPLAY_DATA_H
#include <stdbool.h>
#include "../../../include/types.h"

#define GF4_TIMINGS 32U
#define GF4_NEG_H 1U
#define GF4_NEG_V 2U
#define GF4_INTERLACE 4U
#define GF4_DBLSCAN 8U
#define GF4_DATA_VERSION 1U
#define GF4_EDID_VERSION 1U
#define GF4_TV_VERSION 1U

enum gf4_route_type { GF4_ANALOG=0, GF4_TV=1, GF4_TMDS=2, GF4_LVDS=3 };
struct gf4_timing { u32 khz; u16 hd,hs,he,ht,vd,vs,ve,vt; u32 flags; };
struct gf4_route {
    u8 connector,head,type,or_mask,i2c_index,location;
    u8 head_mask,ext_type; bool dual_link; u32 max_khz,tv_norm;
};
struct gf4_edid_state {
    u32 version,size,ntiming,preferred,extensions,next_block,min_v_hz,max_v_hz;
    u32 min_h_khz,max_h_khz,max_khz;
    bool base_valid,digital,range_valid,truncated; i32 last_rc;
    struct gf4_timing timing[GF4_TIMINGS];
};
/* nouveau dispnv04/tvnv17.h:30-51; ctv: disp.h:69, tvmodesnv17.c:406-434. */
struct gf4_tv_state {
    u32 version,size,norm; u8 enc[64];
    u32 hfilter[4][7],hfilter2[4][7],vfilter[4][7],ptv[14],ctv[38];
};
struct gf4_data_ops {
    u32 version,size;
    i32 (*edid_modes)(const u8 block[128],u32 block_no,struct gf4_edid_state *);
    i32 (*tv_state)(u32 norm,const struct gf4_timing *,struct gf4_tv_state *);
};
#endif
