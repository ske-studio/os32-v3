# fs/ext2/ext2_super.c — ext2 スーパーブロックと I/O

> 状態: **現行 (2026-10-31)**

ext2 の媒体アクセスとマウント状態を管理する。
メタデータの I/O 失敗時にはファイルシステムをエラー状態にする。

ブロック／セクタの生 I/O は [ext2_super_block_io](ext2_super_block_io.md)
を include し、範囲検査と Device 発行を集約する。
`ext2_priv.h` が範囲読み書きの内部宣言と最大 32 ブロック定数を持つ。
このファイルの追加常駐領域・ヒープは 0B、既存共有 3×1KiB バッファは増やさない。
[共通失効](ext2_priv.md) を fs_error 入口、mount の早期失敗より前、
unmount の sync 後かつ ctx 解放前に呼ぶ。sync の失敗でも失効する。

## 落とし穴

- **メタデータ I/O の失敗**: 読めなかったものを不存在や空きと扱うと、不正な上書きや交差リンクにつながる。
  エラーを伝播し、メタデータ障害後は `ext2_check_writable` で書込みを拒否する。
  `ext2_fs_error` のエラー記録用バッファは操作中の共有 scratch と分ける。
  実際の open を通す失敗経路と媒体整合の回帰は `tools/tests/test_b8_open.py`。
