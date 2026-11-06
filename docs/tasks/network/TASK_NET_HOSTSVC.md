# TASK_NET_HOSTSVC — ネットワーク越しの Host Services (実機の /host・ネットワーク越しの CD・知らせのメールボックス・サービスの一覧)

> 状態: **実装中 (2026-11-07)** — 詳細設計は確定 ([NET_HOSTSVC_DESIGN](../../decisions/network/NET_HOSTSVC_DESIGN.md)、astra 起草 r1→r3・Opus レビュー 3 回)。A1〜A11・B1〜B4・C5・D1・H1〜H4 と起動の登録を main に入れた (1cbba6f5、CI 成功。NP21/W の host_time・クリップボードと実機 82557 の LAN hsync を確認)。次の波 C1・C3・B5・H6 を実装中、その後に C2・C4・D2〜D8・H5・I・S。往復の版は `~/os32-tmp/run/net-hostsvc/`。
> 上位: [V3_PLAN](../v3/V3_PLAN.md) §5 の「ネットワーク越しの CD・HostDrv の Host Services 経路」。土台: [HOST_SERVICES_PLAN](HOST_SERVICES_PLAN.md) (リンク層の Host Services、KAPI v51)、[TASK_LAN_82557](../realhw/TASK_LAN_82557.md) (82557・LAN 版 SerialFS、L-C・L-D2・L-E)。

## 0. ユーザー決定 (2026-11-05、要旨。原文は往復の置き場の USER_DECISIONS)

| 項目 | 決定 |
|---|---|
| 範囲 | 「範囲はすべてです。」— 実機の /host の常時マウントと読み書き、ネットワーク越しの CD で cdinst を変えずにインストール、TIME・CLIP・印刷・wget を 82557 の実機で |
| プロトコル | リンク層 (net/link.c・host_agent.py) と SFS (fs/serialfs・lanfs_host.py) の 2 本を共存。「実装完了時には1本になることを目指します。とりあえず、通信の保護のために残します。」 |
| ホストの生存 | カーネルは「落ちている」状態を覚えず、要求ごとの短い期限で応答が無ければその要求を即座に HOSTDOWN で返す。再試行はアプリが決める |
| 知らせ | ホストが自発的に送る NOTIFY をカーネルのメールボックス (64 通 × 256B、ヒープ) に溜め、アプリがサービス名で取り出す。ALIVE (起動と復帰) はその 1 つ。同じ世代の ALIVE は 1 枠に上書き |
| 仮想デバイス | 「フロッピーディスクなどのブロックではなく、仮想デバイスで大丈夫です。」ホストの実機器はホスト側のラッパー。取り外せる媒体と同じ扱いで張り直し・同一性・媒体交換 |
| 書き込み | ホストの `--allow-write` に任せる |
| 起動 FD | 2HD の残り 87KB・下限 64KB で使えるのは約 23KB。超えたら外す物 (推奨 fm.mod → pcm.mod) は実測のときにユーザー判断 |
| NP21/W の試験 | LGY-98 の上で同じ SFS を通す |
| アプリの口 | KAPI は汎用 (任意の要求の行) のまま経路を隠す。SDK に `host_call`、ホストに `LIST`、ハンドル 2 → 8 本 |
| 名前 | 「OS 標準のサービスであるという明示的な箇所」— `os32.` で始まる名前だけを OS 標準 (ske-studio が定めた) として予約。SDK の標準の関数は `os32_host_*`。それ以外は自由、拒否の仕組みは作らない |
| サービスの一覧 | 仮想 CD は `os32.iso9660`、DVD は `os32.udf` を名前だけ。`os32.mail`・`os32.feed`・`os32.chat` はスタブ (名前だけ)。ime・build・exec・ai・conv.text は作らない、開発運用は deploy だけ |
| 別の設計へ | `os32.time` (下の §1)、`os32.conv.image` (資源ごとに返す形式を指定できる — 「リソースごとに送り返すデータの形式をリニアに変えれなければホストサービスとしてはあまり価値がない」)、`os32.audio` / `os32.midi` (PCM のストリームとファイルの 2 方式、タイムコードで同期)、`os32.screen` (優先度低め)。順番は time → conv.image → audio/midi → screen |
| 危険 | 平文・認証なしで OS32 はホストを無条件に信用する。対策はしないが設計の末尾に「把握している危険」を残す。接続の推奨 (普段の LAN と分けた口からクロスケーブルで 1 対 1) は [README](../../../README.md) |

## 1. os32.time の要件 — 設計は確定 ([OS32_TIME_DESIGN](../../decisions/time/OS32_TIME_DESIGN.md)、2026-11-05、実装はまだ)

- 起動時と、設定の周期で定期に時計合わせ。時計合わせのたびに RTC へ書く (電池の無い機械では残らない前提で運用)。
- ホストと切れている間は RTC と tick で進める。ホストの復帰 (ALIVE の世代の変化) を受けたら、ほかの張り直しより先に時刻を合わせて RTC へ書く。
- 標準時はホストに従う (OS32 にタイムゾーンの表を持たない)。
- ホストが無くても手で合わせられる (年・月・日・時・分、秒は 0)。シェルの `date` に設定の形、後でコントロールパネルから同じ口。
- いま RTC の時刻を書く口は無い (drivers/char/rtc.c は読みとモードの切り替えだけ)。

## 2. 段 (確定設計 §10・§11 の要約)

| 群 | 中身 | 像 |
|---|---|---|
| A (NIC 境界) | net/nic.h・nic.c・nic_rx.c (1 本の受信を SFS とリンクへ振り分け)、LGY・82557 の adapter、link.c・link_rx.inc・sfs_lan.c の付け替え | vmkernel (82557 の adapter と sfs_lan は lan.mod) |
| B (知らせ) | link.h の NOTIFY・HELLO の伸長、link_notify.c、host_mailbox.c (64 通・ALIVE の世代)、KAPI の取り出し、gshell の出来事 | vmkernel・gshell |
| C (常駐の /host) | serialfs_persistent.c ほか (sfs run の外でも mount、期限・HOSTDOWN・世代と STALE、seq 0xF000 で張り直し) | vmkernel |
| D (仮想 CD) | sfs_proto.h の BOPEN/BREAD/BCLOSE、sfs_block.c・drivers/block/netcd.c (lan.mod)、iso9660 の媒体の一般化、vfs_fd の失効、cd_body.c の空き cdN | lan.mod・vmkernel・cd.mod |
| H (ホスト) | lanfs_host.py・host_image.py・host_notify.py・host_linux.py (Ubuntu の CLIP/PRINT)・host_agent.py・lan_bridge.py | ホスト |
| I (統合) | kapi.json と生成物・誤り番号 (HOSTDOWN・TIMEOUT)・起動の登録・fd_netcd_config.py (起動前の FD の /etc/netcd.cfg)・packages.yaml・install.c の fd_only | — |
| S (SDK) | host_call.c・libos32host.c・os32api の host.rs (os32_host_*、LIST で名前から番号) | SDK |

順序: 契約 (A1・B1・D1) → adapter と H 群を並列 → C・D の結線 → I ([ABI] の clean・all) → NP21/W → 82557 実機。試験は `tools/tests/test_hostsvc_<項目>.py` (nic・deadline・persistent・write・image・isocache・mailbox・gui・linux・bootcd・seq・alive・race・handles・list・sdk・abi)。

os32.time の PM 決定 (確定設計の REVIEW から): `sys_time`・`clock_read_date` は今までどおりローカル時刻 (ホストの標準時に従う壁時計) を epoch として返す (C の time()・SQLite・FAT の源)。UTC とずれは os32.time の中だけ。NP21/W は I/O の RTC の書き込みを捨てるので書き込みの受入は実機 — NP21/W のフォークに TIME SET を足す段は別。RTC の読みは今のパラレルのまま、Ra266 で年を確かめてから。

## 3. os32.conv.image — 設計は確定 ([CONV_IMAGE_DESIGN](../../decisions/network/CONV_IMAGE_DESIGN.md)、2026-11-05、実装はまだ)

ユーザー決定「リソースごとに送り返すデータの形式をリニアに変えれなければホストサービスとしてはあまり価値がない」— 大きさ・色数・パレット (OS32 の既定・最適化・固定)・使ってよい index (index_mask)・減色 (自作の誤差拡散)・形式 (planar4・packed8・.osri・MGX) を要求で指定し、OS32 は面・VRAM にそのまま写す。初版は静的な .osri から (ラスタの .osri は後)。設計のレビューで見つかった Cirrus の既定のパレットの灰の段の違い (カーネルの不具合) は 4e1c9bfd で直した。
