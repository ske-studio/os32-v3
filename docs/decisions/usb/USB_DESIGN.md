> 出所: `~/os32-tmp/run/usb/DESIGN.final.md` (Claude 起草、codex astra のレビュー 7 回 (ユーザーが毎回論点をチェック) で確定、2026-11-09)。段の状態は票 TASK_USB が持つ。
> 状態: **現行 (2026-11-09)** — PCI の USB ホスト (NEC uPD720101: EHCI 1 + OHCI 2) の土台の確定設計。CPU に依らない共通のコード (後で PowerPC でも使う)、FreeBSD の手順を写し、シングルタスクに置き換えた。

# USB の土台 — 詳細設計 final (uPD720101: OHCI ×2 + EHCI ×1)

- 状態: **確定** (2026-11-09、astra の第 7 回の指摘 4 件を PM が反映、末尾の節)。r7: 第 7 版。r6 + REVIEW.r6.md の反映と、HC の故障の簡素化 (PM 決定)。r6: 第 6 版。r5 + REVIEW.r5.md の反映 (r5 までの表は下に履歴として残す)。r5: r4 + REVIEW.r4.md の反映。失敗・取り外し・番地・再試行は FreeBSD の作りに沿わせた (r3→r4 以前の表は履歴として下に残す。そこに出る番地 0 の使用権・P_Q・冷却の記録などは r5 で廃止)。**設計のみ、実装の開始は未承認** (PM 決定)。
- ユーザー決定の正は `~/os32-tmp/run/usb/DECISIONS.md`。前提: シングルタスク・協調切替、9801 は見ない、整数演算、[C1]〜[D3]・[HW2]・[HW3]・[L1]、1 機能 1 ファイル。
- 手本: `FB:` = FreeBSD `sys/dev/usb/` (freebsd-src main f22d6599、手元 `~/os32-tmp/ref/freebsd-usb/src`、BSD-2)。仕様は OHCI 1.0a・EHCI 1.0・USB 2.0。**uPD720101 はデータシート Renesas S16265EJ5V0DS** (p.1・p.3: 最大 5 ポート、OHCI#1 = ポート 1/3/5・OHCI#2 = ポート 2/4、PCI 信号は 5V 耐性 — レビューが確認、本稿の筆者は未読)。詳細のユーザーズマニュアル S16336E は未入手 (手に入れば EXT レジスタ 0xE0/0xE4 を確かめる。それまでは読むだけ・書かない)。
- OS32 の表記は main 46c663cc の `path:line`。

## r6→r7 の変更 (指摘は REVIEW.r6.md の R6-01…。ユーザーの方針 2026-11-09「基本的に重くなければシングルタスクの前提で進めてください。重いものは判断します。」)

| ID | 直した所 |
|---|---|
| R6-01 | **§4-2-2 を簡素化 (PM 決定)**: HC の故障は 1 本の入口 `usb_hc_dead(hc, why)` で即時に処理する。HC を止める書き込みをして (確かめが取れなくても先へ)、その HC の DMA のメモリ (HCCA・ED・TD・転送の塊) は全部隔離して二度と使わず、配下の装置とクラスを全部「無い」にして入力・ブロックに 1 回だけ知らせ、その HC は再起動まで使わない。探索の途中・待ちの途中・前景の同期待ちの途中でも同じ入口。停止の状態機械 (HC_FAULT〜HC_FAILED)・終点の処理・探索の終わりを待つ根の回収 (r6 の R5-02 の経路) は削除した。試験は入口 1 本の G1 にまとめた |
| R6-02 | §4-6-2・§7-2: unsetup 中の xfer は `hc->unsetup_list` に載せ、`usb_service` が毎周 `usb_xfer_unsetup_step` で進める。基準 frame は `x->unsetup_frame` |
| R6-03 | §4-4-1 A4/A5: FB の `usbd_set_config_index` (usb_device.c:671-790) と同じく、構成の取得から SET_CONFIGURATION までを 1 回の「設定の試み」とし、失敗したら `cfg` を捨て (:787-790)、1 回目なら RE → 成功したら A4 から取り直し、2 回目の失敗は構成なしで続ける (:2019-2039) |
| R6-04 | §4-4-1 RE: RE1 (reset) の失敗も RE4 の失敗と同じ内側の再試行の出口へ (FB:usb_request.c:2195-2199 → :2239-2246) |
| R6-05 | §4-4-1 RE: RE3 の SET_ADDRESS の後に 10ms の締切 (A1 と同じ、FB:usb_request.c:1660-1662)。A2 から入った RE が成功したら A4 へ (FB:usb_device.c:1950-1961 は DEVICE の取得を繰り返さない) |
| R6-06 | §4-4-1 A4・§4-4-2・F8l: 切った長さで取り直した後、`cfg` のヘッダの wTotalLength をその長さに書き換える (FB:usb_request.c:1386-1387)。検査は書き換えた後の値で行う。末尾が記述子の途中で切れたら既定の「境界を失った」で構成全体を拒否 |
| R6-07 | §11 F8a: 変異を「A1 の SET_ADDRESS の失敗を ERR にする」に直した |
| R6-08 | D9 を R6-01 の決定に合わせて書き直した (r6 の R5-02 の置き換えは削除済み)。§4-4-1 A2: LS・HS の 18B の取得は失敗したらもう 1 回 (FB:usb_request.c:2111-2113) |

## r5→r6 の変更 (指摘は REVIEW.r5.md の R5-01…。方針は r5 と同じ: FreeBSD の手順をそのまま写し、スレッド・ロック・sleep は持ち込まず順だけを写す)

| ID | 重 | 直した所 |
|---|---|---|
| R5-01 | P1 | §3-1・§4-6-2: xfer は ED/TD を **2 組** 持ち、start のたびに交互に使う (FB:ohci.c:1395-1399・2465・2493-2497 の `curr_dma_set`)。done から直ちに再 start しても HC が持っている前の組は上書きしない |
| R5-02 | P1 | §4-2-2・§4-4-1: HC の故障では FB の `usb_detach` (controller/usb_controller.c:194-224) と同じく、**探索の新しい走査を止め (`no_explore`)、進行中のポートの処理を誤りで終わらせてから** (要求は HC≠RUNNING で即時に誤り → ERR → 子の free → `cfgbuf` は A4 を出るときに返る)、根の子をポート順に free する (`usb_bus_detach` :427-448 → `uhub_detach` :1771-1784) |
| R5-03 | P1 | §3・§4-6-2・§4-7: `usb_xfer_drained` (通知まで済んだ) と `usb_xfer_unsetup_done` (DMA 遅延の後に `xfer_free` まで済んだ) を分けた。class の sc と `usb_dev` は unsetup_done の後でだけ返す (FB:usb_transfer.c:1477-1502・1384-1392) |
| R5-04 | P1 | §4-4-1 P0: C_PORT_ENABLE を消し、接続変化が同時なら無視、enable=1 ならログだけ、**enable=0 なら restartcnt<5 で C_CONNECT を補う** (FB:usb_hub.c:1158-1180)。過電流 (:1143-1149) と最初の走査の C_CONNECT の補い (:1150-1156) を分けた |
| R5-05 | P2 | §4-6-2: `usb_xfer_start` で `did_dma_delay = 0` (FB:usb_transfer.c:1776-1777) |
| R5-06 | P2 | §4-4-1: FB の順 (SET_ADDRESS → `usbd_setup_device_desc`、usb_device.c:1908→1950) に戻した。設定の失敗は re-enumerate 1 回の後も失敗なら**無視して続ける** (:2013-2039)。D4 の説明を直した |
| R5-07 | P2 | §4-4-1 RE: re-enumerate の中身 (reset → 番地 0・MPS 8 → SET_ADDRESS(元の番地) → `usbd_setup_device_desc`、失敗なら 500ms 後に 1 回だけやり直す、usb_request.c:2149-2250) を段と締切で写した |
| R5-08 | P2 | §4-4-1 A4: CONFIG 9B → 長さ検査 (9 未満は誤り、512 超は 512 に切る) → 全長を取る → actual = 全長 → `cfg` へ写す → 塊を返す (FB:usb_request.c:1352-1390) |
| R5-09 | P2 | §4-4-1 R0〜R3: 子の free の後に状態を読み直す (FB:usb_hub.c:726-730)。reset の後も読み直し、C_CONNECT か切断なら 1 回だけ R0 へ戻る (:799-823 の repeat) |
| R5-10 | P2 | §7-2: RHSC と 100ms の監視は `needs_explore` を立てるだけ。進行中の走査は巻き戻さず、走査の終わりに旗が残っていれば次の走査を始める |

## r4→r5 の変更 (指摘は REVIEW.r4.md の R4-01…、方針はユーザー決定 2026-11-09「USBはBSDの手本を参考に出して良いです」「シングルタスクの前提を忘れずに。」)

r5 は、失敗・取り外し・番地・再試行の扱いを**独自の状態 (番地 0 の使用権・P_Q・P_RECLAIMED・DEV_ZOMBIE・F5 の待ち・冷却の記録) をやめて FreeBSD の作りに戻した**。手本の順 (何をどの順で止め、何を返すか) だけを写し、実行の場は OS32 の形 (割込みは要因の採取だけ、続きは tick か前景の `usb_service` の周、待ちは状態と締切を持って次の周で続ける) に置く。ロック・参照数・再入の旗の多重化は足さない (守るのは割込みと前景の間だけ: `usbp_irq_save` と `usb_busy` の 1 本)。

| ID | 重 | 直した所 |
|---|---|---|
| R4-01 | P1 | §4-6-1: 制御の列を FB の 2 通りに戻す。short_ok=0 は DATA→STATUS を繋ぎ (HC が STATUS まで進む)、集計は short で打ち切る。short_ok=1 は DATA の末尾を停止点にし (NextTD=0、alt_next=STATUS)、ソフトが STATUS から 1 回だけ再開する。再開は停止点の TD からだけ |
| R4-02 | P1 | §4-7: 装置の取り外しを FB の `usb_free_device` の順 1 本に (子の取り外しはハブの class の detach の中で、FB:usb_hub.c:1771-1784)。ctx の DETACHING・`usb_port_dev_gone` は無くした |
| R4-03 | P1 | §4-4: 番地は FB と同じく `usb_dev` の寿命で持ち、`usb_free_device` が返す (FB:usb_device.c:2332)。失敗したポートは子を返した後に PORT_ENABLE を落とす (FB:usb_hub.c:921-935)。「確認まで予約」は持たない (下の「手本の前提」) |
| R4-04 | P1 | §4-4: F5 の 500ms の再試行を無くし、FB の再試行 (`usb_alloc_device` 内の re-enumerate 2 回 :1954-1958、設定の再試行 :2019-2027、不正な enable 変化の再接続 :1168-1180 の USB_RESTART_MAX=5) だけにした。探索は HC ごとに 1 本の走査で直列 (FB の探索スレッド 1 本と同じ) なので、親の取り外しと子の列挙が並ぶことは無い |
| R4-05 | P2 | §4-4・§3-1: 構成の 512B の受信塊は E6 の中で取り・写し・返す (E6 を出るときに必ず返す)。port ctx は持たない |
| R4-06 | P2 | §3: `usb_ctrl_async(dev, owner, ...)` に持ち主を足した (列挙は NULL、class は iface) |
| R4-07 | P2 | §4-6-2: DMA 遅延の基準 frame と 20ms の期限を xfer に戻した (`dma_delay_frame`・`dma_delay_deadline`)。切り離しを見せた後に設定 |
| R4-08 | P2 | §4-7: DEV_ZOMBIE を無くした。FB の `USB_STATE_DETACHED` (:2301) と同じ 1 状態で送信を拒む |
| R4-09 | P2 | §11: F14r は「ハブの detach が子を返さずに終わる」変異に、F6s は short_ok=1・結果 SHORT・STATUS 1 回を確かめる形に |
| R4-10 | P3 | §4-6-1・§4-6-3: FB の説明を直した。FB は IN の全 TD に R=1 (FB:ohci.c:1261-1266)、short_ok=0 では DATA→STATUS を直結 (:1340-1352、:1500-1523) し HC が STATUS まで進む。:861-868 は完了後の集計の打ち切り。よって「制御 IN の short でも STATUS」は FB と同じで、相違ではない |

### FreeBSD のスレッド・待ちの置き換え (シングルタスク)

| FreeBSD | OS32 の場所 |
|---|---|
| `usb_bus_explore` (探索スレッド、controller/usb_controller.c:363-418) と `usb_needs_explore` (usb_hub.c:2478) | `usb_service` の中の HC ごとの探索の走査 `hc->explore` (旗 `needs_explore` を RHSC・ハブの割込み pipe の完了で立てる) |
| `uhub_reattach_port` の `usb_pause_mtx` (usb_hub.c:792 等) と `usbd_req_reset_port` の待ち (usb_request.c:820-864) | 探索の走査の中の `port_step` が締切 (`deadline_ms`) を持ち、次の周で続ける |
| `usbd_do_request_flags` の同期待ち (usb_request.c:415) | `usb_ctrl_async` + 完了の通知で次の段へ (探索の走査はその間ほかのポートに進まない) |
| `usbd_transfer_drain` の `cv_wait` (usb_transfer.c:2142-2157) | `usb_xfer_drained(x)` を次の周で見る |
| DMA 遅延のタイマ (`usbd_transfer_timeout_ms` → `usb_dma_delay_done_cb`、usb_transfer.c:2991-3020・2537) と unsetup の `usb_pause_mtx` (:1386-1390) | xfer の `XS_DMA_DELAY` を `usb_service` が `HcFmNumber` の 2 進みで解く (tick が 10ms 刻みで 1125µs を測れないため、frame で測る。20ms の期限切れは HC の故障) |
| コールバックのスレッド (`usbd_callback_wrapper`、usb_transfer.c:2401) | `usb_service` の決まった位置で `done()` を呼ぶ |
| `usb_free_device` の `usb_proc_mwait`・`usb_wait_pending_refs` (usb_device.c:2346-2352) | 無し (参照を持つ並行の主体が無い)。各段は「その段の xfer が drained」を次の周で見るだけ |
| `usbd_enum_lock`・バスの mutex | 無し。`usbp_irq_save` (割込みとの間) と `usb_busy` (前景の `usb_service` と tick の再入) だけ |

### FreeBSD から変えた所 (理由は 1 行ずつ)

| # | 手本 | OS32 | 理由 |
|---|---|---|---|
| D1 | IN の全 TD に R=1 (FB:ohci.c:1261-1266) | 段の途中の TD は R=0、段の最後の TD だけ R=1 | 途中の TD の short で R=1 だと HC が同じ段の次の TD に進み、次の IN (umass の CSW) を data の緩衝に取り込む (OHCI 1.0a §4.3.1.3.5)。R=0 なら DATAUNDERRUN で ED が止まる |
| D2 | DMA 遅延は 1125µs の固定の待ち (FB:ohci.c:2542-2548、usb_transfer.c:2991-3020) | `HcFmNumber` が 2 進むのを見る、20ms の期限 | tick が 10ms 刻みで 1.125ms を測れない。frame の数なら HC の進みで直接測れる |
| D3 | マウスは SET_PROTOCOL(1=report) で HID 記述子を解く (FB:input/ums.c:592-603) | boot (0) の固定 3B | 段 1 は報告の解析器を持たない |
| D4 | 構成 0 に端点が無ければ次の構成を試す (FB:usb_device.c:2040-2046)、auto-install の構成選び (:2048-2081) | 構成 index 0 だけ (設定の失敗の扱いは FB と同じ: 再列挙 1 回の後は無視、:2013-2039) | HID・umass は構成 1 つで足りる |
| D5 | 文字列記述子を読む | 読まない (表示は VID:PID) | 段 1 の範囲外 |
| D6 | D0 でなければ D0 に移し BAR を書き戻す (FB:ohci_pci.c:98-114) | D0 でなければ断る | BAR に書かない規約 (`drivers/pci/pci.h:44`) |
| D7 | EHCI の BIOS 引き継ぎの期限切れでも続ける (FB:ehci_pci.c:579-590) | EHCI を使わない (CF を読み、0 なら OHCI は続ける) | OS32 は SMI との調停を持たない |
| D8 | (無し) | 段 1 でも EHCI を静止し CF=0 を書いて読み戻す | BIOS 等が CF=1 で残すと全ポートが EHCI に行く (段 1 は EHCI を動かさない) |
| D9 | HC の切り離しでは探索スレッドに detach を積み、進行中の探索の後に根から装置を順に free する (controller/usb_controller.c:194-224・427-448) | HC の故障は 1 本の入口 `usb_hc_dead`: 止める書き込みだけして DMA のメモリを全部隔離し、配下の装置は一括で「無い」にして上に 1 回知らせ、再起動まで使わない (§4-2-2) | PM 決定 (2026-11-09、簡素化): 故障した HC の DMA は止まった証拠が取れないことがあり、触ったメモリを二度と使わないのが要。装置ごとの順は要らない (DEVICE_STATES.md:16,31) |
| D10 | 構成は USB_CONFIG_MAX で切る (usb_request.c:1373-1376)、装置数は bus の devices_max | 構成は 512B で切る (切り方は FB と同じ)、ハブ 8 ポート、装置 16 台 | 固定表で持つ (シングルタスクの小さい OS32 の範囲) |

### 手本の前提 (FB と同じ扱いにして、OS32 で守る理由は持たない所)

- 失敗したポートの PORT_ENABLE を落とせなかったとき、FB はログだけ出して続け (FB:usb_hub.c:929-939)、番地は `usb_free_device` で既に返している。OS32 も同じ。無効化に失敗するハブは壊れているので、その下の装置は次の変化で取り直す。
- `usbd_req_set_address` の失敗は無視し、新しい番地で次の要求が通れば成功とみなす (FB:usb_device.c:1908-1930)。

## r3→r4 の変更 (指摘は REVIEW.r3.md の R3-01…)

| ID | 重 | 直した所 |
|---|---|---|
| R3-01 | P1 | §4-6-1: `actual` は DATA の段の TD だけを数える (SETUP・STATUS は数えない、FB:ohci.c:891-929 の段別の集計) |
| R3-02 | P1 | §4-6-1: 最後の TD も含め short を先に判定し `short_seen` に持つ。結果は CC 誤り > short の許否 (SHORT / SHORT_NG) > OK、制御は STATUS の完了まで保留 |
| R3-03 | P1 | §4-6-2・§4-2-2: 冷却の記録を xfer から切り離し (`ohci_cool.notify`)、通知は「HCD の所有を外す 1 か所」(`usb_xfer_notify`) だけ。停止の終点も通知済みの記録には触れない |
| R3-04 | P1 | §4-4-1: 番地 0 の使用権を返す条件を「SET_ADDRESS の成功・無効化の確認・切断の確認・祖先ポートの無効化/切断の確認・HC の停止」に限る。ソフトの DYING では返さない |
| R3-05 | P1 | §4-7-2: 親が DYING のときの子の ctx の終点 `P_RECLAIMED` を足し、親の回収の条件に入れる |
| R3-06 | P1 | §4-4-1・§4-7-2: `usb_dev`・`cfg`・番地・interface を返すのは装置の detach だけ。`usb_port_fail` は装置があれば detach に渡し、終わってからポートの処理に進む |
| R3-07 | P1 | §3・§4-7-1: 転送に持ち主の iface (`x->owner`) を付け、iface を閉じるときは ep0 上のその iface の要求も取り消し・排出してから detach |
| R3-08 | P2 | §4-4-1: 隔離ポート `P_Q` の寿命と、使用権の解除・blocked の解除・FIFO の再開を 1 つの遷移 `usb_addr0_release` にまとめた |
| R3-09 | P2 | §4-4-1: 取消し・切断と、再試行してよい失敗を分ける。後者は 500ms 後に CCS を読み、繋がっていれば E1 へ |
| R3-10 | P2 | §4-2-2: `HC_STARTING` と起動の進み具合の旗。BM を上げる前の失敗は直接返し、上げた後は停止の状態機械へ。終点の処理は 1 回だけ |
| R3-11 | P2 | §2: import 表を `kbd_merge_event`・`kbd_merge_repeat`・`kbd_merge_release_src`・`mouse_ext_add`・`mouse_ext_release` に揃えた |

## r2→r3 の変更 (指摘は REVIEW.r2.md の R2-01…)

| ID | 重 | 直した所 |
|---|---|---|
| R2-01 | P1 | §4-8: HID 側で報告全体をスキャンコードの押下数に写してから差分 (0→1 / 1→0 だけを合成へ)。左右 Shift は同じ src の中で数える |
| R2-02 | P1 | §4-6-1: 転送ごとの ED (FB と同じ) と区切りの停止点 (段の最後の TD の NextTD=0、alt_next)、short 専用の分岐、STATUS の完了でだけ要求を完了 |
| R2-03 | P1 | §4-2-2: UE・時計停止 → 新規要求の禁止 → 非同期の停止の状態機械 (tick は待たない)。起動時の同期 stop は同じ段を前景で回す |
| R2-04 | P1 | §4-6-2: SKIP → リストから切り離し・見せる → 猶予 → done/解放 (FB:ohci.c:1629-1636・688-716・2542) |
| R2-05 | P2 | §4-6-2: 猶予の基準 frame と期限を外した ED ごとに持つ。期限切れは §4-2-2 の故障へ |
| R2-06 | P1 | §4-4-1: 失敗時の番地 0 は「ep0 の排出 + ポート無効化の確認」の後に返す。確認できなければその HC の列挙を止める |
| R2-07 | P2 | §4-4-1: 全失敗を `usb_port_fail` の非同期の後始末 (F1〜F5) に集め、待ち行列からの取消し・資源の返却・再試行の順を固定 |
| R2-08 | P1 | §3-1・§4-4・§5-2: 構成の受信は 512B の DMA 塊 (HC あたり 2 個)、wTotalLength・actual の検査、cfg への写しと塊の返却。池の見積りも直した |
| R2-09 | P2 | §4-4-2: 型ごとの最短長、DEVICE/CONFIG の actual を先に検査、境界を失った構成は全体を拒否、端点の方向・型・MPS・interval を選ぶときに検査 |
| R2-10 | P1 | §4-7: interface だけを閉じる入口 (`usb_iface_close_begin`) と装置全体 (`usb_dev_detach_begin`) を分け、状態も分けた |
| R2-11 | P2 | §4-7: 取り外しの最初にそのハブの探索・新規列挙を止める (`usb_hub_freeze`)、以後は既存の回収だけ |
| R2-12 | P2 | §3・§10: `kbd_deliver` を `kbd_deliver_internal` として公開する変更 (`kbd.c:281,344`、新 `kbd_internal.h`) を一覧に。共通の HID からは `usbp_input_*` を通す |
| R2-13 | P2 | §11: 偽 HC が取得済みの ED を frame の終わりまで持つモデル、F10 は池 3 頁から、R2-02/03/06 の分岐ごとに実行時に落ちる変異 |

## r1→r2 の変更 (指摘は REVIEW.r1.md の上から R1-01…)

| ID | 重 | 直した所 |
|---|---|---|
| R1-01 | P1 | §4-2: 割込み木・番兵・HCCA を DMA に作って見せてから OPERATIONAL に (FB:ohci.c:335-408→:424) |
| R1-02 | P1 | §4-9: EHCI は USBCMD=0 → HCH 確認 → HCRESET → 解除確認の順、期限付き (FB:ehci.c:203-225) |
| R1-03 | P1 | §4-1・§4-9: 段 1 でも EHCI を「引き継ぎ→停止→HCRESET→CF=0 を書いて読み戻す」まで持つ (`ehci_quiesce.c`)。OHCI はその後 |
| R1-04 | P1 | §3・§4-2・§8: `stop` は int を返し、読み戻し・期限・BM 停止・IRQ 排出を順に固定。失敗は DMA の全塊と部品を隔離 |
| R1-05 | P1 | §4-6: 完了は ED.Halted か (HeadP^TailP)&~0xF==0、TD ごとの CC/CBP で status/actual |
| R1-06 | P1 | §4-6: toggle は SETUP=DATA0・最初の DATA=DATA1・STATUS=DATA1、他は carry。pipe に保存、CLEAR_HALT でだけ 0 |
| R1-07 | P2 | §4-6: R (rounding)・short の残 TD・ゼロ長 (CBP=BE=0)・データ無しの STATUS は IN・bulk に SETUP/STATUS 無し |
| R1-08 | P1 | §3・§7: 要求・port reset・unlink 猶予を全部非同期の状態機械に。IRQ0 内では待たない (署名で固定) |
| R1-09 | P1 | §4-7: 取り外しを DYING→再 submit 禁止→停止・排出→DMA 猶予→class 解放→番地返却の順・持ち主で固定 |
| R1-10 | P1 | §4-4: HC ごとの番地 0 の使用権と待ち行列 (E2〜E5 を直列) |
| R1-11 | P1 | §3: interface ごとの `usb_iface` (class 状態・pipe・detach の責任) |
| R1-12 | P2 | §4-3・§4-4: ルートも含め reset 後 20ms、速度別の初期 MPS (HS=64)、E4 後に ep0 の MPS を検証して更新 |
| R1-13 | P2 | §4-4・§4-5: 記述子の長さ検査、端点の選び方、上限超過・確保失敗の戻し方、ハブの子の持ち主 |
| R1-14 | P1 | §7: tick も ISR と同じ要因採取 (`intr_collect`) を回し、ポートも監視。IRQ 無しでは MIE を落とす |
| R1-15 | P2 | §7-2: pending の取り出しは IF=0 で、再入旗・走査の継続位置を定義 |
| R1-16 | P1 | §5-3: MMIO は起動時の予約 (台帳・PT) と部品開始時の写像の 2 段、長さの根拠表、失敗時の戻し |
| R1-17 | P2 | §5-2: 池の同時利用者 (LAN・PCM・GeForce) と span、枯渇時の縮退、拡大は段 3 で 8MB 機を含め判断 |
| R1-18 | P2 | §3・§5: SETUP は DMA の塊、一般メモリは `usbp_mem_*`、資源ごとの持ち主と上限の表 |
| R1-19 | P2 | §6: 範囲付き `usbp_dma_sync` を初版から、所有の移る点を列挙。一貫は platform の実装条件 |
| R1-20 | P1 | §4-8: スキャンコード単位の押下数で合成 (`kbd_merge.c`、本体の鍵盤も入力元 0) |
| R1-21 | P2 | §4-8: マウスは入力元別の buttons を OR、移動量は飽和の累積を読み出しで消す |
| R1-22 | P2 | §4-8: マウスは boot (0) を選ぶ理由 (ums.c:598 は report)、短い報告・符号拡張・失敗時の扱い |
| R1-23 | P2 | §1-2・§4-1: IRQ の判断は probe (pci_bind_match.c:141-150)。D0 以外は断る、`pci_cfg_write8` を足す、引き継ぎの期限切れの扱い |
| R1-24 | P2 | §2・§10: 番人の対象を共通のコードだけに、常駐の踏み台・import/export・ロード順の担当ファイル |
| R1-25 | P2 | §4-9: 段 2 の受入は直結だけ、HS ハブ下の FS/LS (TT/split) は段 2b |
| R1-26 | P2 | §4-10: CSW の長さ・residue・status、STALL/phase error 別の回復、抜去、READ CAPACITY の限界、WRITE は後 |
| R1-27 | P2 | §10: 段 1 を受入単位 1A〜1E に分け、型と API を先に固定、`usbstat` を一覧に |
| R1-28 | P2 | §11: 失敗経路の試験と一意な変異、偽 HC の DMA 継続とイベントのモデル、A12 の変異を host へ |
| R1-29 | P2 | 冒頭・§4-9: S16265EJ5V0DS を引き、ポートの割り当てと 5V 耐性の「要確認」を外す |

## 1. 実物の確認

### 1-1. 使える口 (r1 から変更なし、行のみ)

| 口 | 実物 |
|---|---|
| PCI | `drivers/pci/pci.h:40-51` (cfg 読み、`pci_cfg_write16` は Command 専用)、`include/pci_bind_contract.h:18-26`、`include/pci_dev_contract.h:31-46` |
| DMA 池 | `mm/dma_pool.h` (`dma_alloc/dma_free/dma_mark_leaked`)、`include/memmap.h:267-268` (0x7F0000、64KB、1 回 32KB、span 16)、`include/dma_contract.h:18-30` |
| MMIO | `mm/paging.h:132` `paging_device_map`、前提の検査 `mm/paging_map.inc:129-145` (台帳の準備・共有 PT の存在) |
| 割込み・tick | `kernel/irq/irq.h:9-35`、`kernel/irq/tick.h:5-14`、`platform/pc98/tick_order.h` |
| 部品 | `kernel/module/module_runtime.h:24-34,63`、手本 `drivers/net/e82557/lan_module.c:50,98-107,222` |
| 入力 | `drivers/input/kbd.c:333` (IRQ から `kbd_deliver`)、`:344`、修飾の状態 `:365-377`、`drivers/input/mouse.c:83-128` |
| ブロック | `drivers/base/dev.h:36-119` |
| LE (バイト列) | `include/endian_le.h:36-61` |

### 1-2. 食い違い (9 件、r1 と同じ。#6・末尾を訂正)

1. HWDEV_CORE_API.md は `docs/archive/` だけ → [D4] で使わない (DEVICE_STATES.md:16,28-31 を使う)。
2. tick の slot が 7/7 で満杯 → `PC98_TICK_USB` を足し 8 に。
3. DMA 池は既存の利用者と分け合う (§5-2)。
4. 語単位の LE・MMIO の LE の口が無い → `include/endian_dma.h` (§6)。
5. `pci_driver` に progif の照合が無い → vendor/device で結び probe で progif を見る。
6. PCI 一般の MMIO 写像が無い。`paging_device_map` は台帳の準備と共有 PT を前提に断る (`paging_map.inc:131-140`) ので、写像の関数を足すだけでは動かない → §5-3 の 2 段。
7. 鍵盤・マウスに外からの入口が無い → `kbd_merge.c`・`mouse_ext.c`。
8. Ra266 の PCI スロット 2 本が使用中 (RA266.md:53-54) → ユーザー決定 (2026-11-09「その段で入れ替える」): USB の実機の受入の段に入るとき HighPoint を抜いて USB カードに入れ替える (物理操作はユーザー)。それまでは今の構成のまま (§11 の前提)。
9. NP21/W: RA266.md:199 は「PCI を再現しない」、源には橋 2 つ (`~/np21w-src/src/io/pci/98graphbridge.c:39`・`cbusbridge.c:39`、実行時の設定は未確認)。USB はどちらでも無い。

- (訂正) IRQ が使えない機能を断るのは `pci_bind` ではなく **probe の判断** (`drivers/pci/pci_bind_match.c:141-150` は probe の戻り値で打ち切り/次へを決めるだけ)。USB の probe は irq_line を理由に断らない (§7-3)。
- `pci_cfg_write8` は無い → `drivers/pci/pci.c` に足す (§4-1)。

## 2. 層の図と境目

(図は r1 と同じ。以下を足す)

- 置き場の 3 区分:
  - **共通** (`drivers/usb/` の HCD・中核・class): `usb_platform.h`・`endian_dma.h`・`endian_le.h`・`lib/kstring.h`・`drivers/usb/*.h` だけを include。
  - **常駐の踏み台** (vmkernel、pc98): `drivers/usb/usb_resident.c` — IRQ ×3・tick ×1 の踏み台、module 参照 (`MODULE_REF_IRQ/TICK/DMA`)、`lan_module.c:98-107` と同じ形で最大 3 登録・arg あり。
  - **部品の殻** (`usb.mod`、pc98): `drivers/usb/usb_module.c` (PCI 表・lifecycle)・`platform/pc98/usb_platform.c` (usbp_* の実装、kernel の関数を import で呼ぶ)。
- 番人 `tools/check_usb_portable.py`: 対象は共通の `drivers/usb/{usb_,ohci_,ehci_,hid_,umass_,scsi_}*.c` だけ (`usb_module.c`・`usb_resident.c` は除く)。`le32_t.raw` は `include/endian_dma.h`・`arch/*/arch_endian_dma.h` でだけ許す。
- import 表 `drivers/usb/usb_imports.def`: `dma_alloc/dma_free/dma_mark_leaked`、`pci_cfg_read32/write8/write16`、`pci_bar_map/unmap`、`usb_resident_irq_attach/detach`・`usb_resident_tick_set`、`kbd_merge_event`・`kbd_merge_repeat`・`kbd_merge_release_src`、`mouse_ext_add`・`mouse_ext_release` (§3 の公開口と同じ名前、R3-11)、`kmalloc/kfree`、`cpu_delay_us`、`usb_ms_now`、(段 3) `dev_register/dev_unregister`。export は `initialize` の 1 口。
- ロード順: PCI の結び・入力の初期化の後、シェルの前 (`build/packages.yaml` の boot 列、LAN の後)。

## 3. 構造体と部品の境目 (コード断片)

```c
/* include/usb_platform.h — 契約。共通のコードは IF の状態を仮定しない */
struct usb_dma { u32 bus; void *va; u32 size; u32 cookie; };
int  usbp_dma_alloc(u32 size, u32 align, struct usb_dma *out); /* 頁単位、[HW2]、失敗 <0 */
int  usbp_dma_free(struct usb_dma *d);
int  usbp_dma_quarantine(struct usb_dma *d);       /* 停止を証明できない: 起動の寿命まで返さない */
enum { USBP_TO_DEV, USBP_TO_CPU };
void usbp_dma_sync(struct usb_dma *d, u32 off, u32 len, int dir); /* pc98: 障壁だけ (§6) */
void *usbp_mem_alloc(u32 bytes);  void usbp_mem_free(void *p);    /* 一般メモリ (kmalloc) */
int  usbp_mmio_map(const struct usb_pci_fn *f, u32 bar, volatile void **va);  /* §5-3 */
void usbp_mmio_unmap(const struct usb_pci_fn *f, u32 bar);
u32  usbp_cfg_read32(const struct usb_pci_fn *f, u32 reg);
int  usbp_cfg_write8(const struct usb_pci_fn *f, u32 reg, u8 v);  /* LEGSUP の OS 旗だけ */
int  usbp_cfg_cmd(const struct usb_pci_fn *f, u16 set, u16 clear); /* MEM|BM の上げ下げ、読み戻し */
int  usbp_irq_attach(const struct usb_pci_fn *f, int (*isr)(void *), void *arg); /* -1 = 無し */
void usbp_irq_detach(const struct usb_pci_fn *f);   /* 前景 (部品の abort) だけ */
int  usbp_tick_attach(void (*fn)(void));
u32  usbp_ms(void);                 /* 単調、tick 刻み */
void usbp_udelay(u32 us);           /* 前景の起動・停止だけ。tick の中から呼ばない (§7) */
u32  usbp_irq_save(void);  void usbp_irq_restore(u32 f);
/* 入力への通知 (R2-12)。pc98 の実装は kbd_merge / mouse_ext を呼ぶ。IF=0 で呼ぶ */
int  usbp_input_src_alloc(int kind);          /* kind: KBD/MOUSE。戻り 1..4、尽きたら <0 */
void usbp_input_key(u8 src, u8 sc, int down); /* 押下数の 0→1 / 1→0 だけ (§4-8) */
void usbp_input_key_repeat(u8 src, u8 sc);    /* 繰り返しの make */
void usbp_input_mouse(u8 src, i16 dx, i16 dy, u8 buttons);
void usbp_input_release(u8 src);              /* 抜去: その src の押下・ボタンを全部離す */
```

```c
/* drivers/usb/usb_core.h — 中核と HCD の境目 (FB の usb_bus_methods / usb_pipe_methods に対応) */
struct usb_hc_ops {
    int  (*start)(struct usb_hc *);                 /* 前景、§4-2 / §4-9 */
    void (*kill)(struct usb_hc *);                  /* 止める書き込みだけ (§4-2-2)。待たない・確かめない */
    u32  (*intr_collect)(struct usb_hc *);          /* ISR と tick、IF=0、W1C して要因を返す */
    void (*scan)(struct usb_hc *, u32 budget);      /* 実行中の xfer の完了を確定 (FB ohci_interrupt_poll :1081) */
    int  (*xfer_alloc)(struct usb_hc *, struct usb_xfer *);  /* ED/TD を確保 (FB ohci_xfer_setup :2341) */
    void (*xfer_free)(struct usb_hc *, struct usb_xfer *);   /* DMA 遅延の後だけ呼ばれる */
    int  (*xfer_enter)(struct usb_hc *, struct usb_xfer *);  /* 列を組み ED を繋ぐ (FB *_start) */
    void (*xfer_close)(struct usb_hc *, struct usb_xfer *);  /* ED を外す (FB *_close → ohci_device_done :1614) */
    u16  (*frame)(struct usb_hc *);                 /* HcFmNumber */
    int  (*rh_port)(struct usb_hc *, u8 port, u8 op, u32 *st);
};
enum usb_hc_state { HC_OFF, HC_STARTING, HC_RUNNING, HC_DEAD };
struct usb_hc {                       /* 持ち主 = HCD (state は HCD だけが書く) */
    const struct usb_hc_ops *ops; volatile void *regs; struct usb_pci_fn pci;
    u8 nports, state, has_irq, started, needs_explore; u32 dead_why;
    struct usb_xfer *unsetup_list;                /* unsetup 中の xfer (§4-6-2、R6-02) */
    u32 fault_why, stop_frame, stop_deadline_ms;
    volatile u32 isr_pending;
    struct usb_xfer *active, *scan_next;          /* XS_ACTIVE と XS_DMA_DELAY の xfer */
    struct usb_dev *devices[USB_DEV_MAX + 1];     /* 番地 = 添字 (FB bus->devices[]、0 と 1 は根) */
    struct usb_explore explore;                   /* 探索の走査 (§4-4、HC に 1 本) */
    struct usb_dma dma[USB_HC_DMA_MAX];
};
enum usb_dev_state { DEV_ENUM, DEV_CONFIGURED, DEV_DETACHED };  /* DETACHED = FB USB_STATE_DETACHED */
struct usb_iface { u8 num, alt, src; const struct usb_class *cls; void *sc; };
struct usb_dev {                      /* 持ち主 = 親 (ハブの port、根は HC)。返すのは usb_free_device だけ */
    struct usb_hc *hc; struct usb_dev *parent; u8 port, addr, speed, state, depth;
    u8 ep0_mps; u8 ddesc[18]; u16 cfg_len; u8 *cfg;
    struct usb_xfer ctrl;             /* 既定の pipe の xfer 1 本 (FB udev->ctrl_xfer) */
    struct usb_ctrl_req *ctrl_q;      /* ep0 の要求の列 (1 本ずつ流す) */
    struct usb_iface ifs[USB_DEV_IFACE_MAX];
    struct usb_free_ctx *freeing;     /* usb_free_device の進み (§4-7) */
    u8 tt_hub_addr, tt_port;
};
enum usb_xfer_state { XS_IDLE, XS_ACTIVE, XS_DMA_DELAY, XS_CB_PENDING };
struct usb_xfer {                     /* FB struct usb_xfer。持ち主 = setup した者 (class か中核) */
    struct usb_dev *dev; struct usb_iface *owner; struct usb_pipe pipe;
    struct usb_dma buf; u32 len, actual; i8 error; u8 state, short_ok, did_dma_delay, open, curr_set;
    u8 unsetup;                       /* 0 / UNSETUP_WAIT / UNSETUP_DELAY / UNSETUP_DONE (§4-6-2) */
    u16 dma_delay_frame; u32 dma_delay_deadline_ms, deadline_ms;
    u16 unsetup_frame; u32 unsetup_deadline_ms;    /* UNSETUP_DELAY に入るときだけ設定 (R7-04) */
    struct usb_xfer *unsetup_next;                 /* unsetup_list のリンク (active の next とは別) */
    void *hcpriv;                     /* ED と TD 列を 2 組 (setup で確保、unsetup_done で返す) */
    void (*done)(struct usb_xfer *); void *ctx; struct usb_xfer *next;
};
/* 中核の口 (FB usb_transfer.c に対応。どれも待たない) */
int  usb_xfer_setup(struct usb_dev *, struct usb_iface *owner, const u8 *epd, u32 bufsize,
                    struct usb_xfer *x);            /* FB usbd_transfer_setup */
int  usb_xfer_start(struct usb_xfer *x);            /* FB usbd_transfer_start/submit :1724・:1955 */
void usb_xfer_stop(struct usb_xfer *x);             /* FB usbd_transfer_stop :1991 */
int  usb_xfer_drained(const struct usb_xfer *x);    /* FB usbd_transfer_drain :2126 の待ちの条件 (通知まで済んだ) */
void usb_xfer_unsetup_begin(struct usb_xfer *x);    /* FB usbd_transfer_unsetup :1436-1502 を始める */
int  usb_xfer_unsetup_done(const struct usb_xfer *x); /* xfer_free まで済んだ (FB の unsetup の戻り) */
int  usb_ctrl_async(struct usb_dev *, struct usb_iface *owner, const struct usb_setup *s,
                    struct usb_dma *b, u32 off, void (*done)(struct usb_ctrl_req *), void *ctx);
void usb_ctrl_cancel_owner(struct usb_dev *, struct usb_iface *owner); /* その持ち主の要求を取り消す */
void usb_service(void);
int  usb_xfer_sync(struct usb_xfer *x, u32 timeout_ms);  /* 前景だけ */
void usb_free_device_begin(struct usb_dev *);       /* FB usb_free_device :2289 (§4-7) */
void usb_hc_dead(struct usb_hc *, u32 why);                       /* 故障の入口 (§4-2-2) */
```

- `struct usb_class` は r4 と同じに `gone` を足す: `void (*gone)(struct usb_dev *, struct usb_iface *)` (HC の故障で 1 回だけ、xfer に触らない、§4-2-2)。`detach` は FB のドライバの detach と同じく**自分の xfer を `usb_xfer_unsetup_begin` し、`ctrl_cancel_owner` を呼ぶ**。中核は `usb_class_unsetup_done(dev, iface)` (その iface の xfer が全部 `usb_xfer_unsetup_done`、ep0 の要求が全部通知済み) を次の周で見てから sc を返させる (R5-03)。
- 送信の拒否は 1 か所: `usb_xfer_start` と `usb_ctrl_async` は `dev->state == DEV_DETACHED` か HC ≠ RUNNING なら断る。


```c
/* OS32 の利用者への口 (kernel、R2-12) */
/* drivers/input/kbd_internal.h (新): kbd.c の内部の配送を入力の合成だけに公開する */
void kbd_deliver_internal(u8 scancode);   /* kbd.c:281,344 の static を外して改名。IF=0 で呼ぶ */
/* drivers/input/kbd_merge.c (新) */
void kbd_merge_event(u8 src, u8 sc, int down); /* src 0 = 本体 (kbd.c:333 から)、1..4 = USB */
void kbd_merge_repeat(u8 src, u8 sc);          /* src がその sc を押下中のときだけ make */
void kbd_merge_release_src(u8 src);
/* drivers/input/mouse_ext.c (新) */
void mouse_ext_add(u8 src, i16 dx, i16 dy, u8 buttons);
void mouse_ext_release(u8 src);
```

### 3-1. 資源の持ち主と上限 (R1-18・R2-08)

| 資源 | 確保の口 | 持ち主 | 上限 | 尽きたとき |
|---|---|---|---|---|
| HC の DMA 頁 | `usbp_dma_alloc` | HCD | OHCI あたり 2 頁 (段 1) | その HC を断る (他の HC は続ける) |
| ED/TD・SETUP の塊 (16B) | `usb_dmamem` | xfer (setup〜unsetup、**2 組**を交互に、FB と同じ) | OHCI あたり 174 | submit が `-USB_E_NOMEM`、列挙は締切で再試行 |
| 制御の緩衝 (64B) | `usb_dmamem` | 要求の ctx | HC あたり 8 | 同上 |
| 構成の受信 (512B、512 整列) | `usb_dmamem` | 探索の走査 (A4 の中だけ、出るときに返す) | HC あたり 1 (走査は 1 本) | A4 は次の周に待つ (締切内) |
| `usb_dev` / 番地 | 固定表 / `hc->devices[]` | 親のポート (返すのは `usb_free_device` だけ) | 16 / 127 | ポートを無効にして記録 |
| `cfg` の写し | `usbp_mem_alloc` | `usb_dev` | 512B | その装置を断る (探索の ERR) |
| class の sc・報告の緩衝 | `usbp_mem_alloc`・`usb_dmamem` 64B | class | kbd 4・mouse 4 | attach 失敗 → class が自分の detach (FB のドライバと同じ) |

- OHCI の頁 B (緩衝): 512B × 2 + 64B × 8 + 予備 = 4KB に収まる。頁 A は HCCA + 木 + 番兵 (1,312B) と 16B 塊 174 個 ((4096−1312)/16)。

## 4. 手順

### 4-1. PCI の結びと引き継ぎ

| 順 | OS32 | 手本 | 外れる所 |
|---|---|---|---|
| 1 | 3 機能 (1033:0035 fn0/fn1、1033:00e0 fn2) を結ぶ。probe は progif を見て、irq_line は理由にしない | FB:ohci_pci.c:116、FB:ehci_pci.c:105 | — |
| 2 | 電源が D0 でなければ**断る** (遷移しない) | FB:ohci_pci.c:98-114 は D0 へ移して BAR を書き戻す | BAR に書かない規約 (`pci.h:44`) で書き戻せないため |
| 3 | `usbp_cfg_cmd(MEM)` → `usbp_mmio_map` (§5-3)。BM は §4-2/§4-9 の start の直前に上げる (BIOS が BM+ で渡した HC は、起動の早い段の静止で BM を落としてある、末尾の追記) | FB:ohci_pci.c:203 | 未割当 (BAR が 0 か、大きさの測定値 `~(len-1)` のまま) は断る |
| 4 | EHCI の引き継ぎ: EECP を辿り、LEGSUP の OS 旗を `usbp_cfg_write8` で立て、BIOS 旗を 10ms × 500 (5s) 待つ | FB:ehci_pci.c:552-595 (:579 の 500 回) | 期限切れ: FB は続けるが、OS32 は SMI との調停を持たないので EHCI を FAILED にして触らない。CONFIGFLAG を読み、0 なら OHCI は続け、1 なら OHCI も使えないと記録して断る |
| 5 | **EHCI の静止 (段 1 から)**: §4-9-1 の停止 → HCRESET → `CONFIGFLAG=0` を書いて読み戻す → `USBINTR=0` → BM を下げる | (手本なし、段 1 独自) | 段 1 は EHCI を動かさないが、BIOS 等が CF=1 で残すと全ポートが EHCI に行くため (R1-03) |
| 6 | OHCI ×2 を §4-2 で起動 (5 の後) | — | — |

- `pci_cfg_write8(bus,dev,fn,reg,v)`: `drivers/pci/pci.c` に +15 行。機構 #1 の `0xCFC + (reg&3)` へのバイト書きで、同じ DWORD の他バイト (W1C を含む) を読み書きしない。irq_save で囲む (`pci.h:37`)。

### 4-2. OHCI の起動 (`ohci_init.c`)

```c
int ohci_start(struct usb_hc *hc)   /* 前景、IF=1 可。usbp_udelay を使ってよい */
```

| 順 | 操作 | 手本 (FB:ohci.c) |
|---|---|---|
| 0 | DMA 2 頁を取る。頁 A = HCCA (256 整列) + 割込み ED 63 個の木 + iso 番兵 + 制御・bulk の番兵 ED。木を繋ぎ、HCCA の 32 項目を葉に向け、**`usbp_dma_sync(TO_DEV)`** | `ohci_init` :335-408 (木 :347-388、HCCA :398-400、flush :408) |
| 1 | IR が立っていれば OCR → 1ms × 100 待ち、残れば HCFS=RESET | `ohci_controller_init` :183-200 |
| 2 | `HcControl=HCFS_RESET`、100ms | :211-214 |
| 3 | FI を保存 → `HCR`、10µs × 10。落ちなければ FAILED | :217-232 |
| 4 | `HcHCCA`・`HcControlHeadED`・`HcBulkHeadED` (読み戻して照合) | :247-253 |
| 5 | `HcInterruptDisable=ALL` → IRQ が付いていれば `Enable = WDH|RD|UE|RHSC|MIE`、無ければ同じ要因で **MIE を立てない** (R1-14) | :256-257 |
| 6 | BM を上げる → `HcControl`: CBSR 1:4、PLE|IE|CLE|BLE、OPERATIONAL | :259-264 |
| 7 | FmInterval・PeriodicStart | :271-275 |
| 8 | NOCP → `LPSC` → 待ち → RhDescriptorA を戻す | :278-283 |
| 9 | NDP を最大 10 回読む (uPD720101 は fn0 = 3、fn1 = 2 の見込み、S16265 p.3) | :290-293 |

#### 4-2-2. HC の故障 — 1 本の入口 (R6-01、PM 決定で簡素化)

故障 = HC が UE を出す、時計 (HcFmNumber) が止まる、DMA 遅延・unsetup の 20ms の期限が切れる、起動の BM を上げた後の失敗。原因はカードの故障・外れのほか、ドライバの誤り (DMA の番地の誤り) もあり得るので、**止めて、その HC が触ったメモリを二度と使わない**ことだけを守る。装置ごとの丁寧な外しの順は踏まない。

```c
/* usb_hc_dead.c — どの文脈からでも呼べる (tick・前景・探索の途中・同期待ちの途中)。待たない */
void usb_hc_dead(struct usb_hc *hc, u32 why)
{   if (hc->state == HC_DEAD) return;                    /* 2 回目は何もしない */
    hc->state = HC_DEAD; hc->dead_why = why;             /* 以後 start・ctrl・探索は全部断る */
    hc->ops->kill(hc);          /* 書くだけ: MIE|ALL を Disable、PLE|IE|CLE|BLE を落とす、HCR、
                                   Command.BM を落とす。読み戻しの確かめはしない */
    for (i = 0; i < USB_HC_DMA_MAX; i++) usbp_dma_quarantine(&hc->dma[i]); /* HCCA・ED・TD・緩衝を全部 */
    for each dev in hc->devices[] (番地の順):           /* 根の子もハブの下も同じ表にある */
        for each iface with cls: cls->gone(dev, iface);  /* 上へ 1 回だけ知らせる (下の契約) */
    /* R7-01・R7-02: 通常のメモリ (xfer・usb_dev・cfg・class の sc) も再起動まで返さない。
       hc->devices[]・active・unsetup_list・explore はそのまま残し、以後だれも進めない */
    usb_log_dead(hc, why);                               /* ログ 1 行 */
}
```

- 呼んだ側の続き (R7-01): `usb_hc_dead` を呼んだ `usb_service` のループは**その HC の処理を直ちに抜けて次の HC へ**進む。その HC の scan の続き・DMA 遅延と unsetup の確認・時計の確認・RHSC の再有効化・探索は以後行わない (`state == HC_DEAD` の HC は飛ばす)。予約済みの `done()` は、呼ぶ直前に `x->dev->hc->state == HC_DEAD` なら呼ばずに捨てる (メモリは生きているので参照してよい)。
- 寿命 (R7-02): 死んだ HC に属するメモリはどれも解放されないので、前景の `usb_xfer_sync` も、その呼び手も、戻った後に xfer・dev・sc を参照してよい。上の側 (入力・ブロック) は `gone` を受けたら、その sc・Device を**以後呼ばない**だけ (解放しない)。

- `cls->gone(dev, iface)` の契約: xfer に触らない (DMA の塊は隔離済み)。入力は `usbp_input_release(src)`、ブロックは `Device` を「消えた」にする。**sc は返さない** (R7-01・R7-02)。`done()` は呼ばれない。1 回だけ。
- 前景の `usb_xfer_sync` は待ちの各周で `hc->state == HC_DEAD` を見て `-USB_E_HC` で戻る。
- ISR は `HC_DEAD` なら MMIO を読まず `IRQ_NONE`。線が鳴り続ければ kernel のストーム処理が止める (`kernel/irq/irq.h:53`)。IRQ の解除は部品の abort (前景) でだけ。
- その HC は再起動まで使わない (再初期化しない)。他の HC (もう 1 本の OHCI) は続ける。
- 起動途中の失敗 (r4 の 4-2-2a を簡素化): BM を上げる前 (`hc->started` の旗 ST_DMA・ST_MMIO・ST_IRQ・ST_BM のうち ST_BM が無い) の失敗は DMA を返して写像を外し HC_OFF (HC は DMA していない。BIOS が BM+ で渡した HC も、起動の早い段の静止で BM を落としてある、末尾の追記)。上げた後の失敗は `usb_hc_dead`。
- 部品の abort (`stop_dma`): HC_OFF の HC は何もしない。他は `usb_hc_dead(hc, ABORT)` を呼び、DMA は隔離のまま 0 を返す (部品は隔離される、DEVICE_STATES.md:31 の「止めたと証明できなければ隔離」)。EHCI の静止 (§4-9-1) は従来どおり。

### 4-3. ルートハブ (`ohci_roothub.c`)

- `rh_port` は書いて読むだけ。リセット: `PRS` を書く → 列挙の状態機械が 10ms 刻みで `PRSC` を見る (期限 50ms、FB:ohci.c:2302-2320) → `PRSC` を W1C → **20ms の回復** (USB_PORT_RESET_RECOVERY、FB:usb_request.c:864。ルートにも適用、R1-12)。
- RHSC: 要因を取り込んだら Enable から落とし、探索の後に戻す (FB:ohci.c:1183-1189、`ohci_rhsc_enable` :1065)。加えて tick で 100ms ごとに全ポートの変化 bit を読む (IRQ 無し・取りこぼしの保証、R1-14)。

### 4-4. 列挙 (`usb_explore.c` + `usb_enum.c`)

#### 4-4-1. 探索の走査とポートの扱い (FB の `uhub_explore`・`uhub_reattach_port`・`usb_alloc_device` の順)

探索は **HC ごとに 1 本**の走査 (`hc->explore`) で、ルートのポートを順に、ハブがあればその下を続けて (FB `uhub_explore_sub` :544-547 の再帰) 1 ポートずつ扱う。1 ポートの処理 (reattach) が終わる (成功か、失敗の後始末まで) まで次のポートに進まない。これが FB の探索スレッド 1 本の直列 (controller/usb_controller.c:363-418) と同じ役で、**番地 0 を使うのは常に 1 ポートだけ**になる。

```c
struct usb_explore {                /* HC に 1 本。持ち主 = usb_explore.c */
    struct usb_dev *hub; u8 port;   /* 今のポート (hub==NULL は根) */
    u8 step, ret_step, timeout, re_count, re_retry; u32 deadline_ms; u8 err; u8 old_addr;
    struct usb_dev *child;          /* reattach 中に作っている装置 (FB の child) */
    struct usb_dma *cfgbuf;         /* A4 の中だけ持つ 512B 塊 */
    struct usb_ctrl_req *req;       /* 待っている ep0 の要求 */
};
void usb_explore_step(struct usb_hc *hc);   /* usb_service から。needs_explore で走査を始め、待たずに 1 段 */
```

| 段 (FB の行) | 操作 | 出る条件 → 次 |
|---|---|---|
| X0 走査の開始 | `needs_explore` を落とし、根のポート 1 から (HC_DEAD なら走査しない) | → P0 |
| P0 状態を読む (uhub_explore :1139) | ポートの状態・変化を読む。(a) 過電流の変化は C_OVER_CURRENT を消すだけ (:1143-1149)。(b) そのハブで最初の走査なら C_CONNECT を補う (:1150-1156)。(c) C_PORT_ENABLE: 消す → C_CONNECT も立っていれば何もしない / enable=1 ならログ / **enable=0 なら restartcnt < 5 で C_CONNECT を補い restartcnt++**、5 ならログ (:1158-1180) | C_CONNECT → R0 / 無し → 子がハブなら下を走査 (:544-547)。成功なら restartcnt=0 (:1209-1212)、次のポート |
| R0 repeat (reattach :707-724) | C_CONNECT を消す。子がいれば `usb_free_device_begin(child)` | 子の free が終わった → R0b |
| R0b (:726-730) | **状態を読み直す** | 誤り → ERR / → R1 |
| R1 (:742-793) | CCS=0 → 次のポートへ。CCS=1 → 給電を見て 300ms の締切 | 締切 → R2 |
| R2 reset (usb_request.c:796-864) | reset を出し 10ms 刻みで C_PORT_RESET、消して 20ms | 完了 → R2b / 50ms → ERR |
| R2b (:799-823) | 状態を読み直す。C_CONNECT か CCS=0 なら: 既に 1 回戻っていれば ERR、でなければ timeout=1 で **R0 へ** | 変化なし → R3 |
| R3 速度 (:834-862) | 速度と初期 MPS (8、HS は 64) | → A0 |
| A0 (usb_alloc_device :1752-1797・1849) | 空いている番地を `hc->devices[]` から決める (表にはまだ載せない、:1778-1781)。`child` を作る | 無い → ERR |
| A1 (:1906-1930) | SET_ADDRESS(n)。**失敗は無視**、10ms | → A2 |
| A2 (:1950 → usbd_setup_device_desc usb_request.c:2056-2113) | FS: DEVICE 8B → MPS を検証して ep0 を更新 → DEVICE 18B (8B の失敗は MPS 8・16・32 で 18B を試す :2082-2097)。LS・HS: 18B、**失敗したらもう 1 回** (:2111-2113) | 成功 → A4 / 失敗 → RE (最大 2 回、:1954-1956)、RE が成功したら **A4 へ** (DEVICE の取得は繰り返さない :1950-1961) / 2 回とも失敗 → ERR |
| RE re-enumerate (usb_request.c:2149-2250) | RE1 reset (R2 と同じ操作) → RE2 番地 0・MPS 8 に戻す → RE3 SET_ADDRESS(元の番地) (失敗は無視) → **10ms の締切** (:1660-1662) → RE4 `usbd_setup_device_desc` (A2 と同じ)。**RE1 か RE4 が失敗**し (:2195-2199・:2236)、内側の再試行がまだなら 500ms 待って RE1 から 1 回だけ (:2239-2246) | 成功 → 呼んだ段の次 (A2 からは A4、A5 からは A4) / 失敗 → 呼んだ段の失敗 |
| A4 設定の試み (usbd_set_config_index usb_device.c:671-790 の前半、usbd_req_get_config_desc_full usb_request.c:1352-1390) | 前の `cfg` があれば捨てる (:686) → 512B 塊を取る → CONFIG 9B (actual = 9) → wTotalLength: 9 未満は誤り、512 超は 512 に切る → その長さで取り直し actual = 長さ → **ヘッダの wTotalLength を取った長さに書き換え** (:1386-1387) → `cfg` に写す → 塊を返す (A4 を出るときは成功・失敗とも返す) | → A5 / 失敗 → A5 の失敗と同じ出口 |
| A5 設定の試み (後半 :767-790、呼び手 :2013-2039) | SET_CONFIGURATION。A4・A5 の失敗では `cfg` を捨て (:787-790)、1 回目なら RE → 成功したら **A4 から取り直す** (:2025-2027)。2 回目の失敗 (または RE の失敗) は**ログを出して続ける** (構成なしの装置として載せる、:2029-2037) | → A6 |
| A6 (:2091・usb_probe_and_attach :1456) | `hc->devices[n] = child` に載せ、構成があれば interface ごとに match → attach (class の ep0 の要求は `owner=iface`。走査は attach が終わるまで待つ) | → 次のポート |
| ERR (:921-940) | 子があれば `usb_free_device_begin(child)` → 終わったら、ポートが enable なら PORT_ENABLE を落とす (結果はログだけ) | → 次のポート |
| X9 走査の終わり | 最後のポートの後。`needs_explore` が立っていれば X0 から次の走査 (R5-10) | — |

- 再試行は FB のものだけ: A2 の失敗で RE を 2 回まで、RE の内側の 500ms 後の 1 回、A5 の RE 1 回、R2b の repeat 1 回、P0 の restartcnt 5 回。ERR の後は、次の接続変化か restartcnt の再接続まで触らない (FB と同じ)。
- 探索の走査は 1 本なので、親ハブの取り外し (R0 の `usb_free_device_begin`) と、その下の子の reattach が同時に進むことは無い (R4-04 の状況が起きない)。
- class の attach の失敗: FB のドライバと同じく class が自分の detach を呼んで xfer を unsetup し、中核は `usb_class_unsetup_done` の後に sc を返させてその iface を空にする。装置と他の iface は止めない (R2-10)。

#### 4-4-2. 記述子の検査 (R2-09)

| 記述子 | 最短の bLength | 先に見る物 |
|---|---|---|
| DEVICE | 18 | A2 の actual = 18、bDescriptorType = 1 |
| CONFIG | 9 | 9B の actual = 9、9 ≤ wTotalLength ≤ 512、全体の actual = wTotalLength |
| INTERFACE | 9 | bNumEndpoints ≤ 4 |
| ENDPOINT | 7 | — |
| HID (0x21) | 6 | — |
| HUB (0x29) | 7 | bNbrPorts 1〜8 (超えたら 8 まで) |

- 走査: 各記述子で `bLength < 2` または `bLength > 残長` → **構成全体を拒否** (境界を失ったので再開しない)。型が上の表にあって最短に満たない → 全体を拒否。知らない型は bLength で飛ばす。
- interface の端点を選ぶとき: 番号 1〜15、方向 (HID は IN)、型 (HID・ハブ = 割込み、umass = bulk)、MPS (LS 割込み ≤ 8、FS 割込み ≤ 64、FS bulk ∈ {8,16,32,64}、HS bulk = 512)、interval (FS/LS 1〜255、HS 1〜16、0 は不可)。外れはその interface だけ捨てる (構成の境界は保たれているため)。上限超過 (5 個目の interface など) は記録して無視。

### 4-5. ハブ (`usb_hub.c`)

- attach: GET_DESCRIPTOR(HUB) (bLength ≥ 7、bNbrPorts 1〜8、超えたら 8 まで) → 全ポート PORT_POWER → bPwrOn2PwrGood×2ms 待ち (締切) → 状態変化の割込み pipe (FB:usb_hub.c:1398、:185、FB:usb_request.c:1503)。
- 子の持ち主: ハブの class の sc がポートの表 (`struct usb_dev *child[8]`、restartcnt) を持つ。変化 bitmap の報告 → `hc->needs_explore = 1` (FB uhub_intr_callback :185-198)。走査は X0 から。
- 深さ 5 段まで (FB:usb_hub.c:1066)。

### 4-6. 転送 (`ohci_sched.c`・`ohci_done.c`)

#### 4-6-1. 作り・停止点・完了 (R2-02)

FB と同じく**転送ごとに ED を 1 つ**作り、TailP=0、最後の TD の NextTD=0 で終わる列をリストに繋ぐ (FB:ohci.c `ohci_setup_standard_chain` :1379、:1520-1560、`_ohci_append_qh` :655-686)。toggle は ED を作るときに `pipe->toggle` を HeadP の C bit に入れ、完了で読み戻す (:1048-1054)。

| 区切り (段) | TD | toggle | R | 段の最後の TD の NextTD |
|---|---|---|---|---|
| SETUP | 1 | DATA0 (:1416) | 0 | 次の段へ繋ぐ |
| DATA (制御) | 1 (≤512B、塊は頁をまたがない) | DATA1 (:1441-1448) | IN なら 1 | **short_ok なら 0 (停止点)**、alt_next = STATUS の TD (:1340-1352) |
| STATUS | 1、CBP=BE=0 | DATA1 (:1506-1509) | 0 | 0 (列の終わり、:1523) |
| bulk・割込み | n (頁で区切る) | carry (:1312-1313) | **最後の TD だけ** 1 | 0、alt_next = なし |

- 外れる所: FB は short_ok の全 TD に R=1 を付ける (:1261-1263)。OS32 は段の途中の TD を R=0 にする。理由: 途中の TD が short で R=1 だと HC は同じ段の次の TD に進み、次の IN (umass では CSW) を data の緩衝に取り込む (OHCI 1.0a §4.3.1.3.5)。R=0 なら途中の short は CC=DATAUNDERRUN で ED が止まり、下の short の分岐で扱える。
- 完了と継続 (`scan`、FB:ohci.c `ohci_check_transfer` :1021-1060・`ohci_check_transfer_sub` :939-1020、集計は `ohci_non_isoc_done` :891-929・`_sub` :799-890)。列の組み方は FB の 2 通り (R4-01・R4-10):

| short_ok | 制御の列 | HC の動き | ソフト |
|---|---|---|---|
| 0 | SETUP → DATA (IN は R=1) → STATUS を直結 (:1340-1352 で停止点を作らない、:1500-1523) | short でも HC は STATUS まで進む | ED が空 (HeadP==TailP) か Halted になってから集計。DATA の short で集計を打ち切り (:861-868)、結果は SHORT_NG (FB usb_transfer.c:3097-3103 の SHORT_XFER) |
| 1 | DATA の段の最後の TD の NextTD=0 (停止点)、alt_next=STATUS (:1340-1352) | DATA の後で止まる | 停止点で止まったら、short か否かに依らず STATUS の TD から 1 回だけ再開 (:974-1000)。STATUS の完了で SHORT か OK |

```c
/* ohci_done.c — 1 転送を見る (FB ohci_check_transfer)。戻り 0 = まだ / 1 = 完了 */
static int ohci_check(struct usb_hc *hc, struct usb_xfer *x)
{   struct ohci_xp *p = x->hcpriv;                   /* ED・TD 列・cur・short_seen・ng */
    sync ED TO_CPU; h = headp; t = tailp;
    if (!(h & OHCI_HALTED) && ((h ^ t) & ~0xFu)) return 0;      /* HC がまだ進めている (:1034-1035) */
    for (td = p->cur; td; td = td->sw_next) {                    /* cur から 1 度だけ */
        sync TD TO_CPU; cc = TD_CC(td); cbp = TD_CBP(td);
        int data = (td->stage == TD_DATA);
        if (data) x->actual += td_done_len(td, cbp);             /* DATA だけ (R3-01) */
        if (cc && !(cc == CC_DATAUNDERRUN && data)) { p->ng = cc_to_err(cc); break; }
        int shrt = data && (cc == CC_DATAUNDERRUN || cbp);
        if (shrt) p->short_seen = 1;
        if (td->hw_next_is_end && td->alt_next) {                /* 停止点 (short_ok=1 の制御 DATA の末尾) */
            resume(hc, p, td->alt_next); return 0; }             /* STATUS へ 1 回 (:974-1000) */
        if (shrt) {                                               /* 段の残りは数えない (:861-868) */
            if (p->status_td && !p->resumed) {                   /* short_ok=0 の制御: HC は直結の STATUS を終えている */
                sync; if (TD_CC(p->status_td)) p->ng = cc_to_err(TD_CC(p->status_td)); }
            break; }                                              /* bulk・割込み: 残りの TD は ED ごと外す */
        if (td == p->last) break;
    }
    x->error = p->ng ? p->ng : p->short_seen ? (x->short_ok ? USB_X_SHORT : USB_X_SHORT_NG) : USB_X_OK;
    p->pipe->toggle = (h & OHCI_TOGGLECARRY) ? 1 : 0;            /* :1048-1054 */
    ohci_xfer_close_done(hc, x);                                  /* §4-6-2: 外して通知へ */
    return 1;
}
static void resume(struct usb_hc *hc, struct ohci_xp *p, struct ohci_td *td)
{   p->cur = td; p->resumed = 1;                                  /* resumed: 2 度目は来ない (停止点は 1 つ) */
    dma_le32_put(&p->ed->headp, td->bus); sync ED TO_DEV;          /* :983-1000 */
    mmio_wr32_le(hc->regs, HcCommandStatus, CLF); }
```

- 停止点は short_ok=1 の制御の DATA の末尾 1 か所だけ。STATUS の TD は列の終わり (NextTD=0、alt なし) なので、STATUS の後に `resume` は呼ばれない (R4-01)。
- bulk・割込みは段が 1 つ。段の最後の TD だけ R=1 (D1)。途中の short は DATAUNDERRUN で Halted → 集計して SHORT/SHORT_NG で完了 (残りの TD は ED ごと外す)。

#### 4-6-2. 止める・外す・DMA 遅延・通知 (FB `usbd_transfer_stop` :1991-2050、`ohci_device_done` :1614-1640、`usbd_callback_wrapper_sub` :2969-3020)

xfer は 1 つの状態だけを持ち、通知 (`done()`) は `XS_CB_PENDING` からの 1 か所だけ。ED/TD は xfer が setup から unsetup まで持つ (FB と同じ。冷却の記録は無くした)。

| 状態 | 入る条件 | 操作 | 出る条件 → 次 | 手本 |
|---|---|---|---|---|
| XS_IDLE | setup・通知の後 | — | `usb_xfer_start` (`did_dma_delay = 0` :1776-1777、`curr_set ^= 1` で使う組を切り替え ohci.c:1396) → XS_ACTIVE | :1724 |
| XS_ACTIVE | 上 | `xfer_enter` で列を組み ED を繋ぐ | 完了 (`ohci_check`) → 外して (`xfer_close`) XS_CB_PENDING / `usb_xfer_stop`・締切 → error=CANCELLED/TIMEOUT、`xfer_close` → XS_DMA_DELAY | :1991-2047、ohci.c:1614-1640 |
| XS_DMA_DELAY | 上 (誤りが CANCELLED/TIMEOUT で `did_dma_delay=0` のとき) | `xfer_close` で切り離しを見せた**後に** `dma_delay_frame = frame()`、`dma_delay_deadline_ms = now + 20` (R4-07)、`did_dma_delay = 1` | frame − dma_delay_frame ≥ 2 → XS_CB_PENDING / 期限 → `usb_hc_dead(TIMEOUT_DMA)` (§4-2-2) | usb_transfer.c:2991-3020、`usb_dma_delay_done_cb` :2537 (D2) |
| XS_CB_PENDING | 上 | `usb_service` の決まった位置で `x->state = XS_IDLE; x->done(x)` | done の中で `usb_xfer_start` してよい | :2401 |

- `xfer_close` (OHCI): ED に SKIP → 前の ED の NextED を外す ED の NextED に (外す ED 自身の NextED は残す、`_ohci_remove_qh` :688-716) → TO_DEV。
- HC の故障 (§4-2-2): xfer は状態に依らず通知せずに捨てる。上へは `cls->gone` が 1 回だけ知らせる。
- 2 組 (R5-01): `xfer_enter` は `curr_set` の組の ED/TD で列を組む。done の中で再 start すると次の組を使うので、HC が前の frame で取得した前の組には書かない (FB:ohci.c:1395-1399、組の確保 :2465・:2493-2497)。
- unsetup (R5-03、FB usbd_transfer_unsetup :1436-1502 → unsetup_sub :1376-1435):

| unsetup | 入る条件 | 操作 | 出る条件 → 次 |
|---|---|---|---|
| UNSETUP_WAIT | `usb_xfer_unsetup_begin` | `open = 0` → `usb_xfer_stop` | `usb_xfer_drained` (XS_IDLE、通知済み) → UNSETUP_DELAY |
| UNSETUP_DELAY | 上 | **入るときだけ** `x->unsetup_frame = frame()`、`x->unsetup_deadline_ms = now + 20` (FB の `needs_delay` の 1125µs の待ち :1386-1390、D2) | frame 2 進み → `xfer_free` (2 組とも返す) → UNSETUP_DONE、`unsetup_list` から外す / 期限 → `usb_hc_dead` |
| UNSETUP_DONE | 上 | — | `usb_xfer_unsetup_done` が 1 |

### 4-7. 装置を返す (FB `usb_free_device` usb_device.c:2289-2376 の順、R4-02・R4-03・R4-08)

`usb_free_device_begin(dev)` が唯一の入口で、`dev`・`cfg`・番地・interface を返すのはここだけ。呼ぶのは探索の R0 (接続変化で子がいた)・ERR (reattach の失敗)、ハブの class の detach (配下の子、FB uhub_detach :1771-1784)、HC の停止の終点 (根の子) の 4 か所。どれも探索の走査か停止の段の中から呼び、終わるまで呼び手は次へ進まない。

```c
struct usb_free_ctx { u8 step; u8 iface; u8 child_port; };   /* dev->freeing。持ち主 = usb_detach.c */
void usb_free_device_begin(struct usb_dev *d);   /* 2 回目の呼び出しは何もしない (freeing が立っている) */
int  usb_free_device_step(struct usb_dev *d);    /* usb_service から。1 = 返し終えた */
```

| 段 | FB の行 | 操作 | 次へ進む条件 |
|---|---|---|---|
| D0 | :2301 `usb_set_device_state(DETACHED)` | `state = DEV_DETACHED` (以後この装置への start・ctrl を断る) | 直ちに |
| D1 | :2329 `usb_unconfigure` → :623 `usb_detach_device` (iface を順に :1309-1330) | iface を 1 つずつ `cls->detach` (class は自分の xfer を `usb_xfer_unsetup`、ep0 の要求を `usb_ctrl_cancel_owner`)。**ハブの class の detach は、割込みの xfer を unsetup した後、ポート順に子へ `usb_free_device_begin` し、子が返るのを待つ** (uhub_detach :1771-1784) | その iface の `usb_class_unsetup_done` と、ハブなら全部の子が返った → class が sc を返す → 次の iface |
| D2 | :643-648 `usb_config_parse(FREE)`・`usbd_free_config_desc` | `cfg` を返す、iface の表を空に | 直ちに |
| D3 | :2332 `usb_bus_port_set_device(NULL)` | 親のポートから外し、`hc->devices[addr] = NULL` (番地を返す) | 直ちに |
| D4 | :2337 `usbd_transfer_unsetup(ctrl_xfer)` | ep0 の xfer を `usb_xfer_unsetup_begin` | `usb_xfer_unsetup_done(&d->ctrl)` (R5-03) |
| D5 | :2370-2376 | `usb_dev` を表に戻す | 終点 → 呼び手に 1 を返す |

- 子の取り外しは D1 の中 (ハブの class の detach) で行うので、親の回収が子を待つ仕組み (r4 の `nchild`・P_RECLAIMED・DEV_ZOMBIE) は要らない。子の取り外しは同じ表を再帰で進む (深さ 5 段まで)。
- 番地の返却は D3 で、ep0 の排出 (D4) より前 (FB と同じ順)。その時点で装置は DETACHED なので新しい要求は出ない。番地の再利用は探索の走査の次のポート以降で、走査は D5 まで待つ。
- HC が `HC_DEAD` になったら、この free の途中でも打ち切る (§4-2-2 が装置を一括で「無い」にする)。

### 4-8. HID (`hid_kbd.c`・`hid_kbd_map.c`・`hid_mouse.c`)

| 項目 | OS32 | 手本 / 外れる理由 |
|---|---|---|
| 照合 | class 3 / subclass 1 / protocol 1=kbd・2=mouse | FB:input/ukbd.c:1102-1103、FB:input/ums.c:440 |
| kbd の attach | `usbp_input_src_alloc(KBD)` → SET_PROTOCOL(0=boot) → SET_IDLE(0) (失敗は無視) → 割込み IN (8B) | FB:input/ukbd.c:1344-1360 |
| mouse の attach | `usbp_input_src_alloc(MOUSE)` → SET_PROTOCOL(**0=boot**)、失敗は無視 → 割込み IN | FB:input/ums.c:592-603 は 1=report で HID 記述子を解く。段 1 は解析器を持たないので boot の固定 3B。報告 ID 付きの装置は崩れる — 制限として記録 |
| mouse の報告 | 3B 未満は捨てる。byte0 bit0-2 = ボタン、byte1/2 を i8 で符号拡張 → `usbp_input_mouse` | FB:input/ums.c:218 |
| kbd の写し (R2-01) | 報告 8B を**スキャンコードの押下数** `u8 now[128]` に写す: 修飾 8 bit (usage 0xE0〜0xE7) と 6 個の usage をそれぞれ表で sc にし `now[sc]++` (左右 Shift は同じ sc に 2)。ErrorRollOver の報告は丸ごと捨てる | FB:input/ukbd.c:558 は usage で差分 — OS32 は PC-98 の sc が多対一なので sc で差分する |
| kbd の差分 | 前回の `prev[128]` と比べ、`prev==0 && now>0` → `usbp_input_key(src, sc, 1)`、`prev>0 && now==0` → `usbp_input_key(src, sc, 0)`。それ以外は流さない。`prev = now` | — |
| 合成 (`kbd_merge.c`) | src ごとの押下 bit `down[src][128]` と全体の数 `cnt[128]`。`kbd_merge_event(src, sc, 1)`: bit が 0 なら立てて cnt++ し、make を `kbd_deliver_internal` へ (bit が既に 1 = 本体の鍵盤の繰り返し → make だけ流す)。`(src, sc, 0)`: bit が 1 なら消して cnt--、**cnt が 0 になったら** break を流す | `kbd.c:365-377` の修飾は 1 bit なので、合成を前に置いて別の入力元の離しで解除しない |
| 本体の鍵盤 | `kbd.c:333` を `kbd_merge_event(0, sc & 0x7F, !(sc & SCANCODE_BREAK))` に替える | — |
| 繰り返し | USB の src ごとに最後に 0→1 になった非修飾の sc を覚え、500ms 後 30ms 間隔で `usbp_input_key_repeat` (tick) → `kbd_merge_repeat` | FB:input/ukbd.c:423 |
| 閉じる・抜去 | `cls->detach` で `usbp_input_release(src)` → `kbd_merge_release_src` (その src の bit を全部消し、cnt が 0 になった sc を break) / `mouse_ext_release` | — |
| マウスの合成 | `mouse_ext.c` は src ごとの buttons と全体の dx/dy (i16 で飽和) を持つ。`mouse_poll` (`mouse.c:83`) が読み出して 0 に戻し、buttons = バスマウス OR 全 src の OR。seamless (NP21/W) とは合成しない | — |

- U3 の割り当て (PM 決定): Pause→STOP、PrintScreen→COPY、End→HELP、F11/F12→vf・1/vf・2 (vf・3〜5 は無し)、カタカナひらがな→かな、左右 Alt→GRPH、変換→XFER、無変換→NFER、左右 Shift→SHIFT、左右 Ctrl→CTRL、JIS (109) 配列。GUI キーは割り当てない。

### 4-9. EHCI (段 2、段 1 は 4-9-1 の静止だけ)

#### 4-9-1. 停止と HCRESET (R1-02、段 1 から使う)

```c
int ehci_quiesce(struct usb_hc *hc, u32 *why) /* 前景。0 = 停止・リセット・CF=0 を証明 */
```

| 順 | 操作 | 期限 | 手本 |
|---|---|---|---|
| 1 | `USBINTR = 0` | — | — |
| 2 | `USBCMD = 0` → `USBSTS.HCH` が立つのを待つ | 100 × 1/128s (FB と同じ ≈ 780ms) | FB:ehci.c:203-215 `ehci_hcreset` |
| 3 | `USBCMD = HCRESET` → HCRESET が落ちるのを待つ | 同上 | FB:ehci.c:183-197 `ehci_reset` |
| 4 | `CONFIGFLAG = 0` を書いて読み戻す | — | 段 1 独自 (HCRESET で 0 に戻るかに依らず明示する) |
| 5 | `USBSTS` を全部 W1C、`usbp_cfg_cmd(clear=BM)` | — | — |

- 2 か 3 が期限切れ: EHCI を FAILED、DMA は持っていないので隔離は不要。CONFIGFLAG を読み 0 なら OHCI は続ける、1 なら断る (§4-1 の 4)。

#### 4-9-2. 起動 (段 2)

4-9-1 の 1〜3 → 枠表 (FLS が可変なら 256 項目) と非同期の番兵 QH を作って `usbp_dma_sync(TO_DEV)` → CTRLDSSEGMENT=0 (FB:ehci.c:242) → PERIODICLISTBASE (:246) → ASYNCLISTADDR (:249) → USBINTR (:252) → USBCMD = ITC1|FLS|ASE|PSE|RS (:255-260) → **CONFIGFLAG=1** (:263) → HCH が落ちるのを待つ (:265-267)。

- companion: 接続時 LineStatus=K なら LS → PO (FB:ehci.c:3310-3320)、リセット後 PE=0 なら FS → PO (:3341-3350)。uPD720101 は OHCI#1 = ポート 1/3/5、OHCI#2 = 2/4 (S16265 p.3)。HCSPARAMS の N_PORTS・N_CC・N_PCC を読んで照合し、違えば記録する。
- QH の外しは IAA (FB:ehci.c:2216-2229)。
- **段 2 の受入は直結の装置だけ** (R1-25)。HS ハブ下の FS/LS は TT と split (QH の hub addr/port・S/C マスク) が要るので段 2b に分ける。`usb_dev.tt_hub_addr/tt_port` は今から持つ。

### 4-10. 大容量 (段 3、R1-26)

- 流れ: GET_MAX_LUN (FB:storage/umass.c:238、STALL は LUN 0) → CBW 31B (署名 0x43425355、tag は毎回増やす、:1335-1361) → data → CSW。
- CSW の検査: 実長 = 13、署名 0x53425355、tag 一致、dCSWDataResidue ≤ 要求長、status: 0 = 成功 (actual = 要求 − residue)、1 = 失敗 → REQUEST SENSE、2 = phase error → reset recovery (:1531-1580)。
- 回復: data 段の STALL → その bulk の CLEAR_HALT → CSW を読む。CSW の読みが STALL → CLEAR_HALT → 1 回だけ読み直す。不正な CSW・phase error・期限切れ → BBB reset (:1276) → 両 bulk の CLEAR_HALT (:1311) → その要求は失敗。
- SCSI: INQUIRY・TEST UNIT READY・READ CAPACITY(10)・REQUEST SENSE・READ(10)。最終 LBA = 0xFFFFFFFF (16 が要る) と block 長 ≠ 512 は断る。
- 抜去: `Device` を「消えた」にして以後の `blk_read` は失敗、使い手が 0 になってから `dev_unregister`。
- **段 3 の受入は読みだけ** (U4、PM 決定)。WRITE(10) は別の回。そのとき完了が分からない WRITE は再送しない (DEVICE_STATES.md:16)。

## 5. 記述子の確保と [HW2]

### 5-1. 規則 (r1 と同じ + 追記)

- 頁単位で池から取り、`dma_range_ok` で 64KB 境界を確かめる。中は `usb_dmamem.c` が 16B (ED/TD/SETUP)・256B (制御の緩衝) の塊に分ける。ソフトの欄 (リスト・長さ・xfer への戻り) は DMA に置かず `usbp_mem` の影の表に持つ (r1 の 32B 塊をやめ、頁を減らす)。
- CPU の仮想番地を装置に渡さない。段 3 は USB の緩衝に写す。

### 5-2. 池の見積り (R1-17)

| 利用者 | 大きさ | span | 根拠 |
|---|---|---|---|
| LAN (82557) | 14,032B → 16KB | 1 | `drivers/net/e82557/e82557.c:691`、`e82557_desc.c:10` |
| PCM (CS4231) | 16KB | 1 | `drivers/sound/cs4231/pcm_cs4231.h:121`、`pcm_cs4231.c:538` |
| GeForce | 要実測 | ? | `drivers/video/geforce/geforce_dma.c:9` (大きさは呼び手次第) |
| USB 段 1 | OHCI 2 頁 × 2 = 16KB | 4 | §3-1 |
| USB 段 2 | + EHCI 3 頁 = 12KB | 3 | §4-9-2 |
| USB 段 3 | + bulk 16KB | 1 | §4-10 |

- USB の頁数は r2 と同じ (512B の構成受信の塊 2 個は OHCI の頁 B の中、§3-1)。
- 段 1: LAN + PCM + USB = 48KB、span 6 → GeForce が 16KB 以下なら 64KB に収まる。受入 A8 で `dma_pool_free_pages` を全構成の起動後に記録する。
- 枯渇時: その HC を断り (他は続ける)、`usbstat` と起動ログに「DMA 不足」を出す。EHCI が取れなければ OHCI だけで FS として動く。
- 池の拡大は**段 3 の前に判断**: 池は 0x7F0000 固定 (8MB の上端) なので、128KB にするなら下へ 0x7E0000 から。8MB 機での退行試験 (池の host 試験に 128KB と境界の項目、NP21/W 8MB 構成で起動) を足す。

### 5-3. MMIO の予約と写像 (R1-16)

| 段 | いつ | 何を | 置き場 |
|---|---|---|---|
| 予約 | 起動時、最初の AS より前 (`pc98_boot_pci_resources` から) | 1033:0035/00e0 の BAR0 について、長さの根拠表 (OHCI 4KB = OHCI 1.0a の register 空間を頁に、EHCI 256B (実測)。表は `kdebug/gfinit_resources.c:31-45` と 1 か所に) で範囲を決め、他の BAR・RAM・既存の窓と重ならないことを確かめ、owner (`ledger_owner_new`)・rid を台帳に登録し、PFN の範囲の共有 PT を用意する | `platform/pc98/pci_mmio.c` (新) の `pci_mmio_reserve(dev,bar)`、`platform/pc98/pci_resources.c` (+10) |
| 写像 | 部品の start | `pci_bar_map(dev,bar,&va)` → `paging_device_map(owner,rid,first,end)` (`mm/paging.h:132`) | 同上 |
| 戻し | stop の後 / start の失敗 | `paging_device_unmap`。予約は起動の寿命のまま (再写像は無い) | 同上 |

- 予約が失敗した機能は probe で断る (理由を `pci_bind_set_reason` で残す)。台帳の関数の正確な使い方は `mm/pgalloc.h:222-260` に沿う (実装の段で確かめる)。

## 6. エンディアンと DMA の所有の移り (R1-19)

- `include/endian_dma.h` / `arch/<arch>/arch_endian_dma.h` は r1 と同じ (`le32_t`、`dma_le32_get/put`、`mmio_rd32_le/wr32_le`)。
- `usbp_dma_sync(d,off,len,dir)` を初版から契約に置く。pc98 は障壁だけ (i386 の PCI は snoop)。PowerMac は「一貫していれば障壁、していなければ範囲の flush/invalidate」を platform の実装条件とし、一貫性の根拠 (資料) と受入は PowerPC の回で求める。
- 所有の移る点 (呼ぶ所):

| 点 | 向き | 手本 |
|---|---|---|
| HCCA・木・番兵を作った後、レジスタに番地を書く前 | TO_DEV | FB:ohci.c:408 |
| TD/ED を書いた後、TailP 更新・CLF/BLF の前 | TO_DEV | FB:ohci.c `ohci_setup_standard_chain` |
| OUT の payload を書いた後、submit の前 | TO_DEV | — |
| ED.HeadP・TD の CC/CBP を読む前 | TO_CPU | FB:ohci.c:819、:1030 |
| HCCA.DoneHead を読む前 / 0 に戻した後 | TO_CPU / TO_DEV | FB:ohci.c:1123、:1145 |
| IN の payload を読む前 | TO_CPU | — |

## 7. 割込みと協調切替 (R1-08・R1-14・R1-15)

### 7-1. 要因の採取は 1 本

```c
u32 ohci_intr_collect(struct usb_hc *hc)  /* IF=0 で呼ぶ。待たない */
{   /* st = HcInterruptStatus & 監視する要因 (MIE の有無に依らない)
       WDH: DoneHead を TO_CPU で読み、0 に戻し TO_DEV、WDH を W1C (FB:ohci.c:1123-1146)
       RHSC: Enable から落とす (:1188)。他も W1C。戻り値 = st */ }
int ohci_isr(void *arg)   /* usb_resident の踏み台から */
{   if (hc->state != HC_RUNNING) return IRQ_NONE;   /* 故障後は MMIO を読まない (§4-2-2) */
    u32 st = ohci_intr_collect(hc); if (!st) return IRQ_NONE;
    hc->isr_pending |= st; return IRQ_HANDLED; }
```

- IRQ 無し (`has_irq=0`) は MIE を立てず、同じ `intr_collect` を tick で回すだけ (手本の polling: FB:ohci.c:1230 `ohci_do_poll`)。

### 7-2. `usb_service` (tick と前景)

```c
void usb_service(void)
{   u32 f = usbp_irq_save();                     /* tick では既に IF=0 */
    if (usb_busy) { usbp_irq_restore(f); return; }  /* 再入は断る */
    usb_busy = 1;
    for each hc:
      if (hc->state == HC_RUNNING) {
        u32 p = hc->isr_pending | hc->ops->intr_collect(hc); hc->isr_pending = 0;
        if (p & UE) { usb_hc_dead(hc, USB_WHY_UE); continue; }  /* 直ちに次の HC へ (R7-01) */
        else { hc->ops->scan(hc, 32);                      /* scan_next から 32 件 */
               XS_DMA_DELAY の xfer の frame・期限を見る;   /* 期限切れは usb_hc_dead → continue */
               unsetup_list の各 xfer に usb_xfer_unsetup_step;   /* R6-02: WAIT→DELAY→DONE。期限切れは同上 */
               時計が止まっている (HcFmNumber が 20ms 進まない) なら usb_hc_dead → continue;
               if (p & RHSC || 100ms 経過) hc->needs_explore = 1; RHSC を戻す; }   /* 走査は巻き戻さない (R5-10) */
      }                                                    /* HC_DEAD の HC は何もしない */

    予約された done() を順に呼ぶ (HID の解読 → usbp_input_*)。x->dev->hc が HC_DEAD なら呼ばずに捨てる (R7-01);
    HC_RUNNING の HC ごとに usb_explore_step 1 回 (取り外し中なら usb_free_device_step を 1 段、§4-7);
    キーの繰り返し;
    usb_busy = 0; usbp_irq_restore(f); }
```

- 1 回の仕事は「HC ごとに 32 転送 + DMA 遅延・unsetup の確認 + 探索 1 段 (HC_DEAD の HC は 0)」で有界。どの分岐も `usbp_udelay` を呼ばない。`done()` の中から `usb_xfer_submit` してよい (待たない)。
- `usb_xfer_sync` (前景だけ): `usb_service()` を回しながら締切まで待つ。tick の中から呼ばれたら `-USB_E_CTX`。HC が `HC_DEAD` になったら `-USB_E_HC` で戻る。起動の同期版 (`start`・`ehci_quiesce`) は殻の start / stop_dma (前景、boot 文脈) からだけ呼ぶ。

## 8. 資源の回収 (R1-04 の反映)

| 場面 | 何をするか |
|---|---|
| 装置の抜去 | §4-7 |
| HC の故障 (UE・時計停止・DMA 遅延や unsetup の期限切れ・BM の後の起動失敗) | `usb_hc_dead` (§4-2-2): 止める書き込み → DMA を全部隔離 → 装置を一括で「無い」にして `cls->gone` を 1 回 → 再起動まで使わない |
| 部品の abort | quiesce = 新しい走査を止める / detach = 踏み台を NULL・IRQ 解除 (tick は消せない、`lan_module.c:50` の形) / stop_dma = HC_OFF 以外の HC に `usb_hc_dead(ABORT)`、`ehci_quiesce`。DMA を隔離したので 0 を返し部品は隔離される |
| 実行中の unload | 無い (`module_runtime.h:63`) |

## 9. 部品の大きさ (r1 と同じ、行だけ補う)

- `usb.mod` 段 1 ≈ text 24KB (EHCI の静止を含む)、BSS ≈ 4KB。段 3 まで ≈ 42KB。上限は LAN と同じ 64KB (`docs/08_build.md:201`)。
- vmkernel: `usb_resident.c`・`kbd_merge.c`・`mouse_ext.c`・`pci_mmio.c`・`pci_cfg_write8`・tick slot ≈ 3KB。MINIMAL・起動 FD には `usb.mod` を入れない。kdebug.bin は触らない。

## 10. ファイルの割り当てと受入単位 (R1-24・R1-27)

段 1 は受入単位 1A〜1E。**1A (型と API) を設計レビューで固定してから** 1B〜1D を並列に出す。

| 単位 | ファイル (新/変、行) | 受入 |
|---|---|---|
| 1A 型と契約 | `include/endian_dma.h` 60・`arch/x86/arch_endian_dma.h` 20・`arch/ppc/arch_endian_dma.h` 40、`include/usb_platform.h` 90、`drivers/usb/usb_core.h` 230・`usb_desc.h` 120・`ohci_regs.h` 120・`ohci_priv.h` (影の表・塊の型) 80、`tools/check_usb_portable.py` 80 | A1・A2 (コンパイルと番人だけ) |
| 1B OHCI | `usb_dmamem.c` 150・`ohci_init.c` 260 (start・kill)・`usb_hc_dead.c` 80 (故障の入口)・`ohci_sched.c` 330 (作り・繋ぎ)・`ohci_done.c` 280 (check・short の継続)・`ohci_unlink.c` 150 (close・DMA 遅延)・`ohci_roothub.c` 160・`ehci_quiesce.c` 120、`tools/tests/usb_fake_ohci.c` 450・`usb_fake_ehci_min.c` 120 | A3・F2〜F6m・F9・F9u・F10・G1・G2 |
| 1C 中核 | `usb_xfer.c` 280 (setup/start/stop/drained/unsetup・DMA 遅延・通知)・`usb_req.c` 150・`usb_enum.c` 320・`usb_explore.c` 260・`usb_cfgparse.c` 160・`usb_hub.c` 300・`usb_detach.c` 200 (usb_free_device の段)、`tools/tests/usb_fake_dev.c` 300 | A4・A6・F7〜F8c・F11a・F14 |
| 1D HID と入力 | `hid_kbd.c` 240・`hid_kbd_map.c` 170・`hid_mouse.c` 110、`drivers/input/kbd_internal.h` 15 (新)・`kbd_merge.c` 100・`kbd.c` (:281・:344 の static を外し `kbd_deliver_internal` に改名、:333 を `kbd_merge_event` に、計 ±6 行)・`mouse_ext.c` 80・`mouse.c` (+8) | A5・F11・F11a・F12 |
| 1E 殻と実機 | `drivers/usb/usb_resident.c` 160・`usb_module.c` 250・`usb_imports.def`・`platform/pc98/usb_platform.c` 200 (usbp_input_* を含む)・`drivers/usb/usb_early_quiesce.c` 120 (常駐側の起動の早い段の静止、末尾の追記)・`pci_mmio.c` 160・`pci_resources.c` (+10)・`drivers/pci/pci.c` (+15)、`include/memmap.h`/`kernel/irq/tick.h`/`tick_order.h`/`module_schema.h`/`tools/module_schema.py` (+20)、`build/*.mk`・`packages.yaml`、観測口 (下) | A7〜A11 |

- 観測口 `usbstat`: 起動時と変化時に 1 行ずつ kernel のログ (シリアル) へ出す (段 1 はこれだけで受入できる)。シェルの `usbstat` は KAPI の末尾追加 1 口 (`usb_stat_get`) が要るので、1E で `os32-kapi-add` に従う (A11 で使う)。
- 段 2: 2A `ehci_init.c` (起動) 220・2B `ehci_sched.c` 400・2C `ehci_done.c` 220・2D `ehci_roothub.c` 220 (A12)、2b は TT/split の別票。段 3: `umass_bbb.c` 320・`scsi_cmd.c` 200・`usb_blkdev.c` 160 (A13)。
- 1,000 行を超えるファイルは無い。

## 11. 受入 → 試験 → 変異 (R1-28・R2-13)

偽 HC のモデル (host、libc なし):
- frame ごとに制御・bulk・割込みのリストを辿る。**frame の始めに取得した ED は、その frame の終わりまで HC が持ち続け、その TD に書く** (SKIP・切り離しはその frame の次から効く)。frame の時計はテストが進める・止めることができる。
- 停止の指示 (リストの enable を落とす・HCR) を受けるまでリストを読み続け DMA を書く。HCR を無視する・時計を止める設定がある。
- 解放された塊は毒の値で埋め、HC がそこへ書いたら試験は失敗 (猶予の不足が見える)。
- イベント表: IRQ を出さない / 共有線の他装置 / TD n で STALL・DATAUNDERRUN・short (R=1) / 無応答 / reset が終わらない / PES が落ちない / UE / 抜去。偽装置: boot kbd・boot mouse・複合・4 ポートハブ・不正な記述子。

変異はすべて実行時に試験を落とす (コンパイルや番人で落ちる物は A1・A2 だけ)。

| # | 受入 | 試験 | 変異 (一意、実行時に RED) |
|---|---|---|---|
| A1 | 記述子は口を通る | `-DUSB_TEST_SWAP` で A3〜A6 を再走 | `ed->headp.raw = x` を 1 か所 |
| A2 | 共通は platform に直結しない | 番人 | `ohci_init.c` に `#include "mm/dma_pool.h"` |
| A3 | 起動の順 | OPERATIONAL の時点で HCCA と木が完成 | HCCA を埋める処理を OPERATIONAL の後へ |
| A4 | 列挙・番地を重ねない | 偽装置 3 台 + ハブ | SET_ADDRESS 後の 10ms を消す |
| A5 | HID の差分と合成 | 同じ USB 鍵盤で左右 Shift を押し、左だけ離す → SHIFT の break が出ない。本体と USB で同じキー | `prev/now` の数ではなく bit で差分する |
| A6 | 抜去で離れ、資源が戻る | 押下中に抜去、塊・番地・usb_dev の数が元に戻る | `usbp_input_release` を消す |
| A7 | [HW2] と整列 | 境界直前の池で `usb_dmamem` | 境界の検査を消す |
| A8 | 装置が無い機械 | NP21/W: kbd/mouse が従来どおり、`dma_pool_free_pages` が減らない | probe より前に `usbp_dma_alloc` |
| A9〜A11 | 実機の打鍵・マウス・抜き差し 10 回 | Ra266 (前提: 下の入れ替えの後) | — (実機は変異しない) |
| A12 | 段 2 の受け渡し | host の偽 EHCI + 実機 | PO の書きを消す |
| A13 | 段 3 の読み | 偽装置 (CSW を差し替え) + 実機 | CSW の tag 照合を消す |
| F2 | CF=1 で起動しても OHCI に来る | 偽 EHCI を CF=1 で | CONFIGFLAG=0 を消す |
| F3 | EHCI の停止→リセット | 動作中の HCRESET を記録 | 2 と 3 を入れ替え |
| F4 | IRQ 全欠落 | IRQ を出さない | tick 側の `intr_collect` を消す |
| F5 | 共有線 | 他装置だけ → `IRQ_NONE` | 要因 0 でも `IRQ_HANDLED` |
| F6 | 途中 STALL | 制御の DATA で STALL → STALL、toggle が正しい | 完了判定を「最後の TD の比較」に戻す |
| F6e | actual は DATA だけ | DEVICE 8B → actual = 8 | SETUP の TD も actual に足す |
| F6l | short_ok=0 の short (R4-01) | 制御 IN が short → HC が直結の STATUS を 1 回実行 (偽 HC が数える)、結果 SHORT_NG、STATUS の誤りは STALL で返る / bulk の最後の TD の short → SHORT_NG | short の判定を `td == p->last` の後に戻す |
| F6s | short_ok=1 の short (R4-09) | 制御 IN が short → 停止点で止まり STATUS から 1 回再開、結果 SHORT・actual が正しい・`done` 1 回・STATUS の実行 1 回 | `resume` の条件から `hw_next_is_end` を外す (STATUS の後にも再開して 2 回目の STATUS を偽 HC が検出) |
| F6m | bulk の途中の TD の short | 2 個目の TD で short → 次の IN が緩衝に入らない | 途中の TD にも R=1 |
| F7 | 番地 0 は 1 ポートだけ (探索の直列) | 2 ポート同時接続 → 2 本目の reset は 1 本目の A2 の後 (偽 HC が番地 0 の重なりを検出) | 探索がポートの reattach の終わりを待たずに次へ進む |
| F8 | reset が終わらない | C_PORT_RESET が来ない → ERR → PORT_ENABLE を落とし次のポートへ | ERR で PORT_ENABLE を落とさない (偽 HC が enable のまま残るポートを検出) |
| F8a | SET_ADDRESS の失敗は無視 (FB) | STATUS を返さずに番地を変える偽装置 → 新しい番地で A2 が通り ATTACHED | A1 の SET_ADDRESS の失敗を ERR にする (R6-07) |
| F8r | re-enumerate (FB の 2 回) | A2 で 1 回だけ失敗する装置 → 2 回目で ATTACHED | re-enumerate を消す |
| F8e | A6 の途中で装置が消える (R4-02) | 1 個目の iface の attach の後、2 個目で応答が無くなる → ERR → `usb_free_device`: 1 個目の `cls->detach` が 1 回、`usb_dev` の返却が 1 回 (偽の確保器が二重解放・漏れを検出) | ERR で D1 を飛ばして `usb_dev` を返す |
| F9 | 転送中の抜去と DMA 遅延 | HC が今の frame で取得した ED の xfer を stop → frame が 2 進むまで通知しない・塊を返さない (毒の書き込みが無い) | XS_DMA_DELAY を飛ばして XS_CB_PENDING へ |
| F9u | 切り離しの順 | 外した ED の NextED を HC が辿れる | 外す ED の NextED を 0 にする |
| F10 | 枯渇 | 池の残り 3 頁 → OHCI#1 は動き、#2 は断られる | 確保失敗で全 HC を止める |
| F11 | 複合 HID | kbd+mouse の 1 装置 → 両方動く | `ifs[]` を 1 個に |
| F11a | iface の attach の失敗 | mouse の attach を失敗 → kbd・ep0・番地は保たれる | attach の失敗で `usb_free_device_begin` を呼ぶ |
| F12 | 複数マウスのボタン | mouse 1 押下中に mouse 2 が 0 → 押下のまま | OR をやめ最後の値 |
| F14 | ハブの抜去 (R4-09) | 子 2 台のハブを抜く → 子が D1 の中で返り、その後にハブの番地と `usb_dev` が返る (`hc->devices[]` に残りが無い) | ハブの class の detach が子に `usb_free_device_begin` せずに終わる |
| G1 | **故障の入口 1 本 (R6-01)** | 偽 HC で UE を、(a) 探索の R2 の待ち中、(b) A4 の途中、(c) 割込み IN と bulk の実行中、(d) unsetup の DMA 遅延中、(e) 前景の `usb_xfer_sync` の途中、(f) 時計を止めて DMA 遅延の期限切れ、(g) **scan が done を予約した同じ周に別の xfer の DMA 遅延が期限切れ** (R7-01)、(h) **前景の `usb_xfer_sync` の待ちの途中の故障** (R7-02)、の各時点で起こす → どれも `HC_DEAD`、`hc->dma[]` が全部隔離 (偽の確保器が再利用を検出)、その HC の通常のメモリ (xfer・dev・cfg・sc) が**解放されていない** (偽の確保器の解放の記録が 0)、`cls->gone` が装置・iface ごとに 1 回、`done()` は 0 回 ((g) の予約済みも呼ばれない)、(e)(h) は戻り値 `-USB_E_HC` で、戻った呼び手が xfer・sc を読める、以後その HC への MMIO の書き込みが無い、もう 1 本の OHCI は動き続ける | ① `usb_hc_dead` で隔離せず `usbp_dma_free` する / ② `cls->gone` を xfer の通知でも呼ぶ (2 回目を偽の class が検出) / ③ 予約済みの `done()` を HC_DEAD でも呼ぶ ((g) で done が 1 回以上) / ④ `usb_hc_dead` の後に `continue` せず同じ HC の処理を続ける ((g) で死んだ HC への MMIO の書き込みを検出) |
| G2 | 起動途中の失敗 | BM の前で失敗 → DMA を返し HC_OFF・隔離なし / BM の後 → `usb_hc_dead` | BM の前の失敗でも隔離する (池の残りが減って検出) |
| F6r | **2 組の交互 (R5-01)** | 割込み IN の done の中で直ちに再 start → HC が今の frame で持っている前の組の ED/TD に書かない (偽 HC が毒・上書きを検出) | start で `curr_set` を切り替えない |
| F9d | **2 回続けての stop (R5-05)** | stop → 通知 → start → stop → 2 回目も frame 2 進みまで通知しない | start で `did_dma_delay = 0` にしない |
| F9x | **unsetup の完了 (R5-03)** | 正常完了の直後に class の detach → `xfer_free` は frame 2 進みの後、sc はその後に返る (毒の検出) | `usb_xfer_unsetup_done` を `usb_xfer_drained` と同じにする |
| F8p | **enable の変化 (R5-04)** | 接続変化なしで enable=0 にした偽ハブ → C_CONNECT を補って再接続 (5 回まで) / enable=1 のままの変化 → ログだけ | 条件を enable=1 で再接続に戻す |
| F8s | **SET_ADDRESS が先 (R5-06)** | 番地 0 で GET_DESCRIPTOR に答えない偽装置 → A1 → A2 で ATTACHED | A2 を A1 の前に置く |
| F8c | **設定の失敗は無視 (R5-06)** | SET_CONFIGURATION を 2 回とも STALL → 構成なしで載り、走査は次へ | A5 の失敗を ERR にする |
| F8g | **re-enumerate の内側の再試行 (R5-07)** | RE4 で 1 回だけ失敗する偽装置 → 500ms 後の 2 回目で通る | RE の内側の 500ms の再試行を消す |
| F8h | **RE1 の reset の失敗 (R7-03)** | RE1 の reset が 1 回だけ失敗する偽ハブ → 500ms 後に再 reset して成功し ATTACHED (FB:usb_request.c:2195-2199・2239-2246) | RE1 の失敗だけを内側の再試行に入れず即終了にする |
| F8l | **構成の全長 (R5-08・R6-06)** | wTotalLength=300 → 300B を取り直し、interface・端点が見える / 600 で 512B の切れ目が記述子の境目 → 512 に切り、ヘッダを 512 に書き換えて続ける / 切れ目が記述子の途中 → 「境界を失った」で構成全体を拒否し構成なしで載る | 9B を取った時点で `cfg` に写す / ヘッダの書き換えを消す (512 で検査に落ちる) |
| F8v | **読み直し (R5-09)** | 子の free の途中で抜く → R0b で CCS=0、列挙しない / reset 中に差し替え → R2b で R0 へ 1 回戻る | R0b・R2b の読み直しを消す |
| F4m | **走査中の RHSC (R5-10)** | ポート 3 の処理中に RHSC → ポート 1・2 を今の走査でやり直さず、走査の終わりに次の走査 (偽 HC が reset の回数を数える) | RHSC で走査を X0 に巻き戻す |

- **実機の受入の前提 (ユーザー決定 2026-11-09)**: 段 1 の実機の受入 (A9) の前に、ユーザーが HighPoint (0:12.0) を抜いて uPD720101 のカードに入れ替える。挿した後の `lspci` で 1033:0035 × 2 (fn0・fn1) と 1033:00e0 (fn2) を確かめてから A9 に進む。それまで実機の構成は変えない。

## 12. 未決 (ユーザー判断)

| # | 問い | 状態 / 推奨 |
|---|---|---|
| U4' | 段 3 で書き込みを受け入れるときの対象の媒体 (将来) | 書きの回を始めるときに尋ねる |

- U1 (3 段)・U3 (割り当て)・U4 (読み先行) は PM 決定、U2 (入れ替えは実機の受入の段で) はユーザー決定で確定 (本文に反映)。カード実物の電源・キーの形は入れ替えのときに確かめる (チップの 5V 耐性は S16265EJ5V0DS)。

## 確定 (2026-11-09、astra の第 7 回、PM が反映)

| ID | 反映 |
|---|---|
| R7-01 | `usb_hc_dead` の後、その HC の通常のメモリも再起動まで返さない。呼んだ service のループは直ちに次の HC へ、その HC の予約済み done・走査・RHSC・時計の確認は以後行わない。G1 (g) と変異 ③④ |
| R7-02 | `gone` は上に 1 回知らせるだけで sc を返さない (上は以後呼ばない)。前景の `usb_xfer_sync` は HC_DEAD で誤りを返し、メモリは生きているので戻った後も参照してよい。G1 (h) |
| R7-03 | F8h: RE1 の reset が 1 回だけ失敗 → 500ms 後に再 reset して成功、変異は「RE1 の失敗だけ即終了」 |
| R7-04 | `unsetup_frame`・`unsetup_deadline_ms`・`unsetup_next` を `struct usb_xfer` に足し、基準 frame は UNSETUP_DELAY に入るときだけ設定 |

## 反論

- なし (R6 の 8 件すべて反映。R6-01 は直し案ではなく PM 決定の簡素化で閉じた)。

## 確定の後の追記 (2026-11-10、今の main cfb9cec3 との前提の照らし直し、PM 決定)
- **BIOS が BusMaster+・割込みを持ったまま渡す HC の静止 (起動の早い段、常駐側)**: 実機では BIOS が Command の BusMaster+ で HC を渡すので、§4-2-2 の「BM を上げる前は DMA しない」は usb.mod の probe の時点では成り立たない。さらに BIOS の HC の level の割込みは GeForce (`hwdev/gf/irq.c:99`、0x3C の線で SHARED)・CS4231 (`pcm_cs4231.c:548`)・82557 (`e82557.c:697`) と共有の線に乗り得て、usb.mod を読む前・入れない構成では誰も ack せず、共有の相手ごと storm でマスクされる (`kernel/irq/irq.h:53` `irq_storm_masked`)。そこで vmkernel の常駐側に `drivers/usb/usb_early_quiesce.c` を足し (1E)、`pc98_boot_pci_resources` の予約 (§5-3) の直後に、usb.mod の有無に関わらず 3 機能を静止する。順は手本 FreeBSD `sys/dev/pci/pci.c` の `ohci_early_takeover`・`ehci_early_takeover` (`hw.pci.usb_early_takeover` の early quirk) と `sys/dev/usb/controller/ohci_pci.c`・`ehci_pci.c` に倣う:
  - OHCI: `HcControl.IR` が立っていれば `HcCommandStatus.OCR` → 1ms × 100 待ち、残れば `HCFS=RESET` (§4-2 #1 と同じ) → `HcInterruptDisable=ALL` (MIE を落とす) → `HCFS=RESET` で並びを止める → **最後に** Command の BM を落とす。BM を OCR の引き継ぎより前に落とすと BIOS の SMM と衝突し得るので、BM は必ず引き継ぎと割込みの禁止の後。
  - EHCI: §4-1 #4 の LEGSUP の引き継ぎ (OS 旗 → BIOS 旗を待つ) → `USBINTR=0` → `USBCMD.RS=0` で HCHalted を待つ → `CONFIGFLAG=0` → BM を落とす。期限切れは §4-1 #4 と同じく FAILED と記録し、`USBINTR=0` と BM を落とすことだけはする。
  - usb.mod の §4-1 #4・#5 は残す (引き継ぎ済みなら即座に通る)。これで probe の時点は BM=0 に戻り、§4-2-2 の `ST_BM` の旗と「上げる前の失敗は HC_OFF」は元の意味のまま成り立つ。
  - os32powerpc の指摘: G4 でもキーボード・マウスは USB (KeyLargo の OHCI) なので、この段は pc98 の殻ではなく共通の `drivers/usb/` に置き (`tools/check_usb_portable.py` の対象)、レジスタは `include/endian_le.h`・`include/endian_dma.h` (`mmio_rd32_le/wr32_le`) を、DMA の口は `include/dma_contract.h` を必ず通す (直の番地・x86 の asm を書かない)。早い段は DMA の塊を取らない。G4 は platform の PCI の backend から同じ関数を呼ぶ。
- **未割当の BAR と EHCI の 256B**: BIOS が番地を割り当てない機能の BAR は 0 ではなく大きさの測定値のまま (OHCI 0xFFFFF000・EHCI 0xFFFFFF00)。判定は `kdebug/gfinit_resources/bounds.inc:38-42` の `resource_unassigned` と同じ「0 または `~(len-1)`」にし、`pci_mmio_reserve`・`platform/pc98/pci_resources.c` (今は `:46-49` で番地 0 だけ飛ばし、0xFFFFF000 を RAW の資源として記録する) も同じ判定を使う。長さの表 (OHCI 4KB・EHCI 256B) は `kdebug/gfinit_resources.c:31-45` と 1 か所に寄せ、kdebug と pc98 の両方が読む。
  - 未割当の機能は probe で断り (理由を `pci_bind_set_reason`)、他の機能は続ける。早い段の静止も未割当の機能は MMIO に触らず BM・MEM を落とすだけ。EHCI が未割当なら §4-1 #4・#5 (引き継ぎ・静止) を飛ばし、OHCI は続ける (BIOS は番地の無い EHCI を使えず、CONFIGFLAG はリセットの値 0 で全ポートが OHCI 側)。
  - EHCI の 256B は 4KB の頁を他の BAR と共有し得る。重なりの検査は頁ではなくバイトの範囲 `[base, base+len)` で行い、頁の共有は同じ PCI 装置 (同じ bus:dev の別の機能) とだけ許して写す。別の装置の BAR と頁を共有するときは EHCI を断る (理由を残す)。fixture は `tools/tests/gfinit_bus_host.c:431-435` (0xfffff000) に倣う。
