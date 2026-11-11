/* PC-98 display provider: backend candidates, the GFX= preference, probing and
 * the 9801 fallback. (第 5 段 B4: gfx/gfx_core.c から移した。順と条件は不変。)
 * ここではハードウェアを設定しない — probe と、共通側の mode の確定だけ。 */
#include "display_provider.h"
#include "os32_kapi_shared.h"
#include "pgalloc.h"      /* LEDGER_SF_* */
#include "kprintf.h"

/* probe 順のバックエンド一覧。速い/高機能な順に前から並べ、最初に probe() が
 * 1 を返したものを使う (Cirrus → PEGC → 9801)。
 * 9801 の probe は常に 1 なので、末尾が必ず受け皿になる。 */
static GfxBackend *const g_backend_list[] = {
    &gfx_backend_trident, /* Explicit selection only. */
    &gfx_backend_geforce, /* Explicit selection only. */
    &gfx_backend_cirrus, /* Cirrus GD54xx アクセラレータ (H3)。ID 5Bh のみ */
    &gfx_backend_pegc,   /* 9821 PEGC 256 色 (H2)。9801 では probe が 0 */
    &gfx_backend_pc98    /* 9801 標準グラフィック (H1)。最後の受け皿 */
};

int display_provider_pref(int pref)
{
    if (pref != GFX_PREF_PC98 && pref != GFX_PREF_PEGC &&
        pref != GFX_PREF_CIRRUS && pref != GFX_PREF_GEFORCE &&
        pref != GFX_PREF_TRIDENT)
        pref = GFX_PREF_AUTO;
    return pref;
}

u32 display_provider_ledger(const GfxBackend *be)
{
    return be == &gfx_backend_pc98 ? LEDGER_SF_PC98 :
           be == &gfx_backend_pegc ? LEDGER_SF_PEGC :
           be == &gfx_backend_cirrus ? LEDGER_SF_CIRRUS :
           be == &gfx_backend_trident ? LEDGER_SF_TRIDENT :
           be == &gfx_backend_geforce ? LEDGER_SF_GEFORCE : 0;
}

/* Missing module stubs do not count as a Cirrus hardware probe. */
static int cirrus_probed;

int gfx_cirrus_probed(void)
{
    return cirrus_probed;
}

static int gfx_probe_backend(const GfxBackend *backend)
{
    if (!backend->modes || !backend->probe) return 0;
    if (backend == &gfx_backend_cirrus && backend->init) cirrus_probed = 1;
    return backend->probe();
}

void display_provider_each(void (*fn)(GfxBackend *be, void *arg), void *arg)
{
    for (u32 i = 0; i < sizeof(g_backend_list) / sizeof(g_backend_list[0]); i++) {
        GfxBackend *be = g_backend_list[i];
        if (!be) continue;
        if (be == &gfx_backend_trident || be == &gfx_backend_geforce) {
            if (be == g_backend) fn(be, arg);
            continue;
        }
        /* Enumeration must not set cirrus_probed or configure hardware. */
        if (be->modes && be->probe && be->probe()) fn(be, arg);
    }
}

GfxBackend *display_provider_select(int pref, int legacy200,
                                    display_select_mode_fn select_mode)
{
    int i;
    int n = (int)(sizeof(g_backend_list) / sizeof(g_backend_list[0]));

    /* 強制指定 (GFX=) は probe より先に効く。
     * pc98 は probe をまったく行わない (9801 は最後の受け皿なので常に成功)。
     * pegc は probe だけは通す — 実機に PEGC が無ければ描けないので、
     * 失敗したら従来どおり 9801 へ落とす。 */
    if (pref == GFX_PREF_PC98) {
        select_mode(&gfx_backend_pc98, legacy200);
        return &gfx_backend_pc98;
    }
    if (pref == GFX_PREF_PEGC ||
        pref == GFX_PREF_CIRRUS || pref == GFX_PREF_GEFORCE ||
        pref == GFX_PREF_TRIDENT) {
        const GfxBackend *want = pref == GFX_PREF_TRIDENT ? &gfx_backend_trident :
                                pref == GFX_PREF_GEFORCE ? &gfx_backend_geforce : (pref == GFX_PREF_CIRRUS)
                                     ? &gfx_backend_cirrus
                                     : &gfx_backend_pegc;
        /* Refresh Trident's descriptor/error state before the selection probe,
         * so a failed ScreenInit can be retried. prepare performs no device I/O. */
        if (want == &gfx_backend_trident && want && want->prepare) want->prepare();
        for (i = 0; i < n; i++) {
            /* weak 宣言なので未リンクなら要素が 0 (→ 9801 へ落ちる) */
            if (!g_backend_list[i]) continue;
            if (g_backend_list[i] != want) continue;
            if (gfx_probe_backend(g_backend_list[i]) &&
                select_mode(g_backend_list[i], legacy200))
                return g_backend_list[i];
            break;
        }
        if (pref == GFX_PREF_TRIDENT) {
            GFX_ScreenInfo info;
            int rc = want && want->query ? want->query(&info) : OS32_ERR_NOSYS;
            if (!rc) rc = OS32_ERR_INVAL; /* probe/layout rejection */
            kprintf(KLOG_ATTR, "[gfx] trident stage=select rc=%d -> pc98\n", rc);
        }
        select_mode(&gfx_backend_pc98, legacy200);
        return &gfx_backend_pc98;
    }

    for (i = 0; i < n; i++) {
        /* weak 宣言のバックエンド (未リンクなら 0) を飛ばす。
         * → include/gfx_hal.h の gfx_backend_pegc の注記 */
        if (!g_backend_list[i] || g_backend_list[i] == &gfx_backend_geforce ||
            g_backend_list[i] == &gfx_backend_trident) continue;
        if (gfx_probe_backend(g_backend_list[i])) {
            if (!select_mode(g_backend_list[i], legacy200)) break;
            return g_backend_list[i];
        }
    }
    select_mode(&gfx_backend_pc98, legacy200);
    return &gfx_backend_pc98;   /* フォールバック */
}
