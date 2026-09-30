# irq_math — RED → GREEN の記録

票: [`docs/tasks/v3/TASK_HAL_WIRING.md`](../../docs/tasks/v3/TASK_HAL_WIRING.md) §1-1
対象: `kernel/irq_math.c` (判断) / `kernel/irq.c` (表・PIC・EOI) / `kernel/isr_stub.asm` (共通スタブ)
試験: `tools/tests/test_irq_math.py` + `tools/tests/irq_math_host.c`
実行: `make check-irq-math-host` (`check-par` の列)

## 何を直しているか

`kernel/isr_stub.asm` は装置ごとに固定の C ハンドラを `extern` で結んでおり、
**起動時に読んだ IRQ 番号でハンドラを結ぶ口が無かった**。PCI の装置は BIOS が
IRQ を割り当て、複数の装置が 1 本を共有し得るので、82557 (L-B) と CS4231 を
載せるにはこの層が要る。

## なぜホストで見るのか

この層の危ないところは**どれも「時間の重なり」**で、NP21/W でも実機でも
狙って作れない (機種差ではないので、どちらでも再現を待つしかない):

- **A と B が同時に要因を持つ。** 最初の `IRQ_HANDLED` で走査を打ち切ると B の
  要因が残り、PC-98 の 8259 は `ICW1_INIT` = 0x11 の**エッジ**設定なので
  線が下がらないまま次のエッジが来ない = その線は二度と上がらない。
- **1 巡目で受けて 2 巡目が空。** `any = round` と書くと「受けたのに誰も
  受けなかった」に化け、診断もストーム勘定も狂う。
- **1 tick に 200 回と 201 回。** 閾値は片側だけずらしても気づけない。
- **IRQ15 のスレーブ ISR bit7 が落ちている** (スプリアス)。本物の IR7 を
  潰すかどうかがここで決まる。

W3 (NP21/W) は「配線が通っているか」を `int 0x23` と `/api/pic` で見る側で、
**分岐の網羅はここが持つ**。

## RED

### RED 1 — 実装が無い

`kernel/irq_math.c` / `.h` を置く前にハーネスを回すと `#include` が解決できない。

```
tools/tests/irq_math_host.c:20:10: fatal error: ../../kernel/irq_math.c: No such file or directory
```

### RED 2 — 直す前の姿 (変異 1)

最初の `IRQ_HANDLED` で走査を打ち切る = 「1 装置 1 IRQ」の固定スタブと同じ形。
`dispatch_two_pass` が落ちる (B が呼ばれない)。

## GREEN

```
$ python3 -B tools/tests/test_irq_math.py --target --mutate
HOST GNU89 -Werror compile PASS (real kernel/irq_math.c)
TARGET i386-elf GNU89 -Werror PASS
EXIT dispatch_two_pass=0
EXIT register_rules=0
EXIT storm=0
EXIT eoi_plan=0
SUMMARY 4/4 PASS
MUTATION 1 RED (1 件): 最初の HANDLED で走査を打ち切る (同時に要因を持つ相方を取りこぼす = 共有線が上がったまま次のエッジが来ない)
MUTATION 2 RED (1 件): handled_any を 2 巡目で上書きする (受けたのに「誰も受けなかった」に化け、診断とストーム勘定が狂う)
MUTATION 3 RED (1 件): 5 件目を受けてしまう (表の外へ書く)
MUTATION 4 RED (1 件): 新規の flags だけ見て既存を見ない (排他のつもりの driver が相席させられる)
MUTATION 5 RED (1 件): 閾値が 1 つ手前 (200 回ちょうどでマスクが入る)
MUTATION 6 RED (1 件): IRQ15 のスプリアスでもスレーブに EOI を送る (本物の IR7 を潰す)
MUTATION 7 RED (1 件): 隔離済みの線に登録を許す (打ち切ったはずの線が再び動き出す)
MUTATION 8 RED (1 件): 範囲外 (>= 16 / PCI の未割り当て 0xFF) を「固定 IRQ」と同じ扱いにする (呼び手が番号の壊れと線種の違いを区別できなくなる)
```

## 何を見ているか

| ケース | 見るもの |
|---|---|
| `dispatch_two_pass` | 走査の順、**同時要因で打ち切らない**、2 巡ちょうど (3 巡目は無い)、`handled_any` を 2 巡目の 0 で上書きしない、`IRQ_DEFERRED` の計数、`shared_dispatch` は 2 登録以上のときだけ、登録 0 件・表 NULL でも落ちない |
| `register_rules` | 動的な線は 3/5/6/8/9/10/14/15 だけ、固定 IRQ は `-NOTSUP`、範囲外は `-INVAL`、NULL の fn / 知らない flags、重複 `{fn,arg}`、**両側が SHARED のときだけ受ける**、5 件目、隔離済みは満杯より先、解除の探索と**詰め直し** (穴を作らない) |
| `storm` | 200 回では入らず **201 回で入る**、tick が変われば窓を作り直す、一度立てたら 2 度は返さない、受けた回ではマスクしない |
| `eoi_plan` | irq < 8 = マスタ、irq >= 8 = スレーブ → マスタ、**IRQ15 で ISR bit7 が落ちていればマスタだけ** (handled とは独立)、範囲外は何も送らない |

## ここでは見られないもの (W3 = NP21/W / 実機)

- 実 IRQ のエッジそのもの (master 3 / slave 9)、`/api/pic` の IRR・ISR
- V86 中・CPL=3 アプリ実行中の割り込み
- LGY-98 をアダプタに移した後の実通信
- `int 0x23` を使う kselftest 側 (`test_irq_dynamic`) は「配線が通っているか」
  = 表・PIC のマスク・共通スタブ・`irq_finish` までを起動ごとに踏む
