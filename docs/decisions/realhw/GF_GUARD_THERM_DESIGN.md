# GF_GUARD_THERM_DESIGN — GeForce 層5の時計・熱・perf

> 状態: **現行 (2026-11-07)**。TASK_GF_FEATURES §0 のユーザー決定と、MIT nouveau c03c1a1 を手本にする。

## 1. 根拠と範囲

- N/ = ~/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/。
- 測温は N/therm/nv40.c:29–102、しきい値と alarm は therm/temp.c:81–250、fan は therm/fan.c:32–98、PLL は clk/nv40.c:39–195、RAM は fb/ramnv40.c:34–175 を手本とする。これらは MIT。
- メーカー資料は同じ refs 下の nvidia-open-gpu-doc/classes/host/cl446e.h。これは DMA チャネルの資料であり、NV44A の熱・PLL 全体の公開仕様とは扱わない。
- 単一タスク・協調切替・整数演算。既存の MMIO・保存像・チャネル・世代・資源所有の契約を使う。IRQ に MMIO や待機を追加しない。
- 手本の downclock/pause は任意の emergency コールバックであり、この参照には実装の結線が無い。段階降速・3回冷却・drain_pause・98への帰還・再入場禁止はユーザー決定として実装する。

## 2. calc → prog の出所と照合（A05/A06）

```c
/* feature5.h の内部型。gf5 が所有。公開 plan は今の値型を維持。 */
struct gf5_plan_guard {
    struct gf5_clock request;
    struct gf5_plan canonical;
    u32 epoch, rom_generation;
    bool valid, core_used, ram_used;
};
/* gf5 に plan_guard と u32 rom_generation を追加。 */
i32 gf5_plan_check(struct gf5 *, const struct gf5_plan *, bool ram);
void gf5_plan_invalidate(struct gf5 *);
```

| 手順／手本 | 契約 |
|---|---|
| 計算 N/clk/nv40.c:146–182、N/fb/ramnv40.c:34–63 | clock.c の副作用なし内部 solve_plan(request) と公開 calc を分ける。公開 calc の入口で旧 guard 無効化、全領域の計算成功時だけ要求・正規値・epoch・ROM世代を保持して out を返す。 |
| 照合 N/clk/base.c:195–198 | prog は MMIO 読み／pause より前に guard有効・世代一致・当該領域未使用を検査。不成立 ESTATE。保持した元 request を現在の ROM で再計算し、公開 plan の全9個の u32 値を canonical/再計算値と欄ごとに一致照合。不一致 EINVAL、解読失敗は EBIOS/ERANGE。 |
| 実行 N/clk/nv40.c:186–195、N/fb/ramnv40.c:115–175 | core/RAM 各入口は検証成功後、最初の副作用前に当該 used=true。RAM→core の一対を許す。失敗した領域の再試行は新しい calc 必須。書込列・RAM のみの pause は既定どおり。熱動作はcore/shaderをreadbackし、下記の丸め契約で照合、不一致EIO。 |
| 失効 N/clk/base.c:591–596（終了時 worker 完了） | ROM acquire/release、表示 stop/save 開始、FAULT で無効化。epoch はチャンネル本体の値を検査、同じ ROM ポインタ再利用も generation で拒否。restore は新epochで calc し直す。 |

- 差分理由: 手本は plan が private。OS32 は公開値型を保ちながら、その値が直近 calc の出力であることを内部 guard で強制する。
- readbackは正規係数をO/hwdev/gf/clock.c:79–85と同順でdecodeした値と厳密一致させる。calc actualは単段がO/kdebug/gfinit_pll/solve.inc:28の丸め、readbackは切捨てなので差は最大1kHz、二段は同:43,56,60とclock.c:79,83,85の演算順が一致し差0。actualだけの比較でFAULTにしない。係数/muxの有効ビットも照合し、熱上限はdecode値で判定する。
- actual からだけの再計算は採らない。丸め後の周波数は元要求と異なり、等価な別係数への再探索で正規出力を拒否し得るため。
- guard は1組だけ。calc B は calc A を失効させる。同一値の複写は許すが、改変・旧世代・二度目 prog は拒否する。ROM generation 周回時は旧 guard 無効のまま再初期化する。

## 3. 部品不存在・perf 列挙・GPIO（A07/A13/B03）

```c
/* 内部rc末尾追加。数値は既存を動かさない。 */
/* GF5_ENODEV = -510: 表/配線を正しく解読して「部品無し」と確認した場合だけ */
struct gf5_perf_entry { struct gf5_clock clock; u8 voltage, fanspeed, pstate, bios_index; };
i32 gf5_volt_check_id(struct gf5 *, u8 id, u8 min_id); /* MMIO書込なし */
i32 gf5_fan_check_cstate(struct gf5 *, u8 percent);  /* MMIO書込なし */
```

| 手順／手本 | 契約・誤り |
|---|---|
| 対応判定 N/clk/base.c:178–192、N/volt/base.c:104–183 | volt/fan は部品不存在のみ ENODEV。未知表版・存在する未対応vmap・必要speedo無しは ENOTSUP、破損 EBIOS、候補無し ERANGE。id=0だけで部品不存在と決めない。DCB/電圧表の実在を先に確認する。 |
| perf事前処理 同上、N/clk/base.c:266–305 | entry解読→clock calc→fan/volt の全対応判定を完了してから RAM prog。check は表・要求の可否を確認し、GPIO/電圧/fan/PLLを書かない。OPTIONAL が無視できるのは ENODEV のみ。 |
| perf実行 N/clk/base.c:178–213 | 既定の RAM→fan上げ→volt上げ→core→volt下げ→fan下げを維持。事前拒否は RUN のまま、書込後の失敗は FAULT/受付閉、後続降圧なし。ENODEV は last/source に残して利用可能な部分のみ続行。 |
| pstate N/bios/perf.c:98–101、N/clk/base.c:421–425 | 全対応版で pstate=entry[0] を読む。有効entryだけ count、get/select は同じ密な有効indexを使う。bios_index は元位置の診断用。indexを生表位置として扱う旧診断を更新し、範囲外 EINVAL。全ffなら count=0、select拒否。 |
| GPIO N/gpio/base.c:80–105 | 診断を `gpio-get tag` / `gpio-set tag value` に変更。毎回 gpio_find で line/log を取得後、その同じ呼出し中に get/set。旧line/log構文は EINVAL（勝手にtagと解釈しない）。出力にはtag/line/logを残す。 |

- ENODEV を rc mapper で保持し、fan無しで温度監視まで止めない。ENOTSUP を「無ファン」に偽装しない。
- 差分理由: OS32 の公開indexを有効一覧に統一するため密に再列挙する。内部GPIO低水準口は既存のまま、未検証入力を受ける診断だけをtag化する。

## 4. 寿命と最小の状態

- 通常の F5 操作は RUN・受付・F2 RUN・fault 無し・foundation running・表示の受付と epoch を検査する。停止要求以後は拒否する。
- 熱監視の門は MMIO 窓と表示 epoch。初期化済みなら描画 FAULT/SAVED、受付閉鎖、通常 leave 後でも測温と fan を続ける。有効窓が無ければ触らない。
- 手本由来の状態は、4しきい値の HIGHER/LOWER、校正値・BIOS 表、fan mode/目標と sensor/auto/step/toggle の期限。
- OS32 の抑制状態は down_active、entry_clock、ceiling、cool_samples、critical_paused と対象 epoch。保存像へ入れず、restart/save/restore で停止要求や上限を巻き戻さない。
- 停止状態は常駐の `gf5_stop_state` 一つだけ。`NORMAL → REQUESTED → STOPPED` と最初の reason/milli_c/source_rc を保持する。F5 はその常駐状態へのポインタを借り、要求を直接記録する。module 内の pending/原因のコピーや常駐の独立 latch/pending は置かない。
- stop/save は plan を失効させる。新しい F2 epoch は古い pause 所有と冷却計数を捨てる。降速上限は保持する。

## 5. 前景 alarm と停止経路

- N/therm/temp.c:162–208 の初回＋1秒 alarm を、既存の前景協調点から `geforce_foreground_poll → gf5_service_poll → gf5_fan_tick` へ移す。描画、CUI キー待ち、sys_halt、yield/park、syscall 出口を通す。
- 常駐側は再入防止と次回 tick の cache だけを持つ。期限の正本は F5。最短期限を切り捨てで tick へ写し、遅延分を連続測定して埋めない。借用中や SAVING/RESTORING の slot=NULL は次の協調点へ送る。
- clock64 の一時失敗は診断を残して今回だけ終了し、次の前景 alarm で再試行する。明示再入場の monitor_restart でも監視を再準備する。
- 停止は `常駐 REQUESTED → module 借用を終えた poll 出口 → 共通 backend stop → 98 route` の一本。熱側で active/channel/display から独自の稼働条件を再構成して要求を捨てない。
- backend stop は active/routed を解消し、既存 display stop を通してから 98 へ戻す。下位が OFF かつ display stop 完了を確認済みなら、ハード停止を繰り返さず受付閉鎖・帰還・後処理を続ける。
- stop 失敗では98への帰還より先に既存の `gf_foundation_stop` による下位停止を試す。それも失敗するか帰還先が安全でなければ、REQUESTED のまま vfs_sync、理由・温度・stop/sync rc の polled serial 出力、割込み禁止 halt。成功していない停止を STOPPED としない。
- 停止と帰還を確認してから STOPPED とする。表示を使用中だった場合に描画状態を閉じ、その時の表示所有者の slot だけに abort_req を立てる。CUI は現在アプリ、GUI は既存の gfx owner を使い、別の実行中アプリや GUI 全体へ終了を要求しない。アプリ終了をハード停止の前に待たない。
- 再起動用 `gf_production_return` の経路は維持し、熱の halt 分岐を混ぜない。
- 保証は期限後最初の協調点での実行。非協調の長い計算中の測定/停止までの上限時間や toggle の精密波形は保証しない。

## 6. 熱の4段と復帰

| 段 | 動作 |
|---|---|
| 共通 | VBIOS の4しきい値/hyst。各有効測定で状態を更新し、shutdown を先に記録する。shutdown 時は pause/PLL を省き、fan の失敗でも要求を失わない。 |
| fan_boost | 100% を即時要求し AUTO を選ぶ。**fan_boost の100%は、BIOSの上限で抑えた最大 (nouveauどおり)**。N/therm/fan.c:45–46 と同じ min/max clamp を守る。active 中は MANUAL/NONE/perf からの低下を抑え、毎測定で再要求する。下降後は通常制御。不存在/失敗は記録して測温を続ける。 |
| downclock | 超過中は毎秒1段、有効 perf の core/shader が現値を超えず一方が低い候補を選ぶ。core 降順、同値 shader 降順、同値 BIOS index 順。RAM/電圧は変更しない。最低段では同値を書き直さない。 |
| 降速の照合 | prog 後の clock_read の core/shader を、二段係数と mux から decoded した値と照合する。calc.actual には二段 PLL の最終値でない値があるため使わない。不一致は失敗で停止要求。ceiling は読戻し成功後だけ更新する。 |
| 3回冷却 | downclock−hyst 以下の新しい測定が3回連続したら1段戻す。失敗・境界超過・critical 中は計数を戻す。開始 clock を上限とする同じ到達可能段列を使う。復帰の1段だけ熱ポリシーが許可し、失敗時は元の ceiling を保つ。 |
| critical | 上昇時は受付閉→clock→drain_pause。所有した pause と epoch を記録する。下降後は別の閉鎖理由が消えるまで毎測定で unpause を再試行し、成功後だけ受付を開く。新 epoch で依然 HIGHER なら pause を再適用する。 |
| critical 失敗 | 受付を閉じ plan を失効させ共通停止を要求する。熱独自の F5 FAULT は追加しない。下位の実故障による FAULT は既存契約に従う。 |
| shutdown | 直ちに常駐 REQUESTED。アプリの終了・降速完了を待たない。下降イベントでは解除しない。 |
| センサ喪失 | sensor alarm のみ解除。降速/pause 中の喪失は最初の下位 rc を共通停止へ記録する。独立 fan alarm は維持する。 |
| 停止中の測温 | PLL を動かせない停止済み/保存中の装置では測温/fan を続ける。降速不能だけを新しい shutdown 理由にしない。 |
| 外部操作 | clock/RAM/perf/state は共通の requested/decoded ceiling 検査を使う。直接 prog は MMIO/pause より前に検査し、保存像で新しい熱制限を上書きしない。 |
| 明示再入場 | STOPPED は自動 probe を拒否。解除は gf2 enter または再起動だけ。gf2 enter の準備は MMIO 寿命を確認し新規測温、downclock−hyst 以下を確認して監視を再準備し、その後 NORMAL にする。pause 所有・冷却計数は持ち込まない。 |

## 7. 次の実機の読み取り観測（gf-cui 欠け2）

- 手本: N/clk/nv40.c:39–114 のdecode、F2設計§6:167–186のfault/期限、F5設計§7:168–187のsave所有。観測は次回実機担当への手順であり、今回は実行しない。
- 新しい診断採取はMMIO readだけ。c040/4000/4004/4008/4020/4024を各32bitで読み、mux源、enable、M/N/P、core/shader/mem kHzとrcを出す。未知mux/無効PLLを0のまま記録し補正しない。
- 同じ行群に phase/accepting/busy/poll_off、ch有無/state/epoch/fault、f有無/running、display ready/epoch/saved_closed、image有無/complete、plan有効/世代、last/source/拒否境界名を入れる。ポインタは有無だけ。
- 有効MMIO窓が無ければレジスタは `unreadable`。running無しは状態として別記し、熱監視の可否と混同しない。NULLを追わず、読み失敗を0値へ偽装しない。採取はpollを呼ばず、元last/sourceを上書きしない。

| 順 | 同一シリアルログで採るもの／判定 |
|---|---|
| 0 | ROMのfan_boost/downclock/critical/shutdownと各hyst、現在温度、fanのmode/要求率/取得可能な実率、有効perf全段のbios_index/pstateと現在段をread-onlyで採る。未取得はunknown。判定欄に各「しきい値−平常温度」（同一単位）と測定時刻を記す。過熱やperf変更はしない。 |
| 1 | 通常enter成功の直後、他のF5操作の前に状態＋6レジスタ＋decode。clock-readも比較。ここで0のドメインと原因のregister条件を確定する。 |
| 2 | 既存saveの直前/直後を採取。imageが無ければrestore/freeの-509は像無しと記録、無理に実行しない。imageが有ればrestore前後、成功後free前後を採る。失敗像は既定契約で保持する。 |
| 3 | freshなRUN/有効予約の既存ケースで wait→tile-clear→wait、各前後の状態/rcを採取。tile-clear=-506→FAULT→wait=-509なら因果をそのまま記録。各waitの期限は開始時に新規設定。 |
| 4 | calcが-501だったログに続くprog=-509を、稼働門失敗と区別する。plan未成立なら書込み試行しない。未知muxへの初期PLL注入、pause条件の撤去、強制RUNはしない。 |

- 「読み取りだけ」は追加の採取を指す。save/restore/free/tile-clear自体は既存の状態変更操作であり、完全read-onlyの実機セッションでは手順1だけを実施し2/3は未実施と記す。

## 8. 実装の境界

- feature5.h は常駐停止の値型・参照と共通 ceiling 判定、clock/ramclock/perf/state は同じ判定を使用する。
- therm は測定・しきい値・fan・共通停止要求。perf は1段降速/復帰と decoded 読戻し照合。
- feature5_service は既存寿命の門と pause 所有/解除再試行。geforce_module は常駐状態・協調点・停止消費。backend は通常と共通の停止口と明示再入場の準備。
- gfx の表示停止時の状態閉鎖を、通常故障の GUI 全体への通知から分ける。熱停止は表示所有者だけを回収する。
- KAPI/SDK と公開 plan の配置は変更しない。

## 9. 検証

- kernel ビルドと、熱・U6・foreground・critical・backend・fancheck・perf・state・clock・gf-display の既存 host 試験を変異なしで通す。F1/N1/N2/M1 の既存ケースを維持する。
- 直接 core/RAM の上限拒否、decoded 読戻し不一致、clock 再試行、critical 解除再試行、停止失敗の halt、表示所有者だけの回収、最初の停止理由の保持を実行時 assertion で検査する。
- 各追加検査に一意の変異を置く。正常 GREEN → 変異 runtime RED → 復元 GREEN。移動・削除した製品行に依存する既存変異も移す。コンパイル失敗は RED としない。
- lint と diff の空白検査を実施。全体検査と実機の意図的過熱はこの段では行わない。実機の読み取り観測は §7。

## 10. 未決

ユーザー判断の未決は無し。実機での冷却性能・温度安全性は host 合格から推定しない。

## 実装の段で決めたこと

- 2026-11-07: 常駐 gf5_stop_state を正本にし、F5 は直接参照する。最初の理由/温度/rc を上書きしない。
- 2026-11-07: 共通停止失敗では既存 foundation 停止を使う。下位停止も失敗した場合は REQUESTED を維持して帰還先無しと同じ halt に進む。
- 2026-11-07: 熱停止の GUI 全体終了要求を除き、停止前に採った表示所有者の abort_req だけを立てる。通常の表示故障の通知経路は維持する。
- 2026-11-07: critical の解除保留は同世代の次の有効測定で再試行し、新世代の HIGHER も再適用する。clock64 の一時失敗は次の alarm で再試行する。
- 2026-11-07: 熱復帰の1段を prog の共通上限検査へ渡す間だけ許可し、失敗時は元の上限へ戻す。読戻しは calc.actual ではなく係数からの decoded と比較する。
