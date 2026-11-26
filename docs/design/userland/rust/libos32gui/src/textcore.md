# userland/rust/libos32gui/src/textcore.rs — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_EDIT_GUI (§2 決裁 A1、受入 E8・E10) の契約を今のソースと照らして要約
ソース: `userland/rust/libos32gui/src/textcore.rs`

「UTF-8 の字の桁」と「バイト列の編集」の下請け。core だけに依存し、os32api にもクレートの他の部分にも依存しない。`WK_TEXTBOX` (1 行、種別 4)・`WK_TEXTAREA` (複数行、種別 8)・エディタの本文 (`userland/rust/edit_gui/src/doc.rs`) と、`draw.rs` の幅の計算が同じ実体を使う。写しは作らない (桁の数え方がずれると日本語で必ず露見する)。

## 桁の規則 (`cols_at`)

| 先頭の字 | 桁 |
|---|---|
| `b < 0x80` | 1 (`COLS_ANK`) |
| 多バイトで、復号した字が `LATIN_HALF_FIRST`..=`LATIN_HALF_LAST` (U+00A0..=U+024F) | 1 |
| それ以外の多バイト (和文など。UTF-8 で 3 バイト) | 2 (`COLS_KANJI`) |
| 継続バイト単独・不正バイト | 1 (1 バイト読み飛ばす) |
| 末尾で切れた多バイト | 先頭バイトだけで決める (2) |
| 空 | 0 |

- `seq_len` は先頭バイトからの長さ (`0xC0`/`0xE0`/`0xF0` の型で 2/3/4、それ以外の非 ASCII は 1)。
- `columns(s)` は `cols_at` の和。`cols_of(b)` は先頭バイトだけの近似 (Latin-1 を区別しない)。
- `LATIN_HALF_*` は `os32_font::limits` の同名の定数の写しで、`libos32gui` の `lib.rs` が const assert で一致を確かめる。

## 境界と折り返し

- `next_boundary(s, at)`: 1 字進んだ境界。末尾を越えない (`at >= len` は `len`)。
- `prev_boundary(s, at)`: 1 字戻った境界。継続バイト (`0x80..=0xBF`) を飛ばして戻る。0 を下回らない。
- `wrap_end(s, cols)`: `cols` 桁に収まる最大の境界。字の途中では切らない。残り 1 桁に全角は入れず、その字ごと次の行へ送る。末尾の不完全な列の手前で止まる。`cols == 0` は 0。
- `wrap_rows(s, cols, out)`: 論理行 1 本を見える行の `[開始, 終了)` の列にし、本数を返す。
  - 各行は桁を超えない。例外は幅より広い 1 字だけの行 (1 字も載らない時は 1 字だけ載せて必ず進む。進まない行は作らない)。
  - 行の列は論理行をちょうど 1 回ずつ覆う (隙間も重なりも無い)。
  - 空の論理行も 1 本 `(0, 0)` を返す。`cols == 0` は 1 桁として扱う。`out` が尽きたらそこで止まる。
- `wrap_count(s, cols)`: `wrap_rows` と同じ規則で本数だけを数える (空は 1)。

## バイト列の編集

- `insert(buf, len, at, seq)`: `buf[..len]` の `at` に `seq` を差し込み、新しい長さを `Some` で返す。`len + seq.len() > buf.len()`・`at > len`・`len > buf.len()` は `None` で、`buf` を変えない。
- `remove(buf, len, at, n)`: `at` から `n` バイト消し、新しい長さを返す。末尾を越える `n` は末尾までに縮める。`at >= len`・`n == 0` は何もしない。
- 境界の判断 (何バイト消すか) は呼び手が `prev_boundary`・`next_boundary` で決める。

## `WK_TEXTBOX` が乗っていること

`widget.rs` の `tb_insert`・`tb_remove`・`prev_boundary`・`next_boundary` は、それぞれ `textcore::insert`・`textcore::remove`・`textcore::prev_boundary`・`textcore::next_boundary` を通す。textbox の箱は `TEXT_CAP` (64 バイト) で、入らない字は入れない。`WK_TEXTBOX` の番号は 4 のまま (種別は末尾追記だけ)。ここが崩れると、下請けの試験が textbox の振る舞いを確かめたことにならない。

## 検査

`test_edit_doc.py (旧版 985925016e7d)` (`make check-edit-doc-host`、`--mutate` 付き)。実物を `#[path]` で取り込み (`textcore_tests.rs (旧版 35e6290159cb)`)、`static_checks()` が上の textbox の配線と番号を静的に見る。`draw.rs` の `decode_glyph` は完全な多バイト列の幅を `cols_at` で決める (末尾で切れた列は幅 0 で描画を止める)。
