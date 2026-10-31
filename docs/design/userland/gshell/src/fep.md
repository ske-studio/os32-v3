# userland/gshell/src/fep.rs — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `userland/gshell/src/fep.rs`

- **文字入力は `Text` として別に届ける** (Wayland の wl_keyboard と text-input の分離):
  FEP オフなら印字可能文字 1 つ、FEP オンなら確定文字列。16B のリング項目に収めるため
  8B ずつ UTF-8 境界で区切り、続きがあれば `more=1` (U2 の配置)。

- **FEP の優先** (レビュー指摘: 変換中の BS が未確定文字と本文の両方を消す): WM は入力を
  まず FEP に通す。FEP がオンで**未確定文字列があるとき**、FEP が消費したキー (かな入力、
  BS / DEL による未確定の編集、SPACE / 矢印 / 数字 / ROLL の候補操作、RETURN の確定、
  ESC の取消) は **`Key` としては配送しない**。確定結果だけが `Text` で届く。未確定文字列が
  空のときは BS / 矢印などは通常どおり `Key` で届く。SHIFT+SPACE (FEP 切替) は常に WM が
  消費し、配送しない。これでアプリと FEP の二重処理は起きない。

- **FEP は WM が持つ**: オン/オフ (SHIFT+SPACE) は WM が処理し、未確定文字列と候補窓は
  WM が既存 `ime_render` の関数表に GFX バックエンドを足して描く。

辞書は
  カーネル常駐 (SQLite) のまま。

- [set_text_cursor (window.rs)](../../rust/libos32gui/src/window.md#テキストカーソル)
