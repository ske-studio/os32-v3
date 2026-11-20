#include "machcfg_contract.h"
#include "config.h"
#include "sysconfig.h"

int machcfg_load_root(void)
{
    struct machcfg values = *machcfg_get();
    int n = sysconfig_temp_begin(SYS_MACHINE_CFG);
    u8 keys = 0;

    if (n <= 0) {
        u8 result = n < 0 ? MACHCFG_FILE_BIG : MACHCFG_FILE_NONE;
        sysconfig_temp_end();
        machcfg_note_file(result, 0);
        machcfg_finish(0);
        return result;
    }

    int x = sysconfig_get_int(SYS_MACHINE_CFG, "perf.profile", -1);
    if (x >= 0 && x <= 2) {
        values.perf_request = (u8)x;
        keys |= MACHCFG_KEY_PERF;
    }
    x = sysconfig_get_int(SYS_MACHINE_CFG, "ppc7450.nap", -1);
    if (x >= 0 && x <= 1) {
        values.nap7450 = (u8)x;
        keys |= MACHCFG_KEY_NAP;
    }
    sysconfig_temp_end();
    machcfg_note_file(MACHCFG_FILE_OK, keys);
    machcfg_finish(&values);
    return MACHCFG_FILE_OK;
}
