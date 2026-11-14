# IO_TIER_DESIGN — I/O の Tier 制御 (協調点の Tier・予算・設定表・pump)

> 状態: **現行 (2026-11-14)** — 確定 (ユーザーの草案、astra 起草 r1・r2、Opus 5.5 の独立レビュー 2 回、残りを PM 決定で反映)。段の状態は票 TASK_IO_TIER が持つ。
> 出所: 往復の版は `~/os32-tmp/run/io-tier/` (PACK) と `~/os32-tmp/run/io-tier-r1/` (DESIGN.r1・r2・final、REVIEW.r1・r2)。LAN 一括転送との口は LAN_BULK_DESIGN §5。



## r1→r2 の変更
- R1: PPCのFG処理をPR=1由来syscallに限定し、使用中trapスタックでEEを許可しない。最終N4で共通口へ一本化。
- R2 / U1: 初版は案B。IRQ出口・期限救済・starved_rounds・出口能力を削除し、案Aは利用者決定後の別票へ送る。
- R3: LANの口をio_slice/int結果/io_clock_usへ統一。net/bulk_work.cと予算初期値はLAN側所有、D6を削除。
- R4: link_tickのIRQ0→前景移行はLAN側と明記。
- R5: アプリ表はownerの終了時に適用前へ復元、恒久表はユーザーの起動時設定。
- R6: io_descにbudget_min_us/max_usを追加し、既知ID・制約の正をio_policy.cの定数表とする。
- R7: LINK_IDLE/net_txの協調点を追記し、io_dispatchの入れ子は無動作で戻す。
- R8: Tier別pending bitmask・先頭ビット・cursor、空時の即return、巡回入口での生count換算と読出回数を定義。
- R9: 案Aの別票にx86のlongjmp経路とIRQ_LEAVEの順序検証を送る。
- U2: 不正表は全体拒否し直前の有効表を保持。初回は標準、標準復帰はreset。
- LAT: 4MiB転送中の入力→応答/表示p95≤50ms・max≤100msを唯一の遅延合格線とし、増分は参考記録へ変更。

## 1. 実物と前提
- 確認基点: main `9e39176a4871cb89228c069389e0791d436a0dd2`。以下の file:line はこの基点。
- 単一CPU・単一タスク・協調切替。認証等は対象外。整数演算、物理1kHz・論理100Hzを維持。
- POLICY_DEV §1の判断: x86/PPCには同じtick・時計の役目がある。順位・設定・予算を共通実装し、時計providerと復帰経路だけ分離する。AC-TIME-1/2・AC-SYNC-1・AC-KAPI-1と整合させ、新契約を先に追加する。
- 本稿の予算は割り込み時間も含む経過時間。CPU時間の保守的上限として使い、厳密なCPU時間会計は初期対象外。
- 草案・PACKの前提との食い違いは次の **5件**。登録容量4は登録済み4本という意味ではない。

|番号|実物の確認|設計への反映|
|---|---|---|
|X1|idle_workはIF/EE=1・IRQ深さ0限定、再入禁止、最大4本を1周。優先・期限・予算なし（kernel/idle_work.c:6,12,26）。sys_halt以外に入力待ち・syscall入口・協調切替から呼ぶ（kernel/sys.c:42、drivers/input/kbd.c:736,774,800、exec/exec_syscall.inc:94、exec/exec_schedule.inc:150、net/link.c:64のLINK_IDLE、net/nic.c:151のnet_tx末尾）|協調点はこれら全体。現登録者はNE2K・NIC RX・TSC校正の3種類|
|X2|NE2Kの8件/120µsはFG1のdemux（net/rx_work.c:17,24、net/nic.c:225）。ne2k_bh内で共有（drivers/net/ne2000/ne2000.c:523,553）。IRQは印付け（ne2000_irq.inc:54）|論点4の「IRQ内なのでTier外」は採らず、FG1予算をTierと合成|
|X3|PCMはIRQ10から補充（drivers/sound/cs4231/pcm_cs4231.c:478）、IRQ0のpcm_tick救済も残る（:411,437,484）|T3は全面的なIRQ10専用化ではない。当面は両経路をTier外の装置保証として保存|
|X4|e82557にはidle不足2jiffyでIRQ0のdemux救済がある（net/nic.c:298）。同箇所に独立のpending判定は無い|「IRQでは要求記録のみ」は現状の例外を伴う。今回この救済を削らない|
|X5|PPC syscallはEE=0でidle_workが進まない（exec/exec_syscall.inc:90、arch/ppc/kernel_trap.c:104）。IRQ出口のTier処理も無い（同:98、arch/x86/kernel/isr_stub.asm:95）|PPCのPR=1由来syscallのFG pumpだけ追加。IRQ出口は作らない|

- tick_dispatchは7slotを順に1周し既存tick_budgetを挟む（kernel/irq/tick.h:5、tick.c:25）。IRQ0の論理周に実行（arch/x86/kernel/isr_stub.asm:493、isr_handlers.c:549）。PPCもjiffyで実行（arch/ppc/kernel_dec.c:60）。
- 設計のみ。preflight --fix-fonts・build・lint・変異・guestは未実施。依頼にbase指定はなく、作業木は読み取り専用。試験合格は主張しない。

## 2. 実行層と論点1（U1決定: 初版は案B）
|層|実行するもの|制約|
|---|---|---|
|IRQ本体・Tier外|ACK/EOI、時刻更新、既存tick_dispatch、T3 PCM、e82557救済、要求の印付け|既存の停止・予算契約を維持。Tier設定では変更しない|
|IRQ本体・Tier外のlink_tick|FG移行後もIRQ0の有限救済を残す|link_tickはLAN一括転送の設計で前景へ移行中。FGが10 jiffy linkを回していなければIRQ0が1回回す（e82557救済と同じ型）。移行・救済の所有者はLAN側|
|協調点FG1|Tier1→2→3、予算、公平枠。入力/GUI後続・LAN一括転送等|全Tierがアプリの協調点依存。明示io_pumpでも進める|

|比較|案A: IRQ出口でもTier1（別票へ）|案B: 全Tierをyield責任（初版採用）|
|---|---|---|
|yieldしないゲーム|対応サービスをOSが進められる|Tierは止まる。現T3による用意済みPCM補充は継続|
|Win32/DirectXの音|将来のOSミキサ等の利用者が決まってから検討|互換APIにpumpを置けるが無呼出ループの進行は保証しない|
|既存T3|移行時に二重補充・IRQ0救済を再検証|現IRQ10補充・IRQ0救済を維持|
|作りの単純さ|実行文脈と復帰経路の追加が必要|既存協調点を拡張。現在の利用者で成立する最小構成|
- 案AはTier1の利用者（OSミキサ等）が決まった時に別票で起こす。初版はIRQ_TAIL、出口能力、期限救済、starved_roundsを実装しない。
- R9の送り項目: 案Aのx86出口はv86 timeout・ring3_abort_checkのlongjmp経路の後、IRQ_LEAVEの前であることを別票で検証する。
- その他のTier外: DMA停止/復旧の不可分手順と機器固有の短い排他。長いPIO/RDC/resetはcallbackの単位超過として別計測。
- PPCの既存syscall本体はEE=0を既定とする。arch/ppc/io_pump.c（P1）に内部口 `int ppc_syscall_fg_run(int (*fn)(void *), void *arg)` を1つだけ置く。PR=1由来・vector=0xc00のsyscall内で、共有状態を解放済みのアプリ用kernel stackから呼び、EEを一時許可して処理後に元の状態へ戻す。PR=0由来trap・IRQからは呼ばない。
- 根拠: arch/ppc/kernel_vectors.S:340はPR=1用stack、:329はkernel trapのbusy取得、arch/ppc/kernel_trap.c:104はPR=1 syscall分岐。共通口は保存済みユーザーフレームを保ち、trap busyを保持していない安全な文脈でだけEEを許可する。通常のkernel EE方針・IRQハンドラは変更しない。fnは内部処理でありアプリcallbackではない。
- io_pump（明示呼出・正常syscall復帰時の自動pump）とLANの待ちループ（LAN設計のJ）は同じppc_syscall_fg_runを使い、EE許可の別経路を作らない。明示pumpの予算/結果を受け渡し、同じ復帰で自動pumpを二重実行しない。割り込みは既存の1回のtrap処理で復帰し、IRQ内でEEを再許可する構造を導入しない。

## 3. 時計・設定・LANの境界（論点2〜5）
- AC-TIME-4（新）: io_clock_usはIRQ/FG0/FG1から読める単調µs時計。成功時のみ出力有効、未準備NOSYS・一時失敗AGAIN。短いirq_save/restoreを内包し、源・分解能・停止中進行をAC-TIME-2と揃える。
- x86はT1のTSC/PIT連続時計（platform/pc98/clock_source.c:78）、PPCはupper/lower/upperのTB時計（platform/powermac/clock.c:30,75）。LANが必要とするµsの読み口はio_clock_usに統一。
- 頻繁な予算判定は§5の生countで行う。arch_tick_budget_*は流用しない。x86のstatic budget_tscはIRQで上書きされ得る（platform/pc98/tick_budget.c:8）、PPC内部単位はTB count（platform/powermac/tick_budget.c:4,16）。Tierは巡回ローカルな時計tokenを持つ。
- 時計失敗時は追加単位を開始せずMOREを残し記録。TSC校正だけは時計未準備でも有限な起動例外として1周1回進める。一般の時間保証は準備後。
- AC-IO-1（新）はFG1の協調進行・再入無動作・欠如NOSYS、AC-IO-2（新）は設定の検証・原子的置換・LE形式・owner終了時復元を定義。
- T2の8件/120µsは既存NIC demuxの上限であり、LAN一括転送の初期値ではない。NICは残りTier予算・既存時間上限・既存件数上限の最小まで。ne2k_bh内の複数呼出でも予算を共有する。
- NIC搬送とLAN一括転送は別service ID。LANは1フレーム単位で進め、ACK/credit待ちはWAIT。spin・フレーム途中放棄は禁止。LANの予算初期値はLAN設計だけが定義する。
- LAN側net/bulk_work.cがint lan_bulk_step(void *transfer, struct io_slice *s)を所有し、IO_DONE/MORE/WAIT/負値を返す。Tier側は型と実行器だけを所有する。
- linkのRTO/jiffyは変えない。linkディスパッチの前景移行・rshell/gshell通知の先行選択はLAN側の責任（lan-bulk/PACK.r1.mdとR2_REQ.mdの分担）。
- V3 KAPIはsdk/kapi.json末尾追加・版上げ・生成。V4は同じLE列をROM/CDへ同梱し同じ検証器へ渡す。V3で新bootloaderは作らない。

## 4. 部品の口と状態の所有者
```c
/* 内部型。初版は期限登録・期限救済・出口能力を持たない。 */
struct io_slice {
    u32 start_count, limit_count, max_units, units;
    u32 clock_source, clock_generation;
};
struct io_desc {
    u32 id, default_budget_us, budget_min_us, budget_max_us, max_unit_us;
    u8 default_tier; u8 reserved[3];
    int (*step)(void *state, struct io_slice *slice);
    void *state;
};
enum { IO_DONE = 0, IO_MORE = 1, IO_WAIT = 2 }; /* 負値=エラー */
int io_register(const struct io_desc *d); /* 常駐provider、起動FG0/FG1 */
int io_unregister(u32 id); /* FG1、実行中BUSY、成功後callback参照なし */
void io_request(u32 id); /* IRQ/FG0/FG1: 印だけ。呼出そのものはしない */
int io_clock_us(u32 *lo, u32 *hi);
int io_slice_begin(struct io_slice *s, u32 budget_us, u32 max_units);
int io_slice_take(struct io_slice *s); /* 単位開始前: 生count・件数を確認 */
int io_dispatch(u32 budget_us); /* FG1のみ。入れ子は0で戻る */
int lan_bulk_step(void *transfer, struct io_slice *s); /* LAN側所有 */
```
- io_slice_beginはio_work.h（A1）から利用者へ公開する内部口で、生count換算・開始時刻/token取得・件数初期化を行い、成功0、失敗は負値を返す。LANはdispatcher外の直接stepでもこの口でsliceを作り、失敗時はstepを呼ばない。LANはio_sliceのメンバを直接読み書きせず、begin/takeとstep引数だけで扱う。
- dispatcherが登録順・pending bitmask・cursor・実行中・世代・統計を所有。providerが機器/転送進捗、io_policyが設定とowner保存表、既存時計providerが時計を所有。アプリ関数ポインタは登録しない。
- 既知service ID・既定Tier/予算・min/maxの正はio_policy.cの定数表。io_descの値は登録時にこの表と照合する。未搭載IDも表にあれば設定を保持できる。予算はmin以上max以下、max_unit_usは機器の分割単位でありユーザーから変更不可。
- io_requestは短い排他でbitをOR。実行前にbitを取り出し、callback中の新要求は別pendingとして残す。DONEでも新要求を消さず、MOREは再要求、WAITは次の機器/credit通知まで休眠。再試行timerは既存時計からio_requestする。負値は要求を終了しエラー記録、新要求は保持。
- io_dispatchはrunningなら時計も読まず0で戻る。net_tx末尾・LINK_IDLEからの入れ子も同じ。外側の残予算・visited集合を再初期化せず、callback復帰後に外側が処理を続ける。
- providerのstep自体の再入はproviderがbusy印で防ぐ。入れ子は状態・sliceを進めずIO_MOREで戻り、busyを取得した呼出だけ全終了経路で解除する。LAN側がlan_bulk_stepのbusyを所有し、host_vfs_waitの直接呼出→kbd待ち→idle_work→dispatcherの再入も防ぐ。
- max_unit_usは1単位の測定上限。予算終了前に始めた最後の1単位だけ超過し得る。強制中断・無限callback停止は保証しない。
```c
/* 擬似コード: 空/再入を先に除外。1巡で同じIDを再実行しない。 */
if (running) return 0;
if (compat_count == 0 && !any_pending()) return 0;
/* 互換fnは常にpending扱い。visitedで1巡1回に限る。 */
pass = begin_raw_budget(budget_us); /* 今回のcountへ換算はここだけ */
while (pass_has_time(&pass) && unvisited_ready()) {
    id = pick_fair_else_tier(); /* bitmask + 先頭ビット/cursor */
    slice = bounded_slice_from_counts(id, &pass);
    result = call_once(id, &slice);
    merge_new_requests_and_result(id, result);
}
/* LAN callback: max_unitsと時間初期値はLAN設計が供給 */
while (frame_ready(transfer) && io_slice_take(s))
    advance_one_frame(transfer);
return waiting_for_credit(transfer) ? IO_WAIT :
       has_work(transfer) ? IO_MORE : IO_DONE;
```
- アプリが最初にset/resetする時、owner IDと世代に適用前の有効表を保存。同ownerの再設定では保存元を上書きしない。正常終了・fault・killの共通資源回収で適用前へ戻す。親子は既存実行順の逆順で復元し、ID再利用で旧表を復活させない（docs/09_exec.md:20-21）。
- 恒久表はユーザーの起動時設定。アプリresetは実行中の表をOS標準へ戻す操作で、owner終了時の復元を解除しない。子の表は子終了時に親の表へ、親終了時にその適用前へ戻る。
```c
/* wire: alignment=1、全多byte値LE。native structをcastしない。 */
struct io_policy_header_wire { /* 16 bytes */
    u8 magic[4];          /* 0..3 = "IOT1" */
    u8 version_le[2];     /* 4..5 = 1 */
    u8 entry_size_le[2];  /* 6..7 = 16 */
    u8 count_le[4];       /* 8..11 <= 16 */
    u8 flags_le[4];       /* 12..15 = 0 */
};
struct io_policy_entry_wire { /* 16 bytes */
    u8 service_id_le[4];  /* 0..3: 永続ID、登録indexではない */
    u8 tier;             /* 4: 1..3 */
    u8 reserved[3];       /* 5..7 = 0 */
    u8 budget_us_le[4];   /* 8..11 */
    u8 reserved2[4];      /* 12..15 = 0 */
};
/* 公開型/エラー定数はkapi.jsonで確定、C89互換で生成 */
int io_policy_set(const void *bytes, unsigned int len);
int io_policy_get(void *bytes, unsigned int cap, unsigned int *needed);
int io_policy_reset(void);
int io_pump(unsigned int budget_us); /* callback数、負=失敗 */
```
- 部分表を受理し未記載IDはOS標準で補完（直前表へのpatchではない）。未知ID・重複・版/長さ不一致・予約非0・Tier範囲外・min/max外は全体拒否。検証後、dispatcher非実行時に短い排他でactive交換。実行中BUSY、部分反映なし。
- 不正時は直前の有効表を保持。初回は標準。明示resetで標準へ戻す（U2決定）。拒否したsetでownerの保存表も変更しない。
- set/get/resetはFG1限定。getは必要長を返し、容量不足時は部分コピーしない。PPCはPR=1由来の専用境界でこれらを実行し、一般syscallのEE方針は変えない。
- pumpは安全な協調点で眠らず戻り、要求予算を1巡上限に丸める。budget=0はINVAL。PPCは§2の共通口を使い、返却値も保存フレームへ反映する。未提供像はNOSYS。

## 5. 順位・最低機会・費用（R8）
- 固定16枠、Tier別の登録順indexとpending bitmask。通常はTier1→2→3、それぞれ最初の1ビット。1巡のvisited maskで同一ID再実行を防ぐ。heap・期限走査・毎回sortなし。
- 互換枠が0本かつpending=0なら時計読出0回・count換算0回で即return。互換枠のfnは常にpendingと数え、io_requestなしでも1巡1回呼ぶ。pendingの公開/取得は短いirq_saveで保護し、新要求を失わない。時計未準備の校正要求もpendingに含める。
- Tier2/3にreadyがあれば完了FG巡回を数え、8巡以内に各Tierの1単位分の公平枠を先行予約する。両方満期で予算が一方分しかなければcursorで交互、未実行側の権利は保持。各Tier内の公平枠もcursor順で飢餓を防ぐ。
- 公平枠の条件は協調点と必要な時間予算があること。1単位も賄えない予算では進行保証しない。十分な巡回予算なら8巡以内、片側ずつしか進めない場合は次巡へ持ち越す。無yield時に壁時計保証は無い。
- 選択は最大3つのmaskと先頭ビット/cursor。16枠のbit検査は固定上限、全体O(N)。1巡最大16 callback。サービスの通常順は登録順、公平枠だけcursor順。
- dispatcher内の予算は巡回入口で1回、OS巡回上限・当巡のreadyサービス予算を生countへ整数換算し、sliceはその値と残countの小さい方を使う。LANがdispatcher外で直接stepを呼ぶ場合はio_slice_beginで1回換算する。callback/フレーム境界で換算し直さない。
- 時計providerはsource/generation・単調raw count・µs→count係数を提供。x86はTSCまたはPITの周期込みcount、PPCはTB count。低位差の有効範囲は半周未満。源変更は今回のtoken失敗として次巡で再取得し、異なる単位を差し引かない。
- 係数は時計準備時に算出し、予算換算は部分積とshiftで上限を超えない側に丸める。ホットパスに64bit除算を入れない。PITの周期込みsnapshotは既存tick/PIT読出と整合させ、µsへ変換してからcountへ戻す道は作らない。
- 時計読出は非空巡回の入口1回、各callback直前/直後各1回、単位開始判定1回。callback数K・単位判定数Mなら最大1+2K+M回（通常の成功時）。終了時刻を次の残予算計算に再利用。io_clock_usの明示呼出や既存NIC計測は別に数える。
- 初期調整値[推]: FG1巡上限500µs、公平枠8巡。LAN予算初期値はLAN設計だけで定義。固定CPU配分率は設けない。
- 上限より長い単位は分割するか保証外の互換枠と明示。小予算で装置処理を途中停止させない。

## 6. 移行・容量
|現登録者／新サービス|既定|移行|
|---|---|---|
|ne2k_bh（ne2000_control.inc:96）|Tier2・FG|まず互換adapter、次にPIO/復旧/回収の単位と残予算|
|net_rx_idle（net/nic.c:58）|Tier2・FG|NICと予算共有。IRQ救済は別経路のまま|
|pc98_tsc_calibrate_work（platform/pc98/tsc_clock.c:241）|Tier3・FG|時計未準備の有限例外。通常は既存timerから要求|
|LAN一括転送|Tier2・FG|Tier前は既存4枠の残り1枠で1周1回、LAN設計の自己予算。Tier後はLAN所有の同じstepを直接登録|
|PCM|現IRQ経路|現状維持。案Aの利用者が決まった場合だけ別票|
|入力/GUI、先読み/キャッシュ|将来Tier2、Tier3|現idle登録済みとは扱わず、小単位化後に追加|

- idle_work_register/runの名前と最大4本の互換枠を保持。互換fnは常にpending扱いで1巡1回、io_request不要。予算非対応と明示し、A5後〜D3/D4前も回収を止めない。空の速い道は互換枠0本時だけ。既存登録者移行後に通常の単位契約を有効化。旧tick登録順は不変。
- [推・未ビルド] 共通text 3〜5KiB、FG pump/時計adapter 0.5〜1.5KiB、RAM 3〜4KiB（16枠・表2面・owner別保存表・統計）、lz4増分2〜4KiB、設定最大272B。追加stackはmap/guest watermarkで確認。
- MINIMALはkernel内増分のみ、診断はDEBUGへ。FD空き下限64KiB（docs/08_build.md:768）、同:767の443/655KiBは過去の測定で現余裕の証明ではない。
- resident上限MEM_KERNEL_IMAGE_MAX（include/memmap.h:230）、圧縮像上限520,192B（TICK_1KHZ_NEXT_DESIGN §4-3）を再測定。kdebugへリンクしないので予定増分0。kdebug上限image/mem各256KiB・load_span512KiB（docs/08_build.md:195）を維持。実装時に両FD・MINIMAL・mapで合否判定。

## 7. ファイル・段・担当境界
各行が1段=1ファイル=1実装者。同一ファイルは直列。B1〜B4・旧C3・D6・出口試験は削除。
|段|新/変更ファイル|目安行|依存・並列|
|---|---|---:|---|
|A0|変更 docs/decisions/ARCH_COMPAT_CONTRACT.md|45|FG・時計・表を先に確定|
|A1|新 kernel/io_work.h|90|A0、io_slice_beginを利用者向けの口に追加|
|A3|新 kernel/io_work.c|260|A1、bit選択・予算・再入禁止・io_clock_us薄い共通口|
|A4|新 kernel/io_policy.c|240|A1、A3と並列可。定数表・LE検証・owner保存復元|
|A5|変更 kernel/idle_work.c|60|A3、互換adapter|
|C1|変更 sdk/kapi.json|90|A1/A4、関数・公開型・版|
|C2|新 kernel/io_kapi.c|80|C1、公開口の境界|
|C4|新 arch/x86/io_pump.c|30|C2、FG口|
|P1|新 arch/ppc/io_pump.c|65|C2、PR=1 syscall内の一時EE許可の共通口。io_pumpとLAN待ちJが共用。C4と並列可|
|P2|変更 arch/ppc/kernel_trap.c|20|P1、PR=1 syscall正常復帰の自動pumpを共通口へ結線|
|T1|変更 platform/pc98/clock_source.c|65|A1、生count/token/係数。PPC側と並列可|
|T2|変更 platform/powermac/clock.c|45|A1、TB count/token/係数|
|L1|変更 exec/exec.c|15|A4、owner共通資源回収へ復元口を結線|
|D1|変更 net/rx_work.c|50|A3、残時間合成|
|D2|変更 net/nic.c|80|D1、FG登録移行|
|D3|変更 drivers/net/ne2000/ne2000.c|100|D2、bhの分割・予算|
|D4|変更 drivers/net/ne2000/ne2000_control.inc|15|D3、登録切替|
|D5|変更 platform/pc98/tsc_clock.c|25|A5、起動例外adapter|
|E1|新 tools/tests/io_tier_host.c|350|A1、偽時計/要求/provider|
|E2|新 tools/tests/test_io_tier.py|200|E1、CHECK inputs/covers・変異|
- T1/T2はR8のcount境界、L1はR5の終了時復元に必要な結線。実装前に既存の共通回収位置を限定確認する。ビルド登録・PPC選択集合・生成KAPI結線も各ファイル単位で追従する。生成物は手編集しない。
- net/bulk_work.cとlink前景移行はLAN側の段。io-tier側にD6を作らない。順: A0/A1→共通/時計→host→FG移行→KAPI/PPC pumpとowner回収→guest。

## 8. 受入・変異・計測
|受入|host／guest|実行時REDにする変異|
|---|---|---|
|Tier・登録順・16枠・公平cursor|共通模型。両低Tier満期と小予算の持越しも確認|Tier順反転、公平cursor削除、持越し消去|
|空時高速経路・count予算|互換枠0本の空時は時計0回、互換枠ありは未要求でも1巡1回。begin成功/失敗、非空時読出回数、TSC/PIT/TB換算・wrap・源切替|互換枠を無視した即return、begin換算誤り、空時時計読出、token世代検査削除|
|1巡1回・新要求非消失・MORE/WAIT・再入|LAN step→net_tx→dispatcher、LINK_IDLE、直接step→kbd→dispatcher、解除BUSY。外側予算/進捗不変|runningガード削除、provider busyガード削除、pending全消去、同巡再実行|
|予算・時計失敗・校正|NIC既存8件/120µsと残時間の最小、LAN側設定値、1単位超過|開始時刻リセット、件数上限突破、時計失敗で継続|
|FG文脈・PPC pump|NP21/WとPPC QEMU。PR=1 syscallのpumpとLAN待ちJが同一口を使用、PR=0/IRQでは無動作、保存フレーム/EE復帰、処理中DEC後も正常復帰|PR/vector判定削除、復帰EE誤り、自動/明示pump二重実行|
|LE表・動的更新・資源回収|両arch同一bytes、min/max、不正時active不変、reset、親子・fault・kill・owner再利用で復元|検証前commit、native endian、終了復元削除、保存元上書き|
|T3・tick・NIC救済維持|PCM再生/stop/close、無yield e82557、物理/論理比、校正完了|PCM救済消去、jiffy gate除去|
|転送中の音・入力・速さ|下の同時負荷試験。LAN側と結果表を共有|フレーム境界の予算検査除去|
- 触った検査だけ--mutate。anchorは唯一でruntime REDを確認。既存変異の参照行を検索・移設。CHECK inputs/coversを宣言し最後の変更後にlint。
- 実装段は対象kernel/ユーザーランド/両archをbuild。ABI段はclean→all。着地CIはcheck+check-fast、全変異はfull-check。check-changedは使わない。
- Ra266で44.1kHz PCMを供給しながら4MiB LAN転送・入力・表示負荷を同条件30回。NP21/W NE2K 10Mbpsと実機100Mbpsは別集計。G4はTB/FG pumpをQEMU後に実機確認。
- 音はunderrun/resync・IRQ0/IRQ10補充数・最大補充遅延。入力は同一時計の受信→応答/表示完了のp95/max（取出時刻も補助記録）。転送はbytes/経過µs。CPU占有はIRQ本体とFGを重複計上しない診断区間で測る。
- **遅延の唯一の合格線（ユーザー決定）: 4MiB転送中に入力→応答/表示p95≤50ms、max≤100ms。LAN一括転送と同じ基準・負荷・測定点を使う。**
- r1のpump間隔1ms時のp99増分2ms・最大増分5msは参考記録だけで合否に使わない。音/速度の暫定合否[推]は10分で新規underrun 0、速度はTier前同実装の90%以上。線速70%（100Mbpsで8.75MB/s）はLAN側目標。
- syscall 1回あたりのdispatchの最大時間（自動/明示を含め実際の呼出分を合算）・選択処理の最大時間・単位超過・時計失敗・callback数・時計読出数も報告。音データを供給しないアプリの枯渇は別分類。Tier2/3だけでなくTier1も無yieldでは進まない。

## 9. 決定済み・未決
- U1: 初版は案B、TierはFGだけ。案AはTier1利用者決定後の別票。
- U2: 不正表は全体拒否し直前の有効表を保持。初回は標準、標準復帰は明示reset。
- R5: アプリ表は終了で適用前へ復元、恒久はユーザー起動表。遅延合格線は§8のp95/maxだけ。
- ユーザー判断が必要な未決: 0件。500µs・公平8巡・容量は実測で調整し、機器保証を崩す設定は受理しない。

## 反論
- 反論なし（0件）。

## 確定 (2026-11-14、Opus 第 2 回、PM 決定を反映)
- N1: idle_work互換fnは常時pending・1巡1回。空時即returnは互換枠0本の場合だけ。
- N2: A1の公開口をio_slice_beginに確定。生count換算は内部に隠し、LANは型の中身に触れない。
- N3: stepの再入はproviderのbusyで防止し、入れ子はIO_MOREで戻る。
- N4: P1のppc_syscall_fg_runだけがPR=1 syscall内のEE一時許可を担当し、io_pumpとLAN待ちJで共用する。
- N5: syscall 1回あたりのdispatch最大時間を計測する。
- LAN-N3: link_tickはFGへ移し、FGが10 jiffy回していなければIRQ0で1回回す救済をLAN側が残す。
