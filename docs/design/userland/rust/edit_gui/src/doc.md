# userland/rust/edit_gui/src/doc.rs — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_EDIT_GUI (§2・§3、受入 E7・E8) の契約を今のソースと照らして要約
ソース: `userland/rust/edit_gui/src/doc.rs`

GUI 版エディタの本文。GUI にも描画の KAPI にも依存しない状態機械で、ファイルの読み書きだけが `sys_open`・`sys_read`・`sys_write`・`sys_close` を通る。

## 切り分け

- 本文はこのモジュール (`Doc`) が持つ。`WK_TEXTAREA` は本文を持たない。
- 折り返しと縦位置もここが決め、部品へは「いま見えている行」だけを渡す (`fill_view`)。本文の大きさに依らず部品の側の使用量は一定。
- 桁の数え方と折り返しの切れ目は `libos32gui::textcore` を通す (写しを持たない)。
- 行の配列と、挿入・削除・改行・行の結合だけを持つ。取り消し・検索は持たない。CUI 版エディタの中身は使わない。

## 大きさ

| 定数 | 値 | 意味 |
|---|---|---|
| `MAX_LINES` | 160 | 論理行の本数の上限 |
| `LINE_CAP` | 192 | 論理行 1 本のバイト数 (`GUI_TEXTAREA_ROW_CAP` と同じ。折り返した 1 本が部品の 1 行に必ず収まる) |
| `MAX_VIEW_ROWS` | 32 | `fill_view` が返す見える行の上限 (`GUI_MAX_TEXTAREA_ROWS` と同じ) |

- `Doc::NEW` は全ビット 0 (`.bss` に置くため。1 か所でも非 0 にすると表がまるごと `.data` に移り、`.bin` が約 31KB 太る)。使う前に `reset()` を呼ぶ。`reset` の後は空の 1 行。

## 読み込み (`load`)

- `\n` で行を割り、`\r` は捨てる (`\r\n` と `\n` の両方を区切りにする)。
- 末尾の改行は最後の行の終端で、空行を 1 本足さない (`"a\n"` は 1 行、`"a\n\n"` は 2 行)。開いてそのまま保存しても大きさが変わらない。
- 入り切らない分は捨てて `truncated` にビットを立てる (`TRUNC_LINES`: 行数が `MAX_LINES` を超えた、`TRUNC_COLS`: 1 行が `LINE_CAP` を超えた。その行の残りは次の改行まで読み飛ばす)。字の途中では切らない。呼び手はこれを見て利用者に言う。
- 読み込みの後は `dirty == false`。

## 編集

キャレットは (`cy`, `cx`)。`cx` は行頭からのバイト数で、必ず UTF-8 の境界。編集が起きたら `dirty = true`。

- `insert(seq)`: キャレットへ字を入れ、`cx` を進める。`seq` の中の `\n` は `newline` として扱う。行がいっぱいになったらそこで止め、1 字でも入れば `true`。空の `seq` と末尾の不完全な列は入れない。
- `newline()`: キャレットで行を割り、後ろを次の行へ。キャレットは次の行の頭。行数が `MAX_LINES` なら `false`。
- `backspace()`: 手前の 1 字 (`prev_boundary` まで) を消す。行頭なら前の行と繋ぐ。文書の頭なら `false`。
- `delete()`: キャレットの 1 字 (`next_boundary` まで) を消す。行末なら次の行と繋ぐ。最終行の末尾なら `false`。
- 行の結合: キャレットは継ぎ目へ。繋いだ長さが `LINE_CAP` を超えるなら繋がずに `false` (黙って切らない)。
- `move_*`: 移動の後もキャレットを行の長さと UTF-8 の境界に収める。

## 見える行

- `wrap_count(i, cols)`: 論理行 `i` の見える行の本数 (空行も 1)。`vrow_total(cols)` はその和。
- `vrow_at(cols, index)`: 通し番号の見える行 (`VRow { line, start, end }`)。範囲外は `None`。
- `fill_view(cols, top, out)`: 通し番号 `top` から最大 `out.len()` 本を切り出し、本数を返す。論理行の途中から始められ、論理行をまたぐ時は次の行の頭から取る (飛ばしも重なりも無い)。`top` が範囲外なら 0。`cols == 0` は 1 桁。
- `caret_vrow(cols)`: キャレットの乗る見える行の通し番号。
- `scroll_to_caret(cols, height, top)`: キャレットが `[top, top + height)` に入るように寄せた `top` を返す (上に外れたらキャレットの行、下に外れたら最終行がキャレットの行)。

## ファイル

- `load_file(doc, path, scratch)`: `path` は NUL 終端。読んだバイト数か、`sys_open`・`sys_read` の負の値をそのまま返す。開けなかった時と空ファイル (0) を混ぜない。`scratch` に入る分までを読む。
- `save_file(doc, path)`: `O_WRONLY | O_CREAT | O_TRUNC` で開き、各行の本文と `\n` を書く (空行は `\n` だけ)。書いたバイト数を返す。書けなかったのに成功と答えない:
  - open の失敗 → その負の値
  - write の負 → その負の値
  - 行の短い write・改行の短い write → `ERR_IO` (-1)

## 検査

`tools/tests/test_edit_doc.py` (`make check-edit-doc-host`、`--mutate` 付き)。実物を `#[path]` で取り込み (`userland/rust/edit_gui/host/doc_tests.rs`)、`sys_*` を贋物に替えて保存の失敗を踏む。行の短い write と改行の短い write は別の贋物で確かめる (1 つの贋物で両方を壊すと片方の判定が他方を隠す)。描画・WM 経由の入力の配線・Host Services・設定レジストリはゲストの受入の範囲。
