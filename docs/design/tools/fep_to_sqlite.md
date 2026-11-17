# tools/fep_to_sqlite.py — FEP のシステム辞書 (S/M/L) を作る、辞書の形式 1

> 状態: **現行 (2026-11-18)** — 出所: DICT_META_DESIGN・TASK_DICT_META (要約して移した)
ソース: `tools/fep_to_sqlite.py`・`build/assets.mk` (FEP 辞書の規則)

## 何をするか

IPADIC 2.7.0 の CSV から、かな漢字変換のシステム辞書を SQLite で作る。
ゲストは辞書を読み取り専用で開き、ヘッダと meta を `kernel/sqlite_engine.c` の `fep_meta_probe` で検査する。
この文書の形式を変えたら、その検査と `kernel/ime/ime_store.h` の定数も揃える。

| 版 | 閾値 (正規化後の cost) | 生成先 | ゲストのパス |
|---|---|---|---|
| S | 800 以下 | `assets/fep_s.db` | `/db/fep_s.db` |
| M (既定) | 1500 以下 | `assets/fep.db` | `/db/fep.db` |
| L | 無制限 | `assets/fep_l.db` | `/db/fep_l.db` |

閾値は IPADIC の生の cost ではなく、`normalize_costs` (対数で 0〜2047 に写し、常用漢字・短縮の度合い・カタカナで補正) の後の値。
今の配備で載るのは M だけ。S/L は `build/assets.mk` に規則があるが配備していない。

## 形式 1

- page_size は 1024。ヘッダの user_version (offset 60〜63、ビッグエンディアン) に 1 を書く (`PRAGMA user_version = 1`)。
  ゲストの SQLite は `SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS` のままでよい (ゲストはヘッダを直に読む)。
- `dict(yomi, kanji, pos_id, cost)` と索引 `idx_dict_yomi(yomi)` は旧形式から変えない。同じ yomi・kanji は最小の cost、同点なら最小の pos_id の 1 行にする (再生成で同じ DB になる)。
- `meta(key TEXT PRIMARY KEY, value TEXT NOT NULL)`。数値も十進の ASCII、文字列は UTF-8。

| key | 値 | ゲストの判定 (`fep_meta_probe`) |
|---|---|---|
| `format_version` | `1` | ヘッダの値と一致 |
| `min_fep_version` | `1` | `FEP_DICT_READER_VERSION` 以下 |
| `dict_id` | `ipadic-2.7.0` (S/M/L 共通、版の名前を含めない) | 64 byte 未満。学習 DB との照合に使う |
| `kind` | `system` | 一致 |
| `language` / `charset` | `ja` / `UTF-8` | 一致 |
| `variant` | `S`・`M`・`L` | 1 文字。固定の 3 パスならパスと一致 |
| `source` / `source_version` | `IPADIC` / `2.7.0` | 空でない文字列 |
| `license` / `attribution` | 同梱の `COPYING` の全文 (どちらも同じ原文) | 空でない文字列 |
| `build_tool` / `build_date` | `fep_to_sqlite.py/1` / UTC ISO 8601 | 空でない文字列 |
| `entry_count` | 最終の `dict` の行数 | 十進。起動時は数えない |
| `pos_table_version` | `1` | `FEP_DICT_POS_VERSION` と一致 |
| `cost_scale` | `os32-log2047-boost-v1` (今の `normalize_costs` を指す名前) | 一致 |
| `mem_reserve_kb` | `192` (S/M/L とも) | 0 でない十進 |
| `cache_pages` | `16` | 0 でなく、`mem_reserve_kb` 以下、残りに学習の 8 頁が入る |
| `checksum` | `sha256:` + 64 桁の 16 進 | 形だけ。ゲストは計算しない |

- 必須の 19 キーが全部あること。1 つでも欠ければ `NOTFOUND`、重複・空・形の誤りは `INVAL`。知らないキーは読み飛ばす。
- 品詞の対応や cost の尺度を変えたら `dict_id` か `pos_table_version`・`cost_scale` を変える (古い学習 DB と取り違えないため)。
- `COPYING` は Latin-1 で読む。空なら生成を失敗させる。
- `build_date` は `SOURCE_DATE_EPOCH` があればその時刻 (再現できるビルド)。
- 生成器は `cache_pages × 1024 ≤ mem_reserve_kb × 1024` を確かめる。

## checksum

`dict` の行だけを対象にする (meta は含めない)。`yomi`・`kanji`・`pos_id`・`cost` の昇順 (TEXT は BINARY) に、
各 TEXT は UTF-8 の byte 長 (u32 ビッグエンディアン) と本体、整数は符号つき i32 ビッグエンディアンを並べた列の SHA-256。
ゲストは起動で辞書を全部読まない。照合は生成と受入の側で行う。

## 旧形式 (形式 0)

user_version が 0 で page_size が 1024、`dict` の 4 列が prepare できれば旧形式として開く。meta は無くてよい。
2 以上は `INVAL` で断る。ヘッダが壊れた物を旧形式とみなさない。
旧形式の既定値 (`dict_id`・品詞の版・尺度・予約 192・cache 16、variant はパスから) は `kernel/ime/ime_store.h` の定数。

## 検査

- `fep-generator-meta` (`tools/tests/test_fep_generator_meta.py`): 生成した meta を SQLite の中身と照合 (`entry_count`・checksum・原文)。
- `fep-meta` (`tools/tests/test_fep_meta.py`): 独立に作った正解と、製品の probe で旧形式・未来版・壊れたヘッダ・meta の欠け。
