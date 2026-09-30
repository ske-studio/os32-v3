# pit_clock — RED → GREEN の記録

票: [`docs/tasks/v3/TASK_HAL_WIRING.md`](../../docs/tasks/v3/TASK_HAL_WIRING.md) §1-0
対象: `kernel/pit_math.c` (分周の算数) / `kernel/sysclk.c` (クロック判定) / `kernel/idt.c` (積む側)
試験: `tools/tests/test_pit_clock.py` + `tools/tests/pit_clock_host.c`
実行: `make check-pit-clock-host` (`check-par` の列)

## 何が起きていたか

`kernel/idt.c` の `pit_init()` は `PIT_CLOCK` = 1,996,800Hz **決め打ち**で分周していた。

```c
divisor = (u16)(PIT_CLOCK / hz);   /* 1996800 / 100 = 19968 */
```

PC-98 の 8253 TCU に入るクロックは機種で 2 通りある (`0000:0501h` bit7 で判別、
1 = 8MHz 系 → 1.9968MHz / 0 = 5･10MHz 系 → 2.4576MHz)。**2.4576MHz 系**の機械
(実機 PC-9821Ra266 はこちら。`[ser] 9600bps (clk 2457600Hz, count 16)` と出る) に
19968 を積むと、1 tick は

    19968 / 2457600 = 8.125ms  (= 123Hz)

で、100Hz のつもりが **23% 速い**。`tick_count` を数える待ち・番犬 (FDC の
タイムアウト、シリアルの TX 予算) と CPU 校正が全部ずれる。

**NP21/W は 1.9968MHz 設定なので、エミュレータでは一度も踏めない** — §4-49・
§4-51・§4-53 と同じ型 (エミュレータが模擬しない機種差)。クロック判定そのものは
`drivers/serial.c` の `serial_detect_clock()` に既にあったが、呼ばれるのは
`kernel.c:228` = `pit_init` (194 行) の **34 行あと**で、PIT は見ていなかった。

## RED

### RED 1 — 実装が無い

`kernel/pit_math.c` / `.h` を置く前にハーネスを回すと、`#include` が解決できず
コンパイルが通らない。

```
$ python3 -B tools/tests/test_pit_clock.py
tools/tests/pit_clock_host.c:17:10: fatal error: ../../kernel/pit_math.c: No such file or directory
   17 | #include "../../kernel/pit_math.c"
compilation terminated.
subprocess.CalledProcessError: Command '['gcc', '-std=gnu89', ... ]' returned non-zero exit status 1.
```

### RED 2 — 直す前の姿 (変異 1)

`raw = clk_hz / hz` を `raw = SYSCLK_1997 / hz` に戻す = クロックを見ない実装。
`both_clocks` と `real_hw_story` が落ちる (2 件 RED)。**これが実機で起きていた
ことそのもの**で、1.9968MHz のケースは通ったままなのが「エミュレータでは
気づけない」ことを示している。

## GREEN

```
$ python3 -B tools/tests/test_pit_clock.py --target --mutate
HOST GNU89 -Werror compile PASS (real kernel/pit_math.c)
TARGET i386-elf GNU89 -Werror PASS
EXIT both_clocks=0
EXIT reject_hz=0
EXIT reject_clock=0
EXIT no_overflow=0
EXIT real_hw_story=0
SUMMARY 5/5 PASS
MUTATION 1 RED (2 件): クロックを見ずに 1.9968MHz で割る (= 直す前の姿。実機で tick 8.125ms)
MUTATION 2 RED (1 件): 100Hz 以外を受けてしまう (端数のリロード値が積まれ周期がずれる)
MUTATION 3 RED (1 件): 知らないクロックで割る (0 除算・桁違いのリロード値)
MUTATION 4 RED (2 件): 約分せず 10^6 を掛ける (32bit で溢れて period_us が桁違いになる)
MUTATION 5 RED (2 件): 断ったのに valid を立てる (呼び出し側が古い値を本物と読む)
```

## 何を見ているか

| ケース | 見るもの |
|---|---|
| `both_clocks` | 1.9968MHz → 19968 / 2.4576MHz → **24576**、どちらも周期 10000µs |
| `reject_hz` | 1000 / 0 / 18 / 60 / 99 / 101Hz を**既定へ倒さずに**断る (`PIT_ERR_HZ`) |
| `reject_clock` | 未知のクロック (PC/AT の 1.193182MHz、0、±1) を断る。`out` は valid = 0 |
| `no_overflow` | `PIT_PERIOD_SCALE × PIT_CLOCK_DIV == 10^6` と、中間積が 32bit に収まること |
| `real_hw_story` | 直す前の式が 2.4576MHz 機で 8125µs (ずれ 18.75%) になることを数で固定 |

## 32bit の溢れをホストで踏むための細工

`period_us = reload × 10^6 ÷ clk_hz` は **i386 の `unsigned long` (32bit) で溢れる**
(24576 × 10^6 = 2.4576×10^10)。ところがホストの `unsigned long` は 64bit なので、
素直に書くと**ホストでは溢れず試験が素通しになる**。そこで倍率の定数
(`PIT_PERIOD_SCALE` / `PIT_US_PER_SEC`) を `UL` ではなく **`U` (= どの環境でも
32bit)** で持ち、計算を `unsigned int` で行う。これで変異 4 がホストでも RED になる。
定数そのものの上限は `kernel/pit_math.c` の `STATIC_ASSERT` が i386 ビルドで見る。

## 実機でしか確かめられない残り

ホスト試験は算数だけを見る。**判定 (`0000:0501h` bit7 の実読み) と、それが
`pit_init` より前に走る順序**は実機/エミュレータでしか踏めないので、
`kernel/kselftest.c` の `test_pit_setup()` が起動ごとに見る
(`pit_get_setup()->valid` / `reload == sysclk_hz() / PIT_HZ` / `period_us == 10000`
/ `mode == PIT_MODE_TIMER0` / `sysclk_detected()`)。
実時間そのものの照合は、実機で `tick_count` を 60 秒ぶん読んでホストの単調時計と
突き合わせる (docs/POLICY_DEBUG.md §4-54)。
