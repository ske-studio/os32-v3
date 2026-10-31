# exec/lease.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: MEMMAP_V3・SLIM_KERNEL_DESIGN・SLIM_KERNEL_IMPORTS・GFX_MODES_DESIGN (D1 で移した)
ソース: `exec/lease.c`

- **lease = 台帳の SURFACE (owner、物理範囲または VRAM 範囲、幅・高さ・ピッチ・形式) を 1 つの AS の lease 窓に USER (RW/RO、PCD は物理の属性を継承) で張ること。** `addrspace_map_user_page` の「共有 PT を書き換えて PDE に USER を立てる」経路 (`paging.c:796-812`) は**廃止**し、lease 窓の私有 PT にだけ書く。
- 解放は「その AS の lease を外す」だけで他 AS に波及しない。物理の解放は全 lease が返り、かつ owner が返したとき。
- 表示面 (planar の VRAM、PEGC のリニア窓 / バンク窓、Cirrus の表示面) は**全画面 lease だけ**に貸す (G4、D19)。通常 GUI アプリには VRAM の lease を与えない (`exec.c:1932` の常時 USER 写像を撤去)。CUI アプリの TVRAM 直書き (`lconsole` 等、U20) は TVRAM (0xA0000–0xA3FFF) だけの lease。

- 検査 (§2-3 の ⑥ (b)(c)) は「共有種別なら可」ではなく **「その AS の台帳に lease があるページなら可」**。

- **キャッシュ属性は台帳から**: 現行の `keep_cache` は書込先 PTE の PCD/PWT を読む (`paging.c:806`) が、新規の lease PTE は空なので継承できない (Cirrus の面をこの方法で写すと PCD が消える)。**SURFACE が参照する物理資源の台帳 (RAM / MMIO 登録の属性) から取り、kernel alias と一致させる**。

- **ページ端と寿命**: 面の末尾をページへ切り上げた結果、表示面・MMIO・別 owner の RAM が同じページに同居すると、その部分も露出する。**面はページ単位で占有する** (SURFACE の確保はページ整列、同じページを他の用途と共有しない)。解除の順は **lease 解除 → TLB 反映 → 参照数の減算 → (owner も返していれば) 物理の回収** に固定。再 init・モード変更では**既存の lease を残したまま面の意味を変えない** (先に revoke)。
- **「互いに見えない」の定義**: 同じ物理面を明示的に 2 つの AS に貸せば画素は共有される。隔離が意味するのは **未貸与の面へのアクセスと、lease 操作 (付け外し) の他 AS への波及を防ぐ**こと。

| 時点 | 誰が | 何を |
|---|---|---|
| lease の付け外し | カーネル (通常文脈) | 物理は動かさず、lease 窓への写像だけ。PT が足りないときだけ池から 1 枚 (R3-e) |

AS 生成時に **lease 窓の先頭 4MB の PT を 1 枚事前確保**
**lease 台帳は AS ごとの固定配列**
で実行時確保はしない。

exec/lease.c
leaseは既存観測変数への1代入。

lease_selftest_result

- `kdi_selftest_lease_result` は `exec/lease.c` に既存観測変数への 1 代入として置く。

| 記号 | 元の種類・操作 | r2での扱い |
|---|---|---|
| lease_revoke_sid | FUNC | lease_revoke_allのカーネル実体から呼ぶ |
| lease_revoke_all_fail_count | データ読書き（GETTER/SETTER相当） | lease_revoke_all内の加算のまま。値をkdebugで読み書きしないので追加口不要 |
| lease_selftest_result | データ書込み（SETTER相当） | 新FUNC kdi_selftest_lease_result経由。従来の公開観測シンボルは残す |

- lease は既存の台帳 npages と plane_offset をそのまま使用。
