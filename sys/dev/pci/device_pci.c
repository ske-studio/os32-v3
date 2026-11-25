#include "platform_device.h"
#include "pci_contract.h"
#include "pci_bind_contract.h"
#include "user_read_contract.h"
#include "exec.h"

int platform_pci_count(void)
{
    return pci_count();
}

int platform_pci_get(u32 idx, void *out)
{
    return pci_get(idx, out);
}

u32 platform_pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{
    return pci_cfg_read32(bus, dev, fn, reg);
}

int platform_pci_bind_info_get(int idx, struct pci_bind_info *out)
{
    return pci_bind_info_get(idx, out);
}

/* ======================================================================== */
/*  pci_bind_info — 結線の診断の取得口 (票 TASK_HAL_WIRING §1-4 / KAPI v60) */
/*                                                                          */
/*  生成される __cdecl ラッパ (`wrap_pci_bind_info`) がここを呼ぶ ([ABI])。   */
/*  記録そのものは `sys/dev/pci/pci_bind.c` の `pci_bind_info_get()` が持つ。    */
/*  ここの仕事は CPL=3 のポインタ検証と **8 バイトちょうどの写し** だけ。    */
/*                                                                          */
/*  `pci_get` (v58、40 バイト) と**同じ列挙順の idx** で引く。既存の         */
/*  40 バイトは広げない (旧呼び手のバッファを踏む) ので、別の口にしてある。  */
/*                                                                          */
/*  出力ポインタの扱いは `kapi_sys_time_now` と同じ規則 (実装 A):            */
/*    (1) `out` が NULL → `OS32_ERR_INVAL` (1 バイトも書かない)             */
/*    (2) 8 バイトが帯の境界を跨ぐ → `OS32_ERR_INVAL`                        */
/*    (3) 書く前に present + RW + USER を `ring3_user_ranges_writable` で    */
/*        確かめ、落ちたら `ring3_fault_kill()` (戻らない)。OS32 は          */
/*        CR0.WP = 0 なので、共有ライブラリの `.text` を渡されても           */
/*        ハードウェアは止めない。                                          */
/*  **ローカルの 1 つの写しへ受けてから出す** — `pci_bind_info_get` は       */
/*  `line_state` を読む時点で合成するので、途中で線の様子が変わっても        */
/*  呼び手が見るのは 1 つのスナップショット。                               */
/* ======================================================================== */
int kapi_pci_bind_info(u32 idx, void *out)
{
    struct pci_bind_info snap;
    const u8 *src;
    u8 *dst;
    int i, rc;

    if (!out) return OS32_ERR_INVAL;
    if ((u32)out + (u32)PCI_BIND_INFO_SIZE < (u32)out) return OS32_ERR_INVAL;
    if (!ring3_user_range_ok((u32)out, (u32)PCI_BIND_INFO_SIZE))
        return OS32_ERR_INVAL;

    /* **書く前に**書けることを確かめる (読み取り専用の USER ページは kill)。 */
    if (!ring3_user_ranges_writable((u32)out, (u32)PCI_BIND_INFO_SIZE, 0, 0)) {
        ring3_fault_kill();                 /* 戻らない (longjmp) */
    }

    rc = platform_pci_bind_info_get((int)idx, &snap);
    if (rc != 0) return OS32_ERR_INVAL;     /* 範囲外 — 出力は書かない */

    src = (const u8 *)&snap;
    dst = (u8 *)out;
    for (i = 0; i < PCI_BIND_INFO_SIZE; i++) dst[i] = src[i];
    return 0;
}
