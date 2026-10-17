# リンク層 L0〜L3・N1 の実装経緯

> 状態: **完了記録**

[LINK_PLAN](../../tasks/network/LINK_PLAN.md) §5-1 から抽出。以下は各記録時点の状態で、現行の受入・残件は元文書を参照。

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
  移植性調査は [`docs/archive/portability/SURVEY_N1.md`](../portability/SURVEY_N1.md)。
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

