/* Real generated controls -> platform -> resident facade, copied ILP32 build. */
#include "platform_device.h"
#include "kcg_contract.h"
#include "kcg_module.h"
#include "os32_kapi_shared.h"
#define __cdecl __attribute__((cdecl))
static unsigned hits;
#define KAPI_HIT(slot) do { (void)(slot); ++hits; } while (0)
#include "wrappers.inc"

STATIC_ASSERT(KCG_MODULE_API_SLOTS == 4, rom_slots);
STATIC_ASSERT(KCG_MODULE_OPS_VERSION == 1, rom_version);
STATIC_ASSERT(sizeof(KcgModuleApi) == 16, rom_table_size);
static unsigned rom_calls;
void kcg_rom_read_ank(u8 ch, u8 *buf)
{ (void)ch; ++rom_calls; for (int i = 0; i < KCG_ANK_H; ++i) buf[i] = 0; }
void kcg_rom_read_kanji(u16 jis, u8 *buf)
{ (void)jis; ++rom_calls; for (int i = 0; i < KCG_KANJI_H * KCG_KANJI_BPL; ++i) buf[i] = 0; }
static void stop(int rc) __attribute__((noreturn));
static void stop(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define CHECK(x) do { if (!(x)) stop(1); } while (0)
void _start(void)
{
    /* Both pre-load and post-failure use the resident controls, no ROM calls. */
    for (int repeat = 0; repeat < 2; ++repeat) {
        kcg_scale = 3;
        wrap_kcg_init(); CHECK(kcg_scale == 1);
        const int values[] = {-2147483647 - 1, -1, 0, 1, 2, 3, 4, 5, 2147483647};
        const int expected[] = {1, 1, 1, 1, 2, 3, 4, 4, 4};
        for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
            wrap_kcg_set_scale(values[i]); CHECK(kcg_scale == expected[i]);
        }
        CHECK(wrap_kcg_load_font(0) == OS32_ERR_NOSYS);
        CHECK(wrap_kcg_load_font("legacy-font") == OS32_ERR_NOSYS);
        CHECK(kcg_scale == 4 && !rom_calls);
    }
    u8 glyph[KCG_KANJI_H * KCG_KANJI_BPL];
    kcg_read_ank('A', glyph);
    kcg_read_kanji(0x3021, glyph);
    CHECK(rom_calls == 2 && hits == 24);
    stop(0);
}
