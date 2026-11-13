> 出所: ~/os32-tmp/run/tick-b/DESIGN.final.md (2026-11-09、Claude 起草・codex astra の 3 回のレビュー)。
> 状態: **現行 (2026-11-09)** — 1kHz tick の段 A2・B と winmm のタイマの確定設計 (TICK_1KHZ_DESIGN の続き)。段の状態は票 TASK_TICK_1KHZ が持つ。 tick_clock の口と段の順は [TICK_ARCH_DESIGN](TICK_ARCH_DESIGN.md) (2026-11-12) が本稿より優先。

# tick-b r3: 1kHz tick の次の段 (A2・B) と winmm のタイマ

## r2→r3 の変更 (REVIEW.r2.md の ID ごと、main 13a4ffdd)
- P1-LINK_IDLE: `net/link.c:59` の `LINK_IDLE()` (`link_selftest.inc:22,34,45,61` の待ち) にも IF=1 の協調処理を入れる。既存の link の自己試験を受入に加える (§2-4・A2-c7・§6)。
- P1-IRQ の判定: IRQ 文脈かどうかは `kctx_irq_depth` だけで決める (`nic.in_irq` は使わない)。slot4 経由の送信 (`net_tx` → `ne2k_send`) を故障注入で確かめる (§2-4・§6)。
- P1-IF=0 の経路: IF=0 の経路 (`net_tx` の `net/nic.c:119`、`ne2k_leave` の `ne2000.c:204-215`) では送信列に積むことと印だけを行い、重い再処理は IF=1 の bh に移す。故障試験は IF=0 の全経路を対象にする (§2-4・§6)。
- P1-予算の上限: slot2〜4 の上限を、固定処理 (slot2・demux) と後続処理 (deadlines・timers・link_notify_tick) の最大費用を予約した形で定義し直した (§2-3・§6)。
- P2-B6 のアンカー: B6 に、`test_time_math.py:53-63`・`:70-77` を含む全アンカーと、fixture・期待値・tdd の更新 (time_math_host.c・pit_clock_host.c・dep_dir_b_timer_host.c・recover_t2_host.inc・splash_probe_host.c・*_tdd.md) を割り当てた (§5)。


## r1→r2 の変更 (REVIEW.r1.md の ID ごと、行番号は main 13a4ffdd)
- P1-GUI: GUI の Sleep(1) を 1ms で保証する。WM の再開の印をジフィから ms に変える (新しい poll_clock.rs と multiapp.rs、§4-1・G1/G2)。R10 を追加。
- P1-予算の開始: 予算の開始は `tick_dispatch` の 1 か所だけ (slot2〜4 で共有)。IRQ5 と foreground では掛けない (§2-3)。
- P1-最低進捗: 予算を越えて進めてよいのは 1 ジフィに 1 単位まで (全 slot で共有)。RX は予算の前半、TX は全体まで使う。保証の 1 単位は RX と TX がジフィの偶奇で交代する。合否は slot2〜4 の合計で 200µs を判定する (§2-3・§6)。
- P1-異常復旧: RDC 待ち 4ms・reset 待ち 20ms を含む NE2K の重い所は協調処理に移す (U4 = b)。故障の注入を受入に加える (§2-4・§6)。
- P1-B4 単独: 未 config の step は旧動作 (毎 IRQ でジフィ +1) にする。tick_clock の段 C1・C2 は 1 つの統合の枝で一緒に入れる (§3-2・§5)。
- P1-U4: (b) を採る。契約は緩めない (§2-4)。
- P2-R6: 「ワンショット自体が不可能」を「固定周期・補間時計との併用が不可」に限定した (R6)。
- P2-fast_fn: U3 はユーザー決定 (b)。fast_fn は照合と通知だけに限る。所有者・解除・周回比較・V86 中の挙動・負荷試験を定めた (§3-5)。
- P2-PCM fallback: 64bit のジフィ時計と PCM・import の段を戻した (§3-3・B15/B16)。
- P2-mmt fallback: 時計を切り替えるときの連続性と単調性を定めた。成功→失敗→復帰を試験する (§4-2)。
- P2-U2: 期限は周期の整数倍で進める。上限は 1 イベントにつき 1 回の配りで 8 回、超えた分は missed に数える。試験は時計を逐次進める (§4-2・§6)。
- P2-kill 中の追いつき: 各呼出しの後に ID の世代と live/dying を確かめ直す。同じ配りの中で生まれたイベントは配らない (§4-2)。
- P2-計測の置き場: 計測先を `ne2000_data.inc:83,240` と `ne2000.c:250` に移した。PIO だけの時間と、待ち・再試行込みの時間を分けて測る (§2-2)。
- P2-区間の和: 同じ呼出しの中で区間の和と全体を照合する (§6)。
- P2-境界の試験: 予算ちょうどの場合と、入口で既に予算を越えている場合を加えた (§6)。
- P2-既存の試験: `test_pit_clock.py:52-56` と `test_time_math.py:53-63` のアンカーの付け替えを B5・B6 に割り当てた (§5)。
- P2-B14: 既定 100Hz のまま試験用の 1000Hz 像を作る段 (B17) を加え、既定の切り替えは合格の後にした (§5)。
- P3-.rs: `.rs` も監査に含めた。WM は「要対応」に分類した (§3-4)。

前提 (議論しない): シングルタスク・協調切替、9801 互換は考えない、整数演算が第一、[C1]〜[D3]・[ABI]・[L1]、1 機能 1 ファイル。
既決: T1K の U1 (物理 1kHz・論理 100Hz の二周期)・U2 (スプラッシュは契約の外)・U3 (閾値一式)。サウンド遅延 20ms 台、入力の時刻印 1ms。合成器の起こしは 2026-11-09 のユーザー決定で「1ms ごとの期限照合」に変わった (11-05 の PIT ワンショットを置き換える)。
本稿は tick1k-design/DESIGN.final.md (以下 T1K) の §3〜§5 を正とし、**差分だけ**を書く。T1K と食い違う所は本稿を優先する。

## 1. 実物の確認 (食い違い 10 件)
| # | 票・既存設計の前提 | 実物 (file:line) | 本稿の扱い |
|---|---|---|---|
| R1 | T1K §6: `kernel/time/pit.c`・`pit_math.c`・`time_math.c` を直す | PIT は `platform/pc98/pit.c:30`・`pit_math.c:49` (`hz != PIT_HZ` で拒否)・`pit_time.c:35` (`time_us_from`)・`clock_source.c:17` に移った。`kernel/time/time_math.c` は `time_clamp` だけ | §3 の変更先を platform/pc98 側に付け替える |
| R2 | T1K §4: tick_count の定義は idt.c | 実際は `kernel/time/tick.c:3` | tick.c を tick_clock の置き場にする |
| R3 | T1K D4: ISR は isr_stub.asm:453〜468 | 実際は `isr_stub.asm:493` (inc)・`:497` (timer_handler)・`:501-503` (EOI)・`:506` (V86_REFLECT)・`:510-516` (timeout) | 行番号を更新する。手順は T1K §4 のまま |
| R4 | T1K §4 は tick × period_us の積を禁じた | `pit_time.c:52` がまだ `tick × period_us`。1kHz では −100ppm ずつずれる | §3-3 で有理式に置き換える |
| R5 | T1K §5 の監査台帳 | 台帳に無い C の利用者 5 件 (`geforce_module.c:77`・`splash_raster_run.h:7`・`kernel_boot_cpu.inc:55`・`sfs_proto.h:167`・`kbd_watch.h:17`) と、`.rs` で get_tick を使う 19 ファイル。`snd_engine.c` は hal-av R0 で消える | §3-4 |
| R6 | hal-av §9-4: 代替は「PIT のワンショット」 | IRQ に繋がるカウンタは #0 だけ (`include/pc98.h:129-135`、#1 はビープ、#2 は RS-232C)。#0 をワンショットにすると、mode 2 の固定周期とそれに依る補間時計 (sys_time_now)・ジフィとは**併用できない** (ワンショット自体は可能) | ユーザー決定で 1ms の期限照合に置き換えた (§3-5) |
| R7 | 票 §1: A2 の削減値は A の実測後に決める | A1 は NIC 負荷を流していない。slot4 の 118.7µs は無負荷の値で、制御フレームの送信の PIO (`net/link.c:410-419` → `ne2000_data.inc:266`) と推測 | A2-a で測る |
| R8 | 実機の NIC | Ra266 は Intel 82557 (bus master、RA266.md:29)。LGY-98 の PIO は NP21/W だけ | 契約は両方の機械に同じものを掛ける (U4=b) |
| R9 | winmm に新しい KAPI が要る想定 | `sys_time_now` (kapi.json:1649) と `sys_yield` (:1509) が既にある | KAPI の追加 0 (§4-1) |
| R10 | 「GUI は park → WM が起こす」で 1ms になる想定 | WM は同じ tick に 2 回起こさない (`multiapp.rs:860,874,945`、`tick_now()`=get_tick `:576`)。このままでは GUI の Sleep(1) は約 10ms になる | G1/G2 (§4-1) |

参考: IRQ0 の worst 202.5µs = 短経路 17 + timer_handler 184 (主は slot4 の 118.7)。IF=0 常駐の 6115.8µs は出所が分からない。raw ASM・BIOS の中の配送阻害と、外部基準による欠落の確認は段 A でもまだ取れていない。

## 2. 段 A2: IRQ0 の負荷を減らす
### 2-1. 何が重いか
- `net_tick` (`net/nic.c:178`) は IF=0 で動く。demux (memcpy で最大 8 件) → `link_tick` (`net/link.c:507`) の順に、rx_pump 16 件・deadlines・timers・tx_round・notify を回す。
- NE2K の PIO は 3 か所にある。slot2 の watchdog (`ne2000_irq.inc:95` → `ne2k_poll`)、IRQ5 (`ne2000_irq.inc:54` の `service(NE2K_IRQ_BUDGET)`)、IRQ0 の中の `link_tx_round` → `ne2k_send` → `dma_write`。どこからでも RDC 待ち 4ms (`ne2000.c:52,229`) と reset 待ち 20ms (`ne2000.c:50`、`ne2000_data.inc:225` → `ne2000_control.inc:59`) に到達し得る。
- 二周期では slot はジフィ (10 IRQ に 1 回) だけ走る。問題は 1 回の最大と、そのせいで落ちる IRQ0。
### 2-2. A2-a 測る (診断像だけ)
| 測る物 | 置き場 | 出力 |
|---|---|---|
| net_tick の 6 区間 (demux / rx_pump / deadlines / timers / tx_round / notify) | `net/nic.c`・`net/link.c` の `#ifdef TICK_PROBE` (TP_NET0..5) | **同じ呼出しの**区間の値と全体を 1 組で記録し、全体が worst の回の組を出す |
| NE2K の rx_one・ne2k_send・dma_transfer | `ne2000_data.inc:83` (rx_one)・`:240` (ne2k_send)・`ne2000.c:250` (dma_transfer) | PIO だけの時間と、RDC 待ち・再試行込みの時間を分けて記録。件数・バイト・呼んだ文脈 (IRQ0/IRQ5/foreground) |
| IF=0 の worst の呼び手番地 | `tick_probe.c` の `tick_probe_if_begin` で `__builtin_return_address(0)` | 6115.8µs と 29.7ms (V86) の出所 |
| 負荷の追加 | `~/os32-tmp/run/tick-a/harness/run_loads.py` に nic (lanfs 4MB) と serial (9600bps 受信 + SerialDiag) | A1 と同じ表 |
### 2-3. A2-b IRQ0 の予算
```c
/* kernel/irq/tick_budget.h — 予算を数えるのは tick_dispatch の中だけ */
void tick_budget_config(void);    /* 時計設定後、IF=0/EE=0 で 4 定数を一度換算する */
void tick_budget_begin(void);     /* 予算を分け合う最初の slot (ARCH_TICK_BUDGET_FIRST_SLOT、PC-98 は NE2K=2) の前。slot0・1 (FM・PCM) は B の外で、IRQ0 の worst の「他の slot」に入る。基点 = arch の count、active=1、taken=0 */
void tick_budget_end(void);       /* tick_dispatch の最後。active=0 */
int  tick_budget_take(u32 share_cnt, u32 tail_cnt); /* active でなければ常に 1 (IRQ5・foreground は掛けない)。
                                         経過 + tail_cnt < share_cnt なら 1。越えていても、このジフィでまだ
                                         1 単位も取っておらず、かつ自分が保証の当番なら 1 (取ったら taken=1) */
u32  tick_budget_cnt(u32 us);     /* B/2・B・tail_rx・tail_tx の換算済み count (除算なし) */
#define IRQ0_SLOT_BUDGET_US 120u  /* slot2〜4 で共有。F_slot2 + F_demux = 25.4 ≤ 60 − 22.4µs */
#define NET_TAIL_RX_US 23u       /* deadlines + timers + notify: 実測 14.9µs ×1.5 を切り上げ */
#define NET_TAIL_TX_US 17u       /* link_notify_tick: 実測 11.0µs ×1.5 を切り上げ */
/* 当番: tick_count が偶数なら RX、奇数なら TX。
 * RX: take(cnt(B/2), cnt(NET_TAIL_RX_US))、TX: take(cnt(B), cnt(NET_TAIL_TX_US)) */
```
- 固定処理 (上限の件数があり、止める条件を持たない所) は予算で止めずに予約する: slot2 (bh 化の後は ISR の読みと印だけ)・demux (`NET_RX_SCAN_MAX` 8 件の memcpy)・deadlines (8 handle)・timers (8 handle)・notify (制御 2 件を送信列に積むだけ)。それぞれの最大費用 F_i を A2-a で測る。
- 上限の定義: slot2〜4 の合計 ≤ B + U。ここで U = 1 単位の最大 (保証の 1 単位の越え)。成り立つ条件は 2 つ: (1) RX より前の固定処理 F_slot2 + F_demux ≤ B/2 − tail_rx、(2) tail の定数 ≥ 実際の後続の最大。どちらも A2-a の値で検査し、満たさなければ B を見直す (PM)。
- 1 単位は、`link_rx_pump` の 1 フレーム、または `link_tx_round` の 1 送信 (送信列に積むだけで memcpy のみ、§2-4)。U ≤15µs を A2-a で確かめる。
- IRQ0 の worst ≈ 短経路 17 + B 120 + U 15 + 他の slot ≈ 160µs (≤200)。
- 経過は arch の口 (x86: PIT #0 の count、PowerPC: TB) から取る。tick_dispatch の中は 1 周期を超えないので、折り返しの補正は 1 回で足りる。
### 2-4. NE2K の重い所を協調処理へ (U4 = b、契約は緩めない)
```c
/* kernel/idle_work.h — 協調処理の 1 口。_irq_enabled() && kctx_irq_depth==0 のときだけ動き、それ以外は何もせず戻る */
void idle_work_run(void);          /* 登録された仕事を 1 巡する。再入はしない */
int  idle_work_register(void (*fn)(void));   /* 最大 4 件 */
/* drivers/net/ne2000: 重い処理 (rx_drain・dma_write・wait_rdc・hw_reset・reinit・service) を走らせてよいのは
 * ne2k_bh() の中だけ。ne2k_bh は IF=1・kctx_irq_depth==0 の idle_work から呼ばれ、ne2k_enter で
 * busy=1・IMR を閉じてから IF=1 のまま PIO と待ちを行う (IRQ0・serial はその間も入る)。
 * それ以外の入口は、文脈 (IRQ か foreground か) を問わず IF=0 で走るので、印だけを行う:
 *   - IRQ5 (ne2000_irq.inc:54)・slot2 (:95): ISR を読んで ACK し、need_rx/need_tx/need_reinit を立てる
 *   - ne2k_send (net_tx が nic.c:119 で IF=0 にして呼ぶ。slot4 からも foreground からも): ソフトの送信列
 *     (NE2K_TXQ_SLOTS 4) に積むだけ。満杯なら BUSY (link 側の既存の「次の周回」に乗る)
 *   - ne2k_leave (ne2000.c:204-215): 再確認の loop の中の service() を外し、irq_pending・CURR の差を見て印を立てるだけ
 * IRQ 文脈の判定は kctx_irq_depth だけで行う (nic.in_irq は slot2 の終わりで 0 に戻るので使わない) */
int net_tx(const void *frame, u32 len);  /* irq_restore の後、kctx_irq_depth==0 なら idle_work_run() (IF=1 で送り出す) */
```
- 呼ぶ所 (hlt の直前): `kernel/sys.c:41` (sys_halt。GUI の WM は 1 周ごとに通る)・`exec/exec_schedule.inc:150` (sys_yield の先頭、geforce_foreground_poll の隣)・`drivers/input/kbd.c:734,770,792`・**`net/link.c:59` の `LINK_IDLE()`** (`do { idle_work_run(); _idle(); } while (0)`。`link_selftest.inc:22,34,45,61` の待ちが、送受が進まずに timeout しないようにする。host 試験用の `link_test_idle` (:46-49) は変えない)。
- 制限: アプリが syscall も待ちも無しに計算している間は NIC が止まる。リング 16KB が溢れたら、既存の OVW 復旧を次の協調処理で行い、link の RTO で再送される。この転送量は受入で測る。
- e82557 (実機) は bus master なので PIO が無い。予算 (§2-3) だけ掛ける。異常復旧の待ちが IRQ の中に無いことは A2-a で確かめる。

## 3. 段 B: PIT を 1kHz に (二周期)
### 3-1. 単位の名前
| 名前 | 中身 | 使ってよい所 |
|---|---|---|
| **ジフィ** | `tick_count`・`get_tick`・`PIT_HZ`=`OS32_TICK_HZ`=100・`TICK_MS`=10 | 既存のすべて。新しいコードの期限も原則これ |
| 物理 IRQ | raw 64bit・`PIT_IRQ_HZ` (x86)・`PM_DEC_HZ` (PowerPC) | tick.c・pit*.c・clock_source.c・recover_profile.c・kselftest、PowerPC の `kernel_dec.c`・`platform/powermac/clock.c`・`init/ppc_kernel.c` の起動の照合だけ。時刻として数えない |
| ms 時計 | `tick_clock_ms()`、u32 | 入力の時刻印・合成器の期限 |
| µs 時計 | `sys_time_now` (公開) | 互換層・計測・WM の再開の印 |
### 3-2. 状態と関数 (両 arch 共通の kernel/time/tick.c、新しい kernel/time/tick_clock.h)
口・検査・呼び手は [TICK_ARCH_DESIGN](TICK_ARCH_DESIGN.md) §2 が正。物理の周期は num/den 秒で受ける。
```c
#define TICK_STEP_JIFFY 1u
int  tick_clock_config(u32 period_num, u32 period_den); /* 物理の時計を設定する側が起動中に、IF=0/EE=0。x86 は reload/clk_hz、PowerPC は 1/PM_DEC_HZ */
u32  tick_clock_step(void);   /* ASM から毎 IRQ、IF=0。bit0 = ジフィが進んだ */
void tick_clock_raw(u32 *lo, u32 *hi);          /* IF=0 の呼び手だけ */
u32  tick_clock_ms(void);
void tick_clock_jiffy64(u32 *lo, u32 *hi);      /* 周回しないジフィ (PCM の fallback 用) */
volatile u32 tick_count; static u32 tick_hi, raw_lo, raw_hi, jif_phase, ms_phase, ms_now, step_jif, step_ms, clk; /* clk = period_den、step_jif = num*OS32_TICK_HZ、step_ms = num*1000 */
/* step: if (!clk) { if (!++tick_count) ++tick_hi; return 1; }    ← 未 config は旧動作 (P1-B4)
 *       if (!++raw_lo) ++raw_hi;
 *       ms_phase += step_ms;  while (ms_phase >= clk) { ms_phase -= clk; ++ms_now; }
 *       jif_phase += step_jif; if (jif_phase >= clk) { jif_phase -= clk; if (!++tick_count) ++tick_hi; r = 1; }
 *       if (fast_fn) fast_fn(ms_now);  return r;   (step_ms = reload*1000 ≤ 24576000 で溢れない) */
```
- x86 の ISR (`isr_stub.asm:493`): `inc` を `call tick_clock_step; test eax,1; jz .phys_only` に置き換える。ジフィの回は今の順 (timer_handler → EOI → V86_REFLECT → timeout) のまま。`.phys_only` は EOI だけを出して `.leave` へ。
- PowerPC の DEC (`arch/ppc/kernel_dec.c:56-61`): 経過周期の loop で `if (tick_clock_step() & TICK_STEP_JIFFY) tick_dispatch();` ([TICK_ARCH_DESIGN](TICK_ARCH_DESIGN.md) §2-2)。
- `pit_math.c:49` は 100 と 1000 を受け、reload は最近接 `(clk + hz/2)/hz`。`kernel_boot_cpu.inc:55` は `platform_clock_init(PIT_IRQ_HZ)` を呼ぶ。`tick_clock_config` は物理の時計を設定する側が呼ぶ: x86 は `platform/pc98/clock_source.c` の `platform_clock_init`、PowerPC は `ppc_dec_start`。
- 有効化の単位: 段 C1 (tick_clock.h・tick.c)・C2 (ISR・clock_source) は 1 本の統合の枝で一緒に入れる ([TICK_ARCH_DESIGN](TICK_ARCH_DESIGN.md) §5-1)。途中の像でも、未 config の分岐と旧 tick_count の読み出しが両立するので起動する。
### 3-3. µs 時計と PCM の fallback
```c
void time_us_from64(u32 raw_lo, u32 raw_hi, unsigned count, unsigned reload, u32 clk_hz, u32 *lo, u32 *hi);
/* cycles = raw*reload + (reload - count); us = cycles/clk*1e6 + (cycles%clk)*1e6/clk */
```
- x86 の `clock_source.c:37` は `tick_clock_raw` を読む (B7')。PowerPC の µs 時計は TB のまま (`platform/powermac/clock.c:72-84`)。pending の加算は 64bit にする。period_us は表示だけに使う。
- PCM: `pcm_cs4231.c:187` の `tick_count * PCM_US_PER_TICK` を `tick_clock_jiffy64` × 10000 にする (497 日で戻らない)。`tools/prototypes/pcm_imports.def` に import を足し、生成器で追従させる。
### 3-4. 100Hz 前提の利用者 (T1K §5 + R5)
| 所 | 処置 |
|---|---|
| C の 4 件 (`geforce_module.c:77`・`sfs_proto.h:167`・`splash_raster_run.h:7`・`kbd_watch.h:17`) | ジフィのままで正しい。注記を 1 行ずつ足す |
| `kernel_boot_cpu.inc:55` | `PIT_IRQ_HZ` を使う |
| `cpu_calibrate.c:40`・`fm.c:346` | 維持。校正の誤差は受入で測る |
| `recover_profile.c` | T1K D7 の累積差の直し (B9) |
| `in2_time_ms` (hal-av §6) | `tick_clock_ms()` に替える (hal-av の段) |
| **WM `userland/gshell/src/multiapp.rs:576,860,874,945`** | **要対応**: 再開の印を ms に (G1/G2) |
| `.rs` のその他 (gshell の input・settings・typical_ops・gserial・handler・lib、userland/rust の 7 件、sdk/rust の 3 件) | 待ちの印・計測にジフィを使うだけなので維持。段 B の最後に grep で、分類の表に無い行が 0 であることを確かめる |
| 周回比較 (rshell.c:313・hello/main.c:36) | 差分の比較に直す |
### 3-5. 合成器の起こし: 1ms の期限照合 (U3 = ユーザー決定)
```c
void tick_fast_set(void (*fn)(u32 now_ms)); /* 所有者は 1 者だけ (drivers/sound/snd_wake_tick.c)。2 者目は BUSY */
/* snd_wake_tick.c: SndBackend の hw_timer_arm に CS4231 のタイマが無いときに使う */
static volatile u32 due_ms; static volatile u8 armed;
static void wake_fast(u32 now) { if (armed && (i32)(now - due_ms) >= 0) { armed = 0; v86_fm_timer_wake(); } }
int  snd_wake_arm(u32 after_us);  /* 0 = 解除。due = ms + ceil(after_us/1000)、armed=1 */
```
- fast_fn がしてよいのは照合と印だけ。I/O・待ち・合成はしない (≤1µs)。実処理 (Count・CSM・仮想 IRQ) は配り点、つまりゲストへ戻る前の通常の文脈 (hal-av §9-4 の C3) で行う。
- 解除: `v86_fm_session_end` (end・STOP・異常終了の回収) で `snd_wake_arm(0)` と `tick_fast_set(NULL)` を呼ぶ。
- 周回は u32 の ms を符号付きの差で比べる (半周 24.8 日まで)。
- V86 中も毎 IRQ に走る (ISR は VM=1 でも共通)。印を立てるだけなので、ゲストへの注入は既存の `v86_pic_should_inject` を通る。

## 4. winmm のタイマ
### 4-1. カーネルの口 (KAPI の追加 0) と WM
| 要る物 | 口 | 理由 |
|---|---|---|
| timeGetTime | `sys_time_now` / 1000 | µs の精度、1 回 約 5µs |
| QPC/QPF | `sys_time_now`、QPF = 1,000,000 | TSC は使わない |
| timeBeginPeriod | 無し | 物理は常に 1kHz |
| Sleep | `sys_yield` | CUI は hlt 1 回 (段 B の後は ≤1ms)。GUI は park → WM |
- **G1** `userland/gshell/src/poll_clock.rs` (新): `pub fn poll_now() -> u32` = sys_time_now の ms (失敗時は get_tick×10)。**G2** `multiapp.rs`: `tick_now()` を `poll_clock::poll_now()` に替え (`:860,945`)、規則を「同じ **ms** に 2 回起こさない」にする。WM の sys_halt は物理 IRQ (1ms) で起きるので、park したアプリは ≤1ms + WM の 1 周で再開する。session.rs:41 の注記 (100Hz) も直す。
### 4-2. libmmtime (userland/lib/mmtime、公開ヘッダは C89)
```c
#define MMT_QPF 1000000UL
#define MMT_EVENT_MAX 16
#define MMT_CATCHUP_MAX 8
#define MMT_PERIOD_MIN 1u
#define MMT_PERIOD_MAX 1000000u
typedef void (*MMT_CB)(u32 id, u32 msg, u32 user, u32 dw1, u32 dw2);
u32  mmt_now_ms(void);  int mmt_now_us(u32 *lo, u32 *hi);  /* now_us は常に 0 (成功) を返す */
int  mmt_begin_period(u32 ms);  int mmt_end_period(u32 ms);
u32  mmt_set_event(u32 delay_ms, u32 res_ms, MMT_CB cb, u32 user, u32 flags);  int mmt_kill_event(u32 id);
void mmt_deliver(void);  void mmt_sleep_ms(u32 ms);  int mmt_next_due_ms(u32 *ms);  void mmt_hold(int on);
typedef struct { u32 id, delay_us, next_lo, next_hi; MMT_CB cb; u32 user, missed, born_pass; u8 flags, live, dying; } MmtEvent;
```
- 時計 (mmtime_clock.c): 成功した読みのたびに `base_us` と `base_tick` (get_tick) を覚える。失敗 (AGAIN/NOSYS) したら `v = base_us + (get_tick − base_tick) × 10000`。出す値は `max(v, last_out)` (単調)。復帰の後も、実時計が last_out を越えるまでは last_out に留める。Sleep・イベントもこの値を使う。
- 配り (mmtime_event.c): 再入中か hold>0 なら何もしない。pass を +1 し、slot の順に回す。`born_pass == pass` のイベントは飛ばす。
  - PERIODIC は 1 イベントにつき、期限が来ている間 `cb` → `next += delay` を最大 8 回繰り返す。8 回の後も期限が来ていれば `n = (now − next)/delay + 1` とし、`missed += n`、`next += n × delay` (整数倍なので位相を保つ)。
  - ONESHOT は呼ぶ前に free する (live=0、gen を進める)。
  - 各呼出しの後に `e->id == id && e->live && !e->dying` を確かめ直し、崩れていたらそのイベントの追いつきを止める。kill は dying を立て、配りの終わりで free。古い ID の kill は INVAL。
- 配り点 (U1 = 含める): Sleep/SleepEx・WaitFor*・MsgWaitFor*・GetMessage/PeekMessage・**timeGetTime・QPC**。互換層の `compat_pump()` が `mmt_deliver(); sndl_pump();` の順に呼ぶ。TIME_CALLBACK_EVENT_* は互換層がイベントを立てる。
### 4-3. 大きさ
| 像 | 増える物 | 見積もり | 上限 |
|---|---|---|---|
| 製品 kernel | tick_clock・64bit 式・ISR・tick_budget・idle_work・snd_wake_tick・NE2K の分割 | text +2KB、lz4 後 +1.2KB | vmkernel.lz4 299,560 / 520,192B |
| 起動 FD | vmkernel.lz4 | +1.2KB | `make fd144` の容量検査 |
| 診断像・kdebug | TP_NET0..5・NE2K の 3 区間・呼び手番地 | kernel +0.6KB、kdebug +0.3KB | kdebug 42% |
| userland | libmmtime 約 2.5KB・gshell +0.2KB | — | /sys |

## 5. ファイルごとの変更 (1 ファイル = 1 段 = 1 実装者)
| 段 | ファイル | 新/変 | 行 | 依存 |
|---|---|---|---|---|
| A2-a1〜a2 | kernel/time/tick_probe.h・tick_probe.c | 変 | +12・+45 | 先 |
| A2-a3〜a4 | net/nic.c・net/link.c (計測点) | 変 | +8・+12 | a1 |
| A2-a5〜a6 | ne2000_data.inc (:83・:240)・ne2000.c (:250) (計測点) | 変 | +8・+6 | a1 |
| A2-a7 | kdebug/tick_probe.c | 変 | +35 | a1 |
| A2-a8 | run_loads.py (nic・serial) | 変 | +60 | PM |
| A2-b1〜b2 | kernel/irq/tick_budget.h・.c | 新 | 30・55 | A2-a の後 |
| A2-b3 | kernel/irq/tick.c (begin・end) | 変 | +3 | b1 |
| A2-b4〜b5 | net/link_rx.inc・net/link.c (take) | 変 | +4・+6 | b1 |
| A2-c1〜c2 | kernel/idle_work.h・.c | 新 | 20・40 | 先 |
| A2-c3 | drivers/net/ne2000/ne2000_irq.inc (印だけ) | 変 | +25 | c1 |
| A2-c4 | drivers/net/ne2000/ne2000_data.inc (送信列・bh) | 変 | +50 | c1 |
| A2-c5 | drivers/net/ne2000/ne2000.c (bh の登録・`ne2k_leave` を印だけに・wait_rdc/hw_reset の入口で IF=1 と depth==0 を検査) | 変 | +30 | c4 |
| A2-c6 | kernel/sys.c・exec/exec_schedule.inc・drivers/input/kbd.c (呼ぶ所、各 1 段) | 変 | +1・+1・+3 | c2 |
| A2-c7 | net/link.c (`LINK_IDLE`) | 変 | +2 | c2 |
| A2-c8 | net/nic.c (`net_tx` の後の idle_work_run) | 変 | +2 | c2 |
| A2-t1〜t2 | tools/tests/tick_budget_host.c・ne2k_bh_host.c (故障注入) | 新 | 130・150 | b2・c4 |
| C1〜C3 | tick_clock の口・x86 の 100Hz の結線・host 試験 ([TICK_ARCH_DESIGN](TICK_ARCH_DESIGN.md) §5-1) | 新・変 | — | 先 |
| B1 | include/memmap.h (`#ifndef PIT_IRQ_HZ` → 100)。`OS32_TICK_HZ` と assert は C1・C2 | 変 | +3 | C2 |
| B5 | platform/pc98/pit_math.c + `test_pit_clock.py:52-56` のアンカー | 変 | +15・+6 | B1 |
| B6 | platform/pc98/pit_time.c・.h + 変更する式を参照する全アンカー (`test_time_math.py:53-63`・`:70-77` ほか)・fixture・期待値・tdd (time_math_host.c・pit_clock_host.c・dep_dir_b_timer_host.c・recover_t2_host.inc・splash_probe_host.c・time_math_tdd.md・pit_clock_tdd.md)。旧式のアンカーが 0 件で、全変異が実行時に RED になることを確かめる | 変 | +35・+40 | C1 |
| B7' | platform/pc98/clock_source.c (`tick_clock_raw` を読む・64bit の pending) | 変 | +6 | B6 (同じ枝) |
| B9〜B10 | kdebug/recover_profile.c・kselftest/pit_setup.inc | 変 | +40・+15 | B5 |
| B11 | rshell.c・hello/main.c | 変 | +8・+2 | 独立 |
| B12 | 1kHz の結線の試験 (EOI 全 IRQ、V86 反射・timeout はジフィだけ)。純関数の試験は C3 | 新 | 80 | B6 |
| B13 | 注記 4 件 + session.rs:41 | 変 | +5 | 独立 |
| B15〜B16 | drivers/sound/cs4231/pcm_cs4231.c・tools/prototypes/pcm_imports.def | 変 | +3・+2 | C1 |
| B17 | build/select.mk (`TICK1K=1` で -DPIT_IRQ_HZ=1000 の試験像を build/out/tick1k/ に)。独自の C の recipe を持つなら profile の旗と `\| $(ABI_CONTRACT_ORDER)`、受入に `check-type-contract` (末尾の追記) | 変 | +10 | B1 |
| B14 | include/memmap.h の既定 100→1000 | 変 | 1 | B17 の像で全受入の後、PM |
| S1 | drivers/sound/snd_wake_tick.c (§3-5) | 新 | 50 | C1、hal-av の V0 |
| G1〜G2 | userland/gshell/src/poll_clock.rs・multiapp.rs | 新・変 | 25・+4 | 独立 |
| W1〜W4 | userland/lib/mmtime/mmtime.h・mmtime_clock.c・mmtime_event.c・mmtime_sleep.c | 新 | 50・70・170・45 | W1 が先 |
| W5〜W7 | build/libs.mk・sdk.mk / tools/tests/mmtime_host.c・test_mmtime.py / userland/diag/mmtimetest.c。libmmtime・mmtimetest の規則は profile の旗と `\| $(ABI_CONTRACT_ORDER)`、W5 の受入に `check-type-contract` (末尾の追記) | 変・新 | 12 / 260・150 / 120 | W2〜W4 |
| W8 | 互換層の winmm | 新 | 150 | 置き場が決まってから |

## 6. 受入 → 試験・変異 (無変異で exit 0、変異 1 行で exit 1)
| 受入 | 試験 (環境) | 変異 |
|---|---|---|
| 区間の和 = 全体 | 同じ呼出しで、区間の和と全体の差 ≤ 計測の費用 ×7 (NP21/W) | TP_NET2 の end を消す |
| IF=0 の呼び手番地 | 1.2ms の IF=0 を注入した関数の番地が出る (NP21/W) | 番地を 0 に固定 |
| 予算の境界 | host: 経過 = 予算ちょうどで止まる / 入口で既に予算を越えていても当番の 1 単位だけ進む / 2 単位目は止まる | `<` を `<=` に / 当番の判定を消す / taken を立てない |
| 予算の文脈 | host: active=0 (IRQ5・foreground) では常に 1 | active の検査を消す |
| 折り返し | host: 基点より count が大きい場合 | 補正を消す |
| slot2〜4 の合計 ≤ B + U、IRQ0 ≤200µs | 9+2 負荷 × 5 回 (NP21/W の LGY-98・実機の e82557)。固定処理 F_i と後続の最大が tail の定数以下であることも同じ記録で照合する | IRQ0_SLOT_BUDGET_US を 10 倍 / NET_TAIL_TX_US を実測の半分に |
| 後続の予約 | host: TX が share の直前で 1 単位を取る形で、notify の最大費用を足しても B + U 以内 | tail を take に渡さない |
| IF=0 で重い待ちをしない | host ne2k_bh: RDC が立たない・reset が終わらない偽の I/O で、**IF=0 の全入口** (IRQ5・slot2・slot4 の net_tx (depth=1)・foreground の net_tx (depth=0、IF=0)・ne2k_leave の再確認) が待たずに印だけ立てて戻る。偽の wait_rdc/hw_reset は IF=0 か depth>0 で呼ばれたら exit 1。IF=1 の bh で復旧する。NP21/W: 診断像の `ne2k_fault_rdc` で、lanfs の転送中に slot4 経由の送信で RDC を 1 回落とし、IRQ0 ≤200µs・IF=0 ≤800µs・復旧を確かめる | ne2k_leave に service() を戻す / 判定を nic.in_irq に戻す |
| link の自己試験 | 既存の `link_selftest` (kdebug) が bh 化の後も timeout せずに通る (NP21/W の LGY-98) | LINK_IDLE から idle_work_run を消す |
| NIC の転送量 | lanfs 4MB の時間が A2 前の 120% 以内 (NP21/W・実機) | bh を呼ぶ所を sys_halt だけに |
| ジフィ 100Hz・ms 1kHz | host: 4 つの reload で 60 秒分、期待値は有理式 (差 ≤1) | `while` を `if` に |
| 未 config | host: clk=0 で step が 1 を返し、tick_count が +1 | `!clk` の分岐を消す (止まらなければ timeout で exit 1) |
| raw・ジフィ の繰り上げ | host: 0xFFFFFFFE から 3 step | `++raw_hi`・`++tick_hi` を消す |
| µs 時計 | host: 49.7 日・497 日の合成値と 128bit の期待値 | period_us の積に戻す |
| PCM の fallback | host: tick_count が 0xFFFFFFFF→0 をまたいで単調 | tick_hi を無視する |
| EOI は全 IRQ | NP21/W の B17 像: 60 秒で IRQ0 ≈ 60000・timer_handler ≈ 6000 | `.phys_only` の EOI を消す |
| 時刻差 0.1% | B17 像 (NP21/W・実機) で外部 60 秒基準と比較 | ms_phase を捨てる |
| 期限照合 | host: 期限の前は印 0、期限で 1 回、周回の前後、解除後は 0。NP21/W: 登録したまま 60 秒で IRQ0 の短経路の worst の増分 ≤2µs、V86 中に印が立つ | 符号付きの差を `<` に / 解除を消す |
| 既存の時間の契約 | T1K §7 のまま (B17 像) | T1K のまま |
| Sleep(1) | W7: 中央値・99% とも ≤2.2ms (B17 像、CUI と GUI、NP21/W・実機。目標はユーザー決定 2026-11-13 で Windows 並み) | G2 の印をジフィに戻す |
| 時計の連続 | host: 成功 → 失敗 3 回 → 復帰で単調、逆行 0、復帰の後に追いつく | `max` を外す |
| 周期・追いつき | host: 1ms ずつ 1000ms 進めて 100 回 / 95ms 跳ばすと 8 回と missed=式どおり、next は t0 + k×delay | `next = now + delay` / 上限を外す |
| kill・再利用 | host: 追いつきの途中で自分を kill すると止まる / ONESHOT のコールバックの中で set すると新しい ID になり、この配りでは呼ばない / 再入 0 / hold>0 で 0 | 確かめ直しを消す / born_pass を消す |
| 大きさ | CI の `make all`・`make fd144` | — |

段の順 ([TICK_ARCH_DESIGN](TICK_ARCH_DESIGN.md) §5): 段 C (C1〜C3) → 段 P (PowerPC) と A2-a を並列 → 測る (PM) → A2-b・A2-c → A の再測定とゲート → B1・B5・B6・B7'・B9〜B13・B15〜B17・S1 → B17 像で T1K §7 → B14。W と G は B と並列で進め、1ms の受入は B17 の像で行う。

## 7. 未決
なし。U1・U2・U4 は PM が決め、U3 はユーザーが決めた (2026-11-09)。

## 反論
なし (REVIEW.r1.md の 18 件・REVIEW.r2.md の 5 件は、すべて直し案どおりに反映した)。

## 確定 (2026-11-09、astra の第 3 回)
- 第 3 回のレビューは「設計票は確定してよい」。直しなし。B・U・tail の数値は段 A2 の計測のゲートで確定する。

## 確定の後の追記 (2026-11-10、今の main cfb9cec3 との前提の照らし直し、PM 決定)
- **第 4 段 B の型の契約 (§5 の W5・W7・B17)**: `build/abi_contract.mk:18-22` が probe の order-only を製品の object 一覧と `userland/{cmds,tests,system}` の直の object に付け、`build/libs.mk:66・70` の共通の規則・各ライブラリの規則に `| $(ABI_CONTRACT_ORDER)` が付いた。`tools/tests/test_type_contract.py:153-226` は `make -n -B` の全 C の呼出しの char/enum の旗と probe の前提を検査する (covers `build/**/*.mk`、解析できない呼出しは fail closed)。W5 の libmmtime の規則・W7 の mmtimetest・B17 の `TICK1K=1` の別像は、既存の macro の規則を使えば自動で掛かる。手書きの recipe なら profile の旗と `| $(ABI_CONTRACT_ORDER)` を付ける。`check-type-contract` を W5・B17 の受入に加える。
- **PPC の選択集合 (§5 の C1・A2-b3・A2-c6)**: `kernel/time/tick.c`・`kernel/irq/tick.c`・`kernel/sys.c` は `tools/ppc/selected_success.json` の PPC の初回成功集合に入っている (範囲の前から)。新しいヘッダ `tick_clock.h` (`kernel/time/`)・`tick_budget.h`・`idle_work.h` は `kernel/` に置き、`tools/ppc/check_accept.py:11-18` の禁止の接頭辞 (`platform/pc98/*`・`drivers/*` ほか) を include しない。よって `tools/ppc/known_header_dependencies.json` は変えない (組を足すと `check_accept.py:54-55` で stale になる)。tick_clock の置き場は [TICK_ARCH_DESIGN](TICK_ARCH_DESIGN.md) §3。
