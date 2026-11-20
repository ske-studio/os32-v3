# PowerPC LAN RSH の OUT 欠落調査（d6ffafa78）
提示された証拠では、7455 の失敗時にゲストは id 5 を done と報告し続け、ホストには完了に必要な OUT が届かなかった。欠落箇所と CPU 依存性は未確定。

## ホスト
- `events.json` の rid は link 要求番号、READY 内の id は RSH 操作番号。記録点は READY と全 WDATA 到着後の OUT のサービス呼出し前（`tools/ppc/link_sh_run.py:162–166`、`tools/ppc/usb_run.py:640–653`、`tools/host_agent.py:694–698,810–824,1025–1035`）。15〜21 の欠番では、REQUEST・部分 WDATA・RELEASE・別サービスの有無を判定できない。完全な OUT が Operator に拒否されても、この記録点には残る。
- Agent は受信 rid で high_water を上げ、間を HOLE にして先へ進む（一度に最大64）。欠番そのものへの再送要求は出さず、遅着 REQUEST は HOLE なら受理し、ACTIVE の重複には ACK／保持応答、RELEASED には TOMBSTONE を返す。先着 RELEASE も墓標になる。WDATA の seq 飛びには既存の累積 ACK を返す（`tools/host_agent.py:604–613,638–671,674–698,715–723`）。
- 同じ boot の CUI READY done は `op.ready` を更新する。OUT がなければ `pending.result=None`、`results` に対象 ID は入らず、pending は running のまま期限を迎えて UNKNOWN になる。待機ループと期限判定の順序次第で例外時点は running もあり得る。遅着 OUT は照合と本文検査を通れば確定できる（`tools/host_rshell.py:62–84,126–148,159–218`、`tools/host_operator_socket.py:206`、`tools/ppc/usb_run.py:580–606`）。

## ゲストと原因候補（優先順）
1. **OUT の送信が一度の exchange で打ち切られ、本文が回収不能になった経路が最有力。** done は FREEZE／collect／OUT より前にセットされる。exchange は open の負値（AGAIN/FULL/HOSTDOWN 等）で即終了し、書込・応答待ちも無進捗1秒またはエラーで close。失敗後は READY を再開するが、同じ OUT を送る処理がない（`userland/shell/rshell_lan.c:91–153,171–187,208–222,294–308,338–355`）。これだけで同じ症状になる。どの戻り値だったかは未確認。
   - rid は open 成功時、NIC へ渡す前に消費（`net/link_host_api.inc:174–224`）。GEM 満杯（31件）／busy は AGAIN になり、link は送信印と巡回位置を保持して次周回で試す。受理後の未 ACK REQUEST/WDATA は同一 rid/seq で200msごとに再送する。一方、無進捗1秒でハンドル期限処理が先に走り、STALE として送信を止め RELEASE に切り替える（`drivers/net/gem/gem_tx.c:8–36`、`platform/powermac/nic_gem.c:22–45`、`net/link.c:354–375,481–525,551–572`、`net/link_host_api.inc:31–65`）。
2. **OUT の組立前に失敗。** FREEZE（NOSPC 以外）／stat／collect の失敗も done のまま END に進む。通常の capture overflow は drop 付き OUT を作る経路なので、overflow 単独より各戻り値を確認する（`userland/shell/rshell_lan.c:264–290,338–355`、`kernel/console/con_sink.c:160–174`）。
3. **1 を誘発するタイミング差。** DEC と FG1 の NIC poll、FG1 の ms 間引き、共有 TX 予算により処理間隔が変わる。GEM の5秒 watchdog より前に1秒の要求期限へ達し得る。DMA 公開／回収は両 CPU 共通で、7455 には HID0 NAP・HIGHBAT/XBSEN の差がある。DEC 復帰間隔、DMA sync、GEM 完了、UDP 受信を測るまで、7455 の2件から因果は決められない（`platform/powermac/link_platform.c:1–35`、`net/nic.c:288–302,335–369`、`arch/ppc/kernel_cpu.c:37–42,64–79`、`arch/ppc/kernel_dec.c:49–72`、`drivers/net/gem/gem_tx.c:25–62`）。

## 正典との照合
- `docs/design/tools/host_agent.md:31–38`、`docs/design/net/link_wire.md:47–49,75–105`、`docs/design/net/link_host_api.md:20–29` の HOLE・再送・期限は上記実装と整合。駆動の記述には差があり、wire.md:70–72 は100Hz tickのみだが現行には open/write の前景 TX kick と FG1 の進行がある（`net/link.c:529–544`、`net/nic.c:291–302`）。
- `docs/decisions/network/LAN_OPERATOR_DESIGN.md:52–61,73–78,161` は link 内再送、同一コマンドの再投入禁止、完全な OUT による確定を規定する。exchange 期限後の OUT 保持／再配送は規定されず、実装にもない。 `docs/decisions/ppc/PPC_LAN_DESIGN.md:98–112` の再送なしは旧 PLN1 LOG/BEACON の規定。

## 修正と切り分け
- 第一に既存失敗回の両方向 `frames.log`（`tools/ppc/link_sh_run.py:188–203`）で sess/epoch/rid ごとの REQUEST→ACK→WDATA→RESPONSE→RELEASE を照合する。REQUEST が無い場合は guest 側の FREEZE/stat/collect/open の rc と rid、NIC 受理までを記録。途中までなら WDATA seq／ACK、期限、NO_SLOT、link_tx_deferred、GEM TX head/tail/pending・RX drops、DEC/TB 時刻を突き合わせる。既存 lantrace の out_send/out_done には rc がない（`userland/shell/rshell_lan.c:208–222`）。
- 再現案は単一起動の既存 USB/link セッションで、同じ像・QEMU 設定のまま問題の v1-owner／v1-dirty と echo を反復し、7450/7455 で回数と時刻を揃える。隔離した試験の UDP 層で OUT の REQUEST または WDATA を1フレーム捨てて同一 rid 再送を確認し、次に対象 OUT だけ無進捗期限まで遮断して READY done／UNKNOWN を再現する。46回の起動は不要。
- 修正案はゲストで確定 OUT（世代・boot・id・drop・本文）を期限付きで保持し、一時失敗後に同一操作の本文だけ再配送する。応答だけ消えた場合に備え、ホストは同じ操作キー・同一内容の再配送へ200を返して結果配送は一度とし、異なる内容は拒否。世代／boot 変更や上限到達で破棄。実行回数1、同一世代の完全な OUT・drop=0・既存の本文検査を受入条件に保つ。キャプチャ失敗や TX 停滞が観測された場合は該当箇所も修正する。

静的調査のみ。preflight の基点・環境は一致し、成果物 vmkernel.lz4／kernel.map は未生成。調査範囲につきビルド・lint・QEMU・受入は未実行。失敗回の生フレームは提示資料に含まれていない。
