# drivers/ata/ide_pio.inc — 1 コマンドの PIO 転送

> 状態: **現行 (2026-10-31)**

`ide.c` の内部実装。1〜256 セクタを受け、選択 → BSY 待機 →
開始番地/count 設定 → READ/WRITE SECTOR(S) 1 回 → 各 DRQ 相 → 終了確認と進む。
256 はレジスタ書込み時だけ 0 に変換する。各相は既存の 256 語の
`inpw` / `outpw` ループであり、コマンド途中に協調切替は行わない。

コマンド発行後と各相後は代替ステータスを 4 回読み、400ns 待つ。
その後の上限付き待機では BSY 中の他ビットを無視し、BSY が落ちてから
ERR/DF と DRQ を評価する。終了は BSY=0、DRQ=0、ERR=DF=0 が条件。
終了確認は通常 Status を読み IRQ を acknowledge する。

READ の ERR+DRQ は 512B を読み捨てて失敗とする。
WRITE は ERR/DF の相へデータを送らない。最初の失敗をそのまま返す。
発行前の DRQ=0 確認やリセット・再試行は追加しない。

保持するのは呼出し中のポインタ・相数・状態だけ。
根拠と検証入口は [ide](ide.md)、[ポート模型](../../tools/tests/ide_pio_host.md)。

## 落とし穴

単に旧 `ide_wait_drq` を反復すると、古い DRQ や BSY 中のビットを拾い得る。
IDENTIFY とデータコマンドの待機は分ける。BSY が落ちただけでは成功にしない。
