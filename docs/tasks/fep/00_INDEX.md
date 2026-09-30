# FEP 詳細設計 — 索引

> 発行: PM (2026-08-07) / 状態: **実装中 (2026-09-05)**

OS32 カーネル常駐型 FEP (日本語入力フロントエンドプロセッサ) の拡張・改善に関する
**実装レベルの詳細設計**。[`FEP_FUTURE.md`](FEP_FUTURE.md) の各タスクを、データ構造変更・
関数シグネチャ・アルゴリズム・コードスケッチ・影響範囲・テスト計画まで落とし込んだ設計書群。

- 実装状態の正典は**下表** (フェーズ別)。[`FEP_STATUS.md`](FEP_STATUS.md) は 2026-04-27 時点の
  アーキテクチャ説明 (辞書スキーマ / 入力フロー / 公開 API) で、進捗の判断には使わない
- 元タスク一覧: [`FEP_FUTURE.md`](FEP_FUTURE.md)
- SQLite カーネル統合: [`../../archive/v21/sqlite/00_INDEX.md`](../../archive/v21/sqlite/00_INDEX.md)
- 対象ソース: `kernel/ime.c` / `kernel/ime.h` / `kernel/ime_dict.c` / `kernel/ime_romkana.c`

> **コーディング規約 (本設計のコードスケッチも準拠):**
> C89 — `//` コメント禁止 (`/* */`)、変数宣言はブロック先頭、`for (int i...)` 禁止。
> カーネル本体はレジスタ規約 (`-3r -ecc`)、KAPI ラッパーは `__cdecl`。

---

## フェーズ別ドキュメント

| Phase | ドキュメント | 内容 | 状態 | 優先度 | 依存 |
|-------|-------------|------|------|--------|------|
| P1 | [`01_UI_CANDIDATE.md`](01_UI_CANDIDATE.md) | 候補操作の拡充 (↑↓ / 数字 / ページング) | **実装済** | 高 | §1 描画抽象化 |
| P2 | [`02_SCROLL_GUARD.md`](02_SCROLL_GUARD.md) | TVRAM スクロール保護 | **実装済** | 高 | なし |
| P3 | [`03_DICT_COMMAND.md`](03_DICT_COMMAND.md) | 辞書管理シェルコマンド `ime` | **実装済** | 中 | KAPI 公開済 |
| P4 | [`04_DICT_QUALITY.md`](04_DICT_QUALITY.md) | 辞書品質向上 (PRAGMA / 頻度 / 動的切替) | 一部 (動的切替のみ) | 中 | P3 (切替 UI) |
| P5 | [`05_GFX_MODE.md`](05_GFX_MODE.md) | GFX モード対応 | 未着手 | 高(大規模) | §1 描画抽象化 |
| P6 | [`06_MISC.md`](06_MISC.md) | その他 (XFER / kprintf va_args) | 一部 (XFER のみ) | 低 | なし |
| P7 | [`07_BUNSETSU.md`](07_BUNSETSU.md) | 連文節変換 (将来構想・保留) | 未着手 | 低 | 全て |
| — | [`TASK_DICT_META.md`](TASK_DICT_META.md) | 辞書のメタ情報 (形式の版・dict_id・license・`mem_reserve_kb` 等) と学習データの別ファイル化 (`/etc/fep_user.db`)。ユーザー決定 2026-09-30 (TASK_MEMMAP_V3 D27・D28) | **計画** (着手は **v3 の後の方**) | 中 | v3 T4 (SQLite モジュール化)・T5a (FEP モジュール化) |

P1→P2→P3 は独立して着手可能。P5 は描画バックエンド抽象化を伴う最大の変更で、
他フェーズと切り離して最後に実施する。

> **実装状況 (2026-08-07):** §1 の描画バックエンド抽象化 (`kernel/ime_render.h` +
> `ime_render_tvram.c`) と P1 / P2 / P3 は実装され、本リポジトリ `main` に
> 移植済み (KAPI v35)。経緯は
> [`../../archive/v21/wintree_port/PORT_PLAN.md`](../../archive/v21/wintree_port/PORT_PLAN.md) フェーズ6 を参照。
>
> **解決済み (2026-08-07):** 「漢字変換が候補ゼロになり SPACE がかな確定に
> フォールバックする」不具合は、複合原因を特定して修正した。
> 1. `ime_set_mode()` (`ime` コマンドの `ime on` 経路) が辞書を開いて
>    いなかった — `dict->db == NULL` のまま `ime_dict_search` が即 0 を
>    返していた。→ `ime_toggle()` と同じ遅延オープンを追加。
> 2. 辞書を開けても、外部プログラム終了時の `exec_exit()` が FD 3 以上を
>    一括クローズするため、FEP 辞書の SQLite fd が回収され以後の検索が
>    全て `SQLITE_IOERR_READ` になっていた。→ VFS に FD 保護フラグ
>    (`vfs_fd_set_protect`) を追加し、FEP 常駐接続の fd を保護。
> MEMSYS5 プール枯渇説は 400KB 拡張テストで棄却 (200KB で動作確認済み)。
> `ime_dict_search` には step エラー時のみ出る診断ログを追加した。
> 変換・候補送り・確定・学習 (`dict_user` への UPSERT と 5.7MB DB 末尾=
> 二重間接ブロック領域への永続化) を NP21/W 実機で検証済み。

---

## 0. 設計の前提条件

| 前提 | 内容 |
|------|------|
| シングルタスク | FEP ON 時はアプリのメインループが `ime_getkey()` 内でブロックする。割り込み駆動の非同期描画は不要 |
| メモリ制約 | カーネルヒープ + MEMSYS5 (SQLite 用 100KB)。`IME_State` は `static` グローバル 1 個 (BSS) |
| 描画は CPU 直接書込のみ | EGC/GRCG/GDC 描画コマンド禁止 (`CLAUDE.md` 落とし穴参照)。TVRAM/GVRAM への直接 `volatile` 書込のみ |
| 既存 API 互換維持 | `ime_getchar()` / `ime_getkey()` のシグネチャと戻り値仕様は変更しない。KAPI バイナリ互換を壊さない |
| UTF-8 内部表現 | かなバッファ・確定バッファは全て UTF-8。文字境界処理は `utf8_*` を使用 |

### 0.1 現状の主要データ構造 (再掲・変更基点)

`kernel/ime.h` の `IME_State` が状態の単一実体。各フェーズで拡張するフィールドはこの構造体に追記する。

```c
typedef struct {
    int         mode;           /* IME_MODE_xxx */
    IME_RomKana rk;             /* ローマ字かな変換 */
    char        kana_buf[128];  /* 入力中のかな列 */
    int         kana_len;
    IME_Result  results[IME_MAX_RESULTS];   /* IME_MAX_RESULTS = 32 */
    int         result_count;
    int         candidate_idx;
    int         converting;
    int         convert_len;
    char        commit_buf[256];
    int         commit_pos;
    int         commit_len;
    IME_Dict    dict;
    int         dict_loaded;
} IME_State;
```

---

## 1. 全体アーキテクチャ方針 (共通設計基盤)

本拡張で導入する横断的な設計判断。**P1 と P5 が共有する基盤**であり、
個別フェーズに先立って整備する。

### 1.1 描画バックエンドの抽象化 (P1 / P5 の土台)

現在の `preedit_draw()` は TVRAM への直接書込にハードコードされている。
P5 (GFX モード) で描画先を切り替えるため、**描画プリミティブを関数ポインタ表に逃がす**。
P1 の候補リストウィンドウもこの抽象を通すことで、テキスト/グラフィック両モードで再利用できる。

```c
/* ime_render.h (新規) — 描画バックエンド抽象 */
typedef struct {
    /* 1 セル ANK 文字。x,y はセル座標 (TVRAM 互換 80x25 グリッド) */
    void (*putc)(int x, int y, char ank, u8 color);
    /* 全角 1 文字 (Unicode コードポイント)。戻り値=消費セル幅(1 or 2) */
    int  (*putw)(int x, int y, u32 codepoint, u8 color);
    /* y 行を空白で消去 */
    void (*clear_row)(int y, u8 color);
    /* バックエンド固有: 描画開始/終了 (GFX 退避・復元フック) */
    void (*begin)(void);
    void (*end)(void);
} IME_Render;
```

- テキストモード実装 (`ime_render_tvram.c`): 既存の `tvram_putchar_at` / `tvram_putkanji_at` / `unicode_to_jis` をラップ。
- グラフィックモード実装 (`ime_render_gfx.c`): GVRAM プレーンへの直接ビットマップ転送 ([`05_GFX_MODE.md`](05_GFX_MODE.md))。

`preedit_draw()` は `g_ime.render->putc(...)` のように間接呼出に書き換える。
**P1 着手時にまずこの抽象化リファクタリングを行い**、TVRAM 実装を差し込んで挙動不変を確認してから機能追加する。

> リファクタリングを P1 の頭に置く理由: 候補リストウィンドウ描画 ([`01_UI_CANDIDATE.md`](01_UI_CANDIDATE.md)) は複数行に渡るため、
> どのみち `preedit_draw` の大改修が必要。抽象化を同時に入れた方が手戻りが少ない。

### 1.2 状態機械の明確化

現在 `converting` フラグ 1 個で「変換候補表示中」を表現している。
候補リスト UI 導入に伴い、サブ状態を列挙型で明示する (可読性・将来の連文節拡張のため)。

```c
#define IME_ST_INPUT      0   /* かな入力中 (未変換) */
#define IME_ST_CONVERT    1   /* 変換候補選択中 (インライン ▼ 表示) */
#define IME_ST_CANDLIST   2   /* 候補リストウィンドウ展開中 (P1 で追加) */
```

`converting` は `state != IME_ST_INPUT` と等価。既存コードの `g_ime.converting` 参照は
段階的に `g_ime.state` へ置換する (互換のため当面は両方を同期させてもよい)。
将来の連文節変換 ([`07_BUNSETSU.md`](07_BUNSETSU.md)) では `IME_ST_BUNSETSU` を追加できる構造にしておく。

---

## 9. KAPI 追加一覧 (全フェーズ集約)

| 関数 | フェーズ | 用途 |
|------|---------|------|
| `ime_switch_dict(int variant)` | P3/P4 | 辞書バリアント切替 |
| `ime_user_list(prefix, out, max)` | P3 | 学習辞書列挙 |
| `ime_user_delete(yomi, kanji)` | P3 | 学習エントリ削除 |
| `ime_user_export(path)` | P3 | CSV エクスポート |
| `ime_user_clear()` | P3 | 学習辞書全消去 |

> いずれも `KernelAPI` 構造体末尾追加 → `KAPI_VERSION` +1 → `KAPI_SPEC.md` 更新。
> 既存 `ime_*` (getchar/getkey/toggle/is_active/set_mode/get_mode/trygetchar) は変更なし。
> 詳細手順は [`03_DICT_COMMAND.md`](03_DICT_COMMAND.md) §KAPI 拡張手順。

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
| `kapi/kapi_ime.c` | | | ● | | | | 新規ラッパー |
| `exec/exec.h` / `exec.c` | | | ● | | | | KAPI 登録 |
| `tools/kapi.json` | | | ● | | | | KAPI 定義 |
| `programs/os32api.h` | | | ● | | | | 宣言複製 |
| `programs/ime.c` | | | ● | | | | 新規シェルコマンド |
| `tools/fep_to_sqlite.py` | | | | ● | | | コスト式拡張 |
| `deploy.yaml` | | | | ● | | | S/L 辞書配備 |
| `build.sh` | ● | | | | ● | | C_KERNEL 追加 |

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

各マイルストーン完了時に **`bash build.sh` でエラー 0** を必須確認。
カーネル変更を伴う M1〜M3・M5・M6 は NP21/W リモートテスト (各フェーズのテスト項目) を実施する。

---

*FEP 詳細設計 索引 — 2026-05-30 / 対応タスク: [FEP_FUTURE.md](FEP_FUTURE.md)*
