# TASK_PPC_LAN — PowerPC 移植: 内蔵 Ethernet (GMAC) で起動ログをホストへ、固定の診断コマンド

> 上位: [TASK_PPC_KBOOT](TASK_PPC_KBOOT.md)

> 状態: **受入完了・実機確認待ち (2026-11-11)** — QEMU 7450・7455 で受入済み。LAN (GMAC) で起動ログをホストへ保存し 5 診断が往復する。受入は kboot_run・kboot_accept・kboot_all に入った。実機の前の段 (UniNorth の clock の alias、実機の PHY、cache・DMA) は持越し台帳の PPC-LAN-RH。段の状態は §2。確定設計は [PPC_LAN_DESIGN](../../decisions/ppc/PPC_LAN_DESIGN.md)。手本は BSD (ユーザー決定)。メーカー資料は未入手で、実機に載せる条件にはしない。

## 0. ユーザー決定

- 2026-11-09 決定 (c) (票 TASK_PPC_SEP): 到達点 ② の直後は、内蔵 Ethernet (GMAC) と Host Services の LAN 経由のログ・遠隔操作を最優先にする (実機にはシリアルを簡単に取り出す経路が無い)。
- 2026-11-10 (PM の 3 問への回答):
  1. 道は「GMAC を最初から」— QEMU の sungem と実機 QuickSilver で同じ driver を使う (既存の 82557 を先に動かす案・GMAC を実機だけにする案は採らない)。
  2. ログの保証は「届く分だけ + 欠落数」— 固定長の待ち行列にためて polling で送り、あふれた分は捨て、起動の識別・連番・欠落数をホストに見せる。再送しない。割り込みは使わない。
  3. 遠隔操作は「固定の診断コマンドから」— 決まった要求に答える小さな monitor を先に作る。任意の shell とファイル同期 (hsync) は、ユーザープログラムを動かす段 (③) の後に分ける。

- 2026-11-10 (PM の問い「メーカー資料が入手できない場合に BSD の実装だけを根拠に実機へ載せてよいか」への回答): **「BSD を手本に」** — GMAC・UniNorth・PHY は BSD (OpenBSD・NetBSD・FreeBSD) の実装を手本にして進める。メーカー資料は、手に入れば照合に使うが、実機に載せる条件にはしない。

PM 決定 (2026-11-10、設計の往復の中で。ユーザーに報告済み):

- 起動の識別 (boot_id) はゲストが作る。相手 (peer) の既定は broadcast で、OF の bootargs は任意の上書き。実機で起動ごとの手入力を要求しない。
- 診断コマンドは 5 個。5 番目は範囲つきの読み出し (PEEK): managed RAM とカーネルの像だけ、1 回 256 バイトまで、範囲外・MMIO・NIC が持っている受信 buffer は拒否。書込みと任意番地は入れない。
- ホストの道具 (tools/ppc_lan_host.py) の運び方は 2 つ: QEMU と試験は UDP (QEMU の socket netdev は Ethernet の frame を 1 datagram に 1 枚で運ぶ。root も TAP も要らない)、実機は AF_PACKET。codec は共通。
- PLN1 (線の上の形式) に認証は無い。既定では起動ログを broadcast で流し、同じ L2 から PEEK でカーネルの RAM を読める。開発用の LAN での診断が前提で、共用の LAN につなぐときは peer を指定する。
- ② の境界 (mount・module・exec・USER なし) は広げない。NIC の失敗で OS を止めない (止めるのは「bus master の解除を書いたのに読み戻しが 1」のときだけ)。

## 1. 詳細設計 (確定) の要約

全文は [PPC_LAN_DESIGN](../../decisions/ppc/PPC_LAN_DESIGN.md)。service (段 P24) と起動への結線 (P25・P26) の境目は [PPC_LAN_SERVICE_DESIGN](../../decisions/ppc/PPC_LAN_SERVICE_DESIGN.md)、serial の LAN の印と受入の自動化 (P29〜P32) は [PPC_LAN_MARKS_DESIGN](../../decisions/ppc/PPC_LAN_MARKS_DESIGN.md)。

- 層: OF の tree の採取 (takeover の前) → UniNorth の PCI の設定空間 (OF が示す 1 つの BDF だけ) → MMIO の alias (BAR 2MiB のうち 64KiB を I/G で) → DMA の pool (像の予約の中、descriptor は I/G、buffer は cache の同期) → GEM の core (reset・MIF/PHY・ring・MAC・tx・rx、レジスタも descriptor も LE) → ログの sink (byte 詰めの待ち行列) と線の上の形式 → 診断 monitor → ホストの道具。
- 置き場: drivers/net/gem/ (core)、platform/powermac/ (tree・PCI・DMA・clock・ログ・線・monitor・service)、arch/ppc/ (alias・cache・probe)、tools/ (ホストの道具と meter)。x86 の生成物は変えない (全段 I、build_compare で確かめる)。
- 段: 製品の段 P0〜P33 (1 ファイル = 1 段) と、各段の直後の host 試験の段 H*。並列に出せる組と前提は設計の §6。最初の段 P0 は QEMU での採取とホストの UDP 経路の確認、資料の取り寄せ。
- 受入: A0〜A12 (設計の §7)。QEMU の meter (tools/ppc/kboot_run.py) に LAN の印を足し、既存の KBOOT の 9 行は変えない。7450 と 7455 で、保存したログと欠落数・診断の応答を照合する。
- 実機で初めて確かめる物: clock gate、PHY の固有の処置、DMA の到達性と cache、PCI の domain の差。QEMU の合格から外挿しない。

## 2. 段の状態

| 段 | 状態 |
|---|---|
| 調査 (x86 の LAN の層、GMAC の手順、PPC の PCI、案の比較) | 済 (2026-11-10、~/os32-tmp/run/ppc-lan-survey/SURVEY.md) |
| 設計の往復 (起草 r1 → レビュー → r2 → レビュー → 確定) | 済 (2026-11-10) |
| P0・P1・P2・P3・P8・P11 と各 H | main (0c5ab75f)。P0 の採取 (QEMU 8.2.2、OpenBIOS の対話): GMAC は /pci@f2000000/ethernet@f (compatible gmac、106b:0021)、BAR0 は PCI = CPU 0x82200000・2MiB、MAC 52:54:00:12:34:56、host bridge は uni-north・reg 0xf2000000 から 32MiB・ranges は io → 0xf2000000 (8MiB) と mem 0x80000000 → 0x80000000 (256MiB)。PHY は 2026-11-11 に PM が OF の Forth で MIF を叩いて採取: MDI0 (内部) の address 0、PHYID 0x0040/0x6210 = Broadcom BCM5201 rev 0 (設計の「段 P15〜P19 の取り込みで PM が決定」) |
| P4・P5・P12・P20・P21 と各 H | main (519a39b3)。P5 で gmac_tree.c が PPC の像に入った |
| P9・P10・P13・P14 と各 H | main (7176c2be)。P9 で ② の起動が GMAC の先頭 64KiB と UniNorth の config の 2 page の alias を張る (QEMU で gmac_ready=1)。clock は手本 netbsd uninorth.c:135-142、reset は openbsd gem.c の各関数 |
| P15〜P19 と各 H | main (a9d655bc)。GEM の core (ring・MAC・MIF と PHY・TX・RX)、手本は openbsd gem.c と sys/dev/mii、対応の PHY は BCM5201 だけ (実機の PHY は実機で採ってから足す) |
| P22a〜P22d・P22・P22e・P23 と各 H | main (2598c3fa)。PEEK の下回り (ppc_probe_copy8・ppc_trap_onfault_data)、診断の応答 lan_monitor.c (P22e で境目を単純化: PEEK は RAM の許可表 1 種で VA と PA を照らす)、ホストの道具 tools/ppc_lan_host.py (CI の検査 ppc-lan-p23 を登録) |
| P24a〜P24d・P24 と各 H | main (761ca15f)。gem_phy.c の DONE の link 観測、lan_log.c の統計の口、PPC の I/O の実体 lan_gem_io.c、診断の元 lan_diag.c、service lan_service.c (7 状態、件数の予算、印は LAN STATE と LAN OFF だけ)。新しい TU は PPC の像にまだ入らない (P26) |
| P5b・P14b・P17c・P12b と各 H | main (761ca15f)。QEMU の先行スモーク S1 で見つけた core の直し (bdf の形、半分の reset の bit、AN の判定、descriptor の同期)。4 段を入れた S1 で 7450・7455 とも TX と RX の実 frame が往復した (設計の「S1 の観測」) |
| P26・P25 | main (d50dfda0、7 本目、LAN の実装の到達)。LAN の TU 15 本が PPC の像に入り、起動が ESCC の後に log を登録・GMAC を quiesce・CLOCK の後に pm_lan_start・READY 以後を pm_lan_poll で回す。PM の QEMU の実測で到達点を確認 (設計の「7 本目の実測」) |
| P29・P31+P32・P33 と H29 | 実装・独立レビュー済 (2026-11-11、統合の枝 wt/ppc-lanint8 = 8 本目 = LAN の最後)。P29: kboot_run.py が LAN を持つ像で UDP の netdev とホストの Host を使い、保存・5 診断・PEEK・MEMORY を照合 (sandbox では SKIP LAN socket rc=3)、--lan-off。P31+P32 (最小、1 段にまとめた): kboot_accept が SKIP を 3 で返し LAN 無効を両 CPU で 1 回、kboot_all が 3 を SKIP と数える。P33: PPC_KBOOT_DESIGN に LAN の起動の追記 |
| P30・H30・H31・H32・H33 | やめた (2026-11-11、ユーザーの「過剰に防御してませんか？」を受けた PM 決定。故障の道筋は host の模型 H14・H17・H24 が持ち、QEMU で重ねない。設計 PPC_LAN_MARKS_DESIGN の末尾) |

## 3. 実装スケジュール (2026-11-11、fable 起案、PM 確認)

到達点: QEMU (7450・7455) で起動ログがホストに届き 5 診断が往復する。実機の前の段 (UniNorth の clock の alias、実機の PHY) は入れない ([DEFERRED_TESTS](../DEFERRED_TESTS.md))。段の表と受入は [PPC_LAN_DESIGN](../../decisions/ppc/PPC_LAN_DESIGN.md) §6・§7、P24/P25 の境目は [PPC_LAN_SERVICE_DESIGN](../../decisions/ppc/PPC_LAN_SERVICE_DESIGN.md)。P6・P7・P27・P28 は削除段 (設計 §6 の表の下)。

### 3.1 段の依存と担当 (Codex は同時 3 本、差分のレビューは全部 astra)

| 波 | 段 (ファイル) | 前提 | 担当 | 関門 |
|---|---|---|---|---|
| W1 | P24a gem_phy.c の DONE の link 観測 | main | terra | test_lan_p17 --mutate |
| W1 | P24b lan_log.c の統計の口 + lan_boot.h | main | terra | test_lan_p20 --mutate |
| W1 | P24c lan_gem_io.c + lan_boot.h の宣言 | main | terra | powerpc-elf-gcc で KB_FLAGS の compile、`nm -u` の未定義の一覧、check_compile の selected に登録 |
| W2 | P24d lan_diag.c + lan_boot.h の宣言 | main (W1 を待たない) | terra | H (製品 TU、CLOCK の採取口だけ模型)、変異は RX 区画の除外の削除 |
| W2 | S1 先行スモーク (取り込まない patch を ~/os32-tmp/run/ppc-lan-s1/ に) | P24a・P24c | terra が patch、PM が sandbox の外で QEMU | reset → MIF → PHY → ring → nonce 1 枚の TX と RX 1 枚を tools/ppc_lan_host.py --udp で見る |
| W2 | M1 設計の追記 (LAN 印の一覧、P29〜P32 の受入の形) | なし | astra 起草 → Opus のサブエージェント (往復 3 回まで) | PM が確定して設計に追記 |
| W3 | P24 lan_service.c + H24 | P24a・b・d (P24c は H24 で模型) | terra | H24 = service を製品の TU ごと (gem core・log・wire・monitor・diag) 組み、A8・A10・A11 の変異 |
| W4 | P26 build/ppc-kboot.mk → P25 init/ppc_kernel.c (同じ作業木・同じ実装者・この順) | 6 本目 + S1 の直し | terra、UDP つきの QEMU は PM | 像の link、link map (pool の収容・禁止 symbol)、既存 9 印、PM の実測 A7・A8・A10・A11 |
| W4 | P33 PPC_KBOOT_DESIGN.md | なし | terra または PM (文書だけ、H は作らない = 確定の R3) | check-docs-tree |
| W5 | P29 kboot_run.py + H29 | M1・P26 | terra、UDP の run は PM | 既定 sungem・LAN 無効の負経路・9 印と禁止語の維持 |
| W6 | P30 kboot_mutations.py → P31 kboot_accept.py → P32 kboot_all.py | 直前の段 | terra (直列)、UDP の run は PM | 各 H --mutate、P32 の後に kboot_all 全体 (PM、外) |

- lan_boot.h は P24b・P24c・P24d が 1〜3 行ずつ足す。依頼文で「自分の宣言だけを、既存の関連する宣言の直後に」と指定し、衝突は統合の枝で PM が解く (3 段を直列にしない)。
- P24 は宣言が設計で決まっているので、P24d のレビューが 1 回通った時点で着手してよい。P24c を待たない。
- H29 を fixture (保存済みの serial とホストの capture) で書けるなら、P29 は M1 の確定後に W4 と並べて前倒しする。
- sandbox は AF_INET を作れない。P29 以降は「sandbox の中は fixture の H と rc=3 の別行、UDP つきの実行は PM」の往復になり、1 往復に PM の実行 5〜10 分が乗る。
- P25 の挿入行は 5 本目が main に入った後の init/ppc_kernel.c で照らし直す (DESIGN.final の行番号はレビュー時点)。

### 3.2 取り込みのまとめ

| 本 | 中身 | 像 | 統合の検査 (~/os32-tmp/run/ppc-lan0/int5.sh の形に足す物) |
|---|---|---|---|
| 6 本目 | P24a・P24b・P24c・P24d・P24 と各 H、票と設計の追記 (DESIGN.final、M1 と S1 の観測。S1 で core の直しが出ればその段も) | x86 不変。PPC の像は P5b (像に入っている gmac_tree.c の bdf) で変わる。P14b・P17c・P12b の TU (gem_reset・gem_phy・gmac_dma・lan_gem_io) と P24 の新しい TU は P26 まで ppc-kboot.mk に入らない | (1) 新しい H と直した段の H の --mutate (2) 像が変わるので kboot_all (9 印) と kboot の変異を sandbox の外で、PPCKERN.map に lan_service・lan_diag・lan_log が無いこと (3) 新しい TU の powerpc-elf-gcc compile と `nm -u` (4) gcc の関門 (下) |
| 7 本目 = LAN の実装の到達 | P26 + P25 (同じ取り込み、D3)、QEMU の実測で出た直し、PM の実測の証拠 (起動ログがホストに届き 5 診断が往復、7450・7455) を票と設計の追記に。**これで本線は HAL へ進める** (ユーザー決定 2026-11-11: 7 本目を分ける) |
| 8 本目 = LAN の最後 | P29〜P32 (受入の自動化)、P33、票を「実装済」に、実機前の段を DEFERRED_TESTS に | x86 不変、PPC の像が変わる | (1) kboot_all を sandbox の外で 7450・7455 (2) ホストの保存と serial の一致、欠落数、5 診断の応答 (3) link map (pool・サイズ・禁止 symbol) (4) LAN 無効の負経路 (5) build_compare --all (差は images/os32_install.iso の 1 件だけを許す、いつもの既知の差) (6) gcc の関門 |

gcc 14 の再発防止 (6 本目から int*.sh に固定):
- `grep -n "'gcc'" tools/ppc/test_lan_p*.py tools/ppc/kboot_*.py` で、`-Werror` の付かない gcc 呼び出しを 0 件にする。5 本目で落ちた test_lan_p23.py の driver.c がこの形だった。
- 依頼文の完了条件に「H の中の host gcc は全部 `-Wall -Wextra -Werror`、できない所は `-Werror=incompatible-pointer-types -Werror=int-conversion -Werror=implicit-function-declaration -Werror=implicit-int` (gcc 13 が受ける名前だけ)」を入れる。
- 変異で「compile が落ちる = RED」を数える H は、期待の診断の文字列を gcc の版に依らない形 (`_Static_assert` の文言) に限る。
- 手元に gcc 14 を置くのは環境の変更なのでユーザーに確認してから。置けたら PATH の先頭に gcc → gcc-14 の shim を置いて、CI が回す H (tools/checks の登録分) だけ回す。

### 3.3 見込み (実績: 実装 15〜40 分、レビュー 5〜10 分、往復 1〜3 回 = 1 段 0.5〜2 時間)

| 区間 | 見込み | 根拠 |
|---|---|---|
| W1 (P24a・b・c) | 1〜2 時間 | 小さい段 3 本を並列 |
| W2 (P24d、S1、M1) | 2〜3 時間 | S1 は patch 30 分 + PM の実測 30〜60 分。M1 は起草 30 分 + レビュー 1〜2 回 |
| W3 (P24 + H24) | 3〜4 時間 | 240 行 + 3 受入分の H、往復 2〜3 回を見込む |
| 6 本目の統合と依頼 | 1 時間 | int5 は全体で 15 分弱 |
| **6 本目の合計** | **7〜9 時間 = 約 1 日** | S1 で core の誤りが出ると 1 件につき +1〜2 時間 |
| W4 (P26 + P25、PM の実測) = 7 本目 | 0.5〜1 日 | 実装 1〜1.5 時間。初めての実 boot で直し 1〜3 件 (各 1〜2 時間) を見込む |
| W5・W6 (P29 → P32、P33) = 8 本目 | 0.5〜1 日 | 直列 4 段、各段に PM の UDP の run が挟まる。P30 は変異ごとに QEMU を起動する |
| 7 本目・8 本目の統合と依頼 | 各 1〜2 時間 | kboot_all は今 143 秒、LAN の変異で数倍 (検査 1 本 30 分の枠に収める) |
| **7 本目 + 8 本目の合計** | **1.5〜2 日** | 本線は 7 本目 (6 本目の後 0.5〜1 日) で HAL へ進む |

- 縮める手は S1 を 6 本目の間に済ませることと、P29 を fixture で前倒しすること。両方やって 1.5 日。
- 7 本目の着手から 1 日たって W4 の実測が通っていなければ、本線の PM に見込みを 1 通送る。

### 3.4 危険と先回り

| 危険 (初めて出る所) | 先回り |
|---|---|
| GEM の core (P14〜P19、main) は host 模型だけで、QEMU の sungem にまだ触れていない。A7 の実 frame 往復は未実施 | S1 を W2 で回す。core の直しは 1 ファイル = 1 段で 6 本目に入れる |
| 実 MMU で初めて効く物: descriptor の区画の I/G の map、dcbf、alias 越しの LE アクセス。DSI は FATAL になり 9 印が壊れる | S1 の patch は pm_gmac_dma_init と ppc_dma_sync を製品のまま通す。P25 の実測の最初の 1 回は LAN 以外を変えずに serial だけ見る |
| 像の link: 未定義 symbol (64 bit の除算、libgcc 無し)、pool 192KiB + 整列で像の予約を超える、禁止 symbol | P24c・P24d・P24 の各段で powerpc-elf-gcc の compile と `nm -u` を完了条件に。S1 の link map で pool の収容を先に見る |
| UDP の netdev: ホストが bind する前の datagram は消える。動的 port の取り合い (7450・7455 の並走)。短い frame の扱い | M1 で「ホストの道具を先に bind → QEMU 起動」と port の決め方を固定。60 バイト未満の frame が通るかは S1 で実測し、通らないなら TX 側で 0 詰め |
| ホストの道具との往復: 起動初回の seq=0、ALIVE の行は 2 秒の窓の後に LAN で届く。meter が serial の ALIVE で止めるとホストの保存が欠ける | M1 で meter の終了条件を「ホストの保存に最後の印が届く、または期限」に。S1 で BEACON と REQ 1 個を手で往復 |
| 250us と tick ±5%: QEMU の時間はホストの負荷で揺れる。最後の idle は poll で回り続けホストの CPU を 1 つ使う | H24 で件数の予算 (8/8/4/1/1) を固定。QEMU の 250us は記録にとどめ (D2 と同じ扱い)、合否は tick/TB ±5% と件数。受入の run は Codex を止めて 1 本ずつ |
| kboot_run の禁止語 (FAIL・FATAL・UNSUPPORTED) と 9 印の重複検査は serial 全体に掛かる | LAN の行は `LAN STATE`・`LAN OFF code=<数値>` だけ。error の名前を文字で出さない (数値)。H29 に負経路を固定 |
| 断線・再接続は QEMU では作りにくい | QMP の set_link が PHY の BMSR に映るかを S1 で実測。映らなければ断線は H24 の模型だけで受け、設計の追記に書く |
| P29 の後は kboot_all が sandbox の中で回らない | M1 で「socket が作れない環境は LAN 無しで 9 印だけ + rc=3 の別行」を決め、実装者の完了条件を書き換える |

### 3.5 停止条件・ユーザーに上げる地点

- 仕様の分岐 (ユーザーへ): QEMU の実測で線上形式 PLN1・5 診断の意味・「届く分だけ + 欠落数」を変えないと通らないとき。tick ±5% が件数の予算の調整で収まらないとき。pool が像の予約に収まらず x86 と共有の定数に触れるとき。
- build_compare で x86 の生成物に差が出たら止める。X 分類になり、本線の PM の NP21/W 受入が要る (設計 §6)。
- 往復: 差分のレビュー・設計の追記とも 3 回で決着しなければユーザーへ ([ROLES §0](../agents/ROLES.md))。2 回目の指摘が「境目・検査を足す」なら、シングルタスク・呼び手の保証の前提に照らして PM が不採用で閉じる (P22 の教訓)。
- driver の中で閉じる直し (レジスタの順、詰め物、期限の値) は PM 決定で進め、設計の「実装で決めたこと」に追記する。
- 実機には触れない。clock の alias・早期の clock・実機の PHY は DEFERRED_TESTS に積むだけ ([D2])。
- 日程: 7 本目 (P25 + P26 と実測の証拠) と 8 本目 (P29〜P33、受入の自動化) に分ける (ユーザー決定 2026-11-11、fable の提案)。7 本目が着手から 1 日を超える見込みになったら本線の PM とユーザーに 1 通。
