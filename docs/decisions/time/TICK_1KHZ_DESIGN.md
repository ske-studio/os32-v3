# P3: 1kHz IRQ0 tick の実測計画と切り替え設計 r3

> 出所: ~/os32-tmp/run/tick1k-design/DESIGN.final.md (astra 起草 r1〜r3、Opus レビュー 3 回、PM が確定)
> 状態: **現行 (2026-11-05)** — 段の状態は票 TASK_TICK_1KHZ が持つ。

## r2→r3 の変更
- N1: スプラッシュ再生は作り直さず契約外とする決定を D8・A1・B1・§4〜§8・旧 P1-1 に反映し、A2 の再生変更案を削除。
- N2: 除外境界を boot_splash の入口から復帰までとし、別集計・復帰時解除・除外印の変異試験を追加。
- N3: CTRL+STOP の取りこぼし0と100Hz像同等の効き目、受信の拒否・破棄と復帰後の回復を受入に追加。
- N4: D8 直前の空行を削除し、実物確認表を連続させた。
- U3: 全ゲートのスプラッシュ除外と、serial 実遅延・OE/リング溢れ0を組にした承認条件を明記。

## r1→r2 の変更
- P1-1: 長いラスター IF=0 を D8 に追加。ユーザー決定により再生は作り直さず、スプラッシュ区間を1kHz契約の外に置き、tick・時計の遅れを許す（U2 決定済み）。
- P2-1: NIC の件数上限を時間予算から再評価し、A1 の最大時間・件数測定と削減案を追加。
- P2-2: PIT_HZ=100 を維持し、物理要求を PIT_IRQ_HZ に分離、表外の host 試験の単位も保護。
- P2-3: 採否閾値を U3 のユーザー判断とし、推奨値・IRQ 遅延予算との関係を明記。
- P2-4: A1 で timer_handler を除く短い IRQ 経路を分離測定する定義を追加。
- P3-1: D5 の PIT_HZ 参照元を訂正。
- P3-2: B1 に serial 遅延と SerialDiag の OE・受信リング溢れ 0 を追加。
- P3-3: A0 に外部観測基準とゲスト CPU 設定を追加し、NP21/W の合否をゲストサイクル基準に限定。
- P3-4: SDK→memmap.h の依存を確認し、SDK の kernel 分岐で論理率を STATIC_ASSERT 照合。

調査元: `/home/hight/os32-v3`、HEAD `7b66782741f5108c999017dc54031a9c360142ec`。以下の file:line はこのツリー相対。
本稿だけを作成。ソース編集・ビルド・配備・実機操作はしていない。整数のみ、協調切替、既存の所有権・IRQ 境界を保つ。

## 1. 実物の確認
決定の根拠は `V3_PLAN.md:83` D16 と `TASK_T7_AND_FOLLOWUPS.md:226`。次の **8 件**が「周波数だけ変更」で満たせない前提差。
| ID | 票・コメントからの前提 → 実物と設計への影響 |
|---|---|
| D1 | 8.125ms・23%速いのは過去の不具合。`kernel/time/pit.c:10`、`sysclk.h:9` に履歴、`pit.c:29` は既に実クロックから算出。再修正対象ではない。 |
| D2 | `pit_math.c:48` は「100Hzだけ」と書くが判定は `hz == PIT_HZ`。`include/memmap.h:433` を変えるだけで通り、`pit_math.c:61` の切捨て divisor と `:73` の整数 µs が1kHzで不正確になる。 |
| D3 | `kernel/time/ktime.c:14` は32bit周回非対応、`time_math.c:28` の pending 加算も32bit。物理1kHzなら約49.7日で周回し、clampだけでは時計が長期停止する。 |
| D4 | `arch/x86/kernel/isr_stub.asm:453,458,460,468` は加算・callback・EOI・V86反射/期限を毎IRQで実施。`kernel/irq/tick.c:20` に間引きは無い。既存周期を保つ結線が要る。 |
| D5 | `pcm_cs4231.h:134,142` の5/3tick・10ms、`snd_engine.c:315,502` の毎callback減衰・5/15/8tick、`v86.h:200` の300×100は追従しない。`kbd.c:151` に加え pcm_e4_guest・snd_engine・iso9660.h・platform_splash.h・gfinit・recover_* も PIT_HZ を参照する。固定値と率参照が混在するため、PIT_HZ は論理100Hzのまま保つ。 |
| D6 | `kapi/kapi_generated.c:674` は get_tick を生の tick_count として返す。`userland/shell/cmd_base.c:55,149` の /100、`userland/lib/snd/libos32snd.c:50` のms→tickなどがABIの実質的な100Hz契約。 |
| D7 | `kdebug/recover_profile.c:147,150,154` はUARTフレーム<PIT周期・切捨てdivisorを要求。9600bpsの10bitは約1042µsで1kHzの周期を越す。復旧経路も移行の障害になる。 |
| D8 | `drivers/video/pc98/splash_raster_play.c:94-117` の pl_v2_frame は active0+400行を IF=0 で走る（31k 約13ms、24k 約16ms）。`platform/pc98/splash_image.c:111` から起動時約3秒、wt/splash-time では最大8秒反復する。除外しなければA1 の800µsゲートを超え、B1 では1フレーム当たり約12回以上のIRQ0を失い、物理カウンタ・論理tick・sys_time_nowが遅れる。再生は作り直さない。スプラッシュの間は1kHzの契約の外で、tick・時計の遅れは許す（ユーザー決定 2026-11-05、§8 U2）。 |

## 2. A: 周波数を変える前の実測
専用票 `TASK_TICK_1KHZ.md` を後続PMが新設し、A0/A1/B0/B1の像・結果・判定を分けて記録する。現時点で合格とは扱わない。
| 版 | 内容と次へ進む条件 |
|---|---|
| A0 | 上記HEADの100Hz無計測像。起動媒体・kernel/module hash・設定・CPU/入力clock・PCM/NIC/GPU構成・NP21/W版・ゲストCPUモデル/クロック/倍率/速度設定を記録。外部60秒の基準装置・精度・校正と、IRQ要求/配送の観測点・分解能・欠測検出方法を記録。NP21/WはゲストCPUサイクルを設定周波数で換算した60秒と外部イベントトレースを基準とし、ホスト壁時計は参考のみ。実機は独立した外部60秒基準を使用。既存回帰が通ること。 |
| A1 | A0と同じ100Hzで計測だけを追加。割込み全体/区間・IF=0とNIC callbackの最大時間/処理件数/バイト数を測る。スプラッシュ区間は boot_splash() の入口から復帰までと定義し、そのサンプルを別集計してゲートから除く。区間外の時間超過は別修正段A2で対処して再測定。対象構成の全環境で下記ゲートを通すまで1kHz像を作らない。 |
| B0 | A1合格後だけ作る、換算/時計/復旧修正を含む100Hz像。A0との回帰差を潰す。 |
| B1 | B0と同じコードの1kHz試験像。実測合格後に既定周波数を変更。不合格なら100Hz像へ戻し、既定は維持。起動後の動的再設定は設けない。スプラッシュ中の CTRL+STOP は取りこぼし0、効き目は100Hz像と同じ。NIC/serial の溢れ・破棄は許し、復帰後のOVW回復と再送による正常復帰を必須とする（下記・§7）。 |
環境はNP21/Wの既存1.9968MHz設定と実機PC-9821Ra266の2.4576MHz。NP21/Wで第2clockを設定できれば追加するが、実機の代替にしない。
利用可能な実機/最も遅い対象CPUを票に列挙し、未測定機種の負荷保証はしない。実機操作は担当セッションに依頼し、必要な物理操作の承認は実施時に取得する。
各版・各環境でウォームアップ30秒、各負荷60秒×5回、混合負荷30分。最悪値・p99・合計CPU占有・IRQ回数・観測漏れを保存。
負荷: idle、FM BGM+SE/SSG減衰、PCM 44100/22050Hz連続再生とdrain/再同期、V86 BIOS/ROM/timeout/脱出、打鍵連打/CTRL+STOP、NIC+serial/host転送、FD/ISO、実行可能な混合。描画/splash はゲート対象から外して別ケースとする（通常描画の区間外測定はゲート対象）。
PCM/V86で排他的な音源状態は別ケースにする。ゲストの協調処理は継続し、tickから新たな切替・待ち・確保を呼ばない。
除外印は `init/kernel.c:242` の boot_splash() 呼出し直前で立て、復帰直後に必ず解除する（早期return・診断分岐を含む）。_enable()・services_boot()・NIC legacy 初期化は区間外であり、除外を起動全体へ広げない。区間中もIRQ0のlink_rx_pumpとserial受信は生きる。入口/復帰と外部時刻を記録し、境界をまたぐサンプルは別集計に含め、区間外窓の開始時にpendingと診断カウンタの基準を取り直す。除外区間中の時計の遅れを区間外の時刻差へ持ち越さず、外部基準と時計の差分を区間外窓ごとに測る。wt/splash-time 等の単体診断を boot_splash 区間と偽装しない。
CTRL+STOP の実物: `drivers/input/kbd.c:442-444` はIRQ1で ring3_abort_request() を呼び、cooked打鍵には配らない。`exec/exec_abort.inc:40-55` → `exec/appslot.c:370-377,404-412` の経路では、起動時のアプリ不在/CUIでは appslot_abort_request() が0を返し、abort_reqを立てず、スプラッシュも中断しない。これは100Hz像でも同じ効き目である。受信成功をkill数で代用せず、外部キー入力とIRQ1でのCTRL+STOP認識数を突合する。スプラッシュ中も取りこぼし0を受入とし、実測前に保証済みとは扱わない。
NIC/serial は有限の既存バッファで受けられる分だけ受け、溜まる処理・要求は拒否し、溢れ・破棄を許す。無制限の保留やバッファ拡大で埋めない。8251は1バイトであり13〜16msのIF=0中の9600bps受信を無欠落とはしない。復帰後にNICのOVW回復・serialのOE後の受信再開と再送を確認し、正常な転送へ戻ることを合否とする。SerialDiag OE/受信リング溢れ増分0はスプラッシュ区間を除いた窓だけで判定する。
短いIRQ経路は IRQ_ENTER〜EOI〜IRQ_LEAVE のうち timer_handler 全体を除いた区間と定義し、A1で入口/出口・EOIの区間和を別集計する。論理処理（反射/timeout等）は100Hz側へ分離計上し、timer_handlerのdispatch overheadも含む既存callback群と二重計上しない。B1の実際の短経路はB1で再測定する。
測定点: IRQ_ENTER直前→復帰直前、C timer_handler、7slot各callback、EOI/反射/timeout、非IRQのIF=0最長区間、IRQ1/IRQ10の遅延、連続IRQ0の間隔。
NP21/Wはエミュレータ外のイベント/サイクルトレースでIRQ要求と配送を突合。ホスト壁時計の遅さをゲストCPU負荷に混ぜない。
実機はCPUIDで利用可と確認したTSCの整数差を使い、外部経過時間で周波数を校正。TSCが使えない機体は外部測定ができるまでゲート未達とする。
PIT自身やsys_time_nowだけで欠落/長いIF=0を測ると周回を見落とす。PITラッチ差は1周期未満が別に証明された短区間の補助測定だけに使う。
計測は固定長の集計/ヒストグラムのみ、IRQ中のprint/serialは禁止。計測終了後IF=1で回収。測定器の空区間コストを記録し、合否には控除しない最悪値を使う。
§8 U3で承認を求める保守的ゲート（未承認、スプラッシュ区間を除く）: A1の `1000×短いIRQ経路 + 100×既存callback群` の実測時間合計が1秒の5%以下、全callback集中時のIRQ0最大200µs以下。
さらにスプラッシュ区間を除く全IF=0区間とIRQ配送を止める区間は最大800µs以下（約1msに200µsの余裕）。A1で超えればB1へ進めず、該当経路の別修正後に再測定。
B1でもスプラッシュ区間を除く窓で実測CPU占有5%以下、IRQ0最大200µs以下、阻害区間800µs以下、外部観測のIRQ欠落0、試験追加のPCM underrun/repeat/resync・kbd欠落・V86破損0を要求。serialは9600bps・FIFO無しを含め、受信要求からUART読出しまでの最大遅延を測る。1文字約1042µs未満（他IRQ・読出し時間込み）を必要条件とし、各試験窓のSerialDiag OE増分と受信リング溢れ増分はともに0を要求する。
スプラッシュ区間を除くIRQ欠落0は独立観測のPIT要求数とIRQ0配送/処理数の対応で判定し、測定端のpendingを整合させる。観測漏れがある結果は未判定。NP21/Wでは合否時間をゲストサイクル基準とし、ホストの60秒では判定しない。
スプラッシュ区間を除く窓で、A0で定めた外部60秒基準との時刻差は測定器誤差を除き0.1%以内、同時に分周計算の予測値と一致。非計測像も回帰を実行し、スプラッシュ区間を除く全体処理時間の悪化はA0比5%以内。
閾値は本設計の採否ゲートであり既存実績ではない。満たせないときに平均だけで採用したり、ログ出力を削って測定結果を隠したりしない。

## 3. B: 周期・定数・互換境界（§8の仕様判断を前提）
推奨は **物理IRQ0を約1kHz、既存論理tickを100Hz** とする。tick_count/get_tick/ktime_ticks/既存callback/V86の単位は10msのまま。
1ms精度の新APIや全driverの1kHz化は追加しない。sys_time_nowは物理カウンタから計算する。1kHzという名称は要求周波数であり正確な1000Hzを意味しない。
| 定数層 | 正典と役割 |
|---|---|
| Shared | `sdk/include/os32/os32_kapi_shared.h` に `OS32_TICK_HZ=100`（既存公開tickの意味を明文化）。公開ヘッダはC89、構造体/slot/引数単位を変更しない。 |
| Subsystem | `include/memmap.h` の `PIT_HZ=100` と `TICK_MS=1000u/PIT_HZ` は論理値として維持。新設 `PIT_IRQ_HZ` だけを物理要求100→1000とする。`pit_setup.clk_hz/reload` が実周期の正典。 |
| Local | PCM/FDC/serial等の機構時間・回数・境界余裕は各所有ファイル。物理period_usを論理TICK_MSの代用にしない。同じ意味の新たな写しは増やさない。 |
依存確認: SDK sharedヘッダ:192-195 の `__KERNEL_BUILD__` 分岐が memmap.h を読む。逆向きincludeは追加しない。memmap.h側を100の正典とし、SDK側の同分岐でinclude後に `STATIC_ASSERT(OS32_TICK_HZ == PIT_HZ, logical_tick_hz_match)` を置く。外部SDKのC89経路には内部GNU11のassertを出さない。
物理要求の参照は pit_init・pit_mathの物理設定/検査・recover_profile・kselftest に限定し、物理計算自体は実clock/reloadを使う。論理消費者のPIT_HZ→OS32_TICK_HZ一括置換は不要。`tools/tests/gfinit_budget_host.c:169,210-211`、`pegc_mode_host.c:1415,1423,1443`、`recover_t2_host.inc:113`、`dep_dir_b_timer_host.c:95` もPIT_HZ=100のままで10倍化を防ぐ。
`pit_compute` は測定/比較の100と1000を扱う。未知clock/無効周波数を既存のPIT_ERR_CLOCK/HZで返すのは装置仕様の範囲でありソース許可リストではない。
divisorは最近接整数 `reload=(clk_hz + hz/2)/hz`。1.9968MHz→1997（1000.100160µs、約−100ppm）、2.4576MHz→2458（1000.162760µs、約−163ppm）。
100Hzは19968/24576で正確な10ms。切捨て1996/2457を採らず、周期ごとのreload変更も行わない。既存mode2プログラム順序は `platform/pc98/timer.c:9` を保持。
`pit_setup.period_us` は表示用floorと明記し、時刻累積や復旧判定に使用しない。`pit_init` は従来の負値+既定fallbackを保ち、試験起動では要求と実設定の不一致を失敗として表示。
KAPIの意味/配置は維持するため版上げ不要。変更が必要になった場合はsdk/kapi.jsonを正として末尾追加・版上げ・再生成・clean buildを別段にし、生成ファイルを手編集しない。

## 4. 状態・関数・IRQ手順・周回
新しい `kernel/time/tick_clock.c` が物理カウント上位/下位と論理位相を所有。既存tick_countの定義はidt.cに残し、更新者だけを集約する。
```c
/* tick_clock.h: 内部GNU11、IRQ0/IF=0専用。sampleは同じIF=0内でコピー */
struct tick_clock_sample { unsigned int lo, hi; };
int tick_clock_step(unsigned int reload, unsigned int clk_hz); /* 論理tick発生=1 */
void tick_clock_sample(struct tick_clock_sample *out);
/* step: ++raw_lo; if (!raw_lo) ++raw_hi;
 * phase += reload * OS32_TICK_HZ;
 * if (phase >= clk_hz) { phase -= clk_hz; ++tick_count; return 1; }
 * return 0;  初期phase=0。対応する2周波数では1IRQで高々1論理tick。 */
```
1kHzでの論理tickは主に10IRQごと、位相補正時は9IRQ間隔となる。実時間との遅れは物理1周期未満。単純な10分周の−100/−163ppm累積を避ける。
ASMは既存 `inc tick_count` をstep呼出しへ置換、戻りを保存し、論理tick時のみtimer_handlerを呼ぶ。EOIは全IRQで従来位置、V86_REFLECT 0とv86_tick_and_check_timeoutは論理tick時だけ。
これでV86のINT08h反射・#GP予算リセット・300×100の5分・GCAP300の3秒を保つ。例外脱出とIRQ深度の片付けは現在の順を保持し、分岐でEOIを飛ばさない。
7slotの順・登録/解除・ownerは変えない。FM減衰、PCM停止state、NIC受信budget、GF共有IRQ救済pollは100Hzで継続。全slotの集中最大を測るので平均の間引きで重い1回を隠せない。
`ktime.c` はIF保存後にp1/count/p2とraw_hi/loを採り、pendingなら**64bitで**raw+1、境界不確定なら既存3回retry。論理tick_countを補間に混ぜない。
```c
/* time_math.c: I/Oなし。N=採用した64bit物理tick、c=有効なcount */
cycles = N * reload + (reload - c);
us = (cycles / clk_hz) * 1000000ULL
   + ((cycles % clk_hz) * 1000000ULL) / clk_hz;
/* 上下32bitの出力に分ける。period_usの積算は禁止。 */
```
64bitの入力cycleは実用稼働期間で溢れず、残数との積も64bitに収まる。u32物理周回約49.7日を上位へ繰上げ、pendingの下位0xffffffff境界も同じ式で処理する。
既存get_tickは約497日のu32周回を維持。期限は符号付き差なら2^31tick未満、経過差なら1周未満を前提にする。raw_loだけを期限APIとして公開しない。
sys_time_nowのAGAIN/NOSYS/INVALと負値時出力不変、単調clampを維持。clampは欠落回復機構ではない。IF=0契約は10ms未満から**実物理1周期未満。ただしスプラッシュ再生の区間は除く**へ改訂する。除外境界は§2の boot_splash() の入口から復帰までとし、計測は別集計にする。
PCMのfallback `tick_count*10000` は論理時計として維持（量子化の逆行を既存単調化で吸収）。長期wrap時のfallbackはepoch付き論理時刻を使う内部関数へ変更し、モジュールimportも追従する。
CPU校正は論理tick5回≒50msを維持。`CPU_DELAY_SCALE=100` の2段割りは10000µsの分解であって周波数100の写しではない。新周期jitterによる校正誤差を測る。
復旧UARTは1回のwaitの合計countsがreload以上でも扱える累積差へ直す。poll間隔<実周期を実測で証明し、割込みを含む1周以上の空白があり得る呼出しではこの方式を採用しない。
証明不能なら1kHz化を保留する。IF=0で1042µs待つ経路なら分割だけではゲートを通らないため、IF状態/呼出し元を確認して先に修正する。上限回数とREC_TX_PIT_CONFIG/COUNT/SAMPLESは維持。

## 5. 待ち・期限・マジックナンバーの監査台帳
直接3語の全検索に加え、ktime_ticks/get_tick、期限引数、callback内の回数減算を追った。以下の「維持」は論理100Hzを前提とする意図的な判断である。
| 所有箇所（file:line） | 意味と処置 |
|---|---|
| PCM `pcm_cs4231.h:134` / `pcm_cs4231_math.c:54` / `pcm_cs4231.c:118,271,666` | 5=50ms、3=30msは維持。closeの+3半分はDMA通過数、+3tickは停止期限、slack2は開始端数+証拠1回で、十倍しない。strict > の1回余裕も維持。 |
| PCM `pcm_e4_guest.c:9` | 2秒は既存PIT_HZ=100で算出。frames/44100/22050、DMAサイズ、512frame marginはtickと無関係。 |
| FM `fm.c:346` / `snd_engine.c:19,243,315,502,728` | ms/TICK_MSと最小1tick、MML期限を維持し率は既存PIT_HZ=100を維持。SE5/15/8/10は50/150/80/100ms、減衰3/2/1は100Hzごとの音量。raw APIも10ms単位。 |
| V86 `v86.h:200` / `v86.c:433` / `kdebug/v86_gcap.c:133` | 300×100=5分、GCAP300=3秒、GP上限=10ms窓を維持。物理IRQごとの予算リセットは禁止。 |
| kbd `kbd.c:151,408,667,809` / exec `appslot.h:68` | STOP抑止0.5秒を論理率で算出、APP_RUNAWAY200=2秒、park_poll/last_kernel_tickは同じ論理時計。IRQ1自体は間引かない。 |
| FDC `fdc.h:25,171,178,191,193,206` / `fdc_seek_reset.inc:184` / `fdc_track.h:70` | 30/10/150/100/20/1/10=300/100/1500/1000/200/10/100ms。head settle3=ceil(15ms/10ms)+境界1、margin2は倍率、idle200=2秒。全て維持しFD実測。diskio.c:136の時計も同単位。 |
| serial `serial_plan.h:94,99,104` / `serial_plan.c:153` | 10000µsは論理周期、最低3tick=30ms、edge1は量子化余裕。baudから2文字時間をceilして+1する契約を維持。IF=0側の有限poll回数はtickへ変えない。 |
| NIC `e82557_regs.h:131,153` / `ne2000.c:55` | PHY10=100ms、link300=3秒、TX50=500ms、OVW2=20ms、TX5=50msを維持。e82557.c:375の表示は論理TICK_MS。 |
| link `net/link.h:86` / `link.c:317,493` / `link_selftest.inc` | RTO20=200ms、probe100=1秒を維持。`net/link_rx.inc:233` の LINK_RX_BUDGET16（link.h:90）は最大1.5KBのne2k_recv PIOを16件行い、`lgy98.c:176` のREFLECT_BUDGETも同形。10ms周期由来の件数上限は1ms時代の時間保証にならない。C-bus 1〜2MB/sの仮定では最大長1件だけで約0.75〜1.5ms、16件なら約12〜24ms＋処理費用となる。A1で両経路のcallback最大時間・件数・バイト数と他callback集中時の残予算を測る。IRQ0全体200µsから短経路・他callback等を引いた時間予算内となるよう、件数削減を測定後に決める。1件でも収まらなければ件数削減だけでは不十分で、PIO分割または既存協調処理への移管を別設計し再測定する。nonceのtickは数値種のまま。 |
| FS `sfs_proto.c:149` / `serialfs_session.c:262` / `iso9660.h:96` | ceil(ms/TICK_MS)+1は端数対策。ISO idle2秒は論理率へ。SQLite `os32_sqlite_vfs.c:646` の/10000と最小1tickは現契約のまま（切捨て是正は別不具合票）。 |
| 校正 `cpu_calibrate_math.h:38,47` / `cpu_calibrate.c:145` | 5tick観測、loops/tick下限1000・fallback10000を保持。実測でdelay_usの既存精度を確認。ATA/ATAPIの校正依存pollはtick数へ置換しない。 |
| `kernel/irq/irq_math.c:136` | storm上限は10ms窓のまま、閾値を十倍しない。IRQ配送負荷と隔離動作の回帰を行う。 |
| 起動 `kernel.c:71` / `platform_splash.h:5` / `splash_image.c:15` | reload10=100ms、splash3秒、画像のTICK_MS→µsを保持。ただしラスター再生のIF=0はD8のとおり別問題で、ユーザー決定（2026-11-05、§8 U2）により再生を作り直さず、スプラッシュ区間中のtick・時計の遅れを許す。platform splash/probe、bootlog_save、kernel/sqlite_engineの記録は論理tickと明記。 |
| kdebug `gfinit.c:40,320` / `recover_cleanup.h:62` / `recover_gf.h:59` / `recover_common/session.inc:22` | 復旧/試験予算・秒表示は既存PIT_HZ=100を維持。recover_profile.c:150のPIT実設定検査だけは物理clock/reloadで判断し、二つの率を混同しない。 |
| kdebug `kpeek.c:54` / `gf_foundation_test.c:129` / PEGC diag:108 | retry2=20msを維持。GF/PEGC測定秒は論理率へ。nic_traceは記録のみ、kselftestは実divisor/有理周期に期待値更新。 |
| `kernel/ime/ime_dict.c:60` / `kernel/font_guard_fixture.c:27` | 注入待ちOS32_RAW_WAIT_LOG_TICKSの単位維持、500=5秒。faulttest.c:192,207もPIT_HZ=100を維持。 |
| userland/SDKと生成import | get_tick利用のshell/rshell/sleep/watchdog/gshell/filer/bench/GUI/SDK exampleは100Hz契約を維持。ktime_ticks生成元tools/gen_sqlite_imports.py:73と各module importは同単位。直接物理率を参照する注記を訂正。 |
公開tickの `now < deadline` が残る例（sndtest.c:45、rshell.c:313、sdk/example/hello/main.c:36）は周回で壊れる既存不具合として票に記録し、採用前の回帰範囲では差分比較へ修正する。
実装時は同じ検索をtools/tests・生成物も含め再実行し、対応表で未分類0を確認する。ソースを許可リストで拒否する仕組みは作らない。

## 6. ファイルごとの変更段と大きさ
各行が1ファイル=1段。宣言ヘッダと本体は順に渡す。A（測定）→A2（スプラッシュ区間を除くNIC等の時間超過対処）→A再測定/ゲート→B（100Hz統合）→C（1kHz試験）を固定し、各段は半日〜1日以内を目安。
| ファイル | 新/変・概算差分行 | 役割・依存/並列 |
|---|---|---|
| docs/tasks/v3/TASK_TICK_1KHZ.md | 新100 | Aの票・証拠/判定、PM |
| init/kernel.c | 変4 | A/B診断像のみ、boot_splash呼出し前後の除外印・境界記録、再生は変更しない |
| kernel/time/tick_probe.h | 新30 | 計測境界型、A先行 |
| kernel/time/tick_probe.c | 新150 | 固定集計・スプラッシュ区間の別集計、A・診断像のみ |
| kdebug/tick_probe.c | 新150 | 読出しと結果出力、計測型後に並列 |
| arch/x86/kernel/isr_stub.asm | 変25 | A計測点、B raw step/EOI/論理分岐、逐次 |
| kernel/irq/tick.c | 変15 | A callback計測点、配列順維持 |
| sdk/include/os32/os32_kapi_shared.h | 変5 | B論理率の意味とkernel分岐STATIC_ASSERT、先行 |
| include/memmap.h | 変5 | PIT_HZ/TICK_MS維持、PIT_IRQ_HZ追加、C物理既定値切替は最後 |
| kernel/time/pit_math.h | 変12 | 有理周期と表示値の契約 |
| kernel/time/pit_math.c | 変15 | 100/1000と最近接divisor |
| kernel/time/pit.c | 変10 | 初期化の要求にPIT_IRQ_HZ、設定値/注記 |
| kernel/time/tick_clock.h | 新35 | 状態境界、B先行 |
| kernel/time/tick_clock.c | 新90 | 位相と物理epoch、型確定後 |
| kernel/time/time_math.h | 変15 | 64bit snapshot引数 |
| kernel/time/time_math.c | 変40 | 有理周期変換/pending carry、並列可 |
| kernel/time/ktime.c | 変35 | raw snapshotとfallback時刻の接続、算数後 |
| kernel/time/ktime.h | 変8 | epoch付き論理fallbackの内部宣言 |
| drivers/sound/cs4231/pcm_cs4231.c | 変5 | fallback時計の呼出し、ktime後 |
| tools/prototypes/pcm_imports.def | 変8 | 内部fallback import追加、生成器で追従 |
| drivers/sound/cs4231/pcm_e4_guest.c | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| drivers/sound/fm/snd_engine.c | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| drivers/input/kbd.c | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| fs/isofs/iso9660.h | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| include/platform_splash.h | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| drivers/video/pc98/backend_pegc_diag.inc | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| kdebug/gf_foundation_test.c | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| kdebug/gfinit.c | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| kdebug/kdebug_main.c | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| kdebug/recover_cleanup.h | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| kdebug/recover_gf.h | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| kdebug/recover_common/session.inc | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| kdebug/recover_profile.c | 変40 | UART待ち累積/実周期検査、呼出し文脈実測後 |
| kdebug/kselftest/pit_setup.inc | 変15 | clock別divisor/有理周期の期待値 |
| userland/tests/faulttest.c | 変更0 | 既存PIT_HZ=100を維持、率置換不要 |
| userland/system/sndtest.c | 変2 | 周回差比較 |
| userland/shell/rshell.c | 変8 | 周回差比較 |
| sdk/example/hello/main.c | 変2 | 周回差比較 |
| tools/tests/test_tick_1khz.py | 新180 | clock/phase/wrap/周期結線のhost・変異 |
| tools/tests/tick_1khz_host.c | 新250 | 純関数の32bit/64bit比較fixture |
A2はスプラッシュ区間を除く測定で発見したNIC等の阻害区間を先に解消する独立段。NICの削減値と変更量はA1実測後に確定し、`net/link.h`、`net/link_rx.inc`、`drivers/net/ne2000/lgy98.c` の実際の所有箇所ごとに段を分ける。
ビルド登録は既存所有Makefileの該当リストへ数行、生成物は既存generatorで再生成。既存試験のアンカー/期待値も各所有ファイル単位で更新し並列競合を避ける。
注記変更対象は§5の各所有ファイルに各1〜3行。大きい既存ファイルへ計測本体を足さず新ファイルに集約、time_math等は既存機能の置換だけとする。
見積: 製品kernel text +2〜4KiB/BSS <128B（64bit算数helperの既リンク有無で変動）、PCM <0.5KiB、kdebug復旧修正 <1KiB。
診断像のみkernel +2KiB/BSS最大4KiB、kdebug +2〜4KiB。FD/MINIMALには計測機能を含めず、製品時計部分は含む。各像のmapと媒体空きを実測し既存上限内を必須とする。

## 7. 受入 → 試験・変異
通常の時間・負荷・欠落ゲートはすべてスプラッシュ区間を除く。スプラッシュ専用の受入は下表で別に判定する。
| 受入 | host / NP21/W / 実機と変異 |
|---|---|
| 両clockの正しい設定 | hostで100/1000、19968/24576/1997/2458、無効引数/clock/周波数・fallback・負値を確認。変異: clock固定、floor divisor、valid誤設定。ゲスト両環境で実設定を記録。 |
| 時刻精度・周回 | hostでraw低位fffffffe→0、pending carry、論理tick周回、71分/49.7日/497日の合成値、全p1/p2分岐と3retry/出力不変を確認。変異: carry削除、period_us乗算、32bit積、clamp除去。 |
| 二つの周期の結線 | hostで両clockの60秒分・位相境界を整数基準と比較、callback/V86=100Hz、EOI=全IRQ。変異: 10固定分周、論理分岐削除、EOI間引き、GP毎IRQリセット。ゲストで同じ回数を観測。 |
| 時間契約とABI | hostで§5の待ち端数/strict > /半周限界、PCM closeのframe数とtick数を独立期待値で確認。変異: PCM余裕を10倍、SSGを毎IRQ減衰、TICK_MSを物理率で算出。ゲストで音長/STOP/5分timeout/FD/転送を比較。 |
| 復旧と校正 | hostでUART9600/115200・両clock・複数reload相当・停止PITの有限終了、CPU5論理tickを確認。変異: modulo補正削除、poll上限除去、scale100を10へ変更。実機でIF状態とpoll最大間隔を含め測定。 |
| 負荷と割込み欠落 | §2のA1/B1全ケースを両環境で実施。ゲスト試験用にスプラッシュ区間外で1.2ms/2.2msのIF=0を注入して計測がゲート違反を検出することを確認し、製品像には残さない。tick時計自身を基準にした偽合格を防ぐ。変異: 復帰後も除外印を立てっぱなしにし、区間外の1.2ms注入を検出できず試験が落ちることを確認。 |
| スプラッシュ中のCTRL+STOP | NP21/W・実機で100Hz像/B1を比較。フレーム内の位相をずらした打鍵・連打を外部入力数とIRQ1認識数で突合し、取りこぼし0。アプリ不在時はabort_reqを立てず再生を継続し、復帰後に誤killしないことを確認（100Hz像と同じ効き目）。変異: CTRL+STOP認識/要求呼出しを削除して検出。 |
| スプラッシュ中の受信・要求と復帰 | NIC/9600bps serialを区間前から流し、有限バッファを超える処理・要求を拒否/破棄し無制限に溜めない。区間内の溢れ・OEは別記録で許容。復帰後にOVW回復とserial受信再開・再送完了を確認し、その後の区間外窓で実遅延<1042µs・OE/リング溢れ増分0を判定。変異: OVW回復/再送を止め、正常復帰しないことを検出。 |
実装段は対象ビルド→check-changed MUTATE=0→変更試験の変異→lint。最終統合はPMが全像・媒体・既存PCM/FM/V86/kbd/復旧回帰を確認。今回は設計ファイルの行数確認のみ。

## 8. 未決 2 件（ユーザー判断 U1・U3、U2 は決定済み）
U1: P3の「1kHz tick」は物理IRQ0の1kHz化で、公開/既存サービスtickの100Hz維持を認めるか。**本稿の二周期構成を推奨**する。
全サービス/公開get_tickまで1msにする意図なら仕様分岐であり、§5の全換算・SSG減衰・V86反射・GP窓・ABI版移行を設計し直してから実装へ渡す。測定ゲート自体はどちらでも先に実施できる。

U2（P1-1、決定済み・2026-11-05）: ユーザー決定の原文:
「スプラッシュ中はカーネルが持たなくてはならないのはCtrl+STOPだけでいいですよ。あー、でも受けれるだけのバッファは受けておいたほうが良いかもしれませんが、そもそもバッファに溜まる処理が飛んできたら断る程度で良いですが。」
再生は作り直さない。スプラッシュの間は1kHzの契約の外に置き、tick・時計の遅れを許す。必ず受けるのはCTRL+STOPだけとし、溜められる分はバッファに受け、溜まる処理・要求は断る。境界・合否は§2・§7による。

U3（P2-3）: §2の閾値一式（すべてスプラッシュ区間を除く）を承認するか。800µs・200µsの二つだけではserialの1042µs以内を保証できないため、競合IRQ込みのserial実遅延<1042µs・SerialDiag OE/受信リング溢れ増分0と組で承認を求める。**次の値を暫定採否ゲートとして推奨**する。既存の保証値ではなく、未承認のままB1採否や既定変更を決めない。
| 推奨値 | 根拠と適用限界 |
|---|---|
| IRQ処理CPU占有5%以下（スプラッシュ区間を除く） | 1秒のうちIRQ0に最大50ms、残り95%を協調処理と他IRQへ残す方針。個別IRQの遅延保証は別途必要で、ハード仕様から導いた数値ではない。 |
| IRQ0最大200µs以下（スプラッシュ区間を除く） | serial 9600bps/FIFO無しの1文字約1042µsに対し約1/5をIRQ0へ割当。IRQ1/IRQ10・PCMも実測し、より厳しい要求があれば縮小する。 |
| IF=0/配送阻害最大800µs以下（スプラッシュ区間を除く） | 約1000µsの物理周期に200µsの余裕を残す。阻害800µs＋後続IRQ0 200µsではUART読出しまで約42µsしか残らないため、個々の閾値合格だけでは十分でなく、競合IRQを含むserial実遅延<1042µs・OE/リング溢れ0も必須。 |
| 時刻差0.1%以内（スプラッシュ区間を除く） | 60秒で60msの上限。最近接divisorの約100/163ppmに対し1000ppmの暫定誤差枠で、IRQ欠落の許容枠ではない。校正誤差を記録し分周予測との一致・欠落0を独立検証する。 |
| 非計測像の全体処理時間はA0比5%以内の悪化（スプラッシュ区間を除く） | 協調処理の性能回帰に対する暫定許容値。個別IRQ予算からは導けないためユーザーの性能方針として承認を求める。 |

反論: なし（0件）。

## 確定 (2026-11-05、Opus の r3 の確認で Approve、PM が反映 — この節が本文より優先)
- T1 (P3): §7 の各行は、無変異の正常対照が exit 0、各変異は対応する assert で exit 1。ゲスト fixture は終了値で機械判定し、timeout は host runner が exit 1。
- T2 (P3): スプラッシュ中の CTRL+STOP の取りこぼしが 100Hz 像 (A0/A1) でも出たら、1kHz の採否とは別の既存不具合の票にする。B1 で 100Hz 像より悪いときだけ B1 の不合格にする。
- 残るユーザー判断: U1 (物理 1kHz・論理 100Hz の二周期) と U3 (閾値一式、serial の実遅延 <1042µs と OE・リング溢れの増分 0 を組にし、全ゲートを「スプラッシュ区間を除く」)。U2 は決定済み。
- ユーザー決定 (2026-11-05、7 件とも推奨どおり): U1 は物理 1kHz・論理 100Hz の二周期 (公開の tick と ABI は変えない)、U3 は閾値一式を設計の推奨値で採る (どれもスプラッシュの区間を除く)。§8 の未決はこれで決まった。
