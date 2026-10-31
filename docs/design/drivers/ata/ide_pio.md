# drivers/ata/ide_pio.inc — 1 コマンドの PIO 転送

> 状態: **現行 (2026-10-31)**

`ide.c` の内部実装。1〜256 セクタを受け、選択 → BSY 待機 →
必要なら MULTIPLE 再設定 → 開始番地/count 設定 → データコマンド1回 → 各 DRQ 相 → 終了確認と進む。
256 はレジスタ書込み時だけ 0 に変換する。active>1 なら READ/WRITE MULTIPLE (C4h/C5h)、それ以外は従来の20h/30h。
各相は k=min(active,残セクタ)、未設定なら k=1。256k語を
`inpw` / `outpw` で転送し、相内のセクタ間に status 読取り・settle を挟まない。
コマンド途中に協調切替は行わない。

コマンド発行後と各相後は代替ステータスを 4 回読み、400ns 待つ。
その後の上限付き待機では BSY 中の他ビットを無視し、BSY が落ちてから
ERR/DF と DRQ を評価する。終了は BSY=0、DRQ=0、ERR=DF=0 が条件。
終了確認は通常 Status を読み IRQ を acknowledge する。

READ の ERR+DRQ は端数を含む相の全256k語を読み捨て、呼出し元へ写さず失敗とする。
WRITE は ERR/DF の相へデータを送らない。最初の失敗をそのまま返す。
MULTIPLE 対応ドライブの発行前 DRQ 残留は `ide_multiple` が拒む。
未対応ドライブの発行手順と、データ失敗時の再試行なしの契約を維持する。

保持するのは呼出し中のポインタ・相数・状態だけ。
根拠と検証入口は `ide`、[ポート模型](../../tools/tests/ide_pio_host.md)。

## 落とし穴

単に旧 `ide_wait_drq` を反復すると、古い DRQ や BSY 中のビットを拾い得る。
IDENTIFY とデータコマンドの待機は分ける。BSY が落ちただけでは成功にしない。
