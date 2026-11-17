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
