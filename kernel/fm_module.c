#include "tick.h"
#include "tick_order.h"
_Static_assert(PC98_TICK_COUNT <= TICK_SLOT_COUNT, "PC98 tick slots exceed table");
/* Resident facade and tick registration; all playback state is in fm.mod. */
#include "fm_module.h"
#include "fm_imports.h"
#include "bootinfo.h"
#include "config.h"
#include "os32_generations.h"
#include "os32_kapi_shared.h"
#include "kstring.h"
#include "kprintf.h"
#include "io.h"

extern const u32 fm_kexports[];
extern const struct module_symbol_contract fm_import_contracts[];
extern const struct module_symbol_contract fm_export_contracts[];

static struct module_handle fm_handle;
static FmModuleApi candidate;
static int candidate_valid;
static void (*tick_target)(void);

/* IRQ0 may dispatch a STARTING registration, independently of publication.
 * Registration/removal run in boot context; IF=0 drains the single CPU tick. */
void snd_tick(void)
{
    if (tick_target) tick_target();
}

void opn_write(uchar reg, uchar val)
{
    if (module_exports(fm_handle)) candidate.opn_write(reg, val);
}
void opn_init(void)
{
    if (module_exports(fm_handle)) candidate.opn_init();
}
void fm_set_tone(int ch, const uchar *tone_data)
{
    if (module_exports(fm_handle)) candidate.fm_set_tone(ch, tone_data);
}
void fm_set_tone_num(int ch, int tone_num)
{
    if (module_exports(fm_handle)) candidate.fm_set_tone_num(ch, tone_num);
}
void fm_note_on(int ch, int note)
{
    if (module_exports(fm_handle)) candidate.fm_note_on(ch, note);
}
void fm_note_off(int ch)
{
    if (module_exports(fm_handle)) candidate.fm_note_off(ch);
}
void fm_all_off(void)
{
    if (module_exports(fm_handle)) candidate.fm_all_off();
}
void ssg_tone(int ch, u16 period)
{
    if (module_exports(fm_handle)) candidate.ssg_tone(ch, period);
}
void ssg_volume(int ch, uchar vol)
{
    if (module_exports(fm_handle)) candidate.ssg_volume(ch, vol);
}
void ssg_noise(uchar period)
{
    if (module_exports(fm_handle)) candidate.ssg_noise(period);
}
void ssg_mixer(uchar mask)
{
    if (module_exports(fm_handle)) candidate.ssg_mixer(mask);
}
void ssg_all_off(void)
{
    if (module_exports(fm_handle)) candidate.ssg_all_off();
}
void fm_startup_sound(void)
{
    if (module_exports(fm_handle)) candidate.fm_startup_sound();
}
void fm_play_mml(const char *mml)
{
    if (module_exports(fm_handle)) candidate.fm_play_mml(mml);
}
void fm_play_mml_kapi(const char *mml)
{
    if (module_exports(fm_handle)) candidate.fm_play_mml_kapi(mml);
}
void snd_bgm_play(const char *mml)
{
    if (module_exports(fm_handle)) candidate.snd_bgm_play(mml);
}
void snd_bgm_stop(void)
{
    if (module_exports(fm_handle)) candidate.snd_bgm_stop();
}
int snd_bgm_is_playing(void)
{
    if (module_exports(fm_handle)) return candidate.snd_bgm_is_playing();
    return 0;
}
void snd_se_play(int se_id)
{
    if (module_exports(fm_handle)) candidate.snd_se_play(se_id);
}
void snd_se_play_raw(int note, int duration_ticks, int tone)
{
    if (module_exports(fm_handle)) candidate.snd_se_play_raw(note, duration_ticks, tone);
}
void snd_set_master(int enable)
{
    if (module_exports(fm_handle)) candidate.snd_set_master(enable);
}
void snd_cleanup(void)
{
    if (module_exports(fm_handle)) candidate.snd_cleanup();
}
void snd_bgm_set_persist(int persist)
{
    if (module_exports(fm_handle)) candidate.snd_bgm_set_persist(persist);
}
i32 snd_focus(int app_id)
{
    if (module_exports(fm_handle)) return candidate.snd_focus(app_id);
    return OS32_ERR_NOSYS;
}
int snd_focus_owner(void)
{
    if (module_exports(fm_handle)) return candidate.snd_focus_owner();
    return 0;
}
void snd_owner_exit(int owner)
{
    if (module_exports(fm_handle)) candidate.snd_owner_exit(owner);
}
int snd_bgm_get_persist(void)
{
    if (module_exports(fm_handle)) return candidate.snd_bgm_get_persist();
    return 0;
}

static int fm_start(struct module_record *r, u32 entry, u32 *exports)
{
    (void)exports;
    int (*init)(FmModuleApi *) = (int (*)(FmModuleApi *))(uptr)entry;
    int rc = init(&candidate);
    u32 entries[FM_MODULE_API_SLOTS];
    STATIC_ASSERT(sizeof(candidate) == sizeof(entries), fm_facade_size);
    kmemcpy(entries, &candidate, sizeof(entries));
    for (u32 i = 0; i < FM_MODULE_API_SLOTS; i++)
        if (entries[i] < r->text || entries[i] - r->text >= r->text_size)
            return MODULE_INIT_FAILED;
    candidate_valid = 1;
    if (rc) return rc;
    unsigned int flags = irq_save();
    int acquired = module_ref_acquire(fm_handle, r->owner, MODULE_REF_TICK,
                                      (u32)(uptr)candidate.tick);
    if (acquired) tick_target = candidate.tick;
    irq_restore(flags);
    return acquired ? 0 : MODULE_INIT_FAILED;
}

static int fm_detach(struct module_record *r)
{
    (void)r;
    unsigned int flags = irq_save();
    if (tick_target) {
        tick_target = 0;
        module_ref_release(fm_handle, MODULE_REF_TICK);
    }
    irq_restore(flags);
    return 1;
}

static int fm_quiesce(struct module_record *r)
{
    /* Prevent the next tick from issuing Key-ON after the final Key-OFF.
     * Even if the chip stays BUSY, detach first and retain the body via the
     * failed quiesce result in module_abort. */
    fm_detach(r);
    if (!candidate_valid) return 0;
    candidate.snd_set_master(0);
    return candidate.quiet();
}

int fm_module_load(void)
{
    if (tick_register(PC98_TICK_SND, snd_tick) != 0) return MODULE_INIT_FAILED;
    if (fm_handle.generation) return MODULE_BUSY;
    const struct bootinfo *bi = bootinfo_get();
    const struct module_request request = {
        .path = SYS_FM_MOD, .name = "fm",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, FM_MODULE_ABI_GEN,
            bi->img_crc, FM_MODULE_OPS_VERSION, bi->img_valid,
            FM_IMPORT_COUNT, fm_import_contracts, fm_export_contracts,
            MODULE_ID_FM
        },
        .imports = fm_kexports,
        .lifecycle = {.start = fm_start, .quiesce = fm_quiesce, .detach = fm_detach}
    };
    int rc = module_load(&request, &fm_handle);
    if (!rc) rc = module_start(fm_handle);
    if (!rc && !module_exports(fm_handle)) rc = MODULE_INIT_FAILED;
    kprintf(KLOG_ATTR, "[fm] module rc=%d pair=%08x tick=%d\n",
            rc, bi->img_crc, tick_target != 0);
    return rc;
}

int fm_module_abort(void) { return module_abort(fm_handle); }
