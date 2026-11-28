#include "fm_module.h"
#include "fm_imports.h"
#include "platform_module_pair.h"
#include "config.h"
#include "os32_generations.h"
#include "os32_kapi_shared.h"
#include "kstring.h"
#include "kprintf.h"
#include "io.h"
#include "sys.h"
#include "platform_fm_probe.h"

static struct fm_probe_record boot_probe;

const struct fm_probe_record *fm_boot_probe(void)
{
    return &boot_probe;
}

#ifdef TICK_PROBE
#include "snd2_int.h"
#include "fm.h"

/* Only the exclusive window selects registers. The mixer read establishes
 * a known basic index without writing mixer data. */
int fm_diag_irq_setting(void)
{
    if (!boot_probe.contained || boot_probe.bank[0].model != OPN_PROBE_OPNA ||
        boot_probe.feature_port != OPNA_ID_PORT ||
        (boot_probe.feature_id >> 4 != 4 && boot_probe.feature_id >> 4 != 8))
        return OS32_ERR_NOTFOUND;

    SND2_DevInfo info;
    SND2_ChipCaps caps;
    for (u32 i = 0; !snd2_chip_dev_info(i, &info); ++i) {
        if (info.kind != SND2_KIND_FM || snd2_chip_caps(info.dev, &caps, sizeof(caps)) ||
            caps.base_port != OPN_ADDR || caps.chip != SND2_CHIP_OPNA)
            continue;
        if (platform_fm_probe_busy())
            return OS32_ERR_BUSY;
        u32 handle;
        int rc = snd2_chip_diag_open(info.dev, &handle);
        if (rc) return rc;
        rc = snd2_chip_read(handle, 0, 2, SSG_REG_MIXER);
        if (rc >= 0) {
            rc = snd2_chip_read(handle, 0, 2, 0x0e);
            /* A failed data read faults the HAL handle. Restore the known
             * address directly as soon as hardware accepts another select. */
            unsigned restore_flags = irq_save();
            unsigned n = 0;
            while (n < 10000 && (inp(OPN_STATUS) & OPN_BUSY)) ++n;
            if (n == 10000) {
                snd2_chip_fault(info.dev);
                rc = OS32_ERR_IO;
            } else {
                outp(OPN_ADDR, SSG_REG_MIXER);
                io_wait_n(6);
            }
            irq_restore(restore_flags);
        }
        int closed = snd2_chip_close(handle);
        if (closed < 0) return closed;
        return rc < 0 ? rc : (rc >> 6) & 3;
    }
    return OS32_ERR_NOTFOUND;
}
#endif

static void publish_probe_irq_policy(void)
{
    if (!boot_probe.contained) {
        platform_fm_block_irq12();
    } else if (boot_probe.complete && boot_probe.bank[0].model == OPN_PROBE_ABSENT &&
               boot_probe.bank[1].model == OPN_PROBE_ABSENT) {
        platform_fm_release_irq12();
    }
}

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
    unsigned flags = irq_save();
    candidate.probe(&boot_probe, platform_fm_probe_busy(), 0, sys_time_now);
    publish_probe_irq_policy();
    irq_restore(flags);
    kprintf(KLOG_ATTR, "[fm] 0188=%u 0288=%u (0=? 1=absent 2=OPN 3=OPNA 4=present)\n",
            boot_probe.bank[0].model, boot_probe.bank[1].model);
    return candidate.attach(snd2_chip_fault, &boot_probe);
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

static void forget_absent_module(void)
{
    struct module_record *r = module_record_get(fm_handle);
    if (!r || r->state == MODULE_ABSENT) {
        candidate_valid = 0;
        fm_handle = (struct module_handle){0};
    }
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
    if (rc) forget_absent_module();
    kprintf(KLOG_ATTR, "[fm] module rc=%d pair=%08x\n", rc, pair);
    return rc;
}

int fm_module_abort(void)
{
    int rc = module_abort(fm_handle);
    if (!rc) forget_absent_module();
    return rc;
}
