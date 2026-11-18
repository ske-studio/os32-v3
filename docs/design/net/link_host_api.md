# net/link_host_api.inc — Host Services の非同期の口 (KAPI v51 の実体) と、要求ごとの期限

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `net/link_host_api.inc` (`net/link.c` が取り込む)・`net/link.h`・`net/link_rx.inc`・`kapi/kapi_host.c`・`sdk/include/os32/os32_kapi_shared.h`

KAPI の `host_open`・`host_status`・`host_read`・`host_write`・`host_close` の実体。どれも待たない。
プロトコルを進めるのは `link_tick()` だけで、ここは状態を読み書きするだけ。CPL3 のポインタの検査と写しは `kapi/kapi_host.c` が行う。
経路 (NIC・link・SFS) はアプリから隠す。要求の行は任意の `<service> <verb> [args]` で、KAPI は汎用のまま。

## 容量

- 同時のハンドルは `LINK_HANDLES` = 8。9 本目は `FULL` (閉じた直後で RELEASE がまだ ACK されていない枠しか無ければ `AGAIN`)。
- 本文のリングは 8KiB の 1 本 (`link_stream_buf`、静的)。本文を受けるハンドルは同時に 1 本で、最初に `host_read` したハンドルが持つ (`link_ring_owner`)。他のハンドルの `host_read` は `AGAIN`。要求・応答 (status) と書き込み (WDATA) は 8 本が並んで進む。
- 要求の行は 1〜1400B (`LINK_MAX_PAYLOAD`)、宣言長は 65536 まで (`LINK_DECL_MAX`)。

## 誤りの意味 (ホストの生存はカーネルが覚えない)

| 場合 | 返す値 |
|---|---|
| link が UP でない時の open、NIC 未 bind | `HOSTDOWN` (-18)。HELLO は背景で 1 秒ごとに出し直す (`LINK_HELLO_IDLE_MS`) |
| 開いた要求の相手が 1000ms 進まない (`LINK_HOST_SILENCE_TICKS`=100) | そのハンドルだけ `HOSTDOWN`。他の 7 本とセッションは保つ |
| ハンドルが古いセッションの物 (再同期の後) | `STALE` (-11) |
| 応答がまだ・本文がまだ | `AGAIN` (-14) |
| 要求の行・引数の誤り | `INVAL` |

- 「進んだ」と数えるのは、検査済みの RESPONSE (制御の PROCESSING を含む)、新しい DATA の受理、REQUEST・WDATA の ACK (`link_host_progress`)。重複のフレームと、OS32 が出す STATUS では延ばさない。長い HTTP・印刷・変換は、ホストが STATUS へ PROCESSING を返し続ける限り期限が切れない。
- 本文を全部受けた後は、ローカルの読み手だけが残るので期限を見ない。
- 期限切れのハンドルは STALE の状態にして送受信とタイマーから外し、要求・本文の再送の代わりに RELEASE を出す。RELEASE の再送は元の予算 (RTO・PROBE の残り) を引き継ぎ、前倒ししない。
- セッション全体の張り直しは従来の条件だけ: 再送 `LINK_RTO_TICKS` (200ms) × `LINK_TRIES` (5)、または PROBE `LINK_PROBE_TICKS` (1 秒) × `LINK_PROBE_MAX` (5)。要求の期限を理由に早めない。
- `close` は通信を待たずにローカルで解放できる。owner の終了は `link_host_owner_exit` が回収する。

## 境目

- SDK の全体の期限 (`TIMEOUT`、-19) は KAPI ではなく SDK の `host_call` が持つ ([userland/lib/host/host_call](../userland/lib/host/host_call.md))。相手の沈黙を見るのは KAPI の側だけ (PROCESSING を見られるのはここだけなので)。
- 知らせ (NOTIFY) はハンドルと本文のリングを使わない ([net/link_notify](link_notify.md))。
- ホスト側のサービス名と LIST は [tools/host_services](../tools/host_services.md)、要求の処理は [tools/host_agent](../tools/host_agent.md)。

## 確かめ

- `tools/tests/test_hostsvc_link_api.py` (本物の API と期限の hook: UP でない open・期限切れの各口が `HOSTDOWN`、ハンドルごとの期限)・`test_hostsvc_link_core.py`・`test_host_lib.py` (`host_lib_host.c`)。
