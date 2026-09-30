# TASK_EDIT_GUI — 本文と桁・折り返し (ホスト試験の記録)

- 票: [`docs/archive/gui_v14/TASK_EDIT_GUI.md`](../../docs/archive/gui_v14/TASK_EDIT_GUI.md)
  — 受入 **E8** (本文の操作をゲスト抜きで確かめる) と **E10**
  (既存の `WK_TEXTBOX` の見え方と振る舞いが変わっていない)
- 実行: `python3 -B tools/tests/test_edit_doc.py [--mutate]`
  (`make check-edit-doc-host` が `--mutate` 付きで回す)
- 対象: `userland/rust/libos32gui/src/textcore.rs` (実物を `#[path]` で取り込み) /
  `userland/rust/edit_gui/src/doc.rs` (同上) /
  `userland/rust/libos32gui/src/widget.rs` (静的な突き合わせだけ)

## 0. 正直に書く ([V4])

- この記録は**実装と同じ回に書いた** (2026-09-17)。§2 の RED は、試験を先に
  書いて踏んだものと、実装の後に `--mutate` で人工的に作ったものが混ざっている。
  どちらかを §2 の表の「種類」欄に書き分けた。
- **ゲスト (NP21/W) では 1 回も動かしていない。** 受入 E1〜E7 と E9 は未実施。
  ここが見ているのは E8 と E10 だけ。描画・入力の配線・モーダル・Host Services・
  設定レジストリの実経路は**確かめていない**。

## 1. 何を確かめる試験か

票 §2 の切り分けはこうなっている:

- **本文はアプリ (`doc::Doc`) が持つ。** 部品 (`WK_TEXTAREA`) は持たない。
- アプリが折り返して、**いま見えている行だけ**を部品へ写す。
- 桁の数え方と折り返しの切れ目は `libos32gui::textcore` の 1 本に寄せてある。
  `WK_TEXTBOX` のバイト操作も同じ下請けに乗った (決裁 A1 の「共通の下請け」)。

この形だと、**GUI を一切動かさずに壊れ方の大半を踏める**。踏めるのは:

| 見るもの | なぜホストで足りるか |
|---|---|
| 挿入・削除・改行・行の結合 | `Doc` は KAPI にも描画にも依存しない純粋な状態機械 |
| 折り返しの桁 (日本語 3B 2 桁) | `textcore` は core だけに依存する |
| 「見える範囲」の切り出し | `fill_view` は `Doc` の中で閉じている |
| 保存の失敗の扱い | `sys_open/write/close` を差し替えれば踏める |
| `WK_TEXTBOX` の振る舞い | 下請けに乗ったので、下請けを壊せば textbox も壊れる |

踏めないのは描画そのもの (`draw::text` / `measure_text` が KAPI を叩く)、WM 経由の
イベント配送、モーダル、Host Services、設定レジストリの実 DB。**そこはゲストの仕事**。

## 2. RED → GREEN

| # | 種類 | 症状 (RED) | 直し方 (GREEN) |
|---|---|---|---|
| 1 | 試験が先 | `wrap_rows` が「幅より広い 1 文字」で桁を超える行を返し、試験の「1 本が桁を超えない」が落ちた | **実装は正しく、試験の言い方が粗かった**。1 文字も載せずに進まない行を作ると無限ループになる。超えてよいのは「その 1 文字だけ」の行に限る、と書き直した (`textcore_tests.rs` の `wrap_rows_covers_the_line_exactly_once`) |
| 2 | 試験が先 | `Doc::NEW` に `nlines: 1` があるため 30KB の表がまるごと `.data` へ乗り、`edit_gui.bin` が 61,580B になった | `Doc::NEW` を**全ビット 0** にして `.bss` へ落とし、使う前に `reset()` を呼ぶ。61,580B → 30,556B (−31KB) |
| 3 | 変異 | `wrap_end` が 1 バイトずつ進む版 (UTF-8 の途中で折り返す。§4-27 の再発) | — (否定側の確認。`wrap_splits_utf8`) |
| 4 | 変異 | 全角を 1 桁として数える版 (折り返しが 1 桁はみ出る) | — (`wide_char_counted_as_one`) |
| 5 | 変異 | `fill_view` が論理行の先頭の 1 本を飛ばす版 | — (`view_skips_a_row`) |
| 6 | 変異 | `fill_view` が論理行をまたぐときに行内位置を戻さない版 (行が重なる) | — (`view_rows_overlap`) |
| 7 | 変異 | `textcore::insert` が入る大きさを見ない版 = **`WK_TEXTBOX` が 64B の箱の外へ書く** (E10) | — (`textbox_overflows`) |
| 8 | 変異 | `textcore::prev_boundary` が 1 バイトだけ戻る版 = **textbox の BS が壊れた文字を残す** (E10) | — (`textbox_boundary_is_one_byte`) |
| 9 | 変異 | 保存で**行の**短い write を成功と答える版 | — (`short_write_reported_as_ok`) |
| 10 | 変異 | 保存で**改行の**短い write を成功と答える版 | — (`short_newline_write_reported_as_ok`) |
| 11 | 変異 | `save_file` が open の失敗を 0 (成功) にする版 | — (`open_failure_swallowed`) |

### 変異 9 は一度 GREEN のままだった

最初の贋 `sys_write` は「どの write も 1 バイト少なく返す」だった。この贋物だと
**行の判定を外しても、直後の改行の判定が代わりに拾ってしまう** ので、変異 9 が
GREEN のまま通った。行の短い write (`WRITE_MODE=1`、`size > 1` のときだけ短く) と
改行の短い write (`WRITE_MODE=3`、`size == 1` のときだけ 0) を**別の贋物に分け**、
試験も 2 本にして初めて RED になった。

> 教訓: 同じ失敗を 2 か所で判定しているとき、**両方を一度に壊す贋物では
> 片方が片方を隠す**。判定の数だけ贋物を分ける。

## 3. E10 (`WK_TEXTBOX` が変わっていないこと) の見方

実物の `key_textbox` / `tb_insert` / `tb_remove` は `crate::draw` (KAPI) を通るので
ホストで組めない。だから 2 段で見る:

1. **静的**: `tools/tests/test_edit_doc.py` の `static_checks()` が、実物の
   `widget.rs` の `tb_insert` / `tb_remove` / `prev_boundary` / `next_boundary` が
   `textcore::*` を通っていることと、`WK_TEXTBOX` の番号が **4 のまま**である
   ことを見る。ここが切れていると変異が textbox に届かず、E10 を見たことに
   ならない。
2. **挙動**: `textcore_tests.rs` の `TbModel` が textbox と**同じ手順**を踏む
   (64B の箱・キャレット・BS / DEL / 矢印)。変異 7・8 がこれを RED にする。

`WK_TEXTAREA` は種別 **8** を末尾に足しただけで、`WK_TEXTBOX` (4) の描画・入力・
`send_text_cursor` の経路には手を入れていない (追記したのは
`k == WK_TEXTBOX || k == WK_TEXTAREA` の or 1 つずつ)。

## 4. この試験が見ていないこと ([V4])

- **描画**。`draw_textarea` は 1 度も実行していない。桁の数え方が
  `draw.rs::decode_glyph` と一致していることは**目で突き合わせただけ**で、
  試験では固定していない (`textcore::cols_of` のコメントに規則を書いてある)。
- **`textarea_take_input` の配線**。確定文字列が WM →
  `widget::on_text` → 溜め → `App::on_text_changed` → `doc.insert` と流れることは
  ゲストでしか確かめられない。
- **設定レジストリ / Host Services / モーダル / VFS の実経路**。
- **`GUI_MAX_TEXTAREA_ROWS` を超える要求**。`textarea_add_row` が `FULL` を返す
  経路はホストから叩けない (widget の状態が shlib 側)。
