# sys/dev/char/serial_plan.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_SERIAL_VFAST §1・§2 の契約を今のソースと照らして要約
ソース: `sys/dev/char/serial_plan.c`・`sys/dev/char/serial_plan.h` (`serial_plan`・`serial_vfast_div`・`serial_tx_budget_us`・`serial_tx_budget_ticks`・`serial_fifo_detected`・`serial_*_port`・`serial_*_mask`)・I/O と配線 `sys/dev/char/serial.c`・値の正典 `sdk/include/os32/os32_kapi_shared.h` (`KAPI_SER_*`)

シリアルの「速度をどう出すか」の純粋な判定。**NP21/W は通信速度を模擬しない**ので、分周表やビット位置を間違えても
エミュレータでは動いてしまう。ここをホストで試験し、I/O の順番と IRQ の汲み出しは実機で確かめる。
資料は `docs/hw/undocumented/io_rs.md` (0130h〜013Ah)。

## 速度の決め方 (`serial_plan(baud, has_fifo, clk, want_vfast, out)`)

- 起動時の既定は 9600 の互換モード。V･FAST は `serial N` で明示したときだけ (`want_vfast`)。baud = 0 は 9600。
- **V･FAST** に入るのは `want_vfast` かつ FIFO 搭載 (`has_fifo`) かつ N が表にあるときだけ。013Ah bit3-0 の分周:
  9600 = 1100b、14400 = 1000b、19200 = 0110b、28800 = 0100b、38400 = 0011b、57600 = 0010b、115200 = 0001b。表に無ければ
  `serial_vfast_div` は 0。V･FAST では 8253 を触らない (count = 0)、exact = 1。
- **互換**: 8253 カウンタ#2、count = clk / 16 / baud (0 なら 1、16 ビットで頭打ち)、actual = clk / 16 / count、exact = (actual == baud)。
  clk = 0 なら何も決めない (exact = 0)。1.9968MHz 系で 38400 を頼むと count 3・実効 41600 になる。
- exact でない互換の速度は呼び手 (`serial.c`) が**適用しない** (`SER_INIT_REFUSED` = ハードウェアに 1 バイトも書かない、今の
  設定が生き残る)。

## FIFO 搭載判定 (`serial_fifo_detected(first, second)`)

0136h を 2 回読み、bit5 がどちらも 0 で、bit6 が読むたびに反転していれば搭載。0xFF (未実装ポート) は bit6 が反転しないので
非搭載に落ちる。

## ポートとビット (互換 0032h と FIFO 0132h はビット位置が違う)

| | 互換 (データ 0030h / 状態 0032h) | FIFO (データ 0130h / 状態 0132h) |
|---|---|---|
| TxRDY | bit0 | bit1 |
| RxRDY | bit1 | bit2 |
| TxEMP | bit2 | bit0 |

FIFO モードではコマンドの書き込みも 0132h。選択子 (`serial_data_port`・`serial_cmd_port`・`serial_txrdy_mask`・`serial_rxrdy_mask`・
誤りのマスク) を通して 1 か所に閉じ込める。戻しは `serial 9600` で 013Ah bit7 = 0、0138h = 0。

## 送信の待ち予算

- µs の予算 = 2 文字分 = 2 × 10 × 1e6 / baud (下限 40µs、上限 50ms)。9600 → 2083µs、115200 → 173µs。
- 判定は **tick で測る** (`serial_tx_budget_ticks`): µs を tick (10ms) に切り上げ、境界ずれの 1 を足し、下限 3 tick (保証 20ms 以上、
  FTDI の遅延タイマ 16ms をまたぐ)。cpu_delay_us の校正に依存させない。
- 予算のあいだは `cpu_delay_us(5)` を挟んで TxRDY を見る。超えたら `hlt` (最大 5 回)。IF = 0 では tick が進まないので回数で諦める
  (20ms 相当 = 4000 回)。
- 1 バイトの送信は成否を返す (`SER_TX_OK` / `SER_TX_DROPPED`)。送れなかったことを黙って捨てない。

## 確かめ

`make check-serial-vfast-host` (`test_serial_vfast.py (旧版 850eef82c906)`、`serial_vfast_host.c (旧版 abdb25442030)`)。分周表・モード判定 (FIFO の有無 ×
速度)・0132h と 0032h のビット違い・予算・番犬 (`docs/design/userland/shell/serial_watchdog.md`)。I/O の順番と IRQ の汲み出し、
実効速度は実機でしか見られない。
