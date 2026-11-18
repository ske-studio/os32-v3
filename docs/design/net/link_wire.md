# net/link.c のワイヤ v2 と状態機械 — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_N0 (第 5 版)・TASK_N1 の契約を今のソースと照らして要約
ソース: `net/link.h`・`net/link.c`・`net/link_rx.inc`・`net/link_host_api.inc`

Host Services の link (EtherType 0x88B5) のフレームの形、HELLO、再同期、ハンドルの状態機械 (OS32 側)。
KAPI の口 (`host_open`〜`host_close` の戻り値・期限) は `docs/design/net/link_host_api.md`、
ホスト側の rid 台帳とセッション採番は `docs/design/tools/host_agent.md`。
試験は `tools/tests/net_link_host.c` (実 `net/link.c` + 実 `kapi/kapi_host.c`、対向は実 Agent か台本) と `tools/tests/test_host_agent.py`。

## 1. ヘッダ (20B、LE で明示的に直列化)

| 位置 | 欄 | 型 |
|---|---|---|
| 0 | `op` | u8 |
| 1 | `flags` | u8 |
| 2 | `epoch` | u16 |
| 4 | `seq` | u32 |
| 8 | `ack` | u32 |
| 12 | `length` (payload 長) | u16 |
| 14 | `rid` | u32 |
| 18 | `sess` | u16 |

C 構造体の配置に頼らない (`LINK_OFF_*`)。payload は最大 `LINK_MAX_PAYLOAD` (1400)。
**op ごとの payload 長が表と合わないフレームは捨てる** (`link_paylen_ok`、Agent 側も同じ検査)。

| op | 向き | payload | 意味 |
|---|---|---|---|
| HELLO (1) | 双方 | 下の HELLO の表 | 3 way handshake / 再同期 |
| REQUEST (2) | OS32→Host | 1〜1400 | 要求行。`seq` = 0 |
| RESPONSE (3) | Host→OS32 | 6 (`status u16` + `length u32`) | flags bit0 (`LINK_F_CTRL`) = 制御結果、0 = 業務結果 |
| DATA (4) | Host→OS32 | 1〜1400 | 本文。`seq` = 1〜。WINDOW を受けた rid だけ |
| EOF (5) | Host→OS32 | 0 | 補助。完了は受けたバイト数 = `length` で決める |
| ACK (6) | 双方 | 0 | `ack` = 順序どおり受けた最終 seq (累積)。flags bit0 (`LINK_F_RELACK`) = RELEASE への ACK |
| WINDOW (7) | OS32→Host | 2 (credit ページ数) | 配送の開始許可を兼ねる |
| WDATA (8) | OS32→Host | 1〜1400 | 要求本文。`seq` = 1〜 (REQUEST と同じ rid 内の seq 空間の続き) |
| STATUS (9) | OS32→Host | 0 | 結果の再提示の要求 / 生存確認 |
| RELEASE (10) | OS32→Host | 0 | ハンドルを閉じた通知 |
| NOTIFY (11) | Host→OS32 | 12〜248 | 知らせ (`rid` = 0、`docs/design/net/link_notify.md`) |

制御 RESPONSE の status はリンクの符号で、業務の値 (HTTP の 503・410 など、本文も付く) と混ざらない:
`LINK_CTL_PROCESSING` (1) = 処理中、`LINK_CTL_TOMBSTONE` (2) = 結果を捨てた、`LINK_CTL_NO_SLOT` (3) = 受付枠が無い。

## 2. 識別子

- `sess` — セッション。**Agent が採番**する (0 は使わない)。
- `epoch` — セッション内の再同期の世代。OS32 が +1 を提案する。65535 の次は +1 せず新セッション (SYN の `sess` = 0)。
- `rid` — 要求 ID。セッション内で単調増加し、epoch をまたいでも戻さない (0 は使わない)。新セッションで 1 から。
  `0xFFFFFFFF` に達したら新セッション。
- HELLO 以外のフレームは、セッションが UP で `sess`・`epoch` が控えと一致し、送り手が相手の MAC のものだけ受ける。

## 3. HELLO (3 way)

| 段階 (flags) | 向き | payload | 内容 |
|---|---|---|---|
| SYN (0) | OS32→Host | 0 | `seq` = OS32 の nonce (**HELLO ごと・再送ごとに +1**)。`sess` = 0 (新) か現行 (再同期)、`epoch` = 提案 |
| SYN-ACK (1) | Host→OS32 | 6 か 12 | `seq` = nonce の写し、`ack` = Agent の nonce。payload = `agent u16` + `req_sess u16` + `req_epoch u16` (受けた SYN の写し) [+ 拡張: ホストの世代 u32 + ALIVE の番号 u16] |
| CONFIRM (2) | OS32→Host | 0 | `seq` = 自分の nonce、`ack` = Agent の nonce の写し、`sess`・`epoch` = SYN-ACK の写し |
| ESTABLISHED (3) | Host→OS32 | 2 か 8 | `ack` = OS32 の nonce。payload = `agent u16` [+ 拡張] |

- OS32 は**最後に送った SYN の nonce・`req_sess`・`req_epoch` の 3 つが一致する SYN-ACK だけ**を採る。
  採った時点で `sess`・`epoch` を控え (sess が変われば rid を 1 から)、CONFIRM を出す。
  ESTABLISHED は `sess`・`epoch`・nonce・MAC が一致したものだけを採って UP にする。
- SYN-ACK が来なければ nonce を進めて SYN を出し直す (`LINK_RTO_TICKS` ごと、`LINK_TRIES` を超えたら `LINK_HELLO_IDLE_MS` = 1 秒ごと)。
  ESTABLISHED が来なければ CONFIRM を再送し、`LINK_TRIES` を超えたら SYN からやり直す。
- 遅延した旧い HELLO は Agent に候補を作らせるだけで、現行セッションを壊せない (切り替えには Agent の新しい nonce の写しが要る)。
- 拡張の長さ (12 / 8) の時だけ追加の欄を読む。旧い長さ (6 / 2) の相手からは追加の欄を読まない。

## 4. 駆動と送信

- プロトコルを進めるのは 100Hz の `link_tick` だけ (受信の dispatch は 1 tick に `LINK_RX_BUDGET` = 16 フレームまで、再送、制御の送出、HELLO・再同期)。
  KAPI からは呼ばない。反射モードでは `link_init` も `link_tick` も動かさない。
- KAPI の側はハンドル表・リング・送信の印を割込み禁止の短い区間で読み書きするだけで、区間の中で待たない。
  `host_read` の成功確定点は最初の区間 (リングから中継バッファへ写して `read_bytes` を進めた時点) で、
  その後ユーザー領域へ写す間に再同期・close が起きても戻り値は変えない (次の呼び出しが `STALE` / `INVAL`)。
- 送信は制御 (HELLO・WINDOW・ACK・STATUS・RELEASE) と通常 (REQUEST・WDATA) を**交互**に 1 本ずつ NIC へ渡す (`link_tx_turn`、位置は tick をまたいで保つ)。
  HELLO は制御の中で最優先。NIC が受けなければ印を残して次の tick に持ち越す。**RTO は NIC が送信を受理した tick から数える**。
- RELEASE はハンドルごとの専用スロット (`rel_pending`) に積むので、キューの容量に依らず必ず出せる。bit0 の ACK が来るまで再送する。
  通常の転送 ACK (flags 0) では止めない。

## 5. ハンドルの状態機械 (OS32 側)

状態は FREE / SENT / RESP / DONE / STALE (`LINK_H_*`)。

| 事象 | 動作 |
|---|---|
| `host_open` | FREE → SENT。REQUEST (seq 0) を出す |
| 転送 ACK (flags 0) | REQUEST の再送を止める (SENT のまま)。WDATA なら次の seq へ |
| 業務 RESPONSE | SENT → RESP。`length` = 0 なら DONE。重複は無視 |
| 制御 PROCESSING | 生存の印として受けるだけ |
| 制御 NO_SLOT | 未 ACK の RELEASE を**全部**立て直し、次の送信を制御側にしてから REQUEST を再送する (未達の RELEASE は閉じた別のハンドルの物でありうる) |
| 制御 TOMBSTONE | ハンドルを STALE に (呼び手は close する) |
| DATA (リングの持ち主の rid で、次の seq) | リングへ積み、累積 ACK。飛んだ seq は捨てて同じ ACK を出し直す (Go-Back-N)。持ち主でない・未知の rid は捨てる |
| `host_close` / owner の終了 | 任意 → FREE (`gen` +1)。SENT / RESP / DONE からなら RELEASE を専用スロットへ。**STALE からは RELEASE を送らない** (旧 epoch の物は Agent が切り替えで捨てている) |
| 専用スロットの RELEASE が未 ACK | そのスロットの `host_open` は `AGAIN` |

- 生存確認: REQUEST が ACK 済みで RESPONSE が来ない、または本文が途中で止まったまま `LINK_PROBE_TICKS` (1 秒) フレームが来なければ STATUS を出す。
- WINDOW はリングを持つハンドルが本文を待つ間に出す。credit は NIC の受信の余裕と本文リングの空きの小さい方のページ数から余裕 (`LINK_CREDIT_MARGIN`) を引いたもの。

## 6. 再同期 (`link_resync`)

契機: REQUEST・WDATA の再送が `LINK_TRIES` (5) を超えた、RELEASE の再送が上限を超えた、STATUS が `LINK_PROBE_MAX` (5) 回続けて応えられなかった。

- 生きているハンドル (SENT / RESP / DONE) を全部 STALE にし、保留の送信・制御・専用スロットの RELEASE・本文リングを捨てる。FREE は変えない。rid は戻さない。
- 同じ `sess` で `epoch + 1` の SYN を出す。`sess` が無い・epoch が 65535・rid が尽きた場合は新セッション (`sess` = 0、`epoch` = 1)。
- 要求ごとの期限 (`HOSTDOWN`) はこれとは別で、セッションを張り直さない (`docs/design/net/link_host_api.md`)。
