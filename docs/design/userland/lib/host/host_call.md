# userland/lib/host/host_call.c — SDK の Host Services の呼び出し (host_call・LIST の名前の解決・通知の取り出し)

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `userland/lib/host/host_call.c`・`userland/lib/host/host_call_internal.h`・`userland/lib/host/libos32host.c`・`userland/lib/host/libos32host.h`・`sdk/rust/os32api/src/host.rs`・`sdk/include/os32/os32_host_notice.h`

アプリから NIC・link・SFS は見えない。KAPI の汎用の口 (`host_open`〜`host_close`、`docs/design/net/link_host_api.md`) の上に、待ちと名前の解決を足す。

## 口

```c
/* req は NUL 終端。成功は本文のバイト数 (サービスの失敗の本文も含む)、失敗は負値 */
int host_call(const char *req, const void *in, u32 in_len, void *out, u32 cap, u32 timeout);
int os32_host_notice_read(const char *name, HostNotice *out); /* LIST で名前を番号に引く */
```

- `host_call` は open → 本文の書き込み → status と読み出し → close を、`AGAIN` の間 `sys_yield` しながら進める。どの終わり方でも close する。
- `timeout` は呼び出し全体の期限 (ms、100Hz の tick へ切り上げ)。超えたら `TIMEOUT` (-19)。0 なら待たず、進めなければ `AGAIN`。
- 相手の沈黙 (1000ms) とリンクが UP でない事は KAPI が `HOSTDOWN` (-18) で返す。SDK は 2 つ目の沈黙のタイマーを持たない (PROCESSING を見られるのは KAPI だけなので)。古いハンドルは `STALE`。
- 出力が足りなければ `NOSPC`。切り詰めて成功にしない。
- `host_call` はサービスの status を返さない。status が要る用途は KAPI を直接使う (`host_status`)。
- 用途別の関数 (`host_time`・`host_get`・`host_print_*`・`host_clip_*`、別名 `os32_host_*`) は `host_exchange` の上にあり、1 要求の期限は `HOST_CALL_TIMEOUT_MS` (30 秒)。戻りは `libos32host.h` の `HOST_E*` (リンクの失敗は `HOST_ELINK`、サービスの失敗は `HOST_ESERVICE` と `*svc_status`)。
- Rust は `os32api` の `host.rs` (`host_call`・`notice_read`・`os32_host_*`) が同じ契約を包む。

## 名前の解決と通知

- LIST の本文を世代ごとにまるごと cache する (件数の上限を持たない)。番号の固定表を持たない。
- `os32_host_notice_read` は最初に `os32.alive` を読み、ALIVE が来ていれば LIST を引き直す (ホストの再起動で番号が替わるため)。ALIVE を読んだ時は 1 件取っておき、アプリが `os32.alive` を求めればそれを返す。`STALE` なら cache を捨てて 1 回だけやり直す。
- 取り出しは共有の競合消費 (`docs/design/net/host_mailbox.md`)。他のアプリが先に取って `AGAIN` でも正常。KAPI の版が 82 未満なら `NOSYS`。

## 確かめ

- `tools/tests/test_hostsvc_sdk.py` (yield・全体の期限の TIMEOUT・HOSTDOWN・古いハンドルの STALE・LIST の引き直し)・`test_host_lib.py`・`test_host_sdk_rs.py` (Rust の契約)。
