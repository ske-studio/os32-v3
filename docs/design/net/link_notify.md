# sys/net/link_notify.c — ホストからの知らせ (NOTIFY) の受け手と、HELLO の伸長

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `sys/net/link_notify.c`・`sys/net/link_notify.h`・`sys/net/link.h`・`sys/net/link_rx.inc`・`sys/net/link.c` (`link_notify_control`)・送り手は `tools/host_notify.py` ([tools/host_notify](../tools/host_notify.md))

ホストが自分から送る知らせを、要求のハンドルとは別の系列で受け、`docs/design/net/host_mailbox.md` に入れる。
状態機械を進めるのは `link_tick()` の中だけ。task の側から呼ぶ口は無い。

## 通信の形式 (全部 LE、構造体で読まない)

```text
link op=11 NOTIFY: 既存の 20B ヘッダ、rid=0。seq/ack は要求の系列と独立。
payload: host_generation u32, service u16, kind u16, length u16, reserved u16 (=0), data[0..236]
ALIVE: service = LIST の os32.alive の番号、kind=1、data = host_generation u32 (4B)
ACK/WINDOW (OS32→ホスト) も rid=0。WINDOW の単位は通数 (4 固定)。
```

- service 0 は未割り当て、65535 は「全部」の取り出し専用で、知らせには使わない。番号は LIST がホストの世代の中で割り当てる ([tools/host_services](../tools/host_services.md))。
- HELLO の応答は新旧の長さを両方受ける: SYN-ACK 6B / 12B、ESTABLISHED 2B / 8B。伸ばした分は末尾に `host_generation u32` と ALIVE の番号 `u16`。SYN・CONFIRM は 0B のまま。それ以外の長さは捨てる。旧い長さのときは伸ばした欄を読まず、内部の ALIVE の観測を無効にする (LIST を引くまで ALIVE を識別しない)。

## 受け手の規則 (`link_notify_rx`)

- `link_rx.inc` が先に宛先・相手・sess/epoch・rid=0 を確かめてから渡す。古いセッションのフレームはそこで捨てる。
- 中身の検査: 世代が 0 でない、service が 1〜65534、reserved=0、length が実長と一致、世代が確立時に知った世代と一致。ALIVE は長さ 4 と data の世代の一致も見る。不正な知らせは ACK せずに捨てる (今の送り手は作らない)。
- Go-Back-N: 次の seq だけを受け、抜けも重複も最後の連続した seq を ACK する。u32 の一周は等号で扱う。
- メールボックスが確保できていない (`NOSYS`) ときも ACK して通信を止めない。保存できない知らせも受理したら ACK する。
- `link_notify_session()` (ESTABLISHED の受理時) と `link_notify_reset()` (新しい HELLO の開始時) は通知の系列だけを初期化する。メールボックスと ALIVE の観測はセッションをまたいで残る。確立直後に WINDOW を 1 回出して、ホストの送信を始めさせる。
- ACK・WINDOW の送出は `link_notify_tick()` (`link_tick()` の末尾) が 1 tick に 1 回試み、NIC が受けなければ次の tick に残す。再送のタイマーは送り手 (ホスト) が持つ。

## 境目

- 要求のハンドル (8 本) と本文のリングを消費しない。WINDOW の通数は要求のバイトの credit と別。
- ALIVE の意味 (ホスト世代の変化で /host を失効させる等) は `docs/design/net/host_mailbox.md` の観測の口が受け持つ。IRQ0 から lan.mod や VFS を呼び戻さない。

## 確かめ

- `test_link_notify.py (旧版 422c6dd181af)` (`link_notify_host.c`、時計を 0xFFFFFF00 から始めて一周を見る)・`test_hostsvc_contracts.py` (wire の offset と長さ)・`test_host_notify.py` (送り手)。
