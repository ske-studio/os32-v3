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
/*  LAN の出口 (T5、§2-8): flags の KDEBUG_PEEK_F_LAN が立てば、読めた     */
/*  チャンクを自分で SFS の片道フレーム (SFS_T_DUMP、セッション ID 欄 = tag、*/
/*  番号 = tag ごとの通し番号 0〜) に組み、lan.mod の e82557_send で送る。   */
/*  送信の戻りは AGAIN だけ 2 tick 再試行、他の負の値・期限切れは LAN (7)    */
/*  (L4)。成功はキュー投入まで (L5、欠けはホストが判定)。再送・ACK は無い。 */
/*  KDEBUG_OP_PEEK_LAN_PROBE は e82557_state()==RUNNING と宛先 (dst の明示か */
/*  SerialFS のペア) を確かめる。                                           */
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
#include "kdebug_window.h"
#include "drivers/net/e82557/e82557.h"
#include "fs/serialfs/sfs_proto.h"
#include "fs/serialfs/sfs_lan.h"
#include "fs/serialfs/serialfs.h"
#ifndef KPEEK_HOST
#include "io.h"
#include "generated/kdebug_import_macros.h"
#endif
#include "../lib/crc32_core.inc"

/* 窓の PT 1 枚 (デバイス窓の先頭 4MB) の中のオフセット */
#define KPEEK_WIN_MASK    (MEM_DEVICE_APERTURE_PDE_SIZE - 1U)
#define KPEEK_PAGE_MASK   ((u32)PAGE_SIZE - 1U)
/* LAN の出口 (§2-8): フレーム = Ethernet 14 + SFS ヘッダ 12 + [addr u32][data]
 * + CRC32 4 (最大 1058 B、bss)。ペイロードは SFS_LAN_MAX_PAYLOAD に収まる。 */
#define KPEEK_LAN_ADDR_LEN   4U
#define KPEEK_LAN_PAYLOAD    (KPEEK_LAN_ADDR_LEN + KDEBUG_PEEK_CHUNK)
#define KPEEK_LAN_FRAME      (SFS_LAN_HDR_LEN + SFS_HDR_LEN + KPEEK_LAN_PAYLOAD + SFS_CRC_LEN)
#define KPEEK_LAN_RETRY_TICKS 2U         /* AGAIN の再試行の上限 (L4) */
STATIC_ASSERT(KPEEK_LAN_PAYLOAD <= SFS_LAN_MAX_PAYLOAD, kpeek_lan_payload_fits);

static u8 kpeek_lan_frame[KPEEK_LAN_FRAME];
static u32 kpeek_lan_tag;              /* 直前に送った tag */
static u16 kpeek_lan_seq;              /* その tag の次の通し番号 */
static u8 kpeek_lan_have;              /* 1 = tag が有効 */

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
    if ((req->addr & KPEEK_WIN_MASK) + req->len > MEM_DEVICE_APERTURE_PDE_SIZE) {
        req->fail_addr = req->addr;
        return KDEBUG_PEEK_ARG;
    }
    if (kpeek_allow_check(req->addr, req->len, req->width) != KDEBUG_PEEK_OK) {
        req->fail_addr = req->addr;
        return KDEBUG_PEEK_DENY;
    }
    struct recover_window window;
    recover_window_hal_init(&window);
    window.va += (req->addr & ~KPEEK_PAGE_MASK) & KPEEK_WIN_MASK;
    u32 st = KDEBUG_PEEK_WINDOW;
    if (!kdebug_window_begin(&window, req->addr, req->len, false)) {
        kpeek_load(window.va + (req->addr & KPEEK_PAGE_MASK), req->width, req->len, req->data);
        if (!recover_window_cleanup(&window)) st = KDEBUG_PEEK_OK;
    }
    if (st != KDEBUG_PEEK_OK) req->fail_addr = req->addr;
    return st;
}

static void kpeek_put16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void kpeek_put32(u8 *p, u32 v)
{
    kpeek_put16(p, v);
    kpeek_put16(p + 2, v >> 16);
}

/* 読めたチャンクを SFS_T_DUMP の 1 フレームで送る。戻り OK / LAN。 */
static u32 kpeek_lan_send(const KdebugPeek *req)
{
    u8 *f = kpeek_lan_frame;
    u8 *sfs = f + SFS_LAN_HDR_LEN;
    u32 plen = KPEEK_LAN_ADDR_LEN + req->len;
    u32 size, i, start, crc;
    int rc;

    if (!kpeek_lan_have || req->tag != kpeek_lan_tag) {
        kpeek_lan_tag = req->tag;
        kpeek_lan_seq = 0;
        kpeek_lan_have = 1;
    }
    for (i = 0; i < SFS_LAN_MAC_LEN; i++) f[i] = req->dst[i];
    e82557_get_mac(f + SFS_LAN_MAC_LEN);
    f[2 * SFS_LAN_MAC_LEN] = (u8)(SFS_LAN_ETHERTYPE >> 8);
    f[2 * SFS_LAN_MAC_LEN + 1] = (u8)(SFS_LAN_ETHERTYPE & 0xFF);
    sfs[0] = SFS_ENQ;
    sfs[1] = SFS_MAGIC1;
    sfs[2] = SFS_MAGIC2;
    sfs[3] = SFS_T_DUMP;
    kpeek_put32(sfs + 4, req->tag);
    kpeek_put16(sfs + 8, kpeek_lan_seq);
    kpeek_put16(sfs + 10, plen);
    kpeek_put32(sfs + SFS_HDR_LEN, req->addr);
    for (i = 0; i < req->len; i++)
        sfs[SFS_HDR_LEN + KPEEK_LAN_ADDR_LEN + i] = req->data[i];
    /* CRC32 は種別からペイロードの末尾まで (fs/serialfs/sfs_proto.h) */
    crc = crc32_core_final(crc32_core_update(CRC32_INIT, sfs + 3,
                                             SFS_HDR_LEN - 3U + plen));
    kpeek_put32(sfs + SFS_HDR_LEN + plen, crc);
    size = SFS_LAN_HDR_LEN + SFS_HDR_LEN + plen + SFS_CRC_LEN;
    while (size < SFS_LAN_MIN_FRAME) f[size++] = 0;

    start = tick_count;
    for (;;) {
        rc = e82557_send(f, size);
        if (rc == 0) break;
        if (rc != OS32_ERR_AGAIN ||
            (u32)(tick_count - start) >= KPEEK_LAN_RETRY_TICKS)
            return KDEBUG_PEEK_LAN;
    }
    kpeek_lan_seq++;
    return KDEBUG_PEEK_OK;
}

/* KDEBUG_OP_PEEK_LAN_PROBE: status = OK (dst に宛先) / LAN、fail_addr =
 * e82557_state() の値 (shell が理由の 1 行に使う)。dst が全部 0 なら
 * SerialFS のペアを写す。 */
static u32 kpeek_lan_probe(KdebugPeek *req)
{
    u32 i, any = 0;
    int state = e82557_state();
    req->fail_addr = (u32)state;
    if (state != E82557_RUNNING) return KDEBUG_PEEK_LAN;
    for (i = 0; i < KDEBUG_PEEK_MAC_LEN; i++) any |= req->dst[i];
    if (!any && sfs_lan_peer_mac(req->dst) != 0) return KDEBUG_PEEK_LAN;
    return KDEBUG_PEEK_OK;
}

static u32 kpeek_run(KdebugPeek *req)
{
    u32 w = req->width;
    u32 st;
    if (w != 1U && w != 2U && w != 4U) return KDEBUG_PEEK_WIDTH;
    if (req->len == 0 || req->len > KDEBUG_PEEK_CHUNK) return KDEBUG_PEEK_ARG;
    if ((req->addr | req->len) & (w - 1U)) return KDEBUG_PEEK_ARG;
    if (req->addr + (req->len - 1U) < req->addr) return KDEBUG_PEEK_ARG;
    if (req->flags & ~(u32)KDEBUG_PEEK_F_LAN) return KDEBUG_PEEK_ARG;
    if (req->space == KDEBUG_PEEK_SPACE_RAM) st = kpeek_ram(req);
    else if (req->space == KDEBUG_PEEK_SPACE_MMIO) st = kpeek_mmio(req);
    else return KDEBUG_PEEK_ARG;
    if (st == KDEBUG_PEEK_OK && (req->flags & KDEBUG_PEEK_F_LAN)) {
        st = kpeek_lan_send(req);
        if (st != KDEBUG_PEEK_OK) req->fail_addr = req->addr;
    }
    return st;
}

/* ops slot 19。戻り 0 = 要求を扱った (結果は status)、OS32_ERR_INVAL =
 * buf / size が KdebugPeek でない (1 バイトも書かない)、OS32_ERR_NOSYS =
 * 知らない op。 */
i32 kpeek_call(u32 op, void *buf, u32 size)
{
    KdebugPeek *req = (KdebugPeek *)buf;
    if (op != KDEBUG_OP_PEEK && op != KDEBUG_OP_PEEK_LAN_PROBE)
        return OS32_ERR_NOSYS;
    if (!req || size != sizeof(KdebugPeek)) return OS32_ERR_INVAL;
    if (op == KDEBUG_OP_PEEK_LAN_PROBE) {
        req->status = kpeek_lan_probe(req);
        return 0;
    }
    req->fail_addr = 0;
    req->status = kpeek_run(req);
    if (req->status == KDEBUG_PEEK_ARG || req->status == KDEBUG_PEEK_WIDTH) {
        if (!req->fail_addr) req->fail_addr = req->addr;
    }
    return 0;
}
