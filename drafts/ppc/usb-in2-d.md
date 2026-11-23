## r1→r2 の変更

- P3　基点を 801af974b に更新し、既存の起動・入力照合と追加する観測を区別した。
- P2　手順 13 は Q の make と break の配送を個別に待ち、WM の窓別件数を比較する。
- P1　終了時は reclaim 直後の host reader と最終焦点移行後のゲスト reader に別々の期待列を定めた。
- P1　受入像の WM 入口、同期呼出しの親子順、抜去を含む return 前の待機点を定めた。
- P2　ドラッグ枠とクライアント領域の左右 capture を別試行にし、UP 時の最初の失敗を特定した。
- P2　PC-98 ゲストの入口と host 試験を明記し、C probe の収録と変異時の待機条件を定めた。
- 段分け　C の能力 0、D の能力 1 と契約行の同時削除を維持した。

完了の基準の案　7450・7455 の実 gshell で前景 probe と WM の両方が同じ入力を証言し、独占の三つの解除経路が所定の reader に LOST を返し、x86 共通試験と各単独変異が所定の結果になること。

## 1. 実物の確認

照合対象はローカルの origin/main（801af974b）。WM 登録と top-level の同期起動は userland/gshell/src/lib.rs:231–239,288–308、GUI 選択は init/shell_loop.c:9–28、INIT・二窓・列挙・NOSYS・入力照合は userland/ppc/guiprob.c:79–159 にある。この経路を拡張する。

旧 USB 確定設計 §5 本文と実物の食い違いは次の 3 件。末尾の確定事項を優先する。

|件|旧本文の前提|main の実物と今回の扱い|
|---|---|---|
|1|PPC の GUI 提供者と実 gshell のビルドが先行作業|済。tools/accept/ppc_gui.py:87 の READY と同:90 の子窓を用いる。登録を欠落させる対照も同:182 にある。|
|2|KB_ROOT_SHELL で gshell を常駐シェルに上書き|済。tools/accept/ppc_gui.py:169,191 では専用構成と別々の像を使用。旧本文の root 指定を廃する。|
|3|D の 7d を別枝から取り込む|済。input/hal/in2_excl.c:27,42,55,71,88 に遷移・取得・解放・焦点・WM 照会がある。gshell の settle と cancel_capture も main を使う。|

|確認箇所|現在の境界|
|---|---|
|sdk/kapi_target_ppc.py:20–111|不在集合に in2_acquire の行は存在しない。生成器から削除する差分はゼロ。|
|platform/powermac/platform_input_caps.h:4–6|IN2_PUBLIC_EXCLUSIVE の既定は 0。C の負試験を通してから D で 1 にする。|
|docs/decisions/ARCH_COMPAT_CONTRACT.md:206|§9 に未揃えの独占行がある。削除は D の成立と同じ段。|
|tools/accept/ppc_gui.py:91–122|済。A、保持、二窓の切替、クリック解除、ドラッグ終了、次の前景キーを観測済みの台本。|
|tools/ppc/usb_run.py:845–859,906–943|CUI は USB edge と probe 応答を別々に待つ。抜去後に再列挙し再追加マウスを選択する。|
|tools/accept/ppc_gui.py:129–139|済。CUI 復帰後に ESC break を待ってから物理入力し、画面の応答行を照合。|

## 2. 段 C-GUI の受入

- normal の C-GUI 像に載せる。既存 guiprob を前景 probe に拡張し、CUI 専用 in2prob の標準入出力を GUI に持ち込まない。
- PPC_C_GUI=1、IMAGE=kboot-link-sh、実 gshell の c_gui_accept feature、USB trace の既存組を使う。
- P1（常駐選択）では /SYS/SHELL.BIN を通常 shell、/BIN/GSHELL.BIN を gshell と byte 比較する。
- 共通 loop の gui_default が GUI を選び、実能力で prepare(1) が成功し、選択パス SYS_GSHELL_BIN から is_gui が立つことが条件。
- Loading /bin/gshell.bin → READY → 二窓の枠・表題・字形を順に要求する。常駐の直接ジャンプは用いない。
- missing と連続 3 fault の既存対照を残し、どちらも /sys/shell.bin 起動と cooked 応答まで通す。
- P1（KAPI）では登録済み WM から前景の起動操作を実行し、実 exec で /bin/guiprob.bin を子 owner にする。
- 試験 feature は通常の起動処理に窓・実行パスを渡す。ホストの RSH から子を代行起動しない。
- 子の gui_call が WM に届き、作成した窓 P の前景設定が完了してから子が測定開始。背景窓を Q とする。
- register 成功、子の非零 owner、P の窓 ID と前景 owner の対応、WM の in2_wm_state 成功を READY の条件に含める。
- 子は公開 in2_dev_count/info から鍵盤・マウスの dev を記録。イベントは in2_read の正の戻り件数内だけを解析する。
- WM への同期 gui_call を測定の区切りに使う。子が短い一歩を実行して制御を返し、handler で通常入力処理が進められる形にする。
- 同期 handler 内で子の応答を待たない。ホストだけが s.wait で次の入力まで待機する。
- 計測値には試行番号・操作番号・owner・窓 ID を添える。host は送信直前の serial offset 以後だけを読む。
- WM は通常の raw 消費箇所で観測し、別の raw reader を作らない。子は in2 の単一 reader とする。
- C では WM の初期 excl=0、前景 P の acquire(3)=NOSYS、直後も焦点・excl が不変であることを記録する。

|順|host の操作|次の操作を許す条件（各行 s.wait）|
|---|---|---|
|1|冷起動|GUI 選択、READY、二窓、P 前景、列挙完了。C_GUI_POINTER の初期値を保存。|
|2|A make 単独|P が KEY(dev=鍵盤,sc=0x1d,down=1) と pressed=1、WM が同じ raw make を消費。|
|3|押したまま|今回の USB REPEAT を待つ。子は読み続け、break までの KEY 列を累計する。|
|4|A break 単独|P の同じ dev/sc の down=0、pressed=0、WM raw break。P の列は make,break の 2 件だけ。|
|5|相対 (+12,−7)|P の MOVE(dev=マウス,dx=12,dy=−7) と WM の初期座標からの同一変位。|
|6|左押下|P の BUTTON(changed=1,down=1) を読む機会を確保してから WM が焦点クリックを処理。|
|7|焦点確定|Q の前景通知と更新後の表題色を照合。物理 buttons=1、実効=0。|
|8|左解除|新前景の reader が同じ dev の BUTTON(changed=1,down=0) を確認。WM の物理・実効とも 0。|
|9|タイトルを再押下|DRAG START、物理・実効とも 1。|
|10|相対 (+10,+3)|ドラッグ枠が (+10,+3) 移動し、確定矩形は元位置。|
|11|左解除|UP、両 buttons=0。確定矩形の (+10,+3) を照合して保存。|
|12|相対 (+12,−7)|STABLE、ポインタだけが動き窓矩形は保存値と一致。|
|13|A make / break|Q の GUI_EV_KEY(make, Q ID) を待って break を送る。GUI_EV_KEY(break, Q ID) 消費後の通知まで待つ。WM の通常 enqueue 成功数は操作前比で Q が +2、P が +0。|
|13a|下表 C の抜去・再追加|再追加後の MOVE と WM 応答を待ち、probe が DONE を出して成功 return。|
|14|WM が CUI 切替を要求して終了|Loading /sys/shell.bin と CUI ready。単なる WM 終了による再起動を区別。|
|15|RSH を ESC で閉じる|今回の usage=29,down=0 を待ち echo cooked と Enter。画面の独立した cooked 行で完了。|

- P2（make/break）では QMP 成功を受信証拠にしない。make の二者応答と保持期限を満たしてから break を送る。
- P2（前景）ではクリック送信と前景確定を別段にする。同一 owner の二窓でも窓 ID を照合し、D の owner 交代とは分ける。
- C の二窓は同一 probe owner でよい。手順 6 は子が read 後に WM へ制御を返す。7–8 は同じ子が Q を対象に継続する。
- raw は同一物理押下の legacy 反復を許す。反復 KEY が 0 件という判定は子の in2 列に適用する。
- 抜去試行は通常ドラッグ試行後に独立して行う。左保持を子・WM が確認してから device_del、RELEASE と列挙消失を待つ。
- WM が buttons=0、残留ドラッグなしになることを確認。再追加後の公開 dev を取り直し、以前と同値でも許す。
- host は USB の解除→新たな列挙を順序付きで記録し、query-mice の新 index を選び、再度 MOVE が新列挙の dev に届くまで待つ。

## 3. 段 D

公開能力の境目（PowerMac ヘッダの既存ガード内の既定値）。

```diff
-#define IN2_PUBLIC_EXCLUSIVE 0
+#define IN2_PUBLIC_EXCLUSIVE 1
```

- PPC 固有の追加は能力宣言。KAPI は既存の公開入口を通し、共通 owner 検査、IRQ 保護、世代更新、purge と LOST をそのまま使用する。
- 共通 in2_excl.c と製品 gshell の settle / cancel_capture を複製しない。切断時の入力解放は C の通常経路を使う。
- C の NOSYS は C のコミットで固定する。D の通常像では同じ地点の期待を成功・所有・配送抑止まで含む検査に置き換える。
- C の回帰を D で能力 0 に偽装しない。通常入力の行は非取得状態で引き続き実行する。

共通実装の境目（抜粋）。flags の不正値検査は前景判定より先にある。

```c
if (!owner || owner != av_focus_owner()) return OS32_ERR_BUSY;
/* acquire の変更時、IRQ 保護区間内 */
transition(owner, flags, 0);
++focus_gen;
/* transition の末尾 */
kbd_raw_purge_keep();
in2_ring_lost(IN2_LOST_EXCL);
```

- D は異なる実 exec owner の P と Q を使用する。窓 ID だけの交代では owner の BUSY・LOST を検証できない。
- 競合試行は外側 P が取得してから同じ probe を役割 Q で同期起動する。Q は背景で開始し、前景窓を作る前に acquire(3) を呼ぶ。
- Q の開始時点で前景 P、owner=P、excl=3、取得後と同じ gen を確認する。Q が BUSY を記録するまで P は生存する。
- 登録中は av_focus_exec_enter が焦点を変更しない（kernel/av_focus.c）。exec_run は exec_launch(cmdline,0) を通る。同期起動された外側 probe が gui_call で窓を持っても、exec_start の GUI スロットからの再帰起動にはならない。起動失敗・役割違い・owner 不一致は試行失敗とする。

### 試行の入口と完了条件

受入像を生成するときに試行名を gshell の c_gui_accept ブロックの固定引数にする。CPU 間では同じ像を使う。WM が登録後に一度だけ /bin/guiprob.bin と表の引数で exec_run する。入れ子の呼出しでは外側は停止し、内側だけが GUI_OP_WAIT で通常 WM 処理を進める。各 probe は自身の判定完了後にだけ成功を返し、親は子の実終了結果も検査する。最外側成功後に限り既存の quit → switch_cui に進む。

|像・WM が渡す引数|probe の役割と待機|host の s.wait（左から順に、各点で試行 ID を照合）|
|---|---|---|
|C normal / c-input|同一 owner が P/Q の手順 1–13 を終え、Q を保持したまま抜去段へ。再追加したマウスの MOVE まで return を保留。|Q break → 抜去用の新しい左 down と WM 保持 → device_del 後の RELEASE・列挙消失・WM 解放 → device_add 後の列挙 → 新 index を選んで MOVE・WM 変位 → DONE → CUI。|
|D normal / d-input|非独占の C 入力列・抜去を再実行。能力確認の取得と明示解除、境界 LOST の消費を入力列の前に完了する。|取得・解除と初期状態の復帰 → C の各入力待機点 → DONE → CUI。|
|D / explicit|外側 P が P 窓で取得し取得 LOST を読む。同期 exec_run の内側 busy-Q は窓生成前に BUSY を確認して戻る。P が独占配送試験後に明示解除して読む。|P 取得 → Q BUSY と状態不変 → P 再開 → 下表の各 edge と WM 周期完了 → P 解除・初回 read → 通常入力再開 → DONE。|
|D / focus|外側 P が取得・drain。同期起動の内側 focus-Q が自身の窓を生成し通常の前景設定を受ける。P は待機中。Q が初回 read と再開検査を終えて戻る。|P 取得 → WM の Q ID/owner 確定 → Q の先頭 FOCUS, EXCL → 通常入力再開 → DONE。|
|D / exit|外側 Q が生存窓を作り同期起動した exit-P の完了を待つ。内側 P が P 窓で取得・drain して独占のまま終了。復帰した Q は GUI 呼出しで通常 WM の再前景化を進め、Q 照合まで read を保留。|Q 作成 → P 取得 → P 終了 → WM の最終 Q ID/owner と世代 → Q の先頭 FOCUS, EXCL → Q 窓で通常入力再開 → DONE。|

D の各解除行は下記の事前状態を引数で一つ選んで冷起動する。各 DONE の後も CUI と cooked 応答まで待つ。WM の同期 handler は状態を採取して直ちに戻る。

|取得・配送|観測|
|---|---|
|P 前景で acquire(KBD\|MOUSE)|戻り 0、WM state の owner=P、excl=3、gen 更新。P が取得時 LOST_EXCL を drain。|
|背景 Q の acquire(3)|BUSY。直前直後で前景、owner、excl、gen が同一。|
|独占中の A、MOVE、左 down/up|P が各 in2 を順に受信。WM の通常窓への配送カウンタは不変。WM の各処理周期完了で比較。|
|STOP・CTRL・GRPH・TAB|各 make を raw 観測後に break。予約キーは WM まで両 edge が届く。副作用のある組合せを同時送信しない。|

解除の reader 境界（受入の疑似コード、各行を個別冷起動で実行）。

```text
共通: 前景 P → acquire(3) → P が取得時 LOST を drain → 他 reader 停止
明示: P.unacquire()==0 → 前景 P 継続 → P.read が最初
焦点: WM の通常 focus(Q) 完了 → 窓 ID=Q かつ owner=Q を通知 → Q.read が最初
終了: P.exit → reclaim 完了 → 生存中 Q の前景確定 → Q.read が最初
明示・焦点・終了の各ゲスト試行: read の返却件数内に type=IN2_EV_LOST, u.lost.reason=IN2_LOST_EXCL
```

- Q を P の終了前に動かす必要がある焦点試行では、外側を P、内側を Q とし、Q の handler から通常 focus(Q) を完了して Q に戻す。
- この試行で Q 起動時に通常の前景設定が走るならそれを試験対象の遷移とし、重ねて focus を呼ばない。
- 終了試行は外側 Q が P の終了から戻る順で行う。Q の復帰だけで前景完了とみなさず WM の owner 照合を待つ。
- 終了の因果判定は 7d §5 に揃える。exec_finish.inc:163 の reclaim はリングを消してから所有を解除する。tools/tests/input_focus_host.c:100–108 の host reader が直後に LOST_EXCL 一件・次は空、owner=excl=0、gen=5 を検査する。続く exec_leave と WM の Q 前景化では owner 変更ごとに再び clear → LOST_FOCUS → LOST_EXCL。ゲスト Q は最後の変更に対応する先頭二件を累積照合する。終了 release を落としてもこの二件は生成されるので、終了の合格には host とゲストの両方を要求する。
- 事前状態は三種類。タイトル押下では WM の枠あり・元矩形を採取し、取得 settle 後は枠なし・元矩形のままを要求する。左／右 capture はそれぞれ前景クライアント領域で新規押下し、その窓への押下配送成功と対応 capture の成立を先に待つ。どの試行でも取得 settle → 立てた状態の消去 → 独占下で物理 up の順に待つ。
- 各解除後は WM state が excl=0、owner=0、gen 更新、左右 capture が空、synth_buttons=0、ドラッグ枠消去を観測。
- 移動のみで窓が静止し、新たな左押下で通常配送・ドラッグが再開する。終了試行の位置比較には生存する窓を用いる。
- 7450・7455 は同一 D 像で個別冷起動。x86 は同じ前景・予約キー・三解除の期待表を使い、USB trace を合否条件にしない。

## 4. ファイルごとの変更と大きさ

行数は実装の概算。WM の観測は c_gui_accept に収録する。C の guiprob は別ビルドで、PPC_C_GUI=1 の受入像専用の出力先で役割付き版をコンパイルし /bin/guiprob.bin に収録する。

|ファイル|段|差分の目安と責務|
|---|---|---|
|userland/gshell/src/lib.rs、build/ppc-user.mk|C/D|既存 top-level の同期起動に試行引数を渡す。像ごとの出力を分離し、試行名変更で WM を再リンクする。|
|PC-98 受入用ビルド・tools/accept/pc98_in2_gui.py（新設）|D|PC-98 SDK の CRT・リンカで共通役割の guiprob をビルドし、専用ディスクの /bin に配置。試行引数入り feature 版実 gshell を GUI シェルとして起動。NP21/W を試行別に冷起動し、台本は np21w 制御で鍵盤・マウスを送り、ゲストの serial を読む s.wait で D の各段を進める。物理入力は PC-98 の対応キーに変換し、USB 抜去は PPC で実施。|
|userland/ppc/guiprob.c|C|+70–100 行。公開 dev、pressed、in2 列の集計、短い gui_call、抜去・再列挙の応答。|
|userland/gshell/src/input.rs|C|+35–55 行。既存入力処理の raw・配送先・ドラッグ終了を試験 feature から記録。|
|tools/accept/ppc_gui.py|C|+70–100 行。既存 normal を拡張し、二者応答・抜去・復帰の s.wait を追加。|
|platform/powermac/platform_input_caps.h|D|1 行。既定を 1 にする。|
|docs/decisions/ARCH_COMPAT_CONTRACT.md|D|−1 行。§9 の公開 acquire 行を削除。|
|userland/ppc/guiprob.c|D|+75–110 行。同一バイナリを役割引数付きで同期起動し、戻り値・最初の reader を記録。|
|userland/gshell/src/excl.rs|D|+20–35 行。feature 内で実 settle の完了と capture・世代を報告。|
|userland/gshell/src/input.rs|D|+20–35 行。試験操作から通常の focus を呼び完了後に実 owner を報告。|
|tools/accept/ppc_gui.py|D|+80–120 行。CPU・三解除の別試行、予約入力、通常配送差分、変異。|
|tools/tests/test_gshell_excl.py|D|+15–30 行。共有 WM の境界期待を x86 側でも検査。|

## 5. 受入から試験と変異へ

各行は正常像 GREEN の後に一箇所だけ変え、指定した観測で RED を要求する。無関係な起動失敗は無効。

|段・受入|試験|単独変異 → 失敗点|
|---|---|---|
|C・GUI 登録|両 CPU の READY と窓|既存 register 欠落 → READY 不達|
|C・常駐選択|ISO と Loading 履歴|GUI 選択を CUI に固定 → GUI 起動履歴不一致|
|C・鍵盤|P の make/hold/break と WM raw|KEY capture を飛ばす → USB make 済みで P の MAKE 欠落|
|C・ボタン|P の down/up|BUTTON capture を飛ばす → WM 物理押下済みで P の down 欠落|
|C・前景|Q の FRONT KEY|前景更新を抑止 → Q の前景通知欠落|
|C・解除|UP 後の窓位置|ドラッグ終了を飛ばす → UP の (+10,+3) 照合が先に失敗|
|C・切断|保持中に抜去|ボタン解放を飛ばす → WM に保持が残る|
|D・能力|両 CPU で P の取得|能力を 0 に戻す → 成功期待に NOSYS|
|D・競合|背景 Q の要求|前景検査を除く → BUSY 欠落|
|D・配送|予約 TAB の raw|除外から TAB を落とす → 独占中の TAB 欠落|
|D・明示|P が最初に読む|release の LOST 投稿を抑止 → P の LOST_EXCL 欠落|
|D・焦点|生存 P から Q に変更|焦点変更時の独占解除を抑止 → Q 確定後も excl=3|
|D・終了|P 終了直後の解放|終了処理の release を抑止 → 次の焦点処理前に owner=P が残る|
|D・残留|共有 WM 試験と実窓|cancel_capture を飛ばす → settle 後に capture が残る|

- 終了変異は後続の焦点処理が欠陥を覆いうるため、release を外すと host の reclaim 直後の最初の LOST_EXCL 照合で RED とする。
- C と D は順に独立した変更とする。各段で既存 CUI、C-GUI normal/missing/fault、x86 の共有試験を再実行する。
- D の記録には CPU、ELF digest、操作番号、owner と窓 ID、世代、最初の reader、LOST reason、変異の失敗点を残す。

変異ごとに新しい像で冷起動し、直前の正常な前提点を s.wait してから入力する。各操作の送信前 offset を固定し、表の期待値と異なる実測またはその期待通知の期限切れを最初の RED とする。host 変異は同じ箇所の実行時 assertion の失敗で判定する。test_input_focus.py は C host、test_gshell_excl.py は Rust WM host、NP21/W は PC-98 ゲストであり、各結果を別に記録する。

## 6. 未決

ユーザー判断を要する項目は 0 件。実装時の同期順は実 owner と WM 応答の記録で検証する。

反論なし。

## 確定 (2026-11-23、sol 第 2 回の条件付き承認、PM が反映)
本文と食い違う所はこの節を正とする。
- **R2-P1 取得の前後の同期 (§3)**: 全部の解除の試行は共通の前置きで始める — P が前景・入力の準備完了 → ホストが押下 (枠か capture を立てる) → probe が WM の成立を確かめてから in2_acquire → `GUI_OP_WAIT` で WM の settle と消去を確かめる → ホストが物理の up を送る → P が up を消費 → 各解除の道。「P 取得」から始めない。
- **R2-P2 手順 13 の数え方 (§2)**: 数えるのは今回の A の `GUI_EV_KEY` だけ (make が積む `GUI_EV_TEXT` は数えない — userland/gshell/src/input.rs の KEY と TEXT の両方を積む所)。窓 ID 別・edge 別の enqueue の成功の差分で、Q は各 +1、P は各 +0。
- **R2-P2 D normal の初期化 (§3)**: P の初回の前景化で積まれる `FOCUS, EXCL` を空になるまで drain してから始める。取得の直後と明示の解除の直後にそれぞれ `EXCL` を照合し、非独占の settle の完了の後に C の入力の列を始める。
- **R2-P2 残留の変異 (§5)**: `cancel_capture` を飛ばす変異の最初の RED は、タイトルの試行ではドラッグの枠の残留、左右の試行では対応するボタンの capture の残留 (取得の直後に観測)。
