# userland/rust/libos32gui/src/widget.rs — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS・DESIGN (D1 で移した)
ソース: `userland/rust/libos32gui/src/widget.rs`

### U6. ウィジェット木 (クライアント側、保持型)

- ウィジェットは**共有ライブラリ内 (アプリの空間)** に置き、WM はウィンドウしか知らない。
  描画は in-process、syscall はゼロ。
- `WidgetId` (generation 付き)。種別: v1 は既存の button / label / checkbox / textbox /
  listbox + コンテナ `row` / `column` (U7)。**v1.4 で `textarea` を末尾に追記** (下記)。
- **プロパティ変更 → 自分の矩形を invalidate**。全面再描画はしない。
- ウィジェットのイベント (`Widget{kind: CLICK / TEXT_CHANGED / TOGGLED / SELECT / FOCUS}`) は
  `Pointer` / `Button` / `Key` からライブラリが合成し、同じリングの流儀でアプリに渡す
  (アプリから見て WM 由来か合成かは区別しない)。
- ハンドラは種別ごと (`on_click(widget, fn)`)。巨大 switch は書かせない。
- 固定配列: ウィジェット 64 / ウィンドウ合計

- [リスト項目プール (uistate.rs)](../../../../../../userland/rust/libos32gui/src/uistate.rs)

#### U6a. `textarea` — 複数行の編集面 (末尾追記、2026-09-17 / 票 TASK_EDIT_GUI 決裁 A1)

`textbox` は 1 行のまま**触らない** (既存の利用者 = filer ほかに影響を出さないため)。

- **部品は本文を持たない。** アプリが本文を持ち、`textarea_clear` +
  `textarea_add_row` で**いま見えている行だけ**を写す。折り返し・縦スクロール・
  選択範囲はアプリの領分。だから 1 画面に入らないファイルでも部品の側の
  使用量は一定 (`GUI_MAX_TEXTAREA_ROWS` × `GUI_TEXTAREA_ROW_CAP`)。
- 折り返しの幅と入る行数はアプリが `textarea_columns` / `textarea_visible_rows`
  で**部品から聞く** ([C4]: 桁を決め打ちしない)。全角は 2 桁。
- 確定文字列 (`Text`) は部品が**溜めるだけ**で、アプリが
  `textarea_take_input` で引き取る。`TEXT_CHANGED` がその合図。
  本文を持たない部品が `Text` を受ける唯一の筋道 (U2a の分離を壊さない)。

| 採用 | 解く欠点 | 386 でのコスト |
|---|---|---|
| **保持型ウィジェット木 + 世代付きハンドル + 明示的無効化** (Qt/GTK 系) | ダングリング、描き過ぎ、巨大 switch | 固定配列で O(変更分) |
