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
- 確かめ: `test_hostsvc_linux.py (旧版 7588119d5f26)` (Linux の CLIP・PRINT、依存の無い時の 503)・`test_hostsvc_services.py`。

## リンクのセッションと rid 台帳

フレームの形と OS32 側の状態機械は `docs/design/net/link_wire.md`。Agent 側の決まり (`HostAgent`):

- **セッションの採番** (`SessCounter`): 新セッションごとに +1 (0 は使わない)。`--state-dir` があれば `sess.txt` に原子的に書いて
  Agent の再起動をまたいでも再使用しない。`SESS_MAX` (65535) に達したら新セッションの SYN に応えず、枯渇をログに出して止まる
  (運用者が全 OS32 を止めて `sess.txt` を消して再開する)。
- **SYN** (`_on_syn`): 現行セッションは変えず、候補を 1 件だけ持つ (新しい SYN で差し替える)。`req_sess` が確立済みの現行 sess なら
  同じ sess (セッション内の再同期)、未確認の候補と同じ MAC・同じ `req_sess` の SYN なら候補の sess を使い回す (SYN の再送で sess を減らさない)、
  それ以外は新しく採番する。確認済みの候補は使い回さない (OS32 の再起動に旧い sess を渡さない)。
- **CONFIRM** (`_on_confirm`): MAC・OS32 の nonce・Agent の nonce・sess・epoch が候補と全部一致した時だけ候補へ切り替えて ESTABLISHED を返す。
  一致しない CONFIRM は無視、重複には ESTABLISHED を返し直す。`agent` は起動ごとの乱数の世代 (u16)。
- **切り替え** (`_switch_to`): 実行中の子プロセスは捨てる。sess が変われば台帳を空にする。同じ sess の epoch 更新なら
  ACTIVE を捨て、HOLE を RELEASED にし、RELEASED は保つ。
- **rid 台帳**: rid ごとに ACTIVE (受付済み〜結果保持) / RELEASED (墓標) / HOLE (見たがその rid の REQUEST・RELEASE をまだ受けていない)。
  `high_water` はどのフレームで見た rid でも上げ、間の rid を HOLE にする (一度に作るのは `HOLE_FILL_MAX` = 64 まで)。
  1. `rid > high_water` の REQUEST = 新規。ACTIVE が `ACTIVE_MAX` (2) 件あれば制御 NO_SLOT を返し、その rid は HOLE で残す。
  2. ACTIVE の rid の REQUEST・WDATA・STATUS = 重複。再実行せず ACK・結果を返し直す。HOLE の rid の REQUEST = 新規として受ける。
  3. RELEASED、または台帳に無く `rid ≤ high_water` の REQUEST・STATUS = 制御 TOMBSTONE (再実行しない)。HOLE への STATUS も TOMBSTONE。
  4. RELEASE は台帳に無い rid・HOLE にも RELEASED を作り (REQUEST より先着した場合も)、flags bit0 の ACK を返す。実行中の子は捨てる。
  5. RELEASED は `rid ≤ high_water − TOMB_WINDOW` (8) で落としてよい。ACTIVE と HOLE は落とさない。
- **STATUS** (`_on_status`): ACTIVE で結果があれば業務 RESPONSE を再提示、処理中なら制御 PROCESSING、それ以外は TOMBSTONE。
- **WDATA**: ACTIVE の rid の WDATA は常に累積 ACK する。次の seq の時だけ本文に足す (宣言長まで)。宣言長に達したら要求を処理する。
  宣言長の上限は `DECL_MAX_BYTES` (65536)。
- **DATA の配送**: WINDOW を受けた rid だけ流し、credit (ページ数) を超えない。ACK が進まない WINDOW が続けば未 ACK の所から出し直す (Go-Back-N)。

## 落とし穴

- **常駐不在**: エージェントがいないと Host Services の open は `HOSTDOWN` (SDK の用途別の関数では `HOST_ELINK`) になる。
  `tools/host_agent.py` の `--listen` は既定で 127.0.0.1:8026。接続先とエミュレータの `/api/net` の実状態を照合する。
