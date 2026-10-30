# drivers/input/kbd_dlog.c — キーボード診断リング

> 状態: **現行 (2026-10-31)**

受信バイトと処理後の修飾状態を通し番号付きで保持する。

## 落とし穴

- **上書きされた区間は判定できない**: 受信順は最後のコードだけでは分からず、遅い読み手は循環リングの古い項目を失う。
  `drivers/input/kbd_dlog.c:34` は seq を振り、`drivers/input/kbd_dlog.c:45` は残っている範囲を返す。
  `kbdstat -w` の seq の飛びと LOST 表示を確認し、欠落区間のキー動作を推定しない (`userland/shell/kbd_watch.c:146`)。
  `make check-kbd-dlog-host` が欠落表示を検査する。
