# init/bootlog.c — 起動ログの収集と保存

> 状態: **現行 (2026-10-31)**

起動中の文字出力を保持し、ヘッダと末尾を付ける。
ext2 または FAT に保存し、前回の世代を残す。

## 落とし穴

- **保存途中のログ**: 容量を超えると後ろが捨てられ、`dropped` が増える (`init/bootlog.c`, `init/bootlog.c`)。
  前の一時ファイルを消し、今回分の完全な書込みを確認してから旧ログを回し、新しいログを公開する (`init/bootlog.c` の `bootlog_save_with()`, `init/bootlog.c` の `bootlog_save_with()`, `init/bootlog.c` の `bootlog_save_with()`, `init/bootlog.c` の `bootlog_save_with()`)。
  旧ログが無い場合は旧世代を残す。`boot.new` の残存だけでは完全な書込みを保証しない。
  ヘッダの Commit・Image CRC と `ver` を照合する。回帰は `tools/tests/test_bootlog.py`。
