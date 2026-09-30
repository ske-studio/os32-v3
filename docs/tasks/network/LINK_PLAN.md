# OS32 リンクプロトコル / Host Services 実装計画

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-14)**

> **L0〜L3 の機構はエミュレータ実装・合格済み (2026-09-05、ブランチ→main)。残るは L3 の
> KAPI 公開のみ。** 版番号は v50 まで進んだので v43 は欠番、KAPI 公開と印刷などの追加サービスは
> [HOST_SERVICES_PLAN.md](HOST_SERVICES_PLAN.md) (2026-09-13、v51 で公開する詳細計画) に移した。 対象は LGY-98 ドライバの上に載る 2 層 ―
> OS32 独自リンクプロトコル (フロー制御つき raw Ethernet) と Host Services
> (HTTP / File / RPC の要求応答)。この文書を設計・進捗の正典とする。
> NIC ドライバそのものは [PLAN.md](PLAN.md) (別計画)。実装完了後の現行仕様は
> `docs/05_drivers.md` (リンク層) と `docs/KAPI_SPEC.md` (Host Services API) に移す。

## 1. 方針

OS32 に TCP/IP・DNS・HTTP・FTP・TLS を載せない。これらの重い処理は現代の
ホスト側 (Host Agent) が担い、OS32 は独自 P2P プロトコルで「HTTP_GET」
「NET_CONNECT」などの要求を出して結果だけ受け取る。ホストをネットワーク用の
I/O コプロセッサとして扱い、将来はファイル・時刻・デバッグにも同じ仕組みを広げる。

3 層を分離する。各層は下位の抽象だけに依存し、上位の都合を持ち込まない。

```
┌──────────────────────────────┐
│ Host Services                │  HTTP / File / RPC / Socket
│ stream_id / request_id       │  KAPI として外部プログラムへ公開
├──────────────────────────────┤
│ OS32 Link Protocol           │  HELLO / REQUEST / RESPONSE
│ sequence / flow control      │  DATA / EOF / ACK / WINDOW
├──────────────────────────────┤
│ NE2000 / LGY-98 Driver       │  raw Ethernet / Remote DMA
│ ([PLAN.md] の M1〜M5)        │  RX ring / TX / OVW recovery / stats
└──────────────────────────────┘
```

- ドライバは NIC 操作だけ。フロー制御・HTTP・分割はここに入れない。
- リンク層は「確実に届ける」と「溢れさせない」。中身の意味は解釈しない。
- Host Services は要求応答とストリームの意味づけ。KAPI を末尾追加する。

## 2. 一番重要なのは Host → OS32 方向のフロー制御

現代 PC は LGY-98 より圧倒的に速い。ホストが連続送信すると NE2000 の RX SRAM
リング (16〜32KB、256B ページ) が溢れる。逆に OS32 → Host はホストに余裕がある
ので詰まらない。よって**非対称なフロー制御**でよい ― Host→OS32 だけを絞る。

**Credit と OVW recovery は役割が違う。両方いる。**

| 機構 | 役割 | 置き場所 |
|---|---|---|
| WINDOW / Credit | 通常運転で**溢れさせない** | リンク層 |
| OVW recovery ([PLAN.md] M4) | 溢れても**壊れない・復旧する** | ドライバ |

Credit は性能向上機能ではなく、通常運転で OVW を起こさないための機構。M4 は
Credit が壊れても (ホストの誤動作・欠落・競合) OS が死なないための安全網。

### 2-1. WINDOW は絶対値通知 (加算式にしない)

`CREDIT +6` のような加算式は、制御フレームの重複受信でホストが増分を二重に
数える危険がある。OS32 は**絶対値の広告**を送る。新しい WINDOW が古いものを
置き換えるので、重複しても安全 (最後の 1 通だけが効く)。

```
WINDOW
  epoch        = 1     // リンクセッションの世代。再同期で +1
  ack_seq      = 123   // ここまで受信・処理済み (これ以下は再送不要)
  credit_pages = 12    // 未 ACK 分を含め、あと最大 12 ページ分まで送ってよい
```

意味は「現在、未 ACK 分を含めて最大 credit_pages ページ分まで送信可能」。
ホストは `sent_pages(未ACK) ≤ credit_pages` を保ちながら送る。将来は
`grant_total` / `sent_total` の累積カウンタ方式にしてもよいが、初版は
`ack_seq + credit_pages` で足りる。epoch はリンク再同期 (HELLO 再送) で増やし、
古い epoch の WINDOW / ACK を捨てるのに使う。

### 2-2. Credit は 2 段のボトルネックの小さい方から出す

受信は 2 段ある。Credit はどちらも溢れさせてはいけない。

```
          RX SRAM               RX software queue
Host → LGY-98 [ Ring ] → Remote DMA → [ RX Queue ] → Host Service
```

```
credit_pages = min( rx_ring_free_pages, rx_queue_free_pages_equiv ) - safety_margin
```

- `safety_margin`: IRQ 遅延・Remote DMA 中・処理遅延を見て、最大 Ethernet フレーム
  1〜2 枚分のページを予約する。空きを全部 credit にしない。
- **層の分離を保つ**: ドライバは空きを返すだけ (`ne2k_rx_ring_free()` /
  `ne2k_rx_queue_free()`)、WINDOW を作るのはリンク層。ドライバに credit を
  計算させない。これらの getter は M4 で PLAN.md 側に足す (リンク層の着手前)。

### 2-3. ページ消費量は実測して確定する (決め打ちしない)

DP8390 は受信リングで各パケットの前に 4 バイトの独自受信ヘッダを置き、リングは
256B ページ単位で進む (DP8390 Overview / データシート §7)。しかし Host Agent が
送る raw Ethernet フレームが **LGY-98 / NP2 の SRAM 上で実際に何ページ消費するか**は、
`ceil((4 + サイズ) / 256)` と最初から決め打ちせず、**実測を credit cost の正典にする**。

- ドライバは受信ヘッダの `next page` を持つ。CURR が実際に何ページ進むかが
  最も確実な情報なので、これを credit の 1 単位あたり消費量の基準にする。
- 試験: 代表長 (60 / 61 / 255 / 256 / 257 / 1514B) を inject し、CURR の進みを
  記録して「フレーム長 → 消費ページ数」の表を凍結する (Stop-and-Wait 段階で実施)。
- `tools/net_m2_test.py` は既に CURR 由来の `rx_next` を観測できる。ここに
  ページ消費の計測を足す。

## 3. 大容量データは再結合せずストリーミング

HTTP レスポンスが 10MB でも、OS32 側で 10MB に再構築しない (それは「重い処理は
ホストへ」の思想と逆)。ホスト RAM を巨大な受信バッファとして使い、OS32 は
**ストリームとして順次消費**する。

```
アプリ: HTTP_GET → (Host RAM に全体を保持)
Host → OS32: DATA seq=0 / DATA seq=1 / ... / EOF     (credit の分だけ小出し)
OS32 Host Service: host_read(handle, buf, 1024) で順番に読む
```

リンク層が保証するのは DATA フレームまで:

```
DATA
  stream_id    // どの要求のストリームか
  seq          // ストリーム内の連番 (順序保証・欠落検出・再送)
  length       // このフレームのペイロード長
  payload      // ≤ MTU − ヘッダ
EOF stream_id  // ストリーム終端
```

- OS32 側に巨大な再結合バッファは要らない。未読分はホスト RAM に残り、credit で
  流量を抑える。アプリが読み進めた分だけ ACK / WINDOW が進み、次が届く。
- MTU (1514B) を超えるペイロードは seq で分割されて届く。「分割・再結合」ではなく
  「分割されたストリームを順に消費」である点が前回の表現との違い。

## 4. フレーム形式 (raw Ethernet, 独自 EtherType)

**正典は `docs/archive/network/TASK_N0.md` §1b (ワイヤ v2、2026-09-14)**。ここは要約で、
食い違ったら N0 が勝つ。v1 (12B ヘッダ、`stream_id` 無し、L0〜L3 で合格) は N1 で v2 に
置き換え、合格実績は v2 で取り直す。

- P2P なので ARP は不要。OS32 の MAC と Host Agent の MAC は HELLO で交換して
  以後固定する。EtherType は 0x88B5 (experimental 帯)。
- リンクヘッダ **20B、明示的に直列化 (LE アクセサ、C 構造体の padding に依存しない)**:
  `op(u8)` `flags(u8)` `epoch(u16)` `seq(u32)` `ack(u32)` `length(u16)` `rid(u32)` `sess(u16)`。
  `sess` = セッション ID (**Agent が永続カウンタで採番、再使用せず枯渇で停止**、全フレーム)、`epoch` = セッション内の
  再同期世代 (OS32 が +1、周回は新セッション)、`rid` = 要求 ID (セッション内で単調増加、0 は
  使わない)。HELLO 以外は `sess` と `epoch` が控えと一致するフレームだけ受け付ける (両端とも)。
- 制御フレーム (WINDOW / ACK / STATUS / RELEASE / HELLO) は小さく、60B へ padding して送る。

| op | 向き | 用途 |
|---|---|---|
| HELLO (1) | 双方向 | **3 way** (flags: SYN / SYN-ACK / CONFIRM / ESTABLISHED)。`seq` = OS32 の nonce、`ack` = Agent の nonce、payload = Agent 世代 (`agent u16`)。MAC / epoch / sess の交換、再同期。遅延した HELLO 1 通では切替が起きない |
| REQUEST (2) | OS32 → Host | 要求行 (`rid`、`seq` = 0)。本文が要る要求は要求行に宣言長 |
| WDATA (8) | OS32 → Host | 要求本文 (`rid`、`seq` = 1〜、REQUEST と同じ seq 空間) |
| RESPONSE (3) | Host → OS32 | 要求の結果 (`rid`、本文 6B 固定 = `status u16` + `length u32`)。flags bit0 = 制御結果 (PROCESSING / TOMBSTONE / NO_SLOT)、flags 0 = 業務結果 (HTTP ステータスはそのまま) |
| STATUS (9) | OS32 → Host | `rid` の結果の再提示 / 生存確認の要求 |
| RELEASE (10) | OS32 → Host | ハンドルを閉じた通知 (Agent はその `rid` を捨てて墓標を残し、flags bit0 の ACK を返す) |
| DATA (4) / EOF (5) | Host → OS32 | 応答本文のストリーム (`rid`、`seq` = 1〜)。WINDOW を受けた `rid` だけ |
| ACK (6) | 双方向 | `rid` + `ack` = 順序どおり受けた最終 seq (累積)。flags bit0 = RELEASE への ACK |
| WINDOW (7) | OS32 → Host | 絶対値 credit の広告 (§2-1) と配送開始の許可 |

OS32 → Host のデータ (WDATA) は 1 本ずつ ACK を待って送る (ホストに余裕があるので
credit は無いが、stop-and-wait で順序と重複排除を単純にする — 非対称、§2)。

## 5. 開発順序

```
M4 (PLAN.md)     リングを溢れさせても復旧できる安全網。rx_ring_free/rx_queue_free を公開
   ↓
Stop-and-Wait    独自 EtherType で確実に 1 往復 (HELLO → REQUEST/RESPONSE → ACK)。
   ↓             同時にページ消費量を実測して credit cost 表を凍結 (§2-3)
WINDOW/Credit    絶対値 WINDOW で Host→OS32 を複数フレーム化 (§2-1, §2-2)
   ↓
Streaming        DATA seq による連続転送、OS32 は順次消費 (§3)
   ↓
Host Services    HTTP / File / RPC を KAPI 末尾追加。Host Agent を実装
```

| 段階 | 合格条件 |
|---|---|
| L0 Stop-and-Wait | **エミュレータ合格 (2026-09-05)**。HELLO 交換後、1 要求 1 応答が往復。ACK まで次を送らない。重複 WINDOW/ACK に耐える。ページ消費表を凍結 |
| L1 WINDOW/Credit | **エミュレータ合格 (2026-09-05)**。Host が credit_pages を超えて送らない。OVW を通常運転で起こさない。重複 WINDOW で二重加算しない (絶対値) |
| L2 Streaming | **エミュレータ合格 (2026-09-05)**。大容量応答を seq 連続で受け、OS32 側の再結合バッファ無しで順次消費。欠落は再送で埋まる |
| L3 Host Services | **機構はエミュレータ合格 (2026-09-05)**。外部プログラムから HTTP_GET → host_read が動く。回線速度に依らず OS32 側のメモリ上限が一定 |

## 5-1. 進捗 (ブランチ `feat/net-link`)

> **履歴**: この節の v43 予約・KAPI 4 本案・呼び手側で `link_poll` を回す記述は v1 時点のもの。現行は KAPI v51 の 5 本と `link_tick` 駆動 (TASK_N0 §1a / §2a)。

- **L0 実装・エミュレータ合格 (2026-09-05)**: `net/link.{c,h}` (独自 EtherType 0x88B5、
  16B リンクヘッダ op/epoch/seq/ack/length、EtherType はワイヤ big-endian・以降は LE)。
  `drivers/lgy98.c` は `LGY98_FLAG_LINKTEST` で attach 後に `link_selftest(10)` を実行。
  `tools/host_agent.py` (HELLO 応答 + REQUEST→RESPONSE echo) を対向に、
  `make check-net-l0` で HELLO 確立・10/10 往復・再送 0・取りこぼし 0 を確認。
  ビルドは `make kernel-lgy98-link` (LGY98_FLAGS=9 = DIAG+LINKTEST、反射なし)。
- **L1 実装・エミュレータ合格 (2026-09-05)**: 絶対値 WINDOW (ack=順序どおり受けた最終 seq、
  payload に credit_pages)。credit = min(`ne2k_rx_ring_free_pages`, `ne2k_rx_queue_free`×6)
  − 安全マージン 12 ページ。`link_l1_bulk` がホストに DATA を流させ、`make check-net-l1` で
  200/200 を順序どおり・EOF・NIC drop 0・credit 有界 (36 ページ) を確認。ホストは credit を
  絶対値として守り (host_agent.py の in-flight 計上)、リングを溢れさせない。
- **ページ消費の実測 (§2-3) 完了**: ドライバの `rx_pages_total` を CURR の進みから積算し、
  512B ペイロードで **2.99 ページ/フレーム** を計測 (理論値 ceil((4+14+16+512)/256)=3 と一致)。
  credit cost は当面この実測値を使い、host_agent は保守的に 6 ページ/フレームで見積もる。
- **途中の修正**: 連続転送中に Remote DMA の RDC が 1 回タイムアウトし reinit が走って
  リング全体 (in-flight DATA) が消え、L1 が gap でデッドロックしていた。dma_read/write を
  1 回だけ再発行するようにして解消 (commit)。
- **L2 実装・エミュレータ合格 (2026-09-05)**: 8KB のリングバッファに順序どおりの DATA を
  溜め `link_stream_read` で順次消費 (再結合バッファなし)。バッファ空きを credit に反映して
  消費が遅ければホストへ背圧。欠落 (seq>期待) は捨てて ack を止め、ホストの Go-Back-N 再送で
  埋める。`make check-net-l2`: 128KB を 8KB バッファで全消費・内容一致・EOF・誘発した欠落を
  回復 (gaps>0 かつ read==total)・overflow 0・NIC drop 0。
- **L3 実装・エミュレータ合格 (2026-09-05)**: `link_service_get` が「GET <資源>」を出し、
  ホストの RESPONSE (status u16 + len u32) を受けて本文を L2 ストリームで消費する。`make
  check-net-l3`: GET /pattern/65536 を検証、/notfound で 404、TIME RPC、そして **GET
  http://example.com/ でホストが実 HTTP を取得し 559B をストリーム配送** (オンライン時)。
  OS32 は TCP/IP も HTTP も持たず、要求を出して結果だけ受け取る (方針どおり)。
  `link_stream_read` がアプリ側の消費入口。Host Agent (`tools/host_agent.py`) は /pattern を
  生成配送、http(s):// を urllib で実取得、/file/ をホストファイル読み、TIME を時刻応答。
- **N1 実装完了 (2026-09-14、コーダー worktree。ホスト TDD のみ — ゲスト受入は PM)**:
  ワイヤ v2 (20B ヘッダ・3 way HELLO・rid 台帳・RELEASE/STATUS) と
  `net/link.c` の非ブロッキング化、**KAPI v51** (`host_open` / `host_status` /
  `host_read` / `host_write` / `host_close`、slot 208〜212 = 0x348〜0x358)、
  `tools/host_agent.py` v2 を実装。プロトコルを進めるのは 100Hz の `link_tick()`
  だけ (`kernel/isr_handlers.c` の `ne2k_timer_tick()` の直後。反射モードでは
  `link_init` を呼ばないので起動しない)。同期版 `link_hello` / `link_request` /
  `link_service_get` / `link_stream_read` / `link_poll` は**廃止**し、自己試験
  (L0〜L3) を非同期 API + IF=1 の hlt 待ちの上に書き直した。
  ホスト TDD 2 本が GREEN: `make check-host-agent` (25/25、Agent 側の反例) と
  `make check-net-link-host` (29/29、実 `net/link.c` + 実 `kapi/kapi_host.c` +
  実 Agent をサブプロセス)。ケース名と TASK_N0 §3 の指摘番号の対応表・決めたこと・
  既存 `check-net-l0`〜`l3` が読むシンボルの v2 での意味は
  [`tools/tests/n1_tdd.md`](../../../tools/tests/n1_tdd.md)。
  移植性調査は [`docs/tasks/portability/SURVEY_N1.md`](../portability/SURVEY_N1.md)。
  **未実施** (PM / テスターの受入): `make clean` → `make all` → `make check`、
  `kernel-lgy98-link` の配備、`userland/tests/host_test.c` の実機実行、
  `check-net-l0`〜`l3` と `check-net-m2` の回帰。
- **(履歴) 残りは KAPI 公開だけ (KAPI v43 案)**: 以下は v1 時点の案で、確定した ABI は
  **KAPI v51** (TASK_N0 §1a、上の N1 の行)。当時の記録として残す。Host Services を外部プログラムへ
  出す。版番号は [KAPI_SPEC §3-2 の予約表](../../KAPI_SPEC.md) で **v43** に調停済み
  (GUI が v42、その次。2026-09-06 に v41/v42 → v42/v43 へ改訂)。追加する KAPI (案、末尾追記):

  | 関数 | 内容 |
  |---|---|
  | `i32 host_open(const char *req, u32 len)` | サービス要求 (`GET <資源>` 等) を送りハンドルを返す。RESPONSE の status/length は `host_status` で取る。負値は `LINK_ERR*` を `OS32_ERR_*` に写像 |
  | `i32 host_read(i32 h, void *buf, u32 cap)` | ストリームから最大 cap バイト読む (`link_stream_read` の口)。0 = EOF、負値 = エラー。回線速度に依らず OS32 のメモリ上限は buf 分だけ (§3 の思想) |
  | `i32 host_status(i32 h, u32 *status, u32 *length)` | RESPONSE の status (200/404 等) と本文長 |
  | `void host_close(i32 h)` | ハンドルを閉じ、未読ストリームを捨てる |

  エラー番号は既存の `OS32_ERR_*` (-1〜-10) に写像する: 送信失敗 → `IO` (-1)、引数不正 →
  `INVAL` (-9)、未対応 → `NOSYS` (-10)。**GUI が取る -11〜-13 は使わず**、固有番号が要るときは
  -14 以降 (KAPI_SPEC §3-2 の予約)。
  現状の `link_service_get` は同期版 (自己試験用)。KAPI 化では `host_open` / `host_read` を
  逐次・非ブロッキングに分け、アプリの `sys_halt` / GUI の `OP_WAIT` と協調して `link_poll` を
  回す (計算ループでカーソルを止めないのと同じ設計)。**GUI 実装 (v42) の後に着手する。**

## 6. Host Agent

`np2net_helper.py` の stub (ARP/ICMP/UDP echo) を置き換える、OS32 リンクプロトコルを
話す常駐エージェント。ai-debug のソケットバックエンド (`NP2NETSOCK`) か TAP に接続し、

- OS32 の REQUEST を受けて実際のインターネット処理 (TCP/IP/DNS/HTTP/TLS) を行い、
- 結果をホスト RAM に貯め、OS32 の WINDOW/credit の分だけ DATA を小出しする。
- OS32 の ACK / WINDOW を絶対値として解釈し、未 ACK 分を再送する。

実装は WSL2 側 (Python 標準ライブラリ + ホストの通常ネットワーク)。試験順序は
[PLAN.md §8](PLAN.md) の inject/capture を使い、対向機なしで L0→L1 を通してから
実 Host Agent に上げる。

## 7. 参照

- NIC ドライバ: [PLAN.md](PLAN.md) (M1〜M5)。リンク層は M4 完了後に着手。
- 受信リング / 受信ヘッダ / ページ: DP8390 Overview
  (https://www.osdever.net/documents/DP8390Overview.pdf)、DP8390D データシート §7。
- 制約: [CONSTRAINTS.md](../../CONSTRAINTS.md) ([D2] ini 変更、[D3] 秘密の非出力)。
- 現時点は設計のみ。L0〜L3 は未着手。リンク層コード・KAPI・Host Agent は未実装。
