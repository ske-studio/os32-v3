#include "keymap.h"
#include "in2_int.h"
#include "os32_kapi_shared.h"

extern const KmBackend hid_jp_backend;
static KmEntry entries[128];
static u8 compat[128], flags[128];
static KmExactCell bank0[128][8], bank1[128][8];
static KmPc98Extensions ext = {bank0, 0, flags, 0, 0};
static KmBackend pc = {1, 128, entries, compat, 0, &ext};
static u16 live_generation = 0x00ff;
static u8 live_kind = INPUT_KIND_KBD;
int input_src_info(u8 src, u16 gen, u8 *kind, u8 *origin)
{
    if (src != 1 || gen != live_generation) return 0;
    *kind = live_kind; *origin = INPUT_ORIGIN_USB; return 1;
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int check(void)
{
    KmSource out = {&pc, 99};
    CHECK(sizeof(KmEntry) == 10);
    CHECK(km_backend_register(0) == OS32_ERR_INVAL);
    CHECK(km_source_bind(1, 0xff, 2) == OS32_ERR_INVAL);
    CHECK(km_source_lookup(1, 0xff, &out) == OS32_ERR_INVAL && out.map_generation == 99);
    pc.count = 127; CHECK(km_backend_register(&pc) == OS32_ERR_INVAL); pc.count = 128;
    entries[0].group1[0] = 0x0300; CHECK(km_backend_register(&pc) == OS32_ERR_INVAL); entries[0].group1[0] = 0;
    bank0[0][0].kind = KM_CELL_INHERIT; CHECK(km_backend_register(&pc) == OS32_ERR_INVAL); bank0[0][0].kind = KM_CHAR;
    ext.bank1 = bank1; bank1[0][0].kind = KM_BSD_SPECIAL; bank1[0][0].sym = 0xf500; bank1[0][0].op = 0x89;
    CHECK(km_backend_register(&pc) == OS32_ERR_INVAL); bank1[0][0].op = 0; ext.bank1 = 0;
    flags[0] = 4; CHECK(km_backend_register(&pc) == OS32_ERR_INVAL); flags[0] = 0;
    ext.accent_count = 1; CHECK(km_backend_register(&pc) == OS32_ERR_INVAL); ext.accent_count = 0;
    CHECK(km_backend_register(&pc) == 0);
    CHECK(km_backend_register(&pc) == OS32_ERR_INVAL);
    CHECK(km_backend_register(&hid_jp_backend) == 0);
    CHECK(km_source_bind(0, 0xff, 1) == OS32_ERR_INVAL);
    CHECK(km_source_bind(17, 0xff, 1) == OS32_ERR_INVAL);
    CHECK(km_source_bind(1, 0x100, 1) == OS32_ERR_INVAL);
    live_kind = INPUT_KIND_MOUSE; CHECK(km_source_bind(1, 0xff, 1) == OS32_ERR_INVAL); live_kind = INPUT_KIND_KBD;
    CHECK(km_source_bind(1, 0xff, 1) == 0);
    CHECK(km_source_bind(1, 0xff, 2) == OS32_ERR_INVAL);
    CHECK(km_source_lookup(1, 0xff, 0) == OS32_ERR_INVAL);
    CHECK(km_source_lookup(1, 0xff, &out) == 0 && out.map == &pc && out.map_generation == 1);
    CHECK(km_source_lookup(1, 0x100, &out) == OS32_ERR_INVAL);
    km_registry_source_released(1, 0x100);
    CHECK(km_source_lookup(1, 0xff, &out) == 0);
    km_registry_source_released(1, 0xff);
    CHECK(km_source_lookup(1, 0xff, &out) == OS32_ERR_INVAL);
    live_generation = 0x100;
    CHECK(km_source_bind(1, 0x100, 2) == 0);
    CHECK(km_source_lookup(1, 0xff, &out) == OS32_ERR_INVAL);
    CHECK(km_source_lookup(1, 0x100, &out) == 0 && out.map == &hid_jp_backend);
    km_registry_source_released(1, 0xff);
    CHECK(km_source_lookup(1, 0x100, &out) == 0);
    return 0;
}
#ifndef PPC_COMPILE
void _start(void) { int rc = check(); __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
#endif
