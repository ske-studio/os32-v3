# drivers/sound/cs4231/pcm_cs4231.c — CS4231 PCM ドライバ

> 状態: **現行 (2026-10-31)**

PCM の DMA、割込み、再生と停止の状態を制御する。

## 落とし穴

- **ISR と foreground の状態観測**: 停止待ちで通常の読みを使うと、最適化で読みがループ外へ移り、ISR が完了しても期限切れになる。
  `drivers/sound/cs4231/pcm_cs4231.c:667` は ISR が更新する状態を volatile 経由で繰り返し読む。
  別の I/O 命令による偶然の再読込みに頼らず、この観測契約を保つ。`tools/tests/pcm_cs4231_host.c:1145` は停止完了の状態遷移を検査する。

## 設計の補足

- PCM_IRQの値を知るのはdrivers/sound/cs4231/pcm_cs4231側だけ。既存 imports の pcm_module_irq_register(irq,fn,arg,flags) を使い、表やexportスロットは追加しない。
