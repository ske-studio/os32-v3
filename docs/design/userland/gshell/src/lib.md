# userland/gshell/src/lib.rs — 設計 (アプリ起動後の画面の扱い)

> 状態: **現行 (2026-11-18)** — 出所: TASK_K5B_gshell (不具合 W-1) の契約を今のソースと照らして要約
ソース: `userland/gshell/src/lib.rs` (`run_program`・`restore_screen`)

## 起動の間の印

`run_program` は `exec_start` を `multiapp::begin_start()` と `multiapp::end_start(rc)` で挟む。`exec_start` はアプリが最初に park するまで戻らず、その間に走っている ID は表に無いので、この印で譲り合いの表に迎える。

## `exec_start` の後に画を消さない

`gfx_init` は VRAM の両ページをゼロクリアする (`gfx/gfx_core.c`)。WM はクライアント面を持たないので、消した画は本人に `Paint` を出して描き直させるしか戻らない。遮蔽は露出を生まない (`visible::recompute_and_expose` の dirty は新しく見えた分だけ) ので、消したままにすると park 中のアプリは導出群の ready にならず、誰にも起こされず黒のまま残る。

| `exec_start` の戻り | 画面 |
|---|---|
| 全画面に入った (`fullscreen::observe` が `Fullscreen`) | 画面はプログラムのもの。WM は何も描かない |
| 所有者が WM に戻った (`Restored`) | `restore_screen` |
| `rc <= 0` (起動しなかった・park より前に終わった) | `restore_screen` |
| `rc > 0` (最初の `OP_WAIT` まで進んで park した GUI アプリ) | `gfx_init` を呼ばない。パレットを戻して全面を合成するだけ (`repaint_full`) |

`restore_screen` は `gfx_init` → 画面情報の読み直し → `damage::invalidate_all_clients` (生きている全ウィンドウを全面 dirty) → `repaint_full`。消した以上は生き残っている全アプリに全面を描き直させ、park 中のアプリも導出群に入って top-level の `pick` が起こす。

## 検査

`make check-gshell-host` (`userland/gshell/host/wm_tests.rs` の `a_parked_app_is_not_left_black_when_another_app_is_launched`・`a_running_app_yields_to_a_parked_app_that_has_a_deliverable_paint`)。ホストの `gfx_init` の贋物は VRAM を消したことと回数 (`mocks::GFX_INITS`) を記録する。
