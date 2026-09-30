# N0 — Host Services の KAPI v51 (ABI 確定) と非ブロッキング化の設計

> 発行: PM (2026-09-14) / 状態: **受入完了 (2026-09-14)**

完了 — 第 5 版で N1 へ (ユーザー決裁 2026-09-14 「b」: 往復 3 + 追加 1 往復を使い切り、残る疑いは N1 の実装レビューとホスト TDD (§3 に往復 2〜4 の反例を全部載せた) で拾う)**。前提: [HOST_SERVICES_PLAN.md](../../tasks/network/HOST_SERVICES_PLAN.md) (§2 サービス一覧、§3 KAPI 案、§7 票、§9 の決裁は**推奨案で進める**: 印刷 v1 は to-file 既定、Agent は WSL2、LGY-98 は N3 受入後に既定へ、KAPI は v51、CLIP は含め PUT は後回し、HTML は text)、[LINK_PLAN.md](../../tasks/network/LINK_PLAN.md) (L0〜L3 の契約: EtherType 0x88B5、16B ヘッダ、Stop-and-Wait の REQUEST/RESPONSE、L1 絶対値 WINDOW、L2 8KB ストリーム Go-Back-N)、`net/link.{c,h}` (現状は同期版 `link_request` / `link_service_get`、`link_stream_read`)、`docs/KAPI_SPEC.md` §3-1 (追加手順) / §3-2 (予約表)、T9 §1a (ABI 表の書式)、S0-K (v50: CPL=3 ポインタの範囲検証 `ring3_user_range_ok`、owner 回収の位置)。
規約: [ABI1〜3] (kapi.json が正典、末尾追記、版を上げて `make clean`)、[C1] C89、[C2] kstr*、[C4]。

## 0. 範囲

N0 は**設計だけ**: (a) KAPI_SPEC §3-2 (済)、(b) **リンク層ワイヤ v2** (§1b) = LINK_PLAN §4 の改訂 (旧表は要約 + 参照に置換済み、`76ea249`)、(c) v51 の ABI (§1a)、(d) `net/link.c` の駆動・排他・状態機械 (§2)、(e) Agent 側の v2 対応は N1 に含める (§2d)、(f) N1 の TDD と受入 (§3)、(g) HOST_SERVICES_PLAN §2 / §3 の同期 (要求文法 `PRINT DATA <job> <len>`、TIME の標準形、close / STALE の契約、N1 / N2 の分担 — 済 `76ea249`。N0 の改訂のたびに追従する)。

## 1. KAPI_SPEC §3-2 の改訂 (済)

v43 = 欠番、v51 = 5 本 (slot 208〜212、data_fields 0x35C / 0x360)。全体 clean rebuild (SDK・shlib・apps / game・配備先の旧バイナリを作り直す。旧 CPL=3 バイナリは 0x348 / 0x34C を旧データとして読むので互換は無い)。

## 1b. リンク層ワイヤ v2 (LINK_PLAN §4 の改訂)

ヘッダ **20B、明示的に直列化** (LE アクセサで書く。C 構造体の padding に依存しない): `op u8` @0, `flags u8` @1, `epoch u16` @2, `seq u32` @4, `ack u32` @8, `length u16` @12, `rid u32` @14, `sess u16` @18。EtherType 0x88B5。各 op の payload 長は下表の値と一致しないフレームを捨てる (v2 パーサの検査項目)。

**識別子**: `sess` = セッション ID。**Agent が採番** (永続カウンタ `sess.txt`、新セッションごとに +1、0 は使わない。Agent 再起動をまたいでも**再使用しない**: 65535 に達したら Agent は新セッションの SYN に応答せずログに枯渇を出して止まる = 運用者が全 OS32 を止めた上で `sess.txt` を消して再開する。往復 4 の R3 — 周回させない。前提として書いておく: リンクは P2P の Ethernet で中継装置は無く、フレームの寿命は NIC / エミュレータのキューに留まる間 (数秒) だけ。それでも識別子は寿命に頼らず再使用しない)。`epoch` = セッション内の再同期世代 (OS32 が +1)。`rid` = 要求 ID、**セッション内で単調増加** (epoch をまたいでも戻さない、0 は使わない。往復 3 の B6。0xFFFFFFFF に達したら新セッション — セッション内で再使用しない)。HELLO 以外のフレームは `sess` と `epoch` が控えと一致するものだけ受け付ける (両端とも)。

**HELLO は 3 way** (往復 3 の B3 / B4 / B5 / B7)。`flags` で段階を区別する:

| 段階 | 向き | 内容 |
|---|---|---|
| SYN (flags 0) | OS32→Host | `seq` = OS32 の nonce (HELLO ごとに +1 する 32bit、初期値は `tick_count` と `sys_time` を混ぜたもの — 一意性は要らない、応答の対応付けだけ)、`sess` = 0 (新セッション) または現在の sess (セッション内の再同期)、`epoch` = 提案する世代。payload 無し |
| SYN-ACK (flags 1) | Host→OS32 | `seq` = 受けた nonce の写し、`ack` = **Agent の nonce** (乱数 32bit)、`sess` = 割り当て (SYN の sess が 0 か未知なら新規採番、既知ならその値)、`epoch` = 提案の写し、payload 6B = `agent u16` (Agent 起動時の乱数世代) + **`req_sess u16` + `req_epoch u16` (受けた SYN の sess / epoch の写し)**。**この時点では Agent の現行セッションは何も変えない** (候補として 1 件だけ保持、新しい SYN で上書き) |
| CONFIRM (flags 2) | OS32→Host | `seq` = 自分の nonce、`ack` = Agent の nonce の写し、`sess` / `epoch` = SYN-ACK の写し。OS32 は**自分が最後に送った SYN と nonce・`req_sess`・`req_epoch` の 3 つが一致する SYN-ACK だけ採用** (往復 4 の R2: 再起動後の SYN は `req_sess` = 0 なので、旧セッションの再同期 SYN (sess = S) への遅延応答は nonce が同じでも弾かれる。旧起動の新セッション SYN への応答が届いた場合は、それも「未使用の sess を新規に割り当てた応答」なので採用して害が無い)。CONFIRM を送った時点で新 (sess, epoch) に切り替える |
| ESTABLISHED (flags 3) | Host→OS32 | CONFIRM が候補 (Agent nonce) と一致したときだけ、Agent は候補へ切り替えて返す。一致しない CONFIRM (遅延した旧セッションのもの) は無視。重複 CONFIRM には冪等に再送 |

- OS32 は ESTABLISHED が来るまで CONFIRM を `LINK_RTO` ごとに再送 (`LINK_TRIES` 超で SYN からやり直し)。**遅延した旧 HELLO は SYN-ACK の候補を作るだけで現行セッションを壊せない** (CONFIRM に Agent の新鮮な nonce が要る = B4)。
- 切替時に Agent が捨てるもの: 旧 (sess, epoch) の受付済み・配送中・保留 TX と、**セッションが変わるときは墓標も**。同じ sess の epoch 更新では墓標 (§ 下) は保つ (rid はセッション内で単調なので有効)。OS32 側は生存ハンドルを STALE、リング / TX キュー / 保留制御を破棄 (B6 反例 1)。
- epoch は 16bit。**65535 の次は +1 せず新セッション (SYN sess=0)** で始める (B5 — 大小比較で HELLO を拒否する規則は無く、採用は nonce と `req_sess` / `req_epoch` の対応だけで決まる)。rid が 0xFFFFFFFF に達したときも同じ。

| op | 向き | payload | 意味 |
|---|---|---|---|
| HELLO (1) | 双方 | 0 / 2B | 上表 |
| REQUEST (2) | OS32→Host | 1〜1400B | `rid`、`seq` = **0** (rid 内の単一 seq 空間の先頭)。本文が要る要求は要求行に宣言長 |
| WDATA (8) | OS32→Host | 1〜1400B | `rid`、`seq` = **1〜** (REQUEST と同じ空間の続き)。Agent は (sess, rid, seq) で重複排除 |
| ACK (6) | 双方 | 0 | `rid` + `ack` = 順序どおり受けた最終 seq (累積)。**flags bit0 = RELEASE への ACK** (通常の転送 ACK は flags 0。往復 3 の B1: OS32 は RELEASE 待ちのハンドルでは bit0 の ACK だけを完了とみなす)。未送信 seq への ACK / 古い ACK は無視 |
| RESPONSE (3) | Host→OS32 | 6B (`status u16` + `length u32`) | `rid`。**flags bit0 = 制御結果** (`status` はリンク符号: 1 = 処理中 PROCESSING、2 = 墓標 TOMBSTONE、3 = 受付枠無し NO_SLOT。`length` = 0)。flags 0 = 業務結果 (`status` はサービスの値 — HTTP の 503 / 410 もそのまま通り、本文も付く。往復 3 の B7) |
| STATUS (9) | OS32→Host | 0 | `rid`。Agent は結果があれば RESPONSE (業務) を再提示、処理中なら制御 PROCESSING、墓標 / 未知なら制御 TOMBSTONE |
| RELEASE (10) | OS32→Host | 0 | `rid`。ハンドルを閉じた通知。Agent はその rid の受付・配送状態・結果を捨て、**RELEASED の墓標を残し**、ACK (flags bit0、`ack` = 0) を返す。OS32 は bit0 の ACK が来るまで再送 |
| DATA (4) / EOF (5) | Host→OS32 | 1〜1400 / 0 | `rid`、`seq` = ストリーム内 1〜。OS32 が WINDOW (rid) を送ったストリームだけ |
| WINDOW (7) | OS32→Host | 2B (credit_pages) | `rid`、`ack` = 受けた最終 seq。開始許可を兼ねる |

- **Agent の rid 台帳** (往復 2 の R4 / R5、往復 3 の B2、往復 4 の R1): rid ごとに **ACTIVE** (受付済み / 配送中 / 結果保持、RELEASE 前)、**RELEASED** (墓標)、**HOLE** (rid は観測したがその rid 自身の REQUEST / RELEASE をまだ受けていない = 未受理の穴) の記録。`high_water` = どのフレームでも見た最大 rid で、`high_water` が h から h' へ上がるとき (h, h') の rid は **HOLE として台帳に置く** (採番順は到着順を保証しないので、穴を墓標にしない)。規則: (1) `rid > high_water` の REQUEST = 新規。ACTIVE が既に 2 件なら制御 NO_SLOT を返し、**その rid は HOLE として残す** (`high_water` は上げる。OS32 は RELEASE 未達とみなして RELEASE と REQUEST を再送し、再送は (2') で受理される — OS32 のハンドルは 2 本なので、正しく RELEASE が届いていれば起きない)。(2) ACTIVE の rid の REQUEST / WDATA / STATUS = 重複として処理 (再実行しない)。(2') **HOLE の rid の REQUEST = 新規として受理** (ACTIVE へ。枠が無ければ (1) と同じ NO_SLOT)。(3) RELEASED の墓標、または **台帳に無く `rid ≤ high_water`** (= (5) で落とした墓標) の rid の REQUEST / STATUS = 制御 TOMBSTONE。再実行しない。HOLE への STATUS も TOMBSTONE (OS32 は REQUEST の ACK 後にしか STATUS を送らないので、通常は起きない)。(4) RELEASE は HOLE / 台帳に無い rid にも RELEASED の墓標を作る (REQUEST より先着した場合、後着の REQUEST は (3) で止まる)。(5) RELEASED の墓標は `rid ≤ high_water − 8` で落としてよい (落とした後も (3) で止まる)。**ACTIVE と HOLE は watermark で落とさない** (長寿命 A + 短命 B の多数: A の結果は RELEASE まで残る。HOLE は OS32 が REQUEST か RELEASE を再送し続けるので必ず埋まる — 埋まらないのは epoch 切替で OS32 がハンドルを STALE にした場合だけで、**epoch 切替時に Agent は HOLE を全部 RELEASED に変える**)。HOLE の数は OS32 の生存ハンドル数 (≤ 2) で抑えられる。
- **再同期の契機** (往復 2 の R6 / R7): (i) REQUEST / WDATA / RELEASE の ACK が `LINK_TRIES` 回来ない、(ii) 生存ハンドル (SENT / RESP) があるのに `T_probe` (1 秒) 以上フレームを受けていない → STATUS 照会、それに `k` (5) 回応答が無い、(iii) SYN-ACK の `agent` が控えと違う (Agent 再起動 — Agent 側でも sess の候補が新規採番になる)、(iv) 起動時。再同期 = 同じ sess で `epoch + 1` の SYN (周回なら sess=0)。

## 1a. KAPI v51 の ABI (N1 の K が実装、N3 の C / N4 の W が従う)

共通: CPL=3 のポインタ / 長さの検査は v50 と同じ 2 段 (往復 3 の B8): **ポインタ引数の先頭番地が帯外ならディスパッチャ (`ring3_syscall_dispatch` の `kapi_argptr` 早期検査) がラッパー到達前に kill** (`fault_kill_count` +1、`INVAL` は返らない)。先頭が帯内で長さを含めた範囲が帯外 → ラッパーの `ring3_user_range_ok` で `INVAL`。帯内の非 present はアクセス時のフォールトガード (`ring3_in_syscall`) で kill (早期検査ではない)。複数の出力ポインタは**全部を先に検証**してから書く。出力ポインタは NULL 可。失敗時は出力を書かない。どの呼び出しも待たない。NIC 無し / 未初期化 → `NOSYS`。h が範囲外 / FREE / 他人 → `INVAL`。**STALE のハンドルへの status / read / write → `STALE`** (close だけが通る)。所有者の照合は既存の資源 owner ID (アプリ ID、`res_owner_get()`) で行い、内部回収 (`host_owner_exit`) は公開 API を通さず指定 ID で解放する。

| slot | 名前 | 引数 | 戻り | 規則 |
|---|---|---|---|---|
| 208 | `host_open` | `const char *req, u32 len` | h (0 / 1) / 負 | 要求行 1〜1400B (超過 / 0 → `INVAL`)、カーネル所有の領域へ写す。HELLO 未確立 / 再同期中 → `STALE`。空き無し → `FULL`。TX キュー満杯 → `AGAIN`。成功で SENT (REQUEST seq 0 をキューへ)。rid は `next_rid++` (セッション内で単調) |
| 209 | `host_status` | `i32 h, u32 *status, u32 *length` | 0 / `AGAIN` / 負 | 業務 RESPONSE 未着 → `AGAIN`。着いていれば `status` / `length`。Agent が墓標を返したハンドル → `STALE` |
| 210 | `host_read` | `i32 h, void *buf, u32 cap` | 長さ (> 0) / 0 = 完了 / `AGAIN` / 負 | `cap == 0` / `buf == NULL` → `INVAL`。RESPONSE 未着 → `AGAIN`。`length == 0` → 0。リングの所有者でなければ取得を試み、他方が所有中 → `AGAIN`。**1 回に写す量 = min(cap, リングの連続可用, 1400)**。最後のバイトを写した呼び出しは正の長さを返し、**その次**の呼び出しが 0 (`read_bytes == length` のとき)。未着 → `AGAIN` |
| 211 | `host_write` | `i32 h, const void *buf, u32 len` | 受け付けた長さ (= min(len, 1400)) / `AGAIN` / 負 | 宣言長の無い要求 / `len == 0` / `buf == NULL` → `INVAL`。**`len > 残り宣言長` → `INVAL`** (残りまでの部分受付はしない — 呼び手が残りを知っている)。**REQUEST (seq 0) の転送 ACK が来るまで `AGAIN`**。未 ACK の WDATA が 1 本ある間 `AGAIN`。TX キュー満杯 → `AGAIN`。RESPONSE 着後 → `INVAL`。宣言長は 64KB 以下 (`wseq` は枯渇しない) |
| 212 | `host_close` | `i32 h` | 0 / 負 | 任意の状態から FREE。リング所有者なら未読分を捨てて放す。SENT / RESP / DONE なら **RELEASE (rid) をハンドル専用の制御スロットへ** (キューの容量に依らず必ず積める。bit0 の ACK まで `link_tick` が再送、保留 TX の同 rid の REQUEST / WDATA は無効化)。**STALE のハンドルの close は RELEASE を送らない** (旧 epoch のものは切替で Agent が捨てている。往復 3 の B6 反例 2)。ハンドルの `gen` を +1。二重 close → `INVAL` |

- **owner 回収**: `exec_reclaim_owned(x)` の中で `host_owner_exit(x)` = owner が x のハンドルを内部解放 (RELEASE も送る)。
- **同期版** `link_service_get()` / `link_request()` / L1 / L2 の自己試験は **非同期 API + `sys_yield` 相当 (IF=1 の hlt 待ち) の上に書き直し**、状態機械を進めるのは `link_tick` だけ (往復 2 の R8)。

## 2. `net/link.c` の非ブロッキング化 (N1 の設計)

### 2a. 駆動と排他
- **プロトコルの進行はタイマ**: 100Hz の `link_tick` (`ne2k_timer_tick` の後) が受信 dispatch (最大 16 フレーム / 周回)、再送、WINDOW / ACK / STATUS / RELEASE の送出、HELLO / 再同期を行う。KAPI からは `link_tick` を呼ばない。**反射モード (`LGY98_FLAG_REFLECT`) では `link_tick` を起動しない** = RX キューの消費者は反射かリンク層のどちらか 1 つ (往復 2 の R10、M2 の反射試験を回帰対象に)。
- **KAPI の更新は割込み禁止区間**: ハンドル表 / リング / TX キューへの KAPI からの読み書きは `cli` / `sti` (IF 保存復元) の短い区間で行い、区間内で待たない。`host_read` の**成功確定点は最初の cli 区間**: そこでリング → 中継バッファ (ハンドルごと 1400B) へ写し、`read_bytes` を進め、リングを消費する (この時点で呼び出しの戻り値 = 写した長さが確定)。その後 IF=1 でユーザー領域へ写して確定済みの長さを返す — **コピー中に再同期 / close が起きても戻り値は変えない** (次の呼び出しが `STALE` / `INVAL` を返す。往復 4 の R4: 「失敗時は出力を書かない」を守るために、失敗の判定を出力の前に終える)。ユーザー領域の #PF はアプリの kill なので途中状態は問題にならない。`host_open` / `host_write` は要求 / WDATA をカーネル領域に写してから cli 区間で **`gen` / `state` を再確認の上**キューへ積む (再確認に落ちれば `STALE` / `INVAL`。出力ポインタは無いので契約に反しない)。IF=0 中に来たタイマは延期されるだけで、IF 復元直後の tick が処理する。
- **TX の公平化** (往復 2 の R9): 送信は制御 (WINDOW / ACK / STATUS: rid ごとに **1 周回 1 本に集約**。RELEASE はハンドル専用スロット 2 つ) と通常 (REQUEST / WDATA: ハンドルごと 1 本) で、`link_tick` は **交互** (制御 1 本 → 通常 1 本、順番の位置は tick をまたいで保持) に NIC へ渡す。NIC が busy で受けなければ次の周回に持ち越す (制御通知も捨てない)。**RTO は NIC が送信を受理した tick から数える**。credit は既存の計算 (NIC リング / RX キュー / 8KB リング / 安全余裕の最小値) を保つ。

### 2b. 配送の直列化
- ハンドル 2 本、リング 1 本。リングの所有権は最初に `host_read` した側 (cli 区間で取得) が **close まで**持ち、`link_tick` がその rid の WINDOW を送る。Agent は WINDOW を受けた rid だけ流す。他方は `AGAIN`。
- Agent 側: ACTIVE は rid ごと最大 2、追放は RELEASE だけ (§1b)。

### 2c. 状態機械
ハンドル: `state` (FREE / SENT / RESP / DONE / STALE)、`gen` (FREE に戻るたび +1)、`owner`、`rid`、`epoch`、`req_copy[1400]` + `req_len`、`req_acked`、`status`、`length`、`recv_bytes`、`read_bytes`、`ring_owner`、`wseq`、`wcopy[1400]` + `wlen` + `wacked`、`decl_len` + `wsent_total`、`last_tx_tick`、`retries`、`last_rx_tick`、`rel_pending` (専用スロット: rid / epoch / 再送計数)。

| 事象 | 遷移 / 動作 |
|---|---|
| `host_open` | FREE → SENT、REQUEST (seq 0) を通常キューへ |
| 転送 ACK (flags 0、rid、ack ≥ 0) | `req_acked = 1`、REQUEST の再送停止 (状態は SENT のまま) |
| 転送 ACK 不着 (`LINK_RTO` × `LINK_TRIES`、NIC 受理 tick から) | REQUEST 再送 (同 rid / seq 0)。上限超 → 再同期 |
| 業務 RESPONSE (flags 0、rid 一致) | SENT → RESP。`length == 0` → DONE (**0 長の成功応答も STATUS の再提示対象**)。重複は無視 |
| 制御 PROCESSING | `last_rx_tick` を更新するだけ |
| 制御 NO_SLOT | RELEASE 未達 = 専用スロットの RELEASE を再送してから REQUEST を再送 (SENT のまま) |
| 制御 TOMBSTONE | Agent が結果を捨てた → ハンドルを STALE (呼び手は close) |
| `req_acked` で RESPONSE 未着、または RESP で `recv_bytes < length` のまま、`last_rx_tick` から `T_probe` | STATUS (rid) を制御キューへ (1 秒ごと)。`k` (5) 回応答が無い → 再同期。制御枠の集約では STATUS を WINDOW / ACK より後回しにしない (rid ごとに種類を巡回) |
| WINDOW 送出 | `ring_owner` かつ `recv_bytes < length` のとき credit = 上の最小値 (制御キュー、rid ごと 1 周回 1 本) |
| DATA (ring_owner の rid、順次) | リングへ、`recv_bytes += n`、ACK (累積) を制御キューへ。gap は捨てて再 ACK (Go-Back-N)。非所有 / 未知 rid は捨てる |
| EOF | 補助。完了は `recv_bytes == length` |
| `host_read` | cli 区間で所有権取得 → 中継 → `read_bytes` 更新 (成功確定) → IF=1 でユーザーへ。`read_bytes == length` で 0 (DONE) |
| `host_write` | `req_acked` かつ `wacked` → WDATA (seq = `wseq`) を写して通常キューへ (`wacked = 0`)。ACK (flags 0、rid、ack ≥ seq) で `wacked = 1`、`wseq++`。RTO で再送 (同 seq)、上限超 → 再同期 |
| Agent が本文を受け切る | 業務 RESPONSE → RESP / DONE |
| 再同期 (CONFIRM 送出 = 新 epoch へ切替) | SENT / RESP / DONE → STALE (`epoch` は旧値のまま)、リング / TX キュー / 制御キュー / **専用スロットの RELEASE** を破棄、`ring_owner` を放す。FREE は不変。rid は戻さない |
| `host_close` / `host_owner_exit` | 任意 → FREE (`gen` +1)。SENT / RESP / DONE からなら RELEASE (rid, 現 epoch) を専用スロットへ (bit0 の ACK まで再送、上限超 → 再同期)、同 rid の保留 TX を無効化。STALE からは送らない。専用スロットが埋まっている (前の住人の RELEASE が未 ACK) 間、そのハンドルは `host_open` に `AGAIN` を返す |

### 2d. Agent 側 (N1 に含める `host_agent.py` の v2 対応)
20B ヘッダ (LE 直列化、op ごとの payload 長検査)、3 way HELLO (候補 1 件、Agent nonce、`req_sess` / `req_epoch` の写し、`sess.txt` の永続採番と枯渇停止、`agent` 世代)、rid 台帳 (ACTIVE ≤ 2 / RELEASED 墓標 / HOLE / `high_water` 規則 (1)〜(5)、epoch 切替で HOLE → RELEASED)、STATUS への再提示 / PROCESSING / TOMBSTONE、制御結果は flags bit0、RELEASE の処理と bit0 ACK、WINDOW 待ちの配送開始、WDATA の (sess, rid, seq) 重複排除と累積 ACK、宣言長で本文完了 → RESPONSE、TIME / PING を標準ヘッダに、N1 の受入用 `ECHO <len>` サービス。PRINT / CLIP / PUT は N2。既存 `check-net-l0`〜`l3` は v2 で回帰 (ゲスト観測)、ホストハーネスと分けて記録。

## 3. N1 のホスト TDD と受入

- `tools/tests/net_link_host.c`: `net/link.c` を `#include`、NIC (RX キュー、TX 受理の可否 = 1 tick 1 フレームの条件も、tick) と `cli` / `sti` を贋物に。**タイマ入口だけで進む**、**割込み挿入** (リング更新 / TX 構築の途中に贋 IRQ5 が積む、IF=0 中のタイマ延期と復元直後の tick)、**実 Agent** (`host_agent.py` v2 をサブプロセス、UNIX ソケットで結ぶ) で:
  - 往復 2: 非ゼロの `agent` 世代での HELLO → REQUEST → RESPONSE → 本文完了 (R1)、REQUEST ACK の遅延重複 + WDATA 喪失の同時注入 (R2)、転送 ACK 後の RESPONSE 消失 → STATUS で再提示 (0 長も) (R3)、A / B の close 順 × 配送状態で RELEASE が正しい rid を捨てる (R4)、長寿命 A + 多数の短命 B で A の結果が残る、8 件境界を越えた遅延 REQUEST が TOMBSTONE で再実行されない (R5)、2 ハンドル ACK 済みで Agent 再起動 → STATUS 無応答 → 再同期 → STALE (R6)、旧 HELLO / 旧応答の遅延 (R7)、自己試験の実行中のタイマ挿入 (R8)、credit 0 の A と B の並行で NIC が 1 tick 1 フレーム (R9)、反射モードで `link_tick` が起動しない (R10)。
  - 往復 3: **RELEASE 消失 + 旧 REQUEST ACK (flags 0) 到着で RELEASE 再送が止まらない** (B1)、**RELEASE 直後の同 rid の遅延 REQUEST が TOMBSTONE** と **RELEASE が REQUEST に先着** (B2)、**同じ RTC 秒・同じ初期 tick での OS32 再起動** を Agent が別セッションとして扱う (sess は Agent 採番) (B3)、**旧 sess の遅延 SYN が現行セッションを壊さない** (SYN-ACK だけで切替が起きない、旧 CONFIRM は nonce 不一致で無視) (B4)、**epoch 65535 の次の再同期が新セッションで成立** (B5)、**同 sess の epoch 更新で Agent の ACTIVE 2 件が消え新要求が NO_SLOT にならない**、**STALE ハンドルの close が新 epoch の rid を解放しない** (B6)、**HTTP 503 / 本文付き HTTP 410 が業務結果として呼び手に届き、制御 NO_SLOT / TOMBSTONE と混ざらない** (B7)、**先頭帯外ポインタはディスパッチャ経由で kill、先頭帯内 + 長さ超過はラッパーで INVAL** (`kapi_host.c` にディスパッチャの早期検査を含める) (B8)。
  - 往復 4: **A の REQUEST 初回欠落 → B 受理 → A 再送が新規として受理される** (HOLE) と NO_SLOT 後の再送 (R1)、**再起動 + nonce 一致 + 旧再同期 SYN の先後逆転** で旧セッションへ戻らない (R2)、**sess 枯渇で Agent が停止**し、旧 (sess, epoch, rid) の RESPONSE / DATA を注入しても新要求に採用されない (R3)、**ユーザーコピー中の再同期で戻り値と buf の内容が一致** (確定済みの長さ、次回 STALE) (R4)、HELLO 各段階 (SYN / SYN-ACK / CONFIRM / ESTABLISHED) の消失、CPL=0 経路の open / close。
  - その他: HTTP 処理中 (PROCESSING) の別要求の ACK / WINDOW、最終 DATA 消失、EOF 消失、owner 回収 (親へ戻った状態)、payload 長不一致のフレーム破棄、ユーザーコピー中の再同期 / close (gen 再確認)。既存 M2 反射試験と L0〜L3 を回帰対象に。
- ゲスト (`kernel-lgy98-link`、WSL2 の `host_agent.py` v2): `userland/tests/host_test.c` で `GET /pattern/65536` (AGAIN ループ)、`GET /notfound` = 404、`TIME`、`ECHO 5` + write、GUI 配下で `gui_busy` と同時、CTRL+STOP 後の open、Agent 再起動 → STALE → close → open。

## 4. レビューで見てほしい点

1. §1b の 3 way HELLO (SYN / SYN-ACK / CONFIRM / ESTABLISHED、Agent 採番の sess) が B3 / B4 / B5 を閉じるか、CONFIRM / ESTABLISHED の消失時の挙動。
2. rid 台帳の規則 (1)〜(5) と RELEASE の bit0 ACK が B1 / B2 / R4 / R5 を閉じるか (RELEASE 未達 + 新 REQUEST、STATUS と RESPONSE の競合、REQUEST 先着 / RELEASE 先着)。
3. 制御結果 (flags bit0) と業務結果の分離 (B7)、TOMBSTONE を受けたハンドルの STALE 化。
4. セッション内単調な rid と STALE close の無通知、専用スロットの RELEASE (B6、non-blocker の容量)。
5. §2a の駆動・排他・公平化 (R8 / R9 / R10) と、KAPI の cli 区間 + gen 再確認。
6. §3 の試験が B1〜B8、R1〜R4 の反例を個別に踏むか。
7. HOLE の導入 (R1) が墓標規則 (3) と矛盾しないか、SYN-ACK の `req_sess` / `req_epoch` 照合 (R2) で遅延応答の反例が残らないか、`host_read` の成功確定点 (R4) が STALE 化と競合しないか。

## 5. 呼び出し列の例 (N3 が従う)

```
GET:      h = host_open("GET http://x/") ; loop { host_status(h,&st,&len); AGAIN → sys_yield }
          ; loop { n = host_read(h,buf,4096); AGAIN → sys_yield; 0 → break; >0 → 書く } ; host_close(h)
CLIP PUT: h = host_open("CLIP PUT 5") ; loop { n = host_write(h,"hello",5); AGAIN → sys_yield }
          ; loop { host_status ; AGAIN → sys_yield } ; host_close(h)
PRINT:    h1 = open("PRINT OPEN rep text") → status/read "job 7" → close
          h2 = open("PRINT DATA 7 4096") → write ×3 → status (200) → close   (繰り返し)
          h3 = open("PRINT CLOSE 7") → status/read "pages 1" → close
```

## 6. ユーザー判断が要る点

- HOST_SERVICES_PLAN §9 の 6 項目は推奨案で進める。
- ワイヤ v2 で L0〜L3 の合格実績を取り直す (N1 の受入に含める)。
- **往復 3/3 + 承認済みの追加 1 往復を使い切った** (§7)。第 5 版で R1〜R4 を反映済み。→ ユーザー決裁 (b): 第 5 版で N1 へ進み、実装レビューで見る (2026-09-14)。

## 7. レビュー記録

| 版 | 判定 | 要旨 |
|---|---|---|
| 第 5 版 | (レビュー無し、決裁 (b) で N1 へ) | R1〜R4 の反映のみ。N1 の実装レビューで第 5 版の HOLE / req_sess 照合 / 枯渇停止 / 成功確定点を見る |
| 第 4 版 (追加往復、ユーザー承認) | Request changes | 4 件: R1 high_water 規則が未受理 (欠落) の REQUEST を墓標にする、R2 nonce だけの SYN-ACK 照合では再起動時に旧セッションへ戻れる、R3 16bit sess の周回は「再使用しない」と両立しない、R4 コピー後の STALE 返却が「失敗時は出力を書かない」に反する。non-blocker: RESP の probe、NO_SLOT 後の再送、owner の名称、同期ループの残存確認、非 present はフォールトガード。→ 第 5 版: HOLE、`req_sess` / `req_epoch` の写し、枯渇停止、cli 区間で成功確定 |
| 第 3 版 | Request changes | 9 件: B1 RELEASE の ACK が REQUEST の ACK と識別不能、B2 RELEASE が直近 rid の重複排除情報を消す、B3 sess (時刻 + tick) では再起動を識別できない、B4 遅延した別 sess の HELLO が現行セッションを破棄、B5 epoch 周回で新 HELLO が拒否される、B6 同 sess の epoch 更新で Agent 資源が残る / STALE close の RELEASE が新 epoch と衝突、B7 制御用 503 / 410 が HTTP ステータスと衝突、B8 帯外ポインタはディスパッチャが kill (INVAL ではない)、B9 正典が未同期 (レビュー中に `76ea249` で同期済み)。→ 第 4 版: ACK flags bit0、RELEASED 墓標と台帳規則 (1)〜(5)、3 way HELLO と Agent 採番 sess、epoch 周回は新セッション、セッション内単調 rid + STALE close 無通知 + 専用スロット、RESPONSE flags bit0 で制御 / 業務を分離、2 段検査を ABI に明記 |
| 第 2 版 | Request changes | 11 件: R1 通常フレームの agent が矛盾、R2 REQUEST と WDATA の ACK 空間衝突、R3 転送 ACK 後の RESPONSE 消失、R4 close 通知が無く Agent の受付 2 件を置換できない、R5 結果 8 件では重複排除を保証できない、R6 ACK 済み・読出し中の Agent 再起動を検出できない、R7 HELLO の鮮度と epoch 再使用、R8 同期自己試験の再入、R9 制御通知の無条件優先で飢餓、R10 反射モードの RX 争奪、R11 正典に旧契約が残る。→ 第 3 版: sess / nonce / agent、rid 内単一 seq、STATUS (102 / 410 / 503) と RELEASE と墓標、T_probe の生存確認、自己試験の非同期化、交互送信と NIC 受理からの RTO、反射モードの排他、正典同期を完了条件に |
| 第 1 版 | Request changes | 12 件: B1 非同期の駆動元が無い、B2 KAPI と IRQ の排他が無い、B3 要求と DATA/EOF の対応付けが無く discard 印も不成立、B4 Agent の逐次処理はストリームの直列配送を保証しない、B5 EOF だけでは完了を確定できない、B6 EOF とリング解放の混同、B7 REQUEST 再送の保持・重複排除・期限が無い、B8 WDATA の ACK 待ちに回復と識別が無い、B9 送信本文の終端が無い、B10 TIME の形式、B11 epoch の生成と旧世代拒否が無い、B12 試験骨子が進まない状態機械を合格にできる。→ 第 2 版: ワイヤ v2 (rid / agent 付き 20B ヘッダ、宣言長、WDATA の rid+seq、TIME 標準化、epoch 生成と再同期の契機)、`link_tick` だけが更新者 + cli 区間 + 中継バッファ、WINDOW を開始許可にした直列化と close までの所有権、転送 ACK と RESPONSE の分離、実 Agent を結ぶホスト試験 |
