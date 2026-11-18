# tools/host_notify.py — ホストから OS32 へ知らせを送る側 (NOTIFY・ALIVE・ホストの世代)

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `tools/host_notify.py`・`tools/host_agent.py` (`Notify` の結線・HELLO の伸長)・`tools/host_services.py`

受け手と wire は `docs/design/net/link_notify.md`、溜める所は [net/host_mailbox](../net/host_mailbox.md)。

## 何をするか

- `host_agent.py` が起動ごとに 0 でない u32 のホストの世代を作り (`new_generation`、乱数)、`Registry` と `Notify` に渡す。再起動で新しい世代になる。永続しない (state-dir に世代を書かない)。
- HELLO の応答の末尾に `hello_extension()` (世代 u32 + `os32.alive` の番号 u16) を付ける。
- `notify(name, kind, data)` は LIST の登録簿で名前を番号に引いて積む。番号を直接指定する `notify_number` もある (操作口の予約番号用)。data は 236B まで。
- ALIVE: セッションを確立した時と、その後 5 秒ごと (`ALIVE_SECONDS`) に `os32.alive` kind=1 を送る。

## 送りの規則

- 未 ACK の上限 64 通 (`QUEUE_MAX`)。満杯なら最古の未送信を捨てて `dropped` に数える (未送信が無ければ新着を捨てる)。送った seq に穴を作らない。
- WINDOW は最大 4 通 (OS32 の WINDOW の値と小さい方)、累積 ACK、Go-Back-N、100ms (`RETRANSMIT_SECONDS`) で全部を再送。
- **沈黙:** 未 ACK が残ったまま、最後に受理した ACK (または空から非空になった時) から 2.5 秒 (`SILENCE_SECONDS`) 進まなければ、そのセッションを沈黙にする。未送信を捨てて数え、沈黙中の新しい知らせ (ALIVE も) も捨てる。沈黙の間は 2.5 秒ごとに未 ACK の先頭 1 本だけを同じ seq・同じ中身で送る (長い syscall・V86・重い前景からの復帰を待ち、再起動中のゲストに電文を溜めない)。
- 受理する ACK はセッション (sess・epoch) が一致し、`ack < next_seq` の物だけ。受理した ACK だけが沈黙を解く。新しいセッションは `reset` で旧い未 ACK を捨てる。
- 沈黙を作る理由: QEMU の sungem は受信を有効にする前に溜まった電文で受信が止まる。ホストが送り続けない事で防ぐ (製品の側では回避しない)。

## 確かめ

- `tools/tests/test_host_notify.py` (窓・再送・沈黙と解除)・`test_hostsvc_services.py`・`test_remote_g2.py`・`test_remote_x3.py` (操作口の予約番号での送り)。
