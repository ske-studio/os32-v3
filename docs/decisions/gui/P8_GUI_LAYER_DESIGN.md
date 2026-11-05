# P8 GUI層 詳細設計 r3

> 出所: ~/os32-tmp/run/p8-design/DESIGN.final.md (astra 起草 r1〜r3、Opus レビュー 3 回、PM が確定)
> 状態: **現行 (2026-11-05)** — 段の状態は票 TASK_P8_GUI_LAYER が持つ。

## r2→r3 の変更
- S1: microUI 廃止を反映し、接続案・関連ファイル・試験を削除、WM マウスキーを維持し、未決を0件にした。
- S2: PM 決定に従い通常文脈の synth_pump・owner/非公開 token・pcm_source_push に統一し、D・epoch の private snapshot の担当を明記した。

## r1→r2 の変更
- R1: EXCLUSIVE の鍵盤は既存 inject/trygetchar のまま、binding は Pointer/Button のみとした。
- R2: INPUT_RESET・epoch・入力停止を撤回し、append 失敗の add_dropped と既存 OVERFLOW に統一した。
- R3: 新 KAPI を start/finish/abort/状態照会の4本に削減し、handle・世代・混用禁止を削除した。
- R4: write の src と status の out に既存 pcm_write と同じユーザー範囲検査を明記した。
- R5: 合成器 r1 §5 と生成文脈・呼ぶ側・owner の1行契約を合わせた。
- R6: FM 効果音サービス・キャッシュ・その試験を P8 から外し、将来の接続口だけ残した。
- R7: Pointer 畳込みは現行どおりとし、ring.rs の変更を削除した。
- R8: OPEN で write=0 の場合の start と status の repeats/resyncs を追加した。
- R9: 各受入行に変異の正常対照と終了値を追加し、5本目の slot 試験を外した。
- R10: R2 採用により RESET の詰め直し・tail 巻戻し案を削除した。
- PM: microUI は廃止（V3_PLAN.md:41）。配送方式のユーザー判断は不要。

モデルID: 実行環境から確認不可（BRIEFの担当指定は gpt-6-astra）。
以下のパスは `/home/hight/os32-v3/` 相対。既存動作と提案を区別する。

## 1. 実物の確認
| # | 票の前提と実コードの差 | 反映 |
|---|---|---|
| D1 | P8のPCMリングは未起票だが、`drivers/sound/cs4231/pcm_cs4231.h:120` 以降にDMA・ステージング各4096 frameが存在。`pcm_cs4231.c:515,594` は単一ownerのopen/write | リングを重複新設しない |
| D2 | `pcm_cs4231.c:624` は4096 frameで自動開始、`:703` はcloseで短音を開始。writeだけでは短い操作音が始まらない | 明示startと破棄停止abortを追加し、既存closeのdrainを維持 |
| D3 | `V3_PLAN.md:41` でmicroUIは廃止。`gshell/src/kbdnav.rs:860` 以降のWMマウスキーは同周期にdown/upを発生 | WM宛ての同周期クリックを保持する。microUI接続は実装しない |
| D4 | `docs/09_exec.md:102` の「OP_POLLはリングを読む」は概略。`userland/rust/libos32gui/src/client.rs:331` はOP_POLL後にclientがSHMをコピーしheadを進める | consumerはclient、WMはtailの所有者と明記 |
| D5 | `gshell/src/ring.rs:1` は満杯をcallerが扱う契約だが、`input.rs:1085,1110,1122,1132` のPointer/Button/Focusはappend失敗を処理しない | 欠落通知・状態再同期を入力拡張より先に実装 |
根拠は `V3_PLAN.md:41,61-71` と `TASK_T7_AND_FOLLOWUPS.md:232`。MEMMAP_V3 D19の8bppを維持する。
`TASK_KBD_NAV.md` 冒頭と§4-1にはK2完了記述の差もある。P8では既存K2を推定合格せず再実施する（実コード差の数には含めない）。
`09_exec.md:104` の全PD共有という説明を新しいleaseの隔離保証には使わない。指定範囲から現行PTEの可視性は未確認。

## 2. 境界と順序
シングルタスク・協調切替、整数演算を維持。IRQはPCM補充を許し、GUI/FEP/I/O待ちを呼ばない。
順序はA: PCM内部口 → B: stream口、C: event欠落修正。BとCは並列可能。
本票は同時ミキサ・リサンプラを追加しない。PCM装置は既存どおり1ストリームに排他的に貸す。
合成器と一般PCMの競合はBUSY。FM効果音サービスとキャッシュは別票とし、本票は接続口だけを残す。音の出所・ソース名による許可リストは作らない。
合成器は生成済みPCMを提出するproducer。fmgen本体・OPNA状態・生成工具の中身は並行票に任せる。
内部CはGNU11、公開ヘッダはC89/GNU89。カーネルはkstringと既存DMAのpa/vaを使う。GPL/LGPLをカーネル・SDKへ入れない。

## 3. 音のリング
### 3.1 公開口と所有
```c
/* 既存 pcm_open(rate) / pcm_write(src, bytes) / pcm_close() は維持。 */
/* 以下4本を KAPI 末尾へ追加。unsigned int は32bit。 */
int pcm_start(void);
int pcm_finish(void); /* 非同期drain開始 */
int pcm_abort(void);  /* 未再生分を破棄して停止 */
int pcm_stream_status(struct PcmStreamStatus *out);
```
statusはu32×10の40B: `size,state,produced,consumed,queued_frames,underruns,overruns,repeats,resyncs,flags`。
既存 pcm_status(free_bytes,counters) の ABI は維持する。新照会の出力長は固定40Bで、sizeも出力値。
openは既存の呼出元資源ownerを記録し、各操作は既存 pcm_owner_ok を使う。装置は1 ownerに排他貸与する。
handle・世代・別の予約表は追加しない。既存writeの自動開始とcloseの同期drainは維持し、新旧操作を混用できる。
startは既存の閾値未満の短音を明示開始する口。abortはpcm_reclaimの停止・回収処理を共用する。
DMA・IRQ参照のownerは既存module、利用権ownerはアプリ/gshell。module参照は停止証明まで保持する。
DMAリングとステージングはカーネル所有。アプリVAを保存せずcopyするので、新しい共有DMA leaseを設けない。
既存DMA予約の寿命を継承し、DMA物理番地は公開しない。任意のユーザーcallbackをIRQから呼ぶ口も設けない。

### 3.2 producer/consumer cursor
形式はs16le stereo、1 frame=4B、22050/44100Hzのみ。rate変更・変換はproducer側。
既存4096 frameステージング16KiBとDMA16KiBを再利用する。PCM配列の追加は0B。
P=公開済みframe総数、C=DMAへ転送済みframe総数。u32 wrappingで `queued=(u32)(P-C)` は常に0..4096。
物理位置は `P & 4095` / `C & 4095`。Cは再生済み位置ではない。DMA内の未再生分はqueuedに含めない。
D（DMAが実際に消費したframe）・epoch の private snapshot（position_validを含む）は合成器票が足す（`pcm_source.inc`）。
既存 `pcm_stg.w/r/staged` と同じreserve/publish/consumeでP/Cを更新し、独立した容量判定を増やさない。
```c
reserve_under_irq_lock();              /* owner/状態/空きの検査 */
copy_unpublished_frames_with_irq_on(); /* 末尾なら2分割、最大16KiB */
publish_under_irq_lock();              /* owner・状態を再検査してPを進める */
/* consumer: 既存の位置観測とrefill marginが許す非再生半面だけ */
consume_to_dma_half();                 /* コピー後にCを進め、残りは0埋め */
```
copy中にIRQは未公開領域を読まない。producerはpublishまでpark/WMポンプを呼ばない。
IRQがcopy中にFAULTへ進めたらpublishせずIO。IRQはfreeせず、通常文脈の回収まで領域を保持する。
statusは短いirq禁止区間で全項目をsnapshot。公開点にcompiler barrierを置き、volatileだけへ依存しない。
writeの形式・端数・0Bの扱いは既存 pcm_write の契約を維持する。producerは4B単位で提出し、大きな要求は空きまで部分受付する。
満杯は0B。部分受付も含め未受付があればoverrunsを1回飽和加算。未受付部分の再送はproducerが行う。
古い音の上書きはしない。OPENでwriteが0Bを返したらpcm_startを呼ぶ。RUNNINGで0Bなら既存sys_yield/GUI WAITで譲る。IRQ側に待ち・動的確保を追加しない。

### 3.3 start・underrun・close/fault
openはOPENで返す。既存writeの閾値到達時の自動開始を維持し、startは1 frame以上で既存起動列を実行し残部を0にする。
空startはAGAIN、RUNNINGへの再startは0。短音を4096 frameまで待たせない。
補充不足は0埋めし、補充1回の不足につきunderrunsを飽和加算。Cは実データ分だけ進む。
既存の位置喪失/refill余裕不足は既存RS_STOP/RS_RESTARTへ。古い半面の反復を正常として隠さない。
producer停止で無音でもownerの利用権は有効。再writeで補充復帰。協調切替をしないproducerに無停止再生は保証しない。
状態はCLOSED→OPEN→RUNNING→STOPPING→CLOSED。finishはOPEN/RUNNING→DRAINING→DONE、装置故障はFAULTED。既存sticky faultを維持。
finishは短音も開始し、以後writeはINVAL。DONEは停止証明・DMA解放済み。完了snapshotは終了時ownerに限って照会でき、次のopenまたはowner終了で消す。装置は次のopenに貸与できる。
abortは未再生分を破棄して停止。irq禁止区間で受付停止→既存abort列→停止証明→解放の順。
`pcm_cs4231.c:746` のpcm_reclaimを内部停止関数へ抽出しs_claimed/証明/隔離を共有。別のI/O停止列を作らない。
停止証明成功で0、失敗でIO。失敗時DMAは既存fault隔離規則に従い再貸与しない。無期限待ちは追加しない。
通常終了・fault・STOPはT2の通常文脈回収で同じ停止を一度だけ通す。IRQからfreeしない。
gshell終了はgshellの音だけを回収。他ownerの音を奪わない。合成器のowner終了はsourceをdetachし、以後のpushを拒否する。
最後まで鳴らす単発音はfinishで既存drain処理を使う。abortを「再生完了待ち」として使わない。既存closeは同期drainとして残す。

### 3.4 誤り・合成器・効果音との境目
OS32_ERR体系に合わせる: INVAL=形式/状態/他owner、BUSY=使用中、NOSYS=装置無し。
NOMEM=確保失敗、AGAIN=空start/一時状態、IO=装置/停止失敗。既存PCM_ERR別名を再利用する。
writeは受付byte数か負値で失敗時P不変。未open・終了後の操作は既存の状態/owner検査に従う。
KAPI境界でwriteのsrcは既存 pcm_write と同じくowner検査→NULL拒否→番地+bytesのwrap拒否→ring3_user_range_ok(a, bytes)の順で検査し、検査成功前にcopyしない。
新statusのoutもowner検査→NULL拒否→番地+40のwrap拒否→ring3_user_range_ok(a, 40)の順で全出力範囲を検査する。失敗はINVALで出力先を変更せず、カーネル番地・範囲跨ぎ・wrapを拒否する。sdk/kapi.jsonを正典とし生成wrapperにも反映する。
**合成器との1行契約（REVIEW.r2.md S2 の PM 決定）:** 通常文脈の synth_pump が、attach で持った owner と非公開の token で内部の pcm_source_push を呼び、IRQ は生成せず work_pending を立てて起こすだけ（既存PCM補充は維持）。
内部source口は合成器票の責務であり上記4本の公開KAPIに含めない。世代付きhandle・STALEは導入せず、tokenをユーザーへ渡さず、pushからpcm_advanceへ再帰しない。owner/tokenの検査、短いIRQ禁止区間と停止時hook解除はこのPM決定に合わせる合成器票の責務とする。
将来の効果音向けには既存open/writeとstart/finish/status/abortの接続口だけを提供する。sfxサービス、FM経路、キャッシュ生成/読込、起動音の実装はP8外。
既存pcm_closeのdrain開始と完了照会を内部口 `drain_begin()/drain_poll()` に分離し、旧closeはそれを同期的に包む。
この内部口をprivate module API経由で接続し、公開pcm_finish/pcm_stream_statusから使う。finishは開始成功で0、重複開始も0。
statusと既存tick/補充経路でdrainを進め、IRQでは完了印だけを更新し、停止証明・解放は通常文脈で行う。完了までbusy loopせずsys_yield/GUI WAITで譲る。
finish後はwrite不可。完了は停止証明後、fault/取消はabortと同じ停止経路を使う。

## 4. 入力eventの所有・容量・drop
既存4slot×16KiB、ring128×16B、u16 head/tailを維持。WMがtail、当該clientだけがheadを進める。
WM private状態でowner/世代/配送先を管理する。別ownerのpoll/bindを拒否。新しい共有可視性を仮定しない。
Pointerの連続末尾への畳込みは既存ring.rsの条件を維持する。Button/Key/Text/Focusは順序を保持し、ring.rsは変更しない。
新 `input_queue.rs` の `emit_input(target,event)->bool` から既存appendを呼び、Pointer/Button/Focusを含む各入力append失敗を既存add_droppedへ1回だけ通す。既に数えているKey/Textは二重加算しない。Configure/Paintは既存pending再送。
X4は空き不足ならrawを読まない。X3はSTOPを探す既存処理を維持し、捨てたrawを数える。
pending_rawは既存32件。X4は読取前に空きを確認し、1件退避した後続も退避。無限キューは追加しない。
入力欠落時、WMは対象slotのcaptureを即座に捨てる。downのappend成功後だけcaptureを設定する。
clientは既存T3のOVERFLOW/on_overflowで押下キー・ボタン・ドラッグ・リピート・captureを全解除する。取消でクリックを発火させない。
OVERFLOWを伴うpoll batchとadapterに残る旧入力batchは入力として適用せず、解放後に再びdownが残らないようにする。非入力eventは既存順序で処理する。
Focus欠落時も入力状態を未知として取消し、WMの論理focusを変更し直さない。以後の配送先はWMの現行focusに従う。
INPUT_RESET・epoch・reset_pending・消費確認待ち・入力停止は追加しない。headはclient、tailはWMが前進させる既存T3だけを使い、詰め直しやtail巻戻しを行わない。
libos32guiはOVERFLOWの回復規約を実装し、callbackによるWM再入を行わない。

## 5. focus・WM再入
### 5.1 共通配送
既存CTRL+STOP/FEP/モーダル/WMショートカットの優先順位を維持。未確定文字はfocus変更前の旧窓へ確定。
X1のfocus変更は既存pending commitに旧窓を保持、X3で確定してから新窓のキーを配送。終了済み旧ownerの文字を新ownerへ転送しない。
X4は既存リング追記/カーソルのみ。FEP/WM状態機械/マウスキーをX3へ退避し、再入で実行しない。
Button down受理後だけcaptureを保持。upは元owner/窓/世代へ。owner終了でcaptureを破棄する。
モーダル中の新downはモーダルだけへ、開く前のcaptureのupは元の相手へ返す（`input.rs:753` の既存規則）。
実/合成buttonsは別に保持、ORの0→1/1→0だけを配送。同ボタンを両方で押しても二重downを作らない。
移動は最後に動いた側を採用。止まった実マウスで合成位置を巻戻さない。合成クリックも状態更新毎にエッジ判定する。

## 6. ファイルごとの変更・大きさ・並列性
概算行数。既存巨大ファイルへは接続だけ。共有構造体/ヘッダは1機能1ファイル規則の共有束の例外。
| ファイル（+は新規） | 機能・規模 | 並列条件 |
|---|---|---|
| +`drivers/sound/cs4231/pcm_stream.c` | 既存ownerに接続するAPI/finish/status、200–300行 | PCM口固定後 |
| `drivers/sound/cs4231/pcm_cs4231.c` | start/stop/drain内部口抽出、差分80–120行 | stream担当と直列 |
| `drivers/sound/cs4231/pcm_cs4231_math.c` | cursor・短音開始、差分40–80行 | PCM担当 |
| `drivers/sound/cs4231/pcm_cs4231.h` | 共通定数/内部宣言、差分30行 | PCM担当 |
| `drivers/sound/cs4231/pcm_module.h` | private表末尾追加・版更新、差分20行 | PCM口固定後 |
| `drivers/sound/cs4231/pcm_module.c` | facade接続、差分40行 | 同上 |
| `drivers/sound/cs4231/pcm_body.c` | export接続、差分15行 | 同上 |
| +`userland/gshell/src/input_queue.rs` | append失敗計上/capture取消、80–120行 | PCMと並列可 |
| `userland/gshell/src/input.rs` | 統一emit/論理ボタン、差分80–120行 | queue後 |
| `userland/gshell/src/wm.rs` | 共有状態・回収接続、差分30行 | 共有束の例外 |
| `userland/rust/libos32gui/src/app.rs` と既存widget入力状態 | OVERFLOW時の入力取消、差分40–80行 | 既存on_overflowを使用 |
| +`userland/gshell/host/input_queue_tests.rs` | drop/focus/WMマウスキー試験、300–450行 | 入力契約後 |
| +`tools/tests/pcm_stream_host.c` | PCM口・割込み点試験、350–450行 | PCM契約後 |
| +`tools/tests/test_pcm_stream.py` | host実行/変異登録、150–250行 | host fixture後 |
`kbdnav.rs` は既存のWMマウスキー配送と全画面除外を維持し、変更対象から外す。`handler.rs` も既存OP_POLLのdropped/OVERFLOW通知・消込を再利用するため変更不要。
公開宣言、`sdk/kapi.json`、生成C/Rust binding、syscall wrapper、execのowner回収、build登録も追従する。
その正確な登録先は指定読取範囲外なので実装開始時に正典から特定する。生成物を手編集しない。
KAPIは末尾追加・版上げ。入力欠落対策ではGUI protocolを版上げしない。RESETのための版変更は行わない。PCM moduleは対の版一致を要求。
host登録にはCHECK inputs/coversと一意の変異anchorを付け、既存gshell integrationの変異登録も追従する。
本体概算0.7–1.0千行＋試験（登録・生成物を除く）、各新規ファイル500行以内。状態増分はPCM数十B、WM数百B。PCM配列増分0。
実装時は対応する `docs/design/<ソースパス>.md` へ機能別に分割。本作業では指定の単一成果物にまとめる。

## 7. 受入 → 試験と変異
以下は実装後の計画。現時点で実行済みの項目はない。host/NP21/W/実機は互いの代替にしない。
| ID | 受入・試験 | 変異で落とすもの | 正常対照・終了値 |
|---|---|---|---|
| A1 host | P/C wrap、空/満杯/部分write、末尾2分割、既存端数処理、長大要求。write/statusのNULL・wrap・カーネル番地・範囲跨ぎ | copy前publish、容量±1、byte/frame混同、ユーザー範囲検査削除 | 無変異で順序/guard bytes一致・合法バッファ成功・不正先不変、exit 0。各変異は対応assertでexit 1 |
| A2 host | 1 frame start、空start、OPENでwrite=0→start、underrunゼロ、repeats/resyncs、write中IRQ/publish前FAULT | 短音開始削除、ゼロ埋め削除、状態再検査削除 | 無変異で短音進行・無音補充・snapshot一致、exit 0。変異はexit 1 |
| A3 host | abort/finish/旧close/正常終了/STOP/fault、新旧混用、停止失敗時DMA保持、他owner拒否、module unload拒否 | s_claimed遅延、証明無しfree、owner比較削除 | 無変異で正当owner操作と停止成功・停止失敗時隔離、exit 0。変異はexit 1 |
| A4 host | 128件/u16 wrap、Pointer/Button/Focus失敗→dropped/OVERFLOW、up欠落時の押下/capture解除、旧batch再生無し、他slot進行 | append失敗無視、OVERFLOW後旧batch配送、head早進め | 無変異で欠落0の対照と各欠落1の回復・他slot進行、exit 0。変異はexit 1 |
| A5 host | X4の32raw境界、GRPH+TABと文字順序、旧窓FEP確定、modal/capture/終了 | X4でWM実行、focus後に宛先取得、古いcapture再利用 | 無変異で非再入配送・旧窓確定順・正常capture解除、exit 0。変異はexit 1 |
| A6 host | WM宛ての5/-/+、0→カナ解除、make20回=237dot/CTRL=118dot | up省略、二重repeat、カナ解除時のup欠落 | 無変異で左/右クリック・ダブルクリック・保持解除・移動量一致、exit 0。各変異は対応assertでexit 1 |
| A7 NP21/W | 8bppでedit_gui/filer、移動/クリック/focus/STOP。ログと画面を取得 | X4保留解除忘れを故障注入 | 無注入で一連操作完了をguest fixtureがexit 0。注入は保留不解消を検出しexit 1、timeoutはhost runnerがexit 1 |
| A8 NP21/W | PCM短音/連続音/producer遅延/終了再open、既存ownerとのBUSY競合、start/finish/abort/status | BUSYで音奪取、abort後再開、finish完了喪失 | 無注入で競合拒否・短音完了・停止後再openをguest fixtureがexit 0。注入時exit 1、timeoutはhost runnerがexit 1 |
| A9 実機 | バスマウス無しでA7、KBD_NAV K3・カナmake/break。PCM E5/E6相当の音/停止/underrunを記録 | host停止失敗注入も併記し実機成功で代替しない | 無注入の実機guest fixtureは機械判定成功exit 0、失敗exit 1（音/画面は別途ユーザー記録）。host正常停止対照exit 0、証明無しfree変異exit 1 |

補充最悪時間が既存512 frame余裕内であること、close/abort完了後DMA/IRQが再開しないことを測る。
ABI変更のclean/all・関連host/変異・混在拒否は実装担当/PMの既存手順で実施。本作業ではビルドしない。
NHD配備はNP21/W停止と既存承認範囲を守り、実機物理操作はユーザー側で実施・記録する。

## 8. 未決（ユーザー判断が必要なもの）
未決 0 件。

## 反論
なし（0件）。

## 確定 (2026-11-05、Opus の r3 の確認で Approve、PM が反映 — この節が本文より優先)
- P1 (P3、合成器との順序): drivers/sound/cs4231/pcm_cs4231.c、pcm_module.h/.c、pcm_body.c は、P8 の段 A (pcm_reclaim の内部停止関数への抽出、drain_begin/drain_poll、private 表の末尾追加) を先に取り込む。合成器の段 E はその後に末尾へ足し、pcm_module の版は段ごとに上げる。合成器の source の finish/abort も、P8 で抜き出した同じ内部停止・drain の経路を使い、別の停止列を作らない。
- 未決 0 件 (microUI はユーザー決定で廃止)。
- ユーザー決定 (2026-11-05): 「microUIは廃止です。すでにos32guiがあるので」。§8 の未決は 0 件のまま。
