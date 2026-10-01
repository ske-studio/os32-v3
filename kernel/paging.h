/* ======================================================================== */
/*  PAGING.H — x86ページング (メモリ保護)                                   */
/*                                                                          */
/*  アイデンティティマッピング (仮想=物理) でページ保護を実現する。          */
/*  IVT/BIOSデータのRead-Only化、スタックガードページ、NULL保護等。          */
/* ======================================================================== */

#ifndef __PAGING_H
#define __PAGING_H

#include "types.h"
#include "memmap.h"   /* MEM_APP_BAND_* (struct addrspace の PT 配列長) */

/* ページサイズ */
#define PAGE_SIZE       4096
#define PAGE_SHIFT      12

/* ページディレクトリ/テーブルのエントリ数 */
#define PDE_COUNT       1024
#define PTE_COUNT       1024

/* ページ属性ビット */
#define PTE_PRESENT     0x001   /* P   : 存在 */
#define PTE_RW          0x002   /* R/W : 書き込み可 */
#define PTE_USER        0x004   /* U/S : ユーザアクセス可 */
#define PTE_PWT         0x008   /* PWT : ライトスルー */
#define PTE_PCD         0x010   /* PCD : キャッシュ無効 */
#define PTE_ACCESSED    0x020   /* A   : アクセス済み */
#define PTE_DIRTY       0x040   /* D   : 書き込み済み */
#define PTE_PS          0x080   /* PS  : ページサイズ (PDE用, 4MB) */

/* よく使う組み合わせ */
#define PAGE_RW         (PTE_PRESENT | PTE_RW)       /* 読み書き可 */
#define PAGE_RO         (PTE_PRESENT)                 /* 読み取り専用 */
#define PAGE_NOT_PRESENT 0                            /* アクセス不可 */

/* Phase 1: full 32-bit addressability, expressed as PFNs, never a wrapped
 * 4GiB exclusive byte address. Eight bootstrap PTs remain static; the other
 * PDEs start absent and acquire one zeroed PT only when explicitly mapped.
 * paging_init's identity covers min(detected RAM, PAGING_BOOT_MAP_SIZE): the
 * static bootstrap window, NOT a RAM ceiling (K6-RAM, 2026-09-11). RAM above
 * that window is mapped later by pgalloc_stage_online through paging_map_phys
 * with PTs taken from the boot workspace. Dynamic PT backing scans
 * to pgalloc_limit_pfn(), not the eligible count: only known master shared
 * tables with identity supervisor RW, cacheable PTEs qualify. New PTs still
 * require master CR3 and no live address spaces; no high RAM is auto-mapped.
 * No optional device guard policy is introduced here. */
#define PAGING_PFN_COUNT 1048576UL
#define PAGING_PT_COUNT PDE_COUNT
#define PAGING_BOOT_PT_COUNT MEM_FIXED_BOOT_PT_COUNT
#define PAGING_BOOT_MAP_SIZE (PAGING_BOOT_PT_COUNT * PTE_COUNT * PAGE_SIZE)
/* Legacy backend aperture checks still use the bootstrap window size.
 * Not the mapping API ceiling: use PAGING_PFN_COUNT for address spans. */
#define PAGING_MAP_SIZE PAGING_BOOT_MAP_SIZE

/* ページ境界アライメント (切り上げ/切り下げ)。
 * 手書きの (x + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1) イディオムはこれを使う。 */
#define PAGE_ALIGN_DOWN(x)  ((u32)(x) & ~(u32)(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(x)    (((u32)(x) + PAGE_SIZE - 1) & ~(u32)(PAGE_SIZE - 1))

/* ======== API ======== */

/* ページング初期化・有効化。一度だけ実行し、以後の呼出しは完全な no-op。
 * mem_kb を変更しても既存 PT / AS / CR3 / allocator をリセットしない。 */
/* Internal single-owner boot-stage gate; requires enabled master and no AS. */
int paging_boot_context(void);
/* Trusted layout verifier: complete identity supervisor RW cacheable span.
 * PTE A/D and PDE USER (when PTE is supervisor) do not weaken this contract. */
int paging_verify_identity(u32 first_pfn, u32 pages, void *identity);
void paging_init(u32 mem_kb);
/* End PFN (exclusive) of the identity paging_init actually established, i.e.
 * min(detected RAM, PAGING_BOOT_MAP_SIZE) in pages; 0 before paging_init.
 * It is the boundary between "already mapped, must only be verified" and
 * "must be mapped now", never a limit on how much RAM may be admitted. */
u32 paging_boot_identity_end(void);
/* 台帳の backing [MEM_LEDGER_META_BASE, MEM_LEDGER_META_END) を present /
 * supervisor / RW にする。FIXED 型 (TASK_T1_LEDGER §3-3) のときだけ
 * memory_boot_init が 1 回呼ぶ。ブート文脈の外では何もしない。1 = 成功。
 * 以後 paging_memmap_selftest はこの範囲を MM_RW と期待する。 */
int paging_map_ledger_backing(void);

/* 指定ページの属性を変更。
 * flags に PTE_USER を含めると PDE 側にも USER を伝播させる
 * (i386 の実効権限は PDE と PTE の論理積のため)。
 * 戻り値: 0=成功, -1=必要 PT の確保不可 (何も変更されない)。
 * ガードページ設置のような保護目的の呼び出しは必ず戻り値を確認すること
 * (無言 no-op だと保護が入らないまま fail-open になる)。 */
int paging_set_page(u32 virt_addr, u32 phys_addr, u32 flags);

/* 範囲マップ。[virt_start, virt_end) を phys_start からの連続物理へ
 * flags でマップする (end は exclusive)。TLB フラッシュは最後に 1 回。
 * 複数ページの属性変更/張り替えは paging_set_page のループでなく必ず
 * これを使うこと (ページごとの CR3 全リロードを避ける)。
 * 戻り値: 0=成功, -1=逆順/桁あふれ/必要 PT の確保不可 (全範囲を未変更) */
int paging_map_range(u32 virt_start, u32 virt_end, u32 phys_start, u32 flags);

/* デバイス窓マップ (ページ数で指定する paging_map_range)。
 * master PD の PTE を張るので、以後に作られるアプリ AS
 * (paging_addrspace_create は master の PDE を全部コピーする) からも
 * 同じ物理が見える。PEGC のリニア窓 F00000h のように「機種固有のドライバが
 * 欲しい物理窓」を、ドライバ側にページテーブルを触らせずに張るための入口。
 * flags に PTE_USER を含めれば PDE にも USER が伝播する
 * (同じ PDE 配下の他ページは PTE が supervisor のままなので保護は保たれる)。
 * npages=0 は no-op。最終ページ 0xFFFFF000 も 1 ページとして指定可能。
 * 新 PDE の追加は master CR3、pgalloc 初期化後、live AS が 0 の時だけ。
 * 既存 PT の更新は従来どおり。追加 PT は master の寿命まで保持する。
 * 失敗時には追加 PT を全解放し、既存 PTE/PDE も変更しない。
 *
 * **表示面を含むデバイス窓に PTE_USER を渡してはならない** (レビュー #5 ②)。
 * master に USER で張ると、PDE をまるごと写す paging_addrspace_create() の
 * 先で CPL=3 アプリが表示 VRAM に直接書けてしまい、契約 G4 (commit 前の描画は
 * 表示面に出ない) が崩れる。窓は supervisor + PTE_PCD で張り、アプリに見せる
 * クライアント面だけを exec が paging_addrspace_map_user_keep() で昇格させる。
 * 戻り値: 0=成功, -1=逆順/桁あふれ/必要 PT の確保不可 (全範囲を未変更)。 */
int paging_map_phys(u32 virt_addr, u32 phys_addr, u32 npages, u32 flags);

/* 指定範囲を覆う PDE から USER を落とす (V86 セッション終了時の後始末)
 * end は inclusive。戻り値: 0=成功, -1=逆順 (未変更) */
int paging_pde_clear_user(u32 start, u32 end);

/* 指定範囲の全ページを Read-Only に。
 * end は inclusive。RW だけを落とし物理フレーム/USER/PCD/P/他属性を保持。
 * 不在 PT/PTE は不在のまま (RAM を生成しない)。
 * 戻り値: 0=成功, -1=逆順 (全範囲を未変更) */
int paging_set_readonly(u32 start, u32 end);

/* 指定範囲の全ページを Not-Present に。
 * end は inclusive。PTE のフレーム/属性ビットは保持し P ビットのみ落とす。
 * 戻り値: 0=成功, -1=逆順 (全範囲を未変更) */
int paging_set_not_present(u32 start, u32 end);

/* ブート後のコンベンショナルメモリ再利用 (ページ0: NP, 0x1000-0x9FFFF: R/W) */
void paging_reclaim_conventional(void);

/* ページング有効かどうか */
int paging_enabled(void);

/* 指定アドレスのページがPresentかどうか (メモリダンプ安全チェック用) */
int paging_is_present(u32 virt_addr);

/* その仮想番地の PTE のフラグ (下位 12bit)。ページが無ければ 0。
 * **読むだけ** の問い合わせで、表は 1 ビットも動かさない。USER ビットが
 * 立っているかを自己診断が見る (票 T9 §12 R1: KAPI が CPL=3 へ返す
 * 番地は USER で張られたページでなければならない)。 */
u32 paging_pte_flags(u32 virt_addr);

/* ======================================================================== */
/*  リング3 アドレス空間 (M1b: PD 複製)                                     */
/*                                                                          */
/*  CPL=3 プログラムごとに独立したページディレクトリ (PD) を持たせるための   */
/*  基盤。カーネル帯域・SHM・VRAM・ホットデプロイ窓は全 PD で共有し          */
/*  (同一物理を指す)、0x400000 帯 (プログラム code/data/heap/stack) だけ     */
/*  アプリ固有のページテーブルに差し替える (CONTRACTS C2)。                  */
/*                                                                          */
/*  共有/非共有の境界は PDE 単位:                                            */
/*    PDE 0 (0x000000-0x3FFFFF) : 共有 — カーネル/SQLite/シェル/SHM/VRAM     */
/*    PDE 1 (0x400000-0x7FFFFF) : アプリ固有 — プログラム帯 (APP_BAND_PDE)   */
/*    PDE 2 (0x800000-0xBFFFFF) : 共有                                       */
/*    PDE 3 (0xC00000-0xFFFFFF) : 共有 — 物理末尾のホットデプロイ窓を含む     */
/*    PDE 4-7 (0x1000000-0x1FFFFFF) : 共有 — 16MB 超の RAM の恒等写像。     */
/*      RAM が無ければ全 Not-Present。                                       */
/*    PDE 0x3F8 (0xFE000000-0xFE3FFFFF) : 共有 — OS が割り当てるデバイス窓の */
/*      帯の先頭 (memmap.h MEM_DEVICE_APERTURE_*、Cirrus のリニア窓)。PT は  */
/*      paging_init が静的に 1 枚用意し、既定は全 Not-Present。              */
/*      paging_addrspace_create は PDE を 1024 本すべてコピーするので、       */
/*      master に張った窓はそのまま全アプリ PD から見える (H3b)。            */
/*                                                                          */
/*  M1b の時点ではアプリ PT を master と同一の identity で初期化する          */
/*  (0x400000 帯も present/RW/identity)。これにより CPL=0 のまま CR3 を       */
/*  新 PD に載せてもカーネルは動き続ける (V1)。CPL=3 用の USER マッピングは    */
/*  M1c で overlay する。 */

/* アプリ帯域の **先頭** PDE インデックス (= MEM_APP_BAND_BASE >> 22 = 512)。
 * 共有ライブラリ帯域 (0x400000-0x4FFFFF)・プログラム本体
 * (MEM_EXEC_LOAD_ADDR 0x80100000-)・ヒープ・ユーザスタックがここから載る。
 *
 * 2026-09-10 (票 docs/tasks/memory/APP_BAND_PDE.md): アプリ固有 PDE は
 * 1 枚固定ではなく [APP_BAND_PDE, APP_BAND_PDE + count) の連続 count 枚。
 * count は 1〜MEM_APP_BAND_MAX_PDES で、要求量に応じて exec が決める。
 * 整合は kernel/paging.c の STATIC_ASSERT が検査する。 */
#define APP_BAND_PDE   (MEM_APP_BAND_BASE >> 22)

struct as_lease {
    u32 token, sid, generation, base, npages, flags;
};
struct lease_mapping { u32 base, phys, npages, flags, slot, token, sid, generation; };
struct addrspace {
    u32 pd_phys;       /* 新 PD の物理アドレス (CR3 に載せる値)。0=無効 */
    u32 app_pde;       /* アプリ固有にした先頭 PDE インデックス */
    u32 app_pde_count; /* アプリ固有 PDE の枚数 (0=無効, 1..MAX_PDES) */
    u32 app_pt_phys[MEM_APP_BAND_MAX_PDES];  /* 各 PDE のアプリ PT 物理 */
    /* この AS の台帳の owner (TASK_T1_LEDGER §4-8、T1b)。PD / PT と、
     * free_user_range が返すページはこの owner のもの。取得と返却は呼び手
     * (exec・自己診断) が行い、destroy は消さない (呼び手が回収・返却する)。 */
    u32 owner;
    u32 lease_pt_phys[MEM_LEASE_MAX_PDES];
    struct as_lease leases[MEM_LEASE_MAX];
};
/* T2c launch path: sparse app PTs and the first lease PT. */
int paging_addrspace_create_lease(struct addrspace *as, u32 owner);
int paging_lease_map(struct addrspace *as, const struct lease_mapping *maps, u32 n);
int paging_lease_unmap(struct addrspace *as, u32 base, u32 npages);
u32 paging_lease_pte(const struct addrspace *as, u32 va);


/* カーネル (master) PD の物理アドレス。CR3 を戻すときに使う。 */
u32 paging_kernel_pd_phys(void);

/* 現在の CR3 (= 現在アクティブな PD 物理アドレス) を読む。 */
u32 paging_current_cr3(void);

/* CR3 に PD をロードする (= アドレス空間切り替え + TLB フラッシュ)。 */
void paging_load_cr3(u32 pd_phys);

/* master CR3 の下でアプリ PD を歩き、USER + RW の物理番地を返す。
 * 失敗時 *pa は不変。戻り値は 0 / 下の拒否理由 (診断を呼び手へ渡す)。 */
#define AS_VA_TABLE 1
#define AS_VA_PDE   2
#define AS_VA_PTE   3
int as_va_to_pa(u32 pd_phys, u32 va, u32 *pa);
/* Read translation accepts RO user pages; failure leaves pa untouched. */
int as_va_to_pa_read(u32 pd_phys, u32 va, u32 *pa);

/* T2c: 高位アプリ帯が空の疎 AS を作る (PD のみ確保)。低位カーネル帯と
 * デバイス窓は master と共有し、APP/lease の PDE は継承しない。
 * 高位 PT は paging_addrspace_map_user*() が必要時に owner で確保する。
 * PD/PT のバッキングは pgalloc から AS owner で取得する。
 * pde_count は互換引数 (1..MEM_APP_BAND_MAX_PDES を検査) であり、生成後は
 * 全 MEM_APP_BAND_MAX_PDES が利用可能。起動用の lease 先頭 PT 事前確保は
 * paging_addrspace_create_lease() を使う。
 * 戻り値: 0=成功、-1=引数不正 / PD の物理ページ不足。
 * master の paging_map_phys() / paging_set_page() では私有 PT を変更できない。 */
int paging_addrspace_create_n(struct addrspace *as, u32 owner, u32 pde_count);

/* 疎 AS の生成 (= paging_addrspace_create_n(as, owner, 1))。
 * lease を使わない自己診断はこちらを使う。 */
int paging_addrspace_create(struct addrspace *as, u32 owner);

/* アプリ用アドレス空間を破棄し PD/PT (枚数分) のバッキングページを解放する。
 * 破棄する PD がアクティブ (CR3) であってはならない — 先に
 * paging_load_cr3(paging_kernel_pd_phys()) で master へ戻すこと。 */
void paging_addrspace_destroy(struct addrspace *as);

/* アプリ帯に必要な PDE 枚数を求める (票 §4-1 の規則、純関数)。
 *   code_end : 本体 (code+data+bss) 末尾のページ境界切り上げ済み仮想番地
 *   heap_req : exec_heap の要求量 (OS32X ヘッダの heap_size)。
 *              **0 = 指定なしは必ず 1 枚**を返す — 指定しないプログラムの
 *              レイアウトを従来から 1 バイトも動かさないため (回帰ゼロ)。
 *   ram_top  : 帯を伸ばしてよい物理上限。exec は子プロセスの claim 範囲 A の
 *              末尾を渡す (そこまでは子が予約済み = pgalloc と二重使用しない)。
 * 戻り値: 1..MEM_APP_BAND_MAX_PDES。桁あふれ・上限不足でも 1 は必ず返す。 */
u32 paging_app_band_pdes(u32 code_end, u32 heap_req, u32 ram_top);

/* アプリ AS の 1 ページを USER でマップする (M1c)。
 *   - virt が高位アプリ帯 (app_pde..app_pde_count) なら、疎確保した私有 PT に書く
 *     (このアプリの PD からしか見えない)。
 *   - それ以外の共有帯 (VRAM 0xA8000 / SHM 等、C2 で全 PD 共有 + USER と
 *     定めた領域) なら共有 PT の PTE に USER を立てる。共有 PT は master と
 *     同一だが、master 側の PDE には USER を伝播させないので (このアプリ PD
 *     の PDE コピーにだけ立てる)、カーネル/シェルから見た実効権限は
 *     supervisor のまま保たれる (PDE と PTE の論理積)。
 * flags に PTE_USER を含めること。戻り値 0=成功, -1=範囲外。 */
int paging_addrspace_map_user(struct addrspace *as, u32 virt, u32 phys,
                              u32 flags);

/* [vstart, vend) を identity (phys=virt) で USER マップする (M1c)。
 * end は exclusive。プログラム帯・ユーザスタック・VRAM・SHM に使う。
 * 戻り値 0=成功, -1=AS 無効・逆順・必要 PT/PDE 不在 (全範囲を未変更)。 */
int paging_addrspace_map_user_range(struct addrspace *as, u32 vstart,
                                    u32 vend, u32 flags);

/* [vstart, vend) を **pstart からの連続物理**へ USER マップする (K5b P1)。
 * end は exclusive。identity 版 (paging_addrspace_map_user_range) と違い、
 * アプリごとに別々の物理ページを同じ仮想番地へ載せるための口。
 * アプリ 4 本同時 (票 TASK_K5_multiapp.md D1/I5) の土台で、共有ライブラリの
 * .data が既にこの形 (1 ページ版 paging_addrspace_map_user) で動いている。
 * pstart はページ境界。範囲がアプリ固有 PDE の外にも掛かってよいが、その
 * ぶんは共有 PT を書き替える (= 全 PD に効く) ので呼び出し側の責任。
 * 戻り値 0=成功, -1=AS 無効・逆順・非整列・必要 PT/PDE 不在 (全範囲を未変更)。 */
int paging_addrspace_map_user_range_phys(struct addrspace *as, u32 vstart,
                                         u32 vend, u32 pstart, u32 flags);

/* アプリ固有 PDE 配下の PTE を全部 0 (非 present) に落とす (K5b P2)。
 * paging_addrspace_create_n() はアプリ PT を **master の identity PTE で**
 * 初期化する (V1 のため)。per-app 物理へ移す設計ではこれを落とし忘れると
 * 物理 0x5xxxxx が素通しで見え、他アプリのページや pgalloc の作業域が
 * CPL=3 から読めてしまう (票 TASK_K5_multiapp.md I6 — 最も静かに壊れる箇所)。
 * create_n の直後・per-app 物理を張る前に必ず呼ぶこと。
 * PDE の present/RW はそのまま (PT は残す)。USER は落とす。
 * 戻り値 0=成功, -1=AS 無効。 */
int paging_addrspace_clear_app_band(struct addrspace *as);

/* [vstart, vend) に張ってある **アプリ固有 PT の物理ページを as->owner で
 * pgalloc へ返し** (他 owner のページは返さず ledger_bad_free に数える)、
 * PTE を 0 にする (K5b P6)。範囲はアプリ固有 PDE の中だけを見る — 共有 PT に
 * 掛かる部分は 1 ビットも触らない (VRAM/SHM/フォントを解放しないため)。
 * per-app 物理は連続とは限らない (断片化時はページ単位で張る) ので、
 * 解放も PTE を 1 枚ずつ辿って行う。
 * 戻り値: 返したページ数。AS 無効・逆順なら 0。 */
u32 paging_addrspace_free_user_range(struct addrspace *as, u32 vstart,
                                     u32 vend);

/* map_user_range と同じだが、**既存 PTE のキャッシュ属性 (PCD/PWT) を引き継ぐ**。
 * デバイス窓の一部を CPL=3 へ貸すとき用 (GFX バックバッファ)。Cirrus では
 * クライアント面がカード VRAM (master で PCD 付き) なので、flags をそのまま
 * 書き込むと PCD が消え、CPU が書いた画素がキャッシュに残ったまま BLT エンジン
 * が古い VRAM を読む。共有 PT の PTE は master からも見えるため、属性を落とすと
 * カーネル側の描画まで巻き添えになる (レビュー #5 ③)。
 * 戻り値 0=成功, -1=AS 無効・逆順・必要 PT/PDE 不在 (全範囲を未変更)。 */
int paging_addrspace_map_user_keep(struct addrspace *as, u32 vstart,
                                   u32 vend, u32 flags);

/* ⚠ **走っているアプリの PD を、そのアプリの syscall 中に歩いてはならない**
 * (票 S0-K / 実機 K2、2026-09-13)。カーネルはページテーブルを「物理 = 仮想」で
 * 読むが、PD もアプリ PT も pgalloc から取られ (`MEM_POOL_BASE` は 0x400000 =
 * **アプリ帯そのもの**)、アプリの PD ではその仮想番地が per-app 物理へ
 * 張り替わっている。したがって CR3 = アプリ PD のまま表を辿ると、PT の
 * つもりで **アプリ自身のデータ** を読む (#PF も起きないまま健全なページを
 * 非 present と答える)。`struct addrspace` の控えから引いても PDE から引いても
 * 同じ物理を指すので結果は同じ。表を歩けるのは master CR3 の下だけ。
 * KAPI のポインタ検証は PTE を見ず、帯 (`ring3_ptr_ok`) と長さだけで判断する。 */

/* PD 複製の自己診断 (V1)。CPL=0 のまま:
 *   1. アプリ AS を作る
 *   2. CR3 を新 PD に載せてもカーネル (コード/スタック/データ) が生存する
 *   3. カーネル帯域の 1 語が master PD と新 PD で同一物理を指す (共有の証明)
 *   4. CR3 を master に戻し、AS を破棄する
 * 戻り値: 0=全通過。非0 はビットフラグで失敗内容を示す。
 * ブート時に kselftest_run() から呼ぶ想定 (memory_boot_init 後)。 */
int paging_pd_clone_selftest(void);

/* デバイス窓の貸し出しの自己診断 (レビュー #5 ②③)。守備範囲末尾の 2 ページを
 * 「supervisor + PCD のデバイス窓」に見立て、片方だけを
 * paging_addrspace_map_user_keep() で昇格させて次を確かめる:
 *   USER が立つ / PCD が消えない / 隣のページ (表示面役) が supervisor のまま /
 *   master の PDE に USER が伝播せずアプリ PD の PDE にだけ立つ。
 * ハードウェアには依存しない (実 RAM の無い番地を使い、PTE は必ず戻す)。
 * 戻り値: 0=全通過。非0 はビットフラグで失敗内容を示す。 */
int paging_map_user_keep_selftest(void);

/* T2c の高位アプリ帯の自己診断 (票 docs/tasks/v3/TASK_T2_APPBAND.md §5-1)。
 * 未使用 APP PDE/PT と master の高位 APP PDE が空であること、高位 stack
 * ページの写像時に疎 PT を確保でき、回収で owner と池が元へ戻ることを見る。
 * CR3 は載せ替えない。戻り値: 0=全通過、非0 はビットフラグ。 */
int paging_app_band_selftest(void);

/* ------------------------------------------------------------------------ */
/*  地図 (memmap.h) と実物 (PDE 0 の PTE 1024 本) の照合                      */
/*  (票 docs/archive/kernel_v21/TASK_KSTACK_USER.md §4 の 3)                       */
/* ------------------------------------------------------------------------ */

/* 記録する食い違い区間の上限。ここを超えた分は数だけ数える。 */
#define MM_BAD_MAX 8

/* 食い違った区間を 3 ワードずつ: [start, end(inclusive), (期待<<4)|実物]。
 * 期待 / 実物のコードは paging.c の MM_NP / MM_RW / MM_RO / MM_ROU。
 * 件数は paging_memmap_bad_count (MM_BAD_MAX を超えても数え続ける)。
 * static にしないのは kselftest_pass と同じ理由 — 画面が流れても
 * kernel.map の番地から emu_read_mem で読めるようにするため。 */
extern u32 paging_memmap_bad[MM_BAD_MAX * 3];
extern u32 paging_memmap_bad_count;

/* tramp_page には exec の KAPI 踏み台ページ (RO+USER) の番地を渡す。
 * exec_init の前で不明なら 0。戻り値: 食い違い区間の本数 (0 = 一致)、
 * -1 = ページング無効で検証対象外。
 * **ブート直後に 1 回だけ呼ぶこと** — CPL=3 アプリを起動すると exec が
 * SHM / VRAM / フォント表を USER へ昇格させ、期待値と合わなくなる。 */
int paging_memmap_selftest(u32 tramp_page);

/* **自己診断のための変異だけ**に使う。master の PTE 1 本の USER ビットを
 * 立てる / 落として TLB を無効化する。`paging_set_page` を使わないのは、
 * あちらが USER を **PDE にも伝播させる**から — 変異を戻しても PDE に
 * USER が残り、ブートの状態が静かに変わってしまう。
 * 戻り値 0 = 変えた / -1 = ページング無効か範囲外 (PDE 0 の中だけ)。
 * 呼ぶのは kernel/kselftest.c の MM 検査だけで、**必ず戻すこと**。 */
int paging_poke_user_bit(u32 virt, int set_user);

/* 逆転した範囲 (start > end) を撥ねた回数。範囲 API は前から -1 を返して
 * いたが呼び側が見ていないので、空振りが成功に見えていた (票 §4 の 2)。 */
extern u32 paging_range_reject_count;

#endif /* __PAGING_H */
