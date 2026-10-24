#ifdef OS32_R1_FIXTURE
#include "r1_fixture.h"
#endif
/* ======================================================================== */
/*  V86_MEM.C — V86 ゲスト用アドレス空間                                    */
/* ======================================================================== */

#include "v86_mem.h"
#include "v86_call.h"
#include "kstring.h"
#include "v86.h"
#include "v86_module.h"
#include "paging.h"
#include "pgalloc.h"
#include "io.h"
#include "v86_io.h"
#include "v86_bios.h"
#include "module.h"
#include "kselftest.h"
#include "tvram.h"
#include "../gfx/gfx.h"


enum { V86_RESTORE_COL_BAD = 17, V86_RESTORE_COL_PTE = 30,
       V86_RESTORE_COL_PDE = 43, V86_RESTORE_COL_PCD = 56 };
enum { V86_HEX_DIGITS = 8, V86_HEX_MASK = 15, V86_HEX_SHIFT = 4 };

struct v86_session_state v86_session;
u32 v86_restore_mismatch;

/* VRAM/ROM 帯のマップリスト (setup と teardown で同じ表を使う)。
 * setup はゲスト用に USER を付け、teardown で元の属性に戻す。
 * かつては setup/teardown に同じ範囲リストが 2 回書かれていて、
 * 片方だけ直して食い違う事故の温床だった (R8)。 */
static const struct v86_ident_ent {
    u32 start;
    u32 end;            /* exclusive */
    u32 setup_flags;    /* セッション中 (ゲストから見える) */
    u32 teardown_flags; /* 平常時 */
} v86_ident_map[] = {
    /* PC98_NATIVE_VRAM と同じ区間。surface_bundle_host が照合する。
     * Same FIXED/UC native backing policy as paging_init; CG stays WB. */
    { V86_VRAM_START, TVRAM_CG_WINDOW, PAGE_RW | PTE_USER | PTE_PCD, PAGE_RW | PTE_PCD },
    { TVRAM_CG_WINDOW, GVRAM_PLANE_B, PAGE_RW | PTE_USER, PAGE_RW },
    { GVRAM_PLANE_B, V86_VRAM_END, PAGE_RW | PTE_USER | PTE_PCD, PAGE_RW | PTE_PCD },
    { V86_GVRAM_E_START, V86_GVRAM_E_END, PAGE_RW | PTE_USER | PTE_PCD, PAGE_RW | PTE_PCD },
    { V86_EXTROM_START,   V86_EXTROM_END,       PAGE_RO | PTE_USER, PAGE_RO },
    { V86_SOUNDROM_START, V86_SOUNDROM_END,     PAGE_RO | PTE_USER, PAGE_RO },
    { MEM_BIOS_ROM_START, MEM_BIOS_ROM_END + 1, PAGE_RO | PTE_USER, PAGE_RO },
};
#define V86_IDENT_MAP_N \
    ((int)(sizeof(v86_ident_map) / sizeof(v86_ident_map[0])))

int v86_session_begin(u32 owner)
{
    if (v86_session.open || v86_session.closing || v86_session.release_pending) {
        return -1;              /* 二重セットアップ */
    }

    /* V86_LOW remains FIXED/kernel/WB; only the session changes its PTEs.
     * Save the host mailbox before clearing physical conventional RAM. */
    (void)owner;
    if (!v86_call_mailbox() || !v86_session.real_saved) return -2;
    kmemcpy(v86_call_mailbox(), P2V(MEM_AUTOPLAY_MAILBOX_BASE),
            MEM_AUTOPLAY_MAILBOX_SIZE);
    if (!v86_module_session_acquire()) return -1;
    if (paging_v86_snapshot(&v86_session) != 0) {
        v86_module_session_release();
        return -1;
    }
    v86_session.open = 1;
    v86_session.aborting = 0;

    /* NP21/W BIOS writes physical BDA directly. Page zero is exposed only
     * from PREPARING through restoration of the immutable boot original. */
    if (paging_v86_map_range(0, PAGE_SIZE, 0, PAGE_RW | PTE_USER) != 0)
        goto fail;

    if (paging_v86_map_range(PAGE_SIZE, V86_REMAP_END, PAGE_SIZE,
                             PAGE_RW | PTE_USER) != 0) goto fail;
    kmemset(P2V(V86_REMAP_START), 0, V86_REMAP_END);

    /* VRAM は実物理を直マップ。ここが #GP しないことが性能の前提。
     * ROM 帯は読み取り専用で見せる。BIOS ROM も含めて実行可能にしておく —
     * 前回プロジェクトは ROM 実行を排除する「方法C」を実装した翌日に、
     * NP21/W のソースを読んで「ROM は CPU が直接実行するのが正しい」と
     * 判明して全撤回している (docs/archive/v21/v86v2/01_prior_session_analysis.md)。
     * 同じ回り道はしない。 */
    {
        int mi;
        for (mi = 0; mi < V86_IDENT_MAP_N; mi++) {
            const struct v86_ident_ent *e = &v86_ident_map[mi];
            if (paging_v86_map_range(e->start, e->end, e->start,
                                 e->setup_flags) != 0) {
                goto fail;
            }
        }
    }

    /* IVT/BDA の復元と BIOS スタブの設置 */
    v86_bios_setup();

    /* I/O 許可ビットマップにゲスト用ポリシーを適用 */
    v86_io_apply_policy();

#ifdef OS32_R1_FIXTURE
    r1_fixture_v86_ready();
#endif
    return 0;

fail:
    /* 途中で失敗したら張りかけのマッピングを必ず巻き戻す。
     * 中途半端に USER が付いた低位ページを残したままカーネルに戻ると、
     * 次に低位メモリを使うコード (フォント/GFX) が壊れた地図の上で動く。
     * teardown が PTE・IVT/BDA・mailbox の復元を兼ねる。 */
    v86_mem_teardown();
    return -3;
}

void v86_session_end(void)
{
    u32 flags = irq_save();
    if (v86_session.closing) { irq_restore(flags); return; }
    v86_session.closing = 1;
#ifdef OS32_R1_FIXTURE
    r1_fixture_v86_end();
#endif
    /* Capture becomes active before session_begin, and may outlive end until
     * copyout. Disconnect even when there are no low mappings to restore. */
    if (v86_session.aborting && kdebug.ops.gcap_detach) kdebug.ops.gcap_detach();
    if (!v86_session.open) goto release;
    /* Preserve this context's IF, but leave guest IOPL/other flags behind. */
    if (v86_session.running)
        flags = (v86_session.saved_eflags & ~EFLAGS_IF) | (flags & EFLAGS_IF);
    v86_runtime_end();
    v86_bios_restore_real();
    kmemcpy(P2V(MEM_AUTOPLAY_MAILBOX_BASE), v86_call_mailbox(),
            MEM_AUTOPLAY_MAILBOX_SIZE);

    /* Restore exact saved PTEs, including NP, USER and cache attributes.
     * The table remains the setup/teardown cache-policy contract. */
    for (int mi = 0; mi < V86_IDENT_MAP_N; mi++) {
        const struct v86_ident_ent *e = &v86_ident_map[mi];
        for (u32 a = e->start; a < e->end; a += PAGE_SIZE) {
            u32 pcd = e->teardown_flags & PTE_PCD;
            if ((v86_session.low_pte[a / PAGE_SIZE] & PTE_PCD) != pcd)
                v86_restore_mismatch++;
            for (u32 sid = 0; sid < LEDGER_MAX_SURFACES; sid++) {
                const struct ledger_surface *sf = &ledger_surfaces[sid];
                u32 pfn = a / PAGE_SIZE;
                if (sf->npages && pfn >= sf->first && pfn - sf->first < sf->npages) {
                    u32 want = sf->cache == LEDGER_CACHE_UC ? PTE_PCD : 0;
                    if ((e->setup_flags & PTE_PCD) != want || pcd != want)
                        v86_restore_mismatch++;
                }
            }
        }
    }
    v86_restore_mismatch += paging_v86_restore(&v86_session);
    if (v86_restore_mismatch) {
        char line[] = "V86 RESTORE: bad=00000000 PTE=00000000 PDE=00000000 PCD=00000000";
        u32 counts[] = { v86_restore_mismatch, v86_session.restore_pte_bad,
            v86_session.restore_pde_bad, v86_restore_mismatch -
            v86_session.restore_pte_bad - v86_session.restore_pde_bad };
        const unsigned int offsets[] = { V86_RESTORE_COL_BAD, V86_RESTORE_COL_PTE, V86_RESTORE_COL_PDE, V86_RESTORE_COL_PCD };
        for (unsigned int n = 0; n < sizeof(offsets) / sizeof(offsets[0]); n++)
            for (int i = V86_HEX_DIGITS - 1; i >= 0; i--) {
                line[offsets[n] + i] = "0123456789ABCDEF"[counts[n] & V86_HEX_MASK];
                counts[n] >>= V86_HEX_SHIFT;
            }
        volatile u16 *text = P2V_IO(TVRAM_BASE);
        volatile u16 *attr = P2V_IO(TVRAM_ATTR);
        unsigned int col = 0;
        for (; line[col]; col++) { text[col] = (u8)line[col]; attr[col] = TATTR_RED; }
        while (col < TVRAM_COLS) { text[col] = ' '; attr[col++] = TATTR_RED; }
        for (;;) _stop();
    }
    /* Guest permissions are gone before owner reclamation can run. */
    v86_io_reset_policy();
    if (kdebug.ops.gcap_detach) kdebug.ops.gcap_detach();
    v86_session.open = 0;
    v86_session.release_pending = 1;
release:
    irq_restore(flags);
    /* R1 forbids allocator/VFS reclamation on an exception stack. The kill
     * landing calls end again before exec_finish can reclaim the owner. */
    if (kctx_irq_depth || kctx_exc_depth) goto done;
    if (v86_session.aborting && kdebug.ops.gcap_release) kdebug.ops.gcap_release();
    if (!v86_session.release_pending) {
        /* An interrupted preparation may have pinned the body without yet
         * publishing a low mapping. The existing release is idempotent. */
        if (v86_session.aborting) v86_module_session_release();
        goto done;
    }
    v86_bios_detach_disk();
    v86_session.release_pending = 0;
    gfx_v86_return();
    kselftest_audit_v86_return();
    v86_module_session_release();
done:
    v86_session.closing = 0;
}

/* Internal compatibility names; all four callers use the session boundary. */
int v86_mem_setup(u32 owner) { return v86_session_begin(owner); }
void v86_mem_teardown(void) { v86_session_end(); }
