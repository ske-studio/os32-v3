# TASK_TICK_1KHZ — P3 の 1kHz IRQ0 tick

> 状態: **実装中 (2026-11-13)** — x86 の物理 1kHz を既定にした (B14)。段 A のゲートは実機 Ra266 と NP21/W で満たした (§2)。残りは §3。tick_clock は arch に依らない口 ([TICK_ARCH_DESIGN](../../decisions/time/TICK_ARCH_DESIGN.md))。

## 0. ユーザー決定 (原文)

- 「7 件とも推奨どおり (推奨)」(2026-11-05)。中身: (U1) 物理 1kHz・論理 100Hz の二周期 (公開の tick と ABI は変えない)。(U3) 合否の基準は設計の推奨値 (どれもスプラッシュの区間を除く)。serial の実遅延 <1042µs と OE・リング溢れの増分 0 を組にする。
- スプラッシュ (U2、2026-11-05):「スプラッシュ中はカーネルが持たなくてはならないのはCtrl+STOPだけでいいですよ。あー、でも受けれるだけのバッファは受けておいたほうが良いかもしれませんが、そもそもバッファに溜まる処理が飛んできたら断る程度で良いですが。」再生は作り直さない。スプラッシュの間は 1kHz の契約の外に置く。
- 設計を回す指示: 「実装はとりあえず良いですが、設計は回してください。」(2026-11-05)
- 1kHz tick の票を P3 に起こす、実機と NP21/W で割り込み負荷を実測してから (V3_PLAN D16)。
- 2026-11-09 題目: 「次に詰める設計の題目。推奨は、1kHz tick の次の段と winmm のタイマです。…詰めて」
- 2026-11-12 (選択「設計を直して共通部分を先に (推奨)」): 1. TICK_1KHZ の設計を、arch に依らない tick_clock の口に直す。2. 段 B の共通部分 (tick_clock) を先に入れる。x86 の物理 1kHz への切り替えは最後。3. PowerPC は decrementer を物理 1kHz に戻し、tick_clock_step で論理 100Hz を作る。
- 2026-11-12 (問い「10ms をベースに、は (a) 内部と公開の tick は 10ms、割り込みだけ物理 1kHz か、(b) 全部 10ms か」の答え): 「(a) 内部は 10ms、割り込みは 1kHz」。U1 のまま。
- PM 決定 (2026-11-12): U3 の閾値は今は PowerPC の DEC 1kHz に掛けない。QEMU では論理の rate と raw と TB の照合を受入にし、閾値は G4 実機の段で決める。
- 2026-11-09 合成器の起こし (11-05 の PIT ワンショットの置き換え、選択): 「1ms ごとの期限照合 (推奨)」 — PC-98 で IRQ に繋がるのは PIT #0 だけで 1kHz の周期と µs 時計に使うため。CS4231 のタイマが使えないときは毎 1ms の IRQ0 で期限を照らすだけ、起こすのは配り点。
- 2026-11-13: Quit の猶予 (決裁 A3) は「ジフィ数 (時間) に変える」(300 ジフィ = 3 秒)。キー以外の割り込みの周には WM の全周を回さないこと (カーソルの更新は最大 100 回/秒) は「これでよい」。
- 2026-11-13: WC の 1.9ms は「起動時に移す段を足す」。受入は、実機で、1kHz の像の最初の draw の間隔が ≤1.1ms。GUI の idle (1kHz で gshell 0.90・gui_demo と 0.64 MIPS) は「足りる (B14 へ)」。
- 2026-11-13 B14 の取り込みの条件: CI の check・build の success。full-check は B14 の条件にしない (後で CI で 1 回)。NP21/W は 1kHz の製品の像で 1 回の起動のスモーク (ver・GUI を出して閉じる・pcm 1 曲・LAN の 4MB)。実機は 4 回目の測定で済ませる。
- 2026-11-13 (Fable の調査「Sleep(1) が約 2ms なのは待ちの口が期限を渡せないため、実物の Windows の Sleep(1) も約 2ms」を受けて): W7 は「Windows 並み (約 2ms) に目標を緩める」。期限つきの待ち (sys_wait) の残り待ち (期限の前の最後の tick の後、1 周期未満を時計を読んで待つ) は get_tick スピン禁止の「例外にしてよい」(sys_wait を作るときの許可)。起動の途中の WC の取得 1 回 (約 2ms) は「このままでよい」。続きの段 T1 (時計を TSC に)・T2 (受信を来たら demux まで)・T3 (PCM のコピーを IRQ0 から出す) は「OK」。

## 1. 詳細設計 (確定) の要約

全文: [TICK_1KHZ_DESIGN](../../decisions/time/TICK_1KHZ_DESIGN.md) (確定の節が本文より優先)、[TICK_1KHZ_NEXT_DESIGN](../../decisions/time/TICK_1KHZ_NEXT_DESIGN.md)、[TICK_ARCH_DESIGN](../../decisions/time/TICK_ARCH_DESIGN.md) (tick_clock の口と段の順、前 2 つより優先)。

| 段 | 中身 | 状態 |
|---|---|---|
| A | 周波数を変える前の実測 (診断像のみ: `tick_probe`、`isr_stub.asm` の計測点、スプラッシュ区間は別集計) | 済 (2026-11-13)。実測は §2 |
| C | 両 arch 共通の tick_clock (C1: `kernel/time/tick_clock.h`・`tick.c`・SDK の `OS32_TICK_HZ`)、x86 の 100Hz のままの結線 (C2: `isr_stub.asm`・`clock_source.c`)、host 試験 (C3)。C1・C2 は 1 本の枝 | 設計は確定 (2026-11-12)、実装は未着手 |
| P | PowerPC の B29 の戻し: DEC を物理 1kHz (`PM_DEC_HZ`)、論理 100Hz は tick_clock_step、起動の `dec_rate` (raw と TB)、kboot の試験 (P1〜P3、os32powerpc)。C1 の後、A2 と並列 | 設計は確定 (2026-11-12)、実装は未着手 |
| A2 | NIC など阻害区間の解消: 計測 a・IRQ0 の予算 b・NE2K の重い所を協調処理へ c (スプラッシュ区間を除く、削減値は A2-a の実測後)。P と並列 | 設計は確定 (2026-11-09)、実装は未着手 |
| 再測定 | A 再測定とゲート (閾値一式は全文 §2・§7、承認済み) | 済 (2026-11-13)。実機 Ra266 と NP21/W で満たした (§2) |
| A3 | 実機で見つかった阻害の解消: 計測点 (A3m)、相手不在の HELLO を 5 回の後 1 秒おきに (A3l)、e82557 の SCB の待ちを発行の前だけに (A3e)、GeForce の WC の lease を起動中保持 (A3w)、WC の取得を module の読み込みの時に (A3w2)。WM の全周をジフィの刻みに・Quit の猶予を 300 ジフィに (G3) | 済 (2026-11-13) |
| L | x86 の物理 1kHz の準備: `PIT_IRQ_HZ` (B1)・`pit_math` (B5)・µs 時計 (B6・B7')・復旧の検査 (B9〜B10)・周回比較 (B11)・1kHz の結線の試験 (B12)・注記 (B13)・PCM の fallback (B15〜B16)・1000Hz の試験像 (B17)・合成器の 1ms の期限照合 (S1)。GUI の 1ms 再開 (G1・G2) と winmm のタイマ libmmtime (W1〜W8、KAPI の追加なし) は並列 | 設計は確定、実装は未着手 |
| B14 | x86 の物理の既定値 100→1000 (memmap.h の 1 行) | 済 (2026-11-13)。取り込みの条件はユーザー決定 (§0) |
| T1〜T3 | 続き ([TICK_T123_DESIGN](../../decisions/time/TICK_T123_DESIGN.md)): T1 時計を TSC に (x86、PowerPC は timebase で既に同じ形)、T2 NE2K の受信を来たら同じ協調の文脈で demux まで・NIC 共通の net_rx_idle (e82557 は 2 ジフィ走らなければ IRQ0 の予算で救う)、T3 期限まで遠いうちは IRQ0 で pcm_obs を呼ばず CS4231 の IRQ のコピーに任せる。PowerPC の受信は変更なし。FM の slot0 は対象外 (PM 決定) | 設計は確定 (2026-11-13)、実装は未着手 |

- PM 決定 (確定の節): 各受入行は無変異の正常対照が exit 0・各変異が exit 1。スプラッシュ中の CTRL+STOP の取りこぼしが 100Hz 像でも出たら、1kHz の採否とは別の既存不具合の票にする。
- 公開の `PIT_HZ=100` と ABI は変えない。

## 2. 実測 (2026-11-13、実機 Ra266 = Pentium II 266MHz・e82557・GeForce、F = 265.92MHz)

ゲート: IRQ0 の最大 ≤200µs、slot2〜4 の合計 ≤135µs、IF=0 の最大 ≤800µs。値は負荷ごとの最大 (µs)。observation_lost は全回 0。

| 像 (main の SHA) | 負荷 | IRQ0 | slot2〜4 の上界 | IF=0 | IRQ0 の間隔の最大 |
|---|---|---|---|---|---|
| 100Hz 診断 (16c2966a、直しの前) | idle / pcm / draw / nic / serial | 183.3 / 178.0 / 228.7 / 186.0 / 184.6 | 111.6 / 110.6 / 157.1 / 111.7 / 110.8 | ≤108.8 | draw 14.51ms、他 ≤10.14ms |
| 100Hz 診断 (bad8f4e6) | idle / pcm / draw / nic / serial | 167.4 / 177.0 / 178.2 / 164.6 / 164.9 | 83.2 / 92.9 / 90.0 / 89.5 / 79.7 | ≤94.6 | ≤10.05ms |
| 1kHz 診断 (bad8f4e6) | idle / pcm / draw / nic / serial | 172.3 / 184.7 / 175.5 / 140.1 / 155.3 | 105.0 / 91.8 / 96.5 / 88.5 / 83.4 | ≤108.5 | ≤1.08ms (起動後の最初の draw 1.07ms、再起動の後 1.08ms) |

- WC の取得は起動の窓に 1 回 (2.1ms) 入り、その窓の間隔の最大は 2.3〜2.9ms。draw の最中は 0 回。
- 1kHz の製品像 (bad8f4e6、計測点なし): sleep 10 は 10.44 秒 (往復込み)、pcm_play 58 秒は under・rep・resync 0、4MB は 8.7〜9.0 秒、SerialDiag の誤り 0、mmtimetest の Sleep(1) は中央値 1954µs・99% 1970µs。
- NP21/W (B14 の版 fa3b6a27、1 回の起動): ver は tick=1000Hz・SERVICES_READY、GUI を出して閉じた、pcm 58.28 秒 (under・rep・resync 0)、LAN の 4MB は 121.9 秒 (100Hz は 121.8〜122.3 秒)。GUI の idle は gshell だけ 0.90 MIPS・gshell+gui_demo 0.64 MIPS (100Hz は 0.36 / 0.40)。
- 証拠: `~/os32-tmp/evidence/2026-11-13/` の tick-a-realhw・tick-a3-realhw・tick-a3w-realhw・tick-a3w2-realhw・tk-g3-np21w・tk-b14-np21w・tick1k-b17。集計は `~/os32-tmp/run/tick-a3/RESULT_realhw2.md`。

## 3. 残り

- Sleep(1) の受入 W7 (中央値・99% とも ≤2.2ms、ユーザー決定 2026-11-13): 実機の 1kHz の製品像で中央値 1954µs・99% 1970µs。GUI での mmtimetest は未実施。
- Quit の猶予の 3 秒の打ち切りは host 試験だけ (NP21/W では Quit に応じないアプリを起動できず、観測していない)。
- 受信列が満杯でリングに残した回数が、NP21/W の 1kHz の像で iso・hostdrv の時に約 8 倍 (溢れは 0)。T2 で直す。
- 余裕の小さい所: pcm の IRQ0 は最大 184.7µs (slot1 が最大 120µs)、pit_read_count は 1 回 3.8µs。T3 と T1 で直す。
- hsync は大きさと mtime だけで差し替えを判定する (中身だけ違う部品を見落とす)。
