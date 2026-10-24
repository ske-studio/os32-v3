#include "kcg_module.h"
#include "kcg_imports.h"
#include "kcg.h"
#include "bootinfo.h"
#include "config.h"
#include "os32_generations.h"
#include "os32_kapi_shared.h"
#include "kstring.h"
#include "kprintf.h"
#include "pc98.h"
#include "io.h"

extern const u32 kcg_kexports[];
extern const struct module_symbol_contract kcg_import_contracts[];
extern const struct module_symbol_contract kcg_export_contracts[];
static struct module_handle kcg_handle;
static KcgModuleApi candidate;

/* The void glyph ABI uses blank pixels; TVRAM CUI does not need this reader. */
static void stub_ank(u8 ch, u8 *buf) { (void)ch; kmemset(buf, 0, KCG_ANK_H); }
static void stub_kanji(u16 jis, u8 *buf) { (void)jis; kmemset(buf, 0, KCG_KANJI_H * KCG_KANJI_BPL); }
static void stub_stats(KcgCacheStats *out) { kmemset(out, 0, sizeof(*out)); }
static void stub_invalidate(void) {}
static const KcgModuleApi stub_api = {stub_ank, stub_kanji, stub_stats, stub_invalidate};
static const KcgModuleApi *public_api(void)
{
    return kcg_module_ready() ? &candidate : &stub_api;
}
void kcg_rom_read_ank(u8 ch, u8 *buf) { public_api()->read_ank(ch, buf); }
void kcg_rom_read_kanji(u16 jis, u8 *buf) { public_api()->read_kanji(jis, buf); }
void kcg_cache_stats(KcgCacheStats *out) { public_api()->stats(out); }
int kcg_module_ready(void) { return module_exports(kcg_handle) != 0; }

void kcg_v86_restore(void)
{
    /* Bible §2-6-5 / UNDOCUMENTED io_disp 0068h KAC Mode.
     * The mode belongs to core; absent/failed modules retain no cache. */
    outp(MODE_FF1_PORT, MFF1_KCG_CODE);
    io_wait();
    public_api()->invalidate();
}

static int kcg_start(struct module_record *r, u32 entry, u32 *exports)
{
    (void)exports;
    int (*init)(KcgModuleApi *) = (int (*)(KcgModuleApi *))(uptr)entry;
    int rc = init(&candidate);
    if (rc) return rc;
    u32 entries[KCG_MODULE_API_SLOTS];
    STATIC_ASSERT(sizeof(candidate) == sizeof(entries), kcg_facade_size);
    kmemcpy(entries, &candidate, sizeof(entries));
    for (u32 i = 0; i < KCG_MODULE_API_SLOTS; i++)
        if (entries[i] < r->text || entries[i] - r->text >= r->text_size)
            return MODULE_INIT_FAILED;
    return 0;
}
static int kcg_detach(struct module_record *r)
{
    (void)r;
    /* Synchronous glyph reads retain no callbacks or external pointers. */
    kmemset(&candidate, 0, sizeof(candidate));
    return 1;
}
int kcg_module_load(void)
{
    if (kcg_handle.generation) return MODULE_BUSY;
    const struct bootinfo *bi = bootinfo_get();
    const struct module_request request = {
        .path = SYS_KCG_MOD, .name = "kcg",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, KCG_MODULE_ABI_GEN,
            bi->img_crc, KCG_MODULE_OPS_VERSION, bi->img_valid,
            KCG_IMPORT_COUNT, kcg_import_contracts, kcg_export_contracts,
            MODULE_ID_KCG
        },
        .imports = kcg_kexports,
        .lifecycle = {.start = kcg_start, .detach = kcg_detach}
    };
    int rc = module_load(&request, &kcg_handle);
    if (!rc) rc = module_start(kcg_handle);
    if (!rc && !module_exports(kcg_handle)) rc = MODULE_INIT_FAILED;
    kprintf(KLOG_ATTR, "[KCG] ROM module rc=%d pair=%08x cache=%u\n",
            rc, bi->img_crc, KCG_ROM_CACHE_BYTES);
    if (rc) kprintf(KLOG_ATTR, "[KCG] ROM stub; TVRAM CUI retained\n");
    return rc;
}
int kcg_module_abort(void) { return module_abort(kcg_handle); }
