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

## 設計の補足

- `serial_putchar_kapi`等の正確なプロトタイプはdrivers/char/serial.hに合わせる。表のinit_vfastにもbaud変換adapterを置く。

- UART状態・gate・hold/RXリング・エラー計数・高速モードはserial driverが保持。ポート表への転送は協調/KAPI文脈。診断snapのIRQ扱いも既存serial_diag_getと同じ。

| 項目 | 既存経路 | 専用出口 |
|---|---|---|
| D6 | 「命令前にシリアルへ1行」は既存 `drivers/char/serial.c:822–835` だけでは保証できない。送信待ち切れでも void で戻り、文字が落ちる | 成否と送信完了を返す専用出口を足し、失敗時は次の GPU 操作をしない |

- serialの専用checked送信は既存経路と排他、TxRDY待ち＋最後の送信完了確認を期限付きにし、失敗はLOG_FAILで停止。

```c
/* drivers/char/serial.c: checked API の署名・import は不変。失敗は outp より前 */
int serial_checked_put(u32 byte) {
    unsigned int f=irq_save(); int ready=serial_checked_ready();
    if (ready!=1) { irq_restore(f); return ready<0 ? -21 : -22; }
    outp(s_port_data,(u8)byte); irq_restore(f); return 0;
}
```

- checked UARTの所有はRECの1行単位のTXだけとしRXは維持する；通常出力は既存holdへ保留、SerialFS送信・gate変更・速度変更を拒否し、最後のframe待ち後に既存UARTのTxEMPを有限予算で確認、全帰還で所有を解放する。

- S6修正: checked TX中の通常kprintfは既存holdに入るがchecked_endでは排出せず、次のSerialFS gateで消える仕様と明記する；RECの有限送信予算と既存rshell/SerialFSの動作を変更しない。
