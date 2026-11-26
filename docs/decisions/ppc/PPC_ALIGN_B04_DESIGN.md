> 出所: 本線の機械の ~/os32-tmp/run/ppc-align-b04/DESIGN.final.md (astra 起草・sol レビュー 2 回、2026-11-21 確定)。
> 状態: **確定 (2026-11-21)** — 票 TASK_PPC_ALIGN_X86 の B04 (LAN の運用・link の poll・rshell)。PowerPC の段 E が main に入った後に照合し直して実装に出す。末尾の「確定」の節は本文に優先する。

# B04　製品シェルから使う LAN と PowerMac の前景進行（詳細設計 r2）

## r1→r2 の変更

- P1-1　製品保守は一枠とし、USB と LAN の進行条件を独立させた。
- P1-2　枠を GEM 起動前に確保し、KBOOT ALIVE 直後に poll の所有者を切り替える。
- P1-3　像別の peer、READY、登録条件と段 E 取込時の照合点を表にした。
- P2-1　LOG、boot token、起動引数・rc、hostfs・USB・待ちの順を明記した。
- P2-2　再 bind と同一 session の再同期に別々の合否条件を置いた。
- P2-3　OUT の期限と世代、exit と GUI 引渡しの順を明記した。
- P2-4　製品と旧 PLN1 の試験を各担当へ割り当て、重複通知を試験入力に加えた。
- P2-5　機種ごとの受入と同一親 SHA の比較を明記し、容量と ATA の依存を整理した。

対象は段 E 後の製品像。比較基準は同じ段 E を含む x86。
B04 の成果は GEM の保守をシェルの寿命から外し、既存の共通経路で一巡させること。
LAN の wire、KAPI、常駐 CLI の削除はこの段の変更対象外とする。

## 1. 実物の確認

|役目|PC-98|PowerMac|引き継ぐ所有者|
|---|---|---|---|
|起動|sys/platform/pc98/kernel_boot_memory.inc の kernel_boot_lan の mailbox、module、PCI fallback|sys/platform/powermac/ppc_kernel.c の platform_boot_services の boot token と GEM、sys/platform/powermac/lan_service.c の pm_lan_poll の mailbox・adapter・start|装置発見は各機種、起動後の bind は net|
|周期登録|sys/platform/pc98/link_platform.c の link_platform_tick_register の PC98_TICK_LINK|sys/platform/powermac/link_platform.c の link_platform_tick_register の PM_TICK_LINK|sys/net/nic.c の net_bind の net_bind が一度だけ net_jiffy を登録|
|前景 RX|sys/net/nic.c の rx_service の有限 demux|同じ IO_SVC_NET_RX。別に GEM 状態機械を前景から駆動|共通 NIC|
|HELLO・reopen|sys/net/link.c の link_start_hello・link_resync の HELLO と再同期|同じ実体|net/link|
|切断・drop|net_unbind で NIC・RX を解除。再 bind の link_init で新 session|同じ実体。GEM 喪失時は lan_service.c から unbind|同一 session の保留破棄と epoch 更新は link_resync|
|結果・世代|userland/shell/rshell_lan.c の rsh_lan_flush・rsh_lan_poll|製品シェルも同じ通知・capture・結果送信|シェルと agent|
|idle と待ち|sys/platform/pc98/link_platform.c の link_platform_idle の idle_wait_step|sys/platform/powermac/link_platform.c の link_platform_idle の再入抑止付き待ち、同ファイルの保守処理 に USB と LAN 保守|待ちの入口と NIC の仕事を分ける|
|診断|link の HELLO と host サービスの結果を観測|sys/platform/powermac/lan_service.c の pm_lan_poll の TIME・LOG、同ファイルの診断処理 の PDIAG|wire は共通、GEM・PHY の観測は機種|

票・設計からそのまま変更量や合否条件に使えない点は次の 4 件。

1. docs/tasks/ppc/TASK_PPC_ALIGN_X86.md の 2. B — 揃えられる 2 重 は B04 全体を未着手とするが、docs/tasks/network/TASK_LAN_OPERATOR.md の 2. 分担 は P3 の一部と P5 transport・sd/fs の取込済みを記録する。実コードにも READY と結果送信がある。
2. docs/decisions/ppc/ARCH_DRIFT_AUDIT.md の B　揃えられる二重実装・別経路 の「link シェルで idle に登録」は追加の保守 callback の条件。共通 NIC の登録自体は sys/net/nic.c の net_bind から bind 時に行う。後者をシェル外へ移す変更は不要。
3. docs/decisions/network/LAN_OPERATOR_DESIGN.md の W2: credit・RX 予算・link 進行の確定 の「同じ ms の再呼出は demux のみ」と異なり sys/net/nic.c の rx_service 付近では最近 RX がある同一 ms にも poll できる。x86 実装の挙動を期待値にする。
4. docs/decisions/network/LAN_OPERATOR_DESIGN.md の 5. PowerPC を link に統合する の端末入力 feed の流用は製品の行実行には不要。userland/shell/rshell_lan.c の rsh_lan_poll 以降が NOTIFY から行を直接検証して実行する。PLN1 の行編集は旧像限定。

### 統合判断（POLICY_DEV §1）

- 同じ役目を探した結果、NIC の登録・前景予算、link のセッション、シェルの実行と capture に各一つの実体がある。
- x86 の各実体を製品 PowerPC からそのまま使う。既存の link_platform の名前や登録 ABI の置換は呼出元を増やすだけなので行わない。
- 残る物理差は GEM の準備・PHY・時計・DEC と PC-98 の NIC・tick slot。診断と旧端末のために製品側へ別の操作口を増設しない。

## 2. 起動から定常状態まで

|順|実行文脈|呼ぶ入口と次の状態|
|---|---|---|
|1|早期 boot|LOG リング登録（sys/platform/powermac/ppc_kernel.c の platform_boot_machine）。PCI 列挙後に boot_token_init → boot_token_get(lan_boot.boot_id)（同ファイルの boot token 初期化）|
|2|製品の boot 前景|LAN・USB の進行を無効にして製品 callback を一枠登録。登録成功・GEM 検出・LOG 登録成功が揃えば pm_lan_start(&lan_boot) を一度呼び、rc を保持|
|3|pre-user の 2 秒間|LAN 起動成功時は TB で直接 pm_lan_poll。callback 内の LAN はまだ無効|
|4|RUNNING の初回|mailbox → GEM adapter 登録 → net_start → bind 内で共通前景・tick 登録|
|5|KBOOT ALIVE 出力直後|直接 poll を終了し、LAN 起動成功時のみ登録済み callback の LAN 進行を有効化。root 判定より前|
|6|製品シェル|段 E の枠で rshell。行頭の協調点から READY・通知を進める|
|7|切断・復帰|既存 GEM 状態機械で unbind/reset。再び RUNNING で bind。共通 tick の登録数は一定|

- 2 の成功は reset 開始まで。agent 到達や HELLO を同期的に待たない。
- root 初期化成功後、loop・console とポインタ境界の準備に続き pm_hostfs_boot(info->tb_hz)、pm_usb_boot(info->tb_hz) の順で呼ぶ。hostfs の rc は USB の条件にしない。USB の進行は後者の復帰後に有効にする。
- callback は一回につき初期化済み USB と切替済み LAN をそれぞれ最大一回進める。root 失敗時にも LAN の進行条件は保つ。
- 製品構成は段 E の像選択で決める。旧診断像は従来の明示 poll を使える。
- 製品の root 失敗時も共通の前景待ちを回す。登録済みの GEM に対して末尾の直接 poll を重ねない。
- 登録失敗は rc を boot ログへ出し GEM 起動を見送り、ローカル起動を続ける。USB 初期化は通常どおり行い、枠不足時の USB 周期保守は利用不可と記録する。追加登録はしない。
- 成功時だけ登録済みと扱う。PHY の復帰・シェルの再起動・再 READY は再登録の理由にならない。

## 3. 実装境界

以下は内部 C 宣言。製品の一枠には機種内の pm_link_service_once を使う。

~~~c
int pm_lan_start(const struct pm_gmac_boot *boot);
void pm_lan_poll(u64 now);
void pm_link_service_once(void);
int pm_hostfs_boot(u32 tb_hz);
void pm_usb_boot(u32 tb_hz);
void idle_wait_step(void);
int idle_work_register(void (*work)(void));
int net_adapter_register(enum NetNicId id, const struct NetAdapter *adapter);
int net_start(enum NetNicId choice, const u8 peer[6]);
void net_unbind(void);
int link_platform_tick_register(void (*fn)(void));
void net_jiffy(void);
void link_platform_idle(void);
~~~

- pm_lan_start は static な lan_boot のポインタを s.boot に保持する。時計失敗は clock_rc、識別・PCI 検査失敗は LAN_ENODEV、GEM I/O 失敗はその rc。LAN_OK は RESETTING に入ったことを表す。
- pm_lan_poll の now は TB の生の 64 bit 整数。jiffy や ms を渡さない。
- pm_link_service_once は LAN 進行条件成立時に TB を上位・下位・上位の安定読みで得て pm_lan_poll を一回呼ぶ。
- 既存の再入ガードを callback 全体に掛ける。LOG・TIME の待ちを通っても保守を二重に実行しない。
- 同じ callback の pm_usb_poll は USB 初期化完了だけを条件とする。
- idle_wait_step は IRQ 許可かつ割込み外で idle_work_run を呼び、最近の RX がなければ _idle する。製品の root 失敗後もこの前景待ちへ入る。
- idle_work_register の非零は失敗。§2 の順序によりこの時点で DMA は未起動。
- net_start の peer は従来の固定 MAC または未指定の 0。登録・bind 失敗は現在の rc を記録する。
- link_platform_tick_register は slot の登録結果を返す。net_bind は tick 登録失敗を OS32_ERR_BUSY として上へ返す。
- net_jiffy は割込み側の有限な進行。機種 callback から重ねて呼ばない。
- link_platform_idle は各機種の割込み許可状態に応じた既存の待ち。装置の生 RX を読まない。

## 4. link と rshell の寿命

~~~c
void link_init(const u8 my_mac[ETH_ALEN]);
void link_tick(void);
int rsh_lan_poll(void);
int rsh_lan_execute(const char *line);
int rsh_lan_flush(unsigned int id, unsigned int dropped);
void rsh_lan_reset(void);
~~~

- bind が MAC と queue を初期化して link_init。link のタイマは jiffy の差で評価する。
- 再同期は保留 handle・送信を失効させ、同じ session の次 epoch で HELLO を開く。
- session 未成立、epoch の周回または rid 枯渇時は新 session。旧 epoch の応答を新 handle の完了に使わない。
- NIC の poll と demux と link_tick は sys/net/nic.c の rx_finish・rx_service・net_jiffy の排他と持越しを使う。
- 前景の IRQ 禁止中または IRQ 文脈では schedule のみ。有限の work budget と tick rescue の既存機種条件を保つ。
- 同一 ms の活動中 RX の追加 poll で link_due を新規に立てない。無通信時の空回りも増やさない。
- READY は boot token・mode・直近 ID・状態を通知し、応答から host_generation と service を検証する。
- 前景の行頭で rsh_lan_poll。正しい世代・予約 service の通知だけを一件取得する。
- PROBE は READY、EXECUTE は形式・ID を検証。重複 ID は OS32_ERR_EXIST、異世代は OS32_ERR_STALE。
- capture 開始後、通常行は running を記録して一度実行する。exit は行実行を通さず結果を flush する。
- execute の戻り値はシェル引渡しの判定。コマンド終了値はシェルの通常の終了値経路に残す。
- 通常結果は capture を freeze・回収して長さと dropped 付き OUT を送る。GUI は ACCEPT 成功 → capture END → handoff の順。ACCEPT 失敗では起動しない。
- OUT は同じ boot・世代で結果だけ再配送する。成功または HTTP 400 で保留解除、期限 30 秒または再試行 12 回で破棄。READY の世代更新でも旧世代の保留を破棄し ID 状態を初期化する。
- 通知なしの AGAIN は通常の待ち。READY の再試行間隔は 2 秒。退出時は既存 reset で通知の寿命を閉じる。
- link epoch、agent 世代、boot token、操作 ID を別々に照合する。link 再確立だけで旧操作の成功を推定しない。
- E5a/E5/E6（2b8fbc032）の UART または LAN の判定と常駐側 sfs_run_child を使う。B04 で rshell に機種分岐を追加しない。

## 5. 診断・旧像・容量

現 main（04fa184ed）と wip/shell-align-e5（2b8fbc032）の像選択を基準にした B04 の配線。PowerPC の段 E は main 未取込。段 E が main に入った時に SHA を埋めて確かめ直す（取込 SHA 未定）。製品の像選択が変われば peer の準備・選択と PDIAG 抑止も一緒に移す。

|像|コンパイル条件|peer|READY の持ち主|機種保守 callback|
|---|---|---|---|---|
|製品 kboot-link-sh|KBOOT_LINK=1, KBOOT_SH=1|起動時 pm_hfs_bind_peer、毎 bind の net_start に pm_hfs_link_allowed_peer()|rshell の cui。PDIAG は抑止|製品用一枠|
|link 診断 kboot-link|KBOOT_LINK=1, KBOOT_SH=0|net_start に 0（自動）|PDIAG の diag|なし、明示 poll|
|旧 PLN1 kboot-sh|KBOOT_LINK=0, KBOOT_SH=1|boot->peer_mac を準備し PLN1 で使用|PLN1 の端末状態。link READY なし|従来の pm_terminal_work_register|
|旧 PLN1 の非 sh 像|KBOOT_LINK=0, KBOOT_SH=0|boot->peer_mac|link READY なし|なし、明示 poll|
- shell の無い link 診断像は PDIAG と既存 5 命令を前景で処理する。
- PLN1 の TERM_OPEN/IN/OUT/STATE は既存の明示選択像の回帰試験だけで観測する。
- PLN1 の端末実装と旧 kboot 5 像の削除は、最後の常駐 CLI 撤去と同じ段にする。
- 製品の capture、mailbox、handle 数、RX queue、TX ring の容量は既存値。新しい静的フレームは 0 個。
- x86 の text/data/bss の増分見積りは各 0 B。比較には同じ設定・toolchain・B04 の親 SHA（段 E 取込済み）を使う。
- PowerPC の常駐 text の差は概算 0.1–0.5 KiB、状態はフラグ数語以内。機種の製品保守は計一枠。
- 製品保守の登録 rc を記録し、失敗時は §2 の処置に従う。
- map の text/data/bss と heap 高水位を段 E の同一構成と比較。P1 の heap 1 MiB 以内と link 像の PLN1 静的 TX/RX 不在を確認。
- 圧縮長と FD 使用量は生成物の実測値を記録。未測定値を受入値にしない。

## 6. ファイル別の段

変更行数は追加・削除を合わせた概算。前半を依存順にまとめてビルドする。

|段|担当|ファイル|種別・行数|内容|
|---|---|---|---|---|
|1|PowerPC|sys/platform/powermac/link_platform.h|変更 5|機種内 製品 callback と進行条件 の宣言を製品 link から参照可能にする|
|2|PowerPC|sys/platform/powermac/lan_service.c|変更 10–20|像別の固定 peer 準備・選択と PDIAG 抑止を §5 に合わせる|
|3|PowerPC|sys/platform/powermac/link_platform.c|変更 15–30|TB 読みと再入抑止を 製品の一枠で共有し、USB・LAN を個別に進め、旧像入口は必要な構成だけに残す|
|4|PowerPC|sys/platform/powermac/ppc_kernel.c|変更 25–45|登録の時期と成否、USB、root 失敗後の待ちを結線|
|5|試験|test_net_link.py (旧版 5bc3c6ced683)、test_net_rx_work.py、test_remote_x11.py、製品 QEMU 台本|変更|§7 の観測と変異。test_lan_terminal_host.py (旧版 677c00ca1f6b) は旧 PLN1 専用で維持|
|6|本線|docs/decisions/network/LAN_OPERATOR_DESIGN.md|変更 5–10|同一 ms の活動中 poll、保守 callback の実際の所有者を反映|
|7|本線|docs/decisions/ARCH_COMPAT_CONTRACT.md|変更 2–4|§9 に NIC・周期源の物理差を記録|
|8|本線|docs/tasks/network/TASK_LAN_OPERATOR.md|変更 2–5|製品受入ログを P3⑥・P5 の該当項目へ対応付け|
|9|本線|docs/tasks/ppc/TASK_PPC_ALIGN_X86.md|変更 1–3|受入を満たした SHA と像を B04 に記録|

新規翻訳単位は 0。段 1–4 の途中では旧入口を残し、呼出元の移行と同じ統合で整理する。

## 7. 受入と変異

|環境・条件|観測する合格条件|対応する一行変異（各試験で RED）|
|---|---|---|
|test_net_rx_work.py・再 bind|保留 handle を作り unbind → 再 bind。登録成功は通算一回、新 session の HELLO、旧 handle は失効|unbind で tick_registered を消す|
|test_net_link.py・再同期|保留要求を作り link_resync。同一 session の次 epoch で保留が消え、旧 epoch 応答で新要求が完了しない|受信の epoch 比較を常に一致にする|
|test_net_rx_work.py・前景|IRQ 禁止中 poll 0、許可後は有限、活動中の同一 ms に RX が進む|活動中の追加 poll を省く|
|test_remote_x11.py・結果喪失|OUT 応答を落とし同じ boot・世代で同一 ID の OUT 再配送、実行一回|結果の再配送を省く|
|test_remote_x11.py・重複通知|同じ世代・同じ ID の EXECUTE 通知を再投入し EXIST、実行一回|重複 ID の拒否を外す|
|製品 QEMU 台本・再起動|新 boot の READY 後に旧 boot の OUT を投入し拒否、新操作に結果が付かない|採取 fixture に旧 boot の成功を混入|
|製品 QEMU 台本・登録失敗注入|非零 rc のログ、GEM start・DMA 開始 0 回、ローカル起動継続|登録失敗でも start へ進む|
|NP21/W・LGY-98|HELLO、cui READY、echo、失敗後の終了値、次の READY と /host read|flush の OUT 長を 1 増やす|
|PowerPC CI・製品 QEMU 台本・7450/7455|各製品像で同じ操作列。ESCC の入力行と LAN 結果が混ざらない|LAN capture 中に serial 複写を許す|
|QEMU・前景|シェル待機中、コマンド実行中の協調点、シェル再起動後に LOG/HELLO が進行|保守登録を最初のシェル起動まで遅らせる|
|QEMU・診断|診断像で 5 診断、製品の送信記録で 0x88B6 が 0 件|採取 fixture に PLN1 送信を一件加える|

- host の変異は各 fixture で RED を確認。製品の登録失敗は QEMU 台本で枠不足を注入し、同じ条件で start を許す変異を検出する。通常像のゲスト受入も行う。
- agent は常駐を probe し、版・peer・mode を記録。agent の起動停止を受入台本に加えない。
- 切断直後は UNKNOWN を許す。再 READY を観測し、結果の再送と行の再実行を実行数で区別する。
- QEMU の保存確認は /host の既存許可領域へ一度書き、再起動後に内容一致と旧 handle の失効を確認する。
- build_compare の x86 関数差の許容集合は空。段 E 自体の差を B04 の差分に混ぜない。 同一 toolchain・設定で B04 の親 SHA の成果物と比較する。
- 順序は段 E の製品シェル → B04 の結線 → host → NP21/W・QEMU → 票更新。
- lz4 と ATA は B04 の依存にしない。段 E 製品像の容量と圧縮の要否は PPC_ROOT_LZ4_DESIGN で扱う。
- 同じ起動ファイルに変更が重なる場合だけ直列に統合する。ATA 未着なら製品シェルと既存 root で LAN を受け入れる。
- 既定切替を伴う節目は LAN_OPERATOR の合意どおり、切替後 G4 実機で一回確認する。

## 8. 未決と完了基準

ユーザー判断を要する未決は 0 件。
PowerPC CI の 7450・7455 と製品 QEMU 台本のログ、x86 の NP21/W・LGY-98 操作結果、host 通常・変異結果を受入 SHA に結び付け、同一親 SHA の成果物との map・build_compare を添える。
製品で再起動前後の READY、結果・終了値、保存がそろい、旧像の端末が動き、x86 の関数差が 0 の時点を B04 完了とする。

反論なし。

## 確定 (2026-11-21、sol 第 2 回「条件付きで可」、PM が反映)
以下は本文に優先する。
- **P1 (§4・§5) GUI の引渡しの契約**: 共通の通知・OUT・exit と、常駐版だけの ACCEPT・GUI の引渡しを分ける。今の製品の APP (SHELL_REMOTE_APP、userland/ppc/sh/link_profile.h) では rsh_lan_execute() が常に 0・rsh_lan_handoff() が OS32_ERR_NOSYS で ACCEPT の道に入らない (userland/shell/rshell_lan.c の 512–525 行付近)。ACCEPT・GUI の引渡しの受入は常駐版 (PowerPC の段 E の後の製品のシェル) に割り当て、段 E の前の製品の APP には当てない。
- **P1 (§5・§6) 旧 kboot-sh の peer**: 旧 kboot-sh の行にも `pm_hfs_bind_peer(boot->peer_mac)` を明記する (今の KBOOT_SH は PLN1 でも呼ぶ、sys/platform/powermac/lan_service.c の 233–235 行付近)。旧い像の /host の読み取りを退行の条件に加える。常駐 CLI と旧 kboot の像は最後に外すまで動くこと。
- **P2 (§6・§7) 再 bind の試験の置き場**: 登録 1 回と link_init() の呼び直しは今の NIC の fixture、HELLO の session の更新と旧い handle の失効は実の link を組む試験に (net_rx_work_host.c の link_init() は空の stub なので観測できない)。
- **P2 (§7) 変異の意味**: 旧い boot の OUT の拒否と PLN1 の送信 0 を採取のデータに混ぜる物は「検査器の自己試験」と明記し、製品の側の 1 行の変異と対応する観測点を別に指定する。
- **実装に出す前**: PowerPC の段 E が main に入った SHA で、製品の像の選び・peer・READY の持ち主を照合し直す (段 E の取り込みまで実装に出さない)。
