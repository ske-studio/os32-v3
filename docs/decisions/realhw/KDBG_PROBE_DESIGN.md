出所: /home/hight/os32-tmp/run/kdbg-probe/FINAL.md (確定設計 r2、本文をそのまま収録)。

> 状態: **現行 (2026-11-07)**

# kdebug 名前付き採取点 — 設計改訂 r2
r1→r2 の変更
- F1: 読み方を一本化し、index 選択・復元のみ常に許可、data 書込みは禁止。
- F2: 通常文脈での採取時 serial 出力を追加し、停止後の出力も維持。
- F3: 128B wire・callback・ATTACH・各 export 番号と loader 位置を明文化、PROBE_OP=20。
- F4: GF は MMIO の CR 対、Trident は実際の I/O port の例に修正。
- F5: 停止後に ring を出して消去し、ACK・再送識別・世代付き採取 ID を削除。
- F6: 段 A を A1（契約・broker・ホスト試験）と A2（loader・export）に分割。
- F7: 読出し副作用は指定者の責任、点は手順の区切りに置く利用契約を追加。
状態: 設計案・実装前。2026-11-07。整数演算、シングルタスク・協調切替、kdebug.bin 分離を前提とする。

## 1. 実物と目的
| 現物 | 確認した契約・不足 |
|---|---|
| `hwdev/gf/display/service.c:395,417,441,516` | detect 直前に CR1F/44/11/3E/3F/36/37、PMC、ENGINE_CTRL とソフト状態を固定出力。項目追加には再ビルドが必要 |
| 同 `:402,410` | index を読み、選択して data を読み、index を復元する。「Read-only」コメントでも index 書込みは実在 |
| `kdebug/gf_display_test.c:276,296` | シリアル gate を確認して EDID 等を出力。手順途中の任意レジスタ採取設定ではない |
| `kdebug/hwb_wire.h:6,24,27` | HW_OP=18、version=1、32B 固定 wire。READ/WRITE/POINT、IO/PHYS/PCI |
| `kdebug/hwb_access.c:11,42,60` | 幅・境界検証、MMIO 一時窓と cleanup、失敗読値0。複数項目・保存先・採取点はない |
| `userland/shell/hwb_exec.c:135,432,447` | BAR 解決はシェル、各命令を begin/end 記録。ドライバ途中には入れない |
| `kdebug/kdebug_main.c:126,218,223` | dbg_call で HW_OP を先に振分け。init は表の返却だけで登録も I/O もしない |
| `kernel/module/kdebug_call.c:21` | 未ロード時の呼出しは NOSYS。公開 KAPI を増やさず新しい private op を運べる |
| `kernel/module/kdebug_imports.def:2555` 以降 | HWアクセス・資源照会等の kernel imports。末尾は index 320 |
| `hwdev/hwdev_imports.def:180`、`docs/design/hwdev/hwdev_imports.md:7,38` | HWDEV の core と kernel import は別物。末尾追加、生成、監査が必要 |
| `kernel/module/hwdev_module.c:90,123` | hwdev は kdebug RUNNING が前提、公開後は登録参照で保持 |
| `kernel/module/module.h:7,12`、`kernel/module/module.c:74` | kdebug ops v4、公開は検査後。ops を伸ばさず内部 import で橋渡しする |
| `drivers/video/trident/lifecycle.c:84`、`trident_module.c:156,167` | Trident は独立部品で screen_init と detach を持つ。HWDEV imports だけでは届かない |
- 一度採取点を配備した後は、項目・幅・番地・有効化を台本だけで変更する。新しい採取位置の追加はビルドが要る。
- 既存の固定診断・HW_OP=18・EDID の結果は初版では維持する。ソフト状態の汎用採取は別機能。

## 2. 宣言・登録・境界
```c
/* 新規 include/debug_probe.h。部品内の静的 RW slot と const 宣言表。 */
typedef void (*probe_hit_fn)(u32 id);
struct probe_slot { probe_hit_fn hit; u32 id; };
struct probe_decl { const char *name; struct probe_slot *slot; };
#define PROBE_HIT(s) do { if ((s).hit) (s).hit((s).id); } while (0)
/* 各部品: static struct probe_slot detect_pre; */
/* const table[] = {{"gf4.detect.pre", &detect_pre}, ...}; */
/* kernel resident broker (新規 kernel/module/debug_probe.c): */
i32 debug_probe_register(struct module_handle owner,
                         const struct probe_decl *table, u32 count);
i32 debug_probe_control(u32 action, void *arg, u32 size);
typedef void (*probe_callback_fn)(u32 id); /* slot.hit と同型、同期呼出し */
struct probe_attach { struct module_handle owner; probe_callback_fn callback;
                      u32 count; u32 ids[8]; }; /* kernel 内部のみ、ポインタ保持は参照で保護 */
/* control: LIST=1 / ATTACH=2 / DETACH=3。ATTACH arg=&a, size=sizeof(a)。 */
/* owner は kdebug の handle、ids は有効化する点。全件検証後に一括公開。 */
typedef i32 (*probe_get_decls_fn)(const struct probe_decl **table, u32 *count);
/* 各 export の get_probe_decls は上記型。静的表と件数を返すだけ、成功0。 */
```
- 通常は slot.hit=0。採取箇所はポインタ比較1回だけ、文字列探索・import 呼出し・引数計算・I/Oなし。逆アセンブルでも確認。
- 登録時に名前を kernel 表へコピーし、owner の世代付き handle と slot の所在を保持。採取 ID は表の添字 0..31。解除時に設定も無効化してから再利用し、世代付き採取 ID は設けない（module handle の世代検証は維持）。
- 名前は ASCII `[a-z0-9_.]`、1..31字、全体で重複不可。宣言表の RO、文字列の終端、slot の RW 所属・整列を owner 範囲で検証。
- 各部品の export に宣言表を返す関数を末尾追加し、loader が export 検査後・最初のドライバ操作前に登録する。該当部品 ops/version・export 契約を更新。
- export 番号は0起点: hwdev は `hwdev_main.c:26-30` の末尾に `[2] get_probe_decls`、GF 本番は `drivers/video/geforce/geforce_body.c:65-68` に `[2]`、Trident は `drivers/video/trident/trident_body.c:14-17` に `[4]` を追加。全て上記型、関数 text 所属と表を検証する。
- 登録位置: `kernel/module/hwdev_module.c:82` の検査後・:83 の公開前、`drivers/video/geforce/geforce_module.c:231` の参照保持後・:232 前、`drivers/video/trident/trident_module.c:158` の参照保持後・:159 の prepare 前。start 中の登録は暫定とし、RUNNING 後に利用可、失敗時は解除する。
- HWDEV と GF production 側で同じソースを別像に組む場合は、実行主体ごとに登録し、同名の同時公開を拒否する。採取対象は実際に選択された主体1つ。
- Trident も専用 loader/export/import 生成経路を使う。kdebug の関数アドレスをドライバへリンクしない。
- kdebug_init は変更しない。最初の ARM で kdebug が kernel broker に callback を渡す。broker が RUNNING・text 所属を検査し kdebug の登録参照を保持。
- ARM が成功してから slot.id、最後に slot.hit を公開。STOP は先に全 slot.hit=0、その後 callback 参照を解放。設定変更は停止中のみ。
- 登録中は部品の登録参照を保持。detach/起動失敗は slot を0化→登録削除→参照解放→像解放。loader の失敗巻戻しにも同じ処理を入れる。
- 採取関数は yield しない。再入は busy で拒否し件数だけ増やす。ARM/STOP/登録更新は通常文脈でのみ実施する。
- kdebug 未ロードなら broker は callback を一切設置せず、ドライバは no-op。設定コマンドだけ NOSYS。hwdev 自体の既存ロード条件は維持。
- 置き場: `gf4.detect.pre` は service.c:516 の診断直前、`gf4.mode.post` は :374 の restore 成功後（初期モード限定と明記）。
- `tri.screeninit.post` は lifecycle.c の screen_init 成功帰還直前。初版は IRQ/NMI・共用 MMIO 窓使用中の点を置かない。

## 3. 設定と採取
| 台本命令案 | 意味 |
|---|---|
| `probe reset` / `probe list` | 停止中の設定・記録消去 / 登録名・有効状態を列挙 |
| `probe add gf4.detect.pre ioread8 port 0x1234` | 1行1項目で追加。ioread16/32、mmioread8/16/32 phys も同形 |
| `probe add gf4.detect.pre idxmmio8 <BAR0+0x6013d4> <BAR0+0x6013d5> 0x1f` | GF_MODE_REGISTER_BAR の SVC_CR（service.c:402）。山括弧は説明用、台本では解決済み物理番地を記す |
| `probe add tri.screeninit.post idxio8 0x3d4 0x3d5 0x1f` | Trident の color CR 対（trident.h:89,536-537）。index/data/index値 |
| `probe arm` / `probe arm serial` | 全項目を再検証して一括有効化。既定は ring、serial は採取時出力も行う |
| `probe stop` / `probe drain` | 無効化 / 停止中の記録を既存 HW ログ出力口へ送る |
- 上記は新規 hwb_probe.c が構文・実行を担当し既存 parser/executor は振分けだけ追加する。dot を含む採取名は既存16字変数名と別の字句とする。
- 設定は kdebug 所有の1セッション。run/load終了後も ARM は維持し、別のシェル操作で enter 等を実行してから STOP/DRAIN できる。
- 例: 設定台本を実行→通常のドライバ操作→`hw run -o /host/logs/probe.txt drain.hwb`（stop, drain の2行）。再起動で設定消失。
- 起動前半の採取はこの方式で有効化できない。初版はシェルから再実行できる経路を対象とし、永続ブート設定は持たない。
- private `PROBE_OP=20U` を dbg_call に追加（main の wire 定義を grep 確認: recover=3,4,11,12,15、GF=5..10,13,14,16,17、HW_OP=18、TRI_OP_COMMAND=19）、既存 HW_OP/32B wire・公開 SDK/KAPI slot・kdebug ops v4 は不変。
- 新規 probe_wire.h: version=1、固定128B、ポインタなし。以下の欄順を固定し `STATIC_ASSERT(sizeof(struct probe_wire)==128, probe_wire_size)`。
```c
struct probe_wire { /* 全整数は32bit、offsetはbyte */
    u32 version, size, action, flags;                 /* 0..15 */
    char name[32];                                   /* 16..47 */
    u32 id, kind, width, address, data_address, index;/* 48..71 */
    u32 hit, item, value; i32 status, restore_rc;     /* 72..91 */
    u32 captured, dropped, overflow, more, reserved[5]; /* 92..127 */
}; /* LIST=1, ADD=2, ARM=3, STOP=4, RESET=5, DRAIN=6, STATUS=7; flags bit0=serial */
```
- action ごとの未使用欄0、正確なsize/versionを両側検証。DRAIN は停止中に1項目ずつ返して消費し、more=0まで shell が出力する。ACKなし、call失敗時は出力無効。
- レジスタ記述は kind/width/address/data_address/index の固定5語。初版は port/phys の確定値のみ、BAR変数やユーザーポインタを保持しない。
- 幅はIO/MMIO=1/2/4、index/data=1のみ。自然整列・終端越境・index≤255を事前検証。不正設定はハードへ触らず旧設定を保存。
- 通常IO/MMIOは in/load のみ。read-clear 等の読出し副作用やバス停止の回避は保証しない（既存 HW-BASIC と同じ限界）。
- index/data は選択と復元のみ例外として常に許可する。保存可能な単純対だけ: IF保存停止→旧index読出し→index選択→data読出し→旧index復元→IF復元。
- data への store、unlock、bank/head切替、Attribute Controller の flip-flop 等は不可。対象対の成立は既存ドライバ資料・メーカー資料と MIT/BSD 実装で確認し受入台本に明記する。
- MMIO pair は両番地を単一窓で扱える同一ページに限定し、index用RW写像が必要。dataは常にload。窓確保失敗は書込み前に返す。
- 途中失敗でも保存済み index の復元を試み、read_rc/restore_rc を別記。復元失敗はその点を無効化。GF の既存 accessor を無条件流用しない。
- 通常IO/MMIOは hwb_access の READ のみを内部利用。POINTやWRITEを採取記述から指定できない。窓が使用中なら WINDOW を記録して終了。

## 4. 記録・限界・エラー
| 資源 | 上限・動作 |
|---|---|
| 登録点 / 有効点 / 1点の項目 | 32 / 8 / 16。上限超過は LIMIT、黙って切捨てない |
| 記録 | 64回分の snapshot、各16項目。固定約36KiB。1回分を先に予約し、満杯なら全点停止、OVERFLOWを状態欄に保持 |
| 1回の採取 | 最大16読出し、動的確保・VFS・yieldなし。ring はログ整形なし、serial は通常文脈のみ固定上限の行整形・kprintfを許可 |
| 失敗 | 最初の失敗項目まで記録し残りSKIP。その点は停止、ドライバの戻り値は変えない |
| 連番 | hit はu32。周回前に停止し LIMIT。世代付きの記録 ID は持たない |
- snapshot は名前/hit/項目ごとの番地・幅・値・status・restore_rc を値コピーし、部品・shellへの借用ポインタを含めない。
- レコード例: `HW run=7 seq=2 line=2 phase=end op=probe name=gf4.detect.pre hit=1 item=0 width=4 addr=0x... val=0x... rc=OK`。
- DRAIN 時のrun/seqは外側HWログと共通採番、採取時hitは別欄。終端に captured/dropped/overflow を記録し、欠落を成功扱いしない。
- ファイルは既存 hw run -o の4KiBまとめ書き・短いwrite継続・log_rcを使う。STOP後に全件DRAINして消去する。出力失敗時も残りを消費・消去しlog_rcで失敗を返す（再送保証なし）。
- `-o`なしは停止後に同じ1行を既存kprintfミラーでシリアルへ配送。`probe arm serial` は各項目の採取直後にもkprintfで1行を出す（name/hit/item/値/rc、phase=hit）。通常文脈の点のみ、シリアル利用可能性をARMで検証し、ringも保持する。出力時間が手順に影響することを契約に明記する。
- バス停止・CPU faultでは復元もDRAINも保証できず未出力記録は失われる。通常エラーは ARG/NOSYS/WINDOW/IO、追加は LIMIT/BUSY/OVERFLOW。
- 不明名・未登録IDは ARG、稼働中の変更やDRAINはBUSY。記録はDRAINで消去し、エラー状態は明示RESETまで保持する。

- HW_BASIC_DESIGN の利用契約へ追記: 読むと状態が変わるレジスタ（0x3DAのflip-flop、read-clear ISR等）は指定者の責任。点は手順の区切り（原則post）に置き、detect.preも直前手順完了後の境界とする。

## 5. 容量と段
見積りは未測定。実装後に map/size・部品image/mem上限・FD空き容量で差分を実測する。
| 対象 | 増分目安 |
|---|---|
| 採取側部品 | 1点 code 16～32B、slot 8B、名前/宣言表≤40B。3点で概ね0.25KiB＋export接続 |
| kernel | broker text 2～4KiB、登録表BSS≤2KiB。FD上のkernelはtext/data分と圧縮差を実測 |
| kdebug.bin | text 4～8KiB、設定BSS約3KiB、記録BSS約36KiB。imageとmemを別々に確認 |
| shell / 起動FD | shell text 3～6KiB。kdebugはPOST_ROOTでありFDローダ変更なし。FD収録対象と余白は生成成果物で検査 |
| 段（各段で独立受入） | 変更ファイルと責務 |
|---|---|
| A1 契約・登録 | include/debug_probe.h（型/上限）、kernel/module/debug_probe.c（登録/寿命）、tools/tests/（ホスト偽部品試験） |
| A2 loader・export | module loader各ファイル（結線）、export/import def・生成器（末尾契約）。kdebug_importsにはcontrolを末尾追加 |
| B 採取エンジン | kdebug/probe_wire.h（wire）、probe.c（設定/採取/リング）、probe_index.c（pairアクセス）、kdebug_main.c（op振分け）、ビルド目録 |
| C 台本・出力 | userland/shell/hwb_probe.c（命令）、hwb_parse.c/hwb_exec.c（dispatch）、hwb_log.c（probe行）、HW_BASIC_DESIGN.md（利用契約） |
| D 実点・受入 | hwdev/gf/display/service.c、drivers/video/trident/lifecycle.c（点）、各export宣言表、tools/accept/（台本）。最初の実点はA/Bのホスト偽部品受入後 |
- HWDEV core表を伸ばす必要はない。内部モジュールexport/import変更は生成契約・CRC・該当ABIを同期し、新旧不一致をloaderで拒否する。
- 公開KAPI追加を避ける。もし実装で必要と判明した場合は本票を改訂し sdk/kapi.json と[ABI]の clean→all を別途適用する。

## 6. 受入・試験・変異
| 対象段 | 観測による合格条件（ホスト試験と変異） |
|---|---|
| A1/A2 | kdebug不在/未ARMではhitが0でI/Oゼロ、無効経路1判定。重複名・範囲外slot拒否。detach/登録失敗/再登録で古い参照なし。変異: 範囲検査・解除順を各1個外しRED |
| B | 8点×16項目、幅/境界/overflow/再入/登録解除を偽IOで確認。通常IO/MMIOのstore回数0。変異: 幅・満杯・解除処理を個別除去しRED |
| B pair | 選択書込みと復元だけ、data storeゼロ。各段の注入失敗でIF/PTE/indexを検査。変異: 復元・IF復元・data宛先禁止を外しRED |
| C | 256B行内の命令、31字名、停止後の全件出力と消去・出力失敗log_rc、serialの採取直後出力と通常文脈制限、既存HW_OPと -o/step/abort の回帰。変異: ring消費・serial呼出し・wireサイズ検査を外しRED |
| D | NP21/Wで未ARMの表示/enter既存結果不変、ARM→操作→STOP→-oの実ログ照合。実機GF/TridentはPMがノートへ依頼し値を既存診断と比較 |
- 実機では同一バイナリのまま台本の項目を変え、2回の採取内容が変わることを必須にする。点未到達もcaptured=0として識別する。
- 新規 tools/tests はCHECK入力宣言、変異アンカー一意・実行時RED。担当像ビルド＋触った試験 --mutate＋--lint、取り込みはGitHub check/build。未実施を合格扱いしない。
## 7. ユーザー判断が要る未決（0件）
- F1〜F7 は全て PM 決定を採用。
反論: 0件。

## 確定 (2026-11-07、Opus 第 1 回の F1〜F7 を r2 で反映、PM が確認)
- r2 は確認だけで足りるとのレビュアーの判断に沿い、PM が F1〜F7 の反映と PROBE_OP=20 を確かめて確定。

## 追記 (2026-11-07): 段 A3 — kdebug の owner の取得と登録の epoch

> 出所: ~/os32-tmp/run/kdbg-badd/ADDENDUM.final.md (astra 起草、Opus 第 1 回、PM が反映)。段 B の実装者が止まった 2 件 (自分の handle を得る口、停止中の解除と再登録の見分け) を埋める。末尾の「確定」が本文より優先。

対象: main cde310ef、A1/A2 済み。単一アプリ・協調切替、公開 KAPI は変更しない。
### 1. kdebug の owner 取得
既存 control に専用 op を足す。init、ATTACH の owner/text 検証・参照保持は維持する。
```c
/* include/debug_probe.h: LIST=1, ATTACH=2, DETACH=3 は維持 */
#define PROBE_KDEBUG_OWNER 4U
/* debug_probe_control(PROBE_KDEBUG_OWNER, &owner, sizeof(owner));
 * out: struct module_handle owner; 成功0、未RUNNING/サイズ不正はPROBE_ERR_ARG */
```
broker が module_records[0..MODULE_RECORD_CAP) の RUNNING かつ MODULE_ID_KDEBUG を探し、
`(struct module_handle){slot_index, record.generation}` を返す。取得だけでは参照を取らない。
これは汎用の「呼出し元識別」ではなく唯一の kdebug の取得。ARM 直前に呼び、失敗なら ARM しない。
### 2. 停止中の unregister を検出
登録集合の epoch を LIST に追加し、解除があれば全設定を失効させる（最小の一括失効）。
```c
/* include/debug_probe.h: 既存欄の末尾に追加 */
struct probe_list { u32 count; struct probe_info points[PROBE_POINT_MAX]; u32 epoch; };
/* kernel/module/debug_probe.c: 新規内部状態 */
static u32 registry_epoch = 1U;
```
LIST は件数0でも現在の epoch を返す。実際に点を1つ以上消す unregister で、
既存 stop_attachment → epoch 更新 → entries 消去・参照解放の順とする。停止中も更新する。
空解除・失敗解除・DETACH・register では更新しない。同じ owner/name/id の再登録でも旧 epoch と異なる。
周回しない: U32_MAX の次は0（永久失効）。0では非空 register を PROBE_ERR_LIMIT、ATTACH も PROBE_ERR_LIMIT。
既存点の解除・LIST・DETACH は引き続き可能。失敗 register による点・参照の部分更新は禁止。
B は最初の ADD で epoch を保存し、以後 ADD/ARM/STATUS/DRAIN の入口で LIST と照合する。
不一致/0なら全設定を破棄し STOP 状態へ、ARM は失敗（再ADDを要求）。旧記録は停止済みとして排出可。
ARM の LIST 照合から ATTACH までは yield しない。wire と採取 ID は変更しない。
これは §2 の「解除時に設定も無効化」を遅延照合で具体化する。点ごとの世代付き採取 ID は設けない。
### 3. 生成契約・固定値
`kernel/module/kdebug_imports.def` は control の既存 index=321、schema=2、計322本を維持（追加 import 不要）。
`tools/module_schema.py` の KDEBUG `ops_version=4` を `5` に上げ、旧像を loader で拒否する。
`python3 tools/gen_module_schema.py` → `kernel/module/module_schema.h`、
`python3 tools/gen_kdebug_imports.py --stage S6 --out-dir kdebug/generated` を実行する。
control の関数型は同じなので KDEBUG_ABI_GEN は変わらない。私有契約変更は ops_version で識別する。
`make all` の build/kdebug.mk の正規レシピで hwdev/generated を再生成（debug_probe.h は contract CRC の入力）。
HWDEV_ABI=1、HWDEV_MODULE_OPS_VERSION=2、core 26 slots と provider export 番号・版は維持。
hwdev_contract_fixture.py の import/layout/inventory 固定値は当該入力不変なので維持し、CRC の kernel/core/body 一致を確認する。
KDEBUG 版を固定する試験値のみ5へ追従し、旧4の拒否も試験。生成物の手編集は禁止。
### 4. 段と受入
A3: include/debug_probe.h、kernel/module/debug_probe.c、tools/module_schema.py、上記生成物、tools/tests/debug_probe_host.c・test_debug_probe.py と KDEBUG版固定の既存試験。正典への本追記はPMが反映。
B PACK追記:「A3後の基点を使い、ARM直前に PROBE_KDEBUG_OWNER で owner を取得する。ADD時epoch保存、操作入口の照合・全設定失効・再ADD要求を実装する。」
試験: A3はowner取得の成功/未RUNNING/誤サイズ、停止中ADD相当のLIST→解除→同一再登録のepoch差、空解除不変、周回封鎖、既存detach/参照回帰をqemuで検査。Bで旧設定ARM拒否→再ADD成功を検査。実装時に対象ビルド・変更試験 --mutate・--lint、取込CIはPM。
変異: owner取得のRUNNING条件削除、停止中unregisterのepoch更新削除、Bのepoch比較削除を各一意アンカーで実行時REDにする（A3とBそれぞれの担当試験）。

### 確定 (2026-11-07、Opus 第 1 回を PM が反映、本文より優先)
- KDEBUG の ops_version は 4 のまま (確定設計 §3「kdebug ops v4 は不変」)。§3 の 5 への版上げ・module_schema.h と kdebug/generated の再生成・「旧 4 の拒否」の試験は削除。旧 kdebug.bin は FLAG_PAIR の pair CRC (kernel/module/module_image.c:127) で拒否される。
- epoch は単に足すだけ。周回の扱い (0 で永久失効、0 のとき register・ATTACH を LIMIT で拒否、B の「0 なら破棄」、試験の「周回封鎖」) は削除 (シングルタスクで 4G 回の解除に届かない)。
- hwdev/generated/{hwdev_contract.h, hwdev_core.c, hwdev_imports.json} は include/debug_probe.h が contract の入力なので CRC (HWDEV_CONTRACT_CRC・module_abi_gen) だけ変わる。再生成は make all (build/kdebug.mk:81-82) で行い、A3 の取り込みに含める。REVIEWED_* の 3 値は動かない。hwdev・GeForce・Trident の像は kernel と同時に配備する。
- B: ADD で保存する epoch は、名前から ID を引いたのと同じ LIST の応答の値。ADD の入口で不一致なら全設定を破棄し、今の ADD を最初の ADD として続ける (失敗にするのは ARM だけ、状態の欄で失効を示す)。記録の名前は ADD のときに保存したものを使う。
- 変異を 1 件足す: OWNER の検索から MODULE_ID_KDEBUG の条件を外す (kdebug より若い slot に RUNNING の別の module の記録を置く試験で RED)。
