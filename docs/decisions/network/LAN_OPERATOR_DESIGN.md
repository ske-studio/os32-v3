# LAN_OPERATOR_DESIGN — 操作口 (rshell・gshell) を LAN が既定・serial が退避先に、PowerPC の PLN1 を link に統合

> 状態: **現行 (2026-11-14)** — 確定 (astra 起草 r1〜r4、Opus 5.5 の独立レビュー 3 回と PowerPC PM の見直し、条件付き Approve を PM が反映)。段の状態は票 TASK_LAN_OPERATOR が持つ。
> 出所: 往復の版は `~/os32-tmp/run/lan-rshell*/` (DESIGN.r1〜r4、REVIEW.r1・r2・REVIEW_PPC.r2)。

作業木は読取りのみ。preflight（修正を伴う）・ビルド・試験・配備は未実施。以下の合格条件は実装後の計画であり、性能・容量は未測定。

## r3→r4 の変更
- U5: NICごとの常駐agent、unit・設定・8027へのCLI接続を追加。受入台本の起動停止を廃し、再READYは維持。
- POLICY_DEV §1: 既存機能・HALによる共通化・計画との整合の3点を§1に追加。

## r2→r3 の変更
- N1: capture中もkbdの30秒期限を維持し、入力を閉じる段・試験・変異を追加。
- N2: X7/X8をPPC像変更と明記し、console_logのflag所有を統一、kbootリンクとQ最小回帰を追加。
- N3: PPCのlink_tickをtick登録から呼び、時計をtick_countへ統一、GEM RX上限を維持。
- N4: 初回link像はP1＋LOGとし、FB帯にHELLO送信/確立の印を追加。
- N5: 1文字PRINTの最悪容量とレコード数境界を明記し、既定切替前のhsync実測・必要時増量を必須化。
- N6: READYに直近実行ID/状態を追加し、UNKNOWNの遅延OUTで結果を確定。
- N7: AGAIN/STALE時は2秒ごとに再READY。r4ではU5によりagentを常駐化し、受入台本は起動しない。
- PPC-P1b: link.cのne2000.h・idt.h・io.h依存を除去しPPCビルドを完了条件化。
- PPC-P1d: heap高水位をmapとともに記録し、1MiB以内を確認。
- PPC-P1g: link像にPLN1の静的TX/RX領域が残らないことをmapで確認。
- PPC-P2: P1→LOG受け口/表示→P2c→P2aの順で初回像を組み、観測不能な像を出さない。
- PPC-P5: 共通transportと台本別段に分割し、節目前に移行、PLN1試験は既定切替まで保持。
- U4: PPC移行の各段の受入はH/Q（7450・7455）。G4基準確認は移行前の現行PLN1版で行い、既定をlinkへ切り替えた後にG4実機の確認を1回行う。
- PPC-reset: reset/PHY/pollが同じ関数の流用であることをdiffで示す。
- PM-LOG: PLOGを共通LOG/_service_logに変更、x86 bootlogも送れる契約にしos32.予約名を使わない。

## 1. 実物の確認（r1 の有効な確認を保持）
- 統合判断1（wt/int-1114cのPOLICY_DEV §1）: 同じ役目は本線のlink/host_agent、HOST_SERVICES_PLAN §§2–3、NET_HOSTSVC_DESIGN §§2/5/6/9とPPCのPLN1診断・LOG・端末・HostFSにある（r3の確認を継承、§5の対応表）。新しい独立プロトコルは作らない。
- 統合判断2: link/通知/LOG/SFS backendとホスト操作口は1つの実装に統合する。tick・IRQ・idle・NICはlink_platform/NetNicOpsのHALへ分離し、PC98/GEMと既存CLIのadapterだけをplatformに残す。PPC専用GSERIALは作らない。
- 統合判断3: 共通link v2・共通tick・既存VFS/常駐mount・既存shellの実行経路を維持するため計画の向きと一致。PPCからSFS on linkを実証しx86 raw SFS廃止は§9の後続関門とする。レビューにもこの3点を渡す。
|確認|現在の根拠・含意（作業木相対 file:line）|
|---|---|
|serial TX|`drivers/char/serial.c:644,654,677` は TxRDY の待ち・予算超過時 HLT・IF=0 の上限。常時spinとの断定は誤りだが、出力中の待ちを避ける目的は有効。|
|console|`kernel/console/console.c:203,218,300,340` はsinkへ出力。`:205,222,302,342` のserial複写は rshell_active **または** boot_serial_mirror。文字単位の既存非複写とsinkの全PRINT取得は区別する。|
|EOT/実行|`userland/shell/rshell.c:425–497` の拒否・空行・exit例外・SFS分岐・echo/実行後EOTを維持。`tools/rshell_serial.py:258,592` のEOTはrcではなく、SFS結果行判定は専用。|
|像と起動|rshellはshell.bin (`build/programs.mk:106`)、sh.bin対象外 (`userland/shell/main.c:128`)。`ui.c:633` 付近のserial初期化・rshell自動開始を維持。|
|sink|`include/con_sink.h:21` は8192B（レコードheader込み）。`con_sink.c:125,133,243,297,312` にenable/disable/read/stat/owner回収、読取り所有者は1人。enableは公開KAPIでない。|
|通知|`tools/host_notify.py:47` は名前→LIST番号に依存。`NET_HOSTSVC_DESIGN §5` の65535は競合消費なので、番号指定だけでは先取りを防げない。予約番号・検索除外の追加が必要。|
|GUI|`gserial.rs:64,81,292` はserial直結、openはserial初期化依存。`wm.rs:1180` は通知を消費せず前景にイベント配送。LAN入力と返答の差替えが必要。|
|帯域|`net/link.c:514–573` はクラス交互TX、回数上限とtick_budget、100Hz駆動、末尾にnotify_tick。WDATAは最大1400BでACK待ち。LAN化だけで所要時間は保証しない。|
|PPC依存|`net/link.c:56–65` のidt/io/idle、`net/nic.c:8–17,63` のPC98 tick、mailboxのirq依存を移植する必要がある。PLN1 codecだけの移動では成立しない。|
|hsync|`userland/system/hsync.c:370,984,1449,1728` 等にファイルごとの出力があり、件数依存で8KiB以内とは証明できない。上限超過の実行結果と取得失敗を分ける。|
- 9600bps・8N1で1536Bは線上だけで約1600ms。既存T1c遅延との因果とLAN所要時間は今回未測定。
- x86既存NIC demuxは0x88B5内のSFS MARK全3Bとlinkを分離 (`net/nic_rx.c:54,61`)。x86のraw SFSは今回廃止しない。

## 2. 通信契約とホストの口
- EtherType **0x88B5**、既存link v2を使う。新しいEthernet型・GSERIAL型・任意execサービスを追加しない。NOTIFYはコマンド、REQUEST/WDATAは結果を運ぶ。
- 操作番号を **65534=rshell、65533=GSERIAL、65532=PPC診断** と固定予約（名前なし）。Registryの上限を65531へ変更しLISTに載せない。通知wireは既存のまま、Notifyに番号指定の内部入口を足す。
- mailboxのservice=65535検索では上記3番号を飛ばす。明示番号の取得だけが消費できる。購読・複製は作らず、rshell/gshell/診断の前景所有者を各1つに固定する。
- host_generationは現在のALIVE/HELLOと同じ。ゲストは操作受付準備時に `RSH READY <boot-token> <mode> 1 <last-id|none> <idle|running|done>` をhost_openで送り、mode=cui/gui/diagをagentが覚える。READYの200本文はhost_generationと対応操作番号を返し、ゲストはそれをhost_notice_readの世代指定に使う（世代不一致なら再READY）。boot-tokenは起動識別用128bit表記で認証ではない。last-id/状態は直近実行のhost_generationに紐づけ、世代変更時はnone/idle。AGAIN/STALEなら前景から2秒ごとに再READY（待ちでlinkを止めない）、世代更新後も同じ規則とし後起動agentへ復帰する。
- agentはHELLO確立と対応READYを見てからCLIへ送信可を返す。HELLO切断・世代変更・ゲスト再起動でREADYと保留操作を無効化し、旧結果を新操作に流用しない。
- kind=1は実行、kind=2は無実行の準備照会。rshell dataは `id8 SP command LF`、commandは最大126B・ASCII可視文字、1行のみ。空行はCLIで無送信、ESC/NUL/複数改行/過長は実行前拒否。
- GSERIAL dataは既存 `SOH id8 SP body LF`（既存bodyを含む行上限128Bを維持）。各通知は236B以内。kindの追加はNOTIFYの用途値であり新wire型ではない。
- id8はagent世代内で単調増加32bit、周回前に受付停止。ゲストは最後に受理したIDと実行中/完了を保持し、同一ID再実行・古いIDを拒否。link再接続後も同一host_generation中は保持する。
- 結果要求は `RSH OUT <boot-token> <id8> <drop> <len>` + WDATA。本文はecho/本文/末尾0x04。agentの `_service_rshell` が宣言長と本文を照合してから200、CLIへは全長受信後だけ完了を返す。
- GSERIALは `GS OUT <boot-token> <id8|!> <drop> <len>` +既存 `gs:<id> ... end N` 行群。非同期 `gs:!` は別結果で配送し、当該IDのendと混同しない。
- 結果本文の初期上限は8193B（sink由来の最大8192B以下のテキスト＋EOT、capture増量時は容量＋1へ更新）。drop非zero、長さ不足、EOT/end欠落、世代/ID不一致は取得失敗。部分表示は許すが完了にはしない。
- echoの一致も検査。exitは既存どおりechoなし、入力付き終了EOTあり。通常のrcは次の `echo $?`、SFSは既存 `sfs: exit=N`。LAN通信成功をコマンドrc=0とはしない。
- NOTIFY重複ACK/Go-Back-N、REQUEST/WDATAの再送はlinkに任せる。CLIから同じコマンドを再投入しない。ACKはmailbox受理であり実行完了ではない。
```python
# tools/host_rshell.py: agent内の操作状態。Agent._service_rshellから利用。
class Operator:
    def submit(self, mode, line, client): ...  # 全mode合計1件、busyは無送信
    def accept_output(self, generation, session, boot, ident, dropped, body): ...
    def session_reset(self): ...              # 実行中はUNKNOWN、キューを破棄
# tools/rshell.py / gserial.py は以下の共通transportへ渡すだけ。
class CommandTransport:
    def request(self, mode, line, timeout): ...
```
- **H4の接続を固定**: agentにloopback TCP（既定127.0.0.1:8027、設定可）の操作専用口を追加。4B BE長＋UTF-8 JSON、1レコード初期上限16KiB（capture増量時はbase64とJSON見出し込みで更新）、本文はbase64。操作はprobe/submit/result/noticeにLOG購読を追加。SFS CLIはsubmitで既存ゲストSFSコマンドを操作し、root/RO/peerの照会はprobeで行う。任意ファイルRPC・SFS世代永続化は扱わない。Windows/WSLとLinuxで同じCLIを使えるためこの方式を選ぶ。
- agentの既存selectループへ非同期で組込み、CLI待ちでlink処理を止めない。probeにはagent世代/peer/HELLO/READY、submitにはmode/lineを渡しIDはagentが付ける。接続切れ後も送信済み操作を自動再投入しない。
- submit は任意の `timeout_ms`（正の整数）も受け、ホストの操作期限を submit 時刻＋そのミリ秒数にする。CLI は指定 timeout を渡し、省略した旧 client だけ 15000ms を使う。不正値は BAD_REQUEST。
- autoはprobe/READY待ちを合計4秒まで、agent不在・HELLO/READYなしなら送信前にserialへ。**submitを送ろうとする時点から退避禁止**（NOTIFY予約より保守的）。例外・timeout・agent再起動もUNKNOWN、次の自動コマンド列を止める。
- agentは1件を結果またはUNKNOWNまで保持し、UNKNOWN後は対象ゲストのREADY再確認と利用者の明示的な再開まで次を送らない。CLIの切断だけでは実行中枠を解放しない。UNKNOWNのIDも照合情報を保持し、同じagent世代・boot-token・IDの遅延RSH OUTを再接続後も受理する。長さ/drop/echo/EOT検査合格なら結果確定、READYのdoneだけでは完了にしない。旧世代/別bootのOUTは拒否し、遅延結果でも再送しない。

- **U5 常駐**: ホスト起動時からNICごとにhost_agentを1つ常駐させる。rshell/gserial/LOG表示/SFSのCLIは8027へ接続し、agentを子プロセスとして起動・停止しない。切断してもサービスは存続し、ゲスト不在中も操作口と再接続待ちを維持する（既存open_streamのブロック中もprobeを処理）。
- 設定は `/etc/os32/host-agent/<instance>.json`（root所有0640）に `iface,root,allow-write,peer,backend,frame-endpoint,operator-endpoint,state-dir` を記載。rootは絶対パス、allow-write既定false、peerは固定ゲストMAC。state-dir に世代・id・sfs_generation を永続しない (NET_HOSTSVC_DESIGN:154、agent の再起動は新しい世代、A5)。ロックとログだけを置く。設定検証後に起動し、NIC重複はロックで拒否。複数NICは127.0.0.1:8027、127.0.0.2:8027等で操作口を分離し、8026とstate-dirもinstance別にする。
- unit原本は `tools/systemd/os32-host-agent@.service`、ExecStartは `/usr/bin/python3 /opt/os32/tools/host_agent_service.py --config /etc/os32/host-agent/%i.json`、Type=simple、Restart=on-failure、RestartSec=2、WantedBy=multi-user.target。専用os32-hostユーザー、永続状態は `/var/lib/os32-host-agent/%i`。必要なraw権限はunitのCAP_NET_RAWに限定する。
- インストールは管理者が一度、専用ユーザーと設定/状態ディレクトリを作成→tools一式を `/opt/os32/tools` に配置→設定を編集しrootへの必要な読書き権限を付与→unitを `/etc/systemd/system/` へ配置→`systemctl daemon-reload`→`systemctl enable --now os32-host-agent@<instance>`。Ubuntuノート・systemd有効のWSLは同じunitを使う。WSLはWindowsログオン時のタスクで対象ディストリビューションを起動・維持する。systemd無効環境では同タスクが `wsl.exe -d <distro> --exec /usr/bin/python3 /opt/os32/tools/host_agent_service.py --config <設定>` を監視再起動し、ゲスト/台本と独立して常駐させる。
- Ubuntuの物理NICは既存lan_bridge→8026→agent、raw SFSはlanfs_host.pyの既存Server/HostFSを維持する。サービスrunnerがbridge/lanfs_hostを同じ設定で常駐管理し、root/allow-write/peerを渡す。SFS MARK全3Bで分離し二重応答を防ぎ、NIC送信MACを揃える。PPCのSFS CALLは同じbackend実装をagent内で使う。raw SFS自体はCLI用8027へ載せ替えない。
- WSL→Windows NP21/W は既存の NP2NETSOCK (4B BE 長＋Ethernet) の 8026 経路を今の向きのまま使う: WSL の agent が 127.0.0.1:8026 で listen し、NP21/W が NP2NETSOCK=127.0.0.1:8026 で接続しに来る (`host_agent.py:17`、`docs/tasks/network/PLAN.md:266`)。Windows 側に listen の口を開かない (A3)。NP21/W が切断の後に再接続するかは NP21/W の試験の項目に入れる。WSL の NAT を raw Ethernet の橋とみなさない。NP21 の raw SFS もこの frame 経路で MARK を demux する。

## 3. CUI の境目・出力・寿命
```c
/* 追加KAPI 1本、GNU89公開宣言。既存read/statを流用。 */
int con_sink_capture(unsigned int op); /* BEGIN=1, FREEZE=2, END=3 */
/* BEGIN: CUIかつreader不在のみ成功、owner取得、空にしdrop基準保存。
 * rshell_active/boot_serial_mirrorを保存して0、captureを有効化。
 * FREEZE: pushを止めread/stat用に保持、ownerだけ許可。
 * END: 残りを破棄、reader解放、両flag復元。owner_exitも同じcleanup。 */
int rsh_lan_poll(void); /* 行頭のみ: noticeを1件取り、既存execute_commandへ */
int rsh_lan_flush(unsigned int id, unsigned int dropped); /* host_*、非IRQ */
```
- con_sinkにGUI enableと独立のcapture状態を足す。CUI captureで `con_sink_is_enabled()` を真にして画面をGUI扱いにしない。push条件だけGUIまたはcaptureに拡張、GUI中BEGINはBUSY、既存gshell readerを奪わない。
- capture領域は初期8192B（容量定数で管理、実測不足ならcapture専用リングを増量可）、PRINTだけを順に取り出しCLEAR/CURSOR/EXITは本文化しない。FREEZE後にstatのdrop差を確定し、capture容量に対応する固定長のshell側結果bufへPRINTを抽出して宣言長を計算してからhost_open/write。echoもcapture内、EOTは送信側が最後に1個足す。
- PRINTは3B header＋本文。shell_putcharの1文字レコードでは8192Bに最大2048件＝本文2048B（リングの予約領域や制御レコードがあればさらに減少）。境界は本文bytesでなく総レコード占有量で決める。dropの差があれば `output truncated; execution status unconfirmed` としてCLI失敗。hsyncを自動再実行しない。既定切替前に通常/大量manifestのhsyncについて本文量・レコード数・占有量・dropを実測し、不足ならcapture用リングと結果bufを増量して再測定する（PM承認済み、物理制約なし）。増量時は結果本文上限と専用TCPのbase64込みレコード上限も整合させる。
- 読出しbufはCON_SINK_REC_MAX以上の固定サイズ、heap/VLA不要。host_writeは部分受理分だけ進め、AGAINではsys_halt/yield、1000ms無進捗または全体timeoutでclose/END。実行中はhost handleを確保しない。
- rshellはserial半行を先に完成し、行頭でLANを選択。LAN中はkbdのserial取込みとrshell直接serial入力を停止、local STOPは既存経路を維持。kbd.cの期限判定はrshell_activeだけでなくcapture中も有効とし、KBD_RSHELL_TIMEOUT_SEC=30で入力を閉じる。行入力はEnter（空答え）、キー操作はESCを返す。hdprepは空答えで中止、script入力とfilerも待ち続けない。期限適用でserial読取りを再開しない。終了・拒否・中断・host障害・owner破棄の全経路でEND。
- rshell_active=0だけではboot_serial_mirrorを止められないため両方を制御する。serial専用コマンドの直接UART操作はLAN経路で拒否し、LAN sfs runは常駐LAN scope確認後だけ既存sfs_run_childへ渡す。
- serial向けrshell_serial.pyの既存速度交渉・watchdog・ESC・SFS UART動作は維持。新rshell.pyと既存run_lineはsendの注入口を共有し、--sfs-lanもLANコマンドtransportを通せるようにする。

## 4. gshell への引渡し
- `gui`/既存GSERIAL起動形を完全な行として判定し、通常のexecute_commandに入る前にcaptureを終える。`RSH ACCEPT <boot> <id> <len>` + echo/受理文/EOTを送り、host_status=200を確認してからENDしGUIへ入る。
- ACCEPTは「実行完了」ではない。CLIはGUIの `gs:! ready v1` とGS READYまで待つ。起動失敗はCUI READYと失敗通知、ready不着はUNKNOWNでありguiを再送しない。echo $?はGUI復帰後の通常CUIで扱う。
```rust
// 新 gserial_link.rs は通知取得・送信状態を所有、gserial.rs は既存parser/execute。
pub fn poll(st: &mut GuiState); // 自番号を最大1件取得、1周最大1chunk送信
pub fn emit(id: &[u8], bytes: &[u8]); // 有界enqueue、serial_putsを呼ばない
// gserial.rs: receive_line(st, bytes) をserial受信とNOTIFYが共有。
// wm.rs: gserial_link::poll(st); deliver_host_notice(st);（後者は非消費のまま）
```
- gserialのLAN有効化をserial初期化から分離。入力元は要求単位に固定し、LAN応答/gs:!はLANへ、serial退避時は従来UARTへ。serial半行とLAN行を混ぜない。
- LAN応答FIFOは8192B、dropカウンタ、FREEZE相当の確定長を持ち、GS OUTを非同期進行。endを書いた後に送る。gs:!は独立枠1件（満杯はdrop失敗）、終了通知はGUI解放前に送信完了まで協調待ち、期限切れはUNKNOWN。
- WMのpollは通知の投入通番変化だけに依存しない（起動時/前景なしでも自分の番号を読む）。他アプリへのHOST_NOTICE配送・pending/リング満杯時の再試行は維持する。
- tools/gserial.py: requestのwrite/read部分をtransportへ抽出し、既存id8/end N/ready/cui done/watchdogの検証を共有。LAN終了後CUI READYを確認してからrshellを再開する。

## 5. PowerPC を link に統合する
|PLN1の機能|移行先と流用（file:line）|
|---|---|
|BEACON/生存|HELLO/ALIVEが接続・ホスト世代、ゲストboot-token付きREADYが再起動識別。統計は診断STATUSへ。初回ログはHELLO待ちで捨てず既存リングに保持。|
|LOG|`LOG <boot> <seq> <drop_records> <drop_bytes> <flags> <len>` + WDATA。`lan_log.c:98,105,121,127` のregister/peek/stats/completeを流用。host_agentの`_service_log`が受信しCLIで表示。PPC専用名/os32.予約名を使わず、x86 bootlogも同じ形式で送れる共通サービスとする。200受信まで同chunk保持、session断は欠落を計数し完全配送と称さない。|
|REQ/RSP診断|番号65532のNOTIFY(kind=1、id8+opcode+既存BE引数のhex)→`PDIAG OUT <boot> <id> <status> <len>`+WDATA。`lan_monitor.c:43,55,103` のprobe・5命令/範囲検査・cacheを流用、MAC+nonceをlink peer+host世代由来の内部キーに置換。副作用なし・PEEK256B/READ_DIAG128B維持。|
|TERM_OPEN/IN/OUT/STATE|READY/NOTIFY/RSH OUTへ。`terminal_input.c:13,26` の行取消/feedを流用、旧seq/dropはlinkと結果dropへ。`lan_terminal.c:27,52` のPLN1状態は廃止、CLI解釈器は維持。|
|FS_OPEN/REQ/RSP|**SFS on link**を選択。`SFS CALL <len>`+WDATAでSFS frameを渡し、200+DATAにSFS応答frame。`hostfs_io.c:54` のCRC/長さ検査、`:79` の絶対期限、既存SfsClient/VFSを流用。|
|HostFS peer/世代|`hostfs_peer.c:9,21,30` の固定peer検査・世代変化の意図を維持し、PLN1 nonce観測を共通HostAliveへ置換。`:14` の重複host_alive_snapshot実体は削除、共通実体を使う。|
- SFS CALLはhost_agent内backendで既存SFS sid/seqの重複cacheの内側へdispatchする（新たなファイル実装を作らない）。BYE等の片道もlinkでは空本文200を返す。CRC/最大frame1044B、LEのSFSとlink、BEの診断payloadを明示codecで扱う。
- link ridをFSの再実行キーにしない。同一sid/seqの同内容だけ再送、write結果不明は新sidで再実行禁止。ALIVE変更で旧FD失効、/hostの常駐寿命・RO既定・allow-write・HOSTDOWN/STALEを維持。
- PPCのput_untilはSFS frameを1つ保留し、待機pollがhost_open/write/status/read/closeを進め、完成frameだけ既存rxへ渡す。入口の1000ms絶対期限をHELLOから全段で共有し、AGAINで取り直さない。
- x86 raw SFSは現状維持。SFS CALLはPPCから先に実証し、x86全利用者/FD導入を載せ替えてraw SFSを廃止する作業はNET_HOSTSVC_DESIGN §9の後続関門。
```c
/* net/link_platform.h: 機械依存をこの境界へ。x86動作は既存実体で保持。 */
u32 link_platform_now(void); /* 共通tick_countを使用、10ms、周回比較。独自TB時計は作らない */
u32 link_platform_lock(void); void link_platform_unlock(u32 saved);
void link_platform_idle(void); /* PPC: 前景poll、再入禁止。IRQ/送信内から待たない */
/* platform/powermac/nic_gem.c: GEMをNetNicOpsへ、既存DMA/reset/PHYを再利用 */
/* PPCもPC98_TICK_LINKと同じtick枠へtick_registerでlink_tickを登録。
 * USBのusb_platform.c:132と同じ基盤を使う。前景からlink_tickを呼ばない。
 * link像ではGEM RX/TXもx86のnet_jiffyと同じtick枠で汲む(1回TX回収8/RX8の上限は維持)。
 * 前景のpm_lan_pollはlink像のRXに触らない(A1)。 */
```
- tick_budgetは共通tick枠の予算と既存回数上限を守り、PPCで無制限成功stubにしない。mailbox・NICのirq保存はPPCのMSR保存/復元、所有者は既存resident CLIのowner規約を使い、ring3のx86命令依存を持ち込まない。
- `lan_service.c:202,251` のreset/PHY/GEM poll、`lan_gem_io.c` とDMA処理は流用。RXを旧serviceとnetが二重drainしない。移行中はビルド選択でPLN1かlinkのどちらか1つのみ、既存PLN1像を戻り先として保存する。
- PPC CLIは行頭でLAN完全行を受けるadapterを足し、既存CLIを呼ぶ。LANコマンド中のstream出力を8KiB captureに向けESCC複写を抑止、終了後RSH OUT。ESCCの対話・Ctrl-C・行取消は退避先として維持、入力待ちを必要とする対話プログラムはserial選択で使う。
- PPCの早期診断・LOGはshell無し像でも動く。NOTIFYからIRQで診断を実行せず、前景pollの後に1件だけ処理。PPCにgshellが存在するとは仮定せず、GSERIAL移植は含めない。

## 6. terra に出す段（各依頼1〜3ファイル・400行以下）
行数は手書き製品差分の上限予算。各行を別依頼とし、同じ機能の検査も合計400行に収まらなければ契約試験/受入台本を別依頼へ分ける。生成物は機械生成の行数を別記し、手編集しない。
|段|本体ファイル・機能|上限|依存／生成物|
|---|---|---:|---|
|X0|新 `include/remote_control.h`、操作番号/要求契約|100|共通宣言、PPC像はまだ不変|
|X1|`tools/host_services.py`、予約番号を採番から除外|60|X0／hostのみ|
|X2|`net/host_mailbox.c`、全種検索から操作口除外|80|X0／x86 vmkernel|
|X3|`tools/host_notify.py`、番号指定通知入口|80|X0／hostのみ|
|X4|新 `tools/host_rshell.py`、操作状態・結果照合|350|X1/X3／hostのみ|
|X5|新 `tools/host_operator_socket.py`、専用TCP口|280|X4／hostのみ|
|X6|`tools/host_agent.py`、_service_rshell・GS/PDIAG/LOG dispatch・_service_log/WDATA宣言・loop結線|200|X4/X5／hostのみ|
|U5a|新 `tools/host_agent_config.py`・`tools/systemd/host-agent.example.json`、設定検証/instance分離|300|X6後／hostのみ|
|U5b|新 `tools/host_agent_service.py`、NICロック・子サービス監視・設定から起動|350|U5a／hostのみ|
|U5c|新 `tools/systemd/os32-host-agent@.service`・`tools/systemd/README.md`、Ubuntu/WSL導入・ログオンタスク|250|U5b／管理者が受入前に導入|
|U5d|`tools/host_agent.py`・`tools/lanfs_host.py`、ゲスト不在中の操作口・raw SFS adapter/設定結線|380|U5b/P4a後、X6/P4bと直列／hostのみ|
|U5e|`tools/host_operator_socket.py`・`tools/remote_transport.py`、LOG購読・SFS操作/設定照会・接続先指定|350|X5/X13/U5a／X12/G4/P5は常駐clientへ|
|X7|`kernel/console/con_sink.c`、capture状態・回収|250|X0／x86 vmkernel・**PPC像変更**|
|X8|`kernel/console/console.c`、capture中mirror抑止・CUI画面維持|70|X7／x86 vmkernel・**PPC像変更**|
|X8a|`drivers/input/kbd.c`、capture中の入力期限・閉鎖|120|X7／x86 vmkernel|
|X8b|`platform/powermac/console_log.c`、両flagの退避/復元をcapture所有者へ統一|100|X7/X8／**PPC像変更**、既存log側の二重復元を除去|
|X9|`sdk/kapi.json`、con_sink_capture末尾追加・生成|60|X7／ABI版更新、x86 kernel/SDK/userland一式|
|X10|新 `userland/shell/rshell_lan.c`、通知→実行→結果|360|X2/X8a/X9／shell.bin|
|X11|`userland/shell/rshell.c`、行頭調停・gui受理分岐・SFS制限|180|X10／shell.bin|
|X12|新 `tools/rshell.py`、薄いCLI/auto|220|X5／hostのみ|
|X13|新 `tools/remote_transport.py`、専用口client|250|X5／hostのみ|
|X14|`tools/rshell_serial.py`、send注入口|100|X13／hostのみ|
|G1|新 `userland/gshell/src/gserial_link.rs`、LAN状態/通知/結果|360|X0/X6／gshell|
|G2|`userland/gshell/src/gserial.rs`、parser/返答口共有|180|G1／gshell|
|G3|`userland/gshell/src/wm.rs`、自番号poll結線|60|G1/G2／gshell|
|G4|`tools/gserial.py`、transport差替え|180|X13/G2／hostのみ|
|P1a|新 `net/link_platform.h`、arch境界宣言|80|x86受入後／像不変|
|P1b|`net/link.c`、arch口への付替え、ne2000.h/idt.h/io.hをincludeしない|160|P1a／x86・PPC共通対象|
|P1c|新 `platform/pc98/link_platform.c`、既存tick/irq/idle実体|100|P1b／x86 vmkernel|
|P1d|新 `platform/powermac/link_platform.c`、tick_count/tick登録/lock/予算|260|P1b／**PPC像変更**|
|P1e|`net/nic.c`、PC98登録をplatform口へ分離|200|P1c/d／x86・**PPC像変更**|
|P1f|新 `platform/powermac/nic_gem.c`、GEM adapter|300|P1e／**PPC像変更**|
|P1g|`platform/powermac/lan_service.c`、link選択時poll/起動所有|280|P1f／**PPC像変更**|
|P2a|新 `platform/powermac/link_diag.c`、NOTIFY診断adapter|280|P2c／**PPC像変更**|
|P2b|`platform/powermac/lan_monitor.c`、cacheキー/診断分離|120|P2a／**PPC像変更**|
|P2c|新 `platform/powermac/link_log.c`、既存logリング送信|220|P1g/X6/P5-log／**PPC像変更**|
|P3a|新 `platform/powermac/rshell_link.c`、CLI入力/結果adapter|360|P2／**PPC像変更**|
|P3b|`platform/powermac/cli_boot.c`、行実行前後の結線|120|P3a／**PPC像変更**|
|P3c|`platform/powermac/shell_putchar.c`、capture/ESCC出力選択|140|P3a／**PPC像変更**|
|P4a|新 `tools/host_sfs_link.py`、既存backendへのSFS adapter|300|X6／hostのみ|
|P4b|`tools/host_agent.py`、SFS CALL宣言/応答結線|100|P4a／hostのみ、X6後に直列|
|P4c|`platform/powermac/hostfs_io.c`、SfsIo→host_*|300|P1/P4b／**PPC像変更**|
|P4d|`platform/powermac/hostfs_peer.c`、共通ALIVE/peer参照|100|P4c／**PPC像変更**|
|P5-t|新 `tools/ppc_link_transport.py`、共通agent client一本|250|X13／hostのみ、各台本はこれを使用|
|P5-log|`tools/ppc_lan_host.py`、LOG表示と既存CLIのagent接続|250|P5-t/X6／P2cより先、hostのみ|
|P5-k/r|`tools/ppc/kboot_run`・`realhw_run`を各1ファイル別依頼|各250|P5-t／P2受入前|
|P5-sc/sh|`tools/ppc/sc_run`・`ppc_sh_output`を各1ファイル別依頼|各250|P5-t／P3受入前|
|P5-sd/fs|`tools/ppc/sd_run`・`tools/ppc_hostfs.py`を各1ファイル別依頼|各250|P5-t／P4受入前|
|P5-usb|`tools/ppc/usb_run`、共通transportへ|250|P5-t／当該台本の節目より先、遅くとも既定切替前|
- 結線小差分も別依頼: con_sink.h、shellヘッダ、gshell module宣言、x86 build、PPC build、PPC mailboxのlock/heap結線、PPC boot/hostfs_boot、console stream口、host_agentのLOG重複記録をそれぞれ1ファイル・各150行以下。共通tick予算のarch口も1ファイルずつP1へ含め、無制限stubで完了にしない。
- 最初のlink像はP1＋LOG（P2cまで）を合わせる。FB帯に「HELLO送信」「HELLO確立」の段階印を追加し、ログ・診断のない像を出さない。P1＋LOG/P2はlink診断像、P3はlink CLI像、P4はlink HostFS像として独立受入。機能不足の像で既定PLN1像を置換しない。P5後、PPC buildの既定をlinkへ変更する1ファイル段（100行以下、**PPC生成物変更**）で旧wireを廃止。
- X0→X1–X9→X10–X14→G1–G4を先に出してx86受入。P1→X6のLOG受け口/P5-logの表示→P2c→P2a/b→P3→P4→既定切替。P5-tと各台本は対応節目の前に完了。共通ファイルは直列、PPC設計の正典3票とnetwork正典への契約更新はPMが各1ファイル段で行う。
- 各段の試験は `tools/tests/test_remote_<段>.py`、台本は `tools/accept/remote_<節目>.py` に各400行以下で別依頼。CHECKには現行のcovers等で入力を宣言、変更行の既存mutation anchorを検索して移設。対象build/触った試験--mutate/lint、ABI段clean→all、取り込みCIとfull-checkの役割はROLES §0どおり。
- PLN1試験（test_lan_p0/p1/p21/p22/p23/p29、test_lan_terminal_host、test_ppc_terminal、test_ppc_terminal_input、lan_p24_fake.c）は既定切替まで保持し、対応link試験の合格後に切替と同時に退役する。
- 像予算: x86追加RAMはcapture管理＋shell結果buf8KiB＋gshell FIFO8KiB＋転送buf（各1400B程度）、既存sink8KiB/通知16KiB/link ringを再利用。PPCは既存PLN1削除との差とlink/mailbox/8handle分をmapで実測し、kboot/CLI/HostFS各像を別比較。P1でheap高水位をmapとともに記録して1MiB以内を確認し、link像にはlink_txbufだけが残りPLN1の静的TX/RX領域がないことをmapで確認する。capture増量時は各bufの実容量で再計上する。text/FD圧縮増分を未測定の数値で保証しない。

## 7. 受入（各行に試験と変異、今回はすべて未実施）
H=host製品コード試験、N=NP21/W LGY-98/NE2K **上のlink**、R=Ra266 e82557、Q=QEMU **7450と7455両方**。PPC移行の各段の受入はH/Q。G4は移行前に現行PLN1版で基準確認し、既定をlinkへ切り替えた後にG4実機の確認を1回行う（U4）。U5a–eのサービス導入を各節目の前提とし、受入台本はprobeで常駐/peer/READYを確認するだけで起動・停止しない。probe は agent の commit と設定の要約も返し、台本は作業木の HEAD と照合して、違えば前提不足で止める (A2、写し忘れの古い agent で受入しない)。不在は前提不足として記録。障害注入はHの隔離fixture、N/R/Qは管理者が別途行い台本は観測する。変異は固有anchorでruntime RED。
|項目/段|試験（合格観測）|変異|
|---|---|---|
|常駐 U5|H/N/R/Q: ゲスト不在probe・CLI再接続・LOG/SFS併用、設定RO/peerとNIC分離。管理者によるサービス再起動後READY回復、台本に起動停止なし。|ゲスト待ちで操作口停止、またはallow-write既定を反転。|
|既定操作 X|H/N/R: auto ver/peek→echo $?、LAN上echo/EOT一致、serial TX0。NIC/agentなしでserialの同台本が完了。|LAN選択を反転しUARTへ出力。|
|二重実行 X|H/N/R: NOTIFY/ACK/結果を各喪失、agent終了、submit直後接続断。送信後serial書込み0・実行数≤1・UNKNOWN。|送信後auto退避を許す。|
|通知隔離 X2|H/N/R: 全種readerと操作専用reader併走、再送/旧session/64枠溢れ。全種は操作を取らず欠落時timeout。|65535の予約番号除外を外す。|
|capture X7–11|H/N/R: serial半行/STOP/拒否/exit/owner終了/link断、両flag・readerを復元。3B見出し込みで1文字PRINTの2048件境界と次の1件、可変長/制御レコード混在を試し、実容量を超えた場合だけdrop失敗（予約領域分を補正）。|drop比較またはENDのflag復元を削除。|
|入力期限 X8a|H/N/R: capture中のhdprep空答え中止・script入力・filerが30秒期限でEnter/ESCを受け待ちを抜ける。serial RX停止・local STOP・終了後flag復元。|期限条件からcaptureを削除。|
|PPC共通console X7/X8/X8b|H/Q: ppc-kbootリンク、ログ/画面/両flagの退避復元・owner終了の最小回帰。|console_logで古いflagを二重復元。|
|READY N6/N7|H/N/R/Q: agent後起動・AGAIN/STALEで2秒再READY、last-id/状態照合、UNKNOWN後の遅延OUTで結果確定・旧boot/世代拒否。|再READY停止、またはUNKNOWNの遅延OUT破棄。|
|SFS/hsync X|H/N/R: --sfs-lanのscope、UART版拒否、既存serial SFS成功。既定切替前に通常/大量manifestの本文bytes/レコード数/占有量/dropを採取、不足時はcapture増量後に再合格。溢れを成功にしない。|LANでUART scopeを許可。|
|gui引渡し G|H/N/R: ACCEPTの200前にGUI未起動、readyで移行、起動失敗/結果喪失で再実行なし、GSERIAL→CUI復帰。|200確認前にGUI起動。|
|GSERIAL G|H/N/R: SOH/id8/end N・keys/watchdog/gs:!、他アプリHOST_NOTICE・前景なし・FIFO溢れ。serial版同台本も合格。|通知を65535で読む、またはLAN replyをUARTに出す。|
|帯域/共存 X/G|H/N/R: TIME/CLIP/既存SFSと1.5KiB peek併走、ACK遅延/AGAINでも有界進行。Rはserial/LAN時間・TX呼出数・T1c遅延採取、200ms目標（未達は原因記録）。|host_writeのAGAINでbusy loop、またはserial mirror抑止解除。|
|P1＋LOG基盤|H/Q: HELLO/ALIVE/TIME、BE CPUのLE wire golden、link断復帰、tick登録/tick_count・1poll予算/再入なし、旧PLN1選択像も起動。PPCでlinkをビルド、heap高水位≤1MiBとPLN1静的TX/RX不在をmapに記録。lan_service.c:202/:251のreset/PHY/pollが同じ関数の流用であることをdiffで示す。FBのHELLO送信/確立印と初期LOG受信を観測。|tick登録を外す、またはRX二重drain。|
|P2診断/log|H/Q: 5診断・PEEK境界/DSI・cache競合、初期ログ・drop・末尾喪失を表示、shell無しlink像でも成立。|PEEK PA範囲照合またはlog drop報告を削除。|
|P3端末|H/Q: ver相当の既存CLI/echo/失敗rc/出力超過、ESCCとの行分離・Ctrl-C退避、旧PLN1 CLI像の基準と機能照合。|LAN中ESCC複写を有効化、またはID重複実行。|
|P4 HostFS|H/Q: /host read/write/RO・CRC・1044B最大・1000ms期限・再起動旧FD STALE、WDATA/応答喪失時write一回。|新ridごとSFS重複cacheを消す。|
|P5切替/像|H/Q: linkのみでLOG/診断/端末/HostFS一巡、0x88B6 TX0、生成像/map/サイズ記録、旧バイナリのCLI/FS利用契約維持。共通変更はN/R最小回帰へ相乗り。|PPC buildをPLN1既定へ戻す。|
- x86はX/G統合→CI成果物→main直前N、Rはノート担当へ依頼。PPCはP1/P2/P3/P4/既定切替の各節目にH/Q、各節目の小段はまとめてよい。
- R1専用受入は通常不要（CPU/paging/exec/配置を変更しない）。PPC像の増大で配置変更が必要ならその段を別設計・R1相当受入へ分ける。像不変のhostのみ段はguest再配備不要、最終統合台本は省かない。

## 8. 未決・危険・反論
- ユーザー判断の未決: **0件**。FD容量超過時の除外決裁は既存票の関門を継承（実測後fm.mod→pcm.mod推奨）、現段で除外しない。
- 把握している危険: 既存どおり平文・認証なしの信頼LAN。操作口は任意コマンドを実行できる。ゲスト/agentのなりすまし、結果改変、通知溢れは残る。番号/boot/ID照合は認証ではない。
- 反論: **0件**。


## 確定 (2026-11-14、Opus 5.5 第 3 回 Approve 条件付き、PM が反映)
- A1: link 像では GEM の RX/TX も tick の枠で汲む。前景の pm_lan_poll は link 像の RX に触らない (§5 の C ブロック)。
- A2: probe が agent の commit と設定の要約を返す。台本は作業木の HEAD と照合する (§7)。
- A3: NP21/W の 8026 は今の向き (WSL の agent が loopback で listen) を保つ (§2)。
- 運用は VPN とローカルの中なので、操作口・frame の口の番地を制限する検査は置かない (ユーザー 2026-11-14「VPN とローカルで動いているのでセキュリティは気にしないで」。Opus の A4 は取り下げ)。
- A5: state-dir に世代・id を永続しない (§2 設定)。
- 受入で確かめる: 専用ユーザー os32-host が HostFS の root に書くときの所有者と権限、CAP_NET_RAW を AmbientCapabilities で渡すこと。

## 実装の前に PM が決めたこと (2026-11-14、X5 の停止を受けて)

### 操作口 (8027) の JSON の契約
- 枠: 4B BE 長 + UTF-8 JSON の 1 オブジェクト。1 レコードは 16KiB 以内。バイト列はすべて base64 の文字列 (`*_b64`) で運ぶ。
- 要求: `{"op": <名前>, ...}`。応答: `{"ok": true, ...}` か `{"ok": false, "error": <符号>, "detail": <文字列>}`。
- 誤りの符号: `BAD_REQUEST` (形・長さ・JSON の誤り)、`NOT_READY` (HELLO か READY が無い)、`BUSY` (実行中が 1 件ある)、`UNKNOWN_PENDING` (UNKNOWN の後に resume されていない)、`NO_SUCH_ID`、`INVALID_LINE` (設計 §2 の実行前の拒否)。

| op | 要求の欄 | 応答の欄 (ok のとき) |
|---|---|---|
| `probe` | なし | `agent_generation` (int)、`commit` (str)、`config` (`iface`・`root`・`allow_write`・`peer`)、`peer` (MAC か null)、`hello` (bool)、`ready` (`{"mode","last_id","state"}` か null)、`pending` (`{"id","state"}` か null) |
| `submit` | `mode` (`cui`・`gui`・`diag`)、`line` (str、設計 §2 の制限)、任意の `timeout_ms` (正の int、省略時 15000) | `id` (8 桁 16 進) |
| `result` | `id`、`wait_ms` (int、0 なら待たない) | `state` (`pending`・`done`・`unknown`)、`drop` (int)、`body_b64`、`complete` (bool: 長さ・drop・echo・EOT か end がそろったときだけ true) |
| `resume` | なし | なし (UNKNOWN の後の、利用者の明示の再開) |
| `notice` | `wait_ms` | `notices`: `[{"id": "!" か id, "body_b64"}]` (GSERIAL の非同期の `gs:!` など、届いた順) |
| `log` | なし | 購読を始める。以後この接続に `{"event":"log","boot","seq","drop_records","drop_bytes","flags","text_b64"}` を届いた順に流す。購読中の接続でも他の op を送ってよい |

- `result` の `wait_ms` の待ちと `log` の配信は、agent の select ループの中で行う。ソケット 1 本で agent を止めない。
- probe の `commit` と `config` は、agent が口を作るときに渡す (`OperatorSocket(addr, operator, info)` の `info()` が辞書を返す)。
- select ループへの結線の形 (X6 が使う):
  - `fileno_list()`: 待つ fd の一覧。
  - `on_readable(fd)`: その fd を読む。
  - `poll(now)`: `wait_ms` の期限を処理する。
  - `publish_log(record)`: LOG を購読者に流す。

### X6 の停止を受けて PM が決めたこと (2026-11-14)
- `RSH READY` の 200 の本文は 1 行で `gen <host_generation 10 進> svc <操作番号 10 進>\n` とする。ゲストはこれで host_notice_read の世代と番号を決める。
- `PDIAG OUT` は PowerPC の段 P2 で入れる。X6 では受けない (400 を返す)。X4 の Operator の diag も P2 で足す。
- 宣言長: `RSH OUT`・`GS OUT`・`PDIAG OUT`・`LOG` は、どれも末尾の語が WDATA の宣言長になる。
  - ホストの側 (host_agent.py の `_decl`) は X6 で対応する。
  - ゲストの側 (net/link_host_api.inc の `link_decl_len`) は、新しい段 X6g (上限 40 行、x86 vmkernel・PPC 像は P1 の後で変わる) で対応する。

### U5a の停止を受けて PM が決めたこと (2026-11-14)
- 設定の `backend` は `np21w` か `bridge` の 2 つだけにする。
  - `np21w`: NP21/W の NP2NETSOCK が `frame-endpoint` に接続しに来る (WSL)。
  - `bridge`: lan_bridge.py が `iface` の物理 NIC と `frame-endpoint` を中継する (Ubuntu のノート)。

### 実装の途中で PM が決めたこと (2026-11-14、X9b・X10・X11・G1・G3b・U5b・U5c・直し 1・2)
- boot-token: kernel が起動中に 1 回だけ決め、KAPI `boot_token_get` (slot 293、ABI v86、16B) で読む。共通の口 kernel/boot_token.c と platform の provider に分ける (POLICY_DEV §1)。pc98 は TSC・MAC・RTC、powermac は pm_gmac_boot_id。NIC が無ければ MAC の 6B が 0 になるだけで、誤りにはしない。要求行では小文字の 16 進 32 桁。
- capture の KAPI は `con_sink_capture` (slot 292、ABI v85)。
- GUI の終了通知の協調待ち: 起算点は終了通知を FIFO に積んだ時点。無進捗 1000ms・全体 5000ms で打ち切り、結果を送らずに GUI を解放する (ホストが UNKNOWN と判定)。GS の通常の送信にも同じ期限を当てる。非同期の gs:! の枠は 1400B、READY の本文の受け枠は 64B。
- gs:! の宛先: LAN を有効にした時は LAN、その後は直前の完全な要求の入力元。
- gshell は起動で serial の open の後に open_lan を呼ぶ (LAN が既定)。
- GSERIAL の wire は SOH と LF を含めて 130B まで (parser の上限 128B は不変)。
- gui の受理の後は running のまま。同じ boot の GUI の READY と `gs:! ready v1` の両方で done にする。
- LAN の exit は内部の戻り値 RSH_LAN_EXIT=2 で伝え、結果の送信と capture END の後に rshell を終える。
- U5b: NIC の重複のロックは `/run/lock/os32-host-agent-<iface>.lock` (試験は `--lock-dir`)。host_agent には `--config` で同じ設定ファイルを渡す。
- U5c: ログは journal。probe の commit は `/opt/os32` に置いた git の作業木から取る。
