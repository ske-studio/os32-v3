# tick-arch: 1kHz tick を arch に依らない tick_clock の口に直す

> 出所: ~/os32-tmp/run/tick-arch/DESIGN_ADD.md (Claude 起草、本線の独立レビューと os32powerpc のレビューを反映、PM が確定)。
> 状態: **確定 (2026-11-12、独立レビューと os32powerpc のレビュー、ユーザー決定)** — 段の状態は票 `TASK_TICK_1KHZ` が持つ。

本稿は `TICK_1KHZ_DESIGN` (以下 T1K) と `TICK_1KHZ_NEXT_DESIGN` (以下 NEXT) への**差分**。食い違う所は本稿を優先する。
基点 main `170723d0`。file:line はこのツリー相対。
前提 (議論しない): シングルタスク・協調切替、整数演算が第一、[C1]〜[D3]・[ABI]・[L1]、1 機能 1 ファイル、段は 400 行以下・1〜3 ファイル。

## ユーザー決定 (原文)
- U1 (2026-11-05): 物理 1kHz・論理 100Hz の二周期。公開の tick と ABI は変えない。
- 2026-11-12 (選択「設計を直して共通部分を先に (推奨)」):
  1. TICK_1KHZ の設計を、arch に依らない tick_clock の口に直す。
  2. 段 B の共通部分 (tick_clock) を先に入れる。x86 の物理 1kHz への切り替えは最後。
  3. PowerPC は decrementer を物理 1kHz に戻し、tick_clock_step で論理 100Hz を作る。
- 2026-11-12 (問い「10ms をベースに、は (a) 内部と公開の tick は 10ms、割り込みだけ物理 1kHz か、(b) 全部 10ms か」への答え): 「(a) 内部は 10ms、割り込みは 1kHz」。U1 のまま。
- PM 決定 (2026-11-12、推奨どおり): U3 の閾値一式は今は PowerPC の DEC 1kHz に掛けない。QEMU では論理の rate と、raw と TB の照合を受入にする。PowerPC の閾値は G4 実機の段で決める (§7)。

## 0. 実物の確認 (本稿が拾った食い違い)

| # | 既存設計の前提 | 実物 (file:line) | 本稿の扱い |
|---|---|---|---|
| X1 | NEXT §3-2 は `tick_clock_config(reload, clk_hz)` で PIT だけを想定 | PowerPC の DEC は `tb_hz / hz` を整数の period と余りの fraction で配る (`sys/arch/ppc/kernel_dec.c`)。reload・clk_hz の組が無い | 物理の周期を有理数 `num/den` 秒で渡す口にする (§2) |
| X2 | NEXT §3-1「物理 IRQ を使ってよい所は tick.c・pit*.c・recover_profile.c・kselftest だけ」 | PowerPC は `kernel_dec.c` で `++tick_count; tick_dispatch();` を自前で行う。tick_count を進める所が 2 arch で別 | 進めるのは `sys/hal/time/tick.c` の `tick_clock_step` だけにする (§2-2) |
| X3 | B29 (`7d288722`) は PowerPC の DEC を 100Hz にして tick 単位を合わせた | `kernel_dec.c` (`tick_hz != 100`)、`sys/platform/powermac/clock.c` (`pm_tick_hz` = 100)、`sys/platform/powermac/ppc_kernel.c` (待ちは 1・+3 の論理 tick)、`ppc_trap.h` | DEC だけ 1kHz に戻し、論理 100Hz は tick_clock_step で作る。待ちの論理 tick の値は B29 のまま残す (§4) |
| X4 | NEXT B1・T1K §3 は SDK 共有ヘッダに `STATIC_ASSERT(OS32_TICK_HZ == PIT_HZ)` を置いた | `os32_kapi_shared.h` は `__KERNEL_BUILD__` で memmap.h を読み、PowerPC も `-D__KERNEL_BUILD__` (`build/targets/ppc-powermac.mk`)。`tools/ppc/kboot_audit.py` は `-E -dU` で PIT_HZ を**検査しただけ**の TU も落とす | SDK ヘッダには `OS32_TICK_HZ` をリテラルの 100 で置くだけにする。assert は x86 だけの `sys/platform/pc98/clock_source.c` に置く (既に `pit.h` と `os32_kapi_shared.h` を読んでいる) |
| X5 | NEXT §3-3「`clock_source.c` は `tick_clock_raw` を読む」 | PowerPC の µs 時計は TB から直に出す (`sys/platform/powermac/clock.c`)。tick に依らない | raw を µs に使うのは x86 の clock_source だけ。PowerPC は raw を起動の照合にだけ使う (§2-4) |
| X6 | 「sys/hal/time/tick.c は PPC の像に入るか」 | 入る: `build/sources/common.mk` の `C_COMMON_CORE` (sys/kern/irq/tick.c・sys/hal/time/tick.c)。host 試験 `kboot_dec.py (旧版 8d5313fc0345)` も tick.c を直に組む。`build/ppc.mk` の独立像 (`sys/arch/ppc/decrementer.c`) は tick.c を組まない | tick_clock は tick.c に置けば 2 arch で共通になる |
| X7 | B29 の理由「1ms の検査はシェル像で境目の読み違いにより揺らいだ」(PPC_SHELL_EXEC_DESIGN の B29) | 起動の検査は tick_count (論理) を数える (`sys/platform/powermac/ppc_kernel.c`)。QMP の観測 `kboot_tick` は `--defsym,kboot_tick=tick_count` (`build/targets/ppc-powermac.mk`) | 論理 tick は 100Hz のままなので、検査の間隔 (3 tick)・`tools/ppc/kboot_run*.py`・`kboot_dec_fixture.inc` の 100Hz の期待値は**変えない** |

## 1. 今の tick の道 (横並び)

| 項目 | x86 (PC-9821、main 170723d0) | PowerPC (PowerMac G4、B29 の後) | 本稿の後 (両 arch) |
|---|---|---|---|
| 物理の周期 | PIT #0 mode 2、`PIT_HZ`=100 (`sys/include/memmap.h`)。reload = clk/100 = 19968 / 24576 (`sys/platform/pc98/pit_math.c`)、`sys/platform/pc98/kernel_boot_cpu.inc` の `platform_clock_init(PIT_HZ)` | DEC、`pm_tick_hz()`=100 (`sys/platform/powermac/clock.c`)。period = tb_hz/100 + fraction (`kernel_dec.c`) | x86: 段 L の最後まで 100、最後に 1000 (`PIT_IRQ_HZ`)。PowerPC: 1000 (`PM_DEC_HZ`、§3) |
| 論理の周期 | 物理と同じ 100Hz | 物理と同じ 100Hz | 100Hz (`OS32_TICK_HZ`)。内部と公開の tick は 10ms。tick_clock_step の位相で作る |
| tick_count を進める所 | `sys/arch/x86/kernel/isr_stub.asm` `inc dword [tick_count]` (定義は `sys/hal/time/tick.c`) | `sys/arch/ppc/kernel_dec.c` `++tick_count` (追いつきの loop の中) | `sys/hal/time/tick.c` の `tick_clock_step()` だけ |
| callback (7 slot) | `isr_stub.asm` timer_handler → `isr_handlers.c` → `tick_dispatch` (`sys/kern/irq/tick.c`)、毎 IRQ | `kernel_dec.c` で `tick_dispatch`、経過 period ごと | 論理 tick の回だけ (step の bit0) |
| EOI / 再装填 | `isr_stub.asm` の `irq_stub_0` 毎 IRQ。V86 反射 `irq_stub_0`・timeout `irq_stub_0` も毎 IRQ | DEC の再装填 `kernel_dec.c` 毎例外。MPIC は通らない | x86: EOI は毎 IRQ、V86 反射・timeout は論理 tick の回だけ (T1K §4)。PowerPC: 再装填は毎例外 |
| 文脈 | IF=0 (割り込みゲート)、IRQ_ENTER で `kctx_irq_depth` +1 | MSR[EE]=0 のまま (`sys/arch/ppc/kernel_vectors.S`「EE stays clear」、`ppc_kernel_frame_complete` は IR/DR/RI だけ立てる)、`kernel_dec.c` で depth ±1 | 同じ。step は IF=0/EE=0・depth≥1 で呼ぶ |
| µs の時計 (sys_time_now) | `sys/platform/pc98/clock_source.c`: tick_count + PIT のラッチ、`pit_time.c` の `tick × period_us` | `sys/platform/powermac/clock.c`: TB の差を整数で µs に | x86: `tick_clock_raw` + ラッチの有理式 (NEXT §3-3、段 L)。PowerPC: TB のまま |
| ms の時計 | 無し | 無し | `tick_clock_ms()` (物理 IRQ から位相で 1ms 刻み) |
| 使い手 | KAPI get_tick (`kapi_generated.c`)・ktime_ticks・待ち・期限・FM/PCM/NIC/V86 (T1K §5) | exec の閉包・KAPI tick_count、`lan_diag.c`、`cpu_calibrate.c`、iso9660 の idle、起動の検査 `ppc_kernel.c`、QMP の `kboot_tick` | 変わらない (全部ジフィ = 10ms) |

## 2. arch に依らない tick_clock の口

### 2-1. 口 (sys/hal/time/tick_clock.h、内部 GNU11)
```c
/* 共通の物理 1kHz / 論理 100Hz の二周期時計。状態は sys/hal/time/tick.c が持つ。
 * 物理の周期は「num/den 秒」の有理数で受ける (x86: reload/clk_hz、PowerPC: 1/PM_DEC_HZ)。
 * 論理の周期 (ジフィ) は OS32_TICK_HZ=100、ms 時計は 1000Hz に固定 (U1: 公開の tick は変えない)。 */
#define TICK_STEP_JIFFY   1u
#define TICK_CLOCK_MS_HZ  1000u
#define TICK_CLOCK_EINVAL (-1)

int  tick_clock_config(u32 period_num, u32 period_den);
     /* 起動中、IF=0/EE=0。物理の時計を設定する側が、装置を書き換える前に呼ぶ。
      * 検査 (どれかに外れたら TICK_CLOCK_EINVAL、状態は変えない):
      *   num != 0、den != 0、
      *   num * OS32_TICK_HZ <= den             (1 step で高々 1 ジフィ。ASM の bit0 の契約)
      *   num <= 0xFFFFFFFFu / TICK_CLOCK_MS_HZ (step_ms が u32 に収まる)
      *   den <= 0xFFFFFFFFu - num * TICK_CLOCK_MS_HZ (phase + step が溢れない)
      * 成功で位相・ms を 0 にし、0 を返す。raw・tick_count・tick_hi は触らない。 */
u32  tick_clock_step(void);
     /* 物理の 1 周期ごとに 1 回。IF=0/EE=0、kctx_irq_depth>=1。bit0 = ジフィが進んだ。
      * 未 config: tick_count を +1 して 1 を返す (旧動作、NEXT P1-B4)。 */
void tick_clock_raw(u32 *lo, u32 *hi);     /* 物理 IRQ の 64bit 数。IF=0 の呼び手だけ */
u32  tick_clock_ms(void);                   /* u32 の 1 語読み。どの文脈でも可 */
void tick_clock_jiffy64(u32 *lo, u32 *hi); /* 周回しないジフィ。IF=0 の呼び手だけ (PCM の fallback) */
```
- 中身は NEXT §3-2 の step のまま。`step_jif = num * OS32_TICK_HZ`、`step_ms = num * TICK_CLOCK_MS_HZ`、`clk` を `den` に読み替える。`fast_fn` (S1) は段 L の S1 で足す (共通の口の段には入れない)。
- `OS32_TICK_HZ` は SDK 共有ヘッダにリテラルの 100 で置く (PIT_HZ で定義しない。X4)。
- 「Hz を 2 つ渡す」形 (phys_hz, logical_hz) を採らない理由: x86 の実周期は clk/reload で 1000µs ちょうどにならない (1.9968MHz で 1000.100160µs = 約 999.90Hz、−100ppm。T1K §3)。Hz で渡すと −100/−163ppm の累積を戻すことになる。論理の Hz を引数にしないのは、U1 で 100Hz が固定だから (呼び手ごとに変えられる口を作らない)。
- 呼び手の値:

  | arch | 呼ぶ所 | num / den | 1 ジフィの物理 IRQ |
  |---|---|---|---|
  | x86 100Hz (段 L の最後まで) | `sys/platform/pc98/clock_source.c` の `platform_clock_init`、`pit_init` の後 (fallback 込みの実設定 `pit_get_setup()` が valid なら) | reload / clk_hz = 19968/1996800、24576/2457600 | 1 (毎 step で bit0) |
  | x86 1kHz (段 L の B14 の後) | 同じ | 1997/1996800、2458/2457600 | 9〜10 (位相補正) |
  | PowerPC | `sys/arch/ppc/kernel_dec.c` の `ppc_dec_start`、同ファイル内 の検査の直後・`ready = 0`  の前 | 1 / PM_DEC_HZ = 1/1000 | 10 ちょうど (DEC の fraction が平均を tb_hz/1000 に合わせるので) |
- 置く場所は「物理の時計を設定する側」。x86 は reload・clk を知るのが pc98 の pit 側だけ、PowerPC は DEC を装填するのが kernel_dec.c だけ。init/ に arch の値を持ち込まない。
- PowerPC で config を `ready = 0` の前に置くのは、2 回目の `ppc_dec_start` で config が失敗したとき、動いている時計を止めないため (`ppc_trap.h`「failure leaves existing clock untouched」)。

### 2-2. 呼び手
- **x86**: `sys/arch/x86/kernel/isr_stub.asm` の `inc dword [tick_count]` を
  `call tick_clock_step` / `test eax, TICK_STEP_JIFFY` / `jz .phys_only` に置き換える。ジフィの回は今の順 (timer_handler → EOI → V86_REFLECT → timeout)。`.phys_only` は EOI だけを出して IRQ_LEAVE へ (NEXT §3-2 のまま)。pushad の後なので eax/ecx/edx の破壊は問題ない。`TP_*` の計測点の位置は動かさない。
- **PowerPC**: `sys/arch/ppc/kernel_dec.c` の `ppc_decrementer_interrupt()` を次にする。
  ```c
  ++kctx_irq_depth;
  while (elapsed--)
      if (tick_clock_step() & TICK_STEP_JIFFY) tick_dispatch();
  --kctx_irq_depth;
  ```
  追いつき (elapsed>1) では step を物理の周期ぶん呼ぶ。1 step で高々 1 ジフィなので、tick_dispatch は elapsed/10 回 (端数で ±1) まとめて走る。
- USER の syscall の中 (EE=0) にいる間に DEC が負になると、EE を戻した時に DEC の例外が 1 回届き、`kernel_dec.c` の `ppc_decrementer_interrupt()` の loop が経過した周期を数えて step をまとめて回す (100Hz でも 1kHz でも同じ)。
- 限界: DEC が符号を越えると時間を失う (2^31/tb_hz ≈ 64 秒 @33.3MHz。100Hz と同じ)。

### 2-3. IF=0 / 例外の中で呼ぶ約束は PowerPC でも成り立つか
- 成り立つ。DEC 例外 (0x900) はハードが MSR[EE] を落として入り、`kernel_vectors.S` は IR/DR/RI だけを立てて `ppc_trap_dispatch` を呼ぶ (EE は立てない、同ファイル内 の宣言)。`kernel_dec.c` は EE を触らない。よって step・tick_dispatch は EE=0 で走り、x86 の IF=0 と同じ。
- 単一 CPU (CPU0 だけ、`kernel_dec.c`)。step の中の書き込み (raw_lo/raw_hi・位相・tick_count・tick_hi・ms_now) と競う者は、EE=0 の前景の読み手だけ。
- 前景の読み手の約束は 2 arch で同じ: `tick_clock_raw`・`tick_clock_jiffy64` は `irq_save` (x86) / `ppc_irq_save` (PowerPC) の中で 2 語を読む。`tick_clock_ms`・`tick_count` は整列した u32 の 1 語読みなので、どちらの CPU でも割れない。
- `kctx_irq_depth`: x86 は IRQ_ENTER、PowerPC は `kernel_dec.c` で step の前に +1。tick_register の `TICK_ERR_CTX` (`sys/kern/irq/tick.c`) は両方で効く (kboot_dec_host の check 19 が見張る)。
- 例外の中の出力なし・待ちなし・確保なしは T1K §4 と同じ。step は I/O をしない。

### 2-4. raw・ms の使い手
| 口 | x86 | PowerPC |
|---|---|---|
| tick_clock_raw | clock_source の µs 補間 (段 L の B6・B7')、recover_profile・kselftest の物理の検査 | 起動の照合 `dec_rate` で TB の区間と比べるだけ (§4)。µs は TB のまま |
| tick_clock_ms | 入力の時刻印 (hal-av の in2_time_ms)・合成器の期限 (S1) | 今は使い手なし (口だけ共通) |
| tick_clock_jiffy64 | PCM の fallback (B15・B16) | 今は使い手なし |
- PowerPC では EE=0 の間は raw・ms・tick_count が進まない。syscall の中の期限は TB で測る (LAN の ESC と同じ)。

## 3. 置き場と arch の境目

### 3-1. 共通 (両 arch の像に入る)
| ファイル | 持つ物 |
|---|---|
| `sys/hal/time/tick_clock.h` (新) | §2-1 の口と `TICK_STEP_JIFFY`・`TICK_CLOCK_MS_HZ`。`platform/pc98/*`・`arch/*`・`drivers/*` を読まない。`OS32_TICK_HZ` は `os32_kapi_shared.h` から |
| `sys/hal/time/tick.c` (変) | `tick_count` (今の 同ファイル内) と tick_hi・raw・位相・ms の状態、§2-1 の 5 関数 |
| `sys/include/kernel_tick.h` (変なし) | `extern volatile u32 tick_count;` のまま (ASM・既存の読み手) |
| `sdk/include/os32/os32_kapi_shared.h` (変、+2) | `#define OS32_TICK_HZ 100` (リテラル。公開 tick の単位の明文化、C89)。**STATIC_ASSERT はここに置かない** (X4) |
- PPC の閉包: tick_clock.h は `kernel/` に置き、`arch/x86/`・`platform/pc98/`・`drivers/` のヘッダを include しない (include すると層の検査 `include_layers` が止める)。
- `kboot_dec.py (旧版 8d5313fc0345)` の host の組み立てに `-I sdk/include/os32` を、`inputs` に `sys/hal/time/tick_clock.h`・`sdk/include/os32/os32_kapi_shared.h` を足す (tick.c が OS32_TICK_HZ を読むため)。

### 3-2. arch の側
| arch | 物理の既定値 | 置き場 | 境目 |
|---|---|---|---|
| x86 (PC-98) | `PIT_IRQ_HZ` (物理の要求)。段 L の B1 で `#ifndef PIT_IRQ_HZ` → 100、最後の B14 で 1000 | `sys/include/memmap.h` (`PIT_HZ`・`TICK_MS` の隣) | `PIT_HZ`=100・`TICK_MS`=10 は論理の値として残す。`STATIC_ASSERT(OS32_TICK_HZ == PIT_HZ, logical_tick_hz_match)` は x86 だけの `sys/platform/pc98/clock_source.c` に置く (X4) |
| PowerPC | `PM_DEC_HZ` = 1000 | `sys/platform/powermac/kboot_info.h` (今の `pm_tick_hz` の宣言 同ファイル内 の隣)。`pm_tick_hz()` は `PM_DEC_HZ` を返す | `ppc_dec_start(tb_hz, hz)` の受け入れは `hz == PM_DEC_HZ`。kboot_audit の `PIT_HZ` 禁止 (`kboot_audit.py`) は残す。PowerPC の共通のコードは PIT_HZ ではなく OS32_TICK_HZ と tick_clock の口を使う |
- 境目の規則: 共通のコード (tick.c・sys/kern/irq/tick.c・exec・lib) が知ってよいのは `OS32_TICK_HZ` と tick_clock の口だけ。物理の Hz・reload・tb_hz は arch の側に閉じる。
- `clock_info.tick_hz` は両 arch で物理の Hz を指す。PowerPC の読み手は試験だけなので 1000 にしてよい。
- x86 の物理の既定値の切り替え (memmap.h の 100→1000、NEXT B14) は**最後の段**に残す (ユーザー決定 2)。それまで x86 は `tick_clock_config(reload, clk)` を 100Hz の値で呼び、毎 step が 1 ジフィになる (挙動は今と同じ、生成物は変わる ★)。

## 4. PowerPC の B29 を戻す段 (段 P) の中身

B29 (`7d288722`、11 ファイル) のうち、**物理の周期に関わる所だけ**を戻す。論理 tick を数える所 (起動の待ち・QMP・試験の 100Hz の期待値) は B29 のまま残す。

| ファイル | 今 (B29) | 段 P の後 | 行 |
|---|---|---|---|
| `sys/arch/ppc/kernel_dec.c` | `next_period()` fraction ≥100、`ppc_dec_start()` 注記 10ms、`ppc_dec_start()` `tick_hz != 100`、`ppc_decrementer_interrupt()` `++tick_count; tick_dispatch();` | fraction ≥ `PM_DEC_HZ`、`tick_hz != PM_DEC_HZ`、`ppc_dec_start()` の検査の直後・`ready = 0` の前に `if (tick_clock_config(1, tick_hz)) return -1;` (§2-1)、§2-2 の loop | +8 −5 |
| `sys/arch/ppc/ppc_trap.h` | 「tick_hz must be 100 (10ms)」 | 「hz must be PM_DEC_HZ (1ms DEC)。論理 tick (10ms) は tick_clock_step が作る」 | ±2 |
| `sys/platform/powermac/clock.c` | `return 100;` | `return PM_DEC_HZ;` (物理 DEC の Hz と注記)。`platform_clock_init` の検査 同ファイル内 はこのまま | ±2 |
| `sys/platform/powermac/kboot_info.h` | `u32 pm_tick_hz(void);` | `#define PM_DEC_HZ 1000u` を足し、宣言の注記を「物理 DEC の Hz」に | +2 |
| `sys/platform/powermac/ppc_kernel.c` | `CLOCK hz=%u` に `pm_tick_hz()` | `OS32_TICK_HZ` を出す (行の書式・値 100 は変えない → `kboot_run.py` の `CLOCK hz=100` と一致) | ±1 |
| `sys/platform/powermac/ppc_kernel.c` | 論理 tick 1 個 ・+3 個  の待ち、TB と µs の照合 | 待ちはこのまま。tb0・tb1 を読む同じ EE=0 の区間で `tick_clock_raw` も読み、raw の増分を TB の区間 `want = (tb1-tb0)*PM_DEC_HZ/tb_hz` (64bit) と比べる。`|got-want| > want/50 + 2` (2% と ±2) なら `kboot_stop("dec_rate")` | +8 |
| 試験 `kboot_dec_host.c (旧版 6298ca8596fe)` | 100Hz の期待値 (dec=250000 等) | B29 前の 1000Hz の期待値 (`25000000,1000` → dec=25000、`-75001` → 4 物理 step)。ジフィの期待を足す: 4 step では tick_count=0・calls=0、計 10 step で tick_count=1・calls=1、33333333 の 1000 回で sum=33333333 かつ tick_count が +100。2 回目の `ppc_dec_start` で config が断る値でも、動いている DEC と ready を変えない | +14 −9 |
| 試験 `kboot_dec.py (旧版 8d5313fc0345)` | `('100hz', 'tick_hz != 100', ...)` | `('1khz', 'tick_hz != PM_DEC_HZ', 'tick_hz != 100', 5)`、追加 `jiffy-gate` (step の bit0 を見ずに毎回 dispatch → calls の check で RED)、`step-call` (step を消すと tick_count が進まない) | +3 −1 |
| 試験 `kboot_clock_fake.c (旧版 bbecbb96ad2e)` | `platform_clock_init(100)`・`tick_hz==100` | B29 前に戻す (1000) | ±6 |
| 試験 `kboot_mutations.py (旧版 b67d6b0c95b7)` | half-rate の RED の理由は `ALIVE tick/TB mismatch` | period を 2 倍にすると raw が TB の半分になり、ALIVE の前に止まる。期待を `'forbidden diagnostic', r'^FATAL kboot dec_rate$'` に。加えて「DEC を 100Hz に戻した像」(`kernel_dec.c` の `fraction >= PM_DEC_HZ` と period を 100Hz にする変異) も `FATAL kboot dec_rate` で RED になる行を足す | +5 −1 |
| 変えない | `kboot_dec_fixture.inc` (`delta*100`、tick_count は論理)、`kboot_run.py` の PATTERNS と rate、`kboot_run_selftest.py`、`kboot_mutations.py` の METER | — | 0 |
| 注記 | `build/targets/ppc-powermac.mk`「live 1 kHz tick counter」、`kboot_audit.py`「the kernel ticks at 1 kHz」、`kboot_run.py` | 「kboot_tick = 論理 100Hz の tick_count (DEC は物理 1kHz)」。audit の文言は「PIT_HZ は PC-98 の名前。共通は OS32_TICK_HZ」に | +4 |

- 論理 100Hz の tick_count は tick_clock_step から出る。PowerPC で tick_count を書くのは tick.c だけになる (`kernel_dec.c` の `++tick_count` を消す)。
- 受入 (PM 決定): QEMU の kboot (kboot-dec・kboot-clock・kboot-run/accept) で、論理の rate (QMP の照合、100Hz のまま) と `dec_rate` (raw と TB) が合うこと。U3 の閾値は掛けない。G4 実機は後追い。x86 の像は段 P で変わらない (共通のファイルを触らない)。

## 5. 段の切り直し

順: **段 C (共通の口と x86 の 100Hz のままの結線) → 段 P と A2 (並列) → 段 L (x86 の物理 1kHz の準備、winmm ほか) → B14**。段 C の C1 が入れば段 P は C2 と並列でよい。

### 5-1. 段 C: 共通の tick_clock と x86 の 100Hz の結線
| 段 | ファイル | 新/変 | 行 | ★ x86 の生成物 | 試験 |
|---|---|---|---|---|---|
| C1 | `sys/hal/time/tick_clock.h`・`sys/hal/time/tick.c`・`sdk/include/os32/os32_kapi_shared.h` (`OS32_TICK_HZ` をリテラルの 100 で。PIT_HZ で定義しない) | 新・変・変 | 45・+80・+2 | ★ (tick.c に関数が増える。呼び手はまだ無いので挙動は同じ) | C3 の host 試験。PPC は `make ppc-ci-image` (像と `common-compile`) (os32powerpc) |
| C2 | `sys/arch/x86/kernel/isr_stub.asm`・`sys/platform/pc98/clock_source.c` (config と STATIC_ASSERT)・`clock_caller_host.c (旧版 9ef969e40cc8)` (tick_clock_config の偽物 +3) | 変 | +12・+6・+3 | ★ (ISR と起動。100Hz の値で config、毎 IRQ が 1 ジフィ) | 既存の test_tick・test_clock_caller・test_pit_clock・test_tick_probe が無変更で通る。NP21/W の起動・60 秒で IRQ0 = timer_handler = 6000。tick_probe の像も組む。C2 で `isr_handlers.c:540` の注記も直す |
| C3 | `tick_clock_host.c (旧版 3be07e5af15b)`・`test_tick_clock.py (旧版 b4100266a43a)` | 新 | 240・160 | なし | 4 つの x86 の組 (19968/1996800・24576/2457600・1997/1996800・2458/2457600) と PowerPC の 1/1000 で 60 秒分のジフィ・ms が有理式どおり (差 ≤1) / 未 config で旧動作 / config の 5 つの拒否と、拒否で状態が変わらないこと / raw・tick_hi の繰り上げ。変異: `while`→`if`、`!clk` の分岐を消す、`num*OS32_TICK_HZ <= den` の検査を消す、`++raw_hi` を消す |
- C1 と C2 は 1 本の統合の枝で入れる。C1 単独でも、C2 単独 (config だけで ISR が inc のまま) でも起動する。
- 段 C の x86 は物理 100Hz のまま。raw は tick_count と同じ値で進む (config は IRQ 開始 `kernel_boot_cpu.inc` の `kernel_boot_cpu()` より前の 同ファイル内 の中なので、未 config の step は起きない)。clock_source の µs 補間はまだ tick_count を読む (100Hz では同じ値)。

### 5-2. 段 P: PowerPC の B29 の戻し (os32powerpc が実装、本線 PM がレビュー)
| 段 | ファイル | 行 | ★ x86 | 試験 |
|---|---|---|---|---|
| P1 | `sys/arch/ppc/kernel_dec.c`・`sys/arch/ppc/ppc_trap.h`・`kboot_dec_host.c (旧版 6298ca8596fe)` (+ `kboot_dec.py` の変異・-I) | +8・±2・+14 (+4) | なし | kboot-dec の host と mac99 F2 (変異 1khz・jiffy-gate・step-call が RED) |
| P2 | `sys/platform/powermac/clock.c`・`sys/platform/powermac/kboot_info.h`・`kboot_clock_fake.c (旧版 bbecbb96ad2e)` | ±2・+2・±6 | なし | kboot-clock |
| P3 | `sys/platform/powermac/ppc_kernel.c` (CLOCK の hz・`dec_rate` の照合)・`kboot_mutations.py (旧版 b67d6b0c95b7)` (half-rate の期待と DEC 100Hz の変異)・注記 (`build/targets/ppc-powermac.mk`・`kboot_audit.py`・`kboot_run.py`) | +9・+5・+4 | なし | kboot-run/accept (QMP の論理 rate)・kboot-audit。half-rate と DEC 100Hz の変異が `FATAL kboot dec_rate` で RED |
- P1〜P3 は 1 本の枝 (`[skip ci]` で os32powerpc が push、本線 PM が統合の枝で CI)。P1 だけだと `pm_tick_hz()`=100 を `ppc_dec_start` が断り起動が止まる。
- 依存: C1 (tick_clock の口)。PowerPC の変更の受入は QEMU と NP21/W の x86 回帰だけ (実機の回に載せない)。

### 5-3. 段 L: x86 の物理 1kHz (後)
NEXT §5 の B・A2・S1・G・W を、次の差し替えで後に回す。中身は NEXT のまま。
| 段 | NEXT からの差し替え | ★ |
|---|---|---|
| A2-a〜c | 変更なし (A の実測とゲートは x86 の物理 1kHz の前提) | ★ (診断像・製品) |
| B1 | memmap.h の `PIT_IRQ_HZ` (#ifndef → 100) だけ。`OS32_TICK_HZ` と assert は C1・C2 で入っている | ★ |
| B2〜B4・B7〜B8 | **削除** (C1・C2 で済み)。B7 のうち「clock_source が tick_clock_raw を読む」は B6 と同じ枝の B7' (`sys/platform/pc98/clock_source.c` の 1 行 + 64bit の pending) に残す | ★ |
| B5・B6・B7'・B9〜B11・B13・B15〜B17 | 変更なし | ★ |
| B12 | 純関数の試験は C3 に移した。B12 は 1kHz の結線の試験だけ (EOI 全 IRQ、V86 反射・timeout はジフィだけ) | なし |
| S1 | `fast_fn` を tick.c に足す (+10)。tick_clock.h に `tick_fast_set` を足す | ★ |
| G1〜G2・W1〜W8 | 変更なし (1ms の受入は B17 の像) | なし (userland) |
| B14 | 最後。memmap.h の `PIT_IRQ_HZ` 100→1000 (1 行) | ★ |

順: 段 C → (段 P と A2-a を並列) → A2-b・A2-c → A の再測定とゲート → B1・B5・B6・B7'・B9〜B13・B15〜B17・S1 → B17 像で T1K §7 → B14。W と G は B と並列。

## 6. 既存の設計の書き換え (本稿の確定と同時に反映済み)

| 文書 | 書き換えた所 |
|---|---|
| T1K | §3 の定数層と SDK の assert の置き場、§4 の tick_clock の状態と ASM、§6 の tick_clock の行 |
| NEXT | §3-1 の物理 IRQ の使ってよい所、§3-2 の口・呼び手・config の呼び場所・同じ枝、§3-3 の raw の読み手、§5 の B1〜B8・B12、段の順、確定の後の追記の PPC の選択集合 |
| 票 TASK_TICK_1KHZ | §0 にユーザー決定 2026-11-12、§1 の段の表を段 C → 段 P と A2 → 段 L → B14 に |
| PPC_KBOOT_DESIGN | 状態行・最短経路・F2 DEC の行、`pm_tick_hz` の宣言、F2 の記述、QEMU で判定する行、「③前の負債：tick単位」の行 |
| PPC_SHELL_EXEC_DESIGN | B29 の表の行と決定の行 |
| PPC_LAN_DESIGN | TB と tick の行 |
- コードの注記 (`sys/arch/x86/kernel/isr_handlers.c`「tick_countのインクリメントはASMスタブで行う」) は段 C2 の実装で直す。

## 7. 仕様の分岐
- Q1 (PowerPC の 1kHz の合否の基準): PM 決定 (2026-11-12、推奨どおり)。U3 の閾値一式は PC-98 の serial 9600bps などから決めたもので、今は PowerPC の DEC 1kHz に掛けない。QEMU では論理の rate と、raw と TB の照合 (`dec_rate`) を受入にする。PowerPC の閾値は G4 実機の段で決める。
- PM が決めた事: config の呼び場所 (§2-1)、PIT_HZ の監査を残して assert を `clock_source.c` に置くこと (X4)、`pm_tick_hz` の名前を変えないこと、PowerPC の CLOCK 行の書式を変えないこと。
- 残るユーザー判断: なし。

## 反論
なし (本線の独立レビュー R1〜R5 と os32powerpc のレビューの指摘は、すべて直し案どおりに反映した)。

## 実装の後の追記 (2026-11-12、段 P、PM 決定)
- 段 P (main dabcb2cc) で、既存の単体の診断用の `PM_DEC_HZ=100` (sys/platform/powermac/platform_of_layout.h・sys/arch/ppc/decrementer.c) が §3 の `PM_DEC_HZ` (kboot_info.h、1000) と名前でぶつかるので、診断の側を `PM_DIAG_DEC_HZ` に改名した。診断は 100Hz のまま。
- QEMU (7450・7455) で decrementer ≈ 1000Hz・論理 ≈ 99Hz、dec_rate の差 0。half-rate と DEC 100Hz の変異は `FATAL kboot dec_rate` で RED。G4 実機の受入と U3 の閾値は後 (§7)。
