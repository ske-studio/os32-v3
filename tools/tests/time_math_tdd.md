# time_math — RED → GREEN の記録

票: [`docs/tasks/v3/TASK_HAL_WIRING.md`](../../docs/tasks/v3/TASK_HAL_WIRING.md) §1-5
対象: `kernel/time_math.c` (判定と算数) / `kernel/ktime.c` (スナップショット) /
`kapi/kapi_sys.c` (CPL=3 の検証)
試験: `tools/tests/test_time_math.py` + `tools/tests/time_math_host.c`
実行: `make check-time-math-host` (`check-par` の列)

## 何を作っているか

`tick_count` は 10ms 刻みしか無い。82557 の送受信も PCM のリングも
「いま何 µs か」を要る。PIT ch0 をラッチして周期の中を補間する。

## 難しいのは周期の境界

ラッチの**直前**に再ロードが起きると、新しい周期の `count`
(= `reload` に近い大きな値) を古い `tick` と組ませることになり、
**時刻が 1 周期ぶん戻る** (往復 2 の反例 1)。そこでラッチを PIC1 の
IRR bit0 (= IRQ0 が立っているか) で挟んで読む。

| p1 (ラッチ前) | p2 (ラッチ後) | 採るもの |
|---|---|---|
| 0 | 0 | `t`、`count` (境界を踏んでいない) |
| 1 | — | **`t + 1`**、`count` (再ロードはラッチより前。count は新周期) |
| 0 | 1 | **やり直し** (前後どちらか不明。最大 3 回 → `OS32_ERR_AGAIN`) |

## なぜホストで見るのか

- **三分岐を実 PIT で撃ち分けられない** (往復 8 の中継 1)。呼び出しから
  p1 読みまでに境界を越える機械があり、位相を待っても 0/0 と 0/1 を作り
  分けられない。`kernel/kselftest.c` は**入力列 {p1, p2, count} を注入**し、
  `time_branch_hits[]` で踏んだ分岐を数える。実 PIT 側は「残り 1〜2 count を
  観測してから読む」を期限 100 tick で試し、**踏めた分岐を記録して、踏めな
  かったものは未検証と報告する** (失敗にしない)。
- **71 分の桁あふれ**。`tick × period_us` を u32 で組むと 429496 tick
  (= 71.58 分) で時刻が 0 に戻る。起動から 71 分は NP21/W でも実機でも
  1 度も回していない。

## p1/p2 だけでは単調にならない (2026-09-23)

挟み込みは **8254 の再ロードと 8259 の IRR bit0 が原子的に動くこと**を
前提にしている。**NP21/W はそこを模擬しない** — PIT の `count` は経過
サイクルから計算され、IRQ0 は別立てのタイマ事象で上がるので、
次の 2 つが p1/p2 の検査をすり抜ける (実機でも数百 ns の窓で同じ形になる)。

| すり抜ける順序 | 見える値 | なぜ戻るか |
|---|---|---|
| count は再ロード済み・IRR はまだ | `p1=p2=0` → `t × 10000 + 小さい端数` | 旧周期の終わり (`t × 10000 + 9999`) より小さい |
| IRR が先・count は旧周期の終わり | `p1=1` → `(t+2) × 10000` 相当まで跳ぶ | ISR が走った後の読みが `(t+1) × 10000 + 端数` に戻る |

NP21/W の kselftest はこれで **「1 万回連続読みで逆行しない」が FAIL** した。
判定表はそのままにして、最後に**前回値でクランプ**する
(`time_clamp`、書き手は `kernel/ktime.c` の `sys_time_now` だけ、
スナップショットと同じ `irq_save` の中)。押さえた回数は
`ktime_clamp_count` に数え、kselftest が表示する (実機での回数は W4 の記録項目)。

## RED

### RED 1 — 実装が無い

```
tools/tests/time_math_host.c:19:10: fatal error: ../../kernel/time_math.c: No such file or directory
```

### RED 2 — 直す前の姿 (変異 1)

`p1 == 1` で tick を足さない = 「ラッチした count をそのまま今の tick と
組む」素朴な実装。`decide_table` が落ちる。これが往復 2 の反例 1 そのもの。

### RED 3 — クランプが無い (2026-09-23、ケース `clamp` の追加時)

```
tools/tests/time_math_host.c:(.text+0x10a8): undefined reference to `time_clamp'
```

実装を足してから、**「数えるだけで押さえない」** に変異させると
`clamp` が落ちる (変異 7)。これが NP21/W の FAIL そのもの。

## GREEN

```
$ python3 -B tools/tests/test_time_math.py --target --mutate
HOST GNU89 -Werror compile PASS (real kernel/time_math.c)
TARGET i386-elf GNU89 -Werror PASS
EXIT decide_table=0
EXIT us_math=0
EXIT no_u32_overflow=0
EXIT monotonic=0
EXIT clamp=0
SUMMARY 5/5 PASS
MUTATION 1 RED (1 件): p1 = 1 で tick を足さない (新しい周期の count を古い tick と組ませ、時刻が 1 周期ぶん戻る = 往復 2 の反例 1)
MUTATION 2 RED (1 件): p1 = 0 / p2 = 1 を**やり直さずに採用する** (境界のどちら側か分からない count を採るので最大 1 周期ずれる)
MUTATION 3 RED (1 件): tick との積を u32 で組む (**71 分で時刻が 0 に戻る**。起動から 71 分は エミュレータでも実機でも回していない)
MUTATION 4 RED (3 件): 経過ぶんを reload − count ではなく count そのものにする (時計が逆走する)
MUTATION 5 RED (1 件): p1 だけでは tick を足さない (p2 も 1 のときだけ足す。境界の直前で 読んで p2 が 0 に落ち着いた場合に 1 周期ぶん戻る)
MUTATION 6 RED (2 件): 端数の割り算を先に約分する (reload / period_us = 1 か 2 に潰れ、補間が µs ではなく 5〜10ms 刻みになる)
MUTATION 7 RED (1 件): 巻き戻りを数えるだけで**押さえない** (NP21/W は 8254 の再ロードと 8259 の IRR を原子的に模擬しないので、判定表を正しく通しても 1 万回読みが逆行する)
MUTATION 8 RED (1 件): 同じ µs を 2 回読んだだけでクランプに数える (回数が「模擬の粗さ」の指標にならなくなる)
```

## 何を見ているか

| ケース | 見るもの |
|---|---|
| `decide_table` | p1/p2 の 3 分岐、`p1 = 1` なら p2 を見ない、やり直しのとき `out` を触らない、`time_branch_hits[]` が 3 分岐を数える、NULL は採らない |
| `us_math` | 両方のクロック (reload 19968 / 24576) で周期のはじめ・真ん中・終わりが同じ µs、`count == 0` はちょうど 1 周期、`reload == 0` でも 0 除算しない、範囲外の count を巻き込まない |
| `no_u32_overflow` | 429496 tick の直前は収まり、**1 tick 先で hi が 1 になる**、上下を組み直すと素直な値、1 日ぶんでも単調 |
| `monotonic` | tick が進み count が減る筋書きで逆行 0、**周期をまたぐ組** (`t, 1`) → (`t+1, reload`) が逆転しない |
| `clamp` | 進む側は素通しで数えない、**等しいのはクランプではない**、すり抜ける 2 つの順序 (count 先 / IRR 先) を押さえる、1 万回の筋書きで逆行 0 かつ回数ちょうど、71 分の桁を跨いでも単調、NULL は書かないだけ |

## ここでは見られないもの (W4 = kselftest / NP21/W / 実機)

- 実 PIT のラッチと IRR の実読み (`kernel/kselftest.c` の `test_time_now`)
- 1 万回連続読みの逆行 0、`cpu_delay_us(1000)` を挟んだ実測、
  `ktime_clamp_count` の実数 (NP21/W と実機でどれだけすり抜けるか)
- **CPL=3 のポインタ契約** — `ring3_in_syscall` が 0 のあいだ検証は素通しなので、
  カーネル内からは踏めない。`userland/tests/time_test.c` (CPL=3) が
  NULL・範囲の交差・ヒープ / スタックの正常系を見る。
  読み取り専用 USER ページの kill は `time_test ro` (**アプリを殺す**ので明示要求)。
- PDE / PTE のビット判定表そのものは `tools/tests/ring3_str_host.c` のケース 5
