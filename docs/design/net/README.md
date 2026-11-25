# net/ — 設計
> 状態: **現行 (2026-11-18)** — 出所: DEP_DIR_D1_DESIGN (D1 で移した)・NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `net/`

### link

| `sys/net/link.c` | `link_init()` で状態を有効化する前。失敗なら診断して return（初回は link_ready=0、既存状態を変更しない） |

link_init の登録の置き場は関数の先頭 (kmemcpy より前、登録が失敗したら状態を変えずに戻る)。

## Host Services の LAN の経路

ゲストとホストは EtherType 0x88B5 の上で 2 本のプロトコルを使い、1 枚の NIC (82557 か LGY-98) を共用する。

```text
アプリ ─ VFS /host ─ SerialFS 常駐 ─ SFS client ─┐        ┌ lanfs_host.py ─ HostFS / ISO の像
cdinst ─ /cd0 ─ iso9660 ─ netcd ─ SFS block ─────┤ NIC 共用 ┤
アプリ ─ host_* (KAPI v51) ─ link (要求・本文・NOTIFY) ─┘        └ lan_bridge.py ─ host_agent.py
                 └ 知らせ → メールボックス (KAPI v82) / ALIVE の観測 / GUI の出来事
```

| 経路 | ゲスト | ホスト | 運ぶ物 | 設計書 |
|---|---|---|---|---|
| 共用の NIC | `sys/net/nic.c`・`sys/net/nic_rx.c` | — | 先頭 3 バイト `05 53 46` で SFS、他は link | [nic](nic.md) |
| link | `sys/net/link.c`・`sys/net/link_host_api.inc` | `tools/host_agent.py` | TIME・CLIP・PRINT・GET (`os32.http`)・LIST | [link_host_api](link_host_api.md)・[tools/host_agent](../tools/host_agent.md)・[tools/host_services](../tools/host_services.md) |
| 知らせ | `sys/net/link_notify.c`・`sys/net/host_mailbox.c` | `tools/host_notify.py` | NOTIFY・ALIVE | [link_notify](link_notify.md)・[host_mailbox](host_mailbox.md)・[tools/host_notify](../tools/host_notify.md) |
| LAN 版 SFS | `fs/serialfs/` (常駐の /host)・`sys/dev/block/netcd.c` | `tools/lanfs_host.py` | /host のファイル (パス + offset)・仮想 CD のブロック | [serialfs_persistent](../fs/serialfs/serialfs_persistent.md)・[sfs_block](../fs/serialfs/sfs_block.md)・[netcd](../drivers/block/netcd.md)・[tools/lanfs_host](../tools/lanfs_host.md) |
| SDK | `userland/lib/host/` | — | `host_call`・LIST の名前の解決 | [host_call](../userland/lib/host/host_call.md) |

### 共通の決まり

- **カーネルはホストの「落ちている」状態を覚えない。** 要求ごとの 1000ms の期限で相手が答えなければ、その要求だけを `HOSTDOWN` で返す。再試行するかはアプリが決める。
- ホストは起動ごとに新しい世代 (0 でない u32) を持ち、ALIVE (起動時と 5 秒ごと) と HELLO の応答で知らせる。世代が変わったら既存の資源 (/host の FD・仮想 CD の FD・LIST の番号) は `STALE`。同じ世代の ALIVE と、休止による張り直しでは失効させない。
- IRQ0 は受信を進めて状態機械を回すだけ。ネットワークを待つのは task (協調点) だけで、IRQ から lan.mod や VFS へ呼び戻さない。
- 書き込みの許可はホストの `--allow-write` だけ。暗号・認証・送り手の許可リストは足さない ([POLICY_DEV](../../POLICY_DEV.md) §10)。接続の推奨 (普段の LAN と分けた口から 1 対 1) はリポジトリの README。
- NP21/W は既定で HostDrv が `/host`。LAN の SFS の試験は LGY-98 の上で同じ SFS を通す。
- シリアル版 SFS (`sfs run` の UART) は形式を変えずに逃げ道として残す。link と LAN 版 SFS を 1 本に寄せる計画は `docs/tasks/network/TASK_LAN_BULK.md` (設計済み・未実装)。それまでは 2 本が並ぶ。

### 誤りの番号 (`sdk/include/os32/os32_kapi_shared.h`)

| 値 | 意味 |
|---|---|
| `HOSTDOWN` (-18) | リンクが UP でない・NIC 未 bind・要求の相手が 1000ms 答えない |
| `TIMEOUT` (-19) | SDK の `host_call` の全体の期限 (送信の待ちの期限切れにも使う) |
| `STALE` (-11) | 既存の資源の失効 (古いセッション・ホストの再起動・媒体の交換) |
| `AGAIN` (-14) | まだ・空 |
| `ROFS` (-15) | ホストが書き込みを許していない |
| `IO` (-1) | 壊れた応答・媒体の I/O |

## 索引

- [sys/net/nic.c](nic.md)
- [sys/net/link_host_api.inc](link_host_api.md)
- [sys/net/link_notify.c](link_notify.md)
- [sys/net/host_mailbox.c](host_mailbox.md)
- [link_wire](link_wire.md)
