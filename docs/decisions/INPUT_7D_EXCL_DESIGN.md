> 出所: 本線の機械の ~/os32-tmp/run/docs-1122-src-7d.md (入力 7d・G1 の確定設計と PM の追記)。
> 状態: **現行 (2026-11-22)** — 入力の段 7d (gshell の独占の対応、AV の G1) の確定設計の追記。末尾の「確定」と「実装で PM が決めたこと」が本文に優先する。

# 入力 7d・G1 設計追記 r2

## r1→r2 の変更

- P1-1　7c の acquire の閉じを A〜C の基点にし、D の機種選択を明記した。
- P1-2　X4 と X3 の直接取込を共通の配送前判定に通す。
- P1-3　前景変更をキーボード・マウス共通のバッチ終了にした。
- P1-4　入力外の窓変更を照合し、同一 owner の raw と退避を掃除する。
- P1-5　非独占を含め、前景交代時の長押しを実 break まで抑える。
- P1-6　旧アプリの消費と X3 の無入力再送を清算中も進める。
- P1-7　観測した境目と清算を終えた境目を別に持つ。
- P1-8　成功した Pointer の状態と旧窓相対座標を release に反映する。
- P1-9　独占中は WM キーの解釈後に全画面注入・FEP・アプリ配送を止める。
- P1-10　切替の開始から確定まで満杯キューに席を確保し、押下区間を区別する。
- P2-1　世代照会・raw 判定の型、添字と IRQ 排他の責任を定めた。
- P2-2　機種ヘッダの選択を実ビルドと公開入口で照合する。
- P2-3　段ごとに登録先、試験、スタブと変異の移設先を定めた。

基準は AV_HAL_DESIGN §5-3・§5-6・G1、INPUT_HAL_DESIGN 段 7d、AV0_V90_ADDENDUM の前景通知の決定。比較位置は bd00dda01。着手基点は公開 acquire を閉じて着地した 7c とする。

## 1. 欠けと採る案

|出所|採る案と理由|
|---|---|
|sol 1 acquire|旧・新フラグの差で解除を検出する。同じ owner の KBD→MOUSE でもキーは WM に戻る。|
|sol 2 ボタン|物理値、解除時の押下マスク、配送成功値を分ける。Pointer の全ビットにも押しっぱなしが出るため。|
|sol 3 操作取消|枠・捕捉・未確定操作を取消し、送信済み CLOSE は有効とする。通常の up はドラッグを確定してしまう。|
|sol 4 カーソル|再合成の前に状態を読み、開始で hide、解除で現在座標に show。静止中にも境目がある。|
|sol 5 満杯|raw と退避で優先置換し、X3 は配送先の空きを調べる前に WM キーを扱う。詰まる箇所が三つある。|
|sol 6 release|成功した押下だけ旧宛先別に記録し、release が入るまで保持する。満杯で捕捉を失うのを防ぐ。|
|sol 7 前景|前景変更でバッチを切り、通知成功・状態再取得・X3 の清算を待つ。周期末通知では宛先と独占がずれる。|
|sol 8 切替確定|切替を確定した raw は消費する。GRPH 無しの代替 raw も新前景へ漏れ得る。|
|sol 9 ABORTED|前景回収は clear の後で解除し、既存の STOP 掃除を通す。境界通知の消失と二重中断を防ぐ。|
|sol 10 PowerPC|「7d の独占の有効化は PC-98 に限る」を採用。PowerMac は USB cooked・raw スタブ・ポインタ直結で両入力の独占を提供できない。|
|実装者 1 解除キー|カーネルが解除瞬間を記録する。WM は独占中に始まった押下を観測できない。|
|実装者 2 GUI 外 break|合算数が最後に 0 になった世代を照合する。raw を作らない間の離しも区別できる。|
|実装者 3 描画口|cursor.rs の show 自体にも門を置く。移動と再合成が同じ描画口に入る。|
|実装者 4 残すキーのみで満杯|TAB の反復を先に退け、STOP 要求と GRPH 離しを残す。次の入力がなくても切替が確定する。|

## 2. カーネル側

- kbd_merge.c が既存の合算 cnt と同じ添字で u32 の離し世代を所有する。input_merge_key と input_capture_key の有効な compat_sc の増減を同じ関数にまとめる。反復・途中の一台の離しでは進めない。切断の疑似離しも通常の減算を通す。
- 既存の int input_key_pressed(u8 sc)（input/hal/kbd_merge.c の input_key_pressed）と、新設の u32 input_key_release_gen(u8 sc)、int in2_excl_drop_raw(u8 sc, int is_break) を内部で使う。新設二口の宣言は input/hal/in2_int.h。添字は 0..127、範囲外は 0 を返し配列に触れない。合算更新側と前景の記録側が irq_save/restore を所有し、照会関数は呼出元の排他内で読む。legacy_raw_push（input/hal/legacy_ring.c の legacy_raw_push）は IRQ 排他内で drop 判定が真なら戻る。in2_excl.c の valid・saved_gen は前景側だけが書く。

```c
/* 合算を更新する既存の IRQ 排他内、old != down の時だけ */
cnt[sc] += down ? 1 : -1;
if (!down && cnt[sc] == 0) ++release_gen[sc];

/* 前景のみ。呼出側が irq_save してから restore まで通して実行 */
transition(owner, next, force_boundary):
    if 同じ所有者・同じフラグ && !force_boundary: return
    if (excl & ~next & IN2_EXCL_KBD) || owner交代による強制境目:
        各通常 sc の valid = input_key_pressed(sc), saved_gen = input_key_release_gen(sc)
    acq_owner = owner; excl = next
    ++gen; kbd_raw_purge_keep(); in2_ring_lost(IN2_LOST_EXCL)
```

- acquire は既存の引数・前景検証後に transition。unacquire と int in2_excl_release(int owner) は所有一致時だけ解除。in2_excl_focus(old) は独占の有無を問わず境界をちょうど一度作る。focus_gen の加算は既存の各成功経路で一度。
- legacy_raw_push(sc, is_break, mods) は STOP・CTRL・GRPH・TAB を通す。通常キーは KBD 独占中は捨て、解除後は make かつ valid かつ世代一致なら捨てる。実 break は通す。IRQ 側から valid を消さない。in2 本体の入力にはこの門を置かない。
- in2_reclaim(owner) は同じ IRQ 排他内で、前景なら clear_input（リング・移動・ホイール）、次に所有一致なら release、前景なら reclaim_generation。非前景の回収で前景入力を消さない。in2_on_focus は clear → LOST{FOCUS} → excl_focus の順で、新前景の先頭を保つ。振動停止は §5-6 に従う。

## 3. gshell 側

|契機|処理順|
|---|---|
|取込・再合成の入口|X4 の userland/gshell/src/pump.rs の gshell_gui_pump、X3 の userland/gshell/src/handler.rs の op_wait、userland/gshell/src/lib.rs の standalone_loop、userland/gshell/src/multiapp.rs の resume_one と通常周期を同じ同期処理に通す。各入口で取込・再合成前に前景 (窓 ID, owner) と in2_wm_state を読む。失敗時は通常配送を閉じる。初回は現状態を基準にし、以降は組または gen の変化ごとに observed を進め、最新 excl を即適用する。|
|X4 Pump|mouse_poll と座標・物理ボタンの観測を続ける。MOUSE 開始時は描画済みカーソルを hide、show の全経路を閉じる。境目の清算、退避の掃除、UI 操作は X3 に渡す。境目待ちでも WM キーを退避できる。|
|X3 境目|observed ごとに退避を安定圧縮し、旧宛先の成功記録を release 待ちに併合する。既に待つ分は保持。ドラッグ枠、未確定の移動・サイズ・閉じる、マウスキー捕捉を取り消す。通常 up を呼ばず、送信済み CLOSE はそのまま。|
|X3 清算中|イベントを新たに配る門だけ閉じ、userland/gshell/src/multiapp.rs の input_ready・resume_one の起床・再開を続けて旧アプリがリングを読めるようにする。各 X3 の入力有無判定前に残りを再送し、満杯なら次周期へ返す。STOP・切替は受理する。|
|X3 再開|通知済み owner と最新前景を照合→状態再取得→未処理の境目を処理→旧宛先全件の再送。途中で STOP・切替・窓消滅があれば observed を再評価し同じ順を繰り返す。再照合で変更なし、通知成功、待ちゼロの時だけ settled = observed として各ビットの配送を再開する。|
|前景変更|userland/gshell/src/wm.rs の set_focus・destroy_window の後も次の配送前に同期済みの窓 ID・owner と照合する。入力内では切替を起こした時点で両デバイス共通の終了結果を最外の poll へ返す（userland/gshell/src/input.rs の capture・capture_mouse）。残りの raw は境目処理へ渡し、同一 poll の未処理マウス端は捨て物理値を比較基準にする。owner が変われば av_focus_set の成功時だけ通知済み値を更新する。|
|同一 owner の別窓|カーネルの focus 通知は省かれる。同期時に旧 pending_raw、次に kbd_trygetrawkey の空までを順に走査し、WM 用だけ順序を保って退避する。通常 sc は kbd_is_pressed で現時点の保持集合を取り、WM 側で該当 make を反復として捨て、実 break でその sc を外す。空の確認と走査の間の新着も次の取込で break→make の順に判定する。通常配送を開く前に掃除は一度だけ行う。owner 交代時の保持・purge は §2 が同じ IRQ 排他内で行う。|
|GRPH+TAB|kbdnav の確定結果を呼出元へ返す。GRPH break または GRPH の落ちた raw はその場で消費し、通常のキー処理に戻さない。|
|ABORTED|discard_stop を維持して raw・退避の STOP と中断要求を既存手順で掃除し、消滅した窓の再送記録を捨てる。|

- EXCL_MOUSE 中は物理とマウスキーの Pointer/Button・UI 操作を止める。解除の最初の観測で held_mask = physical、以後の poll ごとに held_mask &= physical。Pointer.buttons・Button・UI の端は physical & ~held_mask から作り、解除直後の比較基準もこの値に揃える。独占中からの合成押下も解除まで再始動させない。カーソルは現在座標で再表示し、静止時も次の移動を待たない。
- X4 の間に開始・解除が一巡し gen だけ変わった場合も清算し、その時点の押下ボタンを基準化する。新しい実キーの break は対応する配送済み押下のある宛先だけに送る。

|配送の結果|窓 ID・owner ごとの記録と再送|
|---|---|
|make/Button/Pointer の append 成功|キーごとに scan・mods を保存。ボタンごとに旧窓の内容原点と最後に成功した窓相対座標を保存。Pointer の buttons は宛先の最新配送マスクとして反映し、Pointer のみで立ったビットも清算する。失敗時は記録を変えない。|
|再送イベントの組立|旧窓へ KeyUp(scan, ch=0, 保存 mods)、各保持ビットへ ButtonUp(保存相対 x,y, button)。境目で座標を固定し、各 append に旧 slot の serial を使う。Pointer で通知した宛先には同座標の Pointer(buttons=0) も一度送り、その成功待ちを個別に持つ。|
|通常または合成 release 成功|対応する記録だけ消す。既に成功した release は再送しない。|
|release で満杯|記録と旧宛先を保持。X3 ごとに空きを再確認し、成功した分だけ前進。捕捉解除後も宛先を引ける。|
|窓消滅・所有者不一致|その宛先の記録を破棄。再利用されたスロットには送らない。|

raw と pending_raw は、満杯でも境目・独占中の重要入力を読み取る。X4 の通常時の背圧は維持。カーネル raw では境目後の WM 清算状況が見えないため、満杯での重要入力の保護は常時適用する。

```text
空きあり → 末尾へ
満杯で通常入力 → 到着分を捨てる
残す入力 → 末尾側の通常キーを除いて到着分を末尾へ
全件が残すキー → 同じ物理 GRPH make〜break の区間内、同一 TAB 押下中の反復だけ除く
  各キューの書込側で物理 GRPH の立上りに区間を開始、TAB break で反復判定を解除
  最初の GRPH+TAB make 受理時に確定用の空き一枠を予約（容量計算から差し引く）
  必要なら通常キー、同一区間の反復、未保護の修飾入力の順に除去。二枠を得られなければ新規切替を受理しない
確定の GRPH break（または最初の GRPH 無し raw）は予約枠を解放して末尾に積む
  開始が既に読み出された場合も予約は確定まで保持。確定済みの別区間の対は除去しない
  CTRL+STOP make は未保護入力を押し出せる。異なる区間の開始・確定や予約枠を奪わない
同等以下の到着で候補なし → 到着分を捨てる
除去・棄却ごとに dropped 加算。除去は詰めて生き残った順を保持
```

配送先リングにはこの置換を適用しない。X3 は space_ok 判定より先に STOP・切替を解釈し、通常配送にだけ空き判定を使う。 userland/gshell/src/input.rs の capture_keyboard では STOP・GRPH+TAB の WM 解釈直後に KBD 独占を判定し、残す四キーもそこで消費する。全画面 kbd_inject、FEP、通常 TAB 配送のすべてに先行させる。

## 4. 変更単位（各半日）

|順|ファイルと完了点|依存|
|---|---|---|
|A|input/hal の kbd_merge.c・in2_int.h・in2_excl.c・legacy_ring.c・in2_reclaim.c。解除世代、両リングのうちカーネル raw の優先、回収順。|7c の後。B と並列可。|
|B|userland/gshell/src の 新設 excl.rs（同段で userland/gshell/src/lib.rs の mod input に並べて mod excl;）・wm.rs・input.rs。状態取得、observed・settled、配送記録と ring.rs の append 成否接続。|内部状態の形を固定して C へ。|
|C|input.rs・kbdnav.rs・cursor.rs・multiapp.rs・wm.rs。満杯退避、物理同期、取消、再描画、前景通知とバッチ打切り。|B と直列。同じ input.rs を並列編集しない。|
|D|kapi/kapi_in2.c の 7c の閉じを PC-98 構成でだけ開く。PowerMac は閉じた戻り値と状態を維持。各段で追加した host 試験を公開入口から接続。|A〜C の後に直列。公開 ABI・生成欄の変更なし。|

7c HEAD の kapi/kapi_in2.c の in2_acquire・in2_unacquire を取り込んでから A を開始する。A〜C は IN2_PUBLIC_EXCLUSIVE=0。公開 acquire は引数検査前に OS32_ERR_NOSYS を返し、owner・excl・gen・キューに副作用なし。unacquire は既存の所有検査と解除を通す。D で新設の機種別 platform_input_caps.h を共通 KAPI が同名 include し、PC-98 版で 1、PowerMac 版で 0 を宣言する。PowerMac の根拠は platform/powermac/input_usb.c の pm_input_usb_key、platform/powermac/input_boot.c の pressed、platform/powermac/usb_platform.c の usbp_input_key・usbp_input_mouse・usbp_input_release。
## 5. 受入と変異

各行は host で左を観測し、右の変異で実行時 RED を確認する。

|host の入力と期待|変異|
|---|---|
|KBD→MOUSE、unacquire、focus、reclaim の各解除で A の反復なし、実 break と次の make あり|各経路で解除時の記録を飛ばす|
|従来・キーマップ双方で二台の A の片方を離しても抑止、最後を GUI 外で離して戻ると新しい A が通る|各経路の世代加算を省く／途中の離しでも加算|
|IRQ を模した raw 生成で記録の書込み回数は不変、解除の記録時は IRQ 禁止|raw 側で valid を消す／記録を restore 後に移す|
|独占中も in2 に全入力、WM は四つの残すキーを取得、通常キーのアプリ配送なし|raw の門を in2 にも適用／残すキーを抑止／WM 配送を開く|
|物理・マウスキーとも独占中に配送なし。右長押し解除後に左を新規押下し Pointer と UI に左だけ出る|Pointer に物理値を直渡し／合成経路の門を外す|
|ドラッグ途中の境目で枠のみ消え、位置・サイズ不変、送信済み CLOSE は一件|通常 up で終了／CLOSE を再送|
|再合成を同周期に要求し、独占開始で消え、静止解除で現在座標に戻る|取得を再合成後に移す／解除時 show を省く|
|raw と退避を個別に通常キーまたは残すキーで満杯にし、追加打鍵なしで STOP と切替確定。配送リング満杯でも同じ|順に raw 置換を外す／退避満杯で読まず戻る／space_ok を先に判定|
|複数 release の一部だけ成功→満杯→空きで旧窓へ各一回。途中の窓消滅・slot 再利用で誤配送なし|失敗時も消去／成功分も再送／slot だけで照合|
|前景通知一度失敗→成功。途中の X4 と清算中は新窓への配送ゼロ。切替確定 raw も新窓に出ない|失敗時に通知済み更新／X4 で再開／確定後に同じ raw を配送|
|ABORTED の独占回収後に LOST{EXCL}、STOP 処理一回。前景交代の先頭は LOST{FOCUS}|解除後に clear／STOP 掃除を省く／FOCUS と解除を逆順|
|非独占のアプリ・机、KBD_NAV・STOP の既存配送が通る。7c の閉じ、7d PC-98 の公開成功、PowerMac の閉じを入口と状態で確認|常時配送停止／公開を全機種で開く／PC-98 も閉じる|

|追加試験と段ごとの接続|独立に RED にする変異|
|---|---|
|A：test_input_focus.py・input_focus_host.c のソース一覧に合算の実体を接続し、不足する機器口だけスタブ化。test_input_merge.py でも非独占 A 保持→owner 変更→反復なし→実 break→再押下を確認。|tools/tests/test_input_focus.py の run 前後の旧 release・境目の代入、IRQ の文字列を transition の対応する更新へ移す。owner 交代の保持採取だけを外す。|
|B：新設の tools/tests/test_gshell_excl.py と Rust host ハーネスで状態取得・窓リング・append 成否を制御。各入口を個別に通し gen のみ進む境目、失敗 make、Pointer 単独の押下→満杯→旧窓座標の解除を照合。|各入口の同期を一つずつ外す／excl だけ比較／失敗 append も記録／Pointer の記録を省く。|
|C：同ハーネスで非独占 A 保持中の同一 owner の set_focus・破棄、キー切替後のマウス、左端で交代後の右端、全画面 TAB を確認。旧アプリを起床して消費させ、再送途中の切替・ABORTED を経て無入力 X3 で残り各一回。|両デバイスへの終了伝播を外す／同一 owner の掃除を省く／注入後に門を移す／起床を止める／observed 更新で再送表を消す。|
|A・C：raw と退避を別々に満杯にし、通常・同一区間反復・未保護修飾を混在させて除去順を照合。二つの GRPH 区間を積み、開始後に通常入力で埋めても追加打鍵なしで二回順に確定する。|各キューの隣接する保護順位を逆転／確定枠の予約を外す／異なる区間の確定も重複扱いする。|
|D：make kernel と make ppc-ci-image の KAPI コンパイルに使う実際の機種 include パスで各ヘッダの値を照合。同じ選択で input_focus_host.c の公開 acquire を呼び、前景・有効 flags で PC-98 の成功と gen 更新、PPC の NOSYS と全状態不変を観測。A〜C の閉じの試験も維持する。|各機種の設定値を個別に反転する。|

- 実装時に make all、gshell・gui・wm・input・in2・kapi・av・focus と Rust SDK の関連検査を変異付きで実行し、宣言 lint と PPC 共通層のビルドを確認する。
- NP21/W は PC-98 の gshell で二窓を開き、独占側 in2 と両窓の配送記録を照合。A 長押しで解除→反復なし→離して再押下、ドラッグ中の独占→枠取消→静止解除でカーソル復帰、右押下保持中の左クリックを確認する。
- 同じ二窓で GRPH+TAB を離して追加打鍵なしに一度だけ交代、CTRL+STOP で旧アプリだけ終了することを確認。旧窓に配送済みの押下は各一回清算し、新窓と机で通常操作を再開できることを記録する。

## 6. 未決

ユーザー判断を要する項目は 0 件。

## 確定 (2026-11-22、sol 第 2 回の条件付きの承認の 4 件を PM が反映 — 以下は本文に優先する)
- **P1 同じ owner の長押しの抑止が残り続ける**: 保持の集合のキーの最後の break は、取り込みまで保護の項目として扱い (満杯の規則で捨てない)、捨てられ得る経路では kbd_merge の離しの世代 (PM 決定 (2)) で再同期する。受入: 満杯の中で離してすぐ押し直し、最初の新しい make が届く。
- **P1 予約した代わりの確定の raw が境目で消える**: GRPH+TAB の確定は区間に結び付く保護の項目として持ち、kbd_raw_purge_keep と退避の圧縮の両方で残す。受入: 先の区間の前景の交代を挟み、後の区間を代わりの raw で確定する。
- **P1 保護の済んだ切り替えだけで満杯になると CTRL+STOP が落ちる**: raw と退避の両方で、STOP 用の 1 枠を切り替えの受け入れの容量から除き、保留中の同じ切り替えの要求は 1 つに畳む。受入: 切り替えの保護の項目だけで満杯にして、STOP が追加の打鍵なしで処理される。
- **P2 7c の host 用の上書き**: 実体の側の試験は `-DIN2_PUBLIC_EXCLUSIVE=1` で組み、機種の選択の試験は `-D` を外して実際の機種のヘッダだけで公開の入口の結果 (PC-98 は開く、PowerPC は NOSYS) を確かめる。

## 実装で PM が決めたこと (2026-11-22、段 A・B の停止への答え — 上の「確定」に優先する)
- **段 B の呼ぶ口**: gshell は公開の KAPI (in2_wm_state で独占の状態と境目の世代) だけを使う。§2 のカーネルの内部の口 (input_key_release_gen・in2_excl_drop_raw ほか) は段 A のカーネルの中だけが呼ぶ (PM の依頼文の誤り)。
- **同じ owner の窓の交代の長押しの抑止 (確定の第 1 項の読み替え)**: 抑止の集合は WM が持ち、解除は (a) その キーの break を受けた時、または (b) WM の各 poll で kbd_is_pressed がそのキーを離していると返した時。離して押し直すのが 1 回の poll の間に収まる時の取りこぼし (新しい押下が break まで抑えられる) は受け入れる。raw のリングが満杯で通常のキーが捨てられる時の取りこぼしは、今の満杯の振る舞い (通常のキーは捨てる) のまま受け入れる — 「満杯の中で離してすぐ押し直した最初の make が届く」の受入は外し、「次の poll で離されていれば抑止が解ける」を受入にする。カーネルの離しの世代 (PM 決定 (2)) は、独占の解除の時のカーネルの記録 (owner の交代) にだけ使う。容量は 128 席のまま (STOP の 1 枠・確定の保護の項目は「確定」の第 2・3 項のとおり)。
