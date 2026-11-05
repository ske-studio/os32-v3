/* Temporary dictionary shutdown for explicit stopped-dictionary deployment. */
#include "ime.h"
#include "kprintf.h"

static int suspended;
static int saved_mode;

int ime_dict_suspend(int on)
{
    IME_State *ime = ime_suspend_state();
    int rc;

    if (on) {
        if (suspended || !ime->dict_loaded) return 0;
        saved_mode = ime->mode;
        ime_set_mode(IME_MODE_OFF);
        rc = ime_dict_close(&ime->dict);
        if (rc) return rc;
        ime->dict_loaded = 0;
        suspended = 1;
        return 0;
    }
    if (!suspended) return 0;
    rc = ime_dict_open(&ime->dict, IME_DICT_PATH);
    if (rc) {
        kprintf(ATTR_RED, "IME: Dict load failed, FEP disabled\r\n");
        return rc;
    }
    ime->dict_loaded = 1;
    suspended = 0;
    ime_set_mode(saved_mode);
    return 0;
}
