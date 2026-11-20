# PowerMac 7455 の link RX 停滞
提示された frames.log の要約では、A は WDATA ACK/RESPONSE、B は最初の REQUEST ACK で進まなくなる。ゲストは TX 再送を続け、epoch 2 で復帰。約1秒は200 ms×約5区間の link 再送周期に合う（net/link.h:112–116、net/link.c:350–373）。RX が連続して止まった時間と、ホストの返答が QEMU の NIC backend に入った時刻は未確定。前回の OUT 再配送欠落より手前の現象。

## 候補順
1. **GEM から link キューへの前景配送不足。** DEC は1 ms、論理 jiffy は10 ms。jiffy で GEM を poll するが、IRQ 内 demux 救済の対象は E82557/条件付き LGY98。GEM の4枠が埋まると descriptor 回収が止まり、32枠中28枠の RX ring も詰まりうる。link_tick は link キューが空なら NIC を引かず、期限・TX 再送は進む（arch/ppc/kernel_dec.c:24–58、net/nic.c:273–393、net/link_rx.inc:256–265、platform/powermac/nic_gem.c:62–76）。
   全画面 present は OF FB への volatile 32-bit ストアと末尾 barrier。途中に FG1 がなく、ユーザー syscall 本体は EE を許すため DEC/TX と前景の空白は両立する。初回 present 診断はコピー後。1秒のコピーは未計測。FG1 の1 msゲートには受信直後の例外があり、時計と FG1 が進む条件では単独で1秒を説明しにくい（gfx/gfx_present.c:94–104、gfx/gfx_core.c:93–100、drivers/video/of_fb/backend_of_fb.c:39–72、arch/ppc/kernel_trap.c:105–115、net/nic.c:291–313）。
   修正案 — 前景が途切れたときも GEM が配送できるよう、量を限定した jiffy 救済を設ける。
2. **GEM の RX completion/OWN 停滞・FIFO overflow。** GEM IRQ は全マスク。completion まで回収し、先頭 OWN で停止。CRC/長さ不良でも消費し、head+28 の producer を補充して4枠ごとに kick。枯渇からの再開には回収と kick が要る。overflow 監視は400 ms後に FIFO/MAC を再検査し、停滞時に reset。LAN 前景が unbind・再構築・PHY 復帰を担うため、区間を通じた再送は長いフル reset と整合しにくい（drivers/net/gem/gem_ring.c:60–82、gem_rx.c:25–68、gem_reset.c:122–159、platform/powermac/lan_service.c:249–296）。
   TX reclaim と RX scan は各8の別予算。ただし TX completion 異常/EIO で RX 前に poll を打ち切り、4枠も捨てる。TX watchdog 自体は5秒（platform/powermac/nic_gem.c:62–76、drivers/net/gem/gem_tx.c:39–64）。
   修正案 — 観測した completion/OWN/kick の復帰条件を修正する。TX reclaim エラーなら安全に継続できる RX と分離する。
3. **OHCI の処理時間・共有 tick 予算、または link での棄却。** OHCI は IRQ attach を提供せず slot 0 と前景で poll。EE を落としたまま scan・コールバック・キーのログ生成を行う。slot 0 からの経過時間を link（slot 4）も消費し、RX 予算が先に尽きても TX は交互の先頭1件保証で進みうる。FG1 では IRQ 予算は非アクティブ。USB と GEM は別 DMA プールで、キー行は後で LOG 要求として送る（platform/powermac/usb_platform.c:71–87,180–183,209–259、drivers/usb/usb_service.c:50–86、platform/powermac/link_platform.c:9–11,43–57、arch/ppc/arch_tick_budget.h:4、kernel/irq/tick_budget.c:27–57、platform/powermac/link_log.c:44–105）。
   link キュー満杯は demux を止める。取り込んでも MAC/EtherType/長さ、sess/epoch/rid、ACK番号や RESPONSE 時の handle 状態で棄却・無視されうる。全再送で続く根拠は未取得（net/nic_rx.c:36–82、net/link_rx.inc:118–214,256–265）。
   修正案 — OHCI で予算を消費するならキーのログ生成を EE 有効な前景へ移す。検査落ちなら記録した項目の生成元を直す。
4. **7455 の NAP/DEC または仮想時間。** 7455 のみ NAP を設定し待機時の POW と組み合わさる。通常の復帰は約1 ms。遅れは負の DEC から補填する。描画中の NAP や連続1秒の DEC 停止は再送列と整合しにくい。HIGHBAT/XBSEN は起動時の差で、追加 BAT は無効化される（arch/ppc/kernel_cpu.c:32–80、kernel_dec.c:41–73、mmu.S:242–249）。
   修正案 — NAP 境界だけで欠落するなら NAP 無効の7455を対照にし、確認した復帰条件を修正する。
5. **DMA 可視性。** 16 B descriptor はキャッシュ禁止の先頭64 KiB内。初期化時 dcbf と再マップ、運用中は sync のみ。別領域の RX buffer は32 B整列・2 KiB全体を補充前 dcbf、回収後 dcbi、各 sync 付き（platform/powermac/gmac_dma.c:18–34,62–79、arch/ppc/dma_cache.c:22–50、drivers/net/gem/gem_rx.c:32–66）。
   修正案 — completion 進行中に OWN/内容が古いと確認できた段で、descriptor の別名/WIMG または buffer の同期範囲と所有権順序を修正する。
   7450/7455 はともに256 KiB L2＋外付け L3 対応。mac99/TCG に実機のキャッシュや命令タイミングを仮定できず、icount 設定次第でホストと仮想時間も離れる。PVR/HID0、OF の TB Hz（platform/powermac/clock.c:54–66）、実行時計を比較する。7450 の試行母数、QEMU 版と引数は未確認。[NXP MPC7450UM §10.2](https://www.nxp.com/docs/en/reference-manual/MPC7450UM.pdf)・[NXP MPC7455EC 表1](https://www.nxp.com/docs/en/data-sheet/MPC7455EC.pdf)・[QEMU mac99](https://www.qemu.org/docs/master/system/ppc/powermac.html)・[TCG icount](https://www.qemu.org/docs/master/devel/tcg-icount.html)

## 次の1回で取る値
- 既存のシリアル LAN STATE と gfx 行、frames.log、rshell lantrace の OUT 進捗をまず照合。STATUS/CLOCK は GEM RX/TX/drops/状態/TB/hz/jiffy を返すが、PLN1 または診断用 link イメージの経路。link-SH では PDIAG 無効で TIME 診断は疎通用。シンボルから読めるなら link_rx_frames/dropped、net_rx_link/sfs_dropped、link_tx_deferred の前後差も取る（platform/powermac/lan_service.c:20–39,313–319,361–362、lan_diag.c:60–89、lan_monitor.c:90–107）。
- 同じ QEMU 版・引数・像・入力で、ホスト送出と QEMU netdev の両方を捕捉。TB・tick_ms/jiffy・観測点を固定 RAM 履歴に残し、timeout/resync 前の履歴を保存して復帰後に読む。ホスト単調時刻と epoch/rid/op/seq/ack で突き合わせる。
- nic_gem.c:62–76 と gem_rx.c:27–66 に poll 開始/終了・rc・4枠占有・completion/head/OWN/最終 kick・TX tail/pending。gem_reset.c:128–156 の既存読み出し値から overflow/watch/reset の遷移も記録。netdev 到達済みで poll 継続＋completion 不動なら GEM 側、completion 進行＋OWN 停滞なら descriptor 側、4枠満杯なら下流を追う。
- nic.c:278–313、nic_rx.c:41–82、link_rx.inc:128–214,256–265 に FG 間隔・予算と拒否理由・両キュー深さ・該当フレームの取り込み/検査/ACK 適用・再送時の handle 状態を記録。4枠が空くのに link に着かなければ、この間で切り分ける。
- DEC 入り口の TB/負の DEC/補填数、NAP 前後、OF FB コピーと USB service の開始/終了（EE・描画バイト数・USB 完了数）を記録。各 ACK 欠落を覆う期間の poll 間隔・キュー・時計を比較する。

静的調査。生の frames.log は未入手。preflight は基点・環境が一致し、vmkernel.lz4/kernel.map が未生成。調査範囲につきビルド・lint・QEMU 再現は未実施。
