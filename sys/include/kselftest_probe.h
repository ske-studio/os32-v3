/* ======================================================================== */
/*  KSELFTEST_PROBE.H — kdebug へ移した起動自己試験だけが使う私用の口         */
/*  (票 TASK_SLIM S3、設計 SLIM_KERNEL §2)。kdebug の import 表から引く。     */
/*  KAPI ではない。実体は状態を持つ側 (kbd_inject.c・lease.c・exec.c)。       */
/* ======================================================================== */
#ifndef KSELFTEST_PROBE_H
#define KSELFTEST_PROBE_H

#include "types.h"
#include "os32_kapi_shared.h"

#define KSELFTEST_PROBE_VERSION 1u

/* inj_push をそのまま返す。呼べる条件は inj_push と同じで、錠は足さない。 */
u32 kdi_selftest_inj_push(const u8 *p, u32 len, int dest);

/* lease 自己試験の結果をカーネル側の観測変数 (lease_selftest_result) へ書く。
 * ブートの診断と道具は従来どおりカーネルの番地から読む。 */
void kdi_selftest_lease_result(u32 result);

struct kselftest_exec_view {
    u32 tramp_page;       /* 初期化済みの踏み台ページ。exec_init より前は 0 */
    const KernelAPI *api; /* カーネルの KAPI 表。読むだけ */
};
/* 上の 2 つを写す。exec_tramp_page_addr (整列した番地) は初期化の印にならない。 */
void kdi_selftest_exec_view(struct kselftest_exec_view *out);

#endif /* KSELFTEST_PROBE_H */
