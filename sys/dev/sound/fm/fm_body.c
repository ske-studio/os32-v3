#include "fm_module.h"

#include "opn_chip.h"
#include "fm.h"
#include "pcm_cs4231.h"

static struct opn_chip chip;
static int device = -1;
static void (*report_fault)(u32);
static int contained = 1;

extern void (*fm_write_failed)(void);
extern int (*fm_read_mixer)(void);

static int read_mixer(void)
{
    return opn_chip_backend.read(&chip, 0, 2, SSG_REG_MIXER);
}

static void write_failed(void)
{
    if (device >= 0)
        report_fault(device);
}

static int attach(void (*fault)(u32), const struct fm_probe_record *record)
{
    contained = record->contained;
    if (!contained) return -1;
    chip.model = record->native_model;
    if (chip.model != OPN_PROBE_OPNA && chip.model != OPN_PROBE_OPN)
        return contained ? 0 : -1;
    chip.shared_irq = PCM_IRQ == 12;
    SND2_DevInfo info = {.kind = SND2_KIND_FM, .driver = "fm", .name = "OPNA"};
    if (chip.model == OPN_PROBE_OPN)
        info.name[3] = 0;
    fm_read_mixer = read_mixer;
    fm_write_failed = write_failed;
    report_fault = fault;
    device = snd2_chip_register(&opn_chip_backend, &chip, &info);
    return device < 0 ? device : 0;
}

static int detach(void)
{
    if (device >= 0 && snd2_chip_unregister(device) < 0)
        return 0;
    device = -1;
    return contained;
}

static int quiet(void)
{
    /* Unregister quiets and closes an owned chip while its board is enabled. */
    return contained;
}

int fm_module_init(FmModuleApi *out)
{
    *out = (FmModuleApi){
        .opn_write = opn_write,
        .opn_init = opn_init,
        .fm_set_tone = fm_set_tone,
        .fm_set_tone_num = fm_set_tone_num,
        .fm_note_on = fm_note_on,
        .fm_note_off = fm_note_off,
        .fm_all_off = fm_all_off,
        .ssg_tone = ssg_tone,
        .ssg_volume = ssg_volume,
        .ssg_noise = ssg_noise,
        .ssg_mixer = ssg_mixer,
        .ssg_all_off = ssg_all_off,
        .probe = opn_probe_once, .attach = attach, .detach = detach, .quiet = quiet
    };
    return 0;
}

const struct {
    u32 version;
    int (*initialize)(FmModuleApi *);
} fm_module_ops = {FM_MODULE_OPS_VERSION, fm_module_init};
