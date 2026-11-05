/* Layer 5 internal contract: GF_FEAT5_DESIGN §§2-7 and final A0 addendum;
 * GF_FEAT4_DESIGN, "A0完全共有境界" supplies the complete shared schema.
 * Cooperative foreground only. IRQ records events; it never runs F5 work. */
#ifndef HWDEV_GF_FEATURE5_H
#define HWDEV_GF_FEATURE5_H
#include <stdbool.h>
#include "../../include/types.h"
#include "display/api.h"

typedef unsigned long long u64;
typedef long long i64;
#define GF5_TILE_COUNT 12U /* nouveau c03c1a1 nvkm/subdev/fb/nv44.c:60. */
#define GF5_IMAGE_VERSION 1U
#define GF5_THERM_THRESHOLDS 4U /* fanboost, downclock, critical, shutdown. */

enum gf5_rc {
    GF5_OK=0, GF5_EINVAL=-501, GF5_ENOTSUP=-502, GF5_EBIOS=-503,
    GF5_ERANGE=-504, GF5_EBUSY=-505, GF5_ETIME=-506, GF5_EIO=-507,
    GF5_ENOMEM=-508, GF5_ESTATE=-509
};
struct gf5_clock { u32 core_khz, shader_khz, mem_khz; };
struct gf5_plan { u32 nc, nn, sc, mux, mc, mn; struct gf5_clock actual; };
struct gf5_perf_entry { struct gf5_clock clock; u8 voltage, fanspeed; };
struct gf_init_program;
/* C2-ROM owns full ROM and completed M program, independently of devinit
 * scratch. Publish only on acquisition success. M absent: NULL program/0 pc.
 * Keep through normal stop/restart, last snapshot and cleanup. */
struct gf5_rom_view {
    const u8 *bytes; u32 size;
    const struct gf_init_program *memory_program; u32 memory_pc;
};
struct gf5_tile { u32 base, bytes, pitch, reservation_id, generation; };
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
    struct gf5_image *image;
};
i32 gf5_clock_read(struct gf5 *, struct gf5_clock *);
i32 gf5_clock_calc(struct gf5 *, const struct gf5_clock *, struct gf5_plan *);
i32 gf5_clock_prog(struct gf5 *, const struct gf5_plan *);
i32 gf5_ram_prog(struct gf5 *, const struct gf5_plan *);
i32 gf5_perf_count(struct gf5 *, u32 *);
i32 gf5_perf_get(struct gf5 *, u32 index, struct gf5_perf_entry *);
i32 gf5_perf_select(struct gf5 *, u8 index);
i32 gf5_therm_read(struct gf5 *, struct gf5_thermal *);
/* N/nvkm/subdev/therm/base.c:376–394: sensor setup, saved duty/mode,
 * then sensor/fan deadlines from clock64. Copy into the existing trip array;
 * ntrip mismatch is ESTATE. No allocation or trip ownership transfer. */
i32 gf5_therm_resume(struct gf5 *, const struct gf5_fan_saved *saved);
i32 gf5_fan_mode_set(struct gf5 *, enum gf5_fan_mode);
i32 gf5_fan_get(struct gf5 *, u32 *percent);
i32 gf5_fan_cstate(struct gf5 *, u8 percent, i32 dir);
void gf5_fan_tick(struct gf5 *, u64 now_us);
i32 gf5_volt_get(struct gf5 *, u32 *requested_uv);
i32 gf5_volt_set(struct gf5 *, u32 requested_uv);
i32 gf5_volt_id(struct gf5 *, u8 id, u8 min_id, i32 condition);
i32 gf5_gpio_find(struct gf5 *, u8 tag, u8 *line, u8 log[2]);
i32 gf5_gpio_get(struct gf5 *, u8 line, const u8 log[2], bool *);
i32 gf5_gpio_set(struct gf5 *, u8 line, const u8 log[2], bool);
/* Caller reserves the entire rounded tile range; clear never releases it. */
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
