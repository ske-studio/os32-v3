/* Private, paired FEP facade. KAPI signatures remain in ime.h. */
#ifndef FEP_MODULE_H
#define FEP_MODULE_H
#include "ime.h"
#include "../module/module_runtime.h"

#define FEP_MODULE_OPS_VERSION 1u
typedef struct {
    int (*boot_abort)(void);
    u32 (*boot_bytes)(void);
    void (*toggle)(void);
    int (*feed_key)(int keydata);
    void (*set_render)(void *table);
    int (*is_active)(void);
    void (*set_mode)(int mode);
    int (*get_mode)(void);
    int (*getchar)(void);
    int (*trygetchar)(void);
    int (*getkey)(void);
    int (*trygetkey)(void);
    int (*switch_dict)(int variant);
    int (*user_list)(const char *prefix, void *out, int max);
    int (*user_delete)(const char *yomi, const char *kanji);
    int (*user_export)(const char *path);
    int (*user_clear)(void);
    void (*owner_exit)(int owner);
    int (*getchar_kapi)(void);
    int (*getkey_kapi)(void);
} FepModuleApi;
#define FEP_MODULE_API_SLOTS 20u

int fep_module_load(struct module_handle sqlite);
int fep_module_abort(void);
u32 fep_module_pages(void);
#endif
