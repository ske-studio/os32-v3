# mm/pgalloc.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `mm/pgalloc.h`

SURFACE の GF 識別定数は既存 ID の末尾へ追加し、`O/mm/pgalloc.h:283`・`sdk/include/os32/os32_surface.h:11`・`exec/surface_query.c:66–68,110` を追従する。

## 落とし穴

- **RAM 上端と穴**: 高位 RAM の上端がデバイス窓より上でも、窓自体は RAM のない穴かもしれない。
  `mm/pgalloc.c:1082` の `pgalloc_range_has_ram(first, end)` に PFN 半開区間を渡し、凍結した物理地図で重複を調べる。
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

シェル自身の終了経路 (`exec.c:1367` の `APP_ID_SHELL`、`res_owner 0` の回収) は永続 owner を回収しない。
