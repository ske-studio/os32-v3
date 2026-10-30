/* Resident FEP adapter. The implementation and its state live in fep.mod. */
#include "fep_module.h"
#include "fep_imports.h"
#include "../sqlite_engine.h"
#include "bootinfo.h"
#include "config.h"
#include "os32_generations.h"
#include "input_service.h"
#include "con_sink.h"
#include "kstring.h"
#include "kprintf.h"
#include "../../mm/pgalloc.h"

extern void ring3_abort_check(void);
extern const u32 fep_kexports[];
extern const struct module_symbol_contract fep_import_contracts[];
extern const struct module_symbol_contract fep_export_contracts[];

int fep_services_public(void) { return services_public; }
int fep_services_stage(void) { return services_stage; }
u32 fep_services_boot_fault(void) { return services_boot_fault; }
u8 fep_kbd_shift_state(void) { return input_shift_state(); }

static void stub_void(void) {}
static void stub_mode(int mode) { (void)mode; }
static void stub_render(void *table) { (void)table; }
static int stub_zero(void) { return 0; }
static u32 stub_bytes(void) { return 0; }
/* A missing FEP leaves WM keys intact, including the no-input sentinel. */
static int stub_feed_key(int keydata) { return keydata; }
static int stub_switch(int variant) { (void)variant; return OS32_ERR_NOSYS; }
static int stub_list(const char *prefix, void *out, int max)
{ (void)prefix; (void)out; (void)max; return OS32_ERR_NOSYS; }
static int stub_delete(const char *yomi, const char *kanji)
{ (void)yomi; (void)kanji; return OS32_ERR_NOSYS; }
static int stub_export(const char *path) { (void)path; return OS32_ERR_NOSYS; }
static int stub_clear(void) { return OS32_ERR_NOSYS; }

/* Match the IME OFF byte contract, including terminal injection and STOP's
 * existing safe point. Navigation remains available through the key APIs. */
static int stub_getchar_wait(int interruptible)
{
    int c;
    do {
        c = input_getkey_wait(interruptible);
        if (c < 0 || con_sink_is_enabled()) return c;
    } while (!(c & 0xff));
    return c & 0xff;
}
static int stub_getchar(void) { return stub_getchar_wait(0); }
static int stub_getchar_kapi(void)
{
    int c = stub_getchar_wait(1);
    if (c < 0) ring3_abort_check();
    return c;
}
static int stub_trygetchar(void)
{
    if (con_sink_is_enabled()) return input_trygetchar();
    int c = input_trygetkey();
    return c < 0 || !(c & 0xff) ? -1 : c & 0xff;
}
static const FepModuleApi stub_api = {
    .boot_abort = stub_zero, .boot_bytes = stub_bytes,
    .toggle = stub_void, .feed_key = stub_feed_key, .set_render = stub_render,
    .is_active = stub_zero, .set_mode = stub_mode, .get_mode = stub_zero,
    .getchar = stub_getchar, .trygetchar = stub_trygetchar,
    .getkey = input_getkey, .trygetkey = input_trygetkey,
    .switch_dict = stub_switch, .user_list = stub_list,
    .user_delete = stub_delete, .user_export = stub_export,
    .user_clear = stub_clear, .owner_exit = stub_mode,
    .getchar_kapi = stub_getchar_kapi, .getkey_kapi = input_getkey_kapi
};
static FepModuleApi candidate;
static struct module_handle fep_handle;
static int candidate_valid, init_called;

static const FepModuleApi *public_api(void)
{
    return services_public && module_exports(fep_handle) ? &candidate : &stub_api;
}

/* Boot used to initialize directly linked state here. Module init now does
 * that once, after Unicode and SQLite are available, while exports are private. */
void ime_init(void) {}
void ime_toggle(void) { public_api()->toggle(); }
int ime_feed_key(int keydata) { return public_api()->feed_key(keydata); }
void ime_set_render(void *table) { public_api()->set_render(table); }
int ime_is_active(void) { return public_api()->is_active(); }
void ime_set_mode(int mode) { public_api()->set_mode(mode); }
int ime_get_mode(void) { return public_api()->get_mode(); }
int ime_getchar(void) { return public_api()->getchar(); }
int ime_trygetchar(void) { return public_api()->trygetchar(); }
int ime_getkey(void) { return public_api()->getkey(); }
int ime_trygetkey(void) { return public_api()->trygetkey(); }
int ime_getchar_kapi(void) { return public_api()->getchar_kapi(); }
int ime_getkey_kapi(void) { return public_api()->getkey_kapi(); }
int ime_switch_dict(int variant) { return public_api()->switch_dict(variant); }
int ime_user_list_facade(const char *prefix, void *out, int max)
{ return public_api()->user_list(prefix, out, max); }
int ime_user_delete_facade(const char *yomi, const char *kanji)
{ return public_api()->user_delete(yomi, kanji); }
int ime_user_export_facade(const char *path) { return public_api()->user_export(path); }
int ime_user_clear_facade(void) { return public_api()->user_clear(); }
void ime_owner_exit(int owner) { public_api()->owner_exit(owner); }

static int fep_start(struct module_record *r, u32 entry, u32 *exports)
{
    (void)exports;
    int (*init)(FepModuleApi *) = (int (*)(FepModuleApi *))(uptr)entry;
    init_called = 1;
    int rc = init(&candidate);
    u32 entries[FEP_MODULE_API_SLOTS];
    STATIC_ASSERT(sizeof(candidate) == sizeof(entries), fep_facade_size);
    kmemcpy(entries, &candidate, sizeof(entries));
    for (u32 i = 0; i < FEP_MODULE_API_SLOTS; i++)
        if (entries[i] < r->text || entries[i] - r->text >= r->text_size)
            return MODULE_INIT_FAILED;
    candidate_valid = 1;
    return rc;
}

static int fep_quiesce(struct module_record *r)
{
    (void)r;
    if (init_called) {
        if (!candidate_valid) return 0;
        /* No GUI is published during this boot transaction. Reset the table
         * before reclaim; normal GUI owner exit resets it before shell reuse. */
        candidate.set_render(0);
        if (candidate.boot_abort()) return 0;
    }
    candidate_valid = init_called = 0;
    kmemset(&candidate, 0, sizeof(candidate));
    return 1;
}

int fep_module_load(struct module_handle sqlite)
{
    if (fep_handle.generation) return MODULE_BUSY;
    const struct bootinfo *bi = bootinfo_get();
    const struct module_request request = {
        .path = SYS_FEP_MOD, .name = "fep",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, FEP_MODULE_ABI_GEN,
            bi->img_crc, FEP_MODULE_OPS_VERSION, bi->img_valid,
            FEP_IMPORT_COUNT, fep_import_contracts, fep_export_contracts,
            MODULE_ID_FEP
        },
        .imports = fep_kexports,
        .dependencies = &sqlite, .dependency_count = 1,
        .lifecycle = {.start = fep_start, .quiesce = fep_quiesce}
    };
    int rc = module_load(&request, &fep_handle);
    if (!rc) rc = module_start(fep_handle);
    if (!rc && !module_exports(fep_handle)) rc = MODULE_INIT_FAILED;
    kprintf(KLOG_ATTR, "[fep] module rc=%d pair=%08x pages=%u\n",
            rc, bi->img_crc, fep_module_pages());
    return rc;
}

int fep_module_abort(void) { return module_abort(fep_handle); }
u32 fep_module_pages(void)
{
    struct module_record *r = module_record_get(fep_handle);
    return r && r->state != MODULE_ABSENT && r->owner ? ledger_owner_pages(r->owner) : 0;
}
u32 ime_boot_bytes(void)
{
    /* A failed close keeps the candidate body and its SQLite dependency. */
    return candidate_valid ? candidate.boot_bytes() : 0;
}
