# mm/paging.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_C_DESIGN・MEMMAP_V3 (D1 で移した)
ソース: `mm/paging.c`

|状態・値|所有者|文脈|
|---|---|---|
|PD/PT・boot_identity_end・監査状態|mm/paging.c|initの既存irq_save/restore、監査は既存文脈|

```c
/* mm/paging.c: static helper、既存ループ内で使用 */
static int platform_span_has(const struct platform_memory_span *s, u32 a)
{ return a >= s->first && a < s->end; }
static u32 native_cache_bits(u32 a)
{
    for (u32 i = 0; i < 3; i++)
        if (platform_span_has(&platform_memory_attributes.native_uc[i], a))
            return PTE_PCD;
    return 0;
}
```

- `:379` のROM RO設定は記述のfirst/end-1を渡す。`:1311–1312` の期待属性、`:683` のV86対象上端も同じROM境界を参照。NULL/tramp/guard優先順は変更しない。

「固定」= 起動時に決まり台帳では owner=kernel。「池」= 台帳が owner 付きで配る恒等ページ。**通常アプリ (CPL=3 の AS) の不変条件: PDE 0〜511 に USER の PTE は、トランポリン (RO) と SHM (契約で全アプリ共有) 以外に存在しない**。**V86 セッションは例外** (§2-2 の V86 の項): セッション中だけ master の低位 (ページ 0・アリーナ・VRAM・ROM) に USER を立て、終了時に復元する既存機構 (`v86_mem.c`) を保つ。

- AS 作成時の APP PDE は空で、私有 PT は写像時に疎に確保する。exec は作成直後にそのまま per-app 物理を張る。
- 通常の私有写像は指定された flags を使い、キャッシュ属性は lease 側の台帳から取る。
- V86 終了時は保存済みの master/active PDE を `paging_v86_restore()` で復元する。
- 共有 PT 拒否の自己試験は単ページ・identity/physical 範囲・解放と PTE/PDE 不変を維持する。内部 import 名 `paging_map_user_keep_selftest` は据え置く。

- **共有 PT を書く要求は通常の map / unmap API で拒否する**。V86 の例外は専用の入口と、終了・異常終了時の復元に限る。master を変更できる別の API (`paging.c` 系) も監査対象 — 「共有 PT の物理番号が不変」だけでは USER/RW の変更や他 AS への波及を取り逃す。

| 指摘 | 答え |
|---|---|
| ④ BB / 共有 USER / 後付け PDE | §2-2。共有 PDE の追加は最初の AS の前に全部 (池の PT、PCI BAR、デバイス窓)。lease 窓の PT は AS ごとの私有なので後付けの問題は無い |

| 何 | なぜ連続 | 対処 |
|---|---|---|
| アプリの PD / PT | — | 1 枚ずつ (4KB 整列だけ) |

## ブート窓 (`paging_init`)

> 出所: TASK_K6_ram_ceiling の契約を今のソースと照らして要約 (2026-11-18)

- `paging_init(mem_kb)` が恒等写像を張るのは min(検出量, `PAGING_BOOT_MAP_SIZE`) だけ。`PAGING_BOOT_MAP_SIZE` は静的な bootstrap PT (`PAGING_BOOT_PT_COUNT` 枚) が覆う範囲で、RAM の上限ではない (4MB の倍数であることを STATIC_ASSERT で守る)。
- 実際に張った上端 (PFN) は `paging_boot_identity_end()` で公開する。16MB の申告なら 4096。15〜16MB のシステム空間と、上端より上は Not-Present。
- ブート窓より上の RAM は `pgalloc_stage_online` が `paging_map_phys` で、boot workspace から取った PT で張る。

検査: `tools/tests/test_paging_bounds.py` (`paging_bounds_host.c`)。

## アプリ帯の AS (`mm/paging_addrspace.inc`・`mm/paging_lease.inc`)

> 出所: APP_BAND_PDE の契約を今のソースと照らして要約 (2026-11-18)

アプリ帯は `MEM_APP_BAND_BASE` (0x80000000、`APP_BAND_PDE`) から連続 `MEM_APP_BAND_MAX_PDES` (64) 枚の PDE で、上端 `MEM_APP_BAND_MAX_TOP` が `MEM_APP_STACK_TOP`。帯は lease 窓の PDE (`LEASE_PDE`) より下に収まる (`mm/paging.c` の STATIC_ASSERT)。

- `paging_addrspace_create_n(as, owner, pde_count)`: `pde_count` は 1〜`MEM_APP_BAND_MAX_PDES`、`as`・`owner` が 0 なら -1 で何も確保しない。確保するのは **PD 1 枚だけ**。master の PDE を写したうえで、`APP_BAND_PDE` から lease 窓の終わり (`MEM_LEASE_END`) までの PDE を 0 にする (高位の USER・装置の写像を継がない)。`app_pde_count` は引数によらず常に `MEM_APP_BAND_MAX_PDES`。`paging_addrspace_create` は `pde_count = 1` の呼び出し。
- `paging_addrspace_create_lease` (起動用) は master の CR3 の上・IRQ/例外の外でだけ動き、加えて lease 窓の先頭 PT を 1 枚確保して `PT|RW|USER` で張る (PD + lease PT の 2 枚)。どちらの確保に失敗しても、確保済みを返して所有者のページ数 0 で -1。
- アプリ帯の私有 PT は**写像するときに初めて** 1 枚ずつ確保する (疎)。`paging_addrspace_map_user*` は帯の外 (共有 PT) への写像を -1 で拒否する。USER は当該 AS の PD の PDE にだけ立て、master の PDE・PT は 1 ビットも変えない。
- 範囲の写像 (`paging_addrspace_map_user_range*`) は PDE 境界をまたいでよい。足りない PT を全部確保してからページを書き、途中の PT 確保失敗では今回足した PT と PDE を全部戻して -1 (部分的な PT を残さない)。
- `paging_addrspace_destroy` は lease が残っていれば何もしない。lease PT → アプリ PT → PD の順に所有者付きで返し、返却失敗なら AS を毒状態にして止める。active な PD は破棄しない。破棄後、所有者のページ数と池の空きは作成前に戻る。
- 自己診断 `paging_app_band_selftest` (kselftest に載る) は 64 枚の AS を作り、PT 0 枚・master の帯 PDE が 0 であること、stack の最上ページへの写像と解放、破棄後に空きが元へ戻ることを見る。

検査は `tools/tests/test_app_band_pde.py` (実 `mm/paging.c` をホストで動かす)。

## 地図の自己診断と、最悪配置の表明

> 出所: TASK_KSTACK_USER (§4 の 2・3) の契約を今のソースと照らして要約 (2026-11-18)

ソース: `mm/paging.c` (`paging_memmap_selftest`・`memmap_want_at`・`paging_set_not_present`・ファイル先頭の `STATIC_ASSERT` 群)・`mm/paging.h` (`paging_memmap_bad`・`paging_memmap_bad_count`・`MM_BAD_*`)・`build/os32.ld` の `ASSERT`。静的な地図の検査は `tools/gen_memmap.py` (`docs/design/tools/gen_memmap.md`)。

### `paging_memmap_selftest(tramp_page)` — 実物のページ表と地図の照合

- 0 から `MEM_DMA_POOL_END` までの全ページについて、`memmap.h` から導いた期待 (`memmap_want_at`: NP / RW / RO / RO+USER / RW+USER) と実物の PTE を比べる。`tramp_page` (KAPI の踏み台、RO+USER) も期待に入れる。
- 同じ (期待, 実物) の組が続く区間を 1 本にまとめ、`paging_memmap_bad[]` に 3 語ずつ (先頭、末尾 (含む)、`(期待 << 4) | 実物`) 積む。積むのは `MM_BAD_MAX` 本までだが、本数は超えても数え続ける (`paging_memmap_bad_count`)。戻り値は本数 (0 = 一致)、ページングが無効などで照合できなければ -1。
- **浮動番地の規則はカーネル帯 (`KERNEL_LOAD_ADDR`〜`MEM_KERNEL_BAND_END`) の中にだけ適用する。** 帯の境界は固定番地。境界を越えて育った SHM を期待に含めると、期待値が壊れた定数から作られて予算超過の配置が「一致」に見え、自己診断が穴を隠す。
- `paging_set_not_present(start, end)` は `start > end` を -1 で断り、`paging_range_reject_count` を数える (呼び手が戻り値を見なくても空振りが残る)。

### 最悪配置の表明

`KHEAP_BASE` 以降は `__bss_end` から動く浮動番地なので定数式にならず、`STATIC_ASSERT` に混ぜると「variably modified at file scope」で落ちるか、黙って死ぬ。そこで上限をリンカに置く: `build/os32.ld` の `ASSERT` が常駐の鎖の終端 (`LAYOUT_MEM_SHM_RESV_START`) を固定ページ表の手前のガード (`LAYOUT_MEM_FIXED_PAGING_GUARD_BASE`) 以下に止める (カーネル本体の予算 `MEM_KERNEL_IMAGE_MAX` と同値)。上限が保証されるので、**予算いっぱいまで育った場合の配置** (`*_MAX` の定数) で重なりを表明できる。

| 表明 (`mm/paging.c`) | 意味 |
|---|---|
| `shm_band_within_kernel_band` | 最悪配置でも SHM 帯 (前方ガード〜後方ガード) がカーネル帯に収まる |
| `shm_resv_not_reversed` | 最悪配置でも SHM 後方の予約が逆転しない (空は許す) |
| `shm_band_below_kstack_guard` | 最悪配置でも SHM 帯はスタックガードより下で終わる |
| `kstack_outside_shm_band` | カーネルスタックの全体が SHM 帯の外 |
| `kstack_outside_kernel_band` | スタックガードは浮動番地の帯 (カーネル帯) の外 |
| `kheap_kapi_within_band` | 最悪配置でもヒープと KAPI がカーネル帯に収まる |

**表明の条件に浮動番地を混ぜない。** 浮いた番地と固定番地を同じ帯で隣り合わせにしない (スタックはカーネル帯の外の固定番地に置く)。

検査: `make check-memmap-host` の `tools/tests/test_memmap_boot.py` (本体 `tools/tests/memmap_boot_host.c`)。実物の `mm/paging.c` を `#include` し、起動順を再生してから自己診断を呼ぶ。`mm/shm.c` の `shm_init` は写しで再生し、写しが本物の本文とずれていないことを突き合わせる。`--mutate` の中心は `band-unbounded` (浮動番地の規則を帯の外へ広げる)。
