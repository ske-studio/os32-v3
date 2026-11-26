# sys/net/host_mailbox.c — 知らせのメールボックスと、ALIVE の非消費の観測

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `sys/net/host_mailbox.c`・`sys/net/host_mailbox.h`・`sdk/include/os32/os32_host_notice.h`・`sys/kapi/kapi_host_notice.c`・`sys/kapi/kapi_host_notice.h`・`sys/include/remote_control.h`・`userland/gshell/src/wm.rs` (`deliver_host_notice`)・SDK は `userland/lib/host/host_call.c` (`os32_host_notice_read`)

## 何をするか

[net/link_notify](link_notify.md) が受理した知らせを溜め、アプリがサービスの番号で 1 件ずつ取り出す。
同時に、ALIVE (ホストの起動と復帰) の観測を、アプリの消費と無関係に公開する。

## 状態 (このファイルが単独で持つ)

- 64 枠 × 256B (`HostNotice`) を起動時にヒープから 1 回確保する (`host_mailbox_init`、`sys/platform/pc98/kernel_boot_memory.inc` の `kernel_boot_lan` が NIC を始める前に呼ぶ)。確保に失敗したら通知の口は `NOSYS`、通信そのものは続ける。像に初期化済みの配列を置かない。
- 到着順の添字の列、投入の通番 `serial` (1 から)、捨てた数 `dropped`、今の世代、最新の ALIVE の 1 枠、ALIVE の観測 (`struct HostAlive`)。
- すべての読み書きは IF を保存して短い cli 区間で行う。IRQ0 (link の tick) からの `put` と task からの取り出しが交わるため。

## 入れる (`host_mailbox_put`、IRQ0 から)

- 満杯なら最古を捨てて `dropped++`。
- 世代が変わったら、それまでの知らせを全部捨てて `dropped` に足す (最新の ALIVE は 1 回だけ数える)。
- ALIVE: 受理するたびに観測の通番 `observation++` と世代を同じ cli 区間で更新し `valid=1`。リングに入れるのは世代の最初の 1 通だけ。同じ世代の ALIVE は、まだ読まれていなければその 1 通を上書きし、読まれていれば最新の枠だけを更新する。定期の ALIVE で普通の知らせを押し出さない。
- 観測はメールボックスの確保の失敗・アプリの消費・link の再同期では消えない。旧い長さの HELLO では `valid` を立てない。

## 取り出す (KAPI v82、slot 259・260)

```c
int kapi_host_notice_read(u32 generation, u32 service, HostNotice *out); /* KAPI 名 host_notice_read */
u32 kapi_host_notice_serial(void);                                       /* KAPI 名 host_notice_serial */
```

- `read` は 2 段: `host_mailbox_stage` (非消費で、一致する最古の 1 件をカーネルの一時領域へ) → 呼び手へ写す → 写せた時だけ `host_mailbox_commit` (同じ世代・同じ serial が残っていれば消す)。写している間に IRQ0 が押し出して置いた新しい知らせは消さない。不正な CPL3 のポインタや写しの失敗は消費しない。
- 戻り: 0 / `AGAIN` (空。`HOSTDOWN` ではない) / `INVAL` / `NOSYS` (未確保) / `STALE` (指定した世代が古い)。
- `service=65535` (`MAILBOX_ALL`) は全サービス。ただし操作口の予約番号 (`REMOTE_SVC_RSHELL`・`REMOTE_SVC_GSERIAL`・`REMOTE_SVC_PPC_DIAG`、65532〜65534) は全部の取り出しに含めない。`generation=0` は全サービスの時だけ「今の世代」。
- キューは共有の競合消費。購読・複製は無い。他のアプリが先に取って空でも正常。
- `serial` は最新の投入通番を非消費で返す (初期 0)。
- 公開型 `HostNotice` の定義は `os32_host_notice.h` の 1 か所だけ (C89、256B)。wire とは memcpy しない。

## 使う側

- gshell: `deliver_host_notice` が `host_notice_serial` の変化を見て、前景のアプリの ring に `GUI_EV_HOST_NOTICE` (payload なし) を 1 件置く。ring が満杯なら次の周回でやり直す。アプリは SDK で `AGAIN` まで読む。
- SDK: `os32_host_notice_read(name, out)` が LIST で名前を番号に引いて読む ([userland/lib/host/host_call](../userland/lib/host/host_call.md))。
- ALIVE の観測の口 `host_alive_snapshot()` は世代・通番・valid を 1 回の cli で写す。/host の常駐は世代の差で FD を失効させ、通番の差では張り直しの予約だけをする (`docs/design/fs/serialfs/serialfs_persistent.md`)。netcd は次のアクセスで通番を比べる ([drivers/block/netcd](../drivers/block/netcd.md))。
- 未実装: ホスト世代が変わった復帰で最初に os32.time の時刻合わせへ渡す結線 ([OS32_TIME_DESIGN](../../decisions/time/OS32_TIME_DESIGN.md)、まだ実装が無い)。

## 確かめ

- `test_host_mailbox.py (旧版 811ce48ef1d8)` (65 通で最古を捨て drop=1、サービス別、同世代の ALIVE の繰り返しで押し出さない、全部の取り出しの絞り込み)・`test_host_notice.py` (KAPI、copyout 中の IRQ の投入で新しい serial を消さない、不正な out で不消費)・`test_hostsvc_contracts.py` (256B と各 offset)。
