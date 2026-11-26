# tools/host_services.py — サービスの名前と一覧 (LIST)、os32. の予約

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `tools/host_services.py` (`Registry`)・`tools/host_agent.py` (`_serve`)・`sys/include/remote_control.h`・SDK は `userland/lib/host/host_call.c`

## 要求の行と名前

- 要求の行は `<service> <verb> [args]`。名前は小文字の英数字と点。
- **`os32.` で始まる名前だけが OS 標準 (ske-studio が定めた) の予約。** 他の名前は自由で、作者の接頭辞は強制しない。拒否の仕組みは作らない: プラグインが `os32.` を名乗っても警告 1 行、同じ名前の重複は後勝ちで警告し、LIST は有効な 1 件を返す。
- `os32.clip`・`os32.print` は旧い `CLIP`・`PRINT` に、`os32.http GET <url>` は旧い `GET <url>` に読み替える (`host_agent.py` の `_serve`)。旧い行 (`GET`・`TIME`・`CLIP`・`PRINT`) もそのまま受ける。
- それ以外の点を含む名前と `LIST` は `Registry.dispatch` へ: 未知の名前は 404、処理を持たない名前は 501。

## LIST

```text
generation <u32>
<name> <version> <id:u16> <origin:builtin|plugin|sfs|stub>
...
```

- 本文は UTF-8 の行の列。番号はホストの世代の中で登録順に 1 から割り当てる。上限は `REMOTE_SVC_REGISTRY_MAX` (65531)、65532〜65534 は操作口の予約番号 (`REMOTE_SVC_*`)、65535 は「全部」。番号の固定表を持たない。
- SDK は世代ごとに LIST を cache し、`STALE` か新しい ALIVE の世代で捨てて引き直す ([userland/lib/host/host_call](../userland/lib/host/host_call.md))。
- origin は提供元の分類で、名前の接頭辞から推定しない。

## 今の一覧

| origin | 名前 | 中身 |
|---|---|---|
| builtin | `os32.alive`・`os32.clip`・`os32.print`・`os32.http` | ALIVE の知らせ・クリップボード・印刷・HTTP 取得 (`docs/design/tools/host_agent.md`) |
| sfs | `os32.file`・`os32.iso9660` | `lanfs_host.py` のファイルと仮想 CD。掲載は接続の成功を保証しない。要求の本体は VFS/SFS の経路で、link の要求を lanfs へ転送しない (link で呼ぶと 501) |
| stub | `os32.udf`・`os32.mail`・`os32.feed`・`os32.chat` | 名前だけの予約。ホストは 501 |

- 予約だけで一覧に載せない名前: `os32.deploy` (開発の運用、提供した機能だけ後で載せる)。
- 別の設計で、まだ一覧に無い名前: `os32.time` ([OS32_TIME_DESIGN](../../decisions/time/OS32_TIME_DESIGN.md)、未実装。今の時刻は旧い `TIME` の行)・`os32.conv.image` ([CONV_IMAGE_DESIGN](../../decisions/network/CONV_IMAGE_DESIGN.md)、未実装)・`os32.audio`・`os32.midi`・`os32.screen` (未設計)。予定の順は time → conv.image → audio/midi → screen。
- 作らない名前: `os32.conv.text`・`os32.ime`・`os32.build`・`os32.exec`・`os32.ai`。

## 確かめ

- `test_hostsvc_services.py (旧版 0a23c60eb444)` (番号の割り当て・後勝ち・警告・sfs の静的な行・stub の 501・作らない名前が無い)・`test_hostsvc_sdk.py` (SDK の名前の解決と引き直し)。
