# drivers/char/serial.c — PC-98 シリアル通信

> 状態: **現行 (2026-10-31)**

8251 と FIFO 対応ポートを初期化し、送受信と IRQ を扱う。

## 落とし穴

- **分周と機種差**: 8253 の整数分周では指定速度と実効速度が一致しない場合がある。
  `drivers/char/serial.c:400` はクロックと FIFO の有無から計画し、互換モードで不正確な速度を拒否して現在の設定を保つ。
  9600bps は両クロックで正確に出る。1.9968MHz 系では旧来の切捨て分周で 19200 は 20800、38400 は 41600bps になる。`serial_set_div4` は 0434h bit6 の機種依存の極性を自動で決めず、他ビットを保つ (`drivers/char/serial.c:177`)。
  分周の検査は `tools/tests/test_serial_vfast.py`、実効速度は実機でも確認する。
- **共有ポート C**: 0035h の全体書込みは BUZ など別機能も変え、ブザーが鳴り続けうる。
  `drivers/char/serial.c:144` の BSR で必要な割込み許可ビットだけを操作し、不要な TXRE を触らない。
  BUZ は 0 で鳴動、BSR の ON=06h / OFF=07h (`include/pc98.h`)。`make check-serial-portc-host` が全体書込みを検出する。
