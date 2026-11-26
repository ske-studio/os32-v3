# 入力 HAL 追補 2　S4 / S4i 設計案

> 出所: `~/os32-tmp/run/s4-design/DESIGN.final.md`
> 状態: **確定 (2026-11-21)** — 入力 HAL の S4・S4i の配送と受入を確定した。

## r1→r2 の変更

- P3　辞書失敗・OFF 確定の参照範囲と物理修飾集計の出所を訂正。
- P1-1　取消単独で FG1 に戻る内部待機口と保留時の待機条件を定義。
- P1-2　due 判定と pop を一体化し、OFF 後の確定末尾を両読取り経路で優先。
- P1-3　更新後の共通配送口、未 bind の経路、release・抜去時の解除を指定。
- P2-1　取消の条件と対照試験を M7 active に統一。
- P2-2　XFER 二打の状態遷移と GUI の段別受入を具体化。
- P2-3　PowerPC の先行依存・共通口の登録順と両像の map 受入を指定。
- P2-4　段のリンク単位、既存検査の追従先、guest 登録と個別判定を追加。


対象は 2800873d3。S3 の由来付き評価と M5〜M7・B1〜B8 を利用する。
FG1 は協調切替を行わない通常文脈、IRQ は実打鍵を採取する側を指す。

## 1. 実物の確認

|番号|設計の要求と基点の差|
|---|---|
|1|INPUT_KEYMAP_DESIGN.md §3・§7 (S4) の絶対操作は順序を持つ。sys/input/input_ime_gate.c の input_ime_cancel_capture・input_ime_cancel_take にある保留は取消一件だけで、ON/OFF の列を表せない。|
|2|同設計 §7 (S4i) の FG1 adapter に対し sys/kern/ime/ime.c の ime_toggle・ime_set_mode は反転とモード指定。前者には辞書失敗・OFF 時確定があり、同じ目標の再要求を除く入口が要る。|
|3|INPUT_BIND_M7_3_DESIGN.md の実打鍵採取の項 の実打鍵採取に対し sys/platform/pc98/input/kbd.c の kbd_deliver は HAL、V86、物理救済へ渡す。sys/input/input_ime_gate.c の input_ime_cancel_capture の取消口に、評価済みの実打鍵を渡す結線が要る。|
|4|INPUT_BIND_DESIGN.md の世代照合の項 の世代照合に対し input_ime_gate.c の kbd_policy_ime_state・input_ime_snapshot は公開時に exec_gen を保存せず、読む時の同じ owner から補う。同じ番号の再利用で古い active に新世代が付く。|
|5|INPUT_KEYMAP_DESIGN.md §4 の物理 Ctrl に対し sys/platform/pc98/input/kbd.c の kbd_deliver の kbd_shift_state も kbd_merge.c の物理集計由来。差は救済の判定式にある。S3x の前提差として追跡する。|

参照先は指定枝。M7[3] の確定票だけは指定枝にそのパスが無く、確定票の写し d53261df1 の同名文書を参照した。
sys/kern/ime/ime.c の ime_publish_state の active は前編集・かな・変換の有無。ON の判定はime_is_active のモードであり、両者を使い分ける。
sys/input/input_context.c の input_context_snapshotで前景の owner、exec_gen、command_gen、context、flags を同時に得る。

## 2. 形

### 実打鍵を一度判定する位置

本体 IRQ1 → kbd_deliver → HAL → merge の S3d 出口へ進む。
USB は HID report の新規 make → S2a の usage capture → 同じ出口。poll から来ても IRQ と同じ制約で処理する。
bound は従来どおり更新前の km_eval で effect を得て状態を更新し、更新後の km_eval で配送結果を得る。未 bind は legacy_keymap.c の局所評価を使う。両経路とも更新後の分類は一回だけ。
`int input_capture_delivery(const KmKey *key, u8 was_down, const KmEvalResult *result, u8 raw_due);` を共通 HAL に宣言。key は源・世代・キー・down を保持し、未 bind は本体の源とその世代を指定する。戻り値 1 は消費済み、0 は従来配送。評価済み result は再評価しない。release は抑止印を消し、抜去は当該源・世代の全印を再利用前に消す。
capture の外側の irq_save を保持し、物理状態・in2 更新後に次の順で分岐する。

1. V86 は状態のみ更新し、従来の FIFO・退出へ戻す。
2. GUI は物理 STOP filter → raw_due の raw の順。G でその後に評価済み意味イベントを配送する。IRQ では前景 snapshot を引かない。
3. CUI の物理 Ctrl+STOP は cooked と取消を抑止し、既存の救済に任せる。
4. 新規 make の IME_ON/OFF は no_keydata・修飾文字抑止より前に採る。break/repeat は操作を起こさず消費する。
5. PC98 profile の裸 STOP または真の CHAR_ETX を取消判定へ渡す。判定は cooked の u16 へ畳む前。
6. 取消を採れたらその場で消費する。残りは S3 の出口、将来の端末操作は M6 側の有効化条件に従う。

ここで裸 STOP は物理救済を除いた STOP 操作、ETX は表の通常文字評価の 03。RLDN の LEGACY_CONTROL=03 を除く。
取消を消費した make の repeat は release まで同じ源・キーの保持状態で抑止し、取消後の通常キーに化けさせない。
GUI は G 段で WM が焦点世代を固定して同じ分類を一度使う。裏で RUNNING の owner は参照しない。
G 結線までは既存 WM の直接 STOP と Shift+Space を維持する。注入済み GUI byte を再評価しない。

### M5〜M7 の取消境界

```c
int input_context_snapshot(KbdPolicySnapshot *out);
void kbd_policy_ime_state(u32 active, u32 owner, u32 command_gen);
void input_ime_snapshot(KbdPolicyImeSnapshot *out);
int input_ime_cancel_capture(const KbdPolicyImeSnapshot *destination);
int input_ime_cancel_take(const KbdPolicySnapshot *current);
```

公開口のシグネチャを保ち、同じ排他内で M5 を読み owner・command_gen が一致した時だけ exec_gen も保存する。
無効化・不一致・M5 失敗は全ゼロを公開。snapshot は非 NULL 出力へ保存組をコピーし、後から世代を補わない。
採取は外側の排他中に M5 → M7 snapshot → active と owner/exec_gen/command_gen の一致 → capture の順。
M5 は成功 0、NULL・前景なしで -1。失敗時・active=0 は S3 に戻す。active=1 は変換前のローマ字・かなの前編集も含む。IME が ON だけの状態もここへ戻る。
capture 成功 0 で専用印を立てる。無効宛先は -1 で印・計数不変。同 owner/exec は合流、別宛先は置換する。
取消の take は M7 の既決通り owner/exec で比較。command_gen は採取時の構成確認に使う。
FG1 は待機前・待機復帰直後・非待機・GUI feed の既存 take を使い、IF 無効・M5 失敗なら保留する。
一致の 0 で一度 ime_cancel_input。不一致は -1 で旧印を捨てる。NULL・空は -1。直接 STOP・負値復帰との合流を維持する。
取消は通常 FIFO の空きに依存しない。取得に成功した打鍵から M6 の実行中断・表示復帰を要求しない。
待機中の取消だけでも待機ループが FG1 に制御を返す必要がある。次の入力を消費して起こす方式は採らない。

### CUI の絶対操作と文字の順

通常 cooked FIFO の前後に別々の優先度を与えると「a、OFF、b」の a を OFF 後に処理してしまう。
本案は cooked の成功した push/pop の通番を u32 で持ち、操作に「先行して積めた文字の通番」を付ける。
初期値は双方 0。pop 完了数と先頭操作の通番が等しい時だけ、その操作が配送可能。同位置は採取順。
古いイベントが一周待たないよう、文字の pop は配送可能な操作を追い越せない。通番は等値だけで比較する。

```c
int input_ime_action_capture(u32 action, const KbdPolicySnapshot *dst, u32 after);
int input_ime_action_take(const KbdPolicySnapshot *now, u32 popped, u32 *action);
int ime_apply_absolute(u32 action);
```

capture は CUI 操作専用。1=ON、2=OFF。非 NULL・有効 owner/exec を要求し、command_gen は M5 の値をそのまま保存。
成功 0、不正 -1、満杯 -2。満杯は新着を落として計数し、既存順を守る。いずれも操作キーを文字入口へ戻さない。
take は FG1。先頭の after が popped と一致したら現 owner/exec/command と比較。一致で 1 と操作、不一致は捨てて次を調べる。
空・まだ先の操作なら 0、不正引数 -1。成功以外は出力不変。peek/has は状態を進めない。
sys/kern/input_wait.c に以下の私有口を置く。出力 key は KEY の時だけ有効。

```c
enum InputReadStep { IR_EMPTY, IR_KEY, IR_RECHECK, IR_INTERRUPTED };
enum InputReadStep input_try_fg1(int mode, int *key);
enum InputReadStep input_wait_fg1(int mode, int interruptible, int *key);
```

try は呼出し時の IF を記録し、同一 irq_save 区間で M5・取消照会、due 操作の take、操作なしの場合の provider cooked pop と通番更新までを行う。有効 IF・M5 成功で取消印があれば pop せず IR_RECHECK。due 操作を外した場合は irq_restore 後に adapter を一度実行し、成否とも IR_RECHECK。IF 無効・M5 失敗は印と操作を保持し、due が先頭なら pop を止める。保留だけでは復帰・再試行を連発せず、待機は従来の pending・timeout・wait_step を通り次の通常の起床で再照合する（IF 無効の間は割込みによる起床を前提にしない）。
wait は既存ループの CUI provider 直読を try に置換し、EMPTY の時だけ端末入力・中断・時計を従来順に扱う。FG1 の待機・非待機双方は RECHECK を受けると既存 take、確定末尾の読出し、末尾が空なら次の try/wait の順に戻る。a の処理、OFF の実行、確定した全 byte、b の pop の順になる。公開 input_legacy_wait は IR_KEY の従来値と IR_INTERRUPTED の -1 のみを返す。FG1 外の従来呼出しでは取消単独の再判定を無効にし、操作実行後は内部で再試行する。GUI 注入・タイムアウト値と割込み可能待機の中断規約は現行通り。

通常読出しの直前に resident dispatcher が due 操作を一件外し、割り込みを戻して FEP の adapter を呼ぶ。
操作と次の文字の間は FG1 で IME の確定末尾を先に配送する。既存の待機ループへは内部の再判定結果を返す。
内部再判定を公開 getkey の負値や byte として返さない。直接 cooked を読む呼出しも同じ pop 境界を通る。
待機の条件に実行可能な due 操作と取得可能な M7 の取消ありを含める。取消照会は読取り専用、IF 有効へ戻ってから既存 take へ進む。
操作だけで起床しても byte 待機を完了扱いにしない。非待機なら操作を処理後、文字が無ければ従来の空を返す。
既存の確定末尾は後続の操作より先に返す。先行文字を FEP に渡した後に初めて次の操作を実行する。
flush・表交換・焦点変更・FEP 停止は排他内で操作列を空にする。cooked 自体の flush は pop 通番を push 側へ揃える。
FEP 未公開中は絶対操作を消費して蓄積しない。再公開後に過去の ON を再生しない。
adapter は services_public かつ FG1/割込み有効が前提。無効な action・文脈・未公開は -1。
ime_is_active() と目標が同じなら成功 0。異なる場合だけ ime_toggle() を一度呼び、状態を再読する。
目標到達は 0、辞書失敗など未到達は -1。取り出した操作は再投入しない。次の新規 ON で再試行できる。
常駐から部品を呼ぶ口は私有 FEP ops の末尾に追加し、pair・ops 世代と生成契約を更新する。公開 KAPI は維持する。

### x86 の振る舞いと PowerPC の範囲

|打鍵・文脈|x86 での結果|
|---|---|
|XFER|従来の 3500 と FEP 内の候補一覧展開・次候補選択を維持。ON/OFF の合成には使わない。|
|LANG1 / LANG2|0x90 / 0x91 を明示操作として消費。OFF 時は未確定文字を従来順に確定。|
|裸 STOP|active は専用印で一度取消。inactive は 6000 のまま。通常中断の有効化は E-x。|
|Ctrl+C / RLDN|active 時の真の ETX だけを吸収。RLDN は従来 byte。inactive 時の端末判断は後段。|
|物理 Ctrl+STOP|既存の救済と GUI 抑止を優先。Caps→Ctrl は救済を作らない。|
|GUI|既存 raw/filter・直接 STOP を維持。G で焦点宛て絶対操作を一度実行、CUI cooked は空。|

PowerPC 担当が S2u/S2a/S2m → S3m/S3b の USB 後端を結線。本線は同じ usage 口を x86 USB の 1E で利用する。
K8 に従い kboot は操作を分類した時点で消費。FIFO、FG1 dispatcher、辞書、部品 adapter はリンクしない。
x86 操作列は 8 件、各 5 個の u32 で 160 B、通番・添字・取りこぼし計数を含め 192 B 以内を目標とする。
基点の usb_platform.c の旧 sc 登録と input_usb.c の独自 ring は S2/S3 で置換する。先行段をリンク後、本線は共通 input_capture_key(const KmKey *) と更新後の input_capture_delivery を用意し、PowerPC 担当が起動時に S2/S3 の HID source・provider を登録してから report を有効化する。report → usage → input_capture_key → 更新後の配送口 → kboot の消費判定の順。
PPC の専用操作列は 0 B。既存 snapshot の exec_gen を利用。x86・PPC それぞれ基点と統合後の text/data/bss と差分を記録し、repeat 抑止分も計上する。PPC map で操作列・FG1 dispatcher・FEP adapter の各シンボルが入らないことを確認する。
PPC はリンク末尾と上限 0x275000 の差を前後で記録し、超過時は kboot の不要参照を外して再測定する。

## 3. ファイルごとの段

下表は上から依存順。各行は製品 1 ファイル。私有 ops の世代変更は整合した像で一括受入する。

|担当・段|ファイル|担当する変更|
|---|---|---|
|本線 S4 宣言|sys/input/input_context.h|action capture/take、flush、取消の非消費照会を宣言。|
|本線 S4|sys/input/input_ime_gate.c|公開時世代、取消照会、操作列・宛先照合。|
|本線 S4 ring|sys/hal/input/legacy_ring.c|成功 push/pop 通番、flush、先頭操作と文字の排他。|
|本線 S4 未 bind・表交換|sys/hal/input/legacy_keymap.c|更新後の局所結果を共通配送口へ渡し、表交換時に操作列を破棄。|
|本線 S4 配送|sys/hal/input/kbd_merge.c|S3d の両入口から評価直後に採取。V86/GUI/救済と repeat を分岐。release と km_release_keys で抑止を解除。|
|本線 S3x 前提|sys/platform/pc98/input/kbd.c|物理 Ctrl の救済、既存 GUI 抑止。S4 から重ねて要求しない。|
|本線 私有契約|sys/kern/ime/fep_module.h|末尾に絶対操作関数ポインタ、私有世代を同期。|
|本線 生成元|tools/module_schema.py|FEP ops 世代を進める。|
|本線 生成物|sys/kern/module/module_schema.h|生成元から再生成。|
|本線 私有 ops 実体|sys/kern/ime/fep_body.c|部品 ops initializer 末尾に adapter を登録。|
|本線 S4i|sys/kern/ime/ime.c|adapter 実体、待機再判定、確定末尾の優先。|
|本線 部品境界|sys/kern/ime/fep_module.c|公開中の ops 呼出し、失効時の flush。|
|本線 待機|sys/kern/input_wait.c|try/wait の私有結果と公開 wrapper、待機中の provider pop を同じ境界へ統合。|
|本線 読取り|sys/kern/input_legacy.c|実 pop 前の FG1 配送、待機中の再判定。peek は非消費。|
|本線 モード|sys/hal/input/legacy_mode.c|焦点・モード変更時の flush。表交換も同じ破棄口を利用。|
|PowerPC USB|sys/hal/input/usb_input.c|S2a が共通 capture を利用。S4 専用の USB 操作表は不要。|
|PowerPC 起動|sys/platform/powermac/input_usb.c|S3m の wrapper 経由で文字なしを確認。|
|本線 GUI 後続 G|userland/gshell/src/input.rs|評価済み意味と焦点世代を使用、旧 raw 再評価を終了。|

S2/S3 は先に両像を単独 build。S4 は宣言、ゲート・ring・解除、共通配送の順に実体を揃え、最後に両入口へ結線して単独リンク可能とする。S4i は S4 に依存し、私有 ops の生成元・再生成・部品 initializer・resident と FG1 の両読取りを一括 build してからサービスを公開する。私有世代変更だけの単独 build は不可。PowerPC USB と後続 G はそれぞれ先行段の上で単独 build する。

|既存検査|代役・covers・変異の追従|
|---|---|
|input_wait_host.c / test_input_wait.py|provider の空・後続キーと M5/IF・取消を独立制御し、covers に wait/try と公開 wrapper。直 pop の変異は共通 try の pop と待機の RECHECK 分岐へ移す。|
|input_m7_1_host.c・input_m7_2_host.c|M5 の失敗・世代再利用を模擬。covers に公開時保存、配送口、未 bind、release・抜去。STOP/ETX の変異は更新後の各分類へ個別に置く。|
|test_ime_fg1.py・fep_module_host.c・test_fep_generator_meta.py|待機の代役に RECHECK、私有 ops の代役に絶対操作を追加。covers に末尾再読、fep_body の登録と生成契約。変異は末尾より先の pop、無条件 toggle、世代の不一致へ置く。|

試験を新設する段では host/guest ドライバごとに一ファイルとし、その CHECK に inputs・basis・変異を同時登録する。
既存の生成 stub・ops 表の配置は実装前に生成契約と照合し、生成物を手で直さない。

## 4. 受入と変異

|場所|操作と観測|単独で RED にする変異|
|---|---|---|
|host 実 capture|PC98 本体と USB の make から STOP/ETX を別々に採取。ローマ字途中・かな前編集・変換中の各 active で通常 FIFO 満杯でも一回取消、次の読取りは空。ON のみなら STOP=6000、ETX=03 を通す。|STOP、ETX の結線を一方ずつ外す。|
|host 世代|公開後に同じ owner 番号を再利用。旧 active で取消なし。command 交代後に旧 ON を捨てる。|exec を読取り時に補う、action の command 比較を外す。|
|host 順序|ON、a、OFF、b と OFF、ON を連続投入。文字・確定末尾・最終モードを検査。通番の周回も通す。|最新目標だけ保存、after 比較を除去。|
|host 容量|8 件満杯、9 件目を計数、独立した取消は成功。flush と再公開で古い操作ゼロ。|最古を上書き、失効 flush を外す。|
|host FG1|同じ ON/OFF 二度、repeat/break、辞書失敗後の新規 ON、IF 無効と peek で副作用ゼロ。|無条件 toggle、成功を固定。|
|host 待機|STOP だけで待機復帰し一度取消、次の打鍵が届く。直接 STOP・負値時も二重取消なし。|取消照会を待機条件から外す。|
|NP21/W|変換中の本体 STOP と Ctrl+C を別試行。未確定表示消去、アプリ生存、次の一字と UTF-8 末尾を照合。|guest の ime-stop-delivery-guest で実 capture の各欠落を検出。|
|NP21/W 対照|複数候補・先頭選択の CONVERT から XFER(3500) 一打で CANDLIST・選択不変、二打目で次候補。GUI は filter→raw、直接 STOP、Shift+Space を確認。物理 Ctrl+STOP、GUI STOP 抑止、V86 退出、RLDN。USB 結線後に LANG1/2 を実入力。|救済を logical に変更、LEGACY を ETX に変更。|
|PPC QEMU|USB usage 0x90/0x91 を二度ずつ、続いて A。A のみ読み出し、peek 非消費と抜去を確認。|操作を互換 sc の cooked に戻す。|

ime-stop-delivery-guest は tools/guest_acceptance.yaml に NP21/W の手動受入として登録し索引を再生成する。STOP 試行と Ctrl+C 試行を個別に記録し、それぞれ未確定表示残存、アプリ終了、次字欠落・重複、UTF-8 末尾不一致のいずれでも当該試行を失敗とする。

各製品段で対象 build、共通変更は両 arch リンク、対象試験の変異と lint。NP21/W は本線統合の受入条件。
build_compare は設計・試験だけなら x86 配布像一致。実装統合では kernel・FEP 部品・pair と格納像の差を期待する。
差は map・生成契約で採取口、dispatcher、私有 ops と派生番地へ帰属させる。無関係な userland の差は調べる。

## 5. 未決

ユーザー判断を要する項目は 0 件。通常中断の有効化、GUI の焦点配送、PPC kboot の到達点は既決の段に従う。

## 確定 (2026-11-21、sol 第 2 回「5 点を反映すれば可」、PM が反映)
以下は本文に優先する。
- **P1 待ちの口**: 待ち・非待ちの共通の内部の口に「取消による再判定を有効にするか」の引数を渡し、FEP の読み取りの時だけ有効にする (公開の wrapper は無効のまま渡す — 取消の印が残って空回りしない)。`input_wait_fg1(mode, interruptible, key)` の内側の口に 1 引数を足す形で、公開の待ちの戻り値は今のまま。
- **P1 非待ちの道**: `ime_trygetkey()` は私有の try に直に結ばず、今の CUI の前景の poll の道 (sys/kern/ime/ime.c の trygetkey の今の入口と sys/platform/pc98/input/kbd.c の前景の poll) と端末の優先の順を保つ位置に置く。排他は取消の照会・due の判定・cooked の pop に限る。
- **P1 未 bind の道**: kbd_merge.c が状態の更新の前に採った押下の状態と、イベントの源・世代を、局所の評価の口へ渡す署名にする (sys/hal/input/in2_int.h を変更の表に加える)。repeat を通常の make と区別して渡す。
- **P2 PowerPC と段の順**: 既にある `input_capture_key` を再利用すると明記し、共通の配送の宣言と実体を、両方の入口の結線より先の段に置く。
- **P2 容量の試験**: 通常の FIFO が満杯の時に取消を採った後は、「取消を 1 回取得 → 先の文字を全部順に読める → 最後に空」を確かめる (「次の読み取りは空」ではない)。
- **S4 の実装の PM 決定 (2026-11-21)**: (1) FEP の部品から常駐側の待ち・非待ちの口を呼ぶため、前景の poll と端末の優先の順を保つ FEP 専用の私有の import を tools/prototypes/fep_imports.def に足し、import の側も再生成する (公開の kbd_getkey_wait・kbd_trygetkey の戻り値は今のまま、FEP の私有の世代は同期して進める)。(2) x86 の USB の LANG1/2 (0x90/0x91) は、今の hid_kbd.c が互換の sc に変えてから配るので S4 では識別しない — 本段の host の試験は既存の input_capture_key を入口とし、実の report からの LANG1/2 は TASK_USB の 1E の結線の後の受入に送る。
- **S4 の受入の時期 (PM 決定 2026-11-21)**: 今の製品のコードは input_context_first_shell() を呼ばない (最初のシェルの結線は INPUT_BIND の U2-x0 の担当) ので、M5 の snapshot が実機では失敗し、STOP・Ctrl+C の専用の取消の道と LANG1/2 の宛先は動かない。S4 は前景を登録した host の試験で受け入れ、実機 (NP21/W) の取消の配送は U2-x0 の統合の後に判定する (それまで製品の振る舞いは S4 の前と同じであることを NP21/W で確かめる)。
- **S4 の公開の直の読み取りと FEP の読み取りが交互の時 (PM 決定 2026-11-21)**: かなの前編集の間に「OFF → b」が届き、公開の input_legacy_wait() が先に OFF を実行した時は、その呼び出しは b を返し、OFF で確定した UTF-8 の末尾は次の FEP の読み取りに残す。公開の直の読み取りは IME を通らない道なので、確定の末尾を先に返す条件は FEP の読み取りの道だけに当てる (公開の wrapper は FEP 部品の私有の ime_delivery_take() を呼ばない)。試験でこの順を固定する。
