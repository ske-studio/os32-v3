# userland/gshell/src/handler.rs — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS・DESIGN (D1 で移した)
ソース: `userland/gshell/src/handler.rs`

- **損傷の 3 状態** (2026-09-05 再改訂): WM は窓ごとに (a) **dirty** = `invalidate` 済みで
  `Paint` 未発行、(b) **issued** = `Paint` を発行済み (この `OP_POLL` でクライアントへ渡した
  矩形の集合)、(c) 画面と一致、を持つ。`OP_POLL` で dirty ∩ 可視領域のうち入った分が issued に
  移る (可視矩形 16 超や空き不足で入らなかった分は dirty のまま)。処理中の `invalidate` は
  dirty に足すだけ (次の周)。**可視領域の外にある dirty は「配送できる Paint」ではない**:
  完全に隠れた窓の dirty はそのまま眠り、露出 (可視領域の再計算) で初めて配送対象になる。

- `commit(window)`: **issued の矩形だけ**を `present_rect` で画面へ出し、issued を空にする。
  dirty は触らない。したがって「まだ描いていない領域」を転送することは無い。**ループ 1 周に
  1 回** (U3)、その周の `Paint` を全部処理してから呼ぶ。`Paint` を受けたのに描かなかった
  領域はクライアントの責任 (前の内容が転送される)。

- `gui_call(OP_WAIT, timeout_ticks)`: イベントもタイマも無ければカーネル内で `sys_halt` で
  待つ (v2 M0 のアイドル作法)。GetMessage の代替。

- **2 種類のイベント**:
  - **待ち行列型** (発生時にリングへ入る): `Key` `Text` `Button` `Pointer` `Focus` `Close`
    `Modal` `Quit` `Palette`、およびクライアント側で合成する `Widget`。
  - **導出型** (WM の状態から作る): `Paint` (損傷 ∩ 可視領域)、`Configure` (最新の矩形 1 件)、
    `Timer` (期限を過ぎた ID)。**`OP_POLL` のときに WM が同じリングへ追記する** (WM は
    `tail` の書き手なのでそのまま書ける)。空きが足りなければ入る分だけ書き、残りは状態の
    まま次の `OP_POLL` に持ち越す (状態なので失われない)。アプリから見て待ち行列型と
    導出型の区別は無い (全部リングから読む)。

- **`OP_WAIT` が戻る条件**: リングに未読がある、**配送できる導出型がある** (dirty ∩ 可視領域が
  空でない / Configure 未通知 / 期限切れタイマ) のいずれか、または期限。「`Paint` 待ちだけが
  残る」状態で眠らず、**完全に隠れた窓の dirty だけでは起きない** (2026-09-05 3 回目)。

| 文脈 | いつ | 許される操作 | 禁止 |
|---|---|---|---|
| **X1 ハンドラ** | `gui_call` (WAIT / COMMIT 以外) | WM 状態の更新、リングへの追記、可視領域の再計算 | VRAM 書き込み、`sys_halt`、KAPI 呼び出し |
| **X2 COMMIT** | `gui_call(OP_COMMIT)` | そのアプリの損傷を `present_rect` | 待つこと、他アプリの present |

- [COMMIT 矩形のカーソル退避・再描画](../../../../../userland/gshell/src/cursor.rs)

| 文脈 | いつ | 許される操作 | 禁止 |
|---|---|---|---|
| **X3 WAIT** | `gui_call(OP_WAIT)` の中 | アプリ宛のイベントか期限が来るまで繰り返す | アプリのクライアント面に描くこと |

- [WM の全周期 (wm.rs)](wm.md#wm-の全周期)

OS 側が矩形にクリップして再描画要求 (WM_PAINT 相当) を投げ、`gfx_present_rect` で送る。全画面 present はデスクトップ切替など例外にだけ許す
