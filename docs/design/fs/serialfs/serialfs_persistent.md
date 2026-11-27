# sys/fs/serialfs/serialfs_persistent.c — LAN 常駐の /host (共有の SFS client・mount の世代・期限)

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `sys/fs/serialfs/serialfs_persistent.c`・`sys/fs/serialfs/serialfs.h`・`sys/fs/serialfs/serialfs.c` (`sfs_rpc`・`sfs_resource_check`)・`sys/fs/serialfs/serialfs_session.c`・`sys/fs/serialfs/sfs_client.c` (`sfs_request_deadline`・`sfs_client_call_deadline`・`sfs_client_hello_deadline`・`sfs_client_oneway_deadline`)・`sys/fs/serialfs/sfs_client.h`・`sys/fs/serialfs/sfs_lan.c` (`lan_put_until`)・`sys/platform/pc98/kernel_boot_memory.inc` (`kernel_boot_hostsvc`)・ホストは `tools/lanfs_host.py` ([tools/lanfs_host](../../tools/lanfs_host.md))

## 何をするか

- 起動の設定 `/etc/netcd.cfg` に `SFS_LAN=1` (と `SFS_LAN_PEER=<MAC>`) があれば、起動時に `/host` を SerialFS で mount し、電源を切るまで載せたままにする。HELLO は最初の操作の時まで送らない。相手がいなくても mount は消さない。
- `/etc/netcd.cfg` は起動 FD 用の設定で、install は HDD へ写さない ([drivers/block/netcd](../../drivers/block/netcd.md))。起動時は root の `/etc/netcd.cfg` を読むだけなので、HDD 起動で常駐させるには HDD 側に手で置く。
- 常駐でない時の `sfs run` (常駐シェルの中だけ /host を開く) は従来どおり。LAN か UART かは `/etc/system.cfg` の `SFS_LAN`・`SFS_LAN_PEER` で選ぶ (`sfs_session_fg.c`)。
- NP21/W は既定で HostDrv が `/host` を持つ。既に `/host` が載っていれば `sfs_persistent_start` は `BUSY` で、HostDrv を上書きしない。

## 誰が状態を持つか

- このファイルが唯一の共有 client (`struct SfsPersistent` の `client`) と mount の世代 `generation` を持つ。netcd (仮想 CD) も同じ client を使い、client を新しく作らない。
- `sfs_persistent_client()` は借用の読み取り専用 (sid・seq を見るため)。通信と変更は `sfs_ensure_session` と `sfs_request_deadline` だけが行う。
- 起動の結線 (`kernel_boot_hostsvc`) が `SfsLan` と時計の文脈を起動の寿命で持つ。lan.mod の参照は NIC の側 (`net_start`) が握る。

## セッションと世代

- `sfs_ensure_session(deadline)` が HELLO をし直す条件: sid が無い / 前回の使用から 30 秒 (`SFS_PERSISTENT_IDLE_TICKS`) / 張り直しの予約 / seq が 0xF000 を超えた (`SFS_PERSISTENT_SEQ_LIMIT`、超えていれば先に BYE)。HELLO の nonce は起動の token の CRC32 から始めて毎回 +1 し、失敗した交換の nonce を使い回さない。
- **sid と FD の世代は別。** 30 秒の休止や seq の上限による張り直し、同じ世代の定期の ALIVE では既存の FD を失効させない。
- FD を失効させる (世代を進める) のは 2 つだけ: ホストが知らない sid だと答えた (`SFS_T_ERR`、`sfs_persistent_stale`)、ALIVE のホスト世代が変わった (`sfs_alive_observe`)。古い FD の read・write・fstat・seek は `STALE`、close は解放できる。次の open は新しいセッションで始まる。世代が巡っても失効の印は戻さない (VFS の `vfs_fd_stale_mount`)。
- ALIVE は [net/host_mailbox](../../net/host_mailbox.md) の `host_alive_snapshot()` で観測する。通番が変わっただけなら張り直しの予約、世代が変わったら失効。観測を消費せず、HELLO の間に来た新着は次へ残す。
- ホストの停止中は「落ちている」状態を持たない。要求ごとに通信を試し、答えが無ければその要求が `HOSTDOWN`。連続の失敗で LAN の client を DEAD に固定しない。

## 期限 (10ms tick、比較は `(i32)(now - deadline) >= 0`)

- LAN の 1 回の RPC は入口から 1000ms (`SFS_OPERATION_TICKS`=100) の絶対期限を持ち、ensure・HELLO・送信の待ち・再送に同じ期限を渡して取り直さない。同じ sid/seq の再送は 100ms ごと (`SFS_LAN_RETRY_TICKS`=10)。答えが無ければ `HOSTDOWN`。
- 期限は VFS の 1 操作ではなく RPC ごと。大きなファイルの読み書きは RPC が続く間それぞれに 1000ms ある。
- 送信の待ちも期限に従う: `SfsIo.put_until` (任意、UART は NULL) を LAN は `lan_put_until` で実装し、待ちの上限は `min(開始 + SFS_LAN_PUT_MS, deadline)`。期限切れは `TIMEOUT`。
- 応答は `struct SfsReply` (呼び手が data・capacity を用意) に status ごと写す。容量不足は `NOSPC` (要る長さだけ返す)、壊れた応答は `IO`。status の解釈は `serialfs.c` の `sfs_status` (既知の負値はそのまま、それ以外は `IO`、ホストの書き込み拒否は `ROFS`)。
- ホストが `SFS_T_ERR` を返したら、1 回だけ送った要求 (再送していない) に限り、ホストで実行されていないので同じ期限の中で HELLO し直して 1 回だけ送り直す (write も)。再送した要求は送り直さない。
- 期限が切れた write は「適用されたか分からない」。新しい sid で自動的にやり直さない。同じ sid/seq の重複はホストの応答のキャッシュが二重に実行しない。

## `sfs run` との関係 (LAN の scope)

- 常駐している時、KAPI の `sfs_begin`/`sfs_end` (`serialfs_session_begin`/`_end`) は scope の開始・終了になる。`sfs_persistent_scope_begin_owned` は呼び元の owner を記録し、`sfs_persistent_scope_end_owned` は同じ owner の scope を閉じる。開始で ensure (1 秒) し、終了で BYE も unmount もしない。開始と終了の表示 (`sfs: transport=lan … sid=`・`sfs: exit=N`) は常駐でない時と同じ形。
- 常駐の間も `sfs run hsync` が使える。

## 落とし穴

- ホストの `SFS_T_ERR` は理由を問わず全部 `STALE` に扱う (知らない sid 以外の拒否も)。未整理の P3。
- scope の開始に失敗した時の表示が無い。成功時は transport の describe が返す NIC 名を表示する。
- ホストの再起動の後は FD が `STALE` になる。アプリは開き直す。

## 確かめ

- `test_hostsvc_persistent.py (旧版 030af773a023)` (sfs run の外の読み書き、コマンド終了後も mount、ホストの再起動で古い FD が STALE・新しい open は成功)・`test_hostsvc_sfs_deadline.py` (入口から 1000ms + 1 tick で HOSTDOWN、次の要求で再送信)・`test_hostsvc_serialfs.py`・`test_hostsvc_sfs_lan.py`・`test_hostsvc_open.py`・`test_hostsvc_init.py` (起動の結線と設定の鍵)・`test_serialfs.py` (UART の既存の動作)。

## 絶対期限での交換終了

確定設計 §2 の終了契約。期限付き交換は送信を試みた後の期限切れで、
同じ前景交換から任意の cancel を一度呼んでから HOSTDOWN を返し、timeouts を一つ増やす。
通知には put と同じ ctx、符号化済みの全バイトと長さ、絶対期限を渡す。
送信前の期限切れと正常応答では通知しない。常駐の同期 transport は cancel を 0 とする。
LAN モジュールはローダーが全関数スロットを照合するため、自身の text 内の空の cancel を登録する。
期限付き片道送信は put 中の期限到達、または put 自身の TIMEOUT で一度通知する。
seq 更新時に受理された BYE は ensure が所有し、次の時計確認と HELLO の組み立ての前に cancel する。

PowerMac link は保存した絶対期限・長さ・全バイトが一致する保留だけを解放する。
OPEN 前は予約を消し、ハンドルがあれば前景で close(handle, 0) を一度発行する。
同じフレームを有効な別期限で再送しても元の期限と結果を保つ。保留を取り消した後の再送は TIMEOUT を返す。
poll が先に終わった場合、後続の cancel は完了結果を保つ。絶対期限・長さ・全バイトが一致し、
その交換の READ が共有 RX に格納した応答に限り、未読分をすべて破棄する。
seq 更新の BYE は OPEN 前に消える。ホストは次の HELLO の新 nonce で旧 sid を閉じる。
