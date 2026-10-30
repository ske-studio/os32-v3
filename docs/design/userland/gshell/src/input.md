# userland/gshell/src/input.rs — ウィンドウ入力配送

> 状態: **現行 (2026-10-31)**

マウスとキーを WM およびアプリへ配送する。

## 落とし穴

- **ボタンエッジの所有**: syscall 境界のポンプが先に `prev_buttons` を進めると、WM がドラッグ終了や閉じる操作を見逃す。
  `userland/gshell/src/input.rs:690` は WM が扱うエッジを据え置き、次の WM 処理へ渡す。
  `wm_owns_edge` (`:796`) の判定順を WM 側と揃え、ドラッグ・背面窓・閉じる操作とクライアントへの配送を確認する。
