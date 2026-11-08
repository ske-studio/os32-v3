# userland/rust/libos32gui/src/shlib.rs — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `userland/rust/libos32gui/src/shlib.rs`

shlib protocol は2、SHM の `GUI_PROTO_VERSION` は2。ジャンプ表は124本。
既存の0..=121の順序・意味を保ち、末尾122に `os32gui_scroll`、123に
`os32gui_w_textarea_scroll` を追加した。旧 protocol 1 は両方向で拒否する。
末尾追記なので版は据え置き、新しいアプリは `nfunc` の照合で古い表を拒否する。

`os32gui_scroll(window, Rect, dx, dy)` はDIRECTでU2を呼び、SHADOWでは
矩形をINVALIDATEしてNOSYSを返す。呼び手は従来の全体再描画へ戻る。
`textarea_scroll(id, rows, lines, caret_row, caret_col)` は更新後の表示行をコピーし、
正のrowsで先へ進む。ABIは `(u32, i32, *const *const u8, *const u32, u32, i32, i32)`。
配列は行のポインタ・長さ・本数で、呼出し中だけ借りる。保持される行が一致する
DIRECTの移動だけ複写し、露出・キャレット・フォーカス枠の損傷を積む。
SHADOW・本文変更・複写失敗では従来と同じ全体再描画へ戻る。
既存のclear/add_row/set_caretの振る舞いは維持する。

複数行は**新しい種別**を末尾に足した。ジャンプ表も 111..=117 への末尾追記だけで、
`GUI_PROTO_VERSION` は据え置き (古いアプリは新しい表でもそのまま動き、新しいアプリが
古い表に当たれば `nfunc` の照合で `bind()` が断る)。

## 互換上の注意と改訂記録

`os32gui_w_focused` (entry 77) は窓スロット添字を取る既存 ABI を維持する。
アプリは WindowId を取る `os32gui_w_focused_in` を使う。定義は
[`shlib.rs:1171・1183–1187`](../../../../../../userland/rust/libos32gui/src/shlib.rs)。
