# cpu_calibrate — RED → GREEN の記録

票: [`docs/archive/realhw_v21/TASK_SERIAL_VFAST.md`](../../docs/archive/realhw_v21/TASK_SERIAL_VFAST.md) (往復 3)
対象: `kernel/cpu_calibrate_math.c` (止め方と計算) / `kernel/cpu_calibrate.c` (計測)
試験: `tools/tests/test_cpu_calibrate.py` + `tools/tests/cpu_calibrate_host.c`
実行: `make check-cpu-calibrate-host` (`check-par` の列)

## 何が起きていたか

実機 PC-9821Ra266 でシリアルが **9600 で 389 B/s、38400 に上げても 437 B/s** しか
出なかった。速度を上げても変わらない = 回線ではなく **1 バイトあたり約 2ms の
固定費**。ホスト側で到着間隔を見ると 16ms ごとに 4〜10 バイトの塊 (FTDI の
遅延タイマ) で、ゲストが回線より遅く出していた。

追うと `cpu_calibrate()` に行き着いた:

```c
start = tick_count;
nop_loop(CALIBRATE_LOOPS);      /* 200,000 回 */
elapsed = tick_count - start;
if (elapsed == 0) elapsed = 1;  /* ← ここ */
s_loops_per_tick = CALIBRATE_LOOPS / elapsed;
```

コメントの想定は 8MHz (1 周 125ms = 12.5 tick) と 33MHz (30ms = 3 tick) だった。
266MHz では **1 周が約 1ms = 0.1 tick** で、`elapsed = 0` が 1 に丸められ、
`s_loops_per_tick` が**実際の 1/7〜1/13**になる。

その先:

| 段 | 直す前 (実機 266MHz) | 本来 |
|---|---|---|
| `s_loops_per_tick` | 200,000 | 約 2,000,000 |
| `cpu_delay_us(5)` のループ数 | 100 | 1,000 |
| `cpu_delay_us(5)` の実時間 | **約 0.5µs** | 5µs |
| `serial_putchar` の予算 (`waited += 5` を 416 回) | **約 200µs** | 2083µs |
| 9600 の 1 文字時間 | 1.04ms | 1.04ms |

予算が 1 文字時間の 1/5 で尽きるので、**毎バイト `_halt()` に落ちて次の割り込み
(実測 ≒2ms) まで寝ていた**。速度に依らない 2ms の固定費と一致する。
直す前の「100 回スピン → hlt」も同じ形だった。

**NP21/W でも丸めは起きる。症状が出るほど深くなかっただけ。** 実測は
`rounds = 16 / ticks = 5` = **1 周 ≒ 0.31 tick (3.1ms)**。

⚠ ここを「1 周 0.31 tick だから旧コードでも `elapsed` は 1 以上」と読むのは
**生値と補正後の取り違え**。旧コードの生の `elapsed` は NP21/W でも **0** で、
`if (elapsed == 0) elapsed = 1;` の補正が 1 を入れていた。結果は
200,000 loops/tick = 真値 (640,000) の **約 1/3.2**。

実機 266MHz は 1 周 0.1 tick 未満でもっと深く 0 に落ち、**1/7〜1/13**。
**どちらも生値は 0 で、補正が桁を決めていた。** 症状が出るかどうかの境目は
「`cpu_delay_us` で積んだ予算が 1 文字時間を割り込むか」だけ — 3.2 倍のずれ
では `cpu_delay_us(5)` が 1.6µs で、416 回積めば 650µs。9600 の 1 文字
(1.04ms) には足りないが hlt 1 回で済む。10 倍を超えると毎バイト hlt になる。

## RED

1. ハーネス (`cpu_calibrate_host.c`) と `test_cpu_calibrate.py` を先に書いた。
   `kernel/cpu_calibrate_math.c` が無いのでコンパイルが通らない = RED。

2. 実装後の否定側 (`--mutate`)。**5 本とも実行時 RED** (写しの上で変異するので
   実物のソースは 1 バイトも動かない = `check-par` で並列に回せる)。

   | # | 壊し方 | 結果 |
   |---|---|---|
   | 1 | 1 周で打ち切る (= **直す前の姿**) | RED (3 件) |
   | 2 | 1 tick で確定してよいことにする (±100% の誤差) | RED (2 件) |
   | 3 | 周回数の打ち切りを 10 倍遠くへ (PIT が死んだとき戻るのが遅すぎる) | RED (1 件) |
   | 4 | 測れなかったときに合計を 1 tick で割る | RED (1 件) |
   | 5 | 極端に小さい結果をそのまま採る (安全装置を外す) | RED (1 件) |

## GREEN

```
HOST GNU89 -Werror compile PASS (real kernel/cpu_calibrate_math.c)
TARGET i386-elf GNU89 -Werror PASS   (cpu_calibrate_math.c + cpu_calibrate.c)
SUMMARY 4/4 PASS
MUTATION 1..5 すべて RED
```

## 何を見ているか

| ケース | 見るもの |
|---|---|
| `fast_cpu` | 0 tick のうちは**必ず続行**、1〜4 tick でも確定しない、5 tick で確定。266MHz 相当 (1 周 0.1 tick) は 50 周で 5 tick に届き 200 万 loops/tick。**直す前の値 (20 万) の 10 倍**であることを式で押さえる |
| `slow_cpu` | 8MHz 相当 (1 周 12.5 tick) は **1 周で確定** (遅い機械の起動を延ばしていない)。33MHz 相当は 2 周 |
| `give_up` | PIT が死んで tick が進まないとき、周回数の上限で打ち切って**戻ってくる**こと。そのとき合計を 1 で割らずフォールバック値を返す (4000 万 loops/tick が入ると `cpu_delay_us` が返らない) |
| `delay_scale` | `cpu_delay_us` の式に入れたとき、正しい校正値だと 5µs 要求が 1000 ループ、直す前は 100 ループ = **実際 0.5µs** であることを数で示す |

## 直し方

`kernel/cpu_calibrate.c`:

```c
start = tick_count;
do {
    nop_loop(CALIBRATE_LOOPS);
    total += CALIBRATE_LOOPS;
    rounds++;
    ticks = tick_count - start;
} while (!cpu_calibrate_enough(ticks, rounds));
s_loops_per_tick = cpu_calibrate_compute(total, ticks);
```

- `CALIBRATE_MIN_TICKS` = 5 — 1 tick では ±100% ぶれる (tick は「境界を何回
  跨いだか」なので最初の 1 回は 0〜10ms のどこでも立つ)。5 tick で ±20%。
- `CALIBRATE_MAX_ROUNDS` = 200 — **PIT が死んだときの保険**。無いと起動が
  戻らない。266MHz でも 50 周で足りるので通常経路には掛からない。
- `cpu_calibrate_enough(ticks, rounds)` が `rounds` も取るのはこのため。

診断用に `cpu_calib_rounds` / `cpu_calib_ticks` をグローバルで置いた
(`kernel.map` 越しに読む。実機で「丸めが起きていない」ことを確かめる唯一の手段)。
`kselftest` にも `test_cpu_calibrate()` を足して、起動時に
`loops_per_tick >= CALIBRATE_MIN_LPT` と `cpu_calib_ticks >= CALIBRATE_MIN_TICKS`
を自分で見る。

## `cpu_delay_us` の利用者への影響

**校正が正しくなると実待ち時間が 7〜13 倍に伸びる** (いままで実機で短すぎた)。
伸びるのが正しい向きだが、どこが影響を受けるかは記録しておく:

| 呼び手 | 用途 | 読み |
|---|---|---|
| `drivers/serial.c` | TxRDY のポーリング間隔 (`SER_TX_POLL_US` = 5µs) | **意図どおりになる**。ただし予算自体は tick 基準に変えたので、ここが多少ずれても結果は変わらない |
| `drivers/ne2000.c` | `NE2K_RDC_POLL_US` / `NE2K_RESET_POLL_US` / `NE2K_OVW_WAIT_US` のポーリング間隔 | いずれも**有限回ポーリングの間隔**で、待ちの上限は回数で決まる。間隔が伸びる = 上限までの実時間が伸びるので、**取りこぼしは減る方向**。逆に「回数を使い切るまでの時間」が 7〜13 倍になるので、切断された機器を待つ最悪時間は伸びる (実機で要観察) |
| `gfx/gfx_vram.c` | `RASTER_LINE_US` (走査線 1 本ぶんの待ち) | **ここが一番効く**。短すぎた待ちが本来の長さになるので、ティアリング対策としては正しくなるが、**描画が遅くなったように見える可能性がある**。実機で GUI の体感を見ること |

**まだ実機で確かめていない** ([V4])。NP21/W の実測は
`cpu_calib_rounds = 16 / cpu_calib_ticks = 5` = 校正値 `16 × 200,000 / 5`
= **640,000** loops/tick。旧コードは丸めて 200,000 だったので、
**エミュレータでも約 3.2 倍**に伸びる。待ち時間が伸びる側なので、
NP21/W 上の ne2000 / gfx の挙動 (特にスプラッシュ) は PM が見ること。

## まだ試験していないこと

- **実機での校正値そのもの。** 266MHz で `cpu_calib_rounds` ≒ 50 /
  `cpu_calib_ticks` = 5 / `loops_per_tick` が 100 万台後半〜200 万台になるはず、
  という見積もりでしかない。`kernel.map` から読んで確かめる。
- **シリアルの実効速度が上がったか** (票 S5: 9600 で 900 B/s 以上、
  115200 で 8 KB/s 以上)。ここが本題で、実機でしか測れない。
- **ne2000 / gfx_vram への影響。** 上の表は読みであって実測ではない。
