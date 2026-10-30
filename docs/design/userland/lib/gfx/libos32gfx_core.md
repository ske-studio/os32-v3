# userland/lib/gfx/libos32gfx_core.c — ユーザー描画面の接続

> 状態: **現行 (2026-10-31)**

描画面を query / lease して描画ライブラリに接続する。

## 落とし穴

- **描画形式の初期化漏れ**: attach で PACKED8 を設定しないと、漢字描画がプレーン形式に落ちて表示されない。
  `userland/lib/gfx/libos32gfx_core.c:103` は CLIENT 面を query / lease し、取得した形式から `gfx_packed` を設定する (`:137`)。
  init と attach の両方で同じ接続処理を使い、世代変更や失敗時の lease 寿命も守る。
  `tools/tests/gfx_attach_host.c:159` が接続・再接続と失敗を検査する。
