# sys/kern/mm/pgalloc.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `sys/kern/mm/pgalloc.h`

SURFACE の GF 識別定数は既存 ID の末尾へ追加し、`sys/kern/mm/pgalloc.h`・`sdk/include/os32/os32_surface.h` の `OS32_SURFACE_CIRRUS`・`sys/kern/exec/surface_query.c` を追従する。

## 落とし穴

- **RAM 上端と穴**: 高位 RAM の上端がデバイス窓より上でも、窓自体は RAM のない穴かもしれない。
  `sys/kern/mm/pgalloc.c` の `pgalloc_range_has_ram(first, end)` に PFN 半開区間を渡し、凍結した物理地図で重複を調べる。
  未初期化や不正な範囲も安全側で扱う。`tools/tests/test_pgalloc_range.py` が窓と境界を検査する。

## 内部設計と契約 (D1)

**物理地図と所有権台帳を先に** (RAM の存在 / 割当可否 / owner / 写像を別情報に、PFN 半開区間)

## 3. 物理台帳と P2V / V2P

### 3-1. 台帳 (D2)

RAM の存在 (物理地図)・割当可否・owner・写像を**別の情報**として持つ。
owner タグ: kernel / boot / shlib / gshell / AS ごと / モジュール名。

#### 3-5-1. 規則 R1〜R7

- **R1 割り込みの中では池を確保・解放しない — ISR が行うのは要求と制御移譲だけ、回収は通常文脈で。**
  - ISR (IRQ ハンドラ本体、EOI まで) は台帳を触らない。触れば kselftest / assert で止める (これで池の排他はほぼ不要)。

- **R5 回収の検査 — 通常 AS の終了検査と永続資源を分ける。** (Codex 往復 6 P2)
  - 台帳の owner タグを 2 種に分ける: **AS owner** (アプリの ID。終了で `exec_reclaim_owned(id)` が全部回収) と **永続 owner** (kernel / boot / shlib / gshell / モジュール名。AS の終了経路は触らない)。

シェル自身の終了経路 (`exec.c` の `APP_ID_SHELL`、`res_owner 0` の回収) は永続 owner を回収しない。

## 装置の予約 (`ledger_reserve_set`) と資源レコード

> 出所: TASK_T1_LEDGER (§4-4、T1d) の契約を今のソースと照らして要約 (2026-11-18)

ソース: `sys/kern/mm/pgalloc_resource.inc` (`ledger_reserve_set`・`ledger_resource_add`、`sys/kern/mm/pgalloc.c` が取り込む)・`sys/kern/mm/pgalloc_owner.inc` (`ledger_selfcheck`)・`sys/kern/mm/pgalloc.h` (`struct ledger_span`・`struct ledger_resource`・`LEDGER_SPAN_*`・`LEDGER_WB_*`・`LEDGER_R_*`・`LEDGER_MAX_SPANS`・`LEDGER_MAX_RESOURCES`)。PCI の BAR の取り込み (`ledger_resource_import_pci`) は `sys/platform/pc98/pci_resources.c`。

### 資源レコード (`ledger_resource_add`)

- bus と width_basis (decode 幅の根拠) が既知の値、decode が [0, 4GiB] の中、写像範囲が空か decode の内側なら、表の空きに写して番号を返す。表 (`LEDGER_MAX_RESOURCES` 16 本) が満杯なら `ledger_res_overflow` を数えて断り、表は変えない。
- **予約の根拠になるのは width_basis が SIZING / DATASHEET / GLUE_CONST のレコードだけ。** RAW (列挙の採取値、幅が未確定。decode は空 = BAR の先頭 PFN) と PROBE_UNVERIFIED は表に載るが予約には使えない。

### `ledger_reserve_set(owner, spans, n)` — MMIO / 永久 RAM の一括予約

- 時期と呼び手: 起動時 (ONLINE かつ `paging_boot_context`) だけ。owner は DEVICE 種別。`spans` が NULL・`n` が 0 か `LEDGER_MAX_SPANS` (16) 超・未知の種別は断る。span は PFN の半開区間 (`end` = 1048576 で 4GiB 端) と種別 (`LEDGER_SPAN_MMIO` / `RAM`) と根拠のレコード番号。
- 手順:
  1. **span ごとに** 自分の資源レコードと照合する (正規化の前): decode の内側で、width_basis が予約の根拠になるもの。1 つのレコードで 2 つの窓を表すと片方が decode の外になって断られる。
  2. 正規化: 先頭で並べ、同種の重なる・接する span を併合し、根拠を `res_mask` の和にする。異種の重なりは断る。バイトで接する 2 窓が PFN への切り上げで重なれば、ここで重なりとして扱う。
  3. 区間の表と照合: FIXED・STAGING・BUNDLE・DMA・SURFACE_BACKING、他 owner の DEVICE と交差したら断る。BACKGROUND (RAM でない背景: 15〜16MB、2GB〜4GiB) との交差は許す。
  4. 同じ owner が既に DEVICE 区間を持つときは、正規化後の区間の集合・`res_mask`・種別が **完全に一致するときだけ成功で何も変えない** (冪等の再試行)。部分一致・付け替え (span を他の資源に) は断る。
  5. 管理範囲の中のページは L2 に owner が無いこと (使用中も永久予約も断る)。RAM 種別は全ページが eligible で範囲の内側。区間の表に空きがあること。
  6. 一括 commit: eligible を落とし、DEVICE 区間を PERMANENT で記録する (MMIO は UC、RAM は WB、管理範囲の外にかかれば OUTSIDE)。
- **失敗したら L1・L2・区間の表・資源の表・会計を全部変えない** (2 本目の span の衝突でも 1 本目を残さない)。
- 解除の口は無い。DEVICE owner の回収は断られ、MODULE の一括回収 (`ledger_reclaim_owner`) の後も DEVICE の区間と eligible の落としは残る。予約は写像ではない (PTE は変えない)。
- `ledger_selfcheck` は区間の非重複の例外として、DEVICE が BACKGROUND に重なることだけを許す。

検査: `tools/tests/test_device_reservation.py` (`check-memory-host` の中、`--mutate` 付き)。実物の `sys/kern/mm/pgalloc.c`・`sys/platform/pc98/pci_resources.c` などを ILP32 で取り込み、`irq_restore` のたびに L1 / L2 の不変条件を全ページ検査する。失敗の後は表を中身まで比べる。
