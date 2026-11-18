# PowerPC・PowerMac の技術資料

> 状態: **現行 (2026-11-09)** — BSD を根拠とする事実の正典。未確認は [未] で示す。

この木は 32 ビット PowerPC と PowerMac のハードウェア・規約の事実を持つ。
主な調査対象は G4 の MPC7450/7455 系と PowerMac G4 QuickSilver。
OS32 がどの機能をどう使うかの設計決定は `docs/decisions/ppc/`、
作業の状態と受入は `docs/tasks/ppc/` が持つ。この木では実装完了を判定しない。

## 読む順

| 主題 | 文書 |
|---|---|
| CPU の MSR と例外 | [CPU の状態と例外](cpu-state.md) |
| BAT・セグメント・HPT | [32 ビット OEA MMU](cpu-mmu.md) |
| キャッシュ同期・メモリ順序 | [キャッシュ同期とメモリ順序](cpu-cache.md) |
| TB・DEC | [Time Base と Decrementer](cpu-time.md) |
| 呼出し・型・整列の確認範囲 | [32 ビット ABI](abi.md) |
| ELF の再配置 | [ELF32 の再配置](elf.md) |
| client interface とデバイスツリー | [Open Firmware](open-firmware.md) |
| 装置の発見・初期化と画面 | [PowerMac のハードウェア](powermac.md) |
| 資料だけで確認できる機能と限界 | [QEMU mac99 と実機](qemu-mac99.md) |

## 資料の扱い

BSD 系の実装・ヘッダ・man を正とする。下記のメーカー・規格資料を事実の確定の根拠に併用する。
食い違いは両方を記し、採用または [未] を明記する。
BSD の記述は日本語で要約し、数値は意味を確認して表にする。長い文章や実装構造を写さない。
QEMU は配布文書と help の出力だけを使う。QEMU・OpenBIOS・Linux 等の
GPL/LGPL ソースは読まない・探さない・利用しない。
手元に無い仕様書は取り寄せ依頼にし、記憶で規約を補わない。

根拠の表記は `<BSD>/<path>:<行>`。N=NetBSD、O=OpenBSD、F=FreeBSD。
各 path は以下の sparse clone のルートからの相対位置。行番号の基準はこの版である。

| 略号 | 手元のルート | 調査時の HEAD |
|---|---|---|
| N | `/home/hight/os32-tmp/ref/netbsd` | `36eddebcc36cb0775aa2962005b9c05419a6ff01` |
| O | `/home/hight/os32-tmp/ref/openbsd` | `b907757ada4db4a459aada3573abacab97184813` |
| F | `/home/hight/os32-tmp/ref/freebsd` | `6eae951cd51b5b8603c119f21c77871a8595ff88` |

[未] は資料不足・機種未確認、[推] は根拠からの推論を表す。
BSD 固有の実装選択を CPU の必須仕様や OS32 の採用決定に読み替えない。

### 追加資料の略号と版

根拠は `<略号> <節または頁>`。取得一覧はリポジトリ外の
`/home/hight/os32-tmp/ref/vendor/SOURCES.md`、検索用本文は同 `text/`。
取得日は依頼の管理日付 2026-11-09。公開文書だけを読み、実装ソースは追加取得しない。

| 略号 | 文書・版 | 取得元 |
|---|---|---|
| MPC7450UM | Motorola MPC7450 Family User's Manual Rev 3.1 (2004) | bitsavers の mirrorservice 鏡、components/motorola/PowerPC/ |
| PEM | Motorola Programming Environments Manual for 32-Bit Microprocessors Rev 2 (2001) | 同鏡 |
| MPC7455EC | Motorola MPC7455 Hardware Specifications Rev 4 (2003) | 同鏡 |
| OF-PCI | PCI Bus Binding Rev 2.1 | devicetree.org/open-firmware/bindings/pci/pci2_1.pdf |
| OF-PPC | PowerPC Processor Binding Rev 2.1 (1996-11-06) | public.dhe.ibm.com/rs6000/OpenFirmware/bindings/processor/ppc-2_1.html |
| APPLE-MDD | Apple PowerMac G4 Developer Note、2003 年 MDD (PowerMac3,6) | developer.apple.com/library/archive/、Macintosh_CPUs-G4/PowerMacG4/ |
| APPLE-MDD02 | Apple PowerMac G4 (Sept 2002) Developer Note、MDD (PowerMac3,6) | 同、PowerMacG4Sept02/ |
| QEMU-PM | QEMU 11.1.50 (master) PowerMac family boards 文書、取得時点 | www.qemu.org/docs/master/system/ppc/powermac.html |

MPC7450UM の数値・条件は **Rev 3.1 の値**。Rev 5 の訂正・CPU revision 別 errata は [未]。
MPC7455EC は電気・タイミング仕様の資料で、命令規約の代用にはしない。
Apple の 2 文書とも QuickSilver (PowerMac3,5) の資料ではない。
QuickSilver に外挿する箇所は必ず「MDD の資料による [推]」とする。
QEMU-PM はローカル 8.2.2 と版が違い、その構成の証明にはしない。

### ESCC の受信

段 3a の受信は Open Firmware が開いた ESCC チャネル A の設定を引き継ぎ、
カーネル起動ログと同じ CSR / data のマッピングを前景から使う。
RR0 に受信可能ビットが立ったら RR1、data の順に読み、FIFO 先頭に
対応するハードウェアの overrun、framing、parity を累積する。
エラー付きのバイトも返し、読み取ったエラーは WR0 の Error Reset で解除する。
ソフトウェア FIFO の overflow は 0。入力がないとき try は一度の RR0 読みで -1 を返す。
RR1 の読みは CSR の選択を完結させ、受信は送信データを書き換えない。
受信と通常の送信・drain は協調実行の前景で使う。

根拠は PPC KAPI 補完計画 §5 の port 行、[Zilog SCC/ESCC User Manual](https://zilog.com/docs/serial/UM0109.pdf)
の RR0・RR1・WR0、および NetBSD `sys/dev/ic/z8530sc.c:126-157`
の `zs_iflush` と `sys/dev/ic/z8530reg.h:139,397-404`。
`make check-ppc-kboot-log MUTATE=1` は FIFO と送信の共存をホストで検査する。
QEMU の RSH からの読戻しは、既知バイトを mac99 のチャネル A に送って
PortOps 経由で値と順序、空の try を照合できる port の組の公開の段 (PPC_KAPI_FILL_DESIGN §2 の port の行、道 B) で行う。
