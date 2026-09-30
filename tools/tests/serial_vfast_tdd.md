# serial_vfast — RED → GREEN の記録

票: [`docs/archive/realhw_v21/TASK_SERIAL_VFAST.md`](../../docs/archive/realhw_v21/TASK_SERIAL_VFAST.md)
対象: `drivers/serial_plan.c` (速度の決め事) / `userland/shell/serial_watchdog.c`
      (切替後の番犬) / `drivers/serial.c` `userland/shell/rshell.c` (I/O と配線)
試験: `tools/tests/test_serial_vfast.py` + `tools/tests/serial_vfast_host.c`
実行: `make check-serial-vfast-host` (`check-par` の列)

## なぜホストで試験するのか

**NP21/W は通信速度を模擬しない** (`np21w-src/src/io/serial.c`: `013Ah` の速度表は
持つが、`commng` に速度を伝えるだけでビットの流れる時間は再現しない)。だから
「エミュレータで確認済み」がここでは通用しない — 分周表を 1 行ずらしても、
`0132h` の RxRDY を `0032h` のビット位置で見ても、NP21/W では動いてしまう。

実機に出す前に押さえられるのは「決め事」だけなので、決め事を
`drivers/serial_plan.c` に切り出してそこを試験する。I/O を伴う部分
(`0138h` / `013Ah` を実際に叩く順番、IRQ ハンドラの汲み出し) は
**実機での確認が要る** (票 §3 の S5 / S6)。ここで緑でも実機合格ではない ([V4])。

## RED

1. ハーネス (`serial_vfast_host.c`) と `test_serial_vfast.py` を先に書いた。
   `drivers/serial_plan.c` が無いのでコンパイルが通らない = RED。

   ```
   serial_vfast_host.c:18:10: fatal error: ../../drivers/serial_plan.c:
   No such file or directory
   ```

2. 実装後の否定側 (`--mutate`)。実物を 1 か所だけ壊して、どれも RED になることを見る。
   **変異は写しの上で行う** (一時ディレクトリの `drivers/serial_plan.c` を先に
   引かせる) ので、実物のソースは 1 バイトも動かない = `check-par` で並列に回せる。

   | # | 壊し方 | 結果 |
   |---|---|---|
   | 1 | V･FAST の表を 1 行ずらす (115200 のつもりで 57600 が出る) | RED (3 件) |
   | 2 | 表に無い速度 (4800) を足す = 9600 の戻しが V･FAST に入らない | RED (2 件) |
   | 3 | FIFO の RxRDY を互換のビット位置 (bit1) で見る | RED (1 件) |
   | 4 | FIFO の TxRDY を TxEMP (bit0) と取り違える | RED (1 件) |
   | 5 | 予算の下限を外す (速い速度で 0µs → 直す前の `hlt` 待ちに戻る) | RED (1 件) |
   | 6 | `want_vfast && has_fifo` を `||` にする (明示指定なしでも V･FAST に入る) | RED (2 件) |
   | 7 | 割り切れない分周を「ちょうど出る」と答える (38400 → 41600 を見逃す) | RED (4 件) |
   | 8 | 番犬が期限を受信より先に見る (期限ちょうどに届いた応答を無音と読み替える) | RED (1 件) |
   | 9 | 番犬の期限を 1000 倍にする (事実上いつまでも戻さない) | RED (1 件) |

## GREEN

```
HOST GNU89 -Werror compile PASS (real drivers/serial_plan.c)
TARGET i386-elf GNU89 -Werror PASS      (drivers/serial_plan.c + drivers/serial.c)
SUMMARY 10/10 PASS
MUTATION 1..9 すべて RED (どれも実行時。コンパイルエラーに逃げていない)
```

## 何を見ているか

| ケース | 見るもの | 根拠 |
|---|---|---|
| `vfast_table` | 速度 → `013Ah` bit3-0 の分周。表に無い速度は **0** | `io_rs.md` 352〜380 行 / NP21/W `speedtbl` |
| `compat_exact` | 2.4576MHz なら 9600=16 / 19200=8 / **38400=count 4 ちょうど** | 実機 Ra266 (TASK_FDC_REALHW §9-1) |
| `compat_inexact` | **1.9968MHz の 38400 は count 3 → 実効 41600 (+8.3%)**、`exact=0` | `drivers/serial.h` の表 |
| `mode_choice` | V･FAST に入るのは **FIFO 搭載 × 明示指定 × 表にある速度** の 3 つ揃ったときだけ | 票 §2 の決裁 |
| `tx_budget` | 予算 = 2 × 10 ビット ÷ baud (9600→2083µs / 115200→173µs)、**0 にしない** | 票 §2 |
| `tx_budget_ticks` | **予算は tick で測る**。標準速度は下限の 3 tick (保証 20ms ≥ FTDI の遅延タイマ 16ms)、遅い速度は切り上げ + 境界ずれで伸びる、1 にも 0 にもならない | 往復 3 |
| `status_bits` | 互換 `0032h` は bit0 TxRDY / bit1 RxRDY、FIFO `0132h` は bit1 TxRDY / bit2 RxRDY | `io_rs.md` 0132h / NP21/W `rs232c_i132` |
| `fifo_detect` | `0136h` を 2 回読んで bit6 が反転 + bit5 が 0。**0xFF も 0x00 も非搭載へ落ちる** | `io_rs.md` 304〜323 行 |
| `real_hw_story` | 9600 起動 → `serial 115200` (`013Ah` に 0x81) → `serial 9600` で戻す | 票 §3 |
| `refuse_inexact` | **出せない速度を適用しない**: 1.9968MHz の 38400 は拒否、2.4576MHz の 38400 は count 4 で適用、FIFO 無しの 115200 は**どちらのクロックでも**拒否。起動時の 9600 は両クロックで exact = 拒否の分岐を通らない | Codex レビュー blocker 2a |
| `watchdog` | 切替後 500 tick **`serial ack` が来なければ** REVERT、来れば LINKED、**ack は期限より先に見る**。仕掛かっていないときは印を立てない / 答えは 2 度返らない / arm が印を 0 に戻す / NULL で落ちない / tick の巻き戻りで壊れない | 往復 6 の設計変更 |
| `watchdog_leave` | **rshell を抜けるとき**: 未確認なら期限前でも REVERT、確認済み・解除済み・仕掛けていない (ローカル CUI の切替) なら何もしない、2 度は戻さない | 往復 5 B3・B4 |

## 資料と NP21/W が食い違ったところ

| 箇所 | 資料 (`io_rs.md`) | NP21/W | 採ったもの |
|---|---|---|---|
| `0132h` bit3〜7 | bit5 パリティ / bit4 オーバーラン / bit7 ブレーク / bit3 不明 | bit3 パリティ / bit4 オーバーラン / bit5 フレーミング / bit6 ブレーク (「Vol.2 の記載は誤り」と明記) | **どちらでも共通に立つ bit3〜5 (0x38) をエラーとして見る**。エラーの種別で分岐していないので、これで両方の解釈に耐える |
| `0132h` への書き込み | 記述なし ([READ] のみ) | `0x132` の out を `0032h` と同じハンドラ `rs232c_o32` に繋ぐ (`rs232c_bind`) | **NP21/W に従う**。資料 `0138h` の関連欄も `0030h` / `0032h` を挙げている。実機で確かめる必要がある (票 S5) |
| FIFO モードの割り込みマスク | 記述なし (`0136h` は「参照」で許可/禁止ではない) | `0035h` 以外でマスクしない | **両モードとも `0035h`** を使う |
| `0136h` の読み | 「割り込み要因の取得」 | 読むと `irqflag` を畳んで `pic_resetirq(4)` | **FIFO モードの IRQ ハンドラの最後に 1 回読む**。読まないと同じ要因で割り込みが上がり続ける |
| `013Ah` の速度表 | 7 段 (9600〜115200) | `speedtbl` が完全に一致 | 食い違いなし |

## 往復 3 — 1 バイト 2ms の固定費 (実機実測から)

実機で V･FAST 38400 は効いたが、実効は **9600: 389 B/s / 38400: 437 B/s** で、
速度に依らない 1 バイト約 2ms の固定費が残っていた。真因は
**`cpu_calibrate()` が丸めに負けていたこと** — 詳細と直し方は
[`cpu_calibrate_tdd.md`](cpu_calibrate_tdd.md)。こちら側では 2 つ直した:

| # | 直したもの | なぜ |
|---|---|---|
| 1 | `serial_putchar` の予算を **µs の数え上げから `tick_count` へ** | `waited += SER_TX_POLL_US` は「`cpu_delay_us(5)` が本当に 5µs 待つ」に寄りかかっていた。校正が 1/10 になっていた実機では 2083µs のつもりの予算が約 200µs で尽き、9600 の 1 文字時間 (1.04ms) すら待てずに `_halt()` (実測 ≒2ms) へ落ちていた。**tick は PIT が進める実時間なので、校正が何倍ずれても予算は狂わない** |
| 2 | 予算の下限を **3 tick (保証 20ms)** に | 2 文字時間そのものは 9600 でも 2083µs で 1 tick に収まるが、待つ相手は回線だけではない。ホスト観測では **16ms ごとに塊で届いていた** (FTDI の遅延タイマ)。これをまたげない長さだと結局 hlt に落ちて元に戻る |

**IF=0 で呼ばれたときは tick が進まない** ので、そこだけは回数上限
(`SER_TX_SPIN_MAX` = 20 万) でスピンして諦める。`_halt()` は IF=0 では二度と
起きないので踏まない。判定には `include/io.h` に足した `_irq_enabled()` を使う
(`irq_save()` は cli する副作用があり、戻り値は不透明という契約なので使えない)。

## 設計変更 (往復 6) — 暗黙の推定をやめて `serial ack` にした

往復 1〜3 は「受信した」「1 行往復した」でホストとの足並みを**推定**しようとして、
そのたびに穴が出た (切替行自身を数える ① / 断片を数える ② / EOT の送信失敗を数える ③ /
ローカルキーで解除される ④ / 本文が落ちて EOT だけ通る B2)。**穴が尽きないのは推定だから**
なので、PM が設計を変えた: 合図を明示にする。

| どこ | 新しい約束 |
|---|---|
| arm | rshell 経由の `serial N` が**速度を変えた瞬間** (応答 EOT の前後を問わず、EOT の成否も見ない)。ローカル CUI の `serial N` は arm しない (戻す相手が居ない) |
| 解除 | **新速度で `serial ack` の行を受けて実行したときだけ**。`serial ack` は `ACK <baud> <mode>` を 1 行返して EOT。**arm されていなくても同じ応答** (冪等) なので、ホストは何回でも投げてよい |
| 戻す | 期限 (500 tick) まで ack が来ない / **arm 中に rshell を抜ける** |
| ホスト | 切替 → 旧速度で最大 1 秒だけ待つ → 新速度で開き直す → `serial ack` を 0.5 秒ごとに最大 4 秒。**`ACK` を含む応答が読めるまで他の応答は読み飛ばす** (最初の EOT では判定しない)。読めなければ 6 秒待って旧速度で `ver` |

消えたもの: `serial_watchdog_line_qualifies` (4 つの真偽の論理積)、`lines_completed` の計数、
`rsh_line_terminated` / `_all_serial` / `_executed` の配線、`rsh_switch_pending` の遅延 arm。
**数えるものが無くなったので、数え損ねる穴も無くなった。**

| 見ているか | どこで |
|---|---|
| 判定 (`decide`) と状態 (`arm`/`ack`/`poll`/`leave`) | `serial_vfast_host.c` の `watchdog` / `watchdog_leave` |
| ホストの `ACK` 判定と投げ直し | `test_rshell_serial.py` の `ack` / `wait_ack` |
| 時間の辻褄 (話しかけ始め ≤ 1.5 秒 < 番犬 5 秒、ack の窓が番犬の内側) | 同 `switch` |
| `serial ack` の応答そのもの (`ACK <baud> <mode>` + EOT) | いいえ — 実機 / NP21/W |

## Codex レビュー 往復 3 (47e9680、最終) の 6 件をどう閉じたか

ROLES §5 の 3 往復に達したのでユーザー決裁「もう 1 往復で全部直す」。
PM が設計を確定し、①〜⑥ を一度に閉じた。

| # | 所見 | 閉じ方 | ホストで見ているか |
|---|---|---|---|
| ① (P1) | **切替行 `serial N` そのものが番犬の最初の 1 行に数えられ**、新速度で一度も通信しないまま解除される | arm を `cmd_serial` から **`rshell_end_reply()` の末尾** (切替行の EOT を送り終えた後) へ移した。`cmd_serial` は `rsh_switch_pending` を立てるだけ。line_done は arm より先に走るので**切替行自身は数に入らない** | `arm_after_switch` — 「切替行では数えない」を実物と同じ順で回して固定 |
| ② (P1) | 改行無しの断片 (化けた 1 バイト + 読み取り空振り) や ESC 中断も「行」と数える | 「行」の条件に **改行終端** を入れた (`rsh_line_terminated` は `while` を抜けた `ch` が `\n`/`\r` のときだけ 1) | `line_qualifies` — 16 通りの真偽表 |
| ③ (P1) | `serial_putchar()` に戻り値が無く、**EOT 送信失敗でも完了行に数える** | `serial_putchar` を `int` に (`SER_TX_OK` / `SER_TX_DROPPED`)。KAPI slot 38 の戻り値も `void` → `int` に広げた (**スロット番号も引数も不変** = ABI 互換。cdecl の EAX は呼び手の scratch)。`rshell_end_reply` は EOT の戻りが OK のときだけ数える | `line_qualifies` — `eot_sent = 0` で落ちること |
| ④ (P2) | `rsh_getch()` の serial → kbd **2 度読み**で、間に届いたバイトはシリアル由来の印が付かない | KAPI v57 に **`kbd_trygetchar_local()`** を足した (cooked リングだけ。シリアルも注入リングも見ない)。`rsh_getch` はシリアルを **1 回だけ**読み、空ならローカル専用の口を読む — 窓が消える | いいえ (KAPI の分岐。実機・NP21/W で見る) |
| ⑤ (P2) | 遅れた切替 EOT を新速度の `ver` の失敗と読んで正常な接続を放棄 | `serial N` の後、**旧速度でエコー行 `> serial N` と EOT を最大 5 秒待ってから**新速度へ移る。新速度の確認は `ver` の応答に `Build:` があり**かつ**エコー行が `> ver` であること | `test_rshell_serial.py` の `probe` / `switch` |
| ⑥ (P2) | `exit` のエコー無しを desync と誤判定、`startswith` が行境界を見ない | `check_echo` は **行全体** (`"> " + cmd`) で比較。`exit` は `NO_ECHO_CMDS` で例外 | `test_rshell_serial.py` の `echo` |

**非 blocker 3 件も直した**: 「1 周 0.31 tick なので旧コードでも `elapsed` ≥ 1」は**生値と補正後の取り違え**だったので書き直した (生値は NP21/W でも 0 で、`if (elapsed == 0) elapsed = 1;` が桁を決めていた) / kselftest の整合性検査の `|| rounds > 1` が素通りだったのを「**2 周以上回ったなら 5 tick 以上測れている**」と「1 周なら結果は loops/ticks そのもの」の 2 本に分けた / ラスタのコメントを実装 (VSYNC 待ち・VRAM 転送・パレット out は IF=0 のまま、**開けるのは待ちのぶんだけ**) に合わせた。

## Codex レビュー 往復 2 (07b256c、Request changes) の 4 件をどう閉じたか

実機で 9600 = 952 B/s (回線上限) / 115200 = 3.2 KB/s まで出たあとの指摘。

| # | 指摘 | 閉じ方 | ホストで見ているか |
|---|---|---|---|
| B1 (P1) | 番犬が**行頭の先読み経路**の受信を数えない (ホストの `ver\n` が切替コマンドの終了処理中に届くと、`ver` は返るのに 500 tick 後に旧速度へ戻る) | 読み出しを `rsh_getch()` 1 本に集約し、先読み・`read_rest`・内側の待ちループの**どれもここを通す**。数えるのは `rshell_end_reply()` の 1 か所だけ | `watchdog` — 状態つきの口 (`arm`/`line_done`/`poll`) で「どの経路から来ても同じ」を固定 |
| B2 (P1) | **「任意の 1 バイト受信」は成功の証拠にならない** (`ver` は届いたが EOT が落ちた → ゲストは解除・ホストは旧速度へ → 二度と合わない。化けたバイトやローカルキーでも解除) | 解除条件を **「シリアル由来の有効な 1 行を処理し、その応答の EOT を送り終えた」** に変更。`serial_watchdog_decide(elapsed_ticks, lines_completed)` へ。由来は `rsh_getch()` が `serial_trygetchar()` を自分で先に見て判定するので**取り違えようがない** | `watchdog` — 1 バイトでは解除されない (バイトを数える口が無い) |
| B3 (P2) | `probe()` が**先行コマンドの EOT** を確認成功と取り違える (以後 1 コマンドずれる) | (a) `ver` の応答に **`Build:` が含まれるまで**見る、(b) `serial N` の直後に旧速度で EOT を 1 つ (最大 2 秒) 吸ってから切り替える、(c) `cmd`/`repl` の通常経路も `> <コマンド>` のエコー行を検査し、違えば `desync:` と出して終了コード 1 | いいえ (ホスト道具。実機で確かめる) |
| B4 (P2) | 校正が正しくなると `gfx_present_raster` の IF=0 区間が 7〜13 倍 (12〜48ms) に伸び、38400 以上の受信を取りこぼし **PIT の tick も失う** (送信予算の前提が崩れる) | `_raster_delay_open(&flags)` を入れ、**待ちのあいだだけ IF を戻す** (`irq_restore` → 待つ → `irq_save`)。IF=0 で守るのはパレットの 4 ポート連続 out だけ。3 つのループすべてに適用 | いいえ (割り込みの窓。NP21/W でスプラッシュの見た目を PM が確認) |

**非 blocker 3 件も直した**: (a) `SER_TX_SPIN_MAX` を「合計 20ms 相当」= `SER_TX_SPIN_BUDGET_US / SER_TX_POLL_US` = 4000 回に根拠つきで下げた (校正が正しくなったいま 20 万回は **1 秒/文字**)、(b) 「NP21/W は 1 周で 5 tick を超える」を実測 (`rounds = 16 / ticks = 5`) に合わせて書き直した、(c) kselftest に「打ち切りに当たったら失敗」「フォールバック値と一致したら失敗」を追加。

## Codex レビュー 往復 1 (4600a9d、Request changes) の 3 件をどう閉じたか

| # | 指摘 | 閉じ方 | ホストで見ているか |
|---|---|---|---|
| 1 (P1) | 切替中の IRQ4 とリングの競合 (`ser_head=0; ser_tail=0` の直後に ISR が書くと 1 バイト残る / ポート・マスクの逐次差し替え中に ISR が新ポートを古いビットで読む) | `serial_init_ex` の切替全体を `irq_save()` + `irq_disable(4)` → … → `irq_enable(4)` + `irq_restore()` で囲んだ。ISR 末尾の `0035h` 再許可は直値をやめて `s_mask_ien` (初期化が決めた値) を書く。**`kprintf` は危険区間の外へ出した** — rshell 中は kprintf がシリアルへ流れ、`serial_putchar` の `_halt()` が IF=0 で永久に止まるため | いいえ (I/O と割り込みの順序。実機・エミュレータでしか見えない) |
| 2 (P1) | `--fast` の失敗で戻れなくなる | 3 段: (a) ドライバが exact でない 8253 速度を **適用しない** (`SER_INIT_REFUSED`、ハードウェアには 1 バイトも書かない)、(b) ゲストの番犬が切替後 500 tick 無音なら元へ戻して EOT を返す、(c) ホスト道具が `ver` の往復で確かめ、失敗なら元の速度へ戻って再確認し終了コード 1 | (a) `refuse_inexact` / (b) `watchdog` で見ている。(c) は実機 |
| 3 (P2) | `--fast` の受動待ちで成功時も 15 秒止まる | `sync` の受動待ちをやめ、`ver` の往復 (5 秒) に置き換えた。成功なら 1 秒で返る。EOT の取りこぼしに依存しない | いいえ (ホスト道具。実機で計る) |

## まだ試験していないこと

- **`0138h` / `013Ah` を実際に叩く順番**と、そのあと 8251 のモード/コマンドが
  `0132h` で効くこと。NP21/W では速度が変わらないので確かめられない (票 S3 / S5)。
- **IRQ ハンドラの FIFO 汲み出し** (16 バイト / `0136h` の ack)。
  **16 バイトで抜けたあと残量があるときに再割り込みが来ることは資料で証明
  できない** (`0136h` は「割り込み要因の取得」としか書かれておらず、
  FIFO の残量と割り込みの関係は「詳細不明」)。NP21/W は受信リングの使用量が
  3/4 を超えたときに `irqflag = 2` を立てる実装だが、実機がそうとは限らない。
  実機で大きなファイルを `hexdump` して取りこぼしが無いかを見る (Codex
  レビューの非 blocker 指摘 1)。
- **`0132h` のエラービット `SER_FSTS_ERR` = 0x38 の bit3**。資料は bit3 を
  「不明」とし、NP21/W は「パリティ」とする。どちらでも「エラーが立ったら
  コマンドを打ち直す」だけなので害は出にくいが、**資料で証明できていない**
  (Codex レビューの非 blocker 指摘 2)。
- **`0132h` へのコマンド書き込み**。資料は `[READ]` としか書かず、根拠は
  NP21/W の `rs232c_bind` (`0x132` の out を `rs232c_o32` に繋ぐ) だけ。
  実機で 8251 のモード/コマンドが `0132h` で効くかは確かめていない
  (Codex レビューの非 blocker 指摘 3)。
- **送信ループの実効速度**。目標は 9600 で 900B/s 以上、115200 で 8KB/s 以上
  (票 S5) — 実機でしか測れない。往復 3 で校正と予算の両方を直したが、
  **2ms の固定費が本当に消えたかは未確認** ([V4])。
- **切替区間の排他そのもの** (`irq_save` / `irq_disable(4)` / `s_mask_ien`)。
  競合は「ISR が特定の 2 命令の間に入る」ときだけ起きるので、ホストでは作れない。
  見るなら実機で `serial 115200` → `serial 9600` を繰り返して 1 バイトのずれが
  出ないことを確かめる (票 S5 / S6)。
- **番犬が実際に戻すところ** (`ser_wd_poll` → `serial_init`)。判定と状態遷移は
  `watchdog` / `watchdog_leave` で押さえたが、rshell のループへの配線
  (`cmd_serial` の arm、`serial ack` の応答、抜け口の `ser_wd_leave`) と
  EOT の返しは実機。
  **FIFO 非搭載機を用意できないので、`serial 115200` が拒否される経路も実機待ち。**
- **`gfx_present_raster` の IF を戻す窓** (B4)。待ちの途中で割り込みが入ると
  ラスタの位置が数十µs ずれうる。呼び手は `kernel/boot_splash.c` の 2 か所だけ
  なので、**スプラッシュの見た目**で確かめる (NP21/W)。
- **ホスト道具の `Build:` 判定・EOT の吸い出し・エコー検査** (B3)。pyserial と
  実際の COM ポートが要る。
- **ホスト道具の `serial ack` の投げ直しと失敗時の戻り** (`switch_speed`)。
  判定 (`ack_ok` / `wait_ack`) はホストで固定したが、実際の往復には pyserial と
  COM ポートが要る。
