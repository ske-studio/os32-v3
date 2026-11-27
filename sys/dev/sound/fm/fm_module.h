/* Private operations shared by the resident FM adapter and fm.mod. */
#ifndef FM_MODULE_H
#define FM_MODULE_H
#include "fm_contract.h"
#include "opn_probe.h"
#include "../../../kern/module/module_runtime.h"
#define FM_MODULE_OPS_VERSION 3u
typedef struct {
    void (*opn_write)(uchar reg, uchar val);
    void (*opn_init)(void);
    void (*fm_set_tone)(int ch, const uchar *tone_data);
    void (*fm_set_tone_num)(int ch, int tone_num);
    void (*fm_note_on)(int ch, int note);
    void (*fm_note_off)(int ch);
    void (*fm_all_off)(void);
    void (*ssg_tone)(int ch, u16 period);
    void (*ssg_volume)(int ch, uchar vol);
    void (*ssg_noise)(uchar period);
    void (*ssg_mixer)(uchar mask);
    void (*ssg_all_off)(void);
    int (*quiet)(void);
    void (*probe)(struct fm_probe_record *, int, unsigned char);
    int (*attach)(void (*fault)(u32), const struct fm_probe_record *);
    int (*detach)(void);
} FmModuleApi;
#define FM_MODULE_API_SLOTS 16u
const struct fm_probe_record *fm_boot_probe(void);
int fm_module_load(void);
int fm_module_abort(void);
#endif
