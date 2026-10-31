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

「固定」= 起動時に決まり台帳では owner=kernel。「池」= 台帳が owner 付きで配る恒等ページ。**通常アプリ (CPL=3 の AS) の不変条件: PDE 0〜511 に USER の PTE は、トランポリン (RO) と SHM (契約で全アプリ共有) 以外に存在しない**。**V86 セッションは例外** (§2-2 の V86 の項): セッション中だけ master の低位 (ページ 0・アリーナ・VRAM・ROM) に USER を立て、終了時に復元する既存機構 (`v86_mem.c:73,99`) を保つ。

- **共有 PT を書く要求は通常の map / unmap API で拒否する**。V86 の例外は専用の入口と、終了・異常終了時の復元に限る。master を変更できる別の API (`paging.c:486` 系) も監査対象 — 「共有 PT の物理番号が不変」だけでは USER/RW の変更や他 AS への波及を取り逃す。

| 指摘 | 答え |
|---|---|
| ④ BB / 共有 USER / 後付け PDE | §2-2。共有 PDE の追加は最初の AS の前に全部 (池の PT、PCI BAR、デバイス窓)。lease 窓の PT は AS ごとの私有なので後付けの問題は無い |

| 何 | なぜ連続 | 対処 |
|---|---|---|
| アプリの PD / PT | — | 1 枚ずつ (4KB 整列だけ) |
