/* Adapter/runtime lifetime is real. The injected init supplies body addresses;
 * test_kcg_module.py separately executes the real body with deterministic ports. */
#define vfs_open runtime_fixture_open
#define _start unused_runtime_start
#include "module_runtime_host.c"
#undef _start
#undef vfs_open
#ifndef KCG_ADAPTER
#define KCG_ADAPTER "../../kernel/kcg_module.c"
#endif
#define IO_H
void platform_display_kcg_code(void) {}
#include KCG_ADAPTER
static u32 mode, initialized, stub_logs;
static const struct bootinfo boot = {.img_valid = 1};
const struct bootinfo *bootinfo_get(void) { return &boot; }
const u32 kcg_kexports[KCG_IMPORT_COUNT] = {0};
const struct module_symbol_contract kcg_import_contracts[KCG_IMPORT_COUNT] = {{0}};
const struct module_symbol_contract kcg_export_contracts[1] = {{0}};
int vfs_open(const char *path, int flags)
{ return mode == 5 ? -1 : runtime_fixture_open(path, flags); }
void kprintf(u8 attr, const char *fmt, ...)
{ (void)attr; if (!kstrcmp(fmt, "[KCG] ROM stub; TVRAM CUI retained\n")) ++stub_logs; }
static int init_kcg(KcgModuleApi *out)
{
    struct module_record *r = module_record_get(kcg_handle);
    CHECK(r->state == MODULE_STARTING && !module_exports(kcg_handle));
    ++initialized;
    out->read_ank = (void *)(uptr)r->text;
    out->read_kanji = (void *)(uptr)(r->text + (mode == 1 ? r->text_size : 0));
    out->stats = (void *)(uptr)r->text;
    out->invalidate = (void *)(uptr)r->text;
    return mode == 2 ? -1 : 0;
}
void _start(void)
{
    read_input(input, sizeof(input)); read_input(file, sizeof(file));
    mode = input[1];
    if (mode == 5) {
        CHECK(kcg_module_load() == MODULE_NOT_FOUND && stub_logs == 1 && !allocations);
        u8 glyph[32]; kmemset(glyph, 0xa5, sizeof(glyph));
        kcg_rom_read_ank('A', glyph);
        for (u32 i=0; i<16; i++) CHECK(!glyph[i]);
        CHECK(!kcg_module_abort()); empty(); finish(0);
    }
    kmemcpy(&imports[0].name_hash, file + 152, 4);
    kmemcpy(&exports[4].name_hash, file + 164, 4);
    exports[4].version = 1;
    struct module_request q = request();
    q.lifecycle = (struct module_lifecycle){.start = kcg_start, .detach = kcg_detach};
    if (mode == 4) {
        fail_at = 2;
        CHECK(module_load(&q, &kcg_handle) == MODULE_NO_PAGES);
        CHECK(!kcg_module_ready() && !initialized);
        u8 glyph[32]; kmemset(glyph, 0xa5, sizeof(glyph));
        kcg_rom_read_kanji(0x3021, glyph);
        for (u32 i=0; i<sizeof(glyph); ++i) CHECK(!glyph[i]);
        empty(); finish(0);
    }
    CHECK(!module_load(&q, &kcg_handle));
    struct module_record *r = module_record_get(kcg_handle);
    r->init = (u32)(uptr)init_kcg;
    int rc = module_start(kcg_handle);
    CHECK(initialized == 1);
    if (mode == 1 || mode == 2 || mode == 3) {
        CHECK(rc == MODULE_INIT_FAILED && !module_exports(kcg_handle));
        u8 glyph[32]; KcgCacheStats stats;
        kmemset(glyph, 0xa5, sizeof(glyph));
        kcg_rom_read_kanji(0x3021, glyph);
        for (u32 i = 0; i < sizeof(glyph); i++) CHECK(!glyph[i]);
        kcg_cache_stats(&stats); CHECK(!stats.hits && !stats.misses);
        empty(); finish(0);
    }
    CHECK(!rc && module_exports(kcg_handle));
    CHECK(kcg_module_load() == MODULE_BUSY && initialized == 1);
    CHECK(module_start(kcg_handle) == MODULE_BUSY && initialized == 1);
    CHECK(!kcg_module_abort() && !module_exports(kcg_handle));
    CHECK(!candidate.read_ank && !candidate.read_kanji && !candidate.stats);
    CHECK(!kcg_module_abort());
    empty(); finish(0);
}
