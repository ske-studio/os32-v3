# SerialFS の期限切れと BSD 実装の照合

|対象|照合した版|
|---|---|
|OS32|97fb644ac|
|FreeBSD|6eae951 (2026-11-06)|
|NetBSD|36eddebc (2026-11-04)|
|OpenBSD|b907757 (2026-11-06)|

BSD のパスは各 clone のルート、OS32 のパスは worktree のルートからの相対パス。

## 1. 期限切れの要求の片付け

|観点|BSD の根拠|OS32 の根拠と判定|
|---|---|---|
|UDP の待ちと再送|FreeBSD `sys/rpc/clnt_dg.c:420,449–476,531–538,544–568,640–677,765–780`。送信前に XID を pending に載せ、`sosend` が mbuf を引き取る。待機終了で登録を外し、送信中の数を戻して必要なら待機者を起こす。総タイムアウトの分岐は再送・再登録より先に抜け、出口で要求と残存応答を解放する。|**合う。** `sys/fs/serialfs/sfs_client.c:233–255,272–276` は前景の再送ループを抜け、put 試行済みなら同じフレームと期限で一度 cancel する。`sys/fs/serialfs/sfs_client.h:40–43` は非待機・冪等・同期通知を規定する。共通 client の待ちはこのループで終わる。|
|TCP の待ちと送信済みデータ|FreeBSD `sys/rpc/clnt_vc.c:350–351,390–414,430–460,468–511,590–603`。送信バッファ待ちは送信側の同期経路。ゼロ待ち、受信待ちタイムアウトとも待ち登録を外し、応答 mbuf と要求を解放する。socket が引き取った送信データを期限切れ出口で撤回する処理はない。|**合う。** `sys/platform/powermac/hostfs_link_io.c:64–79,134–165` は pending と期限・長さ・全バイトが一致したら finish。OPEN 前は予約を消し、OPEN 後は close を一度発行する。`sys/kern/dev/device_host.c:24–27` と `sys/net/link_host_api.inc:343–380` で RELEASE を予約し、REQUEST/WDATA の送受信待ちと handle を回収する。RELEASE の到着や相手で実行済みの処理の取り消しは保証の範囲外。|
|返却後に届く応答|FreeBSD `sys/rpc/clnt_dg.c:1098–1131`、`sys/rpc/clnt_vc.c:1191–1220`。pending 上の XID にだけ配送し、配送時には登録上の XID を 0 にして重複を抑える。一致しない応答は解放する。NetBSD NFS も `sys/nfs/nfs_clntsocket.c:401–414,695–704` で待ちリストを照合し、終了時に外す。|**PowerMac の link 層は合う。** `sys/net/link_rx.inc:32–40,211–219` は有効な rid が無い RESPONSE/DATA を棄却する。SerialFS も `sys/fs/serialfs/sfs_client.c:50–65,238,257–269,311–317` でデコーダを交換ごとに初期化し、HELLO は nonce、CALL は sid・seq・応答種別を照合する。nonce は失敗後も更新する（`sys/fs/serialfs/serialfs_persistent.c:163–168`）。|
|格納済み応答の寿命|FreeBSD `sys/rpc/clnt_dg.c:765–780`、`sys/rpc/clnt_vc.c:590–603`。配送済みで未処理の応答 mbuf も要求の出口で解放する。|**違う（P2）。** PowerMac は READ 完了で共有 RX に格納して pending を落とすため、その後 client が失効しても cancel は空振りし、未読分が残る。`sys/platform/powermac/hostfs_link_io.c:39–69,114–121,142–146` と `sys/fs/serialfs/sfs_client.c:239–258,272–276`。識別子照合で旧応答の採用は防ぐが、旧バッファが空くまで次の応答の格納を拒む。期限切れの交換が所有する RX だけを破棄する方式に直すのが妥当。|

- FreeBSD の再接続ラッパーは下位が返った後に再試行を判断する（`sys/rpc/clnt_rc.c:359–391`）。NFS 側は RPC 呼出しへのラッパーを持つ（`sys/fs/nfsclient/nfs_clport.c:922–939`）。OS32 も `sys/fs/serialfs/sfs_client.c:336–354` で交換の外側から同じ絶対期限を渡す。

## 2. 遅れた HELLO と peer の確定

FreeBSD は待ち登録への配送と登録削除を同じロックで順序付ける。既着の応答や upcall による正常起床を先に扱う経路があり、OS32 のように受信ごとに絶対期限を判定する方式とは違う。タイムアウトとして抜けた後は旧 XID に配送できない（`sys/rpc/clnt_dg.c:518–568,640–647,1104–1131`、`sys/rpc/clnt_vc.c:474–511,1191–1220`）。OS32 は get 後の期限判定で応答の採用を止める一方、LAN の get はその前に旧 nonce の HELLO で peer を確定し受信位置を進める（`sys/fs/serialfs/sfs_client.c:239–265`、`sys/fs/serialfs/sfs_lan.c:59–99,114–117`）。段 0 で確定と未読残留を観測し、次の HELLO が旧応答を採用した事例は観測されていない。**違う（P2、別票 SFS-X86-STALE-PEER）。** put 後の期限確認に加え、get 中の期限跨ぎにも備え、現行 nonce の応答を期限内に採用する時点まで peer の確定を遅らせ、失効した交換の未読分を捨てる。

## 3. 82557 の送信完了と watchdog

|手本|OS32 の根拠と判定|
|---|---|
|FreeBSD `sys/dev/fxp/if_fxp.c:1738–1747,2173–2196,2211–2224,2262–2265`。通常は CB の C で送信を回収。watchdog は再初期化し、selective reset で CU/RU を止めてから DMA map と mbuf を回収する。|**合う。** `sys/dev/net/e82557/e82557.c:425–437,516–546,579–604`。受付済み CB は C とバリアで再利用可能にする。NIC 側の期限を超えれば回復に進み、DMA 停止確認後に発行済み TX を失敗として回収する。|
|NetBSD `sys/dev/ic/i82557.c:1182–1197,1623–1642,1658–1688` は C を確認し、watchdog では software reset 後に回収。OpenBSD `sys/dev/ic/fxp.c:805–819,1062–1078,1120–1155` は通常の送信 CB の C を確認し、watchdog では selective reset 後に回収する。|**合う。** `sys/fs/serialfs/sfs_lan.c:130–150` は BUSY/AGAIN を put 内で処理し、受付成功で返る。期限切れ出口後は同じ交換の再送がなく、常駐 LAN の cancel は NULL、モジュール版は空実装（`sys/fs/serialfs/sfs_client.c:272–276`、`sys/dev/net/e82557/lan_body.c:5–11,25`）。段 0 の期限後の DMA 完了は許容できる。NIC 側に修正不要。|

## 4. 取り込み判断

- **P1 なし。** 段 1 の期限出口と段 2 の保留 CALL の解除は妥当。片道の期限切れと seq 更新の BYE も同じ所有権で閉じる（`sys/fs/serialfs/sfs_client.c:210–226`、`sys/fs/serialfs/serialfs_persistent.c:144–165`）。
- **P2（段 2／確定設計 §2）。** 「成功済み受信バッファを消去しない」を、期限切れの交換に属する未読応答は破棄する方針に改める。READ 完了直後と部分読出し後の失効を、直後の別要求の応答と組み合わせて確認する。共有 RX の残留はコード上確認した。次応答の格納拒否が実際に起きる時刻条件は未検証。
- **P2（既存別票）。** x86 の peer 確定と未読応答を §2 のとおり扱う。確定設計の「段 0 の後に決めたこと」どおり段 1 と切り分ける。
- **P3 なし。** 読み取り調査。既存の段 0 記録を参照した。preflight は `vmkernel.lz4` と `kernel.map` が欠けて NG。ビルド、登録チェック、実機・模型の再実行は未実施。
