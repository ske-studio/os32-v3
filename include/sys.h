/* ======================================================================== */
/*  SYS.H — システム制御およびハードウェア制御API                           */
/* ======================================================================== */

#ifndef __SYS_H
#define __SYS_H

#include "types.h"

/* Internal reservation CORE only, not a device activation permission.
 * Trusted caller capability: IDLE attests master CR3, no live AS or CPL0 exec;
 * RAM_MAPPED attests the entire mapped PFN interval is stable supervisor
 * identity RW/cacheable RAM for kernel lifetime. No probe/map callback runs.
 * No current GUI/backend supplies this capability: integration is pending.
 * Owner is a nonzero kernel-lifetime ID, never a process/nesting ID.
 * Spans are PFN half-open intervals (4GiB end is representable). The normalized
 * whole set is immutable per owner; exact retries succeed without new claims.
 * RAM means newly acquired permanent RAM (BB), MMIO never proves RAM/mapping.
 * No release/reset API. Success means RESERVED, not prepared/enabled hardware.
 * Exact RAM placement is supplied by the trusted caller, then acquired in
 * this same transaction; this core does not search, lend low display windows,
 * whitelist device constants, authenticate capabilities, or activate devices.
 * The complete original legacy arena and current sys low fixed extent remain
 * protected, even when their pages are temporarily free. */
#define SYS_DEVICE_MAX_SPANS 16
#define SYS_DEVICE_MMIO 1
#define SYS_DEVICE_RAM 2
#define SYS_DEVICE_IDLE 1
#define SYS_DEVICE_RAM_MAPPED 2
struct sys_device_span { u32 first, end, kind; };
struct sys_device_capability { u32 flags, mapped_first, mapped_end; };
int sys_device_reserve_core(u32 owner, const struct sys_device_span *spans,
                            u32 count, const struct sys_device_capability *cap);

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

/* 物理末尾側 (使用可能上限の直下) に bytes バイトを固定予約し、先頭物理を
 * 返す。以後 sys_usable_mem_end() はその分下がる。ブート中に 1 回だけ。
 * 戻り値 0 = 予約できなかった。→ kernel/sys.c の説明 */
u32 sys_reserve_top(u32 bytes);

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
