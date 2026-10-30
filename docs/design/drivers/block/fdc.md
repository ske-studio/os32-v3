# drivers/block/fdc.c — フロッピーディスク制御

> 状態: **現行 (2026-10-31)**

µPD765 の初期化、IRQ 待ち、DMA と媒体の読み書きを担う。

## 落とし穴

- **機械的な待ち時間と IRQ**: 短い時間上限では実機の SEEK や回転待ちが期限切れになる。
  `drivers/block/fdc.h:171` は SEEK 1.5 秒、R/W 1 秒を取り、`drivers/block/fdc.c:319` の SENSE INTERRUPT で完了を回収する。
  再試行と時間切れを `make check-fdc-seek-host`、`make check-fdc-track-host` で検査する。
- **Ready と DMA**: Ready 線のないドライブでは FRY がないと Not Ready になるため、動作時は FRY を立てる (`drivers/block/fdc.c:410`)。
  1MB 超 DMA の許可は DMA 初期化が担当し、FDC は状態を読む (`drivers/block/fdc.c:1147`)。DMA バッファの範囲も検査する (`drivers/block/fdc.c:79`)。
  エミュレータの成功だけでは機構の時間や DMA 制限を実証できない。
