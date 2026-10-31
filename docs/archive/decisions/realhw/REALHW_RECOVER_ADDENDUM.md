# recover 確定設計への追記 r4（r2への差し込み、2026-10-21）

> 状態: **現行 (2026-10-21)** — 戻す処理の確定設計への追記 (plan の契約 r4 と、T1b・T3・4a S1〜S5 で設計者が決めたこと・送り項目)。本文の一部。段の現在地は票 TASK_REALHW_RECOVER が持つ。
> 出所: `~/os32-tmp/run/realhw_recover/ADDENDUM.final.md` の確定版を 2026-10-21 に写した。往復の版 r1〜rN は ~/os32-tmp に残る。中身は変えず、リポジトリ内の文書への参照だけリンクに直した。

r3→r4 の変更 A：leaveはnext[0]/next[1]の辺だけを確認し、opcode条件の再評価を削除。
r3→r4 の変更 B：abort・失敗でcursorを無効化し、cleanup・①②をplan外のphase ruleで実行。
r3→r4 の変更 C：共有access、BAR測定値受渡し、静的EXPR登録表の型を宣言。
r3→r4 の変更 D：各件数の上限と4aが渡す静的検査領域・成立bitの置き場を明記。
r3→r4 の変更 E：recover_plan_successを削除し、nodeごとの成立bitに統一。
r3→r4 の変更 F：ctxの構造化表を削除し、依存照合に使う不透明IDだけに簡素化。
r3→r4 の変更 G：hashを既存crc_wordの連鎖に確定。
r3→r4 の変更 H：ops.raw_configをcommandとbar[3]へ拡張。
r3→r4 の変更 I：PM推奨どおり、A1でGPU書込み0件なら①省略だけではFAILEDにしない。
r3→r4 の変更 J：固定列実データのS1追補への割当てとT1b合成式のhost限定を明記。
## §2 追加：plan の入力と寿命
- planは4a S2のdecodeが作る、main別の命令列・分岐辺・内部write列だけの静的表。許可表はr2のrecover profileだけであり、planは許可を増やさない。PREPARE・SIZE_BARS・正常終端はrecover側の固定列を同じ形式で添える。固定列の実データはS1追補でrecover GF profileへ収める。任意グラフ言語・callback式・ユーザーポインタ入力は導入しない。
```c
struct recover_plan_arg { u32 kind, value; }; /* IMM=value, READ=target ID, SAVED=snapshot slot */
/* ctxは呼出し・反復を区別する不透明なID。構造化ctx表は持たない。 */
struct recover_plan_node {
    u32 phase, main_id, pc, opcode, substep, ctx, head, skip;
    u32 rule_id, expr_id, arg_first, arg_count, next[2];
};
struct recover_plan_dep { u32 consumer, required_rule, producer; }; /* node IDs */
struct recover_plan_entry { u32 phase, main_id, first; };
struct recover_plan {
    u32 version, profile_crc, rom_crc, rom_size;
    const struct recover_plan_node *nodes; u32 node_count;
    const struct recover_plan_arg *args; u32 arg_count;
    u32 ctx_count; /* node.ctxは0以上ctx_count未満、依存照合にも使うためhash対象 */
    const struct recover_plan_dep *deps; u32 dep_count;
    const struct recover_plan_entry *entries; u32 entry_count;
};
struct recover_plan_buffers { u32 *flow_bits, *valid_bits; }; /* 4aが実node数×64 u32とvalid 64 u32を確保 */
int recover_bind_plan(struct recover *r, const struct recover_profile *p,
                      const struct recover_plan *plan, struct recover_plan_buffers *buf, u32 *plan_hash);
int recover_plan_enter(struct recover *r, u32 node);
int recover_plan_leave(struct recover *r, u32 edge); /* 0/1 */
```
- NONEはr2同様0xffffffff。添字は各配列の0始まり、参照しない欄は0またはNONEと型ごとに固定する。入口はphase/mainごと、RUNは0→4の正常終了順のみ。非scriptはmain/pc=NONE。nodeのrule_id=NONEは制御・読取等の非W、Wは必ず一つのruleに対応し内部substepも省略しない。 件数上限は次表とし、超過はbindで拒否する。
| nodes | args | ctx | deps | entries |
|---|---|---|---|---|
| 2048 | 8192 | 2048 | 64 | 32 |
- 4aはplanと一緒にflow_bits（2048×64 u32 = 512 KiB、nodeごとの必ず成功済みproducer集合）とvalid_bits（64 u32、nodeごとの成立bit）を静的バッファでbindへ渡す。bindで初期化し、flow_bitsは静的検査中、valid_bitsはsession終了まで保持する。集合内producerのctx/ruleから通常requiresを判定する。
- `recover_bind_plan`をtrial_begin直前に呼ぶ。検査成功時のみrecoverが参照と計算したhashを保持し、既存trial_beginのplan_hashとprofileを照合してsessionへ束縛する。hash単独の新規armは禁止。S0等も小さな固定planをbindする。trial_begin/trial_step/tokenの既存引数・承認契約は維持する。
- 表とROMは部品所有のkernelメモリでbindからcleanup完了まで不変・生存。recoverはコピー不要、二重bind不可、開始失敗/終端で参照を解放する。全count・範囲・checked加算/乗算・確保失敗を書込み前に検査する。hashは全表の正規化u32値・参照先列・profile CRC・ROM識別から算出し、ポインタ/paddingを含めない。 hashは既存crc_wordの連鎖で計算する。
## §2 追加：分岐・skip・呼出し・反復と全経路検査
- decodeはSUB/CALLを呼出箇所ごと、REPEATを既知count回だけ静的展開する。呼出し/反復の入口と出口も元opcodeのnodeとして残す。ctxは親ctxと呼出箇所またはREPEAT箇所/0始まり反復番号で一意に採番し、復帰は親ctxへ戻る。phase/mainの根ctxも別ID。再帰・count=0・不整合終端・深さ超過は4a §4どおり拒否する。
- nodeは展開後の命令/substepと到達時のhead・skipを表す。同じ位置に異なるhead/skipで到達する場合だけnodeを分ける。`next[0/1]`は通常同一、条件は偽/真の後続、終了はNONE。呼出し先と復帰先、反復の次回/終了先は展開列の辺となり、後退辺は不要。入口間の正常遷移も検査対象とする。
- skip中の通常W/読取/TIMEは非実行の制御node（rule_id=NONE）として残す。NOT/RESUME・条件のskip保持・SUB/CALLの非呼出し・force・共有head/条件状態は4a §4の27種だけで遷移を検査する。IO_RESTRICTの選択肢と範囲外無書込み、PLL等の内部分岐も辺へ展開し、未知/未解決の制御・番地・headは拒否する。
- 静的検査は各入口から到達可能な両辺を走査し、合流で「必ず成功済みの(ctx,rule)」集合を交差する。Wのrequiresを入力集合で検査し、成功と必要readback後だけ当該ruleを加える。skip/無書込み辺は加えない。失敗はplan走査を終了してcleanupへ移り、以後のtrialへ合流しない。全substepにも同じ規則を適用する。
- call/反復の新ctxに親/前回の成立集合を暗黙継承しない。ctxを戻したときは親自身の記録だけを使う。動的命令数・深さ・待ちの既存上限を経路ごとに検査し、静的node総数を実行stepsに数えない。展開表を確保できなければ開始拒否とし、上限や予算を拡大しない。
- abort・失敗の時点でplanのcursorを無効にする。local_cleanup・①②はplanの辺を通らず、RESTORE/QUIESCE phaseのrule（SAVED・override）と既存の前提検査だけで動く。未成立のtrial依存を成功と仮定せず、独立に成立する②の経路とr2の予約/FAILED契約を維持する。
## §2 追加：世代跨ぎ依存とruntime照合
- 通常requiresは同session・同phase・同ctxの成功を指す。世代を跨ぐ場合だけdepsに`{consumer node, requiresのrule ID, producer node}`を一件追加する。producerは当該ruleの成功点そのもの、consumerの全到達経路に先行する必要がある。別phase/main/呼出し/反復の参照はこの宣言以外不可。存在しないID・重複・循環・requiresに無い追加依存を拒否する。
- 例：PREPAREの停止確認rule Rのnode PをRUNのnode Cが使うなら`{C,R,P}`。Pの書込みと必要readback完了を全経路で証明する。単に「PREPARE済み」や同じruleの前反復成功では代用しない。複数候補から最新成功を拾う形式は設けない。
```c
int recover_access(struct recover *r, u32 node, u32 dir_substep, u32 target, u32 width, u32 *value);
/* dir_substep は READ/WRITE/SELECT/DATA。node≠NONE なら node の opcode/substep と一致を要求、node=NONE (cleanup 中) は dir_substep と RESTORE/QUIESCE phase の rule だけで照合する。 */
/* 共有read/write口。方向はnodeのopcode/substepで決め、readは*valueへ出力、writeは*valueを入力。sessionはplan参照・現在node・valid_bitsだけを保持する。 */
```
- 4aはopcode/substep前にenter、終了時にleaveを呼び、recoverが現在入口/辺・ctxを管理する。条件readと選択値は共有accessorから得て、leaveでは選択されたnext[0]/next[1]の辺であることだけを確認する（両辺は静的全経路検査済み）。実アクセスは現在nodeのphase/main/pc/substep/head・rule・引数と完全一致させる。非実行nodeではW不可（RESET等のforceはr2の許可付きW node）、辺の飛越し/再実行は拒否する。 cleanup中はcursor無効のためaccessのnode=NONEとし、前記RESTORE/QUIESCEのrule・既存前提検査を適用する。
- nodeの成立bitはrecover accessorが書込み成功＋必要readback完了時だけ作る。通常依存は同ctxの先行記録、明示依存は同sessionの指定producerの成立bitとplan上のphase/ctx/node/rule全一致を要求する。session終了で全破棄。PAUSEDは保持するが承認・入口順を飛ばせない。失敗/skipは記録しない。
## §2 追加：EXPR 引数の契約
- 制御nodeもargsに復号オペランドを持ち、opcode/substepごとに引数の順・型を固定する。多肢選択は選択indexとの一致判定を二辺の固定列に展開し、全選択肢と範囲外を残す。条件判定のREADとindexed SELECTにもr2の安全性/許可検査を適用する。
- EXPRのW nodeのexpr_idはrule.value.expr_idと一致し、arg_first/countはargsの連続範囲。expr IDごとにrecover側で引数順・個数・kind・値域・整数評価手順を固定する。IMMはdecode済み即値/表選択値/PLL limits、READは同substepのread_safe target、SAVEDは同sessionのvalid slotだけ。任意アドレス・任意計算結果を引数として受理しない。 登録はカーネル内の静的表とし、型は次のとおり。
```c
struct recover_expr { u32 id, argc; const u32 *kinds; int (*eval)(const u32 *args, u32 argc, u32 *value); }; /* kindsはargc個、検査後の整数値だけをevalへ渡す */
/* 登録表は recover_profile に `const struct recover_expr *exprs; u32 expr_count;` を足して渡し、CRC の対象にする。host 試験は合成式を持つ試験用 profile を渡す (本番の登録表には入れない)。 */
```
- bindでは引数の型/参照/範囲と全選択肢を検査する。runtimeでは同substepで共有accessorが採取した読値とsnapshotを使い同じ登録式をchecked整数演算で評価し、4aの要求値と比較した後r2のmask/SET/RANGE/write_maskも検査する。PLLは登録したsolver/手順に入力とlimitsを渡し、係数の自己申告で済ませない。未実装expr IDはDENY、未分類PLLをT1bで創作しない。
## §2 追加：検証済み BAR 長
```c
struct recover_bar_extent { u32 raw, base, length, valid; };
struct recover_bar_measurement {
    struct recover_identity identity;
    u32 measurement_session;
    struct recover_bar_extent bar[3];
};
/* recover所有：A1検証結果cacheと、各sessionにコピーしたbar[3]。snapshotとは別。 */
```
- 取得元は承認済みA1のSIZE_BARSだけ。recoverが測定mask・元BARを受け、4a §3のmemory32/非零/2冪/整列/溢れ/他装置・bridge窓照合を検証し、BAR復元とCommand overrideおよびA1終端成功後にcacheをvalidにする。途中結果は同A1 sessionだけの候補とし、失敗時に破棄する。ROMや固定想定サイズで補わない。 受渡し口は`int recover_bar_probe(struct recover *r, u32 bar, u32 raw, u32 mask);`とし、barは0〜2、rawは元BAR、maskは測定値。
- A1は独立trialなのでcacheをrecover所有者の同一起動・同装置の測定記録として保持する。後続trial_beginでidentityと生BAR0〜2を再照合してsessionへコピーする。再測定開始・起動/装置/BAR不一致・測定失敗で無効化。実アクセス直前の生Command/BAR確認も維持する。 ops.raw_configはcommandとbar[3]（BAR0〜2）を返す形へ広げる。
- profile_okは表の形式検査と、測定値を受ける範囲検査の二段とする。INSPECT/S2は形式と全経路を先に調べ、未測定は「範囲検査待ち」であり検証完了ではない。通常のBAR利用planのarm前にはsnapshot・guard・cleanupを含む全到達targetについて範囲検査を完了させる。
- BAR相対実効offset（VGA別名/head strideを含む）をchecked演算で求め、`width <= length && offset <= length-width`、整列、base加算を検査する。VRAM範囲は全長で検査し予約範囲条件も併用する。BAR長はVRAM実容量の証明ではない。
- 未測定A1の固定planはPCI_CFGのprobe/復元と独立②だけで開始できる。測定自身だけは段階検証とし、A1内のBAR利用は候補長の検証・BAR復元・Mem有効の確認後のみ、必要targetの範囲検査を済ませる。未成立ならMMIO quiesce（①）を省いて安全な②へ進む。測定前abortを含めA1でGPUへの書込みが0件なら①の省略だけではFAILEDにしない（②等の失敗は既存どおり）。書込み済みで①が必要なのに実行できない場合は既存契約どおりFAILEDとする。全GF profileの未使用BAR行を理由にPCIだけの測定を循環依存にしない。
## §3 追加：分担と合格条件
| T1b で作るもの | S2 以降に回すもの |
|---|---|
| 上記共有型、bind/enter/leave、正規化hash、静的全経路/依存/runtime照合、BAR記録と範囲検査の受渡し。合成plan・fake BARで未bind/hash不一致/分岐片側の依存欠落/skip/別call・前反復・別session流用/明示依存/未測定・境界外を検証 | 4a S2で実ROM decodeから表を生成し、27種・SUB/CALL・REPEAT・head/skipを実fixtureと照合。A1実測の採取/保存接続は既存T2・4a担当で行う |
| r2のlocal_cleanup接続・index選択中abort試験・未分類DENYを維持。登録式の引数検査/評価入口とT1bの合成整数式で契約を検証（合成式はhost試験専用、本番の登録表には入れない）。MUTATE=0、変異実行は既存PMレビュー後 | 固定列の実データはS1追補でrecover GF profileへ。S1追補のGF rule根拠を受けS2〜S5で実式/PLL/内部substep・共有accessorを接続し正常/拒否traceを確認。個別行の許可、A1/A2/RUN承認、T5測定は既存割当てのまま |


## 確定 (2026-10-21、Opus 第 2 回 (r4) の条件付き承認、PM が反映)
- C1: recover_access に dir_substep (READ/WRITE/SELECT/DATA) を足した。node≠NONE なら一致を要求、cleanup 中 (node=NONE) は dir_substep と RESTORE/QUIESCE の rule で照合。
- C2: 式の登録表は recover_profile の exprs/expr_count で渡し CRC の対象。host の合成式は試験用 profile。
- D1: 上限を nodes 2048・args 8192・ctx 2048 に下げ、flow_bits は 2048×64 u32 = 512 KiB、valid_bits は 64 u32 (目標機 32MB で 8 MiB の静的バッファは不可)。上限を超える plan は bind で拒否。**4a S2 は実 ROM の decode で展開後の node 数を数えて報告する** (2048 を超えたら、集合を W node と合流点だけに持つ形への変更を設計者に戻す)。
- D2: node の添字は位相順 — next[i] は NONE か自分より大きい添字。bind で検査し、循環の拒否も兼ねる。全経路検査は 1 回の前向きの走査。
- C3: `int (*raw_config)(void *ctx, u32 *command, u32 bar[3])`。T1 の試験の fake の追従は T1b の作業に含める。

## T1b の欠けの補完 (2026-10-21)
- 他装置・bridge 窓照合の採取担当は4a (A1)、判定担当はrecover。4aはPCI走査でGPU自身を除く装置の有効なmemory BAR範囲と、GPUの上位経路外のbridgeの有効なmemory窓を排他範囲表へ、上位経路の各bridgeのGPU BAR種別に対応する転送窓を包含範囲表へ収める。上位窓は重複禁止範囲には入れない。
```c
#define RECOVER_RANGE_MAX 256
struct recover_range { u32 base, len; };
int recover_bar_probe(struct recover *r, u32 bar, u32 raw, u32 mask,
    const struct recover_range *exclude, u32 exclude_count,
    const struct recover_range *upstream, u32 upstream_count);
```
- 上記を79行の受渡し口の確定シグネチャとする。両表は4a所有の各256要素の固定長配列で、countは使用要素数（0〜256）。bar/raw/maskは既定どおり。upstreamは当該bar用とし、直結ならcount=0。count=0のポインタはNULL可。
- 4aはA1のprobe開始前に完全な走査結果を用意し、全probe終了まで表とPCI割当てを不変に保つ。窓欠落・走査/長さ取得失敗・32bit範囲で表現不能・件数超過は省略せずA1失敗とし、候補/cacheを無効化する。単一タスク前提で再走査・ロックは不要。
- 範囲は半開区間[base, base+len)。recoverはlen非零・端点の溢れを検査し、候補BARがexcludeのどれとも重ならず、upstreamの全窓に包含されることを検査する。端点計算はu64で行い、2^32を超えれば拒否する。
- 戻り値0が当該BARの測定・照合成功、負値が失敗。recoverは成功時のみ候補へ記録し、失敗時は候補/cacheを無効化、4aは既存の失敗/復元経路へ進む。表は呼出し中だけ参照し保持しない。cacheのvalid化は既定のBAR復元・Command override・A1終端成功後だけ。
- PM決定：27 種の実 opcode の引数配置とアクセス方向の表は 4a S2 (decode) の成果物で、S2 の依頼で設計者が表を書く。T1b は合成の opcode (試験専用、2〜3 種: 読み・書き・SELECT+DATA・条件分岐) で引数配置の表の形と検査の入口を作り、host 試験で契約を検査する。固定 PREPARE/SIZE_BARS 列の実データは S1 追補
```c
struct recover_opcode_layout {
    u32 opcode, substep, argc;
    const u32 *kinds; /* argc個の順序付きkind列: 既存IMM/READ/SAVED */
    u32 dir_substep; /* 既存READ/WRITE/SELECT/DATA、アクセスなしはNONE */
};
/* recover_profileへ追加し、内容をprofile CRCの対象とする。 */
/* const struct recover_opcode_layout *layouts; u32 layout_count; */
```
- 表はprofile所有の静的不変表で、寿命は既存profileと同じ。(opcode, substep)を一意キーとし、bindで未登録/重複キー、argc/kinds不一致を拒否、accessで現在nodeの方向と照合する。SELECTとDATA、条件readと非アクセスの制御は別substepに分ける。
- skipの非実行形はアクセスなしのsubstep、forceの実行形は許可付きWのsubstepとして登録する。既定のhead/skip/辺検査とrule検査は維持し、この表だけで許可を増やさない。T1bの合成表はhost試験用profileだけに置き、実opcode・正常終端を含む固定列の未提供データを創作しない。

## T1b の実装で設計者が決めたこと (2026-10-21)
- r5: ops.read/write は `(opaque, const struct recover_bus_access *, value)` とし、共有 target ID、dir_substep、space/bar、検査済み実効 offset、width を渡す。SELECT は index port、DATA/READ は data port、同じ target ID を維持し、callback が前回アクセスから方向を推測する契約にはしない。
- 内部 profile の版は RECOVER_PROFILE_VERSION=2。旧形式は拒否する。式の eval ポインタは CRC に含めず、式 ID・argc・kinds と profile 版／image identity で静的実装を識別する。
- layout のアクセス形は先頭 IMM を target ID とし、READ はこれだけ、非 EXPR の W は続く IMM 値または SAVED slot、EXPR は続く登録式の引数列とする。非アクセス形の全引数も layout で照合する。式の引数上限は32、layoutは先頭targetを加え33。27種の意味・skip/head遷移の復号の正しさは S2 の実表・fixture 照合で確定する。
- flags は N1、LOCAL（分類済み局所 SAVED 復元）、FORCE（skip 時の許可付き実行）に分ける。通常の skip node はアクセスなしの layout を用い、FORCE でも rule の照合を省かない。
- indexed SELECT の成立は同 phase/main/ctx の成功記録を要求し、同一 index port の別 SELECT で古い選択記録を無効化する。静的合流ではこの記録も交差し、runtime でも選択済み target と照合する。
- 局所 snapshot は recover に32件の `{key,dir_substep,value}` を持ち、安全なRW indexedの初回 SELECT/DATA 直前に採取する。LOCAL+SAVED の RESTORE rule だけが対応値を使える。元indexはdataの値と区別し、元index復元のSELECTだけはvalue=target.indexの制約を外す。COLD data は採取・SAVED再生しない。
- ops.local_cleanup は冪等な局所資源の唯一の解放所有者とし、分類済み解錠→data→lock→元indexの列を recover_access(NONE,...) で実施後、PTE/IRQ/窓を解放する。実 lock の列・資料分類は S1追補/S5、T1b は合成data/indexとfake資源でこの契約を検査する。operationは安全点/accessorの非0後に解放・アクセスをせず直ちにreturnする。
- BAR_VRAM 用の予約区間を profile.vram_reserved（BAR相対base/len、CRC対象）に置く。実capacityの証明ではなく、検証済みBAR長と予約全長の双方を検査する。未使用は0/0。
- A1は未測定BAR targetを含む表をbindできるが、snapshotで必要な範囲は開始前、plan内BARアクセスは候補長・生BAR復元・Command=0102hを確認してから段階検査する。cacheの成立は全3本の測定・復元・override・正常終端成功時だけ。GPU W 0件で候補未成立なら①省略だけではFAILEDにしない。
- INSPECTはbind済みprofileのread_safeな非indexed READのみ。BARは同装置のcacheと生Command/BARを照合し、未測定なら範囲検査待ちとして拒否する。SELECTによる読取り準備はINSPECTに含めない。
- 非scriptのmain/pcと非EXPRのexpr_idはNONE、非indexedのindexはNONE、他の未使用欄は0。正常trial_endはplan終端到達を要求し、abort/失敗はcursorを破棄してRESTORE/QUIESCEへ移る。開始失敗・終端でplan参照と成立bitを解放する。
- cfg50/54・PBUS1850/1854は部分幅/途中offsetで迂回できない32bit tupleとし、非零Wを拒否する。N1の前後は必ず4個を別々に読み、PREPARE後のRUN入口/出口と終端でも確認する。Commandは16bit、SAVED禁止、Mem再開時は生BAR復元確認を要求する。
- snapshot専用regsはSELECTを要さないtargetに限定する。indexedの安全な保存は上記局所snapshotに収め、未承認のSELECTをsnapshot/INSPECTから暗黙に発行しない。

## T1b の取り込みで S2 へ送る項目 (2026-10-21、Opus の T1b レビューの P3)
- 実行時の index 選択の記録が 1 つ (r->selected) だけで、静的検査はポート別。SEQ 選択→GFX 選択→SEQ DATA の正しい順を bind は通すが実行時は FAILED (安全側の不一致)。S2 で実データに当てて、ポート別の記録にするかを決める。
- dependencies の indexed 分岐が前の node ごとに layout_for・rule_by_id を線形に引く (O(N²·R))。S2 で展開後の node 数と bind・実行時の時間を測る。

## 4a S1 追補で設計者が決めたこと (2026-10-21)
- GF の許可表は `kdebug/recover_gf.c`、共有 target 144 件・rule 543 件（script 512 件＋固定/復元 31 件）に集約し、fixture の DENY_W は証拠側の既定拒否を維持する；host の ordinal→rule 対応は許可表ではない。
- CONSTANT/SELECT の保持式 `(old & keep) | value` は `write_mask=(~keep|value)&width_mask` と同じ value.mask/value.bits で表し、literal 以外は RANGE と旧値差分検査を併用する；S2 は旧値から要求値を計算する実式と layout を追加する。
- RUN の substep は fixture gf_effects の0始まり ordinal とし、SELECT の8選択肢も別 substep に固定する；S2 は選択値との一致と範囲外無書込みを展開し、同じ rule を反復 ctx ごとに再利用する。opcode の実引数表は今回追加しない。
- read_safe は元命令が読む RMW と VGA indexed、および既定の config/PBUS/PMC 制御に限定し、未分類の literal/trigger の verify_mask は0；PLL/PFB/strap/PRAMIN は COLD とし保存値を終端再生しない。CR44/CR1F/CR11 だけ局所 RW とする。
- 固定 SIZE_BARS/PREPARE は共有 plan_node/arg/entry の実データで持ち、固定専用 opcode 0x100〜0x109 を使う；SIZE_BARS の READ node 中に測定値を recover_bar_probe へ渡す。正常BAR再生だけは valid snapshot の identity EXPR、異常時は RESTORE/SAVED とする。
- profile/固定plan は不変 template とし、所有者が T2 の checked tick 予算と S2 layout を添え、profile CRC と plan.profile_crc を設定する。予約は BAR1 先頭1024 byte のみで、VRAM W/容量試験の未実装式を一括許可しない。
- 619 候補の分類は rule 512、PLL内部手順待ち101、COPY式待ち2、一時値再生待ち3、共有別名検査の欠け1；後4分類は rule ID=NONE のまま拒否する。これらの解消までは全ROM RUNを許可しない。
- 最小の共有検査修正が必要：recover_target_address の raw MMIO 別名禁止は実際の VGA 別名 port に限定し、通常レジスタ 602858h（DED7、PCRTC head1）を巻き込まない。現状は profile 全体を不正にしないため当該1候補を未許可に置く；recover.c/.h は本追補では変更しない。
- 最小の共有検査修正が必要：indexed READ の EXPR operand は、同じ target の先行 SELECT を全経路/実行時に確認して受理する。現行 arg_ok は全拒否するため PREPARE 最終 CR11 node は bind拒否（GPU W=0）として再現し、推測literalに置換しない。
- 一時再生3候補（RESET 184Ch、CR57 head0/1）は同一命令/ctxの採取値を参照する式が必要で、COLD snapshot再生やRESTORE phase偽装はしない；S2/S5へ採取nodeと参照検査を送る。COLD indexed の元indexだけを局所保存する区別も現在の target.access 一個では不足し、SELECT の局所保存可否を data と分離する最小拡張を S2/S5 へ送る。
- N1は main3/E81E/head0 の DATA rule に4対象の前後guardとSELECT・PREPARE停止2件の依存を置く。正常traceと各対象の前/後失敗を real recover 検査器＋host専用layoutで検証し、失敗後の後続access 0・cleanup一回を要求する。
- S1追補完了時の別名禁止は BAR0 の PRMVIO/PRMCIO 各 head の実 port byte とアクセス幅の重なりだけを対象とする（既参照 envytools f102b82 `display/nv_vga.xml` 334–358 の vio/cio 群）；602858h は既存IDを動かさず target144/rule543 として追加し、619候補は rule513・PLL101・COPY2・一時再生3・別名欠け0に更新、残る106候補は拒否を維持する。
- indexed EXPR READ にも既存の同 target・phase/main/ctx の SELECT 成立bitを全経路で要求し、同port再選択による失効と合流交差を適用する；実読取り直前は r->selected と照合し、単一選択記録のポート間制限と計算量の検討は既定どおりS2へ送る。
- T2の予算設定関数 recover_gf_profile と衝突するS1の不変許可表は recover_gf_template に改名する；host所有者もT2の必須 restore_max_polls を設定し、固定PREPARE最終CR11のbind/実行を受入にする。

## T3 の実装で設計者が決めたこと (2026-10-21)
- abort は10 byte（A5 5A、sessionのLE32、先頭6 byteのIEEE CRC32のLE32）、1安全点64 byteまで；不正候補は1 byteずつずらして再同期し、session不一致・通常行・不正CRCは実行せず破棄累計へ入れる。
- 部分フレームは同一kdebug_call中だけ保持し、開始・PAUSED・終端で破棄；各境界では既存4096 byte RXリング分までを有界に排出し、上限到達はFAILED。hostはabortを一度だけ送り、EOTまで次コマンドを送らない（遅延tailを次セッションへ再利用しない）。
- kdebug内のboot寿命singletonをrecover_owner()で共有し、試験所有者は同一個体へprofile/opsを接続する；差替えはIDLEまたは非coldのVERIFIEDだけ、sessionカウンタ・既存終端は保持し、FAILED/coldを初期化で解除しない。
- 未試験のnow 0は新sessionでS0のみを実行する；既存HALがCUI・31kHz・0FACh=98を確認できる場合だけ、装置書込みなしのS0予算/profile/opsを使い、0FACh書込み承認は常にfalse。TVRAM内容を保持しcursorを同期、S1はunknownのPARTIAL（装置保存・実測済みとの主張なし）。
- shell共有opは3=status・4=now、kdebug_callの既存slotを使いC89の専用共有ヘッダに40 byte要求/結果を置く；PAUSEDはnowのみ。snapshot/armの別コマンドは設けずtrial_beginから自動実行する。
- RECはrecoverがsession内seqを1から採番し、状態行にversion/profile_crc/plan_hash/snapshot_crc、各accessのBEGIN/END、最終行にtrial/lost_lines/discarded_bytesを加える；DISCARDは終端直前に累計1行へ集約し、T2の固定Bは拡大せず最大桁数の静的計算へ追加算入する。
- REC送信中は受信による再入cleanupを遅延し1行終了後の安全点で処理、期限超過は送信を打切って呼出元がcleanupする；trialのBEGIN送信失敗では対象accessを行わず、restoreは残る独立S0予約を使う。局所資源の解放をUARTより先に行う既定順を維持し、その内部accessだけを記録する。
- UARTは既存serial設定を参照してTxRDY確認・1byte出力・T2のPITフレーム待ちを使う；SerialFS gate中は開始を拒否。trial送信pollは65536、restoreは既定の独立poll/byte枠、tick停止・送信詰まりでも有限とする。
- hostはexclusive serialとflockで単一所有しraw受信とsession/seq/CRC/最後のBEGINを保存、初回verの起動image CRC/buildを照合；PAUSEDの継続は保存済み起動照合を使い通常コマンドを送らず、以後のsession/seq/CRC不一致を拒否する。host timeoutはabort後に終端が届いても成功へ変更しない。
- hostは終端RECと対応command echo/EOTを揃えて判定し、欠落・lost・FAIL/PARTIAL・shell拒否は非成功；CPU/バス停止や物理再起動は自動操作しない。実ROM・4a実行・実機上限測定は従来の後段に残す。

## T3 の取り込みで送る項目 (2026-10-21、Opus の T3 レビュー。試行の op を結線する段 — 4a の S6 か T6 — の前に直す)
- P2 kdebug/recover.c:730 (logged_write、logged_read も同じ形): BEGIN 行の送信に失敗したら access を行わない、という決定を検査する試験・変異が無い (T3 の詰まり試験は UART を最初から詰まらせるので状態行で先に落ちる)。→ 状態行の後で UART を詰まらせる fake を足し、DATA0 への書込み 0 件を確かめ、`if (log && r->state != REC_RESTORING) return -1;` を変異に加える。
- P3 recover.c:1596・1546・1607: access の中の call_point が abort・期限切れで cleanup を済ませて VERIFIED にした後、呼出し元が fail(REC_BUS) や access_error を重ね、`recover status` の reason が ABORT でなくなる (REC の最終行は正しい)。→ logged_* が -1 を返したとき r->state が VERIFIED/FAILED なら fail を重ねずに -1。
- P3 recover.c:1054・931 (`if (!r->emitting)`): 送信中の期限切れで cleanup を遅らせる処理に試験が無い。→ 行の途中で期限を切らせ REC の行が混ざらないことを試験。
- P3 recover_profile.c SCREEN_CALL: 書いた後の読み直しと最後の照合の読みがどちらも step=4 → 最後の照合を step=7 に。
- P3 tools/rshell_recover.py: exclusive=True と明示の flock の重複 → flock を消してよい。
- kdebug_main.c recovery_call の PAUSED の門と op 1/2 (peek) の経路はホスト試験が無い → T4 (NP21/W) で peek の回帰と PAUSED 中の status 拒否を見る (台帳 RECOVER-T4)。

## 4a S2 の実装で設計者が決めたこと (2026-10-21)
- 内部 profile 版を3とし、plan の capture 表（slot/producer/consumer/target/read ordinal）と unresolved_count を hash 対象に追加する；unresolved_count 非0の inventory は bind を拒否し、PLL 入口の NONE を実装済み扱いしない。
- COPY 2式は GF の登録EXPR、採取3件の identity は既存の登録identity式を共用する；本番の COPY 2行・一時再生3行は S5 の結線まで rule NONE の DENY を維持し、合成profileだけで共有captureのbind/実行/失効を検査する。
- COLD data と独立した index_save_targets は検査済み index port の代表target IDの配列（CRC対象）とし、同portのSELECT保存を1件にまとめる；GFはCRTC/SEQの各headを分類し、data保存・終端再生の権限は増やさない。実cleanup接続はS5に残す。
- node→layout/rule の固定長cacheとtarget→実効index port IDをrecoverがbind時に所有し、依存走査内では線形表探索を行わない；既存512KiB flow_bitsは拡大しない。
- decodeは9本のscript CRC照合後にROM全体CRCをplanへ置き、S1の命令位置とeffect ordinalだけを本番側の範囲表に持つ；ROMバイト列・x86コードは持たない。ホストの支持表は合成データで閉じる。
- CALL/REPEATは先にctx別の命令列へ展開し、各位置のhead/skip状態を合流させる；最長経路steps/待ちは後ろから計算し、分岐全体のnode数を実行予算へ足さない。
- CR44のtargetはhead0で解決するがnode.headは命令入口の制御headを保持するため、共有node検証のtarget/head一致条件からhead0 CR44だけを除外する。
- 規模計数はPLL入口3件を含み内部101候補を含まないinventoryとし、未結線5行もnodeとして残す；S4の内部列追加時に再計数する。計測は同じnode数と上限2048のindexed合成列、同じ表規模と最大表規模を使用し、fake read/write区間とログを除く（qemuと測時計装の費用は含む）。
- skip中の53/54のK nodeはROM検証済みの次node.headへ論理headを進める（HWアクセス0）；RUN main入口はhead0へ戻す。通常nodeのhead不一致拒否は維持する。
- LOCAL/SAVEDのSELECTだけは明示分類済みindex portならCOLD targetにも認め、同portの保存indexを参照可能とする；DATAのCOLD再生は禁止のまま。SUBの表入口は既知pcだけでなくscript IDとの対応も照合する。

## 4a S2 の取り込みで S3 へ送る項目 (2026-10-21、Opus の S2 レビュー、すべて P3)
- recover.c:1213-1219 skip_head_change が汎用コアに GF の 0x53・0x54・0x10000U・0x20000U を直書き ([C4]・層)。別 profile が同じ opcode 番号を使うと skip 中の head 変更を許す。負の試験・変異も無い → layout に「skip 中に head を変える」フラグを持たせ recover_gf.c から渡す。「0x52 の K で head が変わる plan は bind 拒否」の試験と変異を足す。
- gfinit_decode.c:275・310・331 ROM 内の表の位置 0x5daa・0x5d86・0x5d66・0x5d4c・0x5bc8 の直書き → GF_INIT_* の名前付き定数 (これらは script 9 本の CRC の範囲外だが、値は rule で縛られ SUB の表は script ID と照合するので安全上の穴ではない)。
- gfinit_decode.c:467-473 skip 中に head を進めるのは 53/54 だけ。nouveau は 52/62/78 の CR44 書込みでも変える (記憶、推測)。実 ROM では到達しない → skip 中に 53/54 以外が CR44 を書く K は decode で拒否。
- 片付けの気づき: recover_access 末尾の bit_set の重複 (内側を消す)、index_save_safe の常に真の比較 (消す)、fixed_layouts の空行、`STATIC_ASSERT(GF_INIT_SCHEDULE >= RECOVER_NODE_MAX)` を足す、gfinit_host.c の 940 行の node 一覧は引数付きか失敗時だけ出す。

## 4a S3 の実装で設計者が決めたこと (2026-10-21)
- layout に REC_LAYOUT_SKIP_HEAD を追加し profile CRC に含め、内部版を4へ進める；GF の53/54のKだけが付与し、共有コアはopcode番号を解釈しない。
- 実行器はS2が展開したforward planをmain入口から次main直前まで進め、call/repeatの入口・復帰Cで深さを再検査する；4096枠は通ったnodeとEXPR内部READに課金し、静的な分岐総数を課金しない。
- recover_plan_accessは既存recover_accessと同じ検査経路で登録式を一度評価して要求値に用いる；SAVED採取・read順・許可表・mask/guard検査の所有者はrecoverのまま、実行器から生busを呼ばない。
- CR44の実行形RMW（SUB8の78）はruleのvalue.maskが幅全体を覆いbitsが既定head値のときだけbindでheadを確定する；可変結果は拒否し、skip中の53/54以外のCR44書込みもdecode拒否を維持する。
- S3の時計入口は校正済みの実行累積µs（実測UART送信時間を除く）を成功/失敗付きで受け、待ちは最小時間保証・有限pollを持つcallbackへ1000µs以下に分割する；S5/S6の実時計結線前はfakeだけで検証する。
- PLL入口は未解決のまま実行拒否し、S4は許可済み内部nodeへ展開する；hostだけでPLL要求kHzをsynthetic literal writeへ置換し、COPY/採取5行もhost専用ruleで閉じる。本番の8未解決行は解除しない。

## 4a S3 の取り込みで送る項目 (2026-10-21、Opus の S3 レビュー、すべて P3)
- (S4 で) recover.c:1680-1690 DATA の old の bus_read と evaluate が port_selected・UNCLASSIFIED・node_rule の検査より前に移った。今の decoder は DATA の直前に必ず自分の SELECT を出すので RUN では起きないが、restore で profile の順を誤ると別 index の data port を read_safe の確認なしに 1 回読む → 値に依らない検査 (UNCLASSIFIED、node_rule、DATA の port_selected) を読みより前に戻し、順の変異を 1 件足す。
- (S4 で) gfinit_decode.c:670-685 と gfinit_exec.c:92-96 予算の数え方が静的 (node だけ) と実行時 (式の中の READ も 1) で違う。止まる側なので安全 → decode 側も READ を数えるか、票に「静的は下限」と書く。
- (S6 で) main 1 本を trial_step 1 回で実行するので、本番 profile の opcode_limit は 60 秒以上、max_polls はおよそ 2×4096 + 2×5000 以上が要る (recover_gf_template は未設定、試験は 65536)。60 秒の打ち切りの理由が REC_TIMEOUT でなく REC_OPERATION になる → S6 の結線で値を固定し、境界の試験を付ける。
- (S4 で) test_gfinit_exec.py:20-35 変異が突いていない検査: call の復帰時の pc/ctx 照合、repeat の終わりの ctx と count、main 末尾の `calls||repeats`、unresolved_count の門 (fail_kind 6 の試験はあるが変異が無い) → それぞれ 1 行の変異を足す。

## 4a S4 の実装で設計者が決めたこと (2026-10-21)
- PLL0x21は4件・entry長31 byte以上をchecked読取し、指定reg（低位はreg−4）を一意に検索する；ref=0は27000kHz、非零は27000だけ、N/M=1..255、max_p/bias_p=0..7とし、探索のmax_p_usable=6・段別丸め・0.5%補正・fixedgain例外はMIT pllnv04.cのNV4x仕様を維持する。
- NV44Aの内部列は4004hが6書込み、4024hが15書込み、680508hが5書込み；S1の101候補は76件を26本の順序依存つきruleに対応付け、4004h側のMPLL専用3レジスタ×8候補と高位1584hの計25件はチップ分岐で不要として解消し、未解決PLLは0件とする。
- 同値なら省く参照実装の最適化は採らず、承認済みの同じ有限列を毎回実行する；680580hも同値書込みを含める。係数と補助レジスタはreadbackし、lock検出とは呼ばない。元値の読取りは内部列の先頭へまとめ、S5の実accessorや中断後のCOLD復元を追加しない。
- capture上限は3から32（valid bitmapの幅内）にし、PLLの一時値は既存3件に続く23件の単一consumer slotで扱う；4038hの設定元は4020hなのでconsumer宛先とsource targetの同一条件だけ外し、同pc/ctx/head・先行read・全経路の到達・一回消費・許可ruleを維持する。
- static最長経路のstepsはnodeとEXPR内READを数え、実行器と同じ4096枠に統一する；PLL展開後の972 nodeに対する最長stepsはmain0..4で208/281/143/127/1、待ちは2010/13200/0/1160/0µs。COPY2件・非PLL一時再生3件のDENYとS6送りの時計/poll設定は残す。

## 4a S4 の取り込みで S5 へ送る項目 (2026-10-21、Opus の S4 レビュー、すべて P3。単段の << と * は nouveau pllnv04.c:73-76 と同じで誤りではない、除外 25 件は devinit/nv04.c の経路で正しいと確認済み)
- test_gfinit_pll.py:19-28・gfinit_pll_host.c:51-54 `(clk << GF_PLL_P_MAX) < min` の分岐に入る入力 (clk < min/64) が試験に無く、`*` を `<<` に変える変異も生き残る → min_freq > clk*64 の合成表で選ばれる P を確かめる 1 件と変異 1 件。
- gfinit_pll.c:125 原本 (pllnv04.c:93-94) は `crystal/M < minU` で探索全体を return するが、ここは m の繰り返しを break して次の p へ進むので nouveau と違う P/N/M を選ぶことがある (どちらも限界内) → `return s->actual ? 0 : -1` で原本に揃える。
- gfinit_pll.c:171-174 pll_write が範囲を確かめずに count++ (今は固定 15 本で起きない) → `if (q->count < GF_PLL_WRITES_MAX)` か列の長さの STATIC_ASSERT。
- 未確認: VBIOS の version.chip が 0x44 であること (実 ROM で、ノートの nvbios の出力で確かめる)。

## 4a S5 の実装で設計者が決めたこと (2026-10-21)
- T2窓をkdebug_windowへ移し、peekと同じboot寿命の排他・PTE復元失敗ラッチを使う；最大2 PTEを保存し、READはsupervisor/PCD・RWなし、WRITEだけRW、map失敗でも全PTEを復元してIRQを戻す。
- gfinit_busはrecoverの解決済みtupleをdescriptorと照合し、0:13.0の生ID・Mem・全BAR一致後に窓へ渡す；PCI関数表は既存pci_cfg_read32/read16/write32/write16と同じ署名とし、importsと所有者への結線はS6に残す。
- COPY2行と非PLL一時再生3行は既存EXPR/capture契約により本番ruleへ追加する；1700hのCOPY宛先はCOLDとし、実行中の一回消費と終端再生禁止を維持する。
- 局所cleanupは保存済みCR11→CR1F→CR44 DATA→元indexの順とする；RUN許可列はCR1Fの解錠を保持する。LOCAL/SAVED SELECTには保存DATAの対象index選択だけを追加許可し、COLD DATAは再生しない。成功後は局所保存件数を0にして二重再生を防ぐ。
- S4送りの単段PLL低入力境界は探索全体のreturnに揃え、clk<min/64の分岐と書込み列上限を合成入力/変異で検査する；version.chipの実ROM確認は未実施のまま残す。

## 4a S5 の取り込みで送る項目 (2026-10-21、Opus の S5 レビュー)
- **P2 (S6 で、実機の S8 = A2 より前に必ず)** kdebug/gfinit_bus.c:111-121・kdebug/recover_gf.c:295,297: CR1F は読みと書きで値の意味が違う — 読むと 0=LOCKED / 3=UNLOCKED、書くと 0x57=UNLOCK / 0x99=LOCK (envytools nv_vga.xml:253-259、nouveau hw.h:315)。今は局所 cleanup が保存した読み値 (0 か 3) をそのまま書き戻す。PREPARE の rule 0x1a は 0x57 を書いて verify_mask 0xff で比べるので、実機では 3 が読めて REC_VERIFY で止まる見込み (安全側だが A2 が進まない。host の fake は書いた値を返すので試験では見えない)。CR44 は拡張 CR なので CR1F を施錠した後に書くと効かないかもしれない (推測、xf86-video-nv nv_driver.c:2301-2315 は解錠したまま書く)。既存の recover_indexed_cleanup (recover_profile.c:145) は UNLOCK→DATA→LOCK→INDEX で、今回の CR11→CR1F→CR44 と食い違う → CR1F の復元は読み値から書き値へ写す (3→0x57、0→0x99)、PREPARE の verify は 3 と比べる、CR44 は施錠の前に戻す。fake も読みと書きで値が違う形にして試験する。
- P3 tools/tests/gfinit_bus_host.c:185: 局所 cleanup の順序を確かめる試験がなく、入れ替えの変異も生き残る → trace の順の確認 1 件と変異 1 件。
- P3 gfinit_bus.c:80 (PCI cfg の present())、:96 (legacy [A0000h,C0000h) の拒否)、:47-49 (probe の I/O・64bit BAR の拒否) に負の試験と変異が無い → それぞれ合成入力 1 件と変異 1 件。
- P3 kdebug_window.c:89-101: SELECT と DATA が別々の窓 (別々の割込み禁止区間) で、設計 §2「index/data は一組の間だけ割込み禁止」と違う。今の OS32 には GPU に触る割込み処理が無いので実害なし → 設計の文言を今の形に合わせて書く。

## 4a S6 の実装で設計者が決めたこと (2026-10-21)
- CR1Fの局所SAVEDは保存読値0/3を命令値99h/57hへ写して承認し、同じLOCAL ruleでcleanup冒頭の解錠57hを許す；全CR1F DATAのverifyは命令値を読値0/3へ写し、順序をUNLOCK→CR11→CR44→LOCK→INDEXとする。
- GFのSELECTとDATAはそれぞれ短い独立したIRQ禁止・窓区間とする；単一タスクでGPUを扱う割込みが無い前提に合わせ、4a本文§2の「一組の間」をこの形で具体化し、ログ・待ちはIRQ禁止区間に入れない。
- checked UARTの所有はRECの1行単位のTXだけとしRXは維持する；通常出力は既存holdへ保留、SerialFS送信・gate変更・速度変更を拒否し、最後のframe待ち後に既存UARTのTxEMPを有限予算で確認、全帰還で所有を解放する。
- RECの途中送信失敗後は次行前にchecked改行1byteで境界を復帰し、その送信も当該予算へ課金、静的Bには各行1byteを追加算入する；gf_preは展開node/substepごと、gf_busは必要readback後にENDを出し、verify無しのreadbackはNONE（未確認）とする。
- GFの送信枠は不変planの全node・READ引数と最大REC行長から両辺を含む上界をchecked算出し、実UARTの8N1速度でwire時間の2倍＋3秒を有限上限にする；送信中の実tickだけを60秒の実行枠から除外し、PAUSEDを双方から除外、復帰は既定の独立18/33秒枠を維持する。
- A4の読戻しは専用layout flagの読取専用CHECK_EXPR nodeへ8語ずつまとめる；recoverに登録した純整数式とread_safeな非indexed targetだけを許し、Wの1node/1rule・既存件数上限・全経路照合を変えず、式未実行のleaveを拒否する。
- A4先頭1KiBの元値は最初のpattern書込み式の許可済READで部品所有RAMへ一度だけ退避する；破棄予約なので終端で再生せず、2パターンとも全書込み後に全読戻しを行う。
- PFB10020Chの容量はMIT nouveau `subdev/fb/ramnv44.c:30` とenvytools `memory/nv10_pfb.xml:213` のMEM_AMOUNTにより上位8bitをbyte量として読む；16MiB以上の2冪だけをこの段の合理的形式とし、0・他形式は容量未検証と記録して最小試験だけを行い、実容量無しとは主張しない。
- 容量照合は実測BAR1長までの2冪境界・終端DWORD・先頭の固定候補をplanへ収め、PFBとの小さい方に入る点だけを前向き分岐で選ぶ；GPU専有の破棄予約をその候補範囲へ広げるが、W許可は固定候補だけとし、最小試験・容量確認・窓による部分確認とS0復帰の結果を分けてRECへ残す。
- A1の資源採取はGPU直結bus0の全functionを生configで読み、off-path bridgeは正確な転送窓全体で子装置を包含して排他する（TI PCI2050B SCPS076G §§4.20–4.25、FreeBSD releng/14.3 `sys/dev/pci/pcireg.h`）；有効なendpoint memory BAR/ROMの長さ不明は既定どおり書込み前に拒否し、隣接割当・baseの整列から実長を推測しない。
- private opは5=INSPECT・6=SIZE_BARS・7=PREPARE・8=RUN_ONE・9=VERIFY・10=ABORT、版1の40byte要求とし既存kdebug_callを使う；公開SDK・shellのgfinit表示はS7、importは既存258番の後ろに追加して生成し既存slotを保つ。
- hostの--statusはsession/state/image-crc不要の状態表示専用とし、echo・本文・EOTを照合する；復旧成功とは扱わずver・abort・session更新を行わない。aidebug:は既存AidebugPortを使い、serialはexclusive=Trueだけで所有する。
- S6修正: ownerのcallback・状態だけを小さい静的controlへ残し、ROM・decode・plan・flowは既存`pgalloc_alloc_phys(LEDGER_OWNER_KERNEL)`と対のfreeで試行中だけ所有する；flowはbind対象の実node数×64 u32、decodeは作業2048step/引数6144、planはtarget768/rule1280/layout1056/expr16のGF固有上限（超過は開始拒否）とし、32MB機の一時領域を1MiB以内でhost検査する。
- S6修正: SIZE_BARS成功だけは次のPREPARE用にROM/decodeを保持し、終端・中止・開始失敗では全ページを返しlast_opを無効化する；外部recover後の同session再送も成功キャッシュを返さない。
- S6修正: checked TX中の通常kprintfは既存holdに入るがchecked_endでは排出せず、次のSerialFS gateで消える仕様と明記する；RECの有限送信予算と既存rshell/SerialFSの動作を変更しない。
- S6修正: A1の長さ不明memory BAR/ROM・header・bridge窓・GPU不一致/不在は拒否理由とbus0 BDF・reg・rawを開始前にログへ出す；Ra266のTrident 0:8.0/NEC 0:7.0による拒否の実機確認はPMがT5前に行う。

## 4a S6 の取り込みで送る項目 (2026-10-22、Opus の S6 レビュー 2 回目、P3)
- 訂正 (PM 決定 2026-10-22): 以下の251頁は1790 node時の値。2048 nodeの上限は267頁 = 1,093,632Bへ訂正し、M1で固定する ([HWDEV_CORE_API §9](HWDEV_CORE_API.md#9-部品の寿命-manifest-v1-m2m3a-の入力契約))。
- owner の作業領域の上限 (最大 251 ページ = 1,028,096B) を STATIC_ASSERT で固定する。host の fake は生きている頁の合計を 1MiB で打ち切るが node_count=1790 で回していて 2048 の最悪の場合は通していない、実際の確保口の連続性 (first-fit) も模していない — 最悪の node 数で 1 件足す。
- (PM、T5 の前に) 実機 Ra266 の bus0 の Trident 0:8.0 と NEC 0:7.0 の Command とメモリ BAR を読み、A1 の他装置の窓の照合で拒否されないか確かめる (拒否されれば A1 のログに理由・BDF・reg・raw が出る)。
- kdebug.bin は 239,908B で KDEBUG の上限 262,144 (tools/module_schema.py) の 92%。S7 以降で足す前に、上限の引き上げ・装置ごとの部分を別の部品へ・許可表を小さい型に詰める (約 30KB)・kselftest を別の部品へ (54KB) のどれにするかをユーザーに諮る。

## 送り項目の行き先 (2026-10-23、PM が実装と照合)

- S6 送りの上限の STATIC_ASSERT と最悪 node の試験: HWDEV M1 (c9a4817) で 267 頁として固定済み。
- kdebug.bin の 92%: HWDEV の分離で解消 (M3c 92153ea、109,060B = 42%)。
- Trident 0:8.0・NEC 0:7.0 の BAR の確認: 実機を待たず 2026-10-15 の基準構成の生データで照合した ([RA266.md](../../../decisions/realhw/RA266.md) §1-1)。NEC は BAR が 0 で通るが、Trident 0:8.0 と **82557 0:11.0** が長さ不明のメモリ BAR で拒否に当たる。根拠の設計は票 TASK_HWDEV §3-1 の BARLEN。
- S4 送りの pll_write の上限は `kdebug/gfinit_pll.c` に入っている。S3 送りは S6 で回収 (票 TASK_REALHW_RECOVER)。S5 送りの割込み禁止の文言は [GF6200_4A_DESIGN.md](GF6200_4A_DESIGN.md) 末尾の注記で閉じた。
