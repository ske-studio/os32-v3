# userland/rust/libos32gui/src/window.rs — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `userland/rust/libos32gui/src/window.rs`

## 所有型

- クライアント側 (Rust) は所有型 (`Drop` で destroy)。

---

## U. ウィンドウとイベント (USER 相当)

### U1. ウィンドウ

```
create_window(spec) -> WindowId      spec = {title: [u8; 40], rect, flags, min_size}
destroy_window(id)                   move_window(id, xy)     resize_window(id, wh)
show_window(id, bool)                set_title(id, utf8)     client_rect(id) -> Rect
raise(id)                            set_focus(id)
```

## テキストカーソル

`set_text_cursor(window, x, y)` で知らせる (text-input の cursor rectangle 相当)。
