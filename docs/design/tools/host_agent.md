# tools/host_agent.py — Host Services のホスト側常駐

> 状態: **現行 (2026-11-18)** — 出所: NET_HOSTSVC_DESIGN・TASK_NET_HOSTSVC (要約して移した)
ソース: `tools/host_agent.py`・`tools/host_linux.py`・`tools/host_services.py`・`tools/host_notify.py`・`tools/lan_bridge.py`

EtherType 0x88B5 のリンクを介してクリップボードや印刷などを扱う。

## ネットワーク越しの Host Services (Linux・82557 の実機)

- 要求の名前の読み替え・LIST・404/501 は [tools/host_services](host_services.md)、知らせと ALIVE とホストの世代は [tools/host_notify](host_notify.md)。HELLO の応答に世代と ALIVE の番号を付ける (旧い長さの OS32 も受ける)。
- 時刻 (`TIME`) と HTTP 取得 (`GET`・`os32.http`) は従来の処理 (`_service_now`・`_service_get`)。HTTP・TLS・DNS はホストだけが行う。`--file-root` の他に許可リストを足さない。`TIME` はホストのローカル時刻の秒の文字列で、UTC のずれ・世代は運ばない (それを運ぶ `os32.time` は未実装)。
- クリップボード `--clip` (既定 `auto`): `win32clipboard` があれば win32、`clip.exe`・`powershell.exe` があれば wsl、Linux では `auto` のまま `tools/host_linux.py` の `clip_start` が Wayland (`WAYLAND_DISPLAY` があれば `wl-copy`・`wl-paste`) か X11 (`DISPLAY` があれば `xclip`) を選ぶ。コマンドも表示の server も無ければ 503 (表示の無いノートでは 503 が正しい結果)。UTF-8、PUT は 4KB まで、GET は 64KB で UTF-8 の境目で切る。
- 印刷: `--printer` を付けると Linux では CLOSE で `lp` の stdin に非同期で流す (`print_start`、shell を通さない argv)。raw は `lp -o raw` で、Linux の `--printer` の時だけ (それ以外は 501)。ジョブ番号と STATUS の queued/done/error はエージェントが持つ。CUPS が無ければ 503 (`ServiceUnavailable`)。
- 長い処理 (HTTP・印刷) は、OS32 の STATUS に制御の PROCESSING で答え続ける。OS32 の要求の期限 (相手が 1000ms 進まなければ `HOSTDOWN`) はこれで延びる (`docs/design/net/link_host_api.md`)。答えなければ延びない。
- `lan_bridge.py` は SFS (`05 53 46` で始まる payload) を link へ通さない。SFS は `lanfs_host.py` ([tools/lanfs_host](lanfs_host.md)) が受ける。
- 確かめ: `tools/tests/test_hostsvc_linux.py` (Linux の CLIP・PRINT、依存の無い時の 503)・`test_hostsvc_services.py`。

## 落とし穴

- **常駐不在と待ち時間**: エージェントがいないと Host Services の open は `HOST_ELINK` になる。
  `tools/host_agent.py` の `--listen` は既定で 127.0.0.1:8026。接続先とエミュレータの `/api/net` の実状態を照合する。
  最初の open は最大 300 tick 待つ (`userland/lib/host/libos32host.h`) ため、途中の画面を完了結果と取り違えない。
  `tools/tests/host_lib_host.c` の `case_open_stale_up()` が待ちとタイムアウトを検査する。
