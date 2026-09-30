# LGY-98 / NE2000 ドライバ実装計画

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-05)**
>
> 対象は OS32 の Ethernet デバイスドライバ。
> この文書を設計・進捗の正典とする。実装完了後の現行仕様は `docs/05_drivers.md` に移す。

## 1. 到達点と実装方針

LGY-98 1枚を対象に、32bit protected mode のカーネルから Ethernet フレームを
送受信できるようにする。PCI/ISA の汎用検出、多数の互換カード、複数 NIC は対象外。
ARP/IP/ICMP/UDP/TCP、DHCP、DNS、ソケット、アプリケーションは別計画とする
(独自リンクプロトコルと Host Services は [LINK_PLAN.md](LINK_PLAN.md))。
ドライバの完成は raw Ethernet の対向試験で判定でき、IP スタックを前提にしない。

推奨構成は **転送と IRQ 入口を NASM、初期化・リング管理・復旧を GNU89 C** とする。
ベアメタルであることと、全処理をアセンブラで書くことは別の判断である。
既存の `include/io.h` でも `in/out` と `rep insw` は直接生成される。
全面 NASM 化で速度やサイズが必ず改善するとは現時点では断定できない。
まず同じ転送命令を使う C と NASM を比較し、効果がある制御処理は追加で移す。
全面 NASM を選ぶ場合も、以下の API・状態遷移・テスト条件を維持する。

## 2. 調査で確認したこと／実機で確定すること

### LGY-98 固有部分

NE2000 互換なのは制御方式であり、PC/AT 向けの I/O 定数を流用してはいけない。
以下は NP21/W の解析資料と、ローカルの ai-debug fork
`~/np21w-src/src/network/lgy98.c` の `lgy98_bind()` 等で確認した値。
エミュレータ実装の確認と実カードの検証は区別する。

| 項目 | 設計に使う値・扱い |
|---|---|
| NIC レジスタ | BASE + 0x00〜0x0F、8bit I/O |
| データポート | BASE + 0x200、16bit PIO を基本とする |
| リセットポート | BASE + 0x18。読出しで reset、実機の read/write 手順・待ち時間は M0 で確認 |
| 独自設定ポート | BASE + 0x300〜0x30F。初版は EEPROM 書換えを実装しない |
| BASE 候補 | 0x00D0〜0x70D0、0x1000 刻み |
| カード INT → PIC IRQ | INT0 → IRQ3、INT1 → IRQ5、INT2 → IRQ6、INT5 → IRQ12 |
| 開発用の候補 | BASE=0x10D0、IRQ5（ローカル NP21/W ソースの既定値。ユーザー環境の設定値ではない） |

資料とローカルソースのコメントには INT2 の扱いに不一致がある。
初版は設定した BASE と IRQ を明示的に使い、実際の割り込み発生で検証する。
未設定時は無効。広域 I/O スキャンや IRQ 自動検出はしない。
カード未装着・応答不良・IRQ 競合では NIC を無効化して OS 起動を続ける。

OS32 の IRQ12 は音源/V86 用の入口が既にある。初版は IRQ3/5/6 を候補とし、
他の拡張機器との競合も確認する。IRQ12 は既存利用との排他を決めるまで拒否する。
「カードの INT 番号」「PIC IRQ」「IDT ベクタ」を設定・ログで混同しない。

M0 ではカードの型番・基板リビジョン、BASE、INT、媒体、MAC PROM の並び、
搭載 RAM 範囲、16bit 転送、reset 手順、必要な I/O wait を確認して表を凍結する。
MAC の OUI だけで正常／異常を判定せず、全0・全1・multicast MAC 等を検査する。
EEPROM の未知のシーケンスを初期化の必須条件にしない。

### OS32 側

- `kernel/idt.c` は IRQ3/5/6 を unexpected IRQ スタブに接続している。
  専用入口を登録し、デバイス初期化後に `irq_enable()` する必要がある。
- `kernel/isr_stub.asm` のセグメント設定と `IRETD_USER` を使い、CPL3/V86 からの割り込みを壊さない。
  NIC IRQ は OS32 が所有し、DOS ゲストへ反射しない。V86 の I/O ポリシーでも所有ポートを保護する。
- `kernel/isr_handlers.c:timer_handler()` の現行処理は `snd_tick()`。
  常設のネットワーク worker があるとは仮定しない。
- カーネル帯域と 16KB スタックには余裕が限られる。フレームをスタック上に置かず、
  `.text/.data/.bss` とリンク後の帯域を計測する。
- NE2000 の Remote DMA は NIC RAM とデータポートの転送機構。
  ホスト側は CPU の PIO であり、PC-98 DMAC を使わない。
  [HW2] の DMAC 64KB 境界対策を理由に低位メモリを確保する必要はない。

## 3. ファイル構成と境界

| 予定ファイル | 責務 |
|---|---|
| `drivers/lgy98.c` | BASE/IRQ 検証、カード固有ポート、probe/reset、OS32 接続 |
| `drivers/ne2000.c` | 8390 初期化、TX/RX、Remote DMA 制御、エラー復旧、統計 |
| `drivers/ne2000_io.asm` | `rep insw` / `rep outsw` と奇数末尾の安全な転送 |
| `drivers/ne2000.h` | カーネル内部 API、状態、結果コード |
| `drivers/ne2000_regs.h` | レジスタ定数の正典。NASM が必要な定数は生成 include で共有 |
| `kernel/isr_stub.asm` / `kernel/idt.c` | NIC IRQ 入口・登録・EOI の責任を一元化 |
| `kernel/kernel.c` / `include/config.h` | 無効が既定の起動設定、初期化順 |
| `build/kernel.mk` | C/ASM ソースと必要な include path・生成依存関係 |

汎用ネットワークフレームワークや関数ポインタ式のバス抽象化は最初は作らない。
LGY-98 のポートを初期化時に確定し、転送ループ内では変換処理をしない。
C/ASM 間は System V i386 ABI。callee-saved register を保存し、DF=0 を保証する。
IRQ 入口でも C/string 命令の前に `cld` を実行し、中断元の EFLAGS は `iretd` で戻す。
386 命令のみ、データポートは16bit。32bit I/O を動作確認なしに使わない。

内部 API の案（名前と型は M0 で凍結）:

```c
int ne2k_init(const struct ne2k_config *config);
void ne2k_stop(void);
int ne2k_send(const void *frame, unsigned int length);
int ne2k_recv(void *frame, unsigned int capacity, unsigned int *length);
void ne2k_poll(unsigned int budget);
void ne2k_irq(void);
void ne2k_get_stats(struct ne2k_stats *stats);
```

- 対象は1枚、カーネル所有バッファのみ。初版に KAPI は追加しない。
- `send` 成功は NIC RAM へのコピーと送信受付完了。回線上の完了は PTX/TXE と統計で区別。
  呼出しから戻れば送信元バッファを再利用できる。送信中は BUSY、無期限に待たない。
- `recv` は固定キューからコピー。空は AGAIN、容量不足は必要長を返してフレームを残す。
  戻り値と長さを分離し、エラー時のバッファ内容を成功として使わない。
- フレームは Ethernet ヘッダを含み FCS を含まない。初版は通常の非 VLAN フレームを対象とし、
  TX は 14〜1514 bytes を受け付け、60 bytes 未満をゼロで padding。RX は通常60〜1514 bytes。
- `stop` は NIC 割り込みを止め、未完了送信・受信キューを破棄して停止状態にする。
  統計取得は競合しないスナップショットとする。

## 4. 初期化・送受信の実装

### 初期化

設定検証 → NIC 割り込み禁止 → reset と期限付き完了待ち → STOP → DCR/RBCR 設定 →
PROM 読出し・MAC 確認 → RAM 検証 → TX/RX 領域設定 → PAR/MAR/CURR 設定 →
ISR の既存状態消去 → START → 通常の RCR/TCR → 最後に IMR と PIC を有効化する。
RAM パターン試験は NIC を停止している診断時だけ行い、通常運用中には行わない。
送受信許可の正確なレジスタ順序は DP8390D §11 と実カードの条件で確定する。
初版のフィルタは自 MAC + broadcast、multicast/promiscuous は既定で無効。

16KB RAM が確認できた場合の初期配置案は NIC 内アドレス 0x4000〜0x7FFF:

- TX: page 0x40〜0x45（1536 bytes、1スロット）。
- RX: PSTART=0x46、PSTOP=0x80（排他的終端）、BNRY=0x46、CURR/next=0x47。
- ホスト側 RX キュー: 1536 bytes × 8、TX 作業領域: 1536 bytes。
  フレーム領域は合計13.5KiB、別途キュー管理・統計。実装時にマップで収容を確認する。

これは RAM 容量確認前の仮定。初版から RAM 可変配置や TX 二重化は実装しない。

### Remote DMA と TX

転送ごとに古い RDC を消去し、RSAR/RBCR を設定、read/write command を発行する。
データポートへの16bit連続転送後に RDC を期限付きで確認し、DMA を終了状態へ戻す。
CR bit5 のみを汎用の完了条件にしない。LGY-98 特有の追加判定が必要なら実機試験で限定する。

奇数長は最後の1 byteを独立に扱う。TX の最終 word の上位をゼロにし、呼出し元を
1 byte 読み越さない。RX の最終 word は一時レジスタに読み、下位1 byteだけ保存する。
Remote DMA の偶数丸め長、Ethernet padding 後の TBCR、呼出し元長を別変数にする。
短い送信フレームの padding に未初期化メモリを使わない。

TXP 中に同じ TX 領域を上書きしない。RDC は NIC RAM への転送完了、PTX は送信完了。
TXE の衝突・underrun 等を記録し、送信タイムアウトでは有限回の再初期化後に停止する。
任意のフレームを無条件で再送し続けない。

### RX リング

`next == CURR` なら空。4-byte RX header を先に読み、status / next page / byte count を検証する。
受信長は header を除き FCS を含むカウントから FCS 分を除いて上位へ渡す。
範囲外 next、自己参照、長さとページ進行の矛盾、runt/oversize、エラー status を扱う。
ページ数の整合チェックは境界長を含め M1 のモデル試験で確定する。
リング終端をまたぐ読み出しは2転送へ分割する。

データの回収後に `next` を更新し、BNRY を next の1ページ前に置く。
先頭へ回ったときは PSTOP−1。BNRY を早く進めて未コピーのデータを NIC に再利用させない。
ホストキュー満杯なら新しいフレームを捨てて BNRY を進め、drop を数える。
壊れた header をたどって無限ループせず、リング再同期／再初期化へ移る。

### 排他と復旧

CR の register page と Remote DMA は RX/TX/IRQ で共有される。
一つの NIC 操作だけが所有できる状態にし、入出時は page0・DMA idle を保証する。
短い状態変更は既存 `irq_save/irq_restore` を使う。長い完了待ち全体を CLI で囲まない。
foreground の操作中は NIC IMR をマスクし、完了後に ISR/CURR を再確認してから再許可する。
既に pending の PIC IRQ が来ても、busy 中の入口は NIC レジスタを変更しない。
タイマ側の補助処理も busy を見て延期し、再入を許さない。

割り込み禁止中に進まない PIT tick だけで待ちの上限を実装しない。
校正済み delay と有限回ポーリング等で、reset/RDC/TX にそれぞれ上限を設ける。
具体値は M0 で仕様値と実機余裕から決定する。

OVW は単に ISR を消して再開しない。DP8390D §7 の停止・待機・RBCR クリア・
loopback・リング回収・OVW ACK・通常モード復帰の手順を実装する。
送信中だったか、既に完了／失敗したかを保存し、必要な場合だけ再送する。
OVW 中の待機は状態機械に分け、IRQ 内で長い待機をしない。
RDC timeout、header 破損、回復失敗を別カウンタで記録する。

## 5. IRQ と処理時間

M2 までは診断ループから `ne2k_poll(budget)` を頻繁に呼ぶ。
この段階は常時受信を保証する完成ドライバとは扱わない。

M3 は NIC IRQ で TX 結果を記録し、RX を固定長ホストキューへ回収する。
IRQ 内でプロトコル処理、ユーザー callback、printf、malloc、ファイル I/O は行わない。
処理済み ISR bit だけを ACK し、PIC EOI は既存スタブ方式に合わせて1回だけ送る。
ISR の再読出しと CURR 確認により、ACK 前後に到着したフレームを取りこぼさない。

受信処理はフレーム数とバイト数の両方で予算を設け、初期候補を4フレームとする。
予算を使い切ったら NIC 割り込みをマスクして pending を保持し、
`ne2k_poll()` または100Hzタイマの限定的な補助処理で続行して再許可する。
タイマ補助も同じ排他と予算に従い、プロトコル処理はしない。
回復待ち状態もここで進める。単に IRQ を止めたまま次の受信通知を待たない。

100Hz の補助だけでは負荷時のリングあふれを防ぎ切れない。
通常は NIC IRQ で速やかに回収し、過負荷では有界処理と drop 計数を優先する。
386 実機で IRQ 滞在時間・音源/シリアルへの影響を測り、許容できなければ
より短い転送単位またはカーネルの遅延実行機構を別途設計する。
「未実装の worker が後で処理する」前提で M3 を完了にしない。

## 6. 実装順と合格条件

| 段階 | 作業 | 合格条件 |
|---|---|---|
| M0 | 実カード条件、ポート/IRQ、RAM/PROM/reset、API・定数・待ち時間の確定 | 根拠と未確認事項を記録、競合なし、未装着時に起動継続 |
| M1 | NASM PIO、Remote DMA、初期化、リング境界ロジック | RAM パターン一致、奇数末尾・境界外アクセスなし、全待ちが有限 |
| M2 | ポーリング TX/RX、内部 loopback、対向 raw Ethernet | フレーム長・内容・連番が一致、リング wrap を繰り返せる |
| M3 | IRQ、固定キュー、排他、予算と再開経路 | CPL3/V86中も動作、IRQ競合を拒否、通常負荷で回収停止なし |
| M4 | OVW/TXE/RDC timeout・破損ヘッダ・未装着等の障害試験 | 復旧またはNICだけ停止、OS操作継続、drop/error の理由が観測可能 |
| M5 | 実機で性能・サイズ比較、予算調整、現行仕様化 | 安定送受信、回帰なし、対象機種・限界・測定値を記録 |

試験条件:

- TX 入力14/59/60/61/1513/1514 bytes と不正長、RX の最短/最大/奇数長。
  256-byte ページ境界前後、PSTOP wrap、キュー満杯、連続送受信を含める。
- DMA/TX 完了イベントの前後へ RX を重ね、page 切替中の競合を検査する。
- OVW、壊れた next/count、RDC 不成立、ケーブル断、誤 IRQ、再初期化失敗。
  エミュレータが自然再現できない障害はモデル／注入試験と実機確認を区別する。
- 対向機は専用 EtherType の連番付き試験フレームを送受信し、キャプチャと比較する。
  通常のキャプチャは padding/FCS の見え方が異なるため、その点も記録する。
- 低レートで無欠落を確認後、連続10分以上と負荷増加で安定性・drop・復旧を確認。
- キーボード、38400bps シリアル、音源、FDD、CPL3プログラム、V86 の回帰を確認。
  通信中にユーザープログラムが停止してもドライバの IRQ 処理が継続することを確認する。

NASM の評価は C `-O2` / `-Os` と同じアルゴリズム・同じ転送命令で比較する。
指標は `.text/.data/.bss`、リンク・配備バイナリ増分、CPU占有時間、IRQ最大滞在時間、
pps、実効 bytes/s、drop/overrun。エミュレータの速度を実機性能の根拠にしない。
10Mbps 全線速は測定前に約束しない。TX二重化や制御処理のNASM化は測定後の改善候補。

実装時は `make kernel` / `make check`、配備後の新バイナリ確認とゲスト試験を実施する。
純粋なリング計算・境界検証はホストでも試験し、I/O の実動作試験を代替したとは扱わない。
診断を外部プログラム化する段階で KAPI を末尾追加し、生成・版更新・
`make clean` → `make all` と `userland/deploy.yaml` 登録を行う。
NHD/エミュレータ設定変更は実施時に該当する承認・停止手順へ従う。

## 7. 参照と現時点の検証範囲

- [NP21/W LGY-98 解析資料](https://simk98.github.io/np21w/docs/lgy98.html):
  PC-98 固有設定の一次調査。未解明部分を含むため実機条件と区別する。
- [National Semiconductor DP8390D/NS32490D データシート](https://media.digikey.com/pdf/Data%20Sheets/Texas%20Instruments%20PDFs/DP8390D,NS32490D.pdf):
  §7 受信と overwrite recovery、§8 送信、§9 Remote DMA、§10 レジスタ、§11 初期化、§12 loopback。
- ローカル NP21/W: `~/np21w-src/src/network/lgy98.c`、`lgy98.h`、`lgy98dev.h`。
  実装上の観測値を参照し、エミュレータ特有の省略を実カードの仕様にしない。
- OS32: `include/io.h`、`kernel/idt.c`、`kernel/isr_stub.asm`、`kernel/isr_handlers.c`、
  `kernel/cpu_calibrate.h`、`build/kernel.mk`、`build/os32.ld`。
- 制約: [CONSTRAINTS.md](../../CONSTRAINTS.md)、[POLICY_DEV.md](../../POLICY_DEV.md)。

現時点は資料・ソース調査と計画書作成のみ。M0〜M5 は未完了。
NIC コード、KAPI、エミュレータ設定、ディスクイメージは変更していない。
ドライバのビルド・エミュレータ送受信・実機試験・性能測定は未実施。

## 8. エミュレータ側の確認 (2026-09-05 追記、ローカル ai-debug fork のソースで照合)

§2 の値は `~/np21w-src/src/network/lgy98.c` の `lgy98_reset()` / `lgy98_bind()` と
一致することを確認した (BASE 既定 0x10D0、IRQ 既定 5、`lgy98_IRQ2IDX` は IRQ 3/5/6/12 だけが
有効、ポートは BASE+0x00〜0x0F / +0x18 / +0x200 / +0x300〜0x30F)。加えて、計画に載っていない
エミュレータ側の事実と道具を記録する。**いずれもエミュレータの挙動であり実カードの仕様ではない。**

| 項目 | NP21/W (ai-debug fork, x64 Release) の実装 | 計画への影響 |
|---|---|---|
| NIC RAM | 32KB (`NE2000_PMEM_START` 16KB 〜 48KB、ページ 0x40〜0xBF) | §4 の 16KB 配置 (PSTOP=0x80) はそのまま使える。RAM 検証は 16KB / 32KB の両方を受け付け、PSTOP を probe 結果から決める |
| 16bit データポート | `iocore_inp16/out16` が **BASE+0x200 ちょうど**だけを 16bit ハンドラへ回す。DCR bit0 (WTS) が 1 のときだけ word 転送 | `rep insw/outsw` は使える。DCR を先に設定すること。BASE+0x201 を触らない |
| リセットポート +0x18 | **read で `ne2000_reset()` (ISR=RESET、PROM を RAM 先頭へ複製)**、write は何もしない | §2 の「読出しで reset」と一致。実カードの手順は M0 で別途確認 |
| PROM | MAC 6B を RAM 先頭に置き `mem[14] = 0x57` (NE2000 署名) | PROM 読み出しの並び (1B おきか) は Remote DMA の read で確認する |
| 割り込み | `ne2000_update_irq()` に **2ms 以内の再アサートを抑える回避策** (暴走対策) がある | エミュレータでは連続する ISR 変化が 1 回の IRQ にまとまる。§5 の「ACK 後に ISR と CURR を再確認」が無いと取りこぼす。実機では起きない差として M3 の試験条件に明記 |
| 設定ポート +0x300 | EEPROM 相当のシーケンス応答 (`lgy98seq_*`)。INT 設定は `lgy98_setromdata()` で IRQ → 表位置に反映 | 初版は触らない (§2 のとおり)。触るなら実カードの解析が先 |
| ini | `USELGY98=true` / `LGY98IO` / `LGY98IRQ` / `LGY98MAC` / `NP2NETSOCK=127.0.0.1:8026` (ソケット中継。空なら TAP) | 現在の `np21x64w.ini` にはどれも無い。**追加は [D2] 承認事項**、NP21/W 停止中に編集 |

### ai-debug の LAN 道具 (M2 の対向試験に使う)

fork には TAP 不要のソケットバックエンド (`SUPPORT_NET_SOCKET`) と HTTP API がある
(`~/np21w-src/docs/05-network.md`):

| 道具 | 使い方 | 計画での位置 |
|---|---|---|
| `GET /api/net` | backend / connected / I/O ベース / IRQ / MAC / tx・rx フレーム数・バイト数・破棄数 | M1〜M3 の観測 (ドライバ側統計との突き合わせ) |
| `GET /api/net/capture[?clear=1]` | 直近 64 フレーム (`dir` = tx/rx、hex 1600B まで) | M2 の「キャプチャと比較」を対向機なしで行う |
| `POST /api/net/inject` (hex) | ネットワークから届いたことにしてゲスト NIC へ配送 | M2/M4 の RX 試験 (奇数長、境界、壊れた長さの注入) |
| `POST /api/net/send` (hex) | ゲストが送ったことにしてバックエンドへ | ホスト側の疎通確認 (ドライバ不要) |
| `tools/np2net_helper.py` (WSL2) | `--pcap` で pcap 保存。stub モードは 10.0.2.2 として ARP / ICMP echo / UDP echo(7) に応答。`--tap` で Linux tap へ | M2 の対向機。専用 EtherType の連番フレームは helper を拡張して返す |

M2 の合格判定は「inject → recv で内容一致」「send → capture で内容一致」を先に通し、その後に
helper 対向で連続送受信を行う順にする。エミュレータで確認できないのは §6 のとおり
(ウェイト、実 IRQ タイミング、ケーブル断)。

### 手元にある実機 (2026-09-17)

ユーザーが **PC-9821Ra266** を入手した。**ただし当面は使わない** — 検証はエミュレータで
十分というユーザー判断 (2026-09-17)。たまたま入手した機体であって、実機試験を
前倒しする理由にはしない。M4 の残り / M5 / N5 に着手するときのために事実だけ残す。

| 項目 | 内容 | この計画への影響 |
|---|---|---|
| オンボード LAN | **Intel 82557**、PCI、100BASE-TX / 10BASE-T | **v3 の実機 LAN はこれを狙う** (ユーザー決裁 2026-09-17)。PCI の列挙から作る。LGY-98 が高価・品薄なのに対し**機体に載っていてゼロ円**、そして **Intel の公開開発者マニュアルと Linux `e100` / FreeBSD `fxp` という参照実装がある** (LGY-98 は一次資料が無い)。詳細は [../realhw/PLAN.md](../realhw/PLAN.md) §5 |
| 拡張スロット | **C バス 3 本** / PCI 2 本 | **C バスに LGY-98 を挿せば既存のドライバがそのまま使える。** M4 の残り (受信バッファ溢れからの復帰) を確かめるならこれが最短 |
| 内蔵グラフィックス | Trident TGUI9682XGi (VRAM 2MB) | GUI の HAL に無いが **PEGC は使えるとユーザーが確認済み**。NP21/W の Trident は結線されていないので**書いても検証できない** → 保留 ([../realhw/PLAN.md](../realhw/PLAN.md) §7) |

出典: [PC-9821Ra266 データベース](https://www.pc-9800.net/db_98/data/pc-9821ra266.htm)、
[PC-9821Ra ページ](https://takerun.my.coocan.jp/mypc/9821ra.htm)。
**`docs/hw/` (Bible / UNDOCUMENTED) にはこの機種の記述は無い** (調査済み)。

### ローカル資料の所在

`docs/hw/` (PC-9801 Bible / UNDOCUMENTED) に LGY-98 と NE2000 の記述は無い (`io_pnp.md` に
LAN の語があるだけ)。PC-98 固有部分の一次資料は §7 の simk98 の解析ページと本 fork の
ソースだけなので、M0 の「実カードで確定する表」はそのまま必要。

### OS32 側で確認済みのこと

- `include/io.h` には `insw_rep` (rep insw) だけがあり、`outsw` 系は無い。§3 の
  `drivers/ne2000_io.asm` で両方向を持つ方針と矛盾しない。
- `kernel/idt.c` は IRQ 3/5/6 を `irq_stub_unexp_*` に接続している (§2 のとおり)。
  IRQ12 は `irq_stub_12` (サウンドボード)。
- `kernel/cpu_calibrate.h` (`cpu_delay_us`) と `include/io.h` の `irq_save/irq_restore` は存在する。
- ドキュメント運用: 本計画が設計と進捗の正典 (INDEX.md「情報単位ごとの正典」の流儀)。
  実装完了後の現行仕様は `docs/05_drivers.md` へ、落とし穴の経緯は `POLICY_DEBUG.md §4` へ。


## 9. 進捗と確定事項 (2026-09-05 着手、ブランチ `feat/lgy98`)

| 段階 | 状態 | 備考 |
|---|---|---|
| M0 | **凍結 (エミュレータ基準)** | 下表。実カードの列は未確認のまま |
| M1 | **エミュレータで合格** (2026-09-05) | `make clean` → `kernel-lgy98` → NHD 配備。起動後 `ne2k_nic` を `/api/mem` で読み: RUNNING、MAC = ini の値、PROM 重複あり、RAM 32KB 検出 (PSTOP 0xC0)、RAM 全域パターン試験 0 エラー、Remote DMA 往復 (1/2/3/59/60/61/255/256/257/1513/1514B、読み側余白無傷) 0 エラー、RDC timeout 0。kselftest 42/0、配備後回帰 6/6 |
| M2 | **エミュレータで合格** (inject/capture 経路) | `make check-net-m2` (`tools/net_m2_test.py`): 14/60/61/100/255/256/257/1000/1513/1514B の反射が内容一致、連続 10 フレーム順序どおり、1000〜1514B × 60 逐次で PSTOP wrap を繰り返して 0 失敗、ドライバ計数 = 送った数、NP21/W 側 drop 0。**未実施**: 対向機との raw Ethernet (helper は ARP/ICMP/UDP しか返さず、IP 層が無いので保留)、内部 loopback (NP21/W が再現しない、実機項目) |
| M3 | **エミュレータで合格** (2026-09-05) | IRQ 駆動: IRQ5 スタブ登録 + IMR。`make check-net-m2` を IRQ 駆動カーネルで実行し `irq +80`・0 drop・ACK 後の recheck が毎回機能。`make check-net-m2-cpl3` で CPL3 プログラム (`less`) 常駐中も `irq +80`・0 drop、`less` は正常終了しシェル復帰。kselftest 42/0、回帰 6/6 |
| M4 | **エミュレータで合格** (2026-09-05) | 「溢れても・取りこぼしても壊れない」安全網。`ne2k_timer_tick` を 100Hz 受信ウォッチドッグ化 (RUNNING/OVW_WAIT かつ非 busy なら毎 tick 有界 poll、健全時は CURR 1 読みで戻る)。`make check-net-m4`: 200 フレーム一括 inject でも state RUNNING・drop 0・バースト後の単発受信 OK・wedge なし。リンク層向けに `ne2k_rx_ring_free_pages()` / `ne2k_rx_queue_free()` を公開。M3 で見つけた wedge (リングに残り二度と IRQ が来ない) の再発防止。**注記**: 本試験では IRQ が追いつき watchdog_frames は 0 (ウォッチドッグは安全網として実装・コードレビュー確認、強制 IRQ 喪失下の発火は未計測)。NP21/W は OVW を立てず黙って捨てるため OVW 復帰経路は実カード項目 |
| M5 | 未着手 | 実機で性能・サイズ・現行仕様化。**実カードは Intel 82557 になる** (2026-09-17 決裁)。82557 は受信の作りが NE2000 と別 (ディスクリプタ連結) なので、M4 の「溢れからの復帰」の宿題は**82557 の言葉で立て直す** |

### M0 で凍結した表

| 項目 | 設計値 | 根拠 | 実カードでの確認 |
|---|---|---|---|
| レジスタ | BASE+0x00〜0x0F (8bit) | §2 / §8 | 未 |
| データポート | BASE+0x200 (16bit のみ、DCR WTS=1) | §8 (iocore) | 未 (8bit 転送の可否) |
| リセット | BASE+0x18 読み出し → ISR.RST を最大 20ms 待つ | §8 (`lgy98_ib018`)、DP8390D | 未 (read/write の別、待ち時間) |
| 存在確認 | PSTART/PSTOP を書いて page2 から読み返し + PROM 署名 0x57 (byte 14/15、重複なら 28..31) | 不在時は 0xFF | 未 |
| PROM | Remote DMA read 32B。全 16 組が重複なら偶数バイトを MAC | NP21/W は重複 | 未 (並び) |
| MAC 検査 | 全 0 / 全 1 / multicast ビットを拒否 | §2 | — |
| RAM | 先頭ページ 0x40。probe で 16KB / 32KB を判定 (16KB 末尾 + 32KB 側の往復 + 先頭ページのエイリアス検査) | NP21/W は 32KB | 未 (実カードは 16KB 想定) |
| 配置 | TX 0x40〜0x45 (1 スロット)、RX PSTART 0x46、PSTOP = 0x40 + RAM ページ数 (0x80 / 0xC0) | §4 | — |
| DCR | 0x49 (WTS, LS, FT1) | NE2000 系ドライバの標準値 | 未 |
| RCR / TCR | 通常: RCR=AB (自 MAC + broadcast)、TCR=0。probe 中: RCR=MON、TCR=内部 loopback | DP8390D §11 | 未 |
| IRQ | PIC IRQ 3 / 5 / 6 のみ受理、12 は拒否 (NE2K_ERR_IRQ)。IMR でマスク中 (未使用) であることを確認 | §2 | — |
| 受信 count | 実カード: ヘッダ 4B + データ + FCS 4B (DP8390D)。NP21/W: FCS を含まない (`np2_detect()` で切替) | §8 / DP8390D | **未 (最重要)** |
| 待ち上限 | reset 20ms / RDC 4ms / OVW 停止後 2ms / TX 50ms (tick, IF=1 のみ) / 再初期化 3 回 / 不整合ヘッダ 4 連続 | DP8390D の値に余裕 | M5 で見直し |
| Remote DMA | STA を立てて発行 (Linux ne.c / FreeBSD if_ed と同じ)。probe 中は PSTOP=0xFF で折り返しを避ける | 8390 は PSTOP で PSTART へ戻る | 未 |

### 実装上の決定 (計画からの差分)

- **NASM の生成 include は不要**: `ne2000_io.asm` はポート番号を引数で受けるので、
  レジスタ定数の正典は `drivers/ne2000_regs.h` 1 か所で済む。
- **リング計算を `ne2000_ring.c` に分離**: I/O 無しの純粋関数にし、`tools/tests/ne2000_ring_test.c`
  をホスト gcc で `make check` から回す (実カード形式と NP21/W 形式の両方、ページ境界、PSTOP wrap、
  異常ヘッダ)。幾何だけでは count の形式違い (FCS の有無) は検出できないので、その判定は
  `np2_detect()` と M0 の実機確認に置く。
- **ホスト側 RX キュー 8 × 1516B、TX 作業領域 1516B** を BSS に置く (約 13.6KB)。
  カーネル帯域の余裕は約 144KB (kernel.map、2026-09-05) なので収まる。
- **診断は初期化時のフラグ** (`LGY98_FLAG_DIAG` → RAM 全域パターン試験 + Remote DMA 往復試験
  1/2/3/59/60/61/255/256/257/1513/1514 バイトと読み側余白の検査) で行い、結果は統計
  (`diag_ram_errors` / `diag_dma_errors` / `rdc_timeout`) と起動ログに出す。
- **有効化はビルド時** `make kernel-lgy98` (スタンプ `build/out/lgy98.enabled` を置くので、以後の
  `make kernel` / `deploy-nhd` も有効のまま。`make kernel-nolgy98` で戻す。emu_agent の許可リストに追加)。
  `CONFIG_LGY98_BASE=0` (既定) では `lgy98_init()` は何もしない。`make kernel LGY98=1` だけだと
  deploy がカーネルを作り直した時に無効へ戻る (2026-09-05 に実際に踏んだ)。
- IRQ 入口 (`isr_stub.asm` のスタブ、`idt.c` の登録、IMR の有効化) は M3 で入れる。
  `ne2k_irq()` の busy/pending の枠だけ先に置いた。
- **M2 の試験経路は反射モード**: `LGY98_FLAG_REFLECT` で `timer_handler` (100Hz) から `lgy98_tick()`
  が `ne2k_poll(2)` と受信フレームの MAC 入れ替え送信を行う。`tools/net_m2_test.py` が
  `/api/net/inject` → `/api/net/capture` (dir=tx) と `lgy98_reflected` (kernel.map 経由) で照合する。
  試験は `make check-net-m2` (ローカル AI の許可リスト) で回す。KAPI は未追加。

### エミュレータでは確認できないこと (M4/M5 の実機項目)

- OVW: NP21/W の `ne2000_receive` はリング満杯時にフレームを**捨てる**だけで OVW を立てない。
  復旧経路 (§4) はモデル試験と実機で確認する。
- 内部 loopback: NP21/W は TCR の loopback を再現せず常に外へ送る。M2 の「内部 loopback」は
  実機項目に回し、エミュレータでは inject/capture で代替する。
- MON: NP21/W は RCR.MON を再現しない。probe 中に自 MAC 宛ユニキャストが届くと PROM 複製
  領域 (mem[0..]) が上書きされ得るが、PROM は reset 直後に読むので実害はない。
- 2ms 以内の IRQ 再アサート抑制 (§8) は M3 の試験条件に残す。合格した M3 試験では
  ドライバの ACK 後 recheck (ISR / CURR を有限回再確認) が毎回働き、この抑制下でも取りこぼしゼロ。

### M3 で分かった注意点

- **aidebug のデバッグサーバは単一スレッド**で要求を 1 つずつ処理する
  (`np21w-src/src/win9x/aidebug/aidebug_server.cpp`)。`sleep` のように戻らないコマンドを
  `/api/cmd` で投げている間は `/api/net/inject` も `/api/mem` も詰まる。同時実行試験は
  `less` のような対話常駐プログラム (画面を出してキー待ちに入り `/api/cmd` が戻る) で行う。
  `v86` セッション中の LAN 試験は inject 経路が使えないので別機構が要る (M4 以降)。
- **過負荷 wedge (M4 で直す)**: 上記の病的手順 (デバッグサーバ 40s 停止 → 80 フレーム一括
  inject) でリングが飽和し、ドライバが `imr 0x3f` / `rx_backlog 0` の見かけ正常なのに IRQ が
  来なくなる状態を再現した。通常運転 (フレームが来るたびにドレイン) では起きない。OVW の
  検出・復帰とホストキュー満杯時のバックプレッシャを M4 で詰める。
