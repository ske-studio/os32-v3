/* Paired POST_ROOT FEP body. Initialization stays private to services_boot. */
#include "fep_module.h"

static const FepModuleApi api = {
    .boot_abort = ime_boot_abort,
    .boot_bytes = ime_boot_bytes,
    .toggle = ime_toggle,
    .feed_key = ime_feed_key,
    .set_render = ime_set_render,
    .is_active = ime_is_active,
    .set_mode = ime_set_mode,
    .get_mode = ime_get_mode,
    .getchar = ime_getchar,
    .trygetchar = ime_trygetchar,
    .getkey = ime_getkey,
    .trygetkey = ime_trygetkey,
    .switch_dict = ime_switch_dict,
    .user_list = ime_user_list_facade,
    .user_delete = ime_user_delete_facade,
    .user_export = ime_user_export_facade,
    .user_clear = ime_user_clear_facade,
    .owner_exit = ime_owner_exit,
    .getchar_kapi = ime_getchar_kapi,
    .getkey_kapi = ime_getkey_kapi,
};

int fep_module_init(FepModuleApi *out)
{
    *out = api;
    ime_init();
    return ime_dict_boot();
}

const struct {
    u32 version;
    int (*initialize)(FepModuleApi *);
} fep_module_ops = {FEP_MODULE_OPS_VERSION, fep_module_init};
