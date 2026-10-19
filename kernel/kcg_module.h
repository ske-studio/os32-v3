/* Paired ROM glyph reader. Unicode lookup and legacy NOSYS slot stay core. */
#ifndef KCG_MODULE_H
#define KCG_MODULE_H
#include "module_runtime.h"

#define KCG_MODULE_OPS_VERSION 1u
#define KCG_ROM_CACHE_BYTES (64u * 1024u)
typedef struct {
    u32 hits, misses;
} KcgCacheStats;
typedef struct {
    void (*read_ank)(u8 ch, u8 *buf);
    void (*read_kanji)(u16 jis, u8 *buf);
    void (*stats)(KcgCacheStats *out);
    void (*invalidate)(void);
} KcgModuleApi;
#define KCG_MODULE_API_SLOTS 4u

int kcg_module_load(void);
int kcg_module_abort(void);
int kcg_module_ready(void);
void kcg_rom_read_ank(u8 ch, u8 *buf);
void kcg_rom_read_kanji(u16 jis, u8 *buf);
void kcg_cache_stats(KcgCacheStats *out);
/* V86 return: CODE even without the body; discard cached ROM/CG RAM glyphs. */
void kcg_v86_restore(void);
/* T7a-1b: missing/init-failed module keeps the core TVRAM CUI path. */
#endif
