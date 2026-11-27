# SerialFS の絶対期限で保留 CALL を閉じる設計 (確定)

## 0. r3→r4 の変更
- sol-P1-LAN　BUSY 中に保存した CALL・HELLO 応答で、段 0・1・最終像の復帰と次要求を比較する。
- sol-P2-hsync　Ra266 の両経路で 2 回とも実コピーを確認する。
- sol-P2-PPC観測　OPEN 前から client と待機可能な poll の実比較値を通番で追う。
- sol-P2-差し替え枠　第 4・5 枠を make の全経路に通し、両像のコンパイル入力を照合する。
- fable-P2-BYE　seq 更新時の link の BYE 破棄と HELLO 初回受付を小試験に加える。
- PM-(1)　x86 LAN・UART の BYE の送り方を段 0 の模型で固定する。
- PM-(2)　ホストの新規 HELLO 受付から旧 sid 破棄までの根拠と試験条件を示す。
- fable-P3-1　変更表に make 規則を加え、既存 fixture との枠の競合を避け、段 3 に全件実行を見込む。
- fable-P3-2　cancel 変異の old を表末尾の関数の並びに限定する。
- fable-P3-3　製品保証と jiffy 側の失効を諮る条件を決定一覧の行案にする。

本線 sol 第 2 回で §0 の判定は可。DEC のまとめ配達で tick が飛んでも、同じ交換で prev < deadline ≤ end を満たす最初の有効な観測から、別の前景周回を挟まず close する。本版を本線 sol 第 3 回（最終回）に提出する。
対象は bd01609c0。目的は、要求を諦めた client が transport にその要求を残して返る経路を塞ぐこと。

## 1. 実物の確認と統合判断

|観点|コード上の事実|
|---|---|
|設定|sys/fs/serialfs/serialfs.c:59,65–67 で resident の現在 tick に 100 を加える。変異では 50。|
|引き渡し|同 :81,147 と sys/fs/serialfs/sfs_client.c:332–350。個々の request 内では ensure、再接続、call が同じ絶対値を受ける。|
|接続|sys/fs/serialfs/serialfs_persistent.c:125–163。期限到達を調べ、必要なら BYE、HELLO に同じ期限を渡す。|
|再試行|sfs_client.c:228–249。retry は送信ごとに 10 tick 先へ動く。deadline は動かない。|
|抜け道|同 :237–240,251–254。ループ先頭と get 直後の両方で timeouts を加算して HOSTDOWN を返す。|
|保留|sys/platform/powermac/hostfs_link_io.c:139–152。送信を受理するとフレームをコピーし OPEN を予約する。再送は保存済み result を返す。|
|前景|同 :73–79,126–132 と hostfs_boot.c:17–22。待機可能な前景 poll が期限を認識すれば finish が閉じる。|
|待機|sys/kern/idle_work.c:28–49、sys/kern/io_work.c:244–255,315–329。idle の dispatch に互換仕事を登録。client の次の比較との間に tick が進み得る。|
|close|sys/kern/dev/device_host.c:24–27 と sys/net/link_host_api.inc:345–380。platform_host_close(handle, 0) は IRQ 保護下で RELEASE を予約し handle を回収して復帰する。dispatch や応答待ちはない。|
|観測|tests/support/fixtures/ppc_link/rpc.inc:3–31 は OPEN 時の deadline、poll 側の検知、最初の送信を rid で保存。client の return、close の発行元は未記録。|
|判定|tests/system/ppc_usb_lan.py:73–87,114–130 は guest を記録し、合否には host の WDATA–RELEASE 差を使う。|

REPORT との食い違いは 1 件。
- 「READ は入口で取った同じ期限」の単位を訂正する。serialfs.c:282 は先行 ensure、:295 はループ内の各 READ 用に期限を個別に生成する。
- 今回照合する単位は一つの CALL。read 全体を一つの期限に変える変更は含めない。

REPORT の記録（再実行なし）では rid 16 の 7450 は期限 649、検知 649、RELEASE 650。
7455 は期限 663、検知ビットなし、RELEASE 713。WDATA 613 から 100 tick の差は下位の無通信回収と整合する。
7455 が実際にどの tick で client から返ったか、どこで前景が止まったかは、この記録から特定できない。

|統合判断|採用する扱い|
|---|---|
|① client|PC-98 と PowerMac は同じ SfsClient。修正箇所は共通の期限付き交換。UART 用の通常交換は別経路（sfs_client.c:69,137,185）。|
|② cancel の所属|保留を作れるのは transport。共通の SfsIo に任意の通知を末尾追加し、client に link handle や CPU 判定を置かない。|
|③ x86 の影響|UART は通常試行とゲートの契約、82557 は絶対期限と SFS 要求の残留を段 0 で観測する。|

実装・転送口の全数（製品ツリーの SfsIo 定義を検索）。
- PC-98 UART は sys/fs/serialfs/sfs_session_fg.c:97,273 の g_sio。期限付き put もない。
- PC-98 常駐の boot_sfs_clock は kernel_boot_memory.inc:34–36,68–74 の時計専用 SfsIo。末尾は暗黙の 0。
- PC-98 Ethernet は sys/fs/serialfs/sfs_lan.c:103–150。BUSY/AGAIN は関数内で再試行し、TX 受付で返る。
- PC-98 モジュール構成は sys/dev/net/e82557/lan_module.c:81–96 から candidate.io へ転送する。
- PowerMac PLN1 は sys/platform/powermac/hostfs_io.c:79–101。送信の busy を関数内で待つ。
- PowerMac link は hostfs_link_io.c:134–162。上記で唯一、put の成功後も CALL の有限状態機械が進む。

## 2. 直しの形

82557（常駐・モジュール）と PowerMac の期限付き交換の終了処理を sfs_client.c に置く。Ra266 の UART hsync は通常交換を使い、絶対期限の修正対象外とする。

SfsIo の末尾に以下を追加する。buf は呼出中だけ有効。deadline は put_until へ渡した絶対 tick。
```c
void (*cancel)(void *ctx, const u8 *buf, u32 n, u32 deadline);
```
- put_until があり、受理後に仕事を残す実装が提供する。同期型の末尾は 0。
- cancel は非待機、冪等。フレームを新規に送らず、idle や dispatch を呼ばない。
- 同じ前景交換から同期で一度だけ通知する。通知や引数の保存、遅延発行、同じ client/transport への再入は禁止する。
- 共通 client の tx はこの交換のエンコード済みバイト列。呼出元が次の要求で上書きする前に通知する。
- sfs_exchange_deadline の 2 か所の期限切れを下記出口に集約する。sent は既存の送信試行回数。
```c
expired:
    if (sent && c->io->cancel)
        c->io->cancel(c->ctx, c->tx, flen, deadline);
    c->timeouts++;
    return OS32_ERR_HOSTDOWN;
```
- get がバイトを返していても期限到達なら従来どおり採用しない。失敗した put の試行も通知対象でよい。
- put 復帰後は sent を増やし、その時点の now で期限到達なら get 前に expired へ進む。未到達なら同じ now から retry を更新する。
- LAN は BUSY 中の応答を保存し、現行の期限判定前の get がフレーム検証・HELLO の peer 確定まで進み得る（sfs_lan.c:33–54,59–99,130–140、sfs_client.c:243–254）。get 省略による受信状態の差を §6 で比較し、既存契約を保てない場合は証跡を残して別票とする。
- 呼出前に期限切れなら sent は 0。未エンコードの tx や前回の CALL を使って cancel しない。
- 片道版 sfs_client_oneway_deadline の :220–221 も、put 試行後の TIMEOUT 出口で同じ通知を行う。戻り値は TIMEOUT。
- 同 :214 の送信前 timeout、:307 の call 入口には通知を追加しない。通常成功した片道送信は従来どおり poll が進める。
- 片道 put の明示 TIMEOUT は transport 独自の短い上限でも通知する。期限比較との両方が成立しても通知は一度。
- seq 上限更新では ensure が成功した BYE の所有者となる。片道呼出内の TIMEOUT はそこで通知済みなので所有権は戻らない。
- BYE 成功時には c->tx がまだ BYE を保持する境界で、同じ長さ・期限で任意 cancel を同期発行して所有を終える。BYE は best effort、未完なら破棄でき、完了済みなら空振りする。
- この解放を次の時計判定・nonce 準備・HELLO のエンコードより前に置く。受付直後の期限跨ぎ、HELLO の送信前終了や最初の put 拒否でも旧 BYE は残らない。HELLO の受付後はその交換が自分のフレームを所有する。
- hostfs_link_io.c:139–152 の put は OPEN を予約して返す。現行は BYE pending 中の HELLO put が拒否され idle の poll が BYE を送る。変更後は間に poll がなく、seq 更新の BYE は必ず OPEN 前に消えて送られない。serialfs_persistent.c:144–146,161–163 のとおり新 nonce の HELLO で更新する。
- ホストは同じ世代の SfsLink を再利用して CALL を渡す（tools/host_agent.py:1050–1056,1066）。同じ Server を持つアダプタ（tools/host_sfs_link.py:30–31,48）は、新 nonce の HELLO を Server に渡し、要求履歴を初期化する（tools/lanfs_host.py:59–79）。tools/serialfs_host.py:696–700,731–755 は BYE を経ず旧 sid を closed に入れ、それと異なる非ゼロ sid を発行する。旧 sid の CALL は tools/lanfs_host.py:85–86、tools/serialfs_host.py:717–718 で無応答となる。

PowerMac link は tx、tx_len、until、handle、pending、result の既存状態を所有する。
put_until の長さ・全バイト一致判定を同ファイル内の same_frame に抽出して共有する。
```c
if (!pending || deadline != until || !same_frame(buf, n))
    return;
finish(OS32_ERR_TIMEOUT);
```
- ポインタ一致や seq 単独では照合しない。HELLO の nonce も比較に含まれる。
- OPEN 待ちなら handle は -1。finish は予約だけを消す。以後 poll が OPEN することはない。
- WRITE/STATUS/READ 待ちなら現行 finish（:64–70）で platform_host_close(handle, 0) を一度発行する。
- 終了後は handle=-1、pending=0、result=TIMEOUT。tx と tx_len を保持する。
- 同じフレームの再送は保存結果を返す。別フレームだけが OPEN に移る。別期限からの古い cancel は無効。
- 期限が有効な同一フレームの put は期限引数だけ変えても既存 result を返し、保存期限・照合対象を更新しない。新しい HELLO は persistent 側で失敗後も nonce を進めることが前提。
- 先に poll が終了した場合は pending=0 で戻る。成功済み受信バッファも消去しない。
- 協調切替の前景の中で close する。最後の poll の追加だけでは can_wait 判定に依存するので、直接 finish を使う。
- close 発行、リンクの RELEASE 送信、相手側の受信は別の時点。client の出口が保証するのは close 発行まで。
- PLN1 と x86 の同期表は cancel=0。段 0 で x86 に期限超過または SFS 固有の残留があれば証跡を保存し、この直しを止めて別票にする。
- lan_module.h:11,28 の内部 ops は SfsIo を値で含む。版を 3→4 とし、本体とモジュールを同時に再生成する。
- tools/module_schema.py:173–181 の LAN ops_version、生成ヘッダ、lan_module.h、本体 policy、lan.mod を版 4 に揃える。module_image.c:137–138 の policy/component 照合と lan_module.c の版照合を通す。4/4 のロード成功、3/4・4/3 の拒否を確認し、Ra266 HDD に同じビルドの本体と lan.mod を配布する。

## 3. 検査と変異の判定

sol 案 2 を採用する。開始、設定期限、終了の理由・時刻を guest の同一 rid に結び付ける。

|記録点|fixture だけで記録する値と条件|
|---|---|
|期限生成|serialfs.c:67 の now を一度だけ読み start として保存し、その値から本物の deadline を計算する。|
|put 受理|新規 pending のとき、現行 request の start と deadline を退避。再送で上書きしない。|
|OPEN 成功|既存 rpc_opened に start も渡し、handle から full wire rid を取得。16 スロットをタグ付きで初期化する。|
|client timeout|cancel 呼出直前に fixture で通知を観測。OPEN との対応を保存したフレーム・期限で照合し、最初の timeout tick を記録。|
|HFS 終了|poll の期限分岐と cancel 一致分岐から、finish の close 発行位置へ理由 POLL または CLIENT を渡して記録する（fixture 内）。|
|下位失効|無通信による STALE 遷移点から同じ rid に IDLE を記録。上位が後で閉じても最初の終了理由・tick を上書きしない。|
|伝送|既存 WDATA/RELEASE の受理 tick と host の gate を維持。tick=0 も有効なので有無は seen で表す。|

- 生成時に交換の記録番号を払い出し、HELLO/BYE/CALL の put 前に ctx・長さ・全バイト・期限を対応付ける。新規受付でのみその対応を保存する。
- OPEN 時に得た full rid をその受付の記録にも保存する。POLL が先に handle を解放しても client の期限判定と HOSTDOWN return の記録は保存済み対応を引く。
- 対象 rid の前景 poll 入口で到達 tick・周回番号・can_wait、client の全期限比較で実際の now と直前値を採る。can_wait による早期 return も残す。
- OPEN 前の client 比較も交換の受付記録から full rid に引き継ぐ。prev は同じ交換の client 比較と待機可能な poll の期限比較を共通通番で並べた直前の実比較値。end は最初に期限に達した比較に用いた値をその場で保存する（hostfs_link_io.c:73–79）。入口 tick は IRQ でずれ得る。can_wait=0 の入口は原因診断専用で比較列から除く。
- 事象ごとに共通の通番を一つ進め、最初の期限観測・timeout・close 発行・client return の番号と tick を保存する。close は発行ごとに数える。
- QMP は対象 cat の HOSTDOWN 完了と RELEASE の両方の後に読む。16 件を超えてタグが衝突したら計測失敗とする。
- python のフィールド順・stride は一つの定義から導く。現在の固定 24 byte（ppc_usb_lan.py:78）を更新する。
- 必須条件は full rid 一致、OPEN、WDATA、終了、RELEASE の各ビット。POLL/CLIENT 以外の終了は不合格。
- 差は (b-a) & 0xffffffff。比較範囲を半周未満とし wrap をまたげるようにする。
- 正常像では deadline-start == 100。変異期待値を製品マクロから取り込まない。
- 最初の now ≥ deadline を観測した前景周回で閉じる。直前の有効な観測 prev と最初の観測 end に prev < deadline ≤ end を wrap 対応で要求し、その周回の終了前に close を発行する。client 側の直前 now も保存する。
- CLIENT は timeout < close < client return を通番で判定し、timeout と close の間に別の前景周回がないことを要求する。POLL は最初の有効な poll 観測で close、後続の client 通知を含め close 回数は両経路とも 1。
- 超過時は期限前後の poll 到達・can_wait・client の最初の比較を突き合わせる。前景不在、到達したが待機不可、有効な観測後の閉じ遅れを識別し、証跡が欠ければ計測失敗とする。
- 既存 host 差 80〜200、単独 HOSTDOWN、USB の進行、再 cat、抜差し、再列挙も正常像の必須条件。
- rpc_ticks は start からの設定幅 50 を直接観測する。両 CPU が同じ式を実行するので、100 tick の下位回収で 50 を隠せない。
- QMP 読み出し直後に対象 full rid と start/deadline の有効性を確かめて幅を最初に分類する。幅 50 の専用目印は「suppressed CALL configured for 50 ticks」。
- 終了理由・遅延・USB 結果も診断に残す。後続の負荷依存の合否で確定した幅 50 の目印を隠さない。起動失敗や対象の幅を読めない場合は目印を返さない。
- verify は 7450 と 7455 の両方が同じ専用目印を返した場合だけその目印を印字する。
- 共通 client の変異は一つ。共通出口の cancel 呼出だけを除去する。実機の poll が偶然先行しても必ず検出できる場所に登録する。
- x86 の決定的な検査に mock SfsIo を使い、実 client の hello/call 入口から put 成功、応答なしを作る。
- now は整数の仮想時計。ケース A は idle で期限へ進める。ケース B は get 中に進め、双方で return 前に通知を 1 回要求する。
- mock は ctx、長さ、全バイト、期限を保存し、cancel 時に一致を検証して pending を消す。
- 変異像では HOSTDOWN 後も mock.pending が真。専用目印「expired exchange left pending CALL」で失敗する。
- 同期 transport の cancel=0 でも同じ期限・戻り値・timeouts。送信前期限切れ、正常応答、wrap も同一 harness で確認。
- PPC fixture の小試験では別フレーム、別期限、重複 cancel、OPEN 前、close 後の同一再送と次フレームを注入する。
- 片道版は put 中に期限を跨ぐケースで TIMEOUT と通知を確認する。
- put で期限に達する交換は get 呼出 0、通知 1。片道の早い明示 TIMEOUT は now < deadline、通知 1 とする。
- seq 上限を超えた ensure で BYE 受付成功後、片道の成功復帰と HELLO の間に時計を進める。HOSTDOWN 復帰時の BYE 保留 0、HELLO 受付 0、次の ensure 成功を検査する。
- 同じ seq 上限の通常更新では BYE の WDATA が 0 件、HELLO は最初の put で受理されること。ホスト実装に旧 sid を確立し、BYE 無しで新 nonce の HELLO を通す。旧 sid が closed に入り旧 CALL は無応答、新 sid の CALL は成功することを照合する。
- 段 2 の新規登録 ID は ppc_hfs_cancel。tests/support/fixtures/ppc_link/ に小試験を置き、link-sh の試験入口から実 pm_hostfs_io と制御した下位応答を駆動する。
- poll を進めず pending を作り、表経由の cancel の直後に残留を検査する。別期限の同一 put が元の期限・結果を保つことも確認する。
- ppc_hfs_cancel --mutate では表末尾の並びを old（pm_hfs_put_until, pm_hfs_cancel）とし、一致 1 件を確認して pm_hfs_put_until, 0 に置換する変異を一つだけ置き、通知なしでも検査を続け「hostfs cancel left pending」で検出する。

## 4. 像ごとの大きさ（32 bit、実装前の概算）

|像|見込み|実装時の確認|
|---|---|---|
|PC-98 常駐 kernel|交換・片道の条件分岐と間接呼出で text 数十〜百数十 B。実体化する SfsIo ごとに 4 B。|同じ設定の map/size の差分。|
|PC-98 LAN モジュール|埋込み io も 4 B 増。表のコピーと初期化コードはリンク後に測る。|版 4 のペアのロードと旧版の拒否。|
|PowerMac link-sh|共通分＋cancel/照合で text 数十〜百数十 B 増。表 4 B、製品の追加フレーム領域 0 B。|同じ link-sh の map と BSS。|
|PowerMac PLN1|共通分と表 4 B。追加 handle・バッファ 0。|PLN1 構成でリンク。|
|検査像のみ|追加 u32 一個につき 16 スロットで 64 B。start/end/reason/client_tick の四語なら 256 B。|補助状態、コードと trace のシンボルサイズを別計上。|

- FD 上の実増はセクタ配置・圧縮に依存する。実装前に空き容量の数値は断定できない。
- PC-98 起動 FD は前後の最終イメージ生成で常駐部・モジュールの格納量と既存の容量判定を確認する。
- kdebug 本体と import 枠への追加は 0。共有する媒体・メモリ配置の余裕は既存上限のまま実測する。

## 5. ファイル別の変更と検査の固定値

|ファイル|担当する変更|
|---|---|
|sys/fs/serialfs/sfs_client.h|SfsIo 末尾の任意 cancel と所有権契約。|
|sys/fs/serialfs/sfs_client.c|期限付き送信済みフレームの通知と交換終了の一本化。|
|sys/platform/powermac/hostfs_link_io.c|同一性照合、保留 CALL 終了、表への登録。|
|sys/fs/serialfs/sfs_lan.c、sfs_session_fg.c|各同期表の末尾を 0 にする。|
|sys/platform/powermac/hostfs_io.c|PLN1 の表の末尾を 0 にする。|
|sys/dev/net/e82557/lan_module.h、lan_module.c|内部 ops 版更新と常駐側同期表の初期化。|
|tools/module_schema.py・対応生成ヘッダ|LAN 内部 ops 版を本体とモジュールの生成入力まで同期する。|
|serialfs_persistent.c|seq 更新時に受理済み BYE の所有を HELLO の前で終える。|
|tests/support/fixtures/ppc_link/rpc.inc|タグ付き CALL 計測と最初の終了理由。|
|tests/support/ppc_fixture.py:131–152|一時コピーへの計測挿入。置換対象は各 1 回と検証する。|
|fixture ビルドの差し替え枠|既存の link.c・hostfs_link_io.c・exec.c の 3 枠を保持し、serialfs.c と sfs_client.c の写しに第 4・第 5 枠を追加する。|
|build/targets/ppc-powermac.mk|第 4・5 枠の SRC/FILE を再帰 make で解除（:155）、kb_src で選択（:259–261）、C・アセンブラの依存に追加（:299–302）、検査の --mutant に両組を渡す（:336）。|
|写しの生成順|registry の変異を先に適用し、その有効ソースを読んで計測を挿入する。各翻訳単位の差し替えは一つとし、製品に #ifdef を足さない。|
|serialfs.c / sfs_client.c の写し|期限生成時の start、各 put のフレーム、各時計比較、cancel 直前、交換 return 直前を記録する。|
|hostfs_link_io.c / link.c の写し|受付から OPEN の full rid、poll と finish の close、下位 STALE・WDATA・RELEASE を紐付ける。|
|tests/system/ppc_usb_lan.py:73–87,114–130|同一 rid の直接照合、CPU 別結果と共通目印。|
|tests/registry.toml:565–574|ppc_usb_lan の sources、expect、rpc_ticks の fail を更新。link-sh、after_build、100u→50u を維持。|
|docs/design/tools/checks/tests_v2.md:110–118|直接判定、既存 USB 境界、新目印。|
|新規 x86 client 検査ソース・registry 項目|実 client＋時計 mock の独立検査 sfs_deadline_cancel。cancel 除去変異をここに 1 本登録。|

- PPC の fixture には client・transport・下位失効の挿入先を registry の sources に列挙し、失効遷移そのものを観測する。
- ATA など既存 3 枠を使う別 fixture と同じ像に重ねない。通常像・rpc_ticks 像それぞれで serialfs.c のコンパイル入力が計測入りの写しであり、その写しの設定幅が順に 100・50 であることを確認する。
- serialfs.c:59 の製品定数、再送 10、下位無通信 100、USB 上限 200、各 CPU 180 秒は固定する。
- 製品 serialfs.c、idle_work.c、io_work.c、hostfs_boot.c は調査対象。製品の期限生成・dispatch に編集は不要。

## 6. 実装・検証の段取り

|段|持ち主・工数|完了条件|
|---|---|---|
|0|本線 x86、レビュー sol|製品コード修正前に下記のホスト試験を実行し、3 構成それぞれの期限・残留の結果を記録する。|
|1|本線 x86、半日、レビュー sol|共通口・版更新・同期表・mock を一緒に実装。PC-98 像と LAN モジュール生成、サイズ比較、通常と cancel 除去変異を通す。|
|2|PowerPC、半日、レビュー fable・本線 sol|link の cancel とフレーム保持を実装。link-sh と PLN1 のビルド、状態遷移の小試験。|
|3|PowerPC、半日〜1 日＋ビルド規則変更で選択される全件検査 1 回分、レビュー fable・本線 sol|共通部・link 修正後に ppc_usb_lan の rpc_ticks 変異を再登録。両 CPU の通常像・rpc_ticks を同一条件で走らせ、rid 単位の証跡、USB、復旧を確認する。|
|4|本線、半日、レビュー sol・取り込み本線|最終差分で影響検査・変更した登録検査の --mutate を再実行。起動 FD と kdebug のサイズ判定を添付する。|

段 0 は UART・82557 に装置模型を各一つ置く。常駐とモジュールは同じ 82557 模型と入力列を使い、製品の接続表を切り替える。
- UART は sfs_session_fg.c:273–282 の通常 HELLO と serialfs.c:79–85 の通常 call から実ゲートを通す。無応答時は put 成功後に各試行の時計が始まり、再試行数・quiesce・最終エラーが既存契約に一致することを測る。
- UART の TX 停滞ではゲートの送信結果と試行回数、解放後に次のセッションがゲートを取得して転送に成功することを確認する。常駐 LAN の 100 tick を UART に課さない。
- 82557 は双方で sfs_lan_io → net_tx → 実ドライバと送信後 dispatch を通す。モジュールは ops 転送も通す。装置レジスタ・DMA 完了・割込み時刻を模型が与え、実 tick 経路を進める。
- 常駐側は boot_sfs_clock.now と idle の _halt を通して次の時計割込みで再開する。BUSY/AGAIN、期限際の受付、遅延応答と異なる次要求を注入する。
- 段 0・段 1・最終像で常駐とモジュールの双方に、CALL と HELLO それぞれの正当な応答を BUSY/AGAIN 中に rx に格納してから期限に達する入力を与える。get 回数、rx_pos/rx_end、prepared、linked・peer と復帰値を比較する。期限切れの交換が旧応答を採用せず、次の別 CALL・新 nonce の HELLO が自分の応答で成功すること。誤った peer 確定も失敗とし、契約を保てなければ §2 の別票条件を適用する。
- x86 の BYE は LAN・UART とも従来の送り方を維持する。常駐 LAN は段 0 の模型で通常の seq 更新時の BYE の送信回数・順序・結果を記録し、段 1・最終像で一致させる。sfs_lan は net_tx の TX 受付で同期的に返り（sfs_lan.c:132–134）、cancel=0 なので受付済み BYE の送信は変わらない。
- UART の通常の `sfs run` は seq 枯渇で SFS_DEAD_SEQ と OS32_ERR_IO を返し、BYE は sfs_finish が送る（sfs_client.c:174–179、sfs_session_fg.c:136–143,273–282）。段 0・段 1・最終像の UART 模型では、通常終了時のゲート内 BYE の送信回数・順序・結果を採取し、seq 枯渇時も従来の失敗と終了時 BYE を照合する。

82557 では設定期限・復帰 tick・戻り値と、要求別の送信受付・DMA 完了・再発行を記録する。
受付済み TX_ISSUED の遅い DMA 完了は許す。期限後の SFS 再送・保留・受信待ち再開は 0、次要求は古い応答を採用せず成功すること。正常な DMA を止めて帳尻を合わせない。
段 0 と同じ入力を段 1 と最終像で再実行する。共通出口の通知回数・cancel=0 の同値性は sfs_deadline_cancel に集約する。
段 2 は python3 -B -m tests.infra.ci run --check ppc_hfs_cancel --mutate で通常試験と表の変異を実行する。
段 4 の登録 ID は sfs_deadline_cancel、ppc_hfs_cancel、ppc_usb_lan（各 --mutate）、ppc_link_session、ppc_usb_lanq、ppc_link_overflow、ppc_common_compile、x86_media_build、ppc_media_build、src_owner、build_sources、include_layers、link_layers、test_registration。
Ra266 は版 4 の組を入れた HDD で起動し、LAN と UART（通常の sfs run hsync）のそれぞれで、既知内容を同じ接続から 2 回転送する。各回は別の新規宛先を使い、実コピーの記録、終了コード 0、転送先の全バイト一致を確認する（hsync_compare.md:30–32 の unchanged を避ける）。

- 設計のみの本段ではビルド、起動、登録検査、変異実行は未実施。表の数値は測定値と区別する。

## 7. 未決（ユーザー判断 0 件）

PM 決定に従い共通 cancel を先に実装する。前景不在が同一 rid の記録で実測された時点で、jiffy 側の失効を別段としてユーザーに諮る。

票名案「SerialFS 絶対期限での保留 CALL 回収」の決定一覧に加える行案：

|製品保証|同じ交換で期限到達を最初に観測できた前景周回で close を発行する。同一 rid の記録で前景不在を実測した時点で、jiffy 側の失効を別段としてユーザーに諮る。|

反論 1 件（sol-P2-前景不在）：end_tick == deadline は DEC のまとめ配達で正しい終了を落とすため、§0 の PM 決定に従い、最初の前景観測と close の順序で判定する。


## 確定 (2026-11-27)
- 本線 sol 第 3 回 (最終) で条件付きで実装可。残り 1 件 (P2・§6 UART の BYE: 通常の seq 更新時の採取は常駐 LAN に、UART は通常終了時のゲート内 BYE と seq 枯渇時の従来の失敗・終了時 BYE を照合) を PM が §6 に反映し、本線 PM (os32pm) の判断で確定。
- fable reviewer は r3 で確定可 (P2 片道 BYE と P3 3 件は r4 で反映)。
- 段 0・1 (本線 x86 の持ち物) は os32pm の了承で PowerPC 側の astra が起こし、本線 sol のレビュー、取り込みは本線。
