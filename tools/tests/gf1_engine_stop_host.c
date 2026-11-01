#include "hwdev/gf/foundation.h"

struct fake { u32 reg, values[6]; int calls, writes, fail, mismatch; };
static int read_reg(void *opaque, u32 bar, u32 off, u32 width, u32 *out)
{
    struct fake *m = opaque;
    if (bar || off != RECOVER_PMC_ENABLE || width != 4) return -1;
    if (++m->calls == m->fail) return -1;
    *out = m->reg ^ (m->calls == m->mismatch ? 1U : 0U);
    return 0;
}
static int write_reg(void *opaque, u32 bar, u32 off, u32 width, u32 value)
{
    struct fake *m = opaque;
    if (bar || off != RECOVER_PMC_ENABLE || width != 4) return -1;
    if (++m->calls == m->fail) return -1;
    if (m->writes >= 6) return -1;
    m->values[m->writes++] = value;
    m->reg = value;
    return 0;
}
static int test(void)
{
    static const u32 expected[] = { 0xfffffeffU, 0xffffffffU, 0xffffefffU,
                                     0xffffffffU, 0xfffffffdU, 0xffffffffU };
    struct fake m = { .reg = 0xffffffffU };
    struct gf_foundation f = { .io = { .opaque = &m, .read = read_reg, .write = write_reg },
                               .bm = true, .expected_pmc = 0xffffffffU };
    int i;
    if (gf_engine_stop(&f) != GF1_OK || m.calls != 15 || m.writes != 6 ||
        !f.bm || f.expected_pmc != 0xffffffffU) return 1;
    for (i = 0; i < 6; ++i) if (m.values[i] != expected[i]) return 2;
    for (i = 1; i <= 15; ++i) {
        m = (struct fake){ .reg = 0xffffffffU, .fail = i };
        if (gf_engine_stop(&f) != GF1_ESTOP || m.calls != i || !f.bm) return 3;
    }
    for (i = 3; i <= 15; i += (i % 5 == 3 ? 2 : 3)) {
        m = (struct fake){ .reg = 0xffffffffU, .mismatch = i };
        if (gf_engine_stop(&f) != GF1_ESTOP || m.calls != i || !f.bm) return 4;
    }
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
