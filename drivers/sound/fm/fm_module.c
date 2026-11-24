#include "fm_module.h"
#include "fm_imports.h"
#include "platform_module_pair.h"
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
extern int res_owner_get(void);

static int legacy_ready(void)
{
    return module_exports(fm_handle) && snd2_chip_implicit(res_owner_get()) == 0;
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
    if (legacy_ready()) candidate.fm_set_tone_num(ch, tone_num);
}
void fm_note_on(int ch, int note)
{
    if (legacy_ready()) candidate.fm_note_on(ch, note);
}
void fm_note_off(int ch)
{
    if (legacy_ready()) candidate.fm_note_off(ch);
}
void fm_all_off(void)
{
    if (module_exports(fm_handle)) candidate.fm_all_off();
}
void ssg_tone(int ch, u16 period)
{
    if (legacy_ready()) candidate.ssg_tone(ch, period);
}
void ssg_volume(int ch, uchar vol)
{
    if (legacy_ready()) candidate.ssg_volume(ch, vol);
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
    if (legacy_ready()) candidate.ssg_all_off();
}
static int fm_start(struct module_record *r, u32 entry, u32 *exports)
{
    (void)exports;
    int (*init)(FmModuleApi *) = (int (*)(FmModuleApi *))(uptr)entry;
    candidate_valid = 0;
    int rc = init(&candidate);
    if (rc) return rc;
    u32 entries[FM_MODULE_API_SLOTS];
    STATIC_ASSERT(sizeof(candidate) == sizeof(entries), fm_facade_size);
    kmemcpy(entries, &candidate, sizeof(entries));
    for (u32 i = 0; i < FM_MODULE_API_SLOTS; i++)
        if (!module_text_contains(r, entries[i]))
            return MODULE_INIT_FAILED;
    candidate_valid = 1;
    return candidate.attach(snd2_chip_fault);
}

static int fm_quiesce(struct module_record *r)
{
    (void)r;
    return candidate_valid && candidate.quiet();
}

static int fm_detach(struct module_record *r)
{
    (void)r;
    return !candidate_valid || candidate.detach();
}

int fm_module_load(void)
{
    if (fm_handle.generation) return MODULE_BUSY;
    u32 pair = 0;
    int pair_valid = platform_module_pair(&pair);
    const struct module_request request = {
        .path = SYS_FM_MOD, .name = "fm",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, FM_MODULE_ABI_GEN,
            pair, FM_MODULE_OPS_VERSION, pair_valid,
            FM_IMPORT_COUNT, fm_import_contracts, fm_export_contracts,
            MODULE_ID_FM
        },
        .imports = fm_kexports,
        .lifecycle = {.start = fm_start, .quiesce = fm_quiesce, .detach = fm_detach}
    };
    int rc = module_load(&request, &fm_handle);
    if (!rc) rc = module_start(fm_handle);
    if (!rc && !module_exports(fm_handle)) rc = MODULE_INIT_FAILED;
    kprintf(KLOG_ATTR, "[fm] module rc=%d pair=%08x\n", rc, pair);
    return rc;
}

int fm_module_abort(void) { return module_abort(fm_handle); }
