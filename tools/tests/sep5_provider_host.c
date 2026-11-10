/* B4: the real display provider (generated include = product text) driven by
 * recording backends. Each failed expectation exits with its own code. */
#include "types.h"
#include "gfx_hal.h"
#include "pgalloc.h"
#include "os32_kapi_shared.h"
#include "kprintf.h"
#include "display_provider.h"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
static char trace[64];
static unsigned count;
static void event(char kind, char who)
{
    if (count + 2 >= sizeof(trace)) finish(2);
    trace[count++] = kind; trace[count++] = who; trace[count] = 0;
}
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

enum { T, G, C, P, N, BACKENDS };
static const char ids[BACKENDS] = { 't', 'g', 'c', 'p', '9' };
static int available[BACKENDS], mode_ok[BACKENDS], query_rc, logged_rc, seen_legacy;
#define PROBE(n, id) static int probe_##n(void) { event('P', ids[id]); return available[id]; }
PROBE(t, T) PROBE(g, G) PROBE(c, C) PROBE(p, P) PROBE(n, N)
static void prepare_t(void) { event('R', 't'); }
static int query_t(GFX_ScreenInfo *info) { (void)info; event('Q', 't'); return query_rc; }
static void init_c(void) { finish(3); }   /* selection must never set hardware up */
static const GfxMode modes[1];
GfxBackend gfx_backend_trident = { .name = "t", .probe = probe_t, .prepare = prepare_t,
                                   .query = query_t, .modes = modes };
GfxBackend gfx_backend_geforce = { .name = "g", .probe = probe_g, .modes = modes };
GfxBackend gfx_backend_cirrus = { .name = "c", .probe = probe_c, .init = init_c, .modes = modes };
GfxBackend gfx_backend_pegc = { .name = "p", .probe = probe_p, .modes = modes };
GfxBackend gfx_backend_pc98 = { .name = "9", .probe = probe_n, .modes = modes };
static GfxBackend *const backends[BACKENDS] = { &gfx_backend_trident, &gfx_backend_geforce,
    &gfx_backend_cirrus, &gfx_backend_pegc, &gfx_backend_pc98 };

void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    __builtin_va_list ap;
    (void)attr;
    if (!same(fmt, "[gfx] trident stage=select rc=%d -> pc98\n")) finish(4);
    __builtin_va_start(ap, fmt);
    logged_rc = __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    event('K', 't');
}

static int select_mode(GfxBackend *backend, int legacy200)
{
    int i;
    for (i = 0; i < BACKENDS && backends[i] != backend; i++) { }
    if (i == BACKENDS) finish(5);
    event('S', ids[i]);
    seen_legacy = legacy200;
    return mode_ok[i];
}

#include "gfx_selector_source.inc"

struct scenario {
    int code, pref, legacy200;
    unsigned avail, modes_ok;      /* bit per backend, T=1 G=2 C=4 P=8 N=16 */
    int want;
    const char *trace;
};
#define ALL 31u
static const struct scenario table[] = {
    /* AUTO: Cirrus → PEGC → 9801; Trident/GeForce are never probed. */
    { 20, GFX_PREF_AUTO, 0, 4 | 16, ALL, C, "PcSc" },
    { 21, GFX_PREF_AUTO, 0, 8 | 16, ALL, P, "PcPpSp" },
    { 22, GFX_PREF_AUTO, 0, 16, ALL, N, "PcPpP9S9" },
    { 23, GFX_PREF_AUTO, 0, 1 | 2 | 16, ALL, N, "PcPpP9S9" },
    /* A probed backend whose mode does not fit ends the scan: 9801, not PEGC. */
    { 24, GFX_PREF_AUTO, 0, 4 | 8 | 16, ALL & ~4u, N, "PcScS9" },
    { 25, GFX_PREF_AUTO, 1, 16, ALL & ~16u, N, "PcPpP9S9S9" },
    /* GFX=pc98 never probes. */
    { 26, GFX_PREF_PC98, 0, ALL, ALL, N, "S9" },
    { 27, GFX_PREF_PC98, 1, ALL, 0, N, "S9" },
    /* Forced backends probe only themselves, then fall back to 9801. */
    { 28, GFX_PREF_PEGC, 0, 4 | 8 | 16, ALL, P, "PpSp" },
    { 29, GFX_PREF_PEGC, 0, 4 | 16, ALL, N, "PpS9" },
    { 30, GFX_PREF_PEGC, 0, 8 | 16, ALL & ~8u, N, "PpSpS9" },
    { 31, GFX_PREF_CIRRUS, 1, 4 | 8 | 16, ALL, C, "PcSc" },
    { 32, GFX_PREF_CIRRUS, 0, 8 | 16, ALL, N, "PcS9" },
    { 33, GFX_PREF_GEFORCE, 0, 2 | 4 | 16, ALL, G, "PgSg" },
    { 34, GFX_PREF_GEFORCE, 0, 4 | 8 | 16, ALL, N, "PgS9" },
    /* Trident: prepare before the probe; a rejection is logged with its rc. */
    { 35, GFX_PREF_TRIDENT, 0, 1 | 4 | 16, ALL, T, "RtPtSt" },
    { 36, GFX_PREF_TRIDENT, 0, 4 | 16, ALL, N, "RtPtQtKtS9" },
    { 37, GFX_PREF_TRIDENT, 0, 1 | 16, ALL & ~1u, N, "RtPtStQtKtS9" },
};

static void reset(void)
{
    count = 0; trace[0] = 0; logged_rc = 12345; seen_legacy = -1;
}

void _start(void)
{
    unsigned i, b;
    static const int prefs[] = { GFX_PREF_AUTO, GFX_PREF_PC98, GFX_PREF_PEGC, GFX_PREF_CIRRUS,
                                 GFX_PREF_GEFORCE, GFX_PREF_TRIDENT };

    if (DISPLAY_FALLBACK_BACKEND != &gfx_backend_pc98) finish(10);
    for (i = 0; i < sizeof(prefs) / sizeof(prefs[0]); i++)
        if (display_provider_pref(prefs[i]) != prefs[i]) finish(11);
    if (display_provider_pref(-1) != GFX_PREF_AUTO || display_provider_pref(6) != GFX_PREF_AUTO ||
        display_provider_pref(0x7fffffff) != GFX_PREF_AUTO) finish(12);
    if (display_provider_ledger(&gfx_backend_pc98) != LEDGER_SF_PC98 ||
        display_provider_ledger(&gfx_backend_pegc) != LEDGER_SF_PEGC ||
        display_provider_ledger(&gfx_backend_cirrus) != LEDGER_SF_CIRRUS ||
        display_provider_ledger(&gfx_backend_geforce) != LEDGER_SF_GEFORCE ||
        display_provider_ledger(&gfx_backend_trident) != LEDGER_SF_TRIDENT ||
        display_provider_ledger((const GfxBackend *)trace) != 0) finish(13);

    /* Before any Cirrus probe the flag is clear; a forced PEGC probe keeps it. */
    if (gfx_cirrus_probed()) finish(14);
    for (b = 0; b < BACKENDS; b++) { available[b] = 1; mode_ok[b] = 1; }
    reset();
    if (display_provider_select(GFX_PREF_PEGC, 0, select_mode) != &gfx_backend_pegc ||
        gfx_cirrus_probed()) finish(15);
    /* A Cirrus stub without init (module missing) is probed but not recorded. */
    gfx_backend_cirrus.init = 0;
    reset();
    if (display_provider_select(GFX_PREF_CIRRUS, 0, select_mode) != &gfx_backend_cirrus ||
        !same(trace, "PcSc") || gfx_cirrus_probed()) finish(16);
    gfx_backend_cirrus.init = init_c;
    /* A backend without registered modes is never probed. */
    gfx_backend_cirrus.modes = 0;
    reset();
    if (display_provider_select(GFX_PREF_AUTO, 0, select_mode) != &gfx_backend_pegc ||
        !same(trace, "PpSp") || gfx_cirrus_probed()) finish(17);
    gfx_backend_cirrus.modes = modes;

    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        const struct scenario *s = &table[i];
        for (b = 0; b < BACKENDS; b++) {
            available[b] = (s->avail >> b) & 1;
            mode_ok[b] = (s->modes_ok >> b) & 1;
        }
        reset();
        query_rc = 0;
        if (display_provider_select(s->pref, s->legacy200, select_mode) != backends[s->want] ||
            !same(trace, s->trace) || seen_legacy != s->legacy200) finish(s->code);
    }
    if (!gfx_cirrus_probed()) finish(40);

    /* Trident rejection: rc 0 from query still reports a failure; others pass through. */
    for (b = 0; b < BACKENDS; b++) { available[b] = b == N; mode_ok[b] = 1; }
    reset(); query_rc = 0;
    display_provider_select(GFX_PREF_TRIDENT, 0, select_mode);
    if (logged_rc != OS32_ERR_INVAL) finish(41);
    reset(); query_rc = OS32_ERR_NOSYS;
    display_provider_select(GFX_PREF_TRIDENT, 0, select_mode);
    if (logged_rc != OS32_ERR_NOSYS) finish(42);
    gfx_backend_trident.query = 0;
    reset();
    display_provider_select(GFX_PREF_TRIDENT, 0, select_mode);
    if (logged_rc != OS32_ERR_NOSYS || !same(trace, "RtPtKtS9")) finish(43);
    finish(0);
}
