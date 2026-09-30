/* ======================================================================== */
/*  SYS.H — システム制御およびハードウェア制御API                           */
/* ======================================================================== */

#ifndef __SYS_H
#define __SYS_H

#include "types.h"

void sys_reboot(void);
void sys_halt(void);
void buz_on(void);
void buz_off(void);

/* Boot-only model handoff (metadata-only variant, used by host tests);
 * metadata must occupy the contiguous low RAM tail. Freezes exec below
 * metadata; verify has pgalloc_init_model's mapping contract.
 * Does not map high RAM or authorize general high-address dereferences. */
struct physmem;
struct pgalloc_layout;
/* Staged boot, called once by memory_boot_init (the only path since T1a).
 * Layout is either ARENA_TOP — [final exec][low PT workspace][metadata] up to
 * real RAM end — or FIXED — metadata + workspace in [MEM_LEDGER_META_BASE,
 * MEM_LEDGER_META_END) with the whole low RAM left to exec (TASK_T1_LEDGER
 * §3-3). Both claims validate atomically before any backing/model/sys state
 * changes. The exec ceiling is frozen from pgalloc_arena_end() in both kinds.
 * Pass paging_verify_identity after paging_init. BOOTSTRAP denies
 * all general allocations; stage maps eligible high RAM and only then
 * publishes ONLINE. Master + no live AS required for the WHOLE stage.
 * Failure after bootstrap is fail-stop: earlier maps may remain, allocator
 * stays BOOTSTRAP. No firmware detection or device activation is implied. */
int sys_memory_bootstrap_model(struct physmem *model,
                               const struct pgalloc_layout *layout,
                               int (*verify)(u32, u32, void *));
int sys_memory_stage_online(void);
int sys_memory_init_model(struct physmem *model, void *backing, u32 capacity,
                          u32 first_pfn, int (*verify)(u32, u32, void *));
u32 sys_get_mem_kb(void);
/* RTC の現在時刻 (UNIX 秒)。KAPI sys_time の実体 (kernel/sys.c)。定義側の戻り型は
 * os_time_t (= u32、os32_kapi_shared.h)。ここは os32_kapi_shared.h を引かない
 * (ホスト試験が sys.h だけを読む) ので同じ型の u32 で宣言する */
u32 sys_time(void);
u32 sys_usable_mem_end(void);


/* ======================================================================== */
/*  sys_time_now — 起動からの経過を µs で (票 TASK_HAL_WIRING §1-5)         */
/*                                                                          */
/*  tick_count + PIT ch0 のラッチ読みを 64bit の µs に組み、上下 2 本の出力  */
/*  引数に**同じスナップショットから**書く (KAPI は 64bit を返せない)。      */
/*  戻り 0 = 成功 / OS32_ERR_AGAIN = 周期境界で 3 回続けて判定できなかった / */
/*  OS32_ERR_NOSYS = PIT 未初期化か mode 2 でない。**負なら出力は不変**。    */
/*  実体は kernel/ktime.c、判定と算数は kernel/time_math.c。                 */
/*  CPL=3 のポインタ検証は kapi/kapi_sys.c の kapi_sys_time_now が行う。     */
/* ======================================================================== */
int sys_time_now(u32 *lo, u32 *hi);

#endif /* __SYS_H */
