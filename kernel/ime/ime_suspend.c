/* Temporary dictionary shutdown for explicit stopped-dictionary deployment. */
#include "ime.h"
#include "kprintf.h"
#include "kstring.h"

static int suspended;
static int saved_mode;
static char saved_path[sizeof(((IME_Dict *)0)->path)];

int ime_dict_suspend(int on)
{
    IME_State *ime = ime_suspend_state();
    int rc;

    if (suspended && ime->dict_loaded) suspended = 0;
    if (on) {
        if (suspended || !ime->dict_loaded) return 0;
        saved_mode = ime->mode;
        kstrncpy(saved_path, ime->dict.path, sizeof(saved_path));
        ime_set_mode(IME_MODE_OFF);
        rc = ime_dict_close(&ime->dict);
        if (rc) {
            if (!ime->dict.isolated) ime_set_mode(saved_mode);
            return rc;
        }
        ime->dict_loaded = 0;
        suspended = 1;
        return 0;
    }
    if (!suspended) return 0;
    rc = ime_dict_open(&ime->dict, saved_path);
    if (rc) {
        kprintf(ATTR_RED, "IME: Dict load failed, FEP disabled\r\n");
        return rc;
    }
    ime->dict_loaded = 1;
    suspended = 0;
    ime_set_mode(saved_mode);
    return 0;
}
