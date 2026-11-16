# PPC_P5_REST_DESIGN — PowerPC P5 の残り: 台本 5 本の link 化と、既定を link に切り替える段

> 状態: **現行 (2026-11-16)** — 確定設計の写し。sol 起草 r1・r2、Opus 5.5 の独立レビュー 2 回、PowerPC PM が第 2 回の N1〜N4 を反映。ユーザー決定 2 件は §6。出所は ~/os32-tmp/run/ppc-p5-rest/DESIGN.final.md。段の状態は票 TASK_PPC_LAN が持つ。上位の確定設計は [LAN_OPERATOR_DESIGN](../network/LAN_OPERATOR_DESIGN.md) §5 (P5 の行)。


r1→r2 の変更
- F1: K は link_run.accept / link_diag_run.Diagnostics を土台にし、BootLink 新設を削除。
- F2: 各台本の start・QEMU 引数・時計を明示し、USB は mouse のみ起動後に kbd を追加。
- F3: 実物確認済みの呼出元表と、各1ファイルの準備段を固定。CI は旧0/新1を明示で両方構築。
- F4: R を ETH_P_ALL の受け身採取と常駐 agent の Host client に分離。
- F5: nest の終了値は直後の RSH `echo $?` で5を照合。
- F6: 台本別に像の作り方と cmd 実装を明記。
- F7: R は C 後、C/U は公開名維持を完了条件とする。
- F8: D に PPC-LINK-G4 の延期行を追加。G4 と cp 速度の判断2件を未決へ。
- F9: 初期 LOG は再送待ちでなく起動前購読。
- F10: VLAN 解析を削除し、QEMU TX0 は frames.log を再利用。

## 1. 実物の確認
- 対象: b00386eb の指定作業木。読み取りだけで調査し、実装・起動・配備はしていない。
- POLICY_DEV §1①: 同じ役目は既存 link_run.accept / link_diag_run.Diagnostics、HostAgent / 共通 link、link_sh_run.Session、sd_run.link_accept にある。PLN1 の再実装はしない。
- 同②: 装置・HAL・ゲスト API は変更しない。既存の共通 link/SFS/RSH を使い、QEMU/AF_PACKET のホスト側入出力だけ分ける。新しい PPC 専用 HAL は不要。
- 同③: LAN_OPERATOR_DESIGN §5 の P5-t 利用、各1ファイル、節目後の既定切替と一致。協調切替・既存 CLI/FS 契約を保持する。
- 互換契約 ID の追加は本段に含めない。既存バイナリの動作比較を必須とし、未閲覧の契約 ID は推測しない。

|台本|現在 PLN1 で観測するもの / 根拠 (行番号)|link 化の留意点|
|---|---|---|
|kboot_run.py|:91–140 LAN STATE、serial と capture の一致、drop、status/clock/memory/read-diag/peek。:151–172 pm_lan_poll の有無で LAN を判定。:225–282 READY 後の TB/EE/100Hz|OF・READY は QMP/serial の独立観測として保持。PLN1 の文面を link に要求しない|
|realhw_run.py|:27–45 nest/shell/exact の端末記録、gap/timeout、boot beacon。:120–151 Packet→Host→Terminal、stdin OPEN|QEMU を起動せず実機記録を評価。画像 SHA・機種・カメラ・pcap を保存 (:152–164)|
|sc_run.py|:37–117 OPEN 後の端末、N01 完全一致、遅延起動・再接続行取消。:129–162 KBOOT_SHELL の nest 二重結果。:345–366 USER sh/serial 回帰|入れ子検証と本番 sh の検証を分ける。旧 prompt/OPEN は RSH と同一ではない|
|ppc_sh_output.py|:74–77 hello/H/fd1 が端末、kernel READY が serial。:104–128 stream mirror 除去の私用像、KBOOT_SHELL=1|RSH OUT と LOG を別々に採取する。serial だけで fd1 を合格にしない|
|usb_run.py|:483–553 sd_run.Session の FS_REQ 応答抑止、RPC エラー後の USB、OPEN による行取消、抜去・復帰|:515–523 は実時間 hold、:583–587 は --lan で normal だけ。通常 product の key (:375–390) と違う|

設計表と現物の食い違いは次の **4 件** (移行済み部分そのものは不具合と数えない)。
1. 設計 :217 の P5-k/r は host の差替えに見えるが、kboot は PLN1 固有の診断形式と像判定にも依存する。
2. 設計 :218 の sc/sh は共通 transport だけでは成立しない。現物の KBOOT_SHELL=1 は link と併用禁止 (mk:16–18)。
3. 設計 :219 の hostfs_io/peer の置換という表記と異なり、現物は hostfs_link_io/peer を選択する (mk:141–147)。既存ファイルの再置換は不要。
4. 設計 :220 の USB 移行は transport 差替えだけでは足りない。--lan は旧 Session・壁時計打鍵で、--mutate を指定しても normal のみ。

## 2. 台本ごとの link 化
### 再利用境界 (以下の関数は新設案。既存 API と混同しない)
- P5-t の共通 agent client を CLI/実機接続の入口にする。台本から別の wire codec を作らない。
- QEMU の HostAgent はプロセス内。K は link_run.accept を丸ごと呼ぶ (pump/word は accept() の中の関数で外から再利用できない、link_run.py:118-136)。C/O/U は Session.pump/word を再利用する。start は下表の条件へ各台本内で上書きし、無条件には共用しない。
- Session.new_agent の RSH イベント採取を踏襲し、LOG と SFS の採取を局所 subclass で追加する。
- Session.cmd は p3_end_count/g_capture 等を要求する (:228–253)。製品像へそのまま使わない。
- 計測付き像では link.settings(work) を利用する。未加工の入力 ELF はローカル cmd 実装で operator の結果だけ待つ。
- sd_run.link_accept の LinkSession は関数内クラス (:288)。直接 import できない。丸ごと複製せず、new_agent の SFS backend/drop 境界だけ局所実装する。
```python
# 各変更対象ファイル内。共通ファイルの変更を前提にしない。
def rsh_command(session, line, expected):
    # operator.submit → pump → 同世代・同 ID の OUT/result を期限内で照合
    # Session.cmd の私用シンボルには依存しない
    ...

class UsbLink(Session):
    def new_agent(self):
        # sd_run.link_accept の file_root/allow_write 設定を踏襲
        # super の RSH 採取を維持、_service_sfs の入口で CALL を記録・応答抑止
        ...

def assess_real_link(events, evidence):
    # LOG/RSH/SFS、世代、完結性、画像 identity を評価。Session.start は呼ばない
    ...
```
|台本|像の作り方 / cmd の実装|start・QEMU 引数・時計|
|---|---|---|
|K|指定 --elf を無改変、診断は Diagnostics の operator.submit('diag')。RSH cmd は使わない|正像は link_run.accept を丸ごと呼ぶ (QEMU の引数は link_run の固定の `-g 800x600x8 -display none` のまま)。--lan-off と negative/旧像は kboot_run の既存の経路 (-nic none 等)|
|R|配備済み製品像と SHA。Host client から RSH submit/result を照合、Session.cmd 不使用|QEMU/start 無し。常駐 agent のみ利用|
|C|link_sh_run.settings(work) の計測付き link-SH に nest 等を追加、Session.cmd を利用。旧serial像は明示0|局所 start は link_sh の -g・USB kbd・USB_QEMU_CLOCK を明示保持|
|O|製品 link-SH (SH=1/LINK=1) に outprobe、必要な変異だけ付与。局所 rsh_command、Session.cmd 不使用|局所 start は -nographic、USB無し、icount無し、sungem 接続|
|U|製品の link-SH (KBOOT_SH=1 KBOOT_LINK=1) と局所の rsh_command (O と同じ形)。差し替え口 (KB_MUT・EXTRA・THIRD の 3 つ、build/ppc-kboot.mk:189) は変異の 1〜2 本だけに使い、観測の差し込みには使わない (PM 決定 N2)|局所 start は -g、USB_QEMU_CLOCK (icount)、起動時 usb-mouse のみ。列挙待ち後 device_add usb-kbd,id=kbd|
### kboot_run (変更 ≤250 行)
- --link を追加。切替までは明示選択、最終的に入力 ELF の link 固有シンボルと指定値を照合し、未知像を黙って PLN1 扱いしない。
- --elf の像は再ビルド・改変禁止。正像は link_run.accept / link_diag_run.Diagnostics、negative/旧像は既存経路へ振り分ける。K が加えるのは ELF/map/OF との値照合と OF/READY の観測だけとし、別のセッション基盤は作らない。
- 旧像は従来どおり QMP で、OF boot-device・ELF entry・milestone の順序・serial B 無出力・READY→2秒 TB・EE・継続100Hz を検証。正像 (link) は、OF・entry・milestone の順・serial B 無出力を accept の後に out/serial-a.log・serial-b.log で照合し、READY→2 秒の TB と EE は link_run の tick の採取 (tick_rate) で代える (accept の diagnostic コールバックは (agent, out) だけで QMP を持たないため。PM 決定 N1 案 a)。
- LOG は HostAgent が保存した生 payload と順序/drop 診断を評価する。link_run.accept と同じく起動前に LOG を購読し、採取開始が遅れた欠落を成功にしない。
- status/clock/memory/read-diag/peek の5診断は共通 client の対応する診断要求へ写像し、値を ELF/map/OF/serial と照合する。
- PLN1 の17語 status、boot ID+MAC の固定 read-diag bytes、capture と serial の CRLF 込み一致は旧像だけに残す。
- link は意味・順序・内容を比較し、PLN1 の LAN STATE state=5 を捨て HELLO/ALIVE と診断成功で接続を判定。
- --lan-off は NIC 不在の有界起動として残す。link の未接続と READY 後の tick 継続を検証する。
### realhw_run (変更 ≤250 行)
- --link は受け身の AF_PACKET ETH_P_ALL 採取 (pcap/TX0用) と ppc_link_transport.Host client (常駐 agent の LOG/RSH/SFS用) に分ける。0x88B6 bind の lan.Packet は流用せず、agent を自分で起動しない。lan_bridge.py と常駐 host_agent の応答を二重化しない。
- QEMU、ROM、cross gcc は不要。image SHA、機種/OF/PVR、UTC、pcap、LOG、RSH raw/result、診断を保存。
- stdin は完成行ごとに RSH に submit。USB の生打鍵記録は RSH OUT に混ぜず、serial/カメラ証跡で評価する。
- run_rh の nest/shell/exact は意味の判定を再利用し、prompt/echo/終端を除く変換を transport ごとに固定する。
- link の capture 合格は HELLO/ALIVE と有効な LOG/診断を要する。nest/shell/exact は対応する完結した RSH 結果も要する。
- PLN1 beacon は link 世代/session/peer に置換。途中世代変更・欠落・未完了結果は失敗。記録の評価だけで実機の電源/TFTP操作は行わない。
### sc_run (変更 ≤250 行)
- --link は KBOOT_SH=1 KBOOT_LINK=1、独立した link USER 出力ディレクトリで hello/fault/nest を組む。
- RSH で /bin/nest.bin を実行し、hello と parent=12345、子 code=7 の2回分、最終 code=5、次の echo 成功を確認。
- CLI ラッパーの exit=5/os32> は旧像だけの期待。link では /bin/nest.bin の直後、他のコマンドを挟まず RSH で `echo $?` を送り5を照合する。RSH 自体の rc と混同しない。
- 旧 KBOOT_SHELL の serial nest は KBOOT_LINK=0 を明記して保持し、USER 入れ子の独立回帰とする。
- 本番 sh の redirect/cat/pipe/失敗を RSH OUT で照合。OPEN 待ち・OPEN 再描画は旧像だけで保持し、link は READY・ID・END・Ctrl-C を link_sh_run の受入へ委ねる。
- parent-result は第二結果だけ壊す既存変異を新しい観測にも適用。fd1-log/no-restart の serial 回帰も残す。
### ppc_sh_output (変更 ≤250 行)
- --link の像は KBOOT_SH=1 KBOOT_LINK=1。outprobe の旧バイナリを同梱し、RSH 実行結果で hello ok/H/FD1 PROBE を一度ずつ確認。
- 正常は FD1 PROBE が OUT にあり LOG にない。kernel READY は LOG にあり OUT にない。serial は起動証跡として別採取。
- fd1-log 変異は現在の console_stream の一意 anchor を再利用。OUT の probe 欠落と LOG の probe 出現を両方観測して RED。
- mirror 除去の私用像は旧像用に保持。link の分離をその注入で代用せず本物の capture 経路で検証する。
### usb_run (変更 ≤250 行)
- --lan を link に切替 (移行中は --link --lan)。旧 PLN1 --lan は明示 legacy 選択だけに残す。
- UsbLink.start は -g・USB_QEMU_CLOCK と QMP を使い、起動時には usb-mouse のみ付ける。mouse 列挙後に device_add usb-kbd,id=kbd を一度だけ送り、kbd 列挙確認後に RSH warm、SFS CALL 応答だけ抑止する。
- 抑止した要求の到達を記録し、cat /host/f が約1000msのゲスト期限で診断終了、tick/USB が進行、HC dead が無いことを確認。
- SFS 応答抑止は link/ALIVE/LOG 全体を止めない。解除後の cat 成功と echo alive を要求する。
- USB の echo hi は FB の証跡だけで見る (link の console_stream には pm_escc_write の目印が無い、platform/powermac/console_stream.c:45-48)。非 capture 時の USB 出力を RSH OUT に期待しない。
- key は product_accept 内の手順を小関数化し共用。押下/最初の REPEAT をゲスト記録で確認して離し、release まで待つ。壁時計 .16 秒を削除。
- PLN1 OPEN の行取消は link に移植しない。半入力中の RSH の有界な扱い・Ctrl-C/行復帰は link_sh_run の契約に従い確認する。
- 抜去 release、再列挙、再打鍵、screen.ppm を残す。--lan --mutate でも no-service/syscall-ee を実行し、生存だけの RED を認めない。

## 3. 既定を link に切り替える段
- build/ppc-kboot.mk だけ、100行以下。KBOOT_LINK ?= 1 とする。KBOOT_SHELL=1 / NEGATIVE≠0 は既存の禁止条件を維持し、呼出側に明示 KBOOT_LINK=0 を要求する。
- F3 の確認済み呼出元と処置は下表で固定。関門は「KBOOT_SH=1 かつ KBOOT_LINK 未指定の呼出元が0件」および各試験の合格。SHELL/NEGATIVE/無指定の旧起動も明示0にする。共有 build 関数へ一律0を注入せず呼出側を直す。
|ファイル:行|今渡しているフラグ|既定の切替の後の扱い (KBOOT_LINK=0 を明示して PLN1 に残す / link に移す)|どの準備段で直すか|
|---|---|---|---|
|tools/ppc/usb_run.py:123,298,583|settings/product_settings/--lan は SH=1、LINK未指定、観測用KB_MUT等|settings/product_settings は0を明示してPLN1、--lan は1を明示してlinkへ|P1 (旧経路の0)、U (--lanの1)|
|tools/ppc/fb_run.py:23,254,281|product_settings由来 SH=1、ROOT_MANIFEST等|0を明示してPLN1、呼出側でも固定|P2|
|tools/ppc/sd_run.py:456,465|SH=1、変異時KB_MUT等。:417,425はlink_settings|旧2箇所は0を明示してPLN1、既存--linkは1を維持|P3|
|tools/tests/test_ppc_link_sh.py:129,136,139,141,143|SH=1のみ / LINK=1のみ / SHELL=1のみ / SH=1,LINK=1 / 禁止組合せ|旧sh/shellとplainに0明示、linkと禁止組合せは1維持。既定1の期待も追加|P4|
|tools/tests/test_ppc_terminal_work.py:137–139|SH=1のみ / LINK=1のみ|SH側に0を明示してPLN1、LINK側は1維持|P5|
|tools/ppc/compile_ci.py:19|SH=1、PPC_CROSS_DIR、LINK未指定|PLN1 (SH=1,LINK=0) とlink-SH (SH=1,LINK=1) を別出力で両方明示構築、どちらの失敗も非0|P6|
|tools/ppc/kboot_accept.py:68,79,86,105|無指定 / NEGATIVE=N / KB_MUT等|全て0を明示してPLN1、build/make公開ヘルパーの既定は変更しない|P7|
|tools/ppc/sb_run.py:134,154|SHELL=1、KB_MUT等|0を明示してPLN1|P8|
|tools/ppc/sc_run.py:133,268,364|SHELL=1,BINS / SH=1,KB_MUT等 / SH=1|既存経路全て0を明示してPLN1、新--linkのみSH=1,LINK=1|P9 (旧経路)、C (link追加)|
|tools/ppc/ppc_sh_output.py:124,139|SHELL=1、FD1_STREAM=1、bins、KB_MUT等|既存経路0を明示してPLN1、新--linkはSH=1,LINK=1|P10 (旧経路)、O (link追加)|
|tools/accept/arch_ctx.py:128|SH=1、LINK 未指定、KB_ROOT_BINS に build/out/ppc-user/fault.bin、KB_MUT|0 を明示して PLN1 に残す (CHECK は arch-ctx-guest)|P11 (PM 決定 N3)|
- 表の SH/LINK/SHELL/FD1_STREAM は KBOOT_SH/KBOOT_LINK/KBOOT_SHELL/KB_FD1_STREAM の略。fb は import 元も追跡し、各ファイルの build/settings 呼出を実物検索した。
- :37/:47 の link/link-sh と USER 出力分離、:89 の旧 wire/terminal/input 除外、:141 の hostfs_link 選択を維持。旧 object の流用は禁止。
- 廃止の意味は既定生成像から PLN1 wire を除去すること。明示0の旧像と旧試験は :232 の旧像回帰として保持し、新 link 像の必須合否に混ぜない。
- PLN1 ソース削除・旧像ビルド禁止はこの段に含めない。ロールバックと旧 CLI/FS 比較を可能にする。
- TX0: 起動前から停止直前まで guest→host の全 Ethernet frame を採取し、EtherType 0x88B6 の送信数=0を数える。
- QEMU は link_run の frames.log を再利用し、`> ` の host送信と印無しの guest送信を分離して0x88B6を数える。短小/壊れた記録・空採取は失敗。有効な link LOG/診断/RSH/SFS も要求する。実機は R の ETH_P_ALL pcap を使う。
- 生成像、map、nm、size (text/data/bss)、ELF/配備用像 SHA256、flags/toolchain/QEMU、全フレームと判定結果を同じ run に保存。
- 旧0/新1の診断・CLI・HostFS像を同条件で比較。PLN1静的TX/RX不在、link_txbuf、heap高水位≤1MiBを記録。未知のサイズ予算を作らない。
- 戻し方: mk既定を0へ戻し、同じflagsでPLN1像を再生成し、SHA/map確認後に従来の起動像へ戻す。link-sh生成物をPLN1像と取り違えない。
- D の完了条件に DEFERRED_TESTS（既存の正本ファイル） の行 PPC-LINK-G4 追加を含め、G4受入完了後に削除する (文書変更は1ファイル別段G)。
- G4: 時期と物理操作は§6の判断待ち。ノート担当の実機受入で TFTP が読む像を link にし、その像 SHA と LOG/診断/RSH/SFS の一巡・TX0を保存。
- /host cp は2048B/4096B/64KiBそれぞれのデータ一致、経過時間・転送量・速度、打鍵中の進行を旧基準と比較する (buffer変更でなくファイル量として記録)。速度は記録のみを推奨し、退行を合否にするかは§6。データ一致と進行は必須合否。
- USB押下/解放/繰返し/抜去と、NAP復帰のSRR1[POW]を確認。QEMUでPOWが消える観測をG4へ外挿しない。異常なら切替受入を止め、修正は別段。

## 4. ファイルごとの変更と段分け
|段 / ファイル|変更上限|依存・並列|受入|
|---|---|---|---|
|K / tools/ppc/kboot_run.py|250行|最初。既存export/serial validateを維持|host判定fixture + QEMU 7450/7455、正/negative/NIC不在|
|R / tools/ppc/realhw_run.py|250行|K/C後 (sc_runのEXPECTED/check_shellとsd_runのexactを利用)|host記録fixture。G4記録は最終受入|
|C / tools/ppc/sc_run.py|250行|K後、EXPECTED/check_shellの公開名維持を完了条件とする|旧serial nest + link nest/sh、変異|
|O / tools/ppc/ppc_sh_output.py|250行|K後、Cと公開関数を変えなければ並列可|host判定 + QEMU OUT/LOG、fd1変異|
|U / tools/ppc/usb_run.py|250行|K後。product_settings/stream_payload/USB_QEMU_CLOCKの公開名維持を完了条件 (fb/link_shがimport)|QEMU USB/FB、--lan、変異|
|P1 / tools/ppc/usb_run.py|250行|並列可、D前|§3の明示フラグと従来受入|
|P2 / tools/ppc/fb_run.py|250行|並列可、D前|§3の明示フラグと従来受入|
|P3 / tools/ppc/sd_run.py|250行|並列可、D前|§3の明示フラグと従来受入|
|P4 / tools/tests/test_ppc_link_sh.py|250行|並列可、D前|§3の明示フラグと従来受入|
|P5 / tools/tests/test_ppc_terminal_work.py|250行|並列可、D前|§3の明示フラグと従来受入|
|P6 / tools/ppc/compile_ci.py|250行|並列可、D前|§3の明示フラグと従来受入、両像ビルド|
|P7 / tools/ppc/kboot_accept.py|250行|並列可、D前|§3の明示フラグと従来受入|
|P8 / tools/ppc/sb_run.py|250行|並列可、D前|§3の明示フラグと従来受入|
|P9 / tools/ppc/sc_run.py|250行|並列可、D前|§3の明示フラグと従来受入|
|P10 / tools/ppc/ppc_sh_output.py|250行|並列可、D前|§3の明示フラグと従来受入|
|P11 / tools/accept/arch_ctx.py|250行|並列可、D前|§3の明示フラグと従来受入 (arch-ctx-guest)|
|G / DEFERRED_TESTS（既存の正本ファイル）|250行|P1〜P11 と並列可、D前|PPC-LINK-G4行追加、G4完了時に同ファイルの別段で削除|
|D / build/ppc-kboot.mk|100行|P1〜P11、K/C/O/U/R、Gと下記統合受入後、直列|像再生成、map/size、QEMU全一巡と旧像回帰、Gの延期行確認|
- 準備段 P1〜P11 は互いに並列可 (どれも別ファイル)。関門は D の前に全部が揃うこと。同じファイルの組 (P1 と U、P9 と C、P10 と O) だけは表の依存に従う (PM 決定 N4)。U/C/O はそれぞれ P1/P9/P10 後、他段の前提ヘルパーを保つ。
- 250行は差分の追加+削除で計数。超過するなら実装前に同一ファイルの別段へ分割し、必要な共通抽出も1ファイル別依頼にする。
- 各試験の CHECK.inputs に実際の共通 transport/HostAgent/Session/生成入力を宣言。kboot/realhw の未登録評価は別 host 試験ファイル (各400行以下) で登録する。
- host fixture追加は各1ファイル別段。五台本の上限へ試験ファイル変更を隠して含めない。
- 各実装段: preflight --base b00386eb --fix-fonts、対象像build、触った試験 --mutate、python3 -B tools/checks/run.py --lint。最終source変更後の結果だけ採用。
- product行を変更/削除する前に rg -n で tools/tests と tools/ppc の既存anchorを検索・移設し、一意一致を要求する。
- host -m32 はlibc無し、HOST32_RUNNERS=qemu。socket/cross/QEMU不足は理由付きSKIP/0、OS32_REQUIRE_PPC=1では非0。host判定/変異は省かない。
- PPC_CROSS_DIR=/nonexistent でも実行。通常/require=1の両方を確認。環境によって代替手順へ切替えない。
- socketを要するQEMUはPMの環境でforeground完走。実装sandboxでのSKIPはQEMU合格として扱わない。
- make check-changed/全変異は実装者は回さない。取り込みCI check+build/check-fast、full-checkはユーザー判断。ABI変更なし、make clean→allの強制対象外。

## 5. 受入項目 → 試験と変異
|受入・期待の出所|試験|変異 (正常判定が対象理由で落ち、到達・後続生存も確認)|
|---|---|---|
|OF/READY継続、既存kboot観測ABI|KのQMP TB/EE/tickとserial milestone|記録fixtureのREADY欠落/順序違反・tick停止を拒否|
|初期LOG・drop、設計:253|KとRのLOG生記録/欠落判定|drop報告消去、末尾欠落fixtureでRED|
|5診断、設計:253 / 旧lan_judgeの値根拠|KのOF/map/entryとの値照合|PEEK応答1byte破損、clock不一致fixtureでRED|
|USER入れ子・旧CLI契約、sc EXPECTED|C旧serialとlinkの二重子結果/親復帰|既存parent-result、第二結果のみ破壊|
|USER fd1、outprobe/既存sh-output|OのOUT/LOGの正負両判定|fd1-logを本物のlink像で実行|
|CLI redirect/pipe/再起動、check_shell/P3|C + 既存link_sh_run|既存fd1-log/no-restart、P3 END/重複ID変異|
|HostFS永続/RO/期限/世代、設計:255|既存sd_run --link --mutateを統合で必須実行|既存tmp-target/fixed-nonce等、永続値・STALEの実観測|
|RPC停止後USB、usb_run --lan/PPC_NOTES §3|UのCALL到達/ゲスト期限/打鍵/復帰|no-service/syscall-ee、打鍵invert-key/no-forwardも維持|
|打鍵がゲスト時計基準、PPC_NOTES §3|Uのpress/repeat/releaseとFB|release待ち省略の試験刺激で余分な文字を検出|
|実機記録の完結性、realhw run_rh|R host fixture + G4 pcap/LOG/RSH|世代途中変更/未完了OUT/欠落記録を拒否|
|linkのみ全一巡、設計:256|Dの同一runでLOG→5診断→RSH→SFS|各イベント欠落fixtureで統合判定RED|
|0x88B6 TX0、設計:256|Dの起動から停止までguest送信集計|guest PLN1 frameを1本挿入、空captureも拒否|
|像/map/サイズ、設計:231/256|Dの成果物・heapと旧新差分|mapへ旧TX/RX領域混入fixture、成果物欠落を拒否|
|旧バイナリCLI/FS契約、設計:256|同じhello/nest/outprobeと既存CLI/FS操作を旧新で比較|既存parent-result/fd1-log/tmp-targetを利用|
|欠如環境、PPC_NOTES §6|全対象のcross/socket/QEMU欠如fixtureとrequire=1|常時成功/常時SKIPへ判定変更してRED|
- 表のfixture変異は判定入力を一箇所だけ壊す。製品の変異は既存一意anchorを使い、timeout/一般例外だけをREDにしない。
- G4はPPC-LINK-G4で追跡し、速度は記録のみ (ユーザー判断で変更)、POWは実測合否で受ける。未実施は未確認と明記し、host fixtureを実機受入の代わりにしない。

## 6. 未決 → ユーザー決定 (2026-11-16)
- G4 の受入の時期と物理操作: 「QEMU 合格後すぐ (推奨)」。既定の切替の段 D が QEMU の統合の受入に通ったら、続けて G4 の受入を行う。実施の日時はその時点で改めてユーザーに聞く。それまでは DEFERRED_TESTS の行 PPC-LINK-G4 で未実施を追跡する。物理の操作 (G4 の電源・TFTP の起動・キーボード) はユーザー。
- cp の速さの退行を合否にするか: 「記録だけ (推奨)」。旧 PLN1 と新 link を同じ条件で 2048B・4096B・64KiB を測って残し、合否には使わない。

## 確定 (2026-11-16、Opus 第 2 回、PM が反映)
- N1 (P1): K の正像は link_run.accept を丸ごと呼ぶ (案 a)。OF・serial の照合は accept の後に記録で行い、READY→2 秒の TB と EE は link_run の tick の採取で代える。link_run.py に QMP を渡す口を足す準備段 (案 b) は作らない。
- N2 (P1): U は製品の link-SH と局所の rsh_command。差し替え口 3 つは変異だけに使い、USB の echo hi は FB の証跡だけで見る。
- N3 (P2): 呼び出し元の表に tools/accept/arch_ctx.py:128 を P11 として足した (0 を明示して PLN1 に残す)。
- N4 (P3): 準備段 P1〜P11 は並列可。D の前に全部が揃うこと。
