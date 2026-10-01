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
#include "paging.h"

/* 管理対象の開始アドレスは include/memmap.h の MEM_POOL_BASE (物理側の定数)。 */

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
 * disjoint and permanently excluded from general allocation. Where they live
 * is the backing kind (TASK_T1_LEDGER §3-3):
 *   ARENA_TOP  mapped low RAM tails above final exec, up to real RAM end,
 *              reserved from RAM; workspace at/above MEM_PHYS_WORKSPACE_FLOOR.
 *   FIXED      [MEM_LEDGER_META_BASE, MEM_LEDGER_META_END) — not RAM in the
 *              model (RESERVED), mapped supervisor RW only on this kind.
 * kind 0 is ARENA_TOP so older positional initialisers keep their meaning. */
#define PGALLOC_BACKING_ARENA_TOP 0
#define PGALLOC_BACKING_FIXED     1
struct pgalloc_layout {
    void *metadata;
    u32 capacity, metadata_first, workspace_first, workspace_end;
    u32 kind;
};
int pgalloc_init_layout(struct physmem *model, const struct pgalloc_layout *layout,
                        int (*verify)(u32, u32, void *));
/* 0 denotes the uninitialized allocator. No public setter or raw high-PFN
 * allocation escape hatch. Stage maps the frozen eligibility snapshot. */
int pgalloc_model_state(void);
int pgalloc_stage_online(void);
/* Paging-internal workspace only; no fallback, never general allocations. */
u32 pgalloc_alloc_pt(void);
void pgalloc_free_pt(u32 phys);
u32 pgalloc_metadata_bytes(const struct physmem *model);
int pgalloc_init_model(struct physmem *model, void *backing, u32 backing_bytes,
                      u32 backing_pfn, int (*verify)(u32, u32, void *));
u32 pgalloc_limit_pfn(void);

/* End PFN (exclusive) of the legacy arena — the contiguous low RAM from
 * MEM_PHYS_EXEC_FLOOR — after the model reserved its backing, frozen at init.
 * ARENA_TOP: equals workspace_first. FIXED: the low RAM top (0x800 on 8MiB),
 * independent of where the backing sits. 0 before the model is initialised.
 * sys freezes the exec ceiling from this (sys_frozen_exec, B2). There is no
 * legacy pgalloc_init any more: every configuration takes the model path. */
u32 pgalloc_arena_end(void);

/* ======================================================================== */
/*  所有権台帳 (TASK_T1_LEDGER §3-1・§3-2、T1b)                              */
/*                                                                          */
/*  L0 物理地図 (physmem) と L1 (eligible / allocated の 2 bitmap) は今まで  */
/*  どおり。L2 = PFN ごとの owner 番号 (u8、0 = 空き) を metadata の同じ塊に  */
/*  足し、不変条件 **eligible なページでは allocated ⇔ owner ≠ 0** を全     */
/*  mutator が同じ IRQ 保存区間で守る (失敗なら両方不変、X11)。永久予約は    */
/*  eligible を落とし、L2 に予約した owner を残す。                           */
/*  L3 = 区間の表 (固定用途・背景・DMA・装置の予約、BSS)。資源の表の登録は  */
/*  T1d、SURFACE の表は型と置き場だけ (登録 API は T1e)。                    */
/*                                                                          */
/*  全 mutator は「全部検査してから commit」。失敗時は L1・L2・L3・**会計**   */
/*  (owner の pages、total/used、区間の本数)・出力引数を全部不変。**診断**   */
/*  (ledger_bad_free ほか、R1 の計数) は失敗した操作でも増える (§3-2)。      */
/*  owner は必ず引数で渡す (暗黙の「今の owner」は使わない)。物理は PFN。    */
/* ======================================================================== */
/* owner の種別 (§3-1)。0 = 表の空き。 */
#define LEDGER_KIND_AS       1   /* アプリの AS。終了で全部回収 (R5 (a)) */
#define LEDGER_KIND_PERSIST  2   /* kernel・boot・bundle・shlib・gshell・staging */
#define LEDGER_KIND_MODULE   3   /* R7 の一括回収の単位 */
#define LEDGER_KIND_DEVICE   4   /* MMIO の永久予約だけ。RAM の確保・回収は拒否 */
/* 固定の owner 番号 (§3-1)。AS / MODULE / 他の DEVICE は ledger_owner_new。 */
#define LEDGER_OWNER_KERNEL  1
#define LEDGER_OWNER_BOOT    2
#define LEDGER_OWNER_BUNDLE  3
#define LEDGER_OWNER_SHLIB   4
#define LEDGER_OWNER_GSHELL  5
#define LEDGER_OWNER_STAGING 6
#define LEDGER_OWNER_GFX     7   /* DEVICE: gfx の候補群 (§3-8、T1e) */
#define LEDGER_OWNER_FIXED_LAST LEDGER_OWNER_GFX
/* 表の大きさ (§3-1)。owner 番号は u8 で、64 本 = 番号 1〜63 (0 は空き)。 */
#define LEDGER_MAX_OWNERS    64
#define LEDGER_MAX_REGIONS   48
#define LEDGER_MAX_RESOURCES 16
#define LEDGER_MAX_SURFACES  16
#define LEDGER_NAME_LEN      8
/* 区間の種別 (§3-1 の type)。 */
#define LEDGER_R_FIXED       1
#define LEDGER_R_BACKGROUND  2   /* RAM でない背景 (15〜16MB、2GB〜4GB) */
#define LEDGER_R_STAGING     3
#define LEDGER_R_BUNDLE      4
#define LEDGER_R_DMA         5
#define LEDGER_R_DEVICE      6   /* 装置の予約 (T1d の ledger_reserve_set) */
#define LEDGER_R_SURFACE_BACKING 7
#define LEDGER_CACHE_WB      0
#define LEDGER_CACHE_UC      1
#define LEDGER_RF_PERMANENT  1   /* 解除 API なし */
#define LEDGER_RF_OUTSIDE    2   /* [0, limit_pfn) の外にかかる (登録時に自動) */
/* 資源レコード (§3-1、X4) の bus と width_basis (decode 幅の根拠)。
 * 予約権限になるのは SIZING / DATASHEET / GLUE_CONST だけ。RAW (列挙の採取値。
 * 幅未確定) と PROBE_UNVERIFIED は表に載るが予約には使えない。 */
#define LEDGER_BUS_PCI       1
#define LEDGER_BUS_CBUS      2   /* C バスの窓 */
#define LEDGER_BUS_FIXED     3   /* 機種固定 (PEGC) */
#define LEDGER_WB_SIZING     1   /* BAR の sizing を実施 (T4 以降) */
#define LEDGER_WB_DATASHEET  2
#define LEDGER_WB_GLUE_CONST 3   /* glue の定数 (Xe10 は NP21/W の値、実機の実測ではない) */
#define LEDGER_WB_RAW        4
#define LEDGER_WB_PROBE_UNVERIFIED 5
/* ledger_reserve_set の span の種別と 1 回の要求の上限 */
#define LEDGER_SPAN_MMIO     1
#define LEDGER_SPAN_RAM      2   /* 新たに取る永久 RAM (全ページ eligible・空き) */
#define LEDGER_MAX_SPANS     16
/* pgalloc_alloc_n_owner の探索の向き */
#define LEDGER_BOTTOM_UP     0
#define LEDGER_TOP_DOWN      1
/* R1 の控えの op (§3-5) */
#define LEDGER_OP_ALLOC      1
#define LEDGER_OP_FREE       2
#define LEDGER_OP_TRANSFER   3
#define LEDGER_OP_RECLAIM    4
#define LEDGER_OP_CLAIM      5
#define LEDGER_OP_RESERVE    6

/* L1 (2 bitmap) + L2 (1B/PFN) のバイト数。pgalloc_metadata_bytes (ページへ
 * 切り上げ)・memory_boot の置き場の計算・STATIC_ASSERT が同じ式を使う。 */
#define PGALLOC_META_BYTES(limit) \
    ((((limit) + 31UL) / 32UL) * 8UL + (u32)(limit))

struct ledger_owner {          /* 24B */
    u8  kind, id;
    char name[LEDGER_NAME_LEN];
    u16 pad;
    u32 pages;                 /* 会計: L2 でこの番号のページ数 (永久予約を含む) */
    u32 alloc_irq, free_irq;   /* 診断: 割り込み / 例外フレームの上の操作 (R1) */
};
struct ledger_region {         /* 16B */
    u32 first, end;            /* PFN 半開 (end = 1048576 で 4GiB 端) */
    u8  type, owner, cache, flags;
    u16 res_mask;              /* 根拠の資源レコード (DEVICE だけ、T1d) */
    u8  span_set, pad;
};
struct ledger_resource {       /* 32B。u32 → u16 → u8 の順で詰め物なし (§3-1) */
    u32 raw_bar, decode_first, decode_end, map_first, map_end;
    u16 bdf, vendor, device;
    u8  bus, revision, bar, width_basis, boot_gen, pad;
};
/* 予約の要求 1 本。first / end は PFN 半開 (end = 1048576 で 4GiB 端)、
 * kind は LEDGER_SPAN_*、res はこの span の根拠の資源レコードの番号。 */
struct ledger_span { u32 first, end, kind, res; };
struct ledger_surface {        /* T2b: whole-page occupancy and stable references */
    u32 first, npages;         /* npages == 0 = 表の空き */
    u32 gen, plane_offset[4];
    u16 lease_count;
    u16 width, height, pitch;
    u8  owner, backing, backend, role, format, planes, cache, perm_max, closing;
};
extern struct ledger_owner ledger_owners[LEDGER_MAX_OWNERS];
extern struct ledger_region ledger_regions[LEDGER_MAX_REGIONS];
extern struct ledger_resource ledger_resources[LEDGER_MAX_RESOURCES];
extern struct ledger_surface ledger_surfaces[LEDGER_MAX_SURFACES];
extern u32 ledger_region_count;

/* 観測用の文脈の深さ (§3-5、B1)。全 IRQ スタブ / 全例外スタブの入口で +1、
 * 復帰点で -1 (isr_stub.asm の IRQ_ENTER 等、ss: で書く)。longjmp で抜ける
 * 経路は exec_setjmp の控え (jmpbuf の語 6・7) へ exec_longjmp が戻す。
 * broker の IRQ_ERR_CTX 判定も同じ kctx_irq_depth を使う (T2a R1)。 */
extern volatile u32 kctx_irq_depth, kctx_exc_depth;
/* R1 の計数 (診断)。last = {op, owner, 呼び出し元 EIP} の最後の 1 件。 */
extern u32 ledger_irq_ops, ledger_exc_ops;
extern u32 ledger_irq_last[3], ledger_exc_last[3];
/* 他の診断: 他 owner 混在の解放 / 参照の残る返却 / 他 owner 混在の claim /
 * 回収で掃除したページの累計 / 自己検査の失敗件数と最後の地点。 */
extern u32 ledger_bad_free, ledger_retire_refused, ledger_claim_refused;
extern u32 ledger_reclaim_pages, ledger_check_fail, ledger_res_overflow;
extern const char *ledger_check_tag;

/* owner 番号の取得 (AS / MODULE / DEVICE。固定番号の後ろから空きを配る) と
 * 返却。返却は pages == 0 かつ区間の表・SURFACE の表のどこからも参照されて
 * いないときだけ (残れば拒否して ledger_retire_refused)。PERSIST / DEVICE は
 * 返さない。返した番号は再利用する。1 = 成功。 */
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner);
int ledger_owner_retire(u32 owner);
u32 ledger_owner_pages(u32 owner);

/* n ページ連続を [first, end) (PFN) から owner で確保 (flags は向き)。
 * ONLINE のときだけ。DEVICE owner は拒否。成功は戻り値、PFN は *pfn。 */
int pgalloc_alloc_n_owner(u32 owner, int n, u32 first, u32 end, u32 flags,
                          u32 *pfn);
/* 汎用の池 [MEM_POOL_BASE, 上端) の最初適合 (下から)。物理番地を返す (池は
 * MEM_POOL_BASE 以上なので 0 = 失敗)。 */
u32 pgalloc_alloc_phys(u32 owner, int n);
/* **全ページが owner のものであるときだけ**解放。他 owner・未確保・永久予約が
 * 混じれば全件不変で拒否し ledger_bad_free を数える。 */
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n);
/* 全ページが from のものなら一括で to へ (会計 pages も動く)。 */
int ledger_transfer(u32 pfn, int n, u32 from, u32 to);
/* owner のページを全部返す (R5 の AS 終了、R7 のモジュール失敗)。DEVICE /
 * PERSIST は拒否、SURFACE を持つ owner も
 * 拒否。*pages に返した数 (ledger_reclaim_pages にも足す)。 */
int ledger_reclaim_owner(u32 owner, u32 *pages);
/* 固定番地 [first, end) (PFN) を owner で押さえる (旧 pgalloc_mark_used)。
 * eligible な空きだけを取り、同じ owner のページは冪等、eligible でない
 * ページは飛ばす。**他 owner のページが 1 つでも混じれば全件不変で拒否**
 * (ledger_claim_refused)。DEVICE owner は拒否。 */
int ledger_claim_fixed(u32 owner, u32 first, u32 end);
/* 区間の表への登録。起動時 (paging_boot_context = master CR3・live AS 0) だけ。
 * 既存の区間と重なれば拒否 (DEVICE の区間は ledger_reserve_set だけが作る)。
 * flags は PERMANENT だけを受け、OUTSIDE は範囲から自動で付く。 */
int ledger_register_region(u32 type, u32 owner, u32 first, u32 end, u32 cache,
                           u32 flags);
/* ======== MMIO 登録と検証済み資源レコード (§3-1・§3-2、T1d、D33・X4) ======== */
/* 資源レコードを表の空きに写す。bus・width_basis が既知の値で、decode は
 * [0, 4GiB] の中 (RAW は幅未確定なので decode_first == decode_end でよい)、
 * 写像範囲は空か decode の内側。表が満杯なら ledger_res_overflow を数えて
 * 断る。*rid に番号。1 = 成功。 */
int ledger_resource_add(const struct ledger_resource *rec, u32 *rid);
/* ⑥-0 (pci_bind_all の直前): g_pci のメモリ BAR を width_basis = RAW で
 * 取り込む (採取値。予約権限にならない、B4)。載せた本数を返す。
 * 実体は kernel/ledger_pci.c (drivers/pci.h を引くのはそちらだけ)。 */
u32 ledger_resource_import_pci(void);
/* MMIO / 永久 RAM の一括予約 (旧 DEVICE_RESERVATION の broker の核を載せ直した
 * もの、D33)。起動時 (ONLINE・paging_boot_context) だけ、owner は
 * DEVICE 種別。手順: (e) span ごとに資源レコード res の decode の内側で、
 * width_basis が SIZING / DATASHEET / GLUE_CONST であることを**正規化の前**に
 * 照合 (B10) → 正規化 (同種の重なる・接する span を併合し、根拠を res_mask の
 * 和に。異種の重なりは拒否) → 区間の表と照合: FIXED / STAGING / BUNDLE / DMA /
 * SURFACE_BACKING と交差したら拒否、他 owner の DEVICE と交差したら拒否、
 * BACKGROUND は許可 → owner が既に DEVICE 区間を持っていれば、正規化後の区間
 * 集合と res_mask (と種別) が完全に一致するときだけ成功 (冪等、何も変えない)。
 * 部分一致は拒否 → PFN の管理範囲の中は allocated でないこと・L2 に owner が
 * 無いこと (永久予約)・RAM 種別は全ページ eligible で範囲の内側 → 区間の表の
 * 空き → 一括 commit (eligible を落とし、DEVICE 区間を PERMANENT で記録。
 * MMIO は UC、RAM は WB。管理範囲外にかかれば OUTSIDE)。解除 API は無い。
 * 失敗時は L1・L2・L3・会計を全部不変。1 = 成功。 */
int ledger_reserve_set(u32 owner, const struct ledger_span *spans, u32 n);
/* ======== SURFACE と CPL=0 子のアリーナ (§3-1・§3-6・§3-8、T1e) ======== */
/* SURFACE の backing / backend / role / perm_max。 */
#define LEDGER_SB_RAM        1   /* 池から確保した PFN 区間 (PEGC の BB) */
#define LEDGER_SB_FIXED_RAM  2   /* 固定の RAM (planar BB 0x6A000) */
#define LEDGER_SB_VRAM       3
#define LEDGER_SB_MMIO       4   /* 装置の窓の中 (Cirrus のクライアント面・表示面) */
#define LEDGER_SF_PC98       1
#define LEDGER_SF_PEGC       2
#define LEDGER_SF_CIRRUS     3
#define LEDGER_ROLE_CLIENT   1   /* アプリへ USER で貸す面 */
#define LEDGER_ROLE_DISPLAY  2   /* 表示面 (supervisor のまま) */
#define LEDGER_PERM_NONE     0
#define LEDGER_PERM_RO       2
#define LEDGER_PERM_RW       1
/* Internal table ownership check (PD/PT preparation and rollback). */
int pgalloc_page_owned(u32 pfn, u32 owner);
/* Validate geometry, whole-page ownership/resource coverage and cache. */
int ledger_surface_validate(const struct ledger_surface *sf);
/* Stop lending; RAM is returned after the last lease, DEVICE stays reserved. */
int ledger_surface_release(u32 sid);
/* Normal-context registration. Reject overlap; zero RAM padding; increment
 * u32 generation. Exhausted generations permanently retire that slot.
 * Success=1, *sid is changed only on success (NULL allowed). */
int ledger_surface_create(const struct ledger_surface *sf, u32 *sid);
/* backend と role が一致する最初の SURFACE (無ければ NULL)。 */
struct ledger_surface *ledger_surface_find(u32 backend, u32 role);
/* SURFACE の owner を to へ (§3-1 の移譲の表)。RAM backing は L2 のページも
 * ledger_transfer で一緒に、FIXED_RAM は同じ owner の SURFACE_BACKING 区間の
 * owner も、MMIO は SURFACE の owner だけ。既に to なら何もしない。1 = 成功。 */
int ledger_surface_transfer(u32 sid, u32 to);
/* 旧起動予算の物理アリーナ [MEM_PHYS_EXEC_FLOOR, pgalloc_arena_end()) の上端
 * (PFN、T1 の間だけ)。アリーナ内の PERSIST owner のページの最下端を ⑥ で
 * ledger_arena_freeze が 1 回だけ凍結する。凍結前・永続確保が無ければ
 * pgalloc_arena_end()。sys_usable_mem_end = min(凍結した exec 上端, これ)。 */
u32 ledger_arena_top(void);
void ledger_arena_freeze(void);

/* 不変条件 (eligible で allocated ⇔ owner ≠ 0、owner の pages と L2 の一致、
 * L2 の番号が生きていること、区間の非重複 (DEVICE と BACKGROUND の重なりだけ
 * 許す) と owner、SURFACE の owner) を任意の
 * 時点で検査する。失敗は ledger_check_fail と ledger_check_tag に残す。 */
int ledger_selfcheck(const char *tag);

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
