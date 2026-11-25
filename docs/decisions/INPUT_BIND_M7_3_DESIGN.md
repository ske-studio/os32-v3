> 出所: 本線の機械の ~/os32-tmp/run/m7-3-design/DESIGN.final.md (astra 起草・sol レビュー 2 回、2026-11-21 確定)。
> 状態: **確定 (2026-11-21)** — 入力 INPUT_BIND の M7[3] (IME の接続) と登録 B を今の main の形で書き直す (11-17 の版は外した)。M7[1]・[2] は wip/km-m7-rebase。末尾の「確定」の節は本文に優先する。

# INPUT_BIND 追記案　M7[3] と登録 B

## r1→r2 の変更

- P3 旧 init の公開呼び出しを 113 行に訂正した。
- P1 B-P の load/start と最終 boot check 後の公開を確定設計に結んだ。
- P1 常駐側が失効を完了してから後始末に入る順を両機種に割り当てた。
- P1 GUI STOP と KAPI 負値復帰の取消を pending と合流させた。
- P2 snapshot・capture・take の戻り値と失敗時の状態を明記した。
- P2 実打鍵の capture と配送を S4 に割り当てた。
- P2 kernel/R1 別の kdebug.bin の組照合と FEP の独立した ABI 負例を加えた。
- P2 FG1 の入口ごとに変異、検査名、fixture・stub の追従先を割り当てた。

対象は main 04fa184ed と入力側の前提 4512a11af。M7[1]・[2] の状態と pending を引き継ぐ。
PowerPC の部品形式と公開順は PPC_MODULE_ALIGN_X86_DESIGN の確定節に従う。

## 1. 11-17 版との食い違い（3 件）

1. 505fdd1c1 の sys/conf/features/pc98-devices.mk の装置選択規則は gate を PC-98 装置の列に入れた。
   gate の持ち主は共通入力で、M5 は 4512a11af の build/sources/common.mk の C_COMMON_EXEC にある。
   gate も同じ共通列で選び、build/sources/order.mk の C_ORDER_LEGACY は x86 のリンク順だけを担う。
2. 旧 build/ppc-kboot.mk の旧ソース一覧 は PowerPC の個別ソース列にも gate を追加した。
   現行の構成は共通ソースを使う。sys/conf/features/fep.mk の C_FEATURES・MODULE_MK_fep は facade の選択と x86 の pack を区別する。
   L1・L2 の取り込みだけでは PowerMac の FEP は公開されない。PPC 個別列への二重登録を廃する。
3. 旧 sys/kern/ime/ime.c の ime_boot_abort の init からの状態公開は、部品の初期化と入力サービス公開の間を区別できない。
   sys/kern/ime/fep_module.c の public_api・ime_init・fep_start は候補を非公開で初期化し、RUNNING と services_public の両方で公開する。
   sys/kern/module/module_runtime.c の module_load は init より前に import を埋める。この時点の関数番地は公開の証明にならない。

## 2. 常駐側と部品側の境界

次の三つを両 CPU 共通の FEP import とする。型の正典は sys/input/input_context.h。
末尾への追加順を宣言順に固定する。いずれも通常文脈で呼ぶ。

```c
void kbd_policy_ime_state(u32 active, u32 owner, u32 command_gen);
int input_context_snapshot(KbdPolicySnapshot *out);
int input_ime_cancel_take(const KbdPolicySnapshot *current);
```

常駐側が三関数を提供し、部品本体が生成 stub を通して呼ぶ。
型・構造体を含む契約から FEP_MODULE_ABI_GEN を再計算する。生成値を数値で転記しない。
旧案と同じく私有 FEP ops 世代を 4 から 5 に揃え、部品 ID 3 と公開 KAPI は維持する。
両機種で同じ定義から対象 CPU 用の表を作る。pack の形式は x86 v1、PPC v2。
型を生成時に照合し、load 時は世代、pair、import 契約を照合してから init に進む。

入力側の採取と、部品を生かす既存の境界は次の通り。

```c
void input_ime_snapshot(KbdPolicyImeSnapshot *out);
int input_ime_cancel_capture(const KbdPolicyImeSnapshot *destination);
int fep_module_load(struct module_handle sqlite);
int fep_module_abort(void);
```

input_context_snapshot は成功で 0、NULL または foreground owner 不在で -1 を返し、失敗時は出力を書き換えない。
capture は有効な active・owner・exec_gen を記録して 0、NULL または無効な宛先なら -1 で slot と計数を保つ。
take は一致で 0、不一致で -1。非 NULL なら不一致でも保留一件を消費する。NULL または空なら -1 で状態を保つ。

PC-98 は現行の kernel と R1 に生成 fep_kexports をリンクする。
既存の fep_module_load が request.imports を渡し、SQLite 依存・start・公開をこの順に通す。
PowerMac は ATA、HDD インストール、HDD root 起動の後、確定設計の FEP 段で同じ表を resident 側に登録する。
B-P の根拠は PPC_MODULE_ALIGN_X86_DESIGN「SQLite、FEP、KCG と診断」と末尾の確定。int sqlite_static_boot(void) の検査成功、SERVICES_SQLITE_ONLY、services_public を確認し、静的 SQL の ready を load と start の各直前にも確認する。FEP request は dependencies=NULL、dependency_count=0 とし、ID 2 は製品選択で充足する。pair は request の呼出元で platform_module_pair の成否と値を policy に渡す。
/db のメタ検査と予約検査成功後に設定を保存する。load/start の成功（0）だけでは入力を開かない。int fep_services_public(void) は FEP の最終検査 os32_sqlite_outer_boot_check(ime_boot_connections(), reserve) 成功後だけ真となり、facade は RUNNING も確認する。途中の失敗では閉じたまま SQL を継続する。
load に必要な表は load 前に揃える。入力から使える provider としての登録時点は FEP の公開成功後とする。
公開判定は既存 facade に集約し、常駐入力に部品の関数ポインタを保存しない。
PowerMac の先行像では「active=0、取消先なし」が不在の組。入力と通常のキー配送を継続する。
欠損・世代不一致・init 失敗・公開前の巻戻しにもこの組を適用する。
非公開 init は active を立てない。停止時は有効な宛先を閉じ、残った pending を失効させてから再公開する。
失効には inactive の公開と、無効な宛先（全フィールド 0）の snapshot を渡した take を使う。
後者は gate の比較で不一致となり、古い一件を消費する。部品内の取消処理は呼ばない。

公開後、未確定ローマ字、かな、候補変換のいずれかを持つ ON 状態だけを active とする。
CUI・GUI の入力処理、モード変更、取消の終了時に M5 の owner と command_gen を添えて公開する。
M5 の取得失敗は無効を公開する。空の ON、OFF、確定済み UTF-8 の未読分だけなら inactive。
採取は割込み状態を保存し、同じ owner の現行 exec_gen を添える。0 は宛先なし。
後続 S4 の STOP/ETX の採取側は独立した pending に記録する。IRQ から FEP や辞書を呼ばない。
同じ owner/exec_gen は一件にまとめ、別の組で置換し、各カウンタを M7[2] の規則で増やす。
FG1 の待機・非待機 CUI と GUI feed は配送前、ブロックして戻った時は戻り値の解釈前にも take する。
割込み禁止または M5 取得失敗では take を見送る。owner と exec_gen が一致した一回だけ ime_cancel_input を呼ぶ。
不一致なら古い要求を捨てる。command_gen は状態の公開値であり、取消の宛先照合には使わない。
GUI の直接 STOP と両 KAPI の負値復帰では、その入力処理で take 成功済みなら既存分を合わせて ime_cancel_input() を一度だけ呼ぶ。待機内で取り消した事実は同じ呼び出しの復帰側まで渡す。pending なし・不一致では従来の STOP 処理と負値時の sink 条件を保つ。KAPI は取消判定後に ring3_abort_check()、復帰すれば元の負値を返す。取消時は未読の確定末尾も破棄する。
既存取消の mode と表示の扱いを保つ。通常の XFER・辞書・学習・確定文字配送は既存経路を通る。

## 3. ファイルごとの段

M5 と 4512a11af の取り込み後に開始。各行は製品 1 ファイル、追加・削除合計 350 行以内。
依存する生成物と像は整合した組で検証・取り込みする。生成出力は生成器で更新する。

|段|担当・ファイル|変更と依存|
|---|---|---|
|B1|本線 build/sources/common.mk|M5 と並べて gate を両像へ一回登録|
|B2|本線 build/sources/order.mk|gate の x86 順を M5/M6 の隣に確定|
|B3|本線 build/fep-module.mk|宣言ヘッダと schema を生成依存に含め、増分ビルドでも再生成|
|B4|本線 tools/prototypes/fep_imports.def|三 import を末尾に追加、宣言元を contracts に追加|
|B5|本線 tools/module_schema.py|FEP の ops_version を 5 にする|
|B6|本線 sys/kern/module/module_schema.h|B5 の C ビューを再生成|
|B7|本線 sys/kern/ime/fep_module.h|facade の世代を 5 に同期|
|M7[3]|本線 sys/kern/ime/ime.c|公開後の状態通知と FG1 の接続、既存取消との合流。内部試験後に B と組で像を検証|
|B8|本線 sys/kern/ime/fep_module.c|失敗・停止の全経路で常駐側が公開を閉じ、inactive を通知し、全ゼロの KbdPolicySnapshot で take してから後始末。候補なし・quiesce 前の終了も同順|
|B-P|PowerPC sys/conf/features/fep.mk|HDD root 後の FEP 実装段で対象 pack を選択、同じ定義の生成 provider を製品リンク|

B-P は確定済み FEP 段の登録を兼ねる。同段の対象別 make 規則に生成依存を渡す。
B-P の request・公開判定・失効は sys/kern/ime/fep_module.c、静的起動は sys/kern/sqlite_static.c、予約と最終検査は sys/kern/sqlite_engine.c の担当とする。PowerPC 側でも常駐で公開を閉じる → inactive → 全ゼロ snapshot で take → 取得済み handle を一度だけ後始末の順。全接続・group 解放後に予約解除し、後始末に失敗したら確定設計どおり隔離する。
S4 は実打鍵 STOP/ETX から capture を呼び、両入力経路への配送をゲストで受け入れる。M7[3] は試験から capture で pending を直接入れる。新設の host 試験は作成段で CHECK と入力・basis を登録する。
M7 は新しい STOP 割込みを有効化しない。後続の S3 → S4 → S4i の依存を守る。

## 4. 受入と変異

|受入|観測する結果|落とす変異|
|---|---|---|
|非公開・失敗|init 中も inactive、取消先なし。欠損時も生キーがシェルへ届く|公開条件を外す|
|変換開始・終了|部分ローマ字、かな、候補で active。確定直後と空の ON で inactive|部分ローマ字を除く、ON だけで active|
|FG1|待機前・復帰後、非待機、GUI で次の配送前に一度取消。直接 STOP・両 KAPI 負値に pending 有無を組み合わせ、取消回数、未読末尾の破棄、次の入力と戻り値を照合|下表の各入口を単独で外す|
|割込み禁止|禁止中の FG1 は pending を保持し、有効化後に一度取消|禁止時にも take|
|宛先再利用|同 owner の別 exec は無効果、同一組の重複と別宛先の置換を計数|exec_gen 比較を外す|
|満杯|通常キュー満杯でも一致する取消が一回届く|満杯なら capture を捨てる|
|失効|停止前に捕捉して同じ owner/exec で再公開しても旧取消が届かない|失効時の take を削る|
|ABI|新旧の kernel と部品を交差させると init 前に拒否、正しい組は成功|世代照合を迂回した独立の不正像|
|既存入力|XFER 切替、STOP、辞書検索・学習、複数 byte の UTF-8 の順序と回数を確認|配送を一回飛ばす|

|担当段|検査名（新設は案名）|単独で落とす変異・追従先|
|---|---|---|
|M7[3]|ime-fg1-host（新設）|getchar/getkey を個別に、待機前・待機復帰後・非待機の各 take を一つずつ外す。各入口だけを通し、取消回数と配送順で識別|
|M7[3]|ime-fg1-host|GUI feed の take を外す。別変異で直接 STOP・各 KAPI 負値の二重取消を復活。未読末尾を残す変異は次の読出しで識別|
|B1–B8|build-selection-host、test_fep_module.py|選択 fixture の gate と生成 provider、fep_module_host.c と IME host stub の三 import を同期。旧 ops/version・公開・入力処理を指す既存変異の目印を各担当段で移し RED を確認|
|B-P|ver-ppc-input-usb、test_fep_module.py|最終検査前の開門、失効 take の欠落を個別に検出。PPC の静的 SQL 選択 fixture と request stub を同期|
|S4|ime-stop-delivery-guest（新設）|STOP・ETX の実 capture 接続を各々外し、実打鍵からの取消回数と次の読出しで検出|

FEP の ABI 世代・ops 世代・import 契約・pair は各一項目だけ不正にした四つの像で init 前の拒否を独立に検証する。
ABI の負例では CRC 等を作り直して対象 field だけを違える。各変異は固有の観測で RED とする。
clean → all、対象 IME・FEP 境界・module・M7 の試験を通常と変異で実行し、CHECK lint を通す。
x86 build_compare は M7/B の前後で kernel・R1・対応 fep.mod・kdebug.bin・それらを収容する像に差が出る。
kernel と R1 の各 vmkernel.lz4 に対して fep.mod と kdebug.bin の両方の pair を照合し、正しい組の受理と異なる pair の拒否を確認する。
map と生成契約で差を gate、入力処理、FEP 世代、pair と派生アドレスに帰属させる。
PowerPC 専用の B-P 前後では同条件の x86 成果物の差はゼロを期待する。
PowerPC は先行像のリンク・起動・不在時入力を先に検証する。
B-P 後は製品 map で同じ三 import の実体を確認し、HDD root の辞書あり変換、欠損時入力を別々に受け入れる。

## 5. 未決

ユーザー判断を要する項目は 0 件。PowerPC の公開時期は確定した HDD root 後の順序に従う。

## 確定 (2026-11-21、sol 第 2 回「4 点を反映すれば可」、PM が反映)
以下は本文に優先する。
- **P1 (§2・§4) 待ちのループの取消の合流**: getchar・getkey の待ちのループでは、pending で取り消した後に次のキーで新しい変換が始まり、同じ呼び出しの後の待ちが負の値で戻り得る。呼び出し全体の「取消済み」の旗は使わない — **負の値を返す待ちの戻りの直後に実行した取消だけ** を KAPI 側の既存の取消と合わせる。試験は両 KAPI で「取消 → 再入力 → 次の待ちで負の値」を照合する。
- **P1 (§2–3、B-P) 学習のパス**: load の前に `fep_boot_user_path_set()` と `fep_boot_user_rc_set()` で学習のパスと取得の結果を設定し、失敗の時は snapshot と一緒に消す (確定設計 PPC_MODULE_ALIGN_X86_DESIGN の確定 §1 と同じ)。担当は PowerPC の FEP の段。
- **P2 (§3、B-P) ファイルの担当**: B-P の製品の選択は sys/conf/features/fep.mk の 1 行に寄せず、確定設計 PPC_MODULE_ALIGN_X86_DESIGN の FEP の段のファイル (build/sources/hooks.mk・build/ppc-fep-module.mk・静的 SQL の後の起動の接続) に、生成の依存・pack・起動の接続の担当を 1 対 1 で結ぶ。
- **P2 (§4) 試験の置き場**: 最終検査の前の開門と失効の漏れの 2 つの変異は ppc-input-usb-host (USB の入力の検査) ではなく、PowerPC の静的 SQL の構成を組む FEP の境界の試験に割り当て、公開の状態と旧い pending の残りをそれぞれ観測する。
