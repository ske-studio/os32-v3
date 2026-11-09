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

BSD 系の実装・ヘッダ・man を基本資料とする。メーカー仕様の未入手部分は断定しない。
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
