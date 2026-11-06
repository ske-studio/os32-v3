# GF_GUARD_THERM_DESIGN — GeForce の手本のガードの追記: 層 5 の時計・熱・perf (熱のしきい値ごとの処理・緊急停止・plan の照合・次の実機の観測)

> 出所: ~/os32-tmp/run/gf-addB/DESIGN.final.md (astra 起草、Opus 2 回のレビュー、2026-11-06 確定)。

> 状態: **現行 (2026-11-06)** — 実装は票 TASK_GF_FEATURES の段。洗い出しの元は ~/os32-tmp/run/guard-gf/report.md。

# GF 層5 確定設計への追記 r2 — 時計・熱・perf（2026-11-06）

## r1→r2 の変更

- P1-1: §6をfan_boost/downclock/critical/shutdownの4段に修正し、§10の帰還質問を削除。
- P1-2: emergency.pause→drain_pause/unpause、critical中の受付閉鎖とESTATEを確定。
- P1-3: 所有者検査を通らないgf_production_return、GUI/CUI回収、帰還先無しの停止を選択。
- P1-4: 熱の門をMMIO窓＋therm初期化に分離し、FAULT/SAVEDでも監視を継続。
- P2-1: stop_pending消費を前景poll出口に固定し、syscall出口に期限比較を追加。
- P2-2: §7に4しきい値/hyst・温度・fan・有効perfと平常温度との差を追加。
- P3-1: §1 #9/#10をmainのPORT_FLOW §4-1・gfx_hal §9参照へ訂正。
- P3-2: 単段PLLの丸め差を1kHz以内、二段は一致とし、係数decodeによる厳密照合を追加。
- P3-3: kbd.cへの処理追加をkbd_fgpoll.incへ分離する段に変更。
- P3-4: downclock/critical/shutdownを分割し、U6と対応変異を追加。

- 対象実物: `/home/hight/wt/os32-v3/gf-cui`、HEAD `b91b27af`。コード無変更、設計のみ。
- O/ = 上記作業木、R/ = `/home/hight/os32-tmp/refs/geforce6200/`、N/ = R/nouveau/drivers/gpu/drm/nouveau/nvkm/subdev/。
- N は INDEX.md の c03c1a1。以下の clk/therm/fb/gpio/bios は MIT の手本。GPL のコード・構造は移さない。
- メーカー資料: R/nvidia-open-gpu-doc/classes/host/cl446e.h（DMAチャネル）。NV44A の熱・PLL 全体を記述する公式公開資料ではない。該当列は N の既存設計根拠を継続する。
- 単一タスク・協調切替・整数演算、C1〜D3、F2/F4 の保存像所有を維持。GUI/SDK に GeForce 固有処理を追加しない。
- 本文は F5 §2〜5 と追記の前景 poll 契約を以下の範囲だけ更新する。実機合格・安全温度の実証とはしない。

## 1. 実物の確認

| # | 前提との食い違い／現物 | 追記 |
|---|---|---|
| 1 A05 | O/hwdev/gf/clock.c:143–153 は公開 plan の nc/nn/sc/mux を無照合で書く。N/clk/nv40.c:146–195 は private な計算結果を使う。 | §2 |
| 2 A06 | O/hwdev/gf/ramclock.c:160–178,214–224 は ROM/phase を検査するが mc/mn の出所を検査しない。N/fb/ramnv40.c:34–63,115–147 は calc の保持値。 | §2 |
| 3 A07 | O/hwdev/gf/perf.c:184–194 は ENOTSUP 全般を無視。O/hwdev/gf/volt.c:325–334 の存在する未対応 vmap も該当。 | §3 |
| 4 A10 | O/hwdev/gf/feature5_service.c:11–19,54–61,91–96 は phase 中心。O/hwdev/gf/display/service.c:418–419,438 の accepting 開閉は修正済みだが、実チャネルとの照合は残る。 | §4 |
| 5 A11 | O/drivers/video/geforce/gf_hook.c:46–48 と backend_geforce.c:81–83 の描画 poll だけでは静止中に監視しない。O/kernel/irq/tick.c:20 の dispatch は IRQ 内。 | §5 |
| 6 A12 | O/hwdev/gf/therm.c:623–646 は shutdown 上昇時の fan 要求のみ。emergency は任意口で production 未結線。更新された4段のユーザー決定を満たさない。 | §6 |
| 7 A13 | O/hwdev/gf/perf.c:111–143 は entry[0] を読まず、148–159 は生 count/index。N/clk/base.c:421–425 は pstate=ff を除外。 | §3 |
| 8 B03 | O/kdebug/gf5_test.c:422–432 は任意 line/log を通す。O/hwdev/gf/volt.c:208–219 は line<14 だけ検査。N/gpio/base.c:80–105 は都度 DCB 照合。 | §3 |
| 9 指定節 | main `/home/hight/os32-v3/docs/tasks/agents/PORT_FLOW.md`:77–92 の §4-1。O/との差を文書欠落と扱わない。 | 手本の状態・順序・失敗後の守りを照合 |
| 10 指定節 | main `/home/hight/os32-v3/docs/design/include/gfx_hal.md`:123–154 の §9。 | HALの面/世代検査とドライバの状態/後始末を分離 |

- 製品の食い違い8件、参照先の訂正2件。材料は guard-gf/report.md:26–34,42,64 と gf-cui/report.md:20–23,34–35。

- `-509` は食い違いとして断定しない。O/hwdev/gf/clock.c:104 の周波数0、O/kdebug/gf5_test.c:349 の plan 無し、:463 の像無し、:314 の非RUNを分離する。
- F4 start の accepting 修正を重ねてやり直さない。F4 保存中は閉鎖し管理口だけ残す（F4設計:372以降、gf-cui報告:9–14）。

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

## 4. F5 の実稼働門と寿命（A10）

```c
/* service.c 内部。ctxはproductionだけが所有、呼出し側は借用。 */
static i32 service_live(struct gf5 *g); /* 正常なら0、停止/世代違い ESTATE */
/* live条件: ctx一致、phase RUN、accepting、!busy、ch && ch->f、
 * ch->state==GF2_RUN、!ch->fault.latched、ch->f->running、
 * display->ready/accepting、!changing、!saved_closed、display->epoch==ch->epoch!=0 */
```

- 手本: N/therm/base.c:343–357、N/clk/base.c:591–596。通常の操作service_*だけがlive門を通す。busyは明示操作EBUSY、周期は再入せず次の前景点へ延期。
- 熱専用 `thermal_live(g)` はctx/世代一致・有効MMIO窓・therm初期化済みだけを要求し、fault.latched/running/accepting/phase RUNを要求しない。fan_tick/service_pollはこの門でFAULT/SAVED/critical中も測定・fan制御する。窓撤去前に監視解除し、窓無しはMMIO0。降速書込みは別途実行可否を検査しFAULTでは再programせず保護停止へ進む。
- 非稼働の通常明示診断は ESTATE。slot は停止時に通常操作を許さない（古いopsポインタでも門で拒否）。
- management の restore/free/last_rc は通常門と分離。SAVED/FAULT の借用像を ch RUN 条件で拒否しない。SAVING/RESTORING は slot=NULL を維持。
- 一操作の開始で busy=true、出口で必ず解除。内部 perf→RAM→clock、restore の呼出しは公開門を再通過させず、内部契約で実行する。
- 差分理由: 協調系では worker join の代わりに、非再入と実状態の門で寿命を閉じる。部品自身の失敗を描画の rc に返さない現実装を維持する。

## 5. 描画に依存しない前景周期処理（A11）

```c
/* geforce_module.h に宣言、geforce_module.c に実装。kernel常駐側。 */
void geforce_foreground_poll(void);
/* accessorを毎回取得→有ればops->service_poll(ops->ctx)→共通出口でstop_pending消費。
 * 常駐側の再入フラグを入口/出口で管理。module内ctx/opsをキャッシュしない。 */
```

| 結線点（実物） | 新しい呼出し位置と責任 |
|---|---|
| O/exec/exec_schedule.inc:95–116,138–161 | exec_park_poll/exec_sys_yield の文脈確認後、park/各haltへ移る前に1回。CUI の早期halt経路にも届かせる。 |
| O/drivers/input/kbd.c:736–788（993行、実体は新規kbd_fgpoll.incへ） | CUI getchar/getkey の待ちループ各周回（halt前）。trygetchar/trygetkey 入口にも追加、キーの有無に依存させない。 |
| O/exec/exec_syscall.inc:159–164 | ring3_abort_check直前で常駐の次回期限と現在tickだけ比較し、期限到来/pending時だけforeground_poll。present/get_tickのみのループも覆う。 |
| O/kernel/sys.c:37–39 | sys_halt の halt 前。アプリの静止待ちを覆う。 |
| O/drivers/video/geforce/geforce_module.c:36–37 | 上記の共通入口をここに置く。実際の時計と次回期限は F5 だけが所有する。 |

- 手本 N/therm/temp.c:162–208 の初回＋1秒再予約を、上記前景入口→service_poll→fan_tick に接続。IRQ0 は既存tick更新だけ、追加MMIO/待ち/printfなし。描画pollも共存可、期限で重複実行を抑える。
- syscall側はMMIOせず、常駐adapterがF5の最短期限をtickへ保守的に変換した値を借りて比較する（初回は期限到来扱い）。期限の正本はF5のみ。SAVING/RESTORINGのslot=NULL期間は次の前景点へ延期し、pendingは常駐側に残す。
- u64 clock64（hi/lo合成）を使用。sensor は now+1,000,000µs、遅れた分を連続実行して埋めない。fan step/toggle は各既存期限を使い、入口を1秒で間引かない。
- 差分理由: alarm worker を既存の協調点に移す。`tick_dispatch` や `_halt` inline 全体のフックは IRQ/IF=0 呼出しまで巻き込むため採らない。
- 保証は「期限後の最初の協調点で実行」。非協調の長い計算中の1秒上限は保証できない。長時間処理は既存 sys_yield を定期的に呼ぶ契約とし、遅延時間/最大間隔を診断する。IRQからの強制切替は追加しない。
- toggle の精密な100ms波形も協調頻度を超えて保証しない。今回の実機で採用したPWM/無ファンと、toggleのhost時刻試験を混同しない。

## 6. 熱の4段と緊急帰還（A12）

```c
/* gf5所有。snapshotで巻き戻さず、停止後の再入場禁止は常駐側にも保持。 */
struct gf5_heat_state {
    bool down_active, critical_paused, stop_pending;
    u32 cool_samples, epoch;
    struct gf5_clock entry_clock, ceiling;
    i32 last_rc;
};
i32 gf5_heat_step(struct gf5 *, const struct gf5_thermal *, u64 now); /* perf.c */
/* emergency.downclock/pauseのownerはproduction gf5。pause adapterはservice.c。 */
/* 常駐adapterへ停止理由・温度・rcを値渡し。moduleのポインタを保持しない。 */
void geforce_heat_stop_request(i32 reason, i32 milli_c, i32 source_rc);
```

| 段／条件 | 決定・手本との差分 |
|---|---|
| 共通 | VBIOSの4しきい値/hystを使う（N/therm/temp.c:104–158）。成功した毎秒測定で判定し、shutdownを最優先する。edge通知だけに依存せずdownclockの継続/復帰を毎秒heat_stepへ渡す。fan失敗でも熱判定を続ける。 |
| fan_boost | temp>=fan_boostで100%要求（既存ROM clamp）。active中はMANUAL/NONE/perfからのfan下げを抑える。tempがfan_boost−hyst以下なら通常制御へ。不存在/失敗は別記録し監視を継続（N/therm/fan.c:32–74）。 |
| downclock | emergency.downclockをperf.cの熱状態へ結線。temp>=downclockで初回即時1段、以後超過中は1秒に1段下げる。有効perfのcore/shaderが現値を両方超えず一方が低い候補を、core降順・同値shader降順・同値bios_index順に選ぶ。calcとdecode上限検査が成功する隣の有効段を使う。mem/電圧は固定、core progのみ（N/clk/nv40.c:146–195）。 |
| 下限と復帰 | 最低有効段では同値の再書込みをしない。downclock−hyst以下の有効1秒測定が3回連続したら隣の有効段へ1段上げ、計数を0へ。上げた後の次測定で再超過すれば直ちに1段下げる。測定失敗/冷却境界超過で計数0。開始時entry_clockを上限とし、超えるBIOS段へは戻さない。候補は降下と同じ到達可能な段列を使う。 |
| critical | O/therm.c:643–646のemergency.pauseをfeature5_service.cのadapterへ結線。上昇時は受付を閉じ、O/feature5.h:80–81のlinks.drain_pause(owner)を1回呼ぶ。走査/表示設定には触れない。critical−hyst以下ならlinks.unpause、成功後だけ受付を開く。SAVED/FAULTなど別の閉鎖理由がある間は開かず、critical自身が取得したpauseだけを解除する。 |
| critical中の入口 | F2描画入口は既存GF2_ESTATE (-207、O/command.h:13)、F4公開口はGF4_ESTATE、F5通常操作はGF5_ESTATEへ既存mapperで伝える。待機キューに積まず受付閉を維持。void描画口は既存last/sourceへ記録。熱の測定/fanと管理停止だけは通す。pause/unpause失敗はFAULT/閉鎖維持＋stop_pending。 |
| shutdown | temp>=shutdownで直ちに停止要求。降速完了やアプリ終了を待たない。前景poll出口で再入防止を保って1回消費し、`gf_production_return()`→`geforce_return()`→platform stop→pc98_route_to_98を使う（O/kernel/sys.c:24–27、O/drivers/video/geforce/geforce_module.c:77–82）。停止失敗も記録して帰還を試みる。N/therm/temp.c:118–127のpoweroffとの差はユーザー決定。 |
| 帰還先無し | geforce_returnにpc98_route_safe()==0分岐を追加。GeForce stopを先行し、vfs_sync（O/fs/vfs.c:738）を試み、理由/最後の温度/stop・syncのrcをシリアルへ記録して割込み禁止のhaltループへ。stopの成功だけを電源断/冷却成功と呼ばない。 |
| 故障 | 下段候補無しだけでは停止せず最低段維持。clock読取/計算/program/readback失敗、熱抑制中のsensor喪失、FAULTで必要な降速不能はstop_pendingに理由を残して保護停止。fan無し単独では停止しない。 |
| 外部操作と再入場 | 抑制中は直接clock/RAM/perfからceilingを超える要求をEBUSY。save/restoreも熱状態を巻き戻さない。shutdown後は自動再入場禁止。常駐latchはstop/saveで消さず、有効な新しい測定がdownclock−hyst以下になった後の明示gf2 enter/gfxmodeだけを許す。停止後も窓が有効なら測定継続、窓が無ければ明示操作の準備段で描画を開く前にsensorを初期化・測定し、失敗なら拒否。 |

- PM決定の3回・1段復帰はOS32方針。手本のalarmを協調前景に移すため、検出は§5の最初の協調点。critical中は昇速を保留し、unpause後に新たに3測定を数える。
- `gfx_shutdown`は使わない。O/gfx/gfx_core.c:433–440のcaller/owner検査により、GUIの非所有アプリが停止要求を消費すると無処理になるため。`gfx/gfx_emergency.c`はこの作業木には存在せず、Trident枝wt/tri-intの物である。
- ハード停止/98帰還の**後**に`geforce_display_failed`（O/gfx/gfx_core.c:459–465）でGUIへ通知。O/kernel/gui.c:241–247は既存SessionActionを要求し、session.rs:234–259はslot無しをkill予約、残りも猶予後killする。multiapp.rs:428–439は全aliveを対象とし全画面も除外しない。したがってGUI全画面も回収対象だが、回収猶予をハード停止の前に置かない。
- CUI全画面ではappslot_abort_request（O/exec/appslot.c:370–377）で現アプリへ要求し、syscall出口ring3_abort_checkで畳む。GUIの現在実行中の全画面にもabort要求を立て、停止中の別アプリはWM回収に任せる。poll自身はlongjmpせず、再入フラグ/借用ctxを解放してから既存出口へ戻す。98上で警告を表示し、元rc・温度は帰還前に値として保存する。

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

## 8. ファイルごとの変更一覧（各行1機能・1ファイル・1段）

| 段 | O/ファイル | 内容／並列の可否 |
|---|---|---|
| D0 | hwdev/gf/feature5.h | 型・ENODEV・内部口・gf5欄の凍結。最初、並列不可 |
| D1 | drivers/video/geforce/geforce_module.h | 前景poll宣言。D0後 |
| D2 | tools/tests/gf5_fake.h | guard/世代・read/write/pause回数・thermal時刻の土台。D0後、D1と並列可 |
| P0 | hwdev/gf/clock.c | private solve/guard検証・core消費。D2後、V0と並列可 |
| P1 | hwdev/gf/ramclock.c | RAM入口guard検証・消費。P0後 |
| P2 | hwdev/gf/feature5_rom.c | ROM世代更新とguard失効。P0後、P1と並列可 |
| V0 | hwdev/gf/volt.c | ENODEV分類・事前対応口。D2後 |
| V1 | hwdev/gf/therm.c | fan分類・書込なしcheck。V0後 |
| V2 | hwdev/gf/feature5_links.c | ENODEV mapper保持。D0後、P/Vと並列可 |
| E0 | hwdev/gf/perf.c | 有効pstate一覧・index統一。D2後、P/Vと並列可 |
| E1 | hwdev/gf/perf.c | 全対応事前判定。E0/V1/P1後、同ファイル直列 |
| L0 | hwdev/gf/feature5_service.c | 実稼働門・busy・管理口・失効。P2後、gf-cui取り込み前提 |
| L1 | hwdev/gf/display/service.c | start/stopでguard失効と熱状態世代管理。L0後、既存受付修正保持 |
| T0 | hwdev/gf/perf.c | downclock毎秒1段・3測定で1段復帰・復帰上限。E1後 |
| T0c | hwdev/gf/feature5_service.c | criticalのdrain_pause/unpause・受付閉・独立thermal門。L0/T0後 |
| T1 | hwdev/gf/therm.c | 4しきい値→heat_step/pause/停止要求・最大fan維持。V1/T0c後、同ファイル直列 |
| T2 | hwdev/gf/state.c | save/restore時の熱上限維持。T0後、T1と並列可 |
| F0 | drivers/video/geforce/geforce_module.c | accessor経由前景poll・再入抑止。L0/D1後 |
| F0s | drivers/video/geforce/geforce_module.c | shutdown消費・所有者非依存の帰還・pc98以外のsync/停止・再入場latch。F0/T1後、同ファイル直列 |
| F0e | exec/exec_syscall.inc | 出口の期限比較とpending時poll。F0s後 |
| F1 | exec/exec_schedule.inc | poll/yield結線。F0後、F2/F3と並列可 |
| F2 | drivers/input/kbd_fgpoll.inc（新） | CUI待ち/try用adapter実体。F0後 |
| F2i | drivers/input/kbd.c | includeと既存ループ/tryからの最小呼出しのみ。F2後 |
| F3 | kernel/sys.c | sys_halt結線。F0後 |
| Q0 | kdebug/gf5_test.c | GPIO tag化。gf-cui後、E/Tと並列可 |
| Q1 | kdebug/gf5_test.c | 読取観測とindex/plan診断更新。Q0後、同ファイル直列 |
| U0 | tools/tests/gf5_clock_host.c | P0受入。P0後、他hostと並列可 |
| U1 | tools/tests/gf5_ramclock_host.c | P1受入。P1後 |
| U2 | tools/tests/gf5_volt_host.c | V0受入。V0後 |
| U3 | tools/tests/gf5_perf_host.c | A07/A13/降速段/復帰受入。T0後 |
| U4 | tools/tests/gf5_therm_host.c | 時刻・無ファン・閾値受入。T1後 |
| U5 | tools/tests/gf5_state_host.c | 保存像と熱上限受入。T2後 |
| U6 | tools/tests/gf5_foreground_host.c（新） | 本番service/module/前景入口・GUI非所有アプリ・CUI全画面・FAULT監視・GUI全画面の既存回収接続・帰還先無し・再入場を試験、診断dispatchのGPIO/観測試験。F0e/F1〜3後 |
| U7 | tools/tests/test_gf5.py | CHECK inputs・各host登録・全変更アンカー移設。最後の単独所有者 |

- 新規import/KAPIなし。内部型の全欄走査・位置初期化は実装D0で列挙。既存gf5_fake.h:61–68のctx内包、stateのfan深いコピーを追従させる。
- 試験は libcなし/qemu、Nの手元ファイルをCIから読まない。出典付きfixtureを保持。製品増分目安8KiB、常駐状態512B以内、heap配列を増やさずROM走査。像の上限は既定値を動かさず実装時に測る。

## 9. 受入各項目 → 試験と変異

| 項目 | host／実機 | 実行時REDにする変異1行 |
|---|---|---|
| A05 | host:未calc/改変全欄/他要求/ROM世代/再progを無書込拒否、正規plan成功。実機:正常calc/progは別途承認範囲 | coreのguard不一致判定をfalseへ |
| A06 | host:旧epoch/改変mc,mn拒否でpauseも0、RAM→coreは成功。実機:既定RAM復旧ケース | RAMの照合結果を0へ |
| A07 | host:無部品は継続、存在するvmap未知版/speedo不足はRAM/fan/volt/PLL全書込0。実機:ROM対応一覧記録 | OPTIONALの許可をENODEVからENOTSUPへ |
| A13 | host:先頭/中間/末尾ff、全ff、密indexとbios_indexを比較。実機:ROM一覧照合 | pstate==ffの除外条件をfalseへ |
| A10 | host:通常操作はch無し/f無し/OFF/fault/epoch不一致/stop/保存中でMMIO0、管理freeは可能。熱は有効窓/初期化済ならFAULT/SAVEDでも進む。実機:enter/leave/save巡回 | running条件をtrueへ |
| A11 | host:描画0のCUI待ち/sys_halt/yieldでsensor時刻進行、IRQ内MMIO0、遅延後1回、再入抑止。present/get_tickだけでも期限後停止要求を消費。本番組立で確認。実機:静止画ログ間隔 | 前景adapterがservice_pollを呼ぶ条件をfalseへ |
| A12 downclock | U3/U4:超過毎秒1段、無fan/最低段、冷却2回は不変・3回で1段、等値/失敗でreset、再超過で降下、save/restore上限。実機:平常監視のみ | downclock activeのheat_stepをedge限定へ |
| A12 critical | U6:受付ESTATE、表示走査維持、hyst未満でunpause、別の閉鎖理由では再開無し。実機:意図的過熱なし | criticalのdrain_pause呼出しを省く |
| A12 shutdown | U6:GUI非所有アプリ/CUI全画面から即stop→98→回収、GUI全画面回収、route不可でstop→sync→serial→halt、明示再入場の温度門 | shutdownの98帰還を省く |
| A12 FAULT | U6:FAULT/SAVEDでsensor/fan継続、shutdownで保護停止、窓撤去後はMMIO0 | thermal門にfault.latched拒否を足す |
| A05 丸め | U0/U3:単段の丸め差1kHzで成功、二段一致、係数改変/未知muxは拒否。実機:§7のdecode比較 | readbackをactual完全一致へ戻す |
| B03 | host:偽line/logの旧構文拒否、tag解決したDCB方向/極性だけ書く。実機:read-only tag一覧 | 診断gpio_findのlogを固定値へ置換 |
| 観測 | host:窓無し/読取失敗/unknown muxを区別しwrite0、元rc保持。実機:§7の条件ごとに原因と未確定を記録 | レジスタ読取失敗を成功0値へ |

- 変異の実装は一意アンカー・正常GREEN→変異runtime RED→復元GREEN。既存アンカーは `rg -n` で tools/tests を調べ移設する。コンパイル失敗をREDに数えない。
- 実装の検証: PACKで指定する基準SHAのpreflight、make all（kernel/module/診断波及）、触れた試験 --mutate、lint。本設計作業では作業木read-onlyのためpreflight --fix-fonts/ビルド/試験/実機を実施していない。

## 10. 未決（ユーザー判断0件）

- PM決定(a)〜(d)を§6へ反映済み。98帰還の可否・復帰3測定・critical受付・帰還先無し・shutdown後の明示再入場は未決に戻さない。

反論: なし（0件）。

## 確定 (2026-11-06、Opus の第 2 回、PM が反映)
本文の §6 (156・157 行) と §8・U6 は、下の 2 点で読み替える。食い違ったらこの節を正とする。
- R2-1 (P1、shutdown の後に自動で戻る): shutdown の消費は、既存の geforce_fail (drivers/video/geforce/backend_geforce.c:440-445) と同じ形で backend の stop を通す (production の stop に加え、backend の active・routed を 0 にし、display_failed を呼ぶ)。gf_production_return → geforce_return (geforce_module.c:77-82) だけでは backend_geforce.c:97 の active/routed が残り、次の gfx 開始で geforce_probe (:393) が ok・geforce_enter (:618) が何もせず止めた装置に present する。再入場の latch は backend が見える所に置き、geforce_probe で見て 0 を返し pc98 に落とす (決定 (d): 冷えた後の明示の操作だけが latch を下ろす)。§8 に backend_geforce.c の段を足す。U6 に「shutdown の後、別のアプリの gfx 開始が pc98 に落ちる」と変異「probe で latch を見ない」。
- R2-2 (P1、戻り先の無い機械の停止): 「GeForce を止め、FS を sync、シリアルに理由と温度を残して halt」は熱の停止を消費する側 (F0s) にだけ置く。geforce_return は変えない (sys_reboot sys.c:31 からも呼ばれ、route が使えない状態の再起動が halt で止まり platform_reset に届かなくなるため)。U6 に「route が使えない状態で再起動すると reset に届く」。
- P3 (推測): syscall の出口から呼ぶと caller_access_leave の後に g_gui_handler を呼ぶ文脈になる。U6 でこの位置から呼ぶ場合を 1 件通す。
- P3-2 (丸め) はレビュー未確認。実装の段で clock_read と calc の式の一致を file:line で確かめ、違えば止めて報告。
