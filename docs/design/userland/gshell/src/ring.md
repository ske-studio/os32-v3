# userland/gshell/src/ring.rs — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `userland/gshell/src/ring.rs`

### T3. イベントの配送とリング — 2026-09-05 改訂 (満杯の規則と head/tail)

- WM はイベントをリングへ**追記するだけ**。アプリは `gui_call(OP_POLL)` 1 回で溜まった分を
  全部受け取る (戻り値 = 件数)。**1 イベント 1 syscall にしない** (int 0x80 は 6µs 級)。
- **head/tail の担当**: `tail` は WM だけが書く (生産者)、`head` はアプリのライブラリだけが
  書く (消費者。読み終えた分だけ進める)。書き手が 1 つずつなのでロックは不要 (PD 切替は
  [T2a の park 条件](../../../../../exec/exec.c) に従う)。`OP_POLL` の戻り値は `tail - head`。

- **合成**: 連続する `Pointer` (移動) は最新 1 件に畳む。

| 項目 | 値 |
|---|---|
| イベントリング | 128 件。`Pointer` は最新 1 件に合成 |
