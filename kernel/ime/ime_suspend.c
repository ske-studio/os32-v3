/* Temporary dictionary shutdown for explicit stopped-dictionary deployment. */
#include "ime.h"
#include "kprintf.h"
#include "kstring.h"

static int suspended;
static int saved_mode;
static char saved_path[OS32_MAX_PATH];
static char saved_user_path[OS32_MAX_PATH];

int ime_dict_suspend(int on)
{
    IME_State *ime = ime_suspend_state();
    int rc;

    if (suspended && ime->dict_loaded) suspended = 0;
    if (on) {
        if (suspended || !ime->dict_loaded) return 0;
        saved_mode = ime->mode;
        kstrncpy(saved_path, ime->store.system.path, sizeof(saved_path));
        kstrncpy(saved_user_path, ime->store.user.path, sizeof(saved_user_path));
        ime_set_mode(IME_MODE_OFF);
        rc = fep_store_close(&ime->store);
        if (rc) {
            ime->dict_loaded = ime->store.system.db != NULL && !ime->store.system.isolated;
            if (ime->dict_loaded && !ime->store.user.isolated) ime_set_mode(saved_mode);
            return rc;
        }
        ime->dict_loaded = 0;
        suspended = 1;
        return 0;
    }
    if (!suspended) return 0;
    FepMeta target;
    rc = fep_meta_probe(saved_path, &target);
    if (rc) return rc;
    if (target.reserve_kb > ime->store.boot_reserve_kb) {
        kprintf(ATTR_RED, "IME: dictionary needs more memory; restart required\r\n");
        return OS32_ERR_AGAIN;
    }
    rc = fep_store_open(&ime->store, saved_path, saved_user_path);
    if (rc) {
        kprintf(ATTR_RED, "IME: Dict load failed, FEP disabled\r\n");
        return rc;
    }
    ime->dict_loaded = 1;
    suspended = 0;
    ime_set_mode(saved_mode);
    return 0;
}
