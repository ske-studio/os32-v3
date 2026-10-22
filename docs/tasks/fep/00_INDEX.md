# FEP 詳細設計 — 索引

> 発行: PM (2026-08-07) / 状態: **実装中 (2026-09-05)**

OS32 カーネル常駐型 FEP (日本語入力フロントエンドプロセッサ) の拡張・改善に関する
**実装レベルの詳細設計**。[`FEP_FUTURE.md`](FEP_FUTURE.md) の各タスクを、データ構造変更・
関数シグネチャ・アルゴリズム・コードスケッチ・影響範囲・テスト計画まで落とし込んだ設計書群。

- 実装状態の正典は**下表** (フェーズ別)。[`FEP_STATUS.md`](FEP_STATUS.md) は 2026-04-27 時点の
  アーキテクチャ説明 (辞書スキーマ / 入力フロー / 公開 API) で、進捗の判断には使わない
- 元タスク一覧: [`FEP_FUTURE.md`](FEP_FUTURE.md)
- SQLite カーネル統合: `docs/archive/v21/sqlite/00_INDEX.md`
- 対象ソース: `kernel/ime.c` / `kernel/ime.h` / `kernel/ime_dict.c` / `kernel/ime_romkana.c`

> **コーディング規約 (本設計のコードスケッチも準拠):**
> [CONSTRAINTS.md C1](../../CONSTRAINTS.md#c1-c11-gnu11-を内部実装の基準とする)・
> [C3](../../CONSTRAINTS.md#c3-外部公開関数は-kapi-に-__cdecl-ラッパーを持つ) と
> [POLICY_DEV.md §2](../../POLICY_DEV.md#2-コーディング規約) に従う。

---

## フェーズ別ドキュメント

| Phase | ドキュメント | 内容 | 状態 | 優先度 | 依存 |
|-------|-------------|------|------|--------|------|
| P1 | [`01_UI_CANDIDATE.md`](01_UI_CANDIDATE.md) | 候補操作の拡充 (↑↓ / 数字 / ページング) | **実装済** | 高 | §1 描画抽象化 |
| P2 | [`02_SCROLL_GUARD.md`](02_SCROLL_GUARD.md) | TVRAM スクロール保護 | **実装済** | 高 | なし |
| P3 | [`03_DICT_COMMAND.md`](03_DICT_COMMAND.md) | 辞書管理シェルコマンド `ime` | **実装済** | 中 | KAPI 公開済 |
| P4 | [`04_DICT_QUALITY.md`](04_DICT_QUALITY.md) | 辞書品質向上 (PRAGMA / 頻度 / 動的切替) | 一部 (動的切替のみ) | 中 | P3 (切替 UI) |
| P5 | [`05_GFX_MODE.md`](05_GFX_MODE.md) | GFX モード対応 | 未着手 | 高(大規模) | §1 描画抽象化 |
| P6 | [`06_MISC.md`](06_MISC.md) | その他 (XFER / kprintf va_args) | 一部 (XFER の候補操作。入力中の変換開始は未対応) | 低 | なし |
| P7 | [`07_BUNSETSU.md`](07_BUNSETSU.md) | 連文節変換 (将来構想・保留) | 未着手 | 低 | 全て |
| — | [`TASK_DICT_META.md`](TASK_DICT_META.md) | 辞書のメタ情報 (形式の版・dict_id・license・`mem_reserve_kb` 等) と学習データの別ファイル化 (`/etc/fep_user.db`)。ユーザー決定 2026-09-30 (MEMMAP_V3 D27・D28) | **計画** (着手は **v3 の後の方**) | 中 | v3 T4 (SQLite モジュール化)・T5a (FEP モジュール化) |

P1→P2→P3 は独立して着手可能。P5 は描画バックエンド抽象化を伴う最大の変更で、
他フェーズと切り離して最後に実施する。

> **実装状況 (2026-08-07):** §1 の描画バックエンド抽象化 (`kernel/ime_render.h` +
> `ime_render_tvram.c`) と P1 / P2 / P3 は実装され、本リポジトリ `main` に
> 移植済み (KAPI v35)。経緯は
> `docs/archive/v21/wintree_port/PORT_PLAN.md` フェーズ6 を参照。
>
> **解決済み (2026-08-07):** 候補ゼロ障害の原因と修正は [P6 §4](06_MISC.md#4-候補ゼロ障害の記録-2026-08-07) を参照。

---

## 0. 設計の前提条件

メモリ・入力モデルの前提は [P1 の共通設計](01_UI_CANDIDATE.md#共通設計の前提) を参照。

## 1. 全体アーキテクチャ方針 (共通設計基盤)

### 1.1 描画バックエンドの抽象化 (P1 / P5 の土台)

実装済みの TVRAM 抽象と将来の GFX 計画は [P1 の共通設計](01_UI_CANDIDATE.md#描画バックエンドと状態機械) を参照。

### 1.2 状態機械の明確化

状態の定義と実装状況は [P1 の共通設計](01_UI_CANDIDATE.md#描画バックエンドと状態機械) を参照。

---

## 9. KAPI 追加一覧 (全フェーズ集約)

| 関数 | フェーズ | 用途 |
|------|---------|------|
| `ime_switch_dict(int variant)` | P3/P4 | 辞書バリアント切替 |
| `ime_user_list(prefix, out, max)` | P3 | 学習辞書列挙 |
| `ime_user_delete(yomi, kanji)` | P3 | 学習エントリ削除 |
| `ime_user_export(path)` | P3 | CSV エクスポート |
| `ime_user_clear()` | P3 | 学習辞書全消去 |

> 追加・再生成の手順は [KAPI_SPEC.md §3-1](../../KAPI_SPEC.md#3-1-kernelapi-関数の追加手順) が正典。上表の関数は実装済み。

---

## 10. ファイル変更マトリクス

| ファイル | P1 | P2 | P3 | P4 | P5 | P6 | 種別 |
|---------|----|----|----|----|----|----|------|
| `kernel/ime.h` | ● | | ● | | ● | | 構造体/定数追加 |
| `kernel/ime.c` | ● | ● | ● | | ● | ● | ロジック改修 |
| `kernel/ime_dict.c` | | | ● | ● | | ● | 辞書操作追加 |
| `kernel/ime_romkana.c` | | | | | | | 変更なし |
| `kernel/ime_render.h` | ● | | | | ● | | 新規 (描画抽象) |
| `kernel/ime_render_tvram.c` | ● | | | | ● | | 新規 |
| `kernel/ime_render_gfx.c` | | | | | ● | | 新規 |
| `kernel/console.c` | | ● | | | | | スクロール改修 |
| `include/tvram.h` | | ● | | | | | API 追加 |
| `gfx/gfx_core.c` / `gfx.h` | | | | | ● | | `gfx_is_active` 公開 |
| `kapi/kapi_generated.c` (生成物) | | | ● | | | | 生成ラッパー |
| `exec/exec_kapi_init.inc` (生成物) | | | ● | | | | KAPI 登録 (手編集しない) |
| `sdk/kapi.json` | | | ● | | | | KAPI 定義 |
| `sdk/include/os32/os32_kapi_shared.h` | | | ● | | | | 共有型定義 |
| `userland/cmds/ime.c` | | | ● | | | | 新規シェルコマンド |
| `tools/fep_to_sqlite.py` | | | | ● | | | コスト式拡張 |
| `deploy.yaml` | | | | ● | | | S/L 辞書配備 |
| `build/kernel.mk` | ● | | | | ● | | C_KERNEL 追加 |

---

## 11. 実装順序とマイルストーン

```
M1 (UI 基盤)       : §1.1 描画抽象化 + P2 スクロール保護    → 既存挙動不変を確認
M2 (候補操作)      : P1 ↑↓/数字/ページング                → 体感品質の即効改善
M3 (辞書管理)      : P3 ime コマンド + P4 PRAGMA            → 運用性・速度
M4 (辞書品質)      : P4 頻度データ + 永続化検証             → DB 再生成中心
M5 (GFX 対応)      : P5 GVRAM オーバーレイ                  → 大規模・独立
M6 (仕上げ)        : P6 XFER / kprintf 調査                → 細部
(保留) P7 連文節
```

各マイルストーン完了時は [ビルド・検証手順](../../08_build.md#ビルドターゲット) (§8-4) に従って確認する。
カーネル変更を伴う M1〜M3・M5・M6 は NP21/W リモートテスト (各フェーズのテスト項目) を実施する。

---

*FEP 詳細設計 索引 — 2026-05-30 / 対応タスク: [FEP_FUTURE.md](FEP_FUTURE.md)*
