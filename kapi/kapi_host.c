/* ======================================================================== */
/*  KAPI_HOST.C — Host Services KAPI v51 の __cdecl 側実体                    */
/*                                                                          */
/*  ABI の正典: docs/archive/network/TASK_N0.md §1a。                        */
/*                                                                          */
/*  ここの仕事は 3 つだけで、プロトコルには一切触らない:                      */
/*    (1) CPL=3 のポインタ / 長さの検証 (v50 と同じ 2 段の 2 段目。           */
/*        **先頭番地が帯外ならここへ来る前にディスパッチャが kill する** ―    */
/*        `ring3_syscall_dispatch` の `kapi_argptr` 早期検査。往復 3 の B8)   */
/*    (2) ユーザー領域 ⇔ カーネル領域の写し                                   */
/*    (3) 所有者 (`res_owner_get()`) を net/link.c へ渡す                     */
/*                                                                          */
/*  **複数の出力ポインタは全部を先に検証してから書く**。失敗時は 1 バイトも    */
/*  書かない (往復 4 の R4)。出力ポインタは NULL 可 (書かないだけ)。          */
/* ======================================================================== */

#include "kapi_host.h"
#include "link.h"               /* LINK_MAX_PAYLOAD */
#include "platform_host.h"
#include "kstring.h"
#include "range_internal.h"
#include "os32_kapi_shared.h"   /* OS32_ERR_* */

extern int res_owner_get(void);

/* 検証後の写し先。KAPI は再入しない (CPL=3 の呼び手は 1 本ずつ)。 */
static char host_req_buf[LINK_MAX_PAYLOAD];
static u8   host_wr_buf[LINK_MAX_PAYLOAD];

i32 kapi_host_open(const char *req, u32 len)
{
    if (req == 0 || len == 0 || len > LINK_MAX_PAYLOAD) return OS32_ERR_INVAL;
    if (!kapi_user_range_ok(req, len)) return OS32_ERR_INVAL;
    kmemcpy(host_req_buf, req, len);
    return platform_host_open(host_req_buf, len, res_owner_get());
}

i32 kapi_host_status(i32 h, u32 *status, u32 *length)
{
    u32 st = 0, ln = 0;
    i32 rc;

    /* **書く前に全部の出力ポインタを検証する** */
    if (status && !kapi_user_range_ok(status, sizeof(u32))) return OS32_ERR_INVAL;
    if (length && !kapi_user_range_ok(length, sizeof(u32))) return OS32_ERR_INVAL;
    rc = platform_host_status(h, &st, &ln, res_owner_get());
    if (rc != 0) return rc;                 /* 失敗時は出力を書かない */
    if (status) *status = st;
    if (length) *length = ln;
    return 0;
}

i32 kapi_host_read(i32 h, void *buf, u32 cap)
{
    const u8 *src = 0;
    i32 n;

    if (buf == 0 || cap == 0) return OS32_ERR_INVAL;
    if (!kapi_user_range_ok(buf, cap)) return OS32_ERR_INVAL;
    /* 成功確定点は link 側の最初の cli 区間。戻った時点で n は確定していて、
     * この写しの途中で再同期 / close が起きても戻り値は変えない
     * (次の呼び出しが STALE / INVAL を返す。往復 4 の R4)。 */
    n = platform_host_read_stage(h, cap, &src, res_owner_get());
    if (n > 0) kmemcpy(buf, src, (u32)n);
    return n;
}

i32 kapi_host_write(i32 h, const void *buf, u32 len)
{
    u32 n;

    if (buf == 0 || len == 0) return OS32_ERR_INVAL;
    if (!kapi_user_range_ok(buf, len)) return OS32_ERR_INVAL;
    /* 1 フレームに載る分だけ写す。受け付ける長さ (= min(len, 1400)) と
     * 「残り宣言長を超えたら INVAL」の判定は link 側が len を見て決める。 */
    n = (len > LINK_MAX_PAYLOAD) ? LINK_MAX_PAYLOAD : len;
    kmemcpy(host_wr_buf, buf, n);
    return platform_host_write(h, host_wr_buf, len, res_owner_get());
}

i32 kapi_host_close(i32 h)
{
    return platform_host_close(h, res_owner_get());
}

void host_owner_exit(int id)
{
    platform_host_owner_exit(id);
}
