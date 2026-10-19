/* Existing PC-98 ROM port sequence moved from drivers/kcg.c unchanged.
 * Source: PC9800Bible §2-6-5; this is a relocation, no new hardware protocol. */
#include "kcg_module.h"
#include "kcg.h"
#include "io.h"
#include "pc98.h"
#include "kstring.h"
#include "kprintf.h"

/* 64KiB accounting budget includes the tags. The final partial slot is unused. */
typedef struct { u16 tag; u8 glyph[KCG_KANJI_H * 2]; } KcgCacheEntry;
#define KCG_CACHE_SLOTS (KCG_ROM_CACHE_BYTES / sizeof(KcgCacheEntry))
#define KCG_ANK_TAG 0x8000u
#define KCG_STATS_SAMPLE 1024u
static KcgCacheEntry cache[KCG_CACHE_SLOTS];
static KcgCacheStats counters;
static int initialized;
static void kcg_wait(void) { io_wait(); }

static void rom_ank(u8 ch, u8 *buf)
{
    int line;
    /*
     * NP2kaiソースコード (cgrom.c) より判明:
     *   oa1: cr->code = (dat << 8) | (cr->code & 0xff)  → A1hは上位バイト
     *   oa3: cr->code = (cr->code & 0xff00) | dat        → A3hは下位バイト
     *   ANK条件: !(cr->code & 0xff00) → A1h = 0x00
     *   フォント: fontrom + 0x80000 + (cr->code << 4)
     *   ライン: ptr[cr->line]  (line = dat & 0x1f)
     */
    outp(KCG_CODE_LO, 0x00);        /* A1h ← 0x00 (上位=0でANK) */
    kcg_wait();
    outp(KCG_CODE_HI, ch);          /* A3h ← ANK文字コード (下位) */
    kcg_wait();

    for (line = 0; line < 16; line++) {
        outp(KCG_LINE_SEL, (u8)line);  /* A5h ← ライン (bit4=0でANK) */
        kcg_wait();
        buf[line] = (u8)inp(KCG_DATA); /* A9h → パターン */
    }
}
static void rom_kanji(u16 jis_code, u8 *buf)
{
    int line;
    u8 hi = (u8)(jis_code >> 8), lo = (u8)jis_code;
    /* 文字コード設定 */
    outp(KCG_CODE_LO, lo);          /* A1h ← JIS下位 */
    kcg_wait();
    outp(KCG_CODE_HI, hi - 0x20);   /* A3h ← JIS上位 - 0x20 */
    kcg_wait();

    /* 左半分 (8ドット×16ライン) — bit5=1 */
    for (line = 0; line < 16; line++) {
        outp(KCG_LINE_SEL, (u8)(line | 0x20));  /* bit5=1: 左 */
        kcg_wait();
        buf[line * 2] = (u8)inp(KCG_DATA);
    }

    /* 右半分 (8ドット×16ライン) — bit5=0 */
    for (line = 0; line < 16; line++) {
        outp(KCG_LINE_SEL, (u8)(line | 0x00));  /* bit5=0: 右 */
        kcg_wait();
        buf[line * 2 + 1] = (u8)inp(KCG_DATA);
    }

}
static void read_glyph(u16 tag, u8 *buf, u32 bytes)
{
    KcgCacheEntry *slot = &cache[tag % KCG_CACHE_SLOTS];
    if (slot->tag == tag && tag) {
        ++counters.hits;
        kmemcpy(buf, slot->glyph, bytes);
    } else {
        ++counters.misses;
        if (tag & KCG_ANK_TAG) rom_ank((u8)tag, buf);
        else rom_kanji(tag, buf);
        kmemcpy(slot->glyph, buf, bytes);
        slot->tag = tag;
    }
    /* One observed sample, no acceptance threshold and no per-glyph logging. */
    if (counters.hits + counters.misses == KCG_STATS_SAMPLE)
        kprintf(0x07, "[KCG] cache hits=%u misses=%u (sample=%u)\n",
                counters.hits, counters.misses, KCG_STATS_SAMPLE);
}
static void read_ank(u8 ch, u8 *buf) { read_glyph(KCG_ANK_TAG | ch, buf, KCG_ANK_H); }
static void read_kanji(u16 jis, u8 *buf)
{
    u8 hi = jis >> 8, lo = (u8)jis;
    if (hi < 0x21 || hi > 0x7e || lo < 0x21 || lo > 0x7e) {
        ++counters.misses;
        rom_kanji(jis, buf);
        return;
    }
    read_glyph(jis, buf, KCG_KANJI_H * 2);
}
static void stats(KcgCacheStats *out) { *out = counters; }
static void invalidate(void)
{
    for (u32 i = 0; i < KCG_CACHE_SLOTS; i++) cache[i].tag = 0;
}
int kcg_module_init(KcgModuleApi *out)
{
    if (initialized) return MODULE_BUSY;
    initialized = 1;
    outp(MODE_FF1_PORT, MFF1_KCG_CODE);
    kcg_wait();
    *out = (KcgModuleApi){read_ank, read_kanji, stats, invalidate};
    return 0;
}
const struct {
    u32 version;
    int (*initialize)(KcgModuleApi *);
} kcg_module_ops = {KCG_MODULE_OPS_VERSION, kcg_module_init};
