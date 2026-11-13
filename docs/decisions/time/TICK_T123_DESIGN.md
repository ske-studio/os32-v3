# TICK_T123_DESIGN — 1kHz tick の続き: 時計を TSC に (T1)・受信を来たら demux まで (T2)・PCM のコピーを IRQ0 から出す (T3)

> 状態: **現行 (2026-11-13)** — 出所: ~/os32-tmp/run/tick-t123-design/DESIGN.final.md (astra 起草、Opus 5.5 の独立レビュー 2 回、PM が確定)。段の状態は票 TASK_TICK_1KHZ が持つ。


## r1→r2 の変更
- P1-1: NIC 共通 net_rx_idle を登録し、e82557 の受信も協調側で demux する。
- P1-2: PowerPC は変更なし、既存の前景 demux を維持し B7/B8 を削除。
- P1-3: deadline-now > R では pcm_obs を呼ばず IRQ10 に任せ、pending/予約方式を削除。
- P2-4: X8 とコピー元別カウンタを追加し、slot1 最大をコピー時間と扱わない。
- P2-5: 予算切れの残存は need_* と独立した net_rx_idle で再実行する。
- P2-6: 既存 CPUID 判定・RDTSC 命令列を再利用し、最大 leaf 検査を追加。
- P2-7: 校正と監視は既存 PIT 時刻の 200ms 端点比較へ変更。
- P2-8: 新規 host C と Python runner を分離し、既存試験の改修段を追加。
- P3-9: read 内で epoch を更新し IRQ0 の edge 結線 A5 を削除。
- P3-10: NP21/W の consttsc=0/1 と multiple 変更を試験に追加。
- P3-11: service 終了時に保留した link_tick を IF=0 で 1 回実行。
- U1: PM 決定により FM slot0 を除外、ユーザー判断待ちは 0 件。

- 基準: main f7f895b3（作業木 HEAD と一致）、2026-11-13。設計のみ。物理 1kHz／論理 100Hz、公開 ABI 不変。
- 単一 CPU・協調切替・整数演算。P100 以上を性能対象とし、i486 は安全な fallback。9801 互換、sys_wait、残り時間のスピンは今回作らない。
- W7 は Windows 並み約 2ms を目標とする。起動時 WC 取得の約 2ms は許容済み。スプラッシュ・起動窓と定常運転を混ぜない。
- 以下の時間・容量の新規値は設計上の受入案であり、実測済みの値ではない。

## 1. 実物の確認（食い違い 8 件）

| ID | 依頼の略記・既存説明 | 現物と設計への影響 |
|---|---|---|
| X1 | 時計を読む関数自体が IF=0 にする | `platform/pc98/clock_source.c:43-69` は再試行だけで irq_save がない。呼出し側の排他契約を維持し、内部状態の更新に必要な保存復元も境界に明記する |
| X2 | net_tick は受信の監視 | `net/nic.c:193,197` は poll/demux と link_tick を実行。監視だけにするにはプロトコル駆動を別 callback に残す必要がある |
| X3 | rxq を空ければ受信停滞が解消 | `net/nic_rx.c:9,51-54` に各 4 枠の二次列と drop がある。満杯の移動を改善と数えない |
| X4 | net_rx_take は取り出しだけ | `net/nic_rx.c:73-75` は IF=0 で net_nic_poll_once を呼ぶ。新しい協調処理をここから強制実行できない |
| X5 | PCM slot1 の観測は毎物理 tick | `kernel/irq/tick.c:25` は論理 dispatch、`pcm_cs4231.c:429-431` はそこから advance。救済の観測間隔は約 10ms。1ms と仮定しない |
| X6 | PCM の tick はコピーだけ | `pcm_cs4231.c:406-418` は PI ACK・停止・再始動も担う。コピーを外しても停止期限の進行は残す |
| X7 | GEM も既存 net/nic の口 | `build/ppc-kboot.mk:58-69` は専用 LAN と GEM、`platform/powermac/lan_service.c:143-158` は PLN1。`net/nic.c` の OS32 demux とは別プロトコル |
| X8 | IRQ0 から IRQ10 へコピーを新設する | `pcm_cs4231.c:390,423-427` は既に IRQ10→advance→copy。IRQ0 のコピーは IF=0 窓で half を跨いだ場合。slot1 120µs の内訳は未測定なのでコピー元別に計測する |

- 実測根拠: `RESULT_realhw2.md` 第4回（run/tick-a3）。IRQ0 最大 184.7µs、slot1 最大 120µs は余裕が小さい。PIT 読み平均 3.8µs（PCM 中 5.9µs）。
- `TASK_TICK_1KHZ.md` §3 の旧 W7 中央値 ≤1.2ms は今回のユーザー決定に置換する。B14 済みの行と古い「未着手」の要約を実装状態の判定に混用しない。

## 2. T1: clocksource の分離

### 境界・状態
```c
/* 新: platform/pc98/tsc_clock.h。全て内部、公開 clock_info の意味は変更しない。 */
struct tsc_epoch { u32 lo, hi, us_lo, us_hi, mult, shift; };
int pc98_pit_read_us(u32 *lo, u32 *hi); /* clock_source.c の既存 PIT 本体、IF=0 */
int pc98_tsc_init(void);             /* capability 検査、未校正は PIT */
void pc98_tsc_calibrate_work(void);  /* 起動中 IF=1、有限回の PIT 比較 */
int pc98_tsc_read_us(u32 *lo, u32 *hi); /* IF 保存復元、失敗は出力不変 */
/* 所有者 tsc_clock.c: epoch、校正値、状態 OFF/CALIBRATING/READY/FAILED。 */
/* 既存の両 arch の予算口はそのまま。base は opaque、cnt と elapsed は同じ単位。 */
u32 arch_tick_budget_base(void);
u32 arch_tick_budget_elapsed(u32 base);
u32 arch_tick_budget_cnt(u32 us);
```
- x86: epoch は read の irq_save 内で TSC 差分が所定閾値を超えたら更新する。epoch.us は前 epoch からの差分で進め、IRQ0/asm に結線しない。閾値を越えた長い空白も 64bit 差で処理する。
- read は epoch と 64bit TSC を短い irq_save 内で取得。変換は差分 × mult >> shift、32×32→64 の部分積を組み合わせる。64bit 差の上位も処理し、長い IF=0 や低位周回で時間を失わない。
- epoch 更新時の小数剰余を保持する（更新時の丸め捨てによる累積誤差を防ぐ）。通常 read は閾値未満なら保存値を進めない。更新と read に同じ純粋整数 helper を使う。
- Pentium の RDTSC は直列化命令ではない。起動校正の端点は CPUID/RDTSC で順序を固定し、通常読出しは volatile asm＋memory clobber、負差検査と単調 clamp。通常 read に CPUID を毎回入れない。
- `arch/x86/arch_cpu.h:63` の arch_cpu_has_cpuid を使い、同ヘッダへ arch_cpu_has_tsc を追加する。leaf0 の最大 leaf ≥1 を確認してから leaf1 の TSC bit を検査。非対応なら RDTSC を一度も実行しない。`kernel/time/tick_probe.c:37` の既存 RDTSC 命令列を再利用し、invariant-TSC bit は必須にしない。

### 校正と故障
- PIT mode2 の reload/clk_hz（1997/1996800 等）を基準にする。1000µs ちょうどと仮定しない。装置設定と論理 tick を変更しない。
- 起動の IF=1 の仕事で約 200ms 離れた端点を 5 窓採る。各端点だけ irq_save 内で (TSC, PIT 基準 µs) の組を採り、待ちの間は割込みを許可。最大 10 試行・起動追加 ≤2 秒で断念。
- `clock_source.c:32-71` の現行 platform_clock_read_us 本体を内部 pc98_pit_read_us として残す。校正中は現行口、READY 後の監視はこの PIT 専用口を呼び、TSC への再帰を避ける。自前 wrap ポーリングは作らない。
- 端点誤差は各約 4µs、合計 ≤8µs/200ms（≤40ppm）を実測で確認。読出し失敗・逆行・端点費用超過は棄却し、5 窓の周波数幅 ≤500ppm、中央値から mult を決定し固定小数点誤差 ≤10ppm を採用条件とする。
- PIT 基準の設計上限は 550ppm。端点費用の約 4µs は機種依存の仮定で、超過時は窓延長か fallback。水晶自体の絶対精度は含めず、独立測定で 60 秒の誤差 ≤0.1% を受入にする。
- 係数の除算は起動時だけ、自前 helper を使用。libgcc・浮動小数点はリンクしない。P100〜対応最大周波数で積・shift の上界を host 試験する。
- NP21/W の TSC と PIT が同じ時間尺度かは未確認。consttsc=0/1 × multiple の各設定、および実行中の multiple 変更で idle／負荷／HLT の窓を比較し、窓間偏差 >0.1%・停止・逆行なら TSC 不採用。エミュレータ名だけでは有効化しない。
- 校正用 idle_work は resident の永久 callback とし、登録は一度だけ。4 枠に空きがなければ PIT 継続と診断、枠数を黙って増やさない。clock 初期化より前に callback を走らせない。
- 起動校正後も協調側で 1 秒間隔の比較窓を採り、同じ PIT 専用口の端点比較で監視する。PIT の長い IF=0 による欠落の限界は残り、既知の欠落窓は校正・監視に使わない。2 窓連続 >0.1% なら FAILED、再有効化は再起動時だけ。
- 切替は IRQ0 の予算区間外で実施。PIT の値に offset を加えて最後の公開時刻から連続させる。逆行は clamp。故障を検知するまでの未知の周波数変動の精度は保証せず、診断に degraded を残す。
- 無 TSC・校正失敗では既存 PIT read/retry に戻す。PIT が一周しか復元できない限界も残る。性能受入は TSC 有効機に適用、fallback は従来の正しさ・不落下を適用する。

### 結線と PowerPC
- `clock_source.c` は READY なら TSC、他は現行 PIT。sys_time_now の引数・戻り値・失敗時出力不変を維持。get_tick と tick_clock_ms は変更しない。
- x86 budget は **µs 単位に固定**（cnt(us)=us）。base/elapsed は TSC 時刻の低位差、fallback は PIT count 差を µs に換算。READY 切替で事前換算した予算定数の単位を壊さない。
- PIT fallback の base は内部で PIT count、elapsed はその差の µs。選択モードは予算開始〜終了中に変えない。TSC の低位 µs 差は IRQ 予算の時間範囲で wrap 安全。
- PowerPC は `clock.c:30,75-86` の upper/lower/upper と TB→µs を維持。budget (`tick_budget.c:4-21`) は TB count のまま。同じ関数契約であり、数値単位を arch 間で共有しない。
- 共通 kernel の tick_budget は変更不要。TB origin、tb_hz、DEC の補償、物理／論理 rate も変更なし。T1 の x86 専用部を PPC にリンクしない。

## 3. T2: NE2K の到着から同じ協調文脈で demux

### 境界
```c
/* 新: net/rx_work.h。装置やプロトコルに依存しない有限実行器。 */
struct rx_work_budget { u32 start, used, max_frames, limit_us; };
int rx_work_take(struct rx_work_budget *b); /* 1 frame 前に確認、最大8件/120us */
/* net/nic.h の内部追加。nic.c が bound/ticking/pending を所有。 */
void net_rx_schedule(void);          /* IF=0/IRQ 可: pending=1 だけ */
int net_rx_service(void);            /* IF=1・depth=0 だけ、demux まで */
static void net_rx_idle(void);       /* nic.c 所有の永久 idle_work、残存を継続 */
/* net/nic_rx.c: 既存 demux の有限版。残存可能なら pending を残す。 */
u32 net_rx_demux_budget(const struct NetNicOps *ops,
    const u8 local[6], const u8 peer[6], struct rx_work_budget *b);
```
- `rx_work_take` は platform_clock_read_us を短い irq_save/restore（PPC は対応する EE 保存復元）で読み、IRQ 共有 budget を開始しない。clock 失敗時は件数上限を使い、計時失敗を記録。今回の呼出しは x86 のみ。PowerPC は既存の前景処理を変更しない。
- 残存は NIC 共通 net_rx_idle に一本化し、ne2k_bh の need_* に依存させない（`ne2000.c:517-521` の早期 return でも次の idle 巡回で継続）。`ne2000_data.inc:65` の enqueue 完了直後にも schedule→service。PIO と同じ IF=1 のまま demux、ne2k_leave を呼ぶ前でもソフト列は読める。
- service は bound->poll を呼ばない。NE2K の busy は装置の所有、ticking は受信・プロトコルの再入防止で別物。busy 中でも ne2k_recv のソフト dequeue は許可し、再び bh に入らない。
- nic.c が協調巡回ごとの budget を所有し、`net_rx_batch_begin/end`（内部宣言を nic.h に追加）で bh 全体を囲む。batch 外の service は局所 budget を生成、入れ子 begin は既存 budget を継承する。
- **1 bh 全体で 8 frame／120µs の demux 予算を共有**。enqueue ごとに予算をリセットしない。予算切れは pending を維持し、次の idle_work で先に残存を処理する。単一 frame の費用だけ上限を超え得る。
- PIO／RDC／reset はこの 120µs の保証外で、現行の有限待ち・round 上限を維持。IF=0 の IRQ5・slot2・send・leave は印／ACK／ソフト TX 列だけという NEXT §2-4 の契約を厳守する。
- net_rx_service の再入番人は irq_save 中に検査・取得し、IF を戻して demux、終了を短い irq_save で公開。IRQ 中の再入は schedule だけ。unbind/reset は番人中には実行せず deferred にする。
- nic.c は bind 前に永久 net_rx_idle を 1 枠登録し、unbound 時は何もしない。登録失敗は bind 失敗として報告し、無処理の NIC を公開しない。T1 校正・既存 NE2K bh と合わせて idle_work の 4 枠内で割当を検証する。
- e82557 は PC98_TICK_E82557 の受信生産を維持し、net_tick が schedule→net_rx_idle が同じ予算で demux。IRQ0 の demux は残さない。`net/nic_e82557.c:22-23` の空 poll や SFS 呼出しに進行を依存させず、SFS 無通信時も LINK 列へ供給する。
- net_tick は watchdog の poll と schedule だけ。登録する同じ slot の wrapper `net_jiffy` は `net_tick(); if (!ticking) link_tick();` とする。link の 100Hz の RX 消費・RTO・deadline・TX・notify を失わない。
- wrapper は IF=0 で ticking を取得して link_tick を実行、終了時に解放。協調 demux 中の IRQ は link_due=1 を残す。service の終了時、番人を保持した IF=0 内で link_due を消費して link_tick を 1 回実行してから解放する。複数回分は絶対期限で集約し、保留回数・遅延を測る。再入 take は schedule のみとし service を再帰させない。
- net_nic_poll_once は互換名を維持し、IF=1・depth=0 なら service、その他は schedule して 0 を返す。IF を勝手に立てない。
- net_rx_take は空ならロック前に文脈を検査して service、IF=0/IRQ なら schedule のみ。その後短いロックで再確認・dequeue。空は AGAIN。既存の IRQ link 消費は後続ジフィに進める。
- demux の共有 rx_frame は ticking で専有。二次列は tail を予約し IF=1 で memcpy、length/count の公開だけ IF=0。head/count の消費と reset は短いロック。reset が予約を無効にしないよう ticking 中は遅延する。
- 二次列満杯では現行の drop 規約を維持し計数。rxq_full だけ下がって sfs/link_drop が増えた場合は不合格。プロトコル状態機械そのものを IF=1 へ移す変更はしない。

### PowerPC の道
- **変更なし、既に前景で demux**。`init/ppc_kernel.c:330` の poll と `lan_service.c:261,267,269-272` の 1 poll 1 frame→返信送信を維持する。
- s.response は 1 枠で、送信前に複数 REQ を処理すると `lan_service.c:167` で drop するため 8 frame 化しない。GEM→PLN1 の道を維持し OS32 demux と共通化しない。
- B7/B8 を削除し PPC の source/build 変更は 0。os32powerpc は既存 PPCKERN.ELF リンクと QEMU LAN／HFS／terminal の退行確認を担当する。

## 4. T3: PCM のコピーを通常は CS4231 IRQ へ

### 境界・実行手順
```c
/* pcm_cs4231.c 内部。公開 pcm ABI と module import は変えない。 */
enum pcm_advance_origin { PCM_FROM_IRQ10, PCM_FROM_TICK };
static int pcm_advance(enum pcm_advance_origin origin);
static void pcm_advance_run(enum pcm_advance_origin origin);
/* pcm_refill.h: 純粋な位置→再進入までの時間と救済閾値の計算のみ。 */
static int pcm_tick_rescue_due(u32 pos, u32 rate, u32 margin_us);
/* 状態の所有者は既存 g_pcm のまま。pending/予約/generation は新設しない。 */
```
- IRQ10 の既存 pcm_irq→pcm_advance→pcm_advance_run を通常補充に維持。tick は origin で区別する。idle_work の新設や module callback 寿命の追加はしない。
- pcm_tick の RUNNING/DRAINING では位置・時刻を採り、half が変化しても `deadline-now > R` なら **pcm_obs を呼ばず戻る**。filled/staging/drain/STOP_REQ を先行更新せず IRQ10 に任せる。
- tick の期限内救済は位置を再確認し、既存 pcm_obs→act→copy/zero→stop_entry を一続きで実行。IRQ10 は従来どおり観測と補充を行う。両者は IF=0、二重消費は既存の last half 判定で防ぐ。
- half 不変時の既存観測・停止/watchdog、close_pending→DRAINING、RS_STOP/STOP_REQ/RS_RESTART は維持する。観測失敗や連続性不明・既に期限超過は単純に延期せず既存 resync/stop の判定へ渡す。PI ACK と入口ガードも維持。
- `pcm_cs4231_math.c:271-300` の consume/filled/drain/STOP_REQ は変更しない。write の予約→IF=1 コピー→publish、free 計算、close/drain/reclaim の所有権も変更しない。
- 診断像では copy/zero の呼出し元を IRQ10/IRQ0 別に数え、観測だけの費用と copy/zero 費用も別集計する。slot1 120µs をコピー時間と断定しない。
- 「under を出さない」は十分な staging 入力、IF=0 最大の契約内、装置正常が条件。アプリの無補給がリング容量を超える場合や無制限の割込み禁止は保証しない。

### 救済閾値
- 最後に確認した位置から、空いた half に DMA が再進入するまでの距離 d frame を算出。`deadline = sample_us + floor(d*1000000/rate)`。再生 half にコピーしない。
- `R = S + J + C + E`。S=次の観測までの最大 10001µs（論理 100Hz、PIT の位相を含む）、J=800µs（定常 IF=0 上限）、C=実機の half copy/zero 最大、E=時計・DMA 位置誤差。
- 初期の仮予算 C=180µs（コピー単独の実測根拠なし、採用前に測定必須）、E=100µs とし R=11081µs を frame 単位に切上げる。rate ごとに half の再利用時間が R より長いことを初期化で検査する。
- `deadline-now <= R` なら IRQ0 で救済。判定は実コピー直前に位置を再確認する。IRQ10 の欠落試験で C/E/S の上界を検証し、超えたら閾値を再計算して採用前に再測定する。
- 救済を含めた IRQ0 最大 ≤200µs は維持する。通常時の IRQ0 削減とは別集計。IRQ10 へ移すコピーも IF=0 費用なので serial 遅延を別に測る。
- PPC には CS4231/DMA の状態を持ち込まない。共通 PCM ABI は変更なし。将来の別音源は IRQ 補充／tick 期限救済の契約を参照可能だが、空の PPC driver や共通 kernel callback は今回追加しない。

## 5. 像・部品の大きさ（KiB、リンク前の見積もり）

| 部品 | text/rodata 増 | data/bss 増 | 像と上限の扱い |
|---|---:|---:|---|
| T1 x86 clock・校正・整数 helper | 3〜6 | <0.5 | resident kernel.bin。PIT fallback を残す分を含む |
| T2 共通予算・NIC 結線 | 1〜3 | <0.25 | x86 kernel.bin、PPC 増分 0 |
| T3 期限判定・救済 | 1〜2 | <0.125 | pcm.mod。既存リング16KB／staging16KBの増設なし |
| 診断カウンタ | <1 | <0.25 | 診断像だけ。kdebug に新コードは入れず既存の出力経路を使う |

- x86 kernel 増分は最大約 10KiB を仮予約。`include/memmap.h:230-232` の resident 上限は 0x275000（2516KiB）、bss/配置丸めも数える。現物の空きは未測定で「収まる」とは判定しない。
- 起動 FD は kernel.bin 直置きではなく vmkernel.lz4＋対象 module 群（`build/image.mk:12`）。非圧縮増分の上限 12KiB＋ファイル丸めを予算とし、1232KB／1440KB 両媒体を実生成して残余 sector と loader の上限を検査する。
- kdebug.bin の実装増分は 0、kernel 対応情報で再生成が必要。kdebug の個別容量制限・module arena・FD 合計を既存生成器／リンク検査で確認し、上限定数は緩めない。未測定の余白を既知扱いしない。
- PPC は PPCKERN.ELF の map/未解決シンボルとロード区間を確認。PPC のコード・リンク入力が変更されていないことも検査する。

## 6. ファイルごとの実装段（各行 = 1 ファイル = 1 実装者、400 行以下）

| 段 | 新/変更・ファイル | 概算変更行 | 依存 |
|---|---|---:|---|
| A0 | 新 platform/pc98/tsc_clock.h | 40 | 口を先に固定 |
| A1 | 新 platform/pc98/tsc_math.c | 180 | A0、部分積・係数計算だけ |
| A2 | 新 platform/pc98/tsc_clock.c | 340 | A1、状態・校正・監視 |
| A3 | 変更 platform/pc98/clock_source.c | 50 | A2、init/read/fallback |
| A4 | 変更 platform/pc98/tick_budget.c | 45 | A2、µs 単位 |
| A8 | 変更 arch/x86/arch_cpu.h | 25 | A2 の前提、既存 CPUID helper と最大 leaf/TSC bit 検査 |
| A6 | 変更 build/kernel-objects.mk | 15 | A1/A2 の object 登録（現行列挙の入口へ） |
| A7 | 変更 platform/pc98/tsc_clock.c | 40 | A3/A8、起動 idle_work 登録・初期化順の統合、A2 と直列 |
| B0 | 新 net/rx_work.h | 35 | 口固定 |
| B1 | 新 net/rx_work.c | 100 | B0、純粋な件数・経過管理 |
| B2 | 変更 net/nic.h | 20 | B0、service の宣言 |
| B3 | 変更 net/nic_rx.c | 110 | B0/B2、有限 demux・take・公開排他 |
| B4 | 変更 net/nic.c | 190 | B3、ticking・wrapper・net_rx_idle・bind/unbind・N1 の e82557 救済 (net_jiffy、2 ジフィ) |
| B5 | 変更 drivers/net/ne2000/ne2000_data.inc | 25 | B4、enqueue 後結線 |
| B6 | 変更 drivers/net/ne2000/ne2000.c | 55 | B5、bh 共通予算・残存 drain |
| B9 | 変更 build/kernel-objects.mk | 5 | B1、A6 と直列 |
| C0 | 新 drivers/sound/cs4231/pcm_refill.h | 100 | 位置と純粋な閾値計算、static inline |
| C1 | 変更 drivers/sound/cs4231/pcm_cs4231.c | 120 | C0、origin・延期判定・既存停止契約・診断 |
| Q1a | 新 tools/tests/test_tick_tsc.py | 180 | A1〜A8（A5 除外）、CHECK・host32 runner・変異 |
| Q1b | 新 tools/tests/tick_tsc_host.c | 360 | Q1a、libc なし校正・単調性模型 |
| Q2a | 新 tools/tests/test_net_rx_work.py | 180 | B1〜B9（B7/B8 除外）、CHECK・runner・変異 |
| Q2b | 新 tools/tests/net_rx_work_host.c | 360 | Q2a、割込み・残存・idle・e82557 模型 |
| Q3a | 新 tools/tests/test_pcm_refill.py | 180 | C0/C1、CHECK・runner・変異 |
| Q3b | 新 tools/tests/pcm_refill_host.c | 360 | Q3a、DMA・期限・コピー元模型 |
| E1 | 変更 tools/tests/test_tick_budget.py | ≤100 | A4、予算単位・stub・anchor 付替え |
| E2 | 変更 tools/tests/test_clock_caller.py | ≤100 | A3、PIT 専用口・保存復元 |
| E3 | 変更 tools/tests/test_hostsvc_nic.py | ≤100 | B4、idle/service 結線 |
| E4 | 変更 tools/tests/test_hostsvc_nic_rx.py | ≤100 | B3、有限 demux・take |
| E5 | 変更 tools/tests/test_ne2k_bh.py | ≤100 | B6、早期 return 後の残存 |
| E6 | 変更 tools/tests/test_ne2000_dma.py | ≤100 | B5/B6、batch・文脈 |
| E7 | 変更 tools/tests/test_pcm_cs4231.py | ≤100 | C1、origin・延期・停止 |
| E8 | 変更 tools/tests/test_pcm_e4.py | ≤100 | C1、close/reclaim 退行 |
| E9 | 変更 tools/tests/test_net_link.py | ≤100 | B4、link_due・e82557 |
| E10 | 変更 tools/tests/test_hostsvc_sfs_lan.py | ≤100 | B3/B4、SFS/LINK 退行 |

- 大段 T1/T2/T3 を一括で 1 人に渡さない。A0/B0/C0 の契約確定後、A・B・C の実装は並列可。A6/B9 と A2/A7 は同一ファイルなので直列。実装開始は本設計のレビュー後。
- E1〜E10 が参照する既存 host C に stub/期待値の修正が必要なら、各 C を独立した追補段（1 ファイル・≤200 行）として割当し、Python に C harness を埋め込まない。新規は test_tick_clock.py:1-14 と同じ host32 の libc なし構成とする。
- 宣言・生成器の依存追加は各ファイルを別の小段とし、生成ファイルを手編集しない。追加試験の CHECK に全入力を列挙。既存 mutation anchor は実装時に検索して移設する。

## 7. 受入 → 試験 → 変異（各変異は正常 exit 0／変異 exit 1）

| 受入・観測条件 | 試験 | 1 行変異の例 |
|---|---|---|
| TSC 非対応で #UD なし、PIT fallback・失敗時出力不変 | libcなし host で ID 不可/最大 leaf 0/TSC bit 0、NP21/W の非対応設定 | capability 判定を常時 true |
| 周回・長い IF=0・剰余保存・切替で単調、誤差上界 | host 64bit参照値／10秒欠落／低位wrap、係数の境界 | 差分の high を 0 にする |
| 校正端点失敗・費用超過・逆行・ばらつきを棄却 | host 偽物PIT/TSC、NP21/W consttsc=0/1 × multiple設定・実行中変更、idle/負荷/HLT | window rejection を無効化 |
| TSC 有効時 sys_time_now 単体 p99 ≤1µs、PIT版より高速 | Ra266、計測器の空計測を控除、各10万回、最大も報告 | READY 分岐を PIT に固定（host 経路検査で RED） |
| budget 開始〜終了で単位一定、120µs共有・保証1単位維持 | host TSC/PIT/TB別、切替要求を途中注入 | cnt(us) に PIT count を返す |
| enqueue と同じ bh 内で demux、最大8件／120µs＋1 frame | host fake clockで9件、IF/EE/depth・再入を全入口注入 | enqueue 後 service 呼出しを削除 |
| IF=0 は印だけ、PIO/RDC/reset は IF=1のみ | 既存NE2K host＋RDC/reset timeout注入、NP21/W転送 | service の文脈ガードを削除 |
| rxq_full/4MB が B14 以下、drop/OVW増分0 | NP21/W iso/hostdrv 各5回、rxq_full・二次列drop・滞留時間を同時計測 | service の共通予算を常時 exhausted |
| link/SFS・RTO/TX・IRQ take・unbind/resetが進む | host 再入全境界・need_*なし残存・保留link_tick、NP21/W link／4MB、実機e82557でSFS無通信のHELLO/ACK | wrapper の link_tick を削除 |
| PPC ELFリンク、TB/DEC rate不変、PLN1/HFS/terminal退行なし | 既存PPC host＋QEMU kboot、PPC PMが実施 | PPC変更なし、新規変異なし（既存退行試験） |
| PCM 通常 tick の copy 0、期限内だけ救済、IRQ10/IRQ0別copy数、under/rep/resync 0 | host PI抑止＋時間進行、NP21/W/実機58秒曲、十分なstaging | deadline比較を反転 |
| PCM 延期中はstaging/filled/drain不変、write/close/reclaim退行なし | host 期限外half変化とwrite/publish境界、既存E7/E8 | 期限外returnを削除してpcm_obsを実行 |
| 救済込みIRQ0 ≤200µs、slot2〜4 ≤135µs、定常IF=0 ≤800µs | 実機 idle/pcm/draw/nic/serial各5回、通常・救済別最大 | 救済条件を常時true（hostコピー回数がRED） |
| 通常PCM時IRQ0最大 ≤160µs、serial遅延 <1042µs・OE/overflow 0 | 診断像でIRQ0/IRQ10を両計測、製品像でSerialDiag/4MB | tickの期限判定を削除 |
| 論理100Hz・物理1kHz・既存時計利用者の退行なし | host境界＋NP21/W GUI開閉/音声/通信、実機Sleep(1)中央値≤2.1ms・p99≤2.2ms、タイマ/音源/V86/timeout | 論理step係数を10倍 |
| 容量・freestanding・ABI不変 | make kernel＋媒体を含むmake all、PPCリンク、map/nm/import差分 | 専用fixtureにlibgcc参照を1行追加 |

- 連続時間の性能値は host だけでは証明しない。変異は論理契約を RED にし、物理受入は対応する実機／エミュレータで別に判定する。観測ロストは全走 0、診断像と製品像を混同しない。
- T1単体→T2単体→T3単体の各 baseline と統合像を保存。T2/T3 は既存 clock でも試験可能、最終性能は T1 統合後。IRQ10 欠落は host 注入を必須とし、実機で無理に装置を壊さない。
- 実装者は対象 build＋触った試験 --mutate＋run.py --lint、PM は make all、CI は check/build、full-check はユーザー判断。check-changed は実行しない。
- 今回は設計のみのため preflight --fix-fonts、build、lint、host/NP21/W/実機試験は未実施。PACK の「run/tick-t123-design 以外へ書かない」を優先し、生成物を伴う検査を行っていない。

## 8. 未決（ユーザー判断 0 件）

- U1: PM 決定により FM（slot0）は今回の対象外。T1〜T3 後の最大値を測って別票とし、PCM の閾値を転用しない。
- TSC/NP21/W の精度、P100 の C、容量余白は実装者が測る技術的な受入事項であり、ユーザー決裁待ちの項目には数えない。

## 反論
- なし（0 件）。

## 確定 (2026-11-13、Opus 5.5 第 2 回の条件付き承認、PM が反映)
- N1 (PM 決定): e82557 の受信の退行は受け入れない。net_jiffy で「net_rx_idle が LINK_RX_IDLE_RESCUE_JIFFIES (=2) ジフィ走っていない、かつ pending がある」ときだけ、既存の IRQ0 の予算 (tick_budget の RX の当番) の中で e82557 を demux する。sys_halt を呼ばない重いアプリ・長い GUI の描画の間も link の probe・RTO が受信を得る。受入に「CPU を回し続けるアプリの下で e82557 の link が落ちない」を足す (NP21/W は e82557 が無いので実機の項目、host 試験は救済の条件の境界 1・2 ジフィと変異「条件を外す」「pending の検査を消す」)。
- N2: §7 の IF=0 の計測に「service の終わりの link_tick (link_due の後追い)」の経路を 1 行足す (定常 IF=0 ≤800µs の対象)。実装の依頼に書く。
- U1 (PM 決定): FM の slot0 は対象外。
