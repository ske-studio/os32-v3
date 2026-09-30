/* ======================================================================== */
/*  PGALLOC.H — 物理ページフレームアロケータ                                 */
/*                                                                          */
/*  プログラム空間 (0x400000〜mem_end) の物理ページを                        */
/*  ビットマップ方式で管理する。                                              */
/*  exec_run() のネスト呼び出し時に、子プロセス用の物理ページを              */
/*  動的に確保・解放するために使用する。                                      */
/* ======================================================================== */

#ifndef __PGALLOC_H
#define __PGALLOC_H

#include "types.h"
#include "sys.h"
/* Allocator-internal broker entry; use sys_device_reserve_core. */
int pgalloc_device_reserve(u32 owner, const struct sys_device_span *spans,
                           u32 count, const struct sys_device_capability *cap,
                           u32 fixed_end);
#include "paging.h"

/* 管理対象の開始アドレス (プログラム空間) */
#define PGALLOC_BASE    0x400000UL

/* ======== API ======== */
#include "physmem.h"
/* Boot-only handoff. Model must be normalized and backing must be unused,
 * page-aligned, identity-mapped bootstrap RAM outside the final exec arena.
 * verify must inspect every backing PTE: supervisor identity RW, no cache
 * disable/write-through or guard, with stable mapping for kernel lifetime.
 * The callback is a trusted platform boundary, not a detector. No callback
 * may reenter allocator or mutate model. Failure changes nothing. Success
 * reserves backing in model and snapshots eligibility; do not edit that model
 * as a live allocator interface afterwards. No reset is provided.
 * backing_bytes is capacity; only metadata_bytes(model) is reserved/touched.
 * PGALLOC_HOST_TEST=1 permits host backing pointers only outside kernel builds.
 * All MODEL general allocation stays blocked in BOOTSTRAP. Metadata-only
 * pgalloc_init_model is retained for ownership unit integration; without a
 * workspace it cannot publish ONLINE. Production staging uses init_layout. */
#define PGALLOC_BOOTSTRAP 1
#define PGALLOC_ONLINE 2
/* Model-only boot layout: [workspace_first,workspace_end) and metadata are
 * disjoint, mapped low RAM tails above final exec, up to real RAM end.
 * Workspace is permanently excluded from general allocation. */
struct pgalloc_layout {
    void *metadata;
    u32 capacity, metadata_first, workspace_first, workspace_end;
};
int pgalloc_init_layout(struct physmem *model, const struct pgalloc_layout *layout,
                        int (*verify)(u32, u32, void *));
/* 0 denotes the legacy/uninitialized path. No public setter or raw high-PFN
 * allocation escape hatch. Stage maps the frozen eligibility snapshot. */
int pgalloc_model_state(void);
int pgalloc_stage_online(void);
/* Paging-internal workspace only; no fallback, never general allocations. */
u32 pgalloc_alloc_pt(void);
void pgalloc_free_pt(u32 phys);
u32 pgalloc_metadata_bytes(const struct physmem *model);
int pgalloc_init_model(struct physmem *model, void *backing, u32 backing_bytes,
                      u32 backing_pfn, int (*verify)(u32, u32, void *));
/* Explicit PFN interface, gated by ONLINE just like pointer-returning APIs.
 * MODEL success only follows complete verified high RAM identity mapping.
 * Success is separate so PFN zero and the final non-PAE page are representable.
 * All failures preserve output/counters/bits. Range endpoints are PFNs. */
int pgalloc_alloc_n_pfn(int n, u32 first, u32 end, u32 *pfn);
int pgalloc_free_n_pfn(u32 first, int n);
/* Permanent exclusion from eligibility; free/mark cannot undo it.
 * Any live allocation in the range rejects the entire reservation.
 * Fixed provenance is retained even for UNKNOWN pages; if its bounded
 * PHYSMEM_MAX_RANGES snapshot cannot represent the claim, fail unchanged. */
int pgalloc_reserve_pfn(u32 first, u32 end);
u32 pgalloc_limit_pfn(void);

/* Legacy one-shot initialization after paging_init. Repeated calls are no-op.
 * Small kernel-owned backing, same PFN core; this fallback alone never admits
 * RAM above the old loader's reportable extent (PHYSMEM_LEGACY_MAX_PFN).
 * High RAM reaches the pool only through the model path (K6-RAM). */
void pgalloc_init(u32 mem_kb);

/* 1ページ(4KB)確保。戻り値: 物理アドレス, 0=失敗 */
u32  pgalloc_alloc_page(void);

/* 1ページ解放 */
void pgalloc_free_page(u32 phys_addr);

/* 連続nページ確保。戻り値: 先頭物理アドレス, 0=失敗 */
u32  pgalloc_alloc_n(int n);

/* カーネル内部用: [lo, hi) 内の連続 n ページ (n > 0) を確保。
 * lo/hi はページ境界、範囲全体が初期化済みの管理域内であること。
 * 空/逆順/非整列/管理域外の範囲は切り詰めず拒否する。
 * 戻り値は先頭物理アドレス、0 は失敗 (ビットマップ/統計は不変)。
 * 範囲外へのフォールバックはしない。既存の free_page/free_n で解放可能。
 * 単一 CPU で探索と全ページ予約を IRQ 禁止区間内に行い、元の IF を復元する。
 * 全 mutator は同じ IF 保存契約。SMP/NMI 同期は対象外。
 * 明示的な物理範囲 API であり、返却ページの mapping は呼出側の責任。 */
u32 pgalloc_alloc_n_range(int n, u32 lo, u32 hi);

/* 全ページが eligible && allocated の場合だけ一括解放。
 * 非整列/範囲外/予約/二重解放が混在する場合は全件不変。 */
void pgalloc_free_n(u32 phys_addr, int n);

/* 旧固定アドレス利用者用の解放可能な claim。eligible な未確保ページだけ
 * allocated にし、確保済みへの再マークは冪等 (参照数は増やさない)。
 * 永久予約/metadata/非 RAM は不変。total_pages (eligible union) は変えない。
 * 非整列/非正数/管理域外は全件不変。検査と更新は IF 保存の IRQ 区間内。
 * free は上記の全ページ検証に従うため、永久予約との混在範囲は解放不可。
 * 永久予約には pgalloc_reserve_pfn を明示的に使う。 */
void pgalloc_mark_used(u32 phys_start, int page_count);

/* デバイス窓 [first, end) (PFN 半開) に「RAM として登録された物理ページ」が
 * 1 枚でもあるか。1 = ある (= そこへデバイス窓を張ってはいけない)。
 * ブート時に凍結した物理地図を physmem_count で見るだけで、状態は変えない。
 * 未初期化・範囲異常は保守的に 1 (RAM あり扱い) を返す。
 * **RAM の上端で窓の可否を決めてはならない** (K6-RAM 以後、上端と「RAM に
 * ならない番地」は一致しない。15MB 機の上端は 17MB で、穴は 15-16MB)。
 * eligible ビットマップではなく地図を見るので、他所の永久予約で
 * eligible が落ちた RAM も「RAM あり」のまま数える。 */
int  pgalloc_range_has_ram(u32 first, u32 end);

/* eligible union / eligible minus allocated。上端は limit_pfn() を使う。
 * BASE + total_pages * PAGE_SIZE からアドレス上端を復元してはならない。 */
u32  pgalloc_total_pages(void);
u32  pgalloc_free_pages(void);

#endif /* __PGALLOC_H */
