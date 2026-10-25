/* ======================================================================== */
/*  SELFTEST_EXEC.C — exec の起動自己試験 2 本 (票 TASK_SLIM S3e)             */
/*                                                                          */
/*  元は exec/exec.c の exec_kapi_layout_selftest・exec_tramp_user_selftest。 */
/*  踏み台ページと KAPI 表は kdi_selftest_exec_view で入口ごとに 1 回写す。   */
/*  どちらも exec_init の後 (kselftest_run_post_exec) に呼ぶ。                */
/* ======================================================================== */
#include "exec.h"
#include "os32x_hdr.h"
#include "ring3_str.h"
#include "ring3_ls.h"
#include "os32_kapi_slots.h"
#include "paging.h"
#include "kstring.h"
#include "vfs.h"
#include "path.h"
#include "kselftest_probe.h"
#include "selftest_internal.h"
#include "generated/kdebug_import_macros.h"

/* データ欄の固定配置と予約スロット (票 TASK_KAPI_DATA_FIELDS)。
 * ビット 0 = 固定オフセット、1 = 本物の表の予約 (NOSYS)、2 = トランポリンの
 * 予約スタブ、3 = ヘッダ v3 の照合。0 = 全部通った。 */
u32 kst_exec_kapi_layout(void)
{
    struct kselftest_exec_view view;
    u32 bad = 0;
    const u32 *real = (const u32 *)KAPI_ADDR;
    const u32 *tbl;
    u32 i;
    OS32Header h;

    kdi_selftest_exec_view(&view);
    tbl = (const u32 *)view.tramp_page;

    if (view.tramp_page == 0) return 0xFu;     /* exec_init より前 */

    /* (0) データ欄は固定オフセット。本物の表とトランポリンの同じ語に値が居る */
    if ((u32)&view.api->sbrk_heap_limit != (u32)KAPI_ADDR + (u32)KAPI_DATA_FIELDS_OFF ||
        real[KAPI_DATA_IDX_SHM_BASE] != view.api->shm_base ||
        tbl[KAPI_DATA_IDX_SHM_BASE] != view.api->shm_base ||
        view.api->shm_base == 0) {
        bad |= 1u << 0;
    }
    /* (1) 本物の表の予約スロットは NULL でなく「未実装」 (CPL=0 は NOSYS)。
     * 関数数 = 容量 (予約 0 本) のとき生成器は kapi_reserved を作らないので、
     * 参照ごと外す (実装レビュー R1: 300 本目でコンパイルが落ちる)。 */
#if KAPI_FUNC_RESERVED > 0
    for (i = 0; i < (u32)KAPI_FUNC_RESERVED; i++) {
        if (view.api->kapi_reserved[i] != KDI_ADDR_kapi_reserved_nosys) bad |= 1u << 1;
    }
    if (view.api->kapi_reserved[0]() != OS32_ERR_NOSYS) bad |= 1u << 1;
#endif
    /* (2) トランポリンの予約スロットは int 0x80 のスタブ (CPL=3 は kill) */
    for (i = (u32)KAPI_FUNC_COUNT; i < (u32)KAPI_FUNC_CAPACITY; i++) {
        const u8 *st = (const u8 *)(view.tramp_page + RING3_USTR_STUB_OFF + i * KAPI_STUB_SIZE);
        if (tbl[KAPI_FN_TABLE_FIRST + i] != (u32)st || st[0] != 0xB8 || st[1] != (u8)(i & 0xFF) ||
            st[2] != (u8)((i >> 8) & 0xFF) || st[5] != 0xCD || st[6] != 0x80) {
            bad |= 1u << 2;
        }
    }
    /* Slot 12 must enter the relocated CPL3 shim, never the ordinary stub. */
    if (tbl[KAPI_FN_TABLE_FIRST + KAPI_SLOT_SYS_LS] != view.tramp_page + RING3_LS_SHIM_OFF) {
        bad |= 1u << 2;
    }
    for (i = 0; i < (u32)(ring3_ls_end - ring3_ls_start); i++) {
        const u8 *shim = (const u8 *)(view.tramp_page + RING3_LS_SHIM_OFF);
        if (shim[i] != ring3_ls_start[i]) bad |= 1u << 2;
    }
    /* (3) ヘッダ v3 の照合: v2 → 断る / v3 値違い → 断る / 一致 → 通す */
    kmemset(&h, 0, sizeof(h));
    h.magic = OS32X_MAGIC;
    h.version = 2;
    h.header_size = OS32X_HDR_V2_SIZE;
    if (os32x_layout_check(&h, 4096u, KAPI_DATA_FIELDS_OFF) != OS32X_LAYOUT_OLD)
        bad |= 1u << 3;
    h.version = OS32X_HDR_VERSION;
    h.header_size = OS32X_HDR_SIZE;
    h.kapi_abi_generation = OS32_KAPI_ABI_GENERATION;
    h.memory_layout_generation = OS32_MEMORY_LAYOUT_GENERATION;
    h.kapi_data_off = (u32)KAPI_DATA_FIELDS_OFF - 4u;
    if (os32x_layout_check(&h, 4096u, KAPI_DATA_FIELDS_OFF) != OS32X_LAYOUT_MISMATCH)
        bad |= 1u << 3;
    h.kapi_data_off = (u32)KAPI_DATA_FIELDS_OFF;
    if (os32x_layout_check(&h, 4096u, KAPI_DATA_FIELDS_OFF) != OS32X_LAYOUT_OK)
        bad |= 1u << 3;
    return bad;
}

/* 写し場の番地とページ属性 (票 T9 §12 R1)。ビット 0..5 が落ちた項目
 * (0 = 全部通った)。 */
u32 kst_exec_tramp_user(void)
{
    struct kselftest_exec_view view;
    u32 bad = 0;
    u32 addr;
    u32 flags;
    const char *before;
    int saved = ring3_in_syscall;

    kdi_selftest_exec_view(&view);

    if (view.tramp_page == 0) return 1u;    /* exec_init より前 */
    addr = view.tramp_page + RING3_USTR_OFF;

    /* (0) 写し場は同じ 1 ページの中に収まっている (末尾の 1 バイトまで) */
    if (((addr + RING3_USTR_CAP - 1u) & ~(u32)(PAGE_SIZE - 1)) !=
        view.tramp_page) {
        bad |= 1u << 0;
    }
    /* スタブの領域と重ならない */
    if (addr < view.tramp_page + RING3_USTR_STUB_OFF +
               (u32)KAPI_FUNC_CAPACITY * KAPI_STUB_SIZE) {
        bad |= 1u << 0;
    }

    /* (1) CPL=3 から読める = PTE に PRESENT と USER。無ければ cd / pwd が
     * #PF で畳まれる (この blocker そのもの)。 */
    flags = paging_pte_flags(addr);
    if ((flags & PTE_PRESENT) == 0) bad |= 1u << 1;
    if ((flags & PTE_USER) == 0)    bad |= 1u << 1;

    /* (2) CPL=0 の呼び手には static cwd がそのまま返り、CPL=3 の呼び手には
     * 写しが返る (中身は同じ)。ガードは必ず元へ戻す。 */
    kdi_set_ring3_in_syscall(0);
    before = vfs_cwd_user();
    if (before != vfs_cwd()) bad |= 1u << 2;
    kdi_set_ring3_in_syscall(1);
    before = vfs_cwd_user();
    if (before != (const char *)addr) bad |= 1u << 2;
    if (kstrcmp(before, vfs_cwd()) != 0) bad |= 1u << 2;

    /* (3) vfs_devname("/") も CPL=3 には写しの番地を返す (TASK_HDD_INSTALL
     * 段 2: cdinst がカーネルのマウント表を読んで落ちた)。CPL=0 には元の番地 */
    before = vfs_devname_user("/");
    if (before != (const char *)addr || kstrcmp(before, vfs_devname("/")) != 0)
        bad |= 1u << 3;
    kdi_set_ring3_in_syscall(0);
    if (vfs_devname_user("/") != vfs_devname("/")) bad |= 1u << 3;

    /* (4) path_get_drive / path_get_cwd も同じ */
    kdi_set_ring3_in_syscall(1);
    before = path_get_drive_user();
    if (before != (const char *)addr || kstrcmp(before, path_get_drive()) != 0)
        bad |= 1u << 4;
    before = path_get_cwd_user();
    if (before != (const char *)addr || kstrcmp(before, path_get_cwd()) != 0)
        bad |= 1u << 4;
    kdi_set_ring3_in_syscall(0);
    if (path_get_drive_user() != path_get_drive() ||
        path_get_cwd_user() != path_get_cwd())
        bad |= 1u << 4;

    /* (5) WM の文脈 (ring3_wm_depth > 0) ではディスパッチ中でも元の番地が
     * 返る — WM はカーネル帯を直接読める常駐側 (2026-09-26)。 */
    kdi_set_ring3_in_syscall(1);
    ring3_wm_enter();
    if (vfs_cwd_user() != vfs_cwd() || vfs_devname_user("/") != vfs_devname("/"))
        bad |= 1u << 5;
    ring3_wm_leave();
    if (vfs_cwd_user() != (const char *)addr) bad |= 1u << 5;
    kdi_set_ring3_in_syscall(saved);

    return bad;
}
