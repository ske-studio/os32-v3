# drivers/block/fdc_sector_transfer.inc — 単発セクタ転送

> 状態: **現行 (2026-11-01)**

`fdc_transfer_sector_geom` は単発 READ/WRITE のシーク、DMA 設定、
[コマンド送出](fdc_command.md)、IRQ 待ち、リザルト回収、再試行を共通化する。
公開入口は `fdc_transfer.inc` に残し、read の出力と write の const 入力を別々に渡す。
トラック内まとめ読みはこの骨格を使わない。

- read だけが `single_reads` と `single_retries` を数え、成功時に出力へコピーする。
- write は世代更新、`writes` の計数、DMA への事前コピーをこの順で 1 回だけ行う。
  失敗しても世代を戻さない。
- 送信途中の失敗・IRQ 待ち・短いリザルトは従来どおり再試行する。
  次の試行の前に回復し、DMA とリザルトの診断状態を消す。
- SEEK の NR は DMA 設定前に打ち切る。リザルトの NR は DMA を閉じ、回復前に打ち切る。
- write の NW は NR より先に見て DMA を閉じ、`FDC_RC_WRITE_PROTECTED` を返す。
- 最終失敗時は開いた DMA を中止し、既知シリンダを捨て、最後の試行を 1 行で報告する。

`test_fdc_track.py` は方向別の計数・コピー・NR/NW・回復を実ソースで検査する。
共通 NR 分岐の変異は一方の方向だけを無効化し、両方向を個別に実行時 RED にする。
