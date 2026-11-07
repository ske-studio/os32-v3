/* Real foreground dispatcher + real memory-only boot_target. The boundary
 * ops model one command at a time; full hardware operations are exercised by
 * gf5_service_live_host.c. No external refs, libc or device access required. */
#define _start observe_fixture_unused_start
#include "gf5_observe_host.c"
#undef _start
static struct gf5_image images[2];
static struct gf5_clock current;
static u32 saves, restores, frees, selections, gets;
static bool ceiling, unchanged, swap_owner;
static struct gf5 other;
i32 gf5_rc_map(i32 rc) { return rc; }
static i32 diag_clock(struct gf5 *ctx, struct gf5_clock *out)
{ REQUIRE(ctx==&g); *out=current; return 0; }
static i32 diag_count(struct gf5 *ctx,u32 *out)
{ REQUIRE(ctx==&g); *out=unchanged ? 1 : 3; return 0; }
static i32 diag_entry(struct gf5 *ctx,u32 index,struct gf5_perf_entry *out)
{
    REQUIRE(ctx==&g && index<3); ++gets;
    *out=(struct gf5_perf_entry){.clock={135000,0,270000}};
    if (index) out->clock=(struct gf5_clock){108000,0,216000};
    if (swap_owner) ops.ctx=&other;
    return 0;
}
static i32 diag_select(struct gf5 *ctx,u8 index)
{
    REQUIRE(ctx==&g && index==1); ++selections;
    if (ceiling) return GF5_EBUSY;
    current=(struct gf5_clock){108000,0,216000}; return 0;
}
static i32 diag_save(struct gf5 *ctx,struct gf5_image **out)
{
    REQUIRE(ctx==&g && saves<2 && g.phase==GF5_RUN);
    *out=&images[saves++];
    **out=(struct gf5_image){.version=GF5_IMAGE_VERSION,.complete=true,
        .clock=current,.clock_available=true,.saved_epoch=saves,
        .clock_raw={saves,saves+1,saves+2,saves+3}};
    g.image=*out; g.phase=GF5_SAVED; return 0;
}
static i32 diag_restore(struct gf5 *ctx,const struct gf5_image *im)
{
    REQUIRE(ctx==&g && im==g.image && g.phase==GF5_SAVED);
    current=im->clock; g.phase=GF5_RUN; ++restores; return 0;
}
static void diag_free(struct gf5 *ctx,struct gf5_image *im)
{
    REQUIRE(ctx==&g && (im==&images[0] || im==&images[1]));
    if (g.image==im) g.image=0;
    ++frees;
}
static void arg(u32 index,u32 value)
{
    REQUIRE(!gf5_test(GF5T_ARG_LO | (index<<GF5_WIRE_INDEX_SHIFT) |
                      ((value&GF5_WIRE_HALF_MASK)<<GF5_WIRE_VALUE_SHIFT)));
    REQUIRE(!gf5_test(GF5T_ARG_HI | (index<<GF5_WIRE_INDEX_SHIFT) |
                      ((value>>GF5_WIRE_VALUE_SHIFT)<<GF5_WIRE_VALUE_SHIFT)));
}
void _start(void)
{
    g.display=&d; d.test.value=(struct gf_vram_reservation){&g,4096,8,1,7};
    g.links=(struct gf5_links){.owner=&g,.alloc=alloc_copy,.free=free_copy,.copy_extent=copy};
    ops=(struct gf5_test_ops){.ctx=&g,.clock_read=diag_clock,.perf_count=diag_count,
        .perf_get=diag_entry,.perf_select=diag_select,.state_save=diag_save,
        .state_restore=diag_restore,.state_free=diag_free,
        .state_boot_target=gf5_state_boot_target};
    current=(struct gf5_clock){135000,0,270000};
    arg(0,2); REQUIRE(gf5_test(GF5T_SAVE)==1 && !saves);
    arg(0,0); REQUIRE(!gf5_test(GF5T_SAVE));
    REQUIRE(gf5_test(GF5T_SAVE)==1 && saves==1);
    REQUIRE(!gf5_test(GF5T_RESTORE) && restores==1);
    arg(0,U32_MAX); unchanged=true;
    host_serial_reset(); REQUIRE(gf5_test(GF5T_PERF_SELECT)==77 && !selections);
    REQUIRE(host_serial_find("verification=unverified reason=no_different_clock"));
    unchanged=false; ceiling=true;
    host_serial_reset(); REQUIRE(gf5_test(GF5T_PERF_SELECT)==1 && selections==1);
    REQUIRE(host_serial_find("verification=unverified reason=thermal_ceiling"));
    ceiling=false; swap_owner=true;
    REQUIRE(gf5_test(GF5T_PERF_SELECT)==1 && selections==1);
    ops.ctx=&g; swap_owner=false; gets=0;
    host_serial_reset(); REQUIRE(!gf5_test(GF5T_PERF_SELECT));
    REQUIRE(gets==2 && selections==2);
    REQUIRE(host_serial_find("stage=before core_khz=135000 shader_khz=0 mem_khz=270000"));
    REQUIRE(host_serial_find("stage=after core_khz=108000 shader_khz=0 mem_khz=216000"));
    REQUIRE(host_serial_find("vram_crc=1070237893"));
    arg(0,1); REQUIRE(!gf5_test(GF5T_SAVE) && saves==2);
    REQUIRE(gf5_test(GF5T_PERF_SELECT)==1 && selections==2); /* SAVED */
    REQUIRE(gf5_test(GF5T_LAST_RC+1)==0);
    REQUIRE(g.image==&images[1] && images[1].saved_epoch==2);
    REQUIRE(images[1].clock.core_khz==135000 && images[1].clock_raw.nc==1);
    ops.ctx=&other; REQUIRE(gf5_test(GF5T_RESTORE)==1 && restores==1);
    ops.ctx=&g;
    REQUIRE(!gf5_test(GF5T_RESTORE) && restores==2);
    REQUIRE(host_serial_find("stage=restored core_khz=135000 shader_khz=0 mem_khz=270000"));
    REQUIRE(host_serial_find("stage=restored_observation_only"));
    arg(0,0); REQUIRE(!gf5_test(GF5T_FREE) && frees==1);
    arg(0,1); REQUIRE(!gf5_test(GF5T_FREE) && frees==2 && !g.image);
    REQUIRE(gf5_test(GF5T_LAST_RC+1)==1);
    quit(0);
}
