# userland/rust/libos32gui/src/app.rs — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `userland/rust/libos32gui/src/app.rs`

<a id="trim-contract"></a>

## TRIM と allocator の再試行

`EV_TRIM=14` (window 0、payload 先頭4B は LE epoch) は SDK 内で消費し、
型付き handler と `on_raw` には渡さない。複数件を同じ poll バッチで受けても
hook / DONE は各1回、最後の epoch を使う。

安全点は `commit`・`after_commit` の後、wait の前。
任意 hook → `gui_call(GUI_OP_TRIM_DONE=81, epoch)` を行い、quit 周も送信後に戻る。

`hook_calls / hook_pages`

- 長い処理をするアプリは `OP_POLL` を定期的に呼ぶか、仕事をタイマで分割する (U5)。

(GetMessage の代替、入れ子なし)

```
loop {
    n = gui_call(OP_POLL)            // リングから一括で取り出す
    for ev in events[..n] { handle(ev) }   // 状態更新 + invalidate だけ。ここで描かない
    paint_damaged()                  // Paint を受けたウィンドウを描く
    commit_all()                     // 1 周 1 回
    gui_call(OP_WAIT, next_timer)    // 何も無ければ sys_halt で待つ
}
```

- ハンドラの中から `OP_WAIT` を呼ばない (再入禁止はライブラリが `debug_assert`)。
- 長い処理は「タイマで分割」か「途中で `OP_POLL` を呼んで `Quit`/`Close` に応じる」。

## commit

ループ 1 周 1 回。

| 項目 | 値 |
|---|---|
| syscall | プリミティブごとには呼ばない。1 周あたり `OP_POLL` + `commit` + `OP_WAIT` の 3 回が基本 |
