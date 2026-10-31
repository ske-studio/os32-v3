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

## 設計の補足

## 3. FDC IRQ の境界・起動・V86
```c
/* drivers/block/fdc.c: 状態と入口をドライバ内へ閉じる。 */
static volatile u32 fdc_irq_fired;
static int fdc_irq(unsigned int irq, void *arg) {
    (void)irq; (void)arg;
    fdc_irq_fired = 1;
    return IRQ_HANDLED;
}
/* fdc_init(): DMA 状態の写し後、motor/reset より前 */
rc = irq_register(FDC_IRQ, fdc_irq, 0, 0);  /* 排他、SHARED にしない */
if (rc != 0 && rc != IRQ_ERR_EXIST) {
    s_init_rc = -1; s_init_st0 = 0; return -1;
}
fdc_irq_fired = 0;                       /* 既存の初期化位置を維持 */
```
- `IRQ_ERR_EXIST` は同一 fn/arg の再初期化だけ。登録成功後の reset/recalibrate 失敗では登録を残す（従来も IRQ11 は開いたまま）。再試行・後続 I/O の寿命を変えない。
- IRQ 内ではフラグだけ更新。SIS 読出し・状態判定・装置待ちは既存の前景処理に残す。2巡でもフラグは1、間に前景は走らない。IRQ_NONE/DEFERRED を返す方式に変えない。
