# exec/lease.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: MEMMAP_V3・SLIM_KERNEL_DESIGN・SLIM_KERNEL_IMPORTS・GFX_MODES_DESIGN (D1 で移した)
ソース: `exec/lease.c`

- **lease = 台帳の SURFACE (owner、物理範囲または VRAM 範囲、幅・高さ・ピッチ・形式) を 1 つの AS の lease 窓に USER (RW/RO、PCD は物理の属性を継承) で張ること。** `addrspace_map_user_page` の「共有 PT を書き換えて PDE に USER を立てる」経路 (`paging.c`) は**廃止**し、lease 窓の私有 PT にだけ書く。
- 解放は「その AS の lease を外す」だけで他 AS に波及しない。物理の解放は全 lease が返り、かつ owner が返したとき。
- 表示面 (planar の VRAM、PEGC のリニア窓 / バンク窓、Cirrus の表示面) は**全画面 lease だけ**に貸す (G4、D19)。通常 GUI アプリには VRAM の lease を与えない (`exec.c` の常時 USER 写像を撤去)。CUI アプリの TVRAM 直書き (`lconsole` 等、U20) は TVRAM (0xA0000–0xA3FFF) だけの lease。

- 検査 (§2-3 の ⑥ (b)(c)) は「共有種別なら可」ではなく **「その AS の台帳に lease があるページなら可」**。

- **キャッシュ属性は台帳から**: 新規の lease PTE は空なので、書込先 PTE から PCD/PWT は継承できない。**SURFACE が参照する物理資源の台帳 (RAM / MMIO 登録の属性) から取り、kernel alias と一致させる**。

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

## 公開 lease の契約 (`surface_lease`・`surface_lease_bundle`)

> 出所: TASK_T2D_T2H (§2-2・§2-3・§2-5 e1〜e3) の契約を今のソースと照らして要約 (2026-11-18)

### 共通 (`lease_acquire`)

- 1 AS の lease は固定 8 本 (`MEM_LEASE_MAX`)、窓は `MEM_LEASE_BASE` から。空き slot が要求本数に足りない・token の採番が尽きる・窓に収まらない・面の lease 数が上限なら FULL。足りないからと枠を増やさない。
- 各 ref について: sid が範囲外は INVAL。面が無い・世代が違う・closing は STALE。backend・役割が授権と違う・`perm_max` が NONE・RW 要求で `perm_max` が RO・台帳の検証や kernel alias のキャッシュ属性 (`alias_cache`) が台帳と食い違うなら INVAL。
- 写像の属性は `PRESENT|USER` (+RW)、UC の面は `PCD`。キャッシュ属性は台帳から取り、kernel alias と同じにする。
- 準備 (slot・VA・PT の見積り) で失敗すれば何も変えない。公開は `paging_lease_map` の短い区間で全部まとめて行い、成功したときだけ token の採番を進める。

### 単面 (`surface_lease`)

- USER の呼び手だけ (保存された由来が `CALLER_USER`)。IRQ・例外の中は INVAL。count は 1。**PC98 の DISPLAY を単面で求めると INVAL** (束を使う)。CLIENT・DISPLAY は選択中 backend 以外なら INVAL。
- 入力の ref は B1 でカーネルへ写し、出力は書く前に B1 で範囲を検査する。授権 (`surface_query_authorize`) と ref の照合 (`surface_query_refs`) を IRQ 保存の区間で行う。
- copyout が拒否されたら今回の token だけを返し、slot のバイトと採番を元に戻して INVAL。既存の lease・他 AS・master は変えない。

### PC98 DISPLAY の 4 面束 (`surface_lease_bundle`)

- 役割 DISPLAY・backend PC98・count 4 だけ。それ以外・IRQ/例外の中は INVAL。
- 授権: CUI で GFX フラグ付きのアプリ、または GUI の全画面の所有者 (`appslot_gfx_owner`) だけ。通常の GUI アプリと GFX 未宣言のアプリは拒否 (`docs/design/exec/surface_query.md` が持つ授権の表)。
- 4 本の ref を全部先に検査してから世代を見るので、後ろの INVAL が前の STALE より優先する。本数・順序 (面の先頭番地順)・欠落・重複・別の束の面・世代 0 は INVAL、古い世代は STALE。どれでも出力は変えない。
- 4 面に要る slot・PT・出力を事前に検査し、全成功か全巻き戻し。4 面目だけ追加の PT が要って取れなければ NOSPC で、master・他 AS・既存 lease・slot の全バイト・台帳・ページ会計・出力・IF/CR3 を変えない。
- copyout 拒否では今回の 4 token を 1 本ずつ返す。全部返せたときだけ slot と採番を元に戻す。返せなかった token は残して `lease_rollback_fail_count` に数える (exit の revoke で回収)。
- release は token ごとでよく、再 init と終了の revoke は関連する全 token を処理する。

### native の VRAM の属性

PC-98 の native VRAM (TVRAM・B/R/G/I の各面) の master alias は、`paging_init` の最初の表の構築で UC にする (`mm/paging.c` の `native_cache_bits`)。後から WB を UC に書き換えない。CG・ROM・RAM は WB のまま。V86 の setup/teardown もこの属性を保ち、exec・V86・再 exec の各時点で低位 1MB の PCD/PWT が変わらないこと、lease の alias と台帳が一致することを `tools/tests/test_surface_bundle.py` が確かめる。
