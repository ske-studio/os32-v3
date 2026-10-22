# OS32 リンクプロトコル / Host Services 実装計画

> 上位: [LGY-98 ドライバの計画](PLAN.md)

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-14)**

> **L0〜L3 は実装・エミュレータ合格済み。KAPI v51 で公開し、N1 のコア受入も完了 (2026-09-14)。**
> 対象は LGY-98 の上のリンクプロトコルと Host Services。本書はリンク層の設計・進捗の正典。
> サービスの受入・実機残件は `HOST_SERVICES_PLAN` §7、
> NIC の受入・残件は [PLAN §9](PLAN.md#9-進捗と確定事項-2026-09-05-着手ブランチ-featlgy98) を参照。

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
置き換えた。v2 の受入範囲と未確認事項は [§5-1](#5-1-進捗と証拠) を参照。

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

## 5-1. 進捗と証拠

- L0〜L3 は実装・エミュレータ合格 (2026-09-05)。各段の条件は §5、試験出力・ページ消費実測・修正経緯は 完了記録 (アーカイブ: `docs/archive/network/LINK_IMPLEMENTATION_RECORDS.md`)。
- N1 はワイヤ v2・KAPI v51 の 5 本を公開し、コア受入完了 (2026-09-14)。契約は `HOST_SERVICES_PLAN` §3、駆動は `link_tick()`。同期版 API は廃止済み。
- N1 のホスト試験・ケース対応は [n1_tdd.md](../../../tools/tests/n1_tdd.md)、ゲスト判定と制限は TASK_N1 (アーカイブ: `docs/archive/network/TASK_N1.md`)。64KB 超の F6 は N3 の `wget` で再確認済み (`HOST_SERVICES_PLAN` §9-3)。
- 実 LAN での受入は未実施。N5 (`HOST_SERVICES_PLAN` §7) と [NIC の実機項目](PLAN.md#エミュレータでは確認できないこと-m4m5-の実機項目) に残す。
- N1 の Agent 再起動後の STALE と `gui_busy` 同時実行は、既存のゲスト記録では未確認 (ホスト試験との区別を維持)。個別手順と当時の代替証拠は N1 ゲスト記録 (アーカイブ: `docs/archive/network/TASK_N1.md`)。

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
- 実装・公開・受入と未確認事項: [§5-1](#5-1-進捗と証拠)。
