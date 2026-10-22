# 実機で試す装置に要る状態 — 共通の要件

> 状態: **現行 (2026-10-22)** — 装置の部品 (HWDEV) の核を作るときに想定した要件。装置ごとの設計はここから引く。
> 出所: ~/os32-tmp/run/devmod/DESIGN.final.md §2 を 2026-10-22 に粒度別に分けて写した。

実機で装置を調べ・試し・戻すとき (realhw_recover と HWDEV の核 — 段の状態は票 TASK_HWDEV が持つ) に、装置の種類ごとに何が要るかの受入要件。ここに書くのは要件の形で、停止レジスタや待ち時間の値ではない。未読の装置仕様から値を創作せず、各装置の profile を設計する前にメーカー資料と BSD/MIT 等の実装を照合して根拠を登録する ([HW3])。

装置に依らない安全の約束 (書き戻してはいけないもの、DMA/IRQ を先に止める、復帰先 S0〜COLD、軽い順の復帰、手で戻す手段と PG リセットの扱い) は [REALHW_RECOVER_DESIGN.md](REALHW_RECOVER_DESIGN.md) §2〜§6 が正で、ここでは繰り返さない。

## 1. 装置ごとに要る状態

| 装置 | 調べる・保存する状態 | BM/DMA の止め方と置き場 | 割込み | 本番のドライバとの持ち主 | 電源を切るまで戻せない状態 | 時間の上限 |
|---|---|---|---|---|---|---|
| PCI GPU (例: GeForce 6200 / 内蔵 Trident) | PCI ID・BAR、ROM、許可した RW、index/lock、出力の経路、COLD の記録 | BM を立てない試行が既定。描画エンジン・出力の停止を装置別に証明。DMA を使う profile は別の受入。VRAM は専有の予約だけ | 装置別に停止を証明してから試す | GUI を止めて CUI で行う。内蔵表示の既存の持ち主と排他 | PLL・メモリ制御・VRAM・内部 FIFO などの内部状態は再初期化か cold-required。安全な RW だけ復元 | 実行枠・送信・復帰の各上限を資料と実測で固定 |
| PC-98 IDE | I/O 範囲、選択中の drive、command の進行、status 読出しの副作用、driver の shadow | PIO では BM なし。DMA 対応機はエンジン停止と完了の確認も要る。転送 buffer と未完了の要求を保全 | 供給を止めてから pending を処理 | 起動ディスクが動いている間は、観測でも副作用の分類が要る。破壊的な試行は拒否。将来は別媒体で起動し、FS を同期・切り離した後 | 媒体への書込みは snapshot で戻らない | busy 待ち・flush・復帰に別々の上限 |
| USB: OHCI/UHCI/EHCI | schedule、root hub、port、所有権、転送要求・IRQ の状態 | controller 別に schedule 停止 → halt 確認 → IRQ 停止/排出 → BM 停止確認。HCCA/ED/TD、frame list、QH/qTD 等は装置別の整列・到達幅の専有通常 RAM | 停止と排出を確かめる | 動いている USB driver・接続機器と排他 | 完了不明の OUT は再送不可。reset で機器の状態も失う | 停止・reset・port・各転送に個別の上限。確認できなければ DMA RAM を隔離 |
| SATA: AHCI | MMIO、port/link、command slot、受信 FIS、IRQ、未完了 I/O | command/FIS エンジンの停止と非稼働の確認、IRQ 停止、BM 停止を profile で順序化。command list/table・FIS・PRDT・data は専有 DMA RAM | 停止を順序の中で確かめる | root やマウント済みのディスクは拒否 | write・cache・消去等は電源断でも巻き戻らない | port 停止・reset・link・command に別々の上限。失敗時は RAM を返さない |
| TV チューナー: bt848 系 | MMIO、capture/RISC、IRQ、I2C でつながる chip、tuner の状態 | capture/DMA sequencer の停止・idle 確認 → IRQ 停止/排出 → BM 停止。RISC 列と frame buffer は専有 DMA RAM。I2C は ACK と待ちに上限 | 停止と排出を確かめる | 動いている capture driver と排他 | 外付け chip の write-only レジスタ、I2C の途中状態は再初期化か電源断が要る場合がある | field・DMA 停止・I2C transaction ごとに上限 |
| LAN (対照) | queue、RX/TX descriptor、IRQ、通信セッション | DMA と callback を止めて確かめてから持ち主を移す。未送信 packet を snapshot の値だけで再開しない | callback の寿命まで止める | SerialFS の転送・配備の経路を自分で止めない。v1 は LAN の所有中を拒否、実験の制御は独立したシリアル | — | 停止・排出の期限が要る |

- USB の BIOS からの引き継ぎ (legacy handoff、BIOS が持つ controller の所有を OS へ移す手順) が PC-98 で要るかは未確認。USB の装置設計の最初に資料で確かめる。

## 2. 今の recover の前提で足りない所

今の recover は「BM を立てない・本番のドライバと共有しない」装置 (GeForce 段 4a) を前提にしている。部品に分けただけでは次を満たしたことにしない。

- DMA の開始と停止の証明、未完了の要求、IRQ の callback の寿命、RAM の隔離、ドライバへの返却。
- [HW2] の 64KiB 境界は ISA DMA の条件。PCI の BM は各 controller のアドレス幅・整列・descriptor 境界・長さに従う。共通の 64KiB 制約で済ませず、CPU の仮想番地を DMA 番地にしない。
- 核の将来の DMA lease: `owner/session/generation, phys, len, align, addr_bits, boundary, active, quarantined`。準備済みの buffer だけを plan から参照し、BM の許可は専用の capability。GF の BM 禁止はこれと独立に永久に維持する。
- DMA を止められなければ FAILED に固定し、buffer と部品を boot の寿命で隔離する。BM bit=0 だけで未完了の転送が消えたとは認めない。IRQ も共有 PIC 線の一括 mask だけで停止の証明にしない。
- 時間は tick の差分と独立の poll/step 上限で測る。バスアクセス自体が帰らない場合はソフトで救えず、立会いの物理復帰 ([REALHW_RECOVER_DESIGN.md](REALHW_RECOVER_DESIGN.md) §6)。媒体の破壊は COLD でも戻らないと結果に区別する。
- 持ち主の取り合い: v1 は「既に停止・非所有」の装置だけを claim でき、root IDE・LAN/SerialFS・表示ドライバの所有中や未分類は拒否する ([HWDEV_DESIGN.md](HWDEV_DESIGN.md) §4)。動的な登録と重複の仲裁 (registry 本体)、ドライバからの引渡し (停止/排出 → 所有世代の更新 → 試行 → 停止確認 → ドライバ再初期化) は、最初の BM 装置の設計で決める。
- 将来の DMA RAM は装置別に明示した上限で事前に予約し、確保に失敗したら書込み 0。停止できずに隔離した分も 32MB の中で数える。
