/* ======================================================================== */
/*  KPEEK.C — kdebug_call の peek 本体 (TASK_KPEEK §2-4)                      */
/*                                                                          */
/*  KAPI kdebug_call (slot 253) → kdebug ops slot 19 dbg_call → kpeek_call。 */
/*  読むだけで、書く口は持たない。1 回の呼び出しで最大 KDEBUG_PEEK_CHUNK    */
/*  バイトを KdebugPeek.data へ写し、結果は status / fail_addr。              */
/*                                                                          */
/*  -k / -p (RAM、恒等写像): 範囲の全ページを先に調べる — 写像が無ければ    */
/*  NOMAP、PCD/PWT のページ (装置) と [A0000h, E8000h) は DENY (K1・R2)。    */
/*  1 ページでも断れば 1 バイトも読まない。fail_addr はそのページの先頭。    */
/*                                                                          */
/*  -m (MMIO): 4MB (窓の PT 1 枚) をまたぐ要求は ARG (K6)。許可リスト       */
/*  (kpeek_allow.c) に収まらなければ DENY。デバイス窓の先頭 4MB の PT の     */
/*  触る PTE (最大 2) だけ退避し、読み専用 (RW なし・PCD・supervisor) で     */
/*  張って幅どおりに読み、退避した生値を書き戻す。張る・読む・戻すは全部     */
/*  irq_save の中 (K7)。張るのに失敗しても戻しは必ず通る。                   */
/*                                                                          */
/*  シングルタスク・協調切替。呼ぶのは KAPI の通常文脈だけ。                */
/* ======================================================================== */
#include "types.h"
#include "os32_kapi_shared.h"
#include "memmap.h"
#include "pc98.h"
#include "paging.h"
#include "kpeek_allow.h"
#include "kpeek.h"
#ifndef KPEEK_HOST
#include "io.h"
#endif

/* 窓の PT 1 枚 (デバイス窓の先頭 4MB) の中のオフセット */
#define KPEEK_WIN_MASK    (MEM_DEVICE_APERTURE_PDE_SIZE - 1U)
#define KPEEK_PAGE_MASK   ((u32)PAGE_SIZE - 1U)
/* 窓の張り方: 読み専用・キャッシュ無効・supervisor (K7) */
#define KPEEK_WIN_FLAGS   (PTE_PRESENT | PTE_PCD)
/* 1 回で触る PTE の上限 (len <= CHUNK < PAGE_SIZE なので 2) */
#define KPEEK_WIN_PAGES   2U

#ifndef KPEEK_HOST
/* 計算した番地 (恒等の RAM、または張ったばかりの窓の VA) から幅どおりに
 * 読む。装置は幅で振る舞いが変わるので幅をそのまま使う。リトルエンディアン
 * のまま data へ並べる。 */
static void kpeek_load(u32 addr, u32 width, u32 len, u8 *out)
{
    u32 i;
    for (i = 0; i < len; i += width) {
        volatile void *p = P2V_IO(addr + i);
        u32 v;
        if (width == 4) v = *(volatile u32 *)p;
        else if (width == 2) v = *(volatile u16 *)p;
        else v = *(volatile u8 *)p;
        out[i] = (u8)v;
        if (width >= 2) out[i + 1] = (u8)(v >> 8);
        if (width == 4) {
            out[i + 2] = (u8)(v >> 16);
            out[i + 3] = (u8)(v >> 24);
        }
    }
}
#endif

static u32 kpeek_ram(KdebugPeek *req)
{
    u32 page = req->addr & ~KPEEK_PAGE_MASK;
    u32 last = (req->addr + req->len - 1U) & ~KPEEK_PAGE_MASK;
    for (;;) {
        u32 st = KDEBUG_PEEK_OK;
        if (!paging_is_present(page))
            st = KDEBUG_PEEK_NOMAP;
        else if (paging_pte_flags(page) & (PTE_PCD | PTE_PWT))
            st = KDEBUG_PEEK_DENY;
        else if (page >= KPEEK_RAM_DENY_LO && page < KPEEK_RAM_DENY_HI)
            st = KDEBUG_PEEK_DENY;
        if (st != KDEBUG_PEEK_OK) {
            req->fail_addr = page;
            return st;
        }
        if (page == last) break;
        page += PAGE_SIZE;
    }
    kpeek_load(req->addr, req->width, req->len, req->data);
    return KDEBUG_PEEK_OK;
}

static u32 kpeek_mmio(KdebugPeek *req)
{
    u32 raw[KPEEK_WIN_PAGES];
    u32 pa0, va0, n, i, st;
    unsigned int fl;

    if ((req->addr & KPEEK_WIN_MASK) + req->len > MEM_DEVICE_APERTURE_PDE_SIZE) {
        req->fail_addr = req->addr;
        return KDEBUG_PEEK_ARG;
    }
    if (kpeek_allow_check(req->addr, req->len, req->width) != KDEBUG_PEEK_OK) {
        req->fail_addr = req->addr;
        return KDEBUG_PEEK_DENY;
    }
    pa0 = req->addr & ~KPEEK_PAGE_MASK;
    n = ((req->addr + req->len - 1U) >> PAGE_SHIFT) - (req->addr >> PAGE_SHIFT) + 1U;
    if (n > KPEEK_WIN_PAGES) {           /* len <= CHUNK では起きない */
        req->fail_addr = req->addr;
        return KDEBUG_PEEK_ARG;
    }
    va0 = (u32)MEM_DEVICE_APERTURE_BASE + (pa0 & KPEEK_WIN_MASK);

    fl = irq_save();
    for (i = 0; i < n; i++) raw[i] = paging_pte_raw(va0 + i * PAGE_SIZE);
    if (paging_map_phys(va0, pa0, n, KPEEK_WIN_FLAGS) == 0) {
        kpeek_load(va0 + (req->addr - pa0), req->width, req->len, req->data);
        st = KDEBUG_PEEK_OK;
    } else {
        st = KDEBUG_PEEK_WINDOW;
    }
    for (i = 0; i < n; i++) {
        if (paging_pte_restore(va0 + i * PAGE_SIZE, raw[i]) != 0)
            st = KDEBUG_PEEK_WINDOW;
    }
    irq_restore(fl);
    if (st != KDEBUG_PEEK_OK) req->fail_addr = req->addr;
    return st;
}

static u32 kpeek_run(KdebugPeek *req)
{
    u32 w = req->width;
    if (w != 1U && w != 2U && w != 4U) return KDEBUG_PEEK_WIDTH;
    if (req->len == 0 || req->len > KDEBUG_PEEK_CHUNK) return KDEBUG_PEEK_ARG;
    if ((req->addr | req->len) & (w - 1U)) return KDEBUG_PEEK_ARG;
    if (req->addr + (req->len - 1U) < req->addr) return KDEBUG_PEEK_ARG;
    /* LAN の出口は T5。それまでは flags を受けない。 */
    if (req->flags != 0) return KDEBUG_PEEK_ARG;
    if (req->space == KDEBUG_PEEK_SPACE_RAM) return kpeek_ram(req);
    if (req->space == KDEBUG_PEEK_SPACE_MMIO) return kpeek_mmio(req);
    return KDEBUG_PEEK_ARG;
}

/* ops slot 19。戻り 0 = 要求を扱った (結果は status)、OS32_ERR_INVAL =
 * buf / size が KdebugPeek でない (1 バイトも書かない)、OS32_ERR_NOSYS =
 * 知らない op (LAN の probe は T5)。 */
i32 kpeek_call(u32 op, void *buf, u32 size)
{
    KdebugPeek *req = (KdebugPeek *)buf;
    if (op != KDEBUG_OP_PEEK) return OS32_ERR_NOSYS;
    if (!req || size != sizeof(KdebugPeek)) return OS32_ERR_INVAL;
    req->fail_addr = 0;
    req->status = kpeek_run(req);
    if (req->status == KDEBUG_PEEK_ARG || req->status == KDEBUG_PEEK_WIDTH) {
        if (!req->fail_addr) req->fail_addr = req->addr;
    }
    return 0;
}
