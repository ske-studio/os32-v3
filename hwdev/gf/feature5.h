/* Layer 5 internal contract: GF_FEAT5_DESIGN §§2-7 and final A0 addendum;
 * GF_FEAT4_DESIGN, "A0完全共有境界" supplies the complete shared schema.
 * Cooperative foreground only. IRQ records events; it never runs F5 work. */
#ifndef HWDEV_GF_FEATURE5_H
#define HWDEV_GF_FEATURE5_H
#include <stdbool.h>
#include "../../include/types.h"
#include "display/api.h"
#include "../../kdebug/gfinit_pll.h"

typedef unsigned long long u64;
typedef long long i64;
#define GF5_TILE_COUNT 12U /* nouveau c03c1a1 nvkm/subdev/fb/nv44.c:60. */
#define GF5_IMAGE_VERSION 2U
#define GF5_THERM_THRESHOLDS 4U /* fanboost, downclock, critical, shutdown. */
#define GF5_SENSOR_INTERVAL_US 1000000U /* Guard design §5: one second. */
#define GF5_HEAT_COOL_SAMPLES 3U /* Guard design §6: consecutive cool samples. */

enum gf5_rc {
    GF5_OK=0, GF5_EINVAL=-501, GF5_ENOTSUP=-502, GF5_EBIOS=-503,
    GF5_ERANGE=-504, GF5_EBUSY=-505, GF5_ETIME=-506, GF5_EIO=-507,
    GF5_ENOMEM=-508, GF5_ESTATE=-509,
    GF5_ENODEV=-510 /* Decoded table/wiring confirms component absence only. */
};
struct gf5_clock { u32 core_khz, shader_khz, mem_khz; };
struct gf5_plan { u32 nc, nn, sc, mux, mc, mn; struct gf5_clock actual; };
/* Private provenance, owned by gf5; the public nine-u32 plan stays unchanged.
 * Check before MMIO/pause, consume each domain before its first side effect.
 * RAM then core is allowed once each; failed programming needs a fresh calc. */
struct gf5_plan_guard {
    struct gf5_clock request;
    struct gf5_plan canonical;
    u32 epoch, rom_generation;
    bool valid, core_used, ram_used;
};
struct gf5_perf_entry {
    struct gf5_clock clock;
    u8 voltage, fanspeed, pstate, bios_index;
};
/* Live protection state, never serialized in gf5_image or rolled back by
 * restore. The resident backend separately retains the shutdown latch. */
enum gf5_stop_phase { GF5_STOP_NORMAL, GF5_STOP_REQUESTED, GF5_STOPPED };
struct gf5_stop_state {
    enum gf5_stop_phase state;
    i32 reason, milli_c, source_rc;
};
struct gf5_heat_state {
    bool down_active, critical_paused;
    u32 cool_samples, epoch;
    struct gf5_clock entry_clock, ceiling;
};
struct gf_init_program;
/* C2-ROM owns full ROM and completed M program, independently of devinit
 * scratch. Publish only on acquisition success. M absent: NULL program/0 pc.
 * Keep through normal stop/restart, last snapshot and cleanup. */
struct gf5_rom_view {
    const u8 *bytes; u32 size;
    const struct gf_init_program *memory_program; u32 memory_pc;
};
struct gf5_tile { u32 base, bytes, pitch, reservation_id, generation; };
/* Guard lifetime design section 4: nodes stay resident across replacement,
 * save/restart/restore and failed I/O. current selects the committed loan;
 * the other loan retains the replacement until all programming succeeds. */
enum gf5_tile_owner { GF_TILE_FREE, GF_TILE_OWNED, GF_TILE_UNKNOWN };
struct gf5_tile_slot {
    enum gf5_tile_owner state;
    u32 raw_addr, raw_limit, raw_pitch;
    struct gf_vram_borrow loan[2];
    u32 current;
};
/* Saved values only: never copy intrusive nodes or borrowed pointers.
 * UNKNOWN cannot be saved. Match retained reservation keys on restore. */
struct gf5_tile_saved {
    enum gf5_tile_owner state;
    u32 raw_addr, raw_limit, raw_pitch;
};
struct gf5_thermal { u32 raw; i32 milli_c; bool calibrated; };
enum gf5_fan_mode { GF5_FAN_NONE, GF5_FAN_MANUAL, GF5_FAN_AUTO };
enum gf5_fan_kind { GF5_FAN_TRIP, GF5_FAN_LINEAR, GF5_FAN_OTHER };
enum gf5_fan_method { GF5_FAN_METHOD_NONE, GF5_FAN_PWM, GF5_FAN_TOGGLE };
struct gf5_fan_trip { i32 temp, hyst; u32 duty; };
/* Temperatures in saved policy are Celsius. calibrated means ROM conversion
 * is possible, not an external measurement. trip is dynamically owned. */
struct gf5_fan_saved {
    enum gf5_fan_mode mode;
    u32 kind, method, min_duty, max_duty, freq, divisor, target, cstate, duty;
    u32 bump_ms, slow_ms;
    i32 last_trip, linear_min, linear_max;
    u32 ntrip; struct gf5_fan_trip *trip;
    i32 threshold[GF5_THERM_THRESHOLDS], hyst[GF5_THERM_THRESHOLDS];
    bool active[GF5_THERM_THRESHOLDS], sensor_poll, toggle_on;
    u8 gpio_line, gpio_log[2];
    i32 slope_mult, slope_div, offset_num, offset_den, offset_constant;
    u32 sensor_setup;
};
struct gf5_emergency_ops {
    void (*downclock)(void *, bool); void (*pause)(void *, bool); void *owner;
};
struct gf5_vid_ops {
    i32 (*get)(void *, u8 *vid); i32 (*set)(void *, u8 vid); void *owner;
};
struct gf5_extent { struct gf_vram_reservation key; u8 *host_copy; };
/* Caller owns image/trip/extent/copies; gf5 borrows even after failure.
 * bytes includes sizeof metadata, deep trip copy, extent array and images;
 * no fixed snapshot ceiling. key.owner is only an identity token: no node,
 * map VA, staging or GPU pointer is serialized. Match retained reservations
 * on restore; never re-reserve. complete follows all successful copies. */
struct gf5_image {
    u32 version, bytes, nextent, saved_epoch;
    struct gf5_clock clock; u32 requested_uv; u8 vid;
    struct gf5_fan_saved fan;
    struct gf5_tile tile[GF5_TILE_COUNT]; bool tile_live[GF5_TILE_COUNT];
    struct gf5_tile_saved tile_saved[GF5_TILE_COUNT];
    struct gf4_saved display;
    struct gf5_extent *extent; bool complete;
};
struct gf5;
/* C2-links owns the resident table; owner borrows the same production gf5.
 * NULL owner: EINVAL. Lower rc is retained by adapters, mapped at public
 * exits, with original value in source_rc. drain_pause closes admission,
 * drains using gf5.deadline, then F2 pause (its own 2s) and GR idle.
 * unpause checks void resume's last_rc/state/fault and returns failure.
 * RAM/tile/save have their own closed intervals; core/volt/fan do not pause. */
struct gf5_links {
    i32 (*drain_pause)(void *);
    i32 (*unpause)(void *);
    i32 (*display_save)(void *, struct gf5_image *);
    i32 (*display_restore)(void *, const struct gf5_image *);
    i32 (*memory_script)(void *, u32 pc);
    void *owner;
    i32 (*admission)(void *, bool open);
    i32 (*drain)(void *, u32 dl);
    i32 (*stop)(void *, u32 dl);
    i32 (*restart)(void *, u32 dl);
    i32 (*copy_extent)(void *, const struct gf_vram_reservation *, u8 *, bool);
    i32 (*alloc)(void *, u32 bytes, void **out);
    void (*free)(void *, void *);
    i32 (*clock64)(void *, u64 *);
};
enum gf5_phase { GF5_RUN, GF5_SAVING, GF5_SAVED, GF5_RESTORING, GF5_FAULT };
/* production owns this immovable context, borrowing existing F2/F3/F4.
 * deadline is set once per outer operation, including nested perf/restore.
 * All fan deadlines come from the full hi/lo clock, never extended low32. */
struct gf5 {
    struct gf2_chan *ch; struct gf3_ctx *accel; struct gf4_ctx *display;
    struct gf5_links links; struct gf5_rom_view rom;
    struct gf5_vid_ops vid; struct gf5_emergency_ops emergency;
    enum gf5_phase phase; u32 deadline; i32 last_rc, source_rc;
    bool accepting, busy;
    bool fan_off, poll_off;
    const char *poll_source; /* Last foreground failure boundary, raw rc in source_rc. */
    struct gf5_fan_saved fan;
    u64 next_auto, next_step, next_sensor, next_toggle;
    struct gf5_tile tile[GF5_TILE_COUNT]; bool tile_live[GF5_TILE_COUNT];
    struct gf5_tile_slot tile_slot[GF5_TILE_COUNT];
    struct gf5_image *image;
    /* Zero starts invalid/inactive. ROM acquisition/release changes generation;
     * ROM/stop/save/FAULT invalidate the plan. Epoch is the live F2 epoch. */
    struct gf5_plan_guard plan_guard;
    u32 rom_generation;
    struct gf5_heat_state heat;
    struct gf5_stop_state *stop; /* Resident, never saved or reset by restart. */
    i32 temperature; /* Latest sample, distinct from the first stop cause. */
};
static inline bool gf5_stopping(const struct gf5 *g)
{
    return g->stop && g->stop->state!=GF5_STOP_NORMAL;
}
static inline void gf5_stop_request(struct gf5 *g,i32 reason,i32 temperature,i32 source)
{
    if (!g->stop || gf5_stopping(g)) return;
    *g->stop=(struct gf5_stop_state){GF5_STOP_REQUESTED,reason,temperature,source};
    g->accepting=false;
    g->plan_guard.valid=false;
}
static inline bool gf5_above_ceiling(const struct gf5 *g,const struct gf5_clock *c)
{
    return g->heat.down_active &&
        (c->core_khz>g->heat.ceiling.core_khz ||
         (c->shader_khz ? c->shader_khz : c->core_khz)>g->heat.ceiling.shader_khz ||
         c->mem_khz>g->heat.ceiling.mem_khz);
}
/* MIT clk/nv40.c:39-78: decode both stages, never trust calc's actual. */
static inline i32 gf5_plan_ceiling(const struct gf5 *g,const struct gf5_plan *p)
{
    if (!g->heat.down_active) return 0;
    struct gf5_clock decoded={0};
    const u32 control[2]={p->nc,p->mc}, coefficient[2]={p->nn,p->mn};
    u32 khz[2];
    if ((p->mux&0x33U)!=0x33U) return GF5_ERANGE;
    for (u32 i=0;i<2;++i) {
        u32 c=control[i],v=coefficient[i],m=v&255U,n=(v>>8)&255U;
        if (!(c&0x80000000U) || !m || !n) return GF5_ERANGE;
        khz[i]=GF_PLL_REF*n/m;
        if ((c&0x40000100U)==0x40000000U) {
            m=(v>>16)&255U; n=(v>>24)&255U;
            if (!m || !n) return GF5_ERANGE;
            khz[i]=khz[i]*n/m;
        }
        khz[i]>>=(c>>16)&7U;
    }
    decoded.core_khz=decoded.shader_khz=khz[0]; decoded.mem_khz=khz[1];
    return gf5_above_ceiling(g,&decoded) ? GF5_EBUSY : 0;
}
i32 gf5_clock_read(struct gf5 *, struct gf5_clock *);
i32 gf5_clock_calc(struct gf5 *, const struct gf5_clock *, struct gf5_plan *);
i32 gf5_clock_prog(struct gf5 *, const struct gf5_plan *);
i32 gf5_ram_prog(struct gf5 *, const struct gf5_plan *);
/* Internal clock.c boundaries; no public facade slots. Invalid/stale/used:
 * ESTATE, altered plan: EINVAL, re-solve failure: EBIOS/ERANGE. */
i32 gf5_plan_check(struct gf5 *, const struct gf5_plan *, bool ram);
void gf5_plan_invalidate(struct gf5 *);
/* count/get/select share dense valid indices; bios_index is diagnostic only. */
i32 gf5_perf_count(struct gf5 *, u32 *);
i32 gf5_perf_get(struct gf5 *, u32 index, struct gf5_perf_entry *);
i32 gf5_perf_select(struct gf5 *, u8 index);
/* perf.c: every successful one-second sample, including active downclock.
 * Shutdown wins; critical defers recovery. Temperatures are milli-Celsius. */
i32 gf5_heat_step(struct gf5 *, const struct gf5_thermal *, u64 now);
i32 gf5_therm_read(struct gf5 *, struct gf5_thermal *);
i32 gf5_therm_monitor_restart(struct gf5 *);
/* N/nvkm/subdev/therm/base.c:376–394: sensor setup, saved duty/mode,
 * then sensor/fan deadlines from clock64. Copy into the existing trip array;
 * ntrip mismatch is ESTATE. No allocation or trip ownership transfer. */
i32 gf5_therm_resume(struct gf5 *, const struct gf5_fan_saved *saved);
i32 gf5_fan_mode_set(struct gf5 *, enum gf5_fan_mode);
i32 gf5_fan_get(struct gf5 *, u32 *percent);
i32 gf5_fan_cstate(struct gf5 *, u8 percent, i32 dir);
/* Preflight table/request support, without MMIO writes. Only ENODEV is
 * optional; unsupported existing components are ENOTSUP, damage EBIOS. */
i32 gf5_fan_check_cstate(struct gf5 *, u8 percent);
void gf5_fan_tick(struct gf5 *, u64 now_us);
i32 gf5_volt_get(struct gf5 *, u32 *requested_uv);
i32 gf5_volt_set(struct gf5 *, u32 requested_uv);
i32 gf5_volt_id(struct gf5 *, u8 id, u8 min_id, i32 condition);
i32 gf5_volt_check_id(struct gf5 *, u8 id, u8 min_id);
i32 gf5_gpio_find(struct gf5 *, u8 tag, u8 *line, u8 log[2]);
i32 gf5_gpio_get(struct gf5 *, u8 line, const u8 log[2], bool *);
i32 gf5_gpio_set(struct gf5 *, u8 line, const u8 log[2], bool);
/* Caller reserves the entire rounded tile range; clear never releases it. */
/* Binding alone does not initialize hardware. After devinit, before F2
 * start/reservations/admission, zero all 12 PFB slots and flush; publish
 * FREE only on complete success, otherwise UNKNOWN with admission closed.
 * Retry only with GR stopped and no loans. Recover keeps OWNED PFB values. */
i32 gf5_tiles_init(struct gf5 *);
/* After windows close, F2 OFF and display quiesce, before release_all:
 * disable PFB/GR without drain_pause. Failed slot stays UNKNOWN with both
 * loans; successful slot puts loans then becomes FREE. End F5 save/free
 * before retire even when SAVED (final R2-2); OWNED never crosses leave. */
i32 gf5_tiles_retire(struct gf5 *);
i32 gf5_tile_find(struct gf5 *, u32 *slot);
i32 gf5_tile_set(struct gf5 *, u32 slot, const struct gf5_tile *);
i32 gf5_tile_clear(struct gf5 *, u32 slot);
i32 gf5_state_save(struct gf5 *, struct gf5_image **out);
i32 gf5_state_restore(struct gf5 *, const struct gf5_image *);
void gf5_state_free(struct gf5 *, struct gf5_image *);

i32 gf5_rom_acquire(struct gf5 *, const u8 *, u32, const struct gf_init_program *);
void gf5_rom_release(struct gf5 *);
i32 gf5_rc_map(i32 lower);
struct gf5_links *gf5_links_table(void);
i32 gf5_link_drain_pause(void *owner);
i32 gf5_link_unpause(void *owner);
i32 gf5_link_display_save(void *owner, struct gf5_image *);
i32 gf5_link_display_restore(void *owner, const struct gf5_image *);
i32 gf5_link_memory_script(void *owner, u32 pc);
i32 gf5_link_stop(void *owner, u32 dl);
i32 gf5_link_restart(void *owner, u32 dl);
i32 gf5_link_admission(void *owner, bool open);
i32 gf5_link_drain(void *owner, u32 dl);
i32 gf5_link_copy_extent(void *owner, const struct gf_vram_reservation *, u8 *, bool);
i32 gf5_link_alloc(void *owner, u32 bytes, void **out);
void gf5_link_free(void *owner, void *allocation);
i32 gf5_link_clock64(void *owner, u64 *us);
struct gf5_test_ops;
struct gf_production;
i32 gf5_service_init(struct gf_production *, struct gf4_ctx *, const struct gf5_test_ops **slot);
/* Foreground HAL operations and diagnostic waits only. busy/non-RUN is a
 * no-op; clock64 -> fan_tick. Never hook IRQ/geforce_tick/kernel idle. */
i32 gf5_service_poll(struct gf5 *);
/* Reread module slot per dispatch. SAVING/RESTORING: NULL. SAVED: only
 * restore/free/last_rc allowed. FAULT retains management while resources
 * remain. F3/F4 rendering stays closed until final successful RUN. */
struct gf5_test_ops {
    struct gf5 *ctx;
    __typeof__(&gf5_clock_read) clock_read;
    __typeof__(&gf5_clock_calc) clock_calc;
    __typeof__(&gf5_clock_prog) clock_prog;
    __typeof__(&gf5_ram_prog) ram_prog;
    __typeof__(&gf5_perf_count) perf_count;
    __typeof__(&gf5_perf_get) perf_get;
    __typeof__(&gf5_perf_select) perf_select;
    __typeof__(&gf5_therm_read) therm_read;
    __typeof__(&gf5_fan_mode_set) fan_mode_set;
    __typeof__(&gf5_fan_get) fan_get;
    __typeof__(&gf5_fan_cstate) fan_cstate;
    __typeof__(&gf5_fan_tick) fan_tick;
    __typeof__(&gf5_volt_get) volt_get;
    __typeof__(&gf5_volt_set) volt_set;
    __typeof__(&gf5_volt_id) volt_id;
    __typeof__(&gf5_gpio_find) gpio_find;
    __typeof__(&gf5_gpio_get) gpio_get;
    __typeof__(&gf5_gpio_set) gpio_set;
    __typeof__(&gf5_tile_find) tile_find;
    __typeof__(&gf5_tile_set) tile_set;
    __typeof__(&gf5_tile_clear) tile_clear;
    __typeof__(&gf5_state_save) state_save;
    __typeof__(&gf5_state_restore) state_restore;
    __typeof__(&gf5_state_free) state_free;
    __typeof__(&gf5_service_poll) service_poll;
    i32 (*last_rc)(struct gf5 *);
};
#endif
