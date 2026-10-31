# tools/tests/cd_read_host_multiple_reset.inc — 実 SRST 経路の通知

> 状態: **現行 (2026-10-31)**

実物 atapi.c の `atapi_srst` を呼び、IDE 通知だけを境界 stub にする。
正常終了と master BSY timeout の両方で、SRST を立てる前に bank1 の
2台だけを無効化したことを、ポートの書込み時点で検査する。
ATAPI DEVICE RESET だけなら IDE 状態を失効しない。
IDE の再設定の挙動は [IDE 模型](ide_pio_host_multiple.md) の製品コードで検査する。

## 落とし穴

SRST の後での通知は遅い。timeout でも古い active を残してはいけない。
