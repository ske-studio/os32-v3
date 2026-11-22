> 出所: 本線の機械の ~/os32-tmp/run/ata-hal/DESIGN.final.md (設計ラリー、2026-11-20 確定)。
> 状態: **確定 (2026-11-20)** — ATA の block の口 (段 8)。本線の M1〜M3 は main 6e4b9eb46 に入った。PowerPC の後端 P1・P2 はこの設計の表に従う。末尾の「確定」の節は本文に優先する。

# 段 8「ATA の block 契約」詳細設計 r2 (本線の HAL の口)

> 状態: 改訂 r2 (2026-11-20)。基点 origin/main cc1260bd2。計画 docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md の提供者の分類・段 8・受入。
> 前提: POLICY_DEV §10「x86 に揃える」「シングルタスク・協調切替」「手本は正解として移す」、整数だけ、[HW1]〜[HW3]。参照は基点の実測。「新設」と書いた名前は今は無い。

## r1→r2 の変更

- P1-1　下の口は宣言・定義とも static inline とし、PC-98 のポート定数を独立ヘッダで共有する。
- P1-2　OF 離脱前に各 channel の物理番地と I/O 写像を確定し、離脱後に検出・登録する。
- P1-3　連続転送の引数をバイト列とし、mac99 の奇数番地で語幅と媒体順を検証する。
- P1-4　常駐 shell 所有者のゲスト試験入口と通常の sh で書き込みの成功・拒否を分けて測る。
- P2-1　IDENTIFY の待ちを発行前・発行後・Data 後に分け、channel と unit の不在を区別した。
- P2-2　4 回の settle を G4 で測定し、x86 の関数差は全件照合する。
- P2-3　像別の選択、段別の PPC コンパイル対象、移す変異 4 件を指定した。
- P2-4　D3 の成功・拒否を媒体と API ごとに定め、再マウントと番兵を受入に含めた。
- P3-1　生 ATA の公開 7 本、転送窓口 8 本、IDENTIFY と BIOS の幾何、§9 の前提を区別した。

## 1. 実物の確認と統合の判断

### 1-1 計画・材料の前提と実コードの食い違い (12 件)

| # | 前提 (計画・PowerPC の PM の材料) | 実物 | 設計への効き |
|---|---|---|---|
| F1 | 口の名は `include/ata_contract.h` | 既に `include/ide_contract.h` がある (IdeInfo・IDE_ERR_*・IDE_MAX_DRIVES、型とエラー定数)。`sdk/kapi.json の ide_contract.h の取り込み` と試験 約 30 本が読む | 新しい名を作らず `ide_contract.h` を広げる (x86 の名が正) |
| F2 | x86 の ide.c が IRQ 前提かもしれない | polling だけ。`irq_disable(PC98_IRQ_IDE)` (`drivers/ata/ide.c` の `ide_init`) と nIEN=1 (`ide_init`)、待ちは Alt Status の読みの回数 (`BSY・DRQ 待ち`) | polling の道を足す必要が無い。そのまま共通 |
| F3 | DMA は後端の能力として口に持つ | x86 の ATA に DMA は無い。全部 16 bit PIO (`drivers/ata/ide_pio.inc` の `ide_pio_command`)。BMIDE の実装は木に無い | 今回の口に DMA を入れない (§3 D3) |
| F4 | reset (SRST・シグネチャ) は x86 と同じ手順で共通に | ide.c に reset は無い。SRST は `drivers/ata/atapi.c` の `atapi_srst` (バンク 1・CD の部品) だけで、ide.c は `ide_multiple_reset_bank` (`drivers/ata/ide_multiple.inc` の `ide_multiple_reset_bank`) で知らせを受けるだけ | 口に reset を入れない |
| F5 | 待ちの単位は後端の時計 | 回数の定数 `IDE_TIMEOUT_LOOP 1000000`・`IDE_TIMEOUT_BSY 500000` (`drivers/ata/ide.h` の `IDE_TIMEOUT_LOOP・IDE_TIMEOUT_BSY`)。時計を読まない | 回数のまま共通 (§3 D5) |
| F6 | LBA48 は x86 の作りに合わせて | LBA28 まで (`IDE_LBA28_LIMIT`、`drivers/ata/ide_addr.h` の `IDE_LBA28_LIMIT`)。6.4 GB = 約 1,250 万セクタ < 2^28 | 追加なし |
| F7 | 生 ATA 7 本は「固定名窓口を共有」 | 窓口 `platform_ide_*` の宣言は共通 (`include/platform_device.h` の `platform_ide_init・platform_disk_geometry_get`) だが実体は機種の `platform/pc98/device_ide.c の 設計時の platform_ide_* 転送` で、装置のヘッダ `ide.h` を読む (`装置ヘッダの取り込み`) | 公開 7 本に内部 range_ok を加えた転送窓口 8 本と、別の IDENTIFY 由来の幾何取得を `kernel/dev/` へ移す (loop の手本 `kernel/dev/device_loop.c`) |
| F8 | block の口はこれから作る | block の口は既にある: `Device.blk_read/blk_write` (`kernel/dev/dev.h` の `Device`)、hd0-3 は `drivers/base/dev.c` の `hd_read・hd_write` が `ide_read_sectors` を直に呼ぶ。dev.c は `C_COMMON_EXEC` (`build/sources/common.mk` の `C_COMMON_EXEC`) で PowerPC の像にも入り、ide_* の参照は `--gc-sections` (`build/config.mk` の `LDFLAGS`) で落ちている | 上の口は作らない。切るのは下 (レジスタの窓) だけ |
| F9 | 物理差の置き場 | origin/wt/docs-x86-baseline の docs/decisions/ARCH_COMPAT_CONTRACT.md §9「機種の作りの違いの一覧」 | 文書段 M3 は当該節の取り込み後 |
| F10 | ext2 の媒体の口は 6c で作る | `include/ext2_medium_contract.h` は main に入っている。HD の提供者 `fs/ext2/ext2_medium_ide.c` は `build/platform/pc98.mk` の `C_PLATFORM` の持ち物で、`find_part` は BIOS 由来の幾何が無いと断る (`ide_find_part`) | D3 の課題 (§7 U2) |
| F11 | PowerMac は Data を 16 bit で読める | `platform/powermac/platform_io.h` の `inpw・outpw` の inpw/outpw は「使えない」の印。MMIO の読みは `ppc_load8` だけ (`arch/ppc/ppc_boot.h` の `ppc_load8`) | 16 bit の MMIO の読み書きは PowerPC の段で新設 |
| F12 | 装置 ide.c は機種を知らない | `pc98.h` を読み (`drivers/ata/ide.c` の `機種依存の宣言`)、番地は `drivers/ata/ide.h` の `IDE_DATA〜IDE_BANK1`、バンクの切替は `drivers/ata/ide.c` の `ide_select_bank`、`irq_disable` を extern で宣言 (`irq_disable の宣言`) | この 4 点だけが機種の物。ここを固定名のヘッダへ |

### 1-2 統合の判断 3 点 (POLICY_DEV §1)

1. 同じ役目の物: ある。ATA の手順は `drivers/ata/ide.c` と `.inc` 3 本・`ide_addr.c`、block の口は dev.c、窓口は `platform_ide_*`、ext2 の媒体は `ext2_medium_contract`。
2. 1 つの実装にするか: する。ATA の手順 (IDENTIFY・方式の選択・PIO・MULTIPLE・範囲の検査) は ide.c の 1 本を両方の像に選ぶ。機種に依るのは「レジスタの窓・バンクの選択・IRQ の止め方」だけで、既にある `io.h` → `platform_io.h` と同じ固定名のヘッダ (check_layers.md §8 の 3 行目) に分ける。PowerPC の側に ATA の手順の関数を書かない。
3. 既存の向き: 合う。polling・PIO・LBA28・回数の待ち・4 台 (2 × 2) は x86 のまま。QuickSilver の 2 channel (Ultra ATA/66・EIDE)、mac99 の IDE 2 本は `IDE_BANK_COUNT 2` (`include/ide_contract.h` の `IDE_DRIVES_PER_BANK`) にそのまま乗る。ARCH_COMPAT の行は §3 末。

## 2. 口の形

切る場所は 2 つ。上 (窓口) は純移動、下 (窓) は固定名のヘッダ。実行時の ops 表にはしない (理由は §3 D0)。

### 2-1 下の口: レジスタの窓 — `include/ide_bus.h` (新設、契約) と `platform/<機種>/platform_ide_bus.h` (新設、実装)

`io.h` (契約) と `platform_io.h` (実装) の対 (`include/io.h` の `platform_io.h` の取り込みと `outpw`) と同じ作り。関数は全部新設、`static inline`。

```c
/* include/ide_bus.h — include/types.h の u8・u32 の u8/u16/u32 を用いる。実装は -Iplatform/$(PLATFORM) が選ぶ platform_ide_bus.h */
#define IDE_REG_ERROR 1  /* W は FEATURES */
#define IDE_REG_SECT_CNT 2
#define IDE_REG_SECT_NUM 3
#define IDE_REG_CYL_LO 4
#define IDE_REG_CYL_HI 5
#define IDE_REG_DRV_HEAD 6
#define IDE_REG_STATUS 7  /* W は COMMAND */
#define IDE_BUS_OPEN8 0xFF   /* 無いバンクを読んだ値 */
static inline void ide_bus_init(void);            /* 装置の IRQ を止める・窓の用意。失敗しない */
static inline void ide_bus_select(int bank);      /* 以後の読み書きの相手 (0/1)。無いバンクも選べる */
static inline u8   ide_bus_in(int reg);           /* taskfile 1〜7 の 8 bit の読み。無いバンクは IDE_BUS_OPEN8 */
static inline void ide_bus_out(int reg, u8 v);    /* 同 書き。無いバンクへは何もしない */
static inline u8   ide_bus_alt(void);             /* Alt Status。無いバンクは IDE_BUS_OPEN8 */
static inline void ide_bus_ctl(u8 v);             /* Device Control */
static inline u16  ide_bus_data_word(void);       /* Data を 1 語、ATA の語の値で (IDENTIFY・読み捨て) */
static inline void ide_bus_data_read(void *dst, u32 words);        /* Data → メモリ。媒体のバイト順のまま */
static inline void ide_bus_data_write(const void *src, u32 words); /* メモリ → Data。同上 */
#include "platform_ide_bus.h"
```

- バイト順の決め: 共通は入れ替えをしない。「語の値」(`ide_bus_data_word`、IDENTIFY の語 1・49・60 などと文字列の `>> 8` の扱い `drivers/ata/ide.c` の `ide_identify`) と「バイトの並び」(`_read`・`_write`、セクタの中身) を口で分け、後端が吸収する。NetBSD の wdc の `bus_space_read_2` と `read_multi_stream_2` の分け方と同じ。x86 は LE なので 2 つは同じ命令になる。
- 状態の持ち主: ドライブの状態 (`drive_present`・`drive_info`・`drive_geom` `drivers/ata/ide.c` の `drive_present・drive_geom`、`drive_multiple` `drivers/ata/ide_multiple.inc` の `ide_multiple_state`) は今のまま ide.c。「今選んでいるバンク」は後端 (PC-98 は装置のレジスタ 0x432 そのもの、PowerMac は基点の番地の表と添字)。共通は I/O の番地を持たない。
- 機種に依らない定数 (状態のビット `drivers/ata/ide.h` の `IDE_ST_BSY・IDE_ST_ERR`、コマンド `ATA コマンド定数`、`IDE_NIEN`・`IDE_SRST`、待ちの回数、`IDE_SEL_SETTLE`) は ide.h に残す。番地 (`IDE_DATA`〜`IDE_DEV_CTRL`・`IDE_BANK0/1`、`drivers/ata/ide.h` の `IDE_DATA〜IDE_BANK1`) は `platform/pc98/platform_ide_ports.h`（新設、定数のみ）へ移す。atapi.c は番地を使い続けるので include を 1 行足す (CD は PC-98 だけ、`build/select.mk` の `FEATURE_KDEBUG`)。
- ide.h は `platform_ide_bus.h` を読まない (dev.c が ide.h を読み、PowerPC の像で今も組まれているため。F8)。読むのは ide.c だけ。

PC-98 の実装は契約ヘッダ経由で読む。ATAPI とホスト試験はポート定数ヘッダだけを直接読む。

```c
/* platform/pc98/platform_ide_bus.h */
#include "io.h"
#include "pc98.h"
#include "platform_ide_ports.h"
extern void irq_disable(unsigned int irq);            /* drivers/ata/ide.c の 機種依存の宣言 から移す */
_Static_assert(IDE_BUS_OPEN8 == PC98_IO_OPEN_BUS8, "ide open bus");
static inline void ide_bus_init(void)        { irq_disable(PC98_IRQ_IDE); }
static inline void ide_bus_select(int bank)  { outp(IDE_BANK1, (unsigned)(bank ? 0x01 : 0x00)); }
static inline u8   ide_bus_in(int reg)       { return (u8)inp(IDE_DATA + 2 * reg); }
static inline void ide_bus_out(int reg, u8 v){ outp(IDE_DATA + 2 * reg, (unsigned)v); }
static inline u8   ide_bus_alt(void)         { return (u8)inp(IDE_ALT_STATUS); }
static inline void ide_bus_ctl(u8 v)         { outp(IDE_DEV_CTRL, (unsigned)v); }
static inline u16  ide_bus_data_word(void)   { return (u16)inpw(IDE_DATA); }
/* 連続転送の I/O は drivers/ata/ide_pio.inc の ide_pio_command と同じく 1 語 1 回。 */
```

x86 の今の物との対応 (置き換えは式の差し替えだけ、順序・回数は変えない):

| 今 | 置き換え後 |
|---|---|
| `irq_disable(PC98_IRQ_IDE)` drivers/ata/ide.c の ide_init | `ide_bus_init()` |
| `ide_select_bank` の中の outp drivers/ata/ide.c の ide_select_bank | `ide_bus_select(bank)` (static 関数 `ide_select_bank` は残す) |
| `inp(IDE_ALT_STATUS)` drivers/ata/ide.c の ide_wait_bsy・ide_wait_drq・ide_select_drive・ide_init、drivers/ata/ide_pio.inc の ide_pio_settle・ide_pio_wait_phase、drivers/ata/ide_multiple.inc の ide_multiple_prepare | `ide_bus_alt()` |
| `probe == PC98_IO_OPEN_BUS8` drivers/ata/ide.c の ide_init | `probe == IDE_BUS_OPEN8` |
| `outp(IDE_DEV_CTRL, IDE_NIEN)` drivers/ata/ide.c の ide_init | `ide_bus_ctl(IDE_NIEN)` |
| `outp(IDE_DRV_HEAD/SECT_CNT/SECT_NUM/CYL_LO/CYL_HI, …)` drivers/ata/ide.c の ide_select_drive・ide_set_addr、drivers/ata/ide_multiple.inc の ide_multiple_prepare | `ide_bus_out(IDE_REG_*, …)` |
| `outp(IDE_COMMAND, …)` drivers/ata/ide.c の ide_identify、drivers/ata/ide_pio.inc の ide_pio_command、drivers/ata/ide_multiple.inc の ide_multiple_prepare | `ide_bus_out(IDE_REG_STATUS, …)` |
| `inp(IDE_STATUS)` drivers/ata/ide.c の ide_identify、drivers/ata/ide_pio.inc の ide_pio_finish | `ide_bus_in(IDE_REG_STATUS)` |
| `inpw(IDE_DATA)` drivers/ata/ide.c の ide_identify、drivers/ata/ide_pio.inc の ide_pio_command (読み捨て) | `ide_bus_data_word()` |
| セクタの for 文 drivers/ata/ide_pio.inc の ide_pio_command | `ide_bus_data_write(src, phase_words); src += 2 * phase_words;`・読みも同じ形。drivers/ata/ide_pio.inc の ide_pio_command のカーソルを u8 単位にする |

連続転送の words は 16 bit 語数。メモリには整列条件を課さない。例として媒体上の 12 A7 を読むと宛先の先頭は 12、次は A7。PowerPC は順序を保証した Data MMIO の 16 bit 読みを各語一度だけ行い、ATA 語値の下位・上位バイトを順にバイトポインタへ格納する。書きは逆に 2 バイトから語を作り、16 bit MMIO に一度書く。IDENTIFY は同じ ATA 語値を数値として返す。x86 もメモリ側は整列を仮定しない。

### 2-2 上の口: 窓口と block (新しい型は作らない)

- `include/ide_contract.h` に、共通が呼ぶ 8 本の宣言を ide.h から移す (宣言は 1 か所、`drivers/ata/ide.h` は `ide_contract.h` を取り込む): `ide_init` (`drivers/ata/ide.h` の `ide_init`)・`ide_drive_present` (`ide.h`)・`ide_identify` (`ide.h`)・`ide_read_sector`・`ide_write_sector`・`ide_write_sectors` (`ide.h`)・`ide_get_info` (`ide.h`)・`ide_range_ok` (`ide.h`)。型は今のまま。
- `platform/pc98/device_ide.c` → `kernel/dev/device_ide.c` (純移動、`#include "ide.h"` を `"ide_contract.h"` に)。`platform_disk_geometry_get` (`device_ide.c`) も一緒。
- block は今の `Device` (hd0-3、`drivers/base/dev.c` の `hd0_dev・hd3_dev`) と `dev_register_hdd` (`drivers/base/dev.c` の `dev_register_hdd`、宣言 `kernel/dev/dev.h` の `dev_register_hdd`)。PowerMac の起動は PC-98 の `platform/pc98/boot.c` の `pc98_boot_disk` と同じ 2 手 (`ide_init` → 在るドライブに `dev_register_hdd`) を呼ぶだけ。
- ext2 の媒体 (`ext2_medium_ops`) はこの口の上に乗る別の段 (6c D3)。この設計では触らない。
- `hdd_geom_info` (`platform/pc98/bootinfo_geom.c` の `hdd_geom_info`) は BIOS 由来なので PowerMac では不在のまま (計画の不在分類 の E)。

## 3. x86 との比べ

| # | 観点 | x86 (PC-98) の実コード | PowerMac (G4・mac99) | 口での扱い | 違いを残すか |
|---|---|---|---|---|---|
| D0 | 後端の差し替え方 | 機種は 1 つ、番地は定数 | 機種は 1 つ、基点は親 MacIO または PCI 資源から解決 | 固定名のヘッダ (`io.h` と同じ)。実行時の ops 表にすると x86 の ide.c の全関数が変わり、語ごとの間接呼び出しが増える。1 つの像に窓の種類は 1 つしかない (MacIO も Kauai も stride 0x10、`docs/ppc/ata.md の 3. レジスタ窓とタイミング`) | 残さない (同じ形) |
| D1 | 番地の写像 | I/O ポート 0x640+2n・0x74C、バンクは 0x432 に書いて 1 組の番地を共有 (`drivers/ata/ide.h` の `IDE_DATA〜IDE_BANK1`、`drivers/ata/ide.c` の `ide_select_bank`) | MMIO、基点 + 0x10 × n・+0x160、channel ごとに別の基点、eieio の順序 | `ide_bus_in/out/alt/ctl/select` の中 | 残す (物理: バスの種類と配線)。後端の中に閉じる |
| D2 | 割込みか polling か | polling。IRQ9 を止め nIEN=1 (`drivers/ata/ide.c` の `ide_init`) | 同じ polling。MPIC の source を許可しない、nIEN は共通が書く | `ide_bus_init` | 残さない |
| D3 | DMA か PIO か | PIO だけ (`drivers/ata/ide_pio.inc` の `ide_pio_command`) | 今回も PIO だけ。DBDMA は使わない | 口に無い | 残さない。[HW2] は PIO に関係しない。DMA を足す時は両方の arch で設計し直す (ARCH_COMPAT の行 2) |
| D4 | timing | 設定しない (BIOS の設定のまま) | 設定しない (OF の設定のまま。FreeBSD も既存の値を初期値に取る `docs/ppc/ata.md の 5. attach、reset、モード設定`) | 口に無い | 残さない |
| D5 | 待ちの単位 | 読みの回数 100 万・50 万 (`drivers/ata/ide.h` の `IDE_TIMEOUT_LOOP・IDE_TIMEOUT_BSY`) | 同じ回数。MMIO の読み 1 回の時間は未測 | 口に無い | 残さない。G4 で停止中のディスクの立ち上がりに足りるかは受入で測り、4 回の settle 区間も観測し、不足は測定値を PM へ報告する (§7 U3) |
| D6 | reset | ide.c はしない (F4) | しない | 口に無い | 残さない |
| D7 | 列挙 | 2 バンク × 2 台を総当たり、浮いたバスは 0xFF で飛ばす (`drivers/ata/ide.c` の `ide_init`) | 同じ総当たり。節が無い channel は後端が 0xFF を返す | `IDE_BUS_OPEN8` | 残さない |
| D8 | Data のバイト順 | LE。語の値とメモリの並びが同じ | BE。語の値は入れ替えて読み、セクタは入れ替えずに運ぶ | `ide_bus_data_word` と `_read/_write` の 2 種 | 残す (物理: CPU のバイト順)。行 1 |
| D9 | BIOS 由来の geom | あり (`bootinfo_hdd_geom` `platform/pc98/bootinfo_geom.c` の `bootinfo_hdd_geom`)。区画表の位置の根拠 | 無い | `hdd_geom_info` は不在、`bootinfo_part_geom` は呼ばない | 残す (物理: ファームウェアが違う)。行 3 |
| D10 | SASI 256 B の判定 | SPT=33 かつ heads 4/6/8 (`drivers/ata/ide.c` の `ide_identify`、NP21/W の HDI) | 同じ式が走る。対象媒体の IDENTIFY の SPT・heads と判定結果を記録する | 共通のまま | 残さない |
| D11 | LBA | LBA28 (`drivers/ata/ide_addr.h` の `IDE_LBA28_LIMIT`)、方式は IDENTIFY から (`drivers/ata/ide_addr.h` の `ide_addr_mode`) | 同じ | 共通のまま | 残さない |

ARCH_COMPAT_CONTRACT §9 に D1 のポート／MMIO 配線、D8 の語順、D9 の firmware 幾何を物理差として追記する（F9）。共通動作の根拠も併記する。
- AC-ATA-1: Data の口は「語の値」と「バイトの並び」の 2 種。共通は入れ替えない。試験 `ide-pio-host` の BE の贋物。
- AC-ATA-2: 両 arch とも PIO・polling・LBA28。DMA・timing・reset は口に無く、足す時は両 arch で設計する。
- AC-ATA-3: BIOS 由来の幾何は PC-98 だけ。PowerMac の `hdd_geom_info` は型ごとの不在。

## 4. 失敗の表 (原因 × 後始末)

共通の ide.c の出口は今のまま (変えない)。N1〜N4 は PowerMac の起動・公開・媒体受入に関わる出口。

| # | 処理 | 原因 | 戻り値 | 後始末・次の状態 | 再試行・根拠 |
|---|---|---|---|---|---|
| E1 | ide_init | バンクが無い (Alt = 0xFF) | そのバンクを飛ばす。found に数えない | present=0、geom.valid=0 のまま | 可 (KAPI の ide_init を呼び直す) |
| E2 | IDENTIFY 発行前 | BSY 50 万回枯渇（選んだ unit が 0xFF の場合も該当） | IDE_ERR_TIMEOUT | 未発行、以前の情報を保存 | 再検出可 |
| E3 | IDENTIFY 発行後 | BSY 100 万回枯渇、ERR、追加 DRQ 待ち 50 万回枯渇 | 内部 TIMEOUT / IO とも IDE_ERR_NO_DRIVE | 情報を保存、装置の完了状態は不定 | 次回も BSY から確認 |
| E3a | IDENTIFY Data 後 | Status 読み、続く BSY 待ちの枯渇 | 結果を捨て IDE_OK | 読んだ語で更新。init は成功台数に加算 | drivers/ata/ide.c の ide_identify |
| E4 | init 時の MULTIPLE 準備 | 選択後 BSY または準備エラー | 台数はそのまま | その bank の準備を打ち切り、次の bank へ。最後は drive 0 を選ぶ | drivers/ata/ide.c の ide_init |
| E5 | 転送入口 | 不在／単発の方式なし | IDE_ERR_NO_DRIVE | 発行なし。複数要求で方式不明なら全域検査で RANGE | drivers/ata/ide_request.inc の ide_request・ide_read_sector・ide_write_sector |
| E6 | アドレス | 全域外、単発アドレス生成失敗 | IDE_ERR_RANGE または生成元のエラー | 範囲外の要求を発行しない。有効ドライブの複数要求 count=0 は成功 | drivers/ata/ide_addr.c の ide_addr_range_ok・ide_addr_make |
| E7 | PIO 発行前 | BSY 枯渇／準備時 BSY・DRQ | TIMEOUT／IO | 読書きコマンド未発行。準備前なら pending 保存。C6 発行時は active,pending をクリアし、失敗後も BSY/DRQ なら元のエラー、解消なら単発へフォールバック | drivers/ata/ide_multiple.inc の ide_multiple_prepare |
| E8 | 各 DRQ 相 | BSY または DRQ 待ち枯渇／安定状態で ERR・DF | TIMEOUT／IO | 読みの ERR+DRQ のみその塊を全語 drain して settle。読み先は未定義、書きは一部変更の可能性 | drivers/ata/ide_pio.inc の ide_pio_wait_phase・ide_pio_command |
| E9 | 最終 Status | ERR・DF／BSY または DRQ が消えず枯渇 | IO／TIMEOUT | 最初の失敗を返す。途中の書きの巻戻し、自動再試行なし | drivers/ata/ide_pio.inc の ide_pio_finish・ide_pio_command |
| E10 | 情報と範囲照会 | 不在・無効幾何・範囲外 | present と range_ok は 0、情報なしは IDE_ERR_NO_DRIVE | 成功時のみ幾何をコピー。転送窓口は下の結果を返し、void の init は台数を捨てる | drivers/ata/ide.c の ide_drive_present・ide_get_info・ide_range_ok、kernel/dev/device_ide.c の platform_ide_init・platform_disk_geometry_get |
| N1 | OF 探索と写像 | 節・親資源・reg 不明、窓範囲不正または写像失敗 | 当該 channel のみ無効 | 他方は独立して解決し起動続行 | 次の起動 |
| N2 | 無効 channel | 選択・アクセス | Status、Alt と他の 8 bit 読みは 0xFF、Data 語は 0xFFFF、連続読みは全バイト 0xFF | 全書きは無操作、MMIO を発行しない。接続済み channel の空 unit は実 Status により E2/E3 | 再検出可 |
| N3 | 公開書き | 権限拒否／許可後の引数検査 | 拒否は -1、許可された複数の count=0 は 0、不正な利用者範囲は fault、媒体範囲外は RANGE | 拒否時は媒体不変 | kapi/kapi_generated.c の wrap_ide_write_sector・wrap_ide_write_sectors |
| N4 | D3 区画探索 | 探索失敗／BIOS 由来の幾何なし | 探索のエラー／EXT2_ERR_INVAL | formatter が書く前に返す | fs/ext2/ext2_medium_ide.c の ide_find_part |

資源: 割り当て・確保は無い (PowerMac の MMIO の写像は起動で 1 回、解放しない)。排他は要らない (協調切替、IRQ から呼ばない)。

## 5. 部品の大きさ・x86 の build_compare の見込み

| 物 | 見込み |
|---|---|
| `include/ide_bus.h` (新設) | 約 35 行 |
| `platform/pc98/platform_ide_bus.h` (新設) | 定数専用の platform_ide_ports.h と合わせて約 45 行（番地 16 行は ide.h から移動） |
| `drivers/ata/ide.c` と .inc 3 本 | 差し替え 約 30 か所、行数はほぼ同じ。ide.h は 16 行減る |
| `kernel/dev/device_ide.c` | 55 行の移動 |
| x86 の像の大きさ | 0 バイトの見込み |
| build_compare (M1) | 関数差 0、申告は `device_ide.c` の置き場の移動だけ (リンクの順は `build/sources/order.mk` の `C_ORDER_LEGACY` の同じ位置に置く) |
| build_compare (M2) | 関数差 0 の見込み (inline が定数の番地に畳まれる)。差が出た全関数の逆アセンブルとホスト I/O 記録で番地・幅・順序・回数を照合する。説明できない差は M2 で直す。生成物の差は申告し NP21/W で受入する。 |
| PowerPC の像 | ide.c・ide_addr.c・device_ide.c で数 KiB の見込み (未測。P1 で測って申告) |

## 6. ファイルごとの変更と段

### 6-1 段と前提

| 段 | 担当 | 中身 | 前提 | 完了の印 |
|---|---|---|---|---|
| M1 | 本線 | 窓口の移動と宣言の移し (§2-2)。像の機能 `IDE` を足す (LOOP の作り `build/select.mk` の `IMAGE_FEATURES`・`build/sources/common.mk` の `C_COMMON_LOOP` に倣う。`C_COMMON_IDE = kernel/dev/device_ide.c drivers/ata/ide.c drivers/ata/ide_addr.c`、PC-98 の像は 1、PowerMac は 0 のまま) | なし | x86 関数差 0、check-layers 両像 0、docs/SRC_MOVE.md |
| M2 | 本線 | 窓の切り出し (§2-1)。ide.c から `io.h`・`pc98.h`・extern を外す。atapi.c に include 1 行 | M1 | x86 関数差 0 (§5)、ide-pio-host と変異が緑 |
| M3 | 本線 | 試験の BE の贋物 (§6-4)、ARCH_COMPAT の行、docs/design/drivers/ata/ の追従 | M2、x86-baseline §9 の取り込み | 新しい変異が赤になる |
| P1 | PowerPC | `platform/powermac/platform_ide_bus.h` (新設)、16 bit の MMIO の読み書き (新設、arch/ppc)、`ide_tree.c` (新設、OF の節 → 基点。手本 `platform/powermac/via_tree.c` の `pm_via_capture`)、`ide_boot.c` (新設、手本 `loop_boot.c`)、sh・link-sh の IDE を 1 に | M2 (M3 と並行可) | QEMU mac99 で hd0 の IDENTIFY と読み |
| P2 | PowerPC | 公開表: `sdk/kapi_target_ppc.py` の `PROVIDER_GROUPS` の `ide` の組から 7 本を出し、`hdd_geom_info` は不在の組に残す。書きの往復 | P1 | 計画の ATA 受入 の受入 |
| 6c D3 | 本線 → PowerPC | ext2 の formatter の選択・HD の提供者の登録・ext2 2 本の公開 | P2、6c D1 | §7 U2 の全組合せ |
| G4 | PowerPC + ユーザー | 実機。読みが先。書きは HDD の中身を確かめてユーザー承認の後 ([D2]) | P2 (QEMU の受入) | 実機の記録 |

M1・M2 は同じ ide.c・ide.h を触るので直列。PowerPC の段は本線の M2 の取り込みの後。

**各像の選択とコンパイル。** build/select.mk の IMAGE_FEATURES の IMAGE_FEATURES に IDE を加え、build/images/ の pc98.mk は 1。kboot-core.mk、kboot-link.mk、kboot-shell.mk、kboot-sh.mk、kboot-link-sh.mk は M1/M2/M3 で各 0。P1 で後二者だけ 1 にする。ppc-flags.mk はフラグの共通片。M1 から移動した device_ide.c と機種非依存の ide_addr.c を PPC の compile-only に明示する。M2/M3 の ide.c は PPC 後端待ちで x86 とホストで組む。P1 から有効像で 3 本と機種の起動・写像を実リンクし、無効像は共通ファイルのコンパイル検査を維持する。common-compile の入力は build/targets/ppc-powermac.mk の KB_PENDING_OBJ・common-compile と build/sources/common.mk の C_COMMON_COMPILE_ONLY_PPC。P2 の公開も有効像に限り、無効像の型別不在を試す。

**P1 の起動順。** platform/powermac/kernel_boot.c の ppc_kernel_start の ofw_detach より前に、機種側で channel ごとに親バスを辿る。MacIO は親 PCI メモリ資源を CPU 物理番地へ変換し、子 reg の taskfile offset を加える。Kauai は該当 PCI function のメモリ BAR 資源を変換して +0x2000。セル数・資源長・加算の収まりを確かめ、Data から +0x160 の Alt/Control までを含むページをキャッシュ禁止・guarded の I/O 属性で写像する。ファームウェア側で必要な写像と、OS の MMU へ切り替えた後も有効な I/O 写像の両方を整え、後端には後者の基点を保存する。既存の ofw_map_io(int mmu, unsigned ac, cell pa) は ESCC 専用の長さ 0x20 のため流用時は長さを分離する（platform/powermac/ofw.c の ofw_map_io、platform/powermac/platform_of_layout.h の PM_ESCC_WINDOW・PM_MAP_MODE_IO、platform/powermac/mmu_start.c の pm_mmu_start）。成功した窓だけを有効にする。離脱後、装置登録の準備が整ってから ide_init、present の各 drive に dev_register_hdd(int drive) の順。後端から OF を再呼出ししない。配線の出所は docs/ppc/ata.md の 1. 機種と配線・2. OF と PCI での識別・3. レジスタ窓とタイミング の Apple Developer Note と版固定 BSD 資料。

### 6-2 ファイルごとの変更

| ファイル | 段 | 変更 |
|---|---|---|
| `include/ide_contract.h` | M1 | 8 本の宣言を足す |
| `drivers/ata/ide.h` | M1・M2 | 8 本の宣言を消す (M1)。番地 `IDE_DATA から IDE_BANK1` を消す (M2) |
| `platform/pc98/device_ide.c` → `kernel/dev/device_ide.c` | M1 | 移動、include の差し替え |
| `build/sources/common.mk`・`build/sources/order.mk` の `C_ORDER_LEGACY`・`build/platform/pc98.mk`・`build/features/pc98-devices.mk` の `C_FEATURES`・`build/select.mk` の `IMAGE_FEATURES` | M1 | 置き場と選択 |
| `include/ide_bus.h`・`platform/pc98/platform_ide_bus.h` | M2 | 新設 |
| `drivers/ata/ide.c`・`ide_pio.inc`・`ide_multiple.inc` | M2 | §2-1 の表の差し替え (`ide_request.inc` は I/O を持たないので触らない) |
| `drivers/ata/atapi.c` | M2 | `#include "platform_ide_ports.h"` を 1 行 |
| `platform/powermac/*`・`arch/ppc/*`・`build/platform/powermac.mk` | P1 | §6-1 |
| `sdk/kapi_target_ppc.py`・`tools/ppc/test_kapi_binding.py` の `EXPECTED_ABSENT_SLOTS` | P2 | 公開表と期待 |

### 6-3 触る既存の検査と固定 (名前の grep の結果)

| 変える名前 | 当たる検査・固定 | する事 |
|---|---|---|
| `device_ide` (置き場) | `tools/tests/test_dep_dir_a_l05a.py` の `OLD・NEW` (変異の目印 `return ide_read_sector(drv, lba, buf);`)・`_l05b`・`_l06`・`_l08`・`_l09`・`_l10`、`test_ci_stab.py`、`fixtures/build_profile_x86.json`、`fixtures/dep_dir_a/l05a.json`、`docs/design/platform/pc98/README.md` | 道の文字列を新しい置き場へ。変異の目印の行は変わらない |
| `drivers/ata/ide.c`・`ide.h` (中身) | `test_ide_pio.py` (変異の目印 の PIO コマンド送信・完了待ち は `ide_pio.inc` の文。M2 で後述の 4 件を移す)・`ide_pio_host.c`・`ide_hostshim/io.h`、`test_cd_read.py の IDE ポートの試験`・`cd_read_host.c` (番地 `IDE_*` を使う)、`test_hdd_stage1.py`、`test_src_layout.py`、`test_bootinfo.py`、module の試験 約 10 本 (`tools/tests/test_fm_module.py` の `product_image`・`tools/tests/test_cd_module.py` の `product_image` ほかは名前の一覧)、`tools/prototypes/cd_imports.def`、`build/cd-module.mk`、`tools/checks/` の host 試験 7 本の include の道 | 贋物の io.h はそのまま使える (PC-98 の実装が io.h を読む)。include の道に `platform/pc98` が入っているかを 1 本ずつ確かめる |
| `IDE_DATA`・`IDE_ALT_STATUS`・`IDE_DEV_CTRL`・`IDE_BANK1` (置き場) | `ide_pio_host.c`、`cd_read_host.c`、`test_cd_read.py`、`cd_read_tdd.md`、`docs/05_drivers.md` | 定義の置き場の記述を直す。値は同じ |
| `ide_contract.h` (宣言が増える) | 贋物を持つ host 試験 (`ext2_*_host.c`・`tools/tests/disk_auth_host.c` の `platform_ide_write_sector・platform_ide_write_sectors`・`tools/tests/kapi_ranges_host.c` の `platform_ide_write_sector・platform_ide_write_sectors`・`dep_dir_d3_*_host.c`・`tools/tests/hdd_pt_fake.h` の `platform_ide_range_ok`) | 贋物の型が宣言と合うかを組んで確かめる |
| `PC98_IRQ_IDE`・`PC98_IO_OPEN_BUS8` | 検査からの参照なし | — |
| static の関数名 (`ide_wait_bsy` ほか) | `docs/design/drivers/ata/ide_pio.md` だけ | 名前は変えない |
| 公開表 (P2) | `tools/ppc/test_kapi_binding.py` の `EXPECTED_ABSENT_SLOTS`・`test_kapi_table.py`、`sdk/kapi_target_ppc.py` の `ZERO_SLOTS` (`ide_drive_present` の 0 を返す組) | 7 本を在る側へ |
| 層 | `tools/layers_baseline.json` (両像 0 のまま)、`ppc-compile` (common-compile) | M1 から両像で 0 |

### 6-4 受入 → 試験と変異

| 受入 | 試験 | 変異 (赤になること) |
|---|---|---|
| x86 の I/O の列が変わらない | 既存 `ide-pio-host` (贋物の io.h で番地・順・回数を見る) と build_compare | M2 で settle・read-drain・multiple-opcode・multiple-drain の各目印を対応する Alt 読み・語読み捨て・コマンド選択・塊全体の drain に移す。各目印は一意、新ヘッダを CHECK.covers と変異用コピーに含め、通常成功と各変異の実行時 RED を確認する |
| バイト順の約束 (AC-ATA-1) | `ide-pio-host` に BE の贋物の `platform_ide_bus.h` を足す (M3、新設): 語の値を入れ替えて返し、セクタは並びのまま。IDENTIFY の total・model と、読んだセクタのバイト列を照合 | 贋物の `_read` を語の値で詰める / 共通が `data_word` でセクタを読む |
| 無いバンク | 同じ贋物で基点なし → `ide_init()==0`、実 MMIO への書きが 0 回。片側だけ有効な両配置も試し、有効側の番号・登録台数と無効側の Status・Alt・Data・書き無操作を照合 | 0xFF の比べを外す |
| PowerMac の読み (P1) | QEMU mac99 に既知の像を `-drive` で付け、IDENTIFY の総数と LBA 0・中ほど・末尾のセクタを照合。不在の像は present=0 | 計画の ATA 受入「先頭 LBA を 1 ずらし隣の番兵で見つける」 |
| PowerMac の書き (P2) | mac99 の使い捨て媒体を IDENTIFY で識別。受入専用像のゲスト起動処理に、appslot_init(void) 後、子 sh の起動前の試験入口を設ける。常駐 shell の slot が RUNNING、非 GUI・非 CPL3、現在 slot と資源 owner が APP_ID_SHELL、syscall・WM・sink の外である通常の初期状態から公開 KAPI 表を直接呼ぶ（exec/appslot.c の appslot_init、exec/exec.c の exec_disk_write_allowed）。権限フラグの書き換えやドライバ直呼びはしない。公開単発・複数書きを奇数番地の非対称パターンで往復し、奇数番地への読み、ホスト媒体と前後番兵を照合。後端逆アセンブルで Data は各語 1 回の 16 bit と確認。通常の非許可パスの子プログラムから同じ要求を出し -1 と媒体全体の不変を確認。許可入口の範囲外拒否も別に照合 | 語の入れ替えを書きだけに入れる |
| G4 | 読みだけの IDENTIFY と LBA 0 の採取 → ユーザーが中身を確かめる → 承認の後に書き ([D2]) | — |

## 7. PM 決定

| 項目 | 実装と受入 |
|---|---|
| U1 | x86 と同じ polling、16 bit PIO、回数待ち。DMA・timing・reset・時計は今回の口に入れない。 |
| U2 | D3 は独立した使い捨て媒体で、loop の ext2_format と ext2_format_at がともに成功、ATA の ext2_format_at が成功。成功ごとに再マウントし、指定範囲の外の前後番兵（loop 全域の場合は backing 領域の外）を比較する。loop の探索は全域を返す（fs/ext2/ext2_medium_loop.c の ext2_medium_loop_ops）。ATA の ext2_format は最初の書き込み前に拒否し、書き回数 0 と媒体全体の一致を確認する。 |
| U3 | IDE_SEL_SETTLE の Alt Status 4 回（drivers/ata/ide.h の IDE_SEL_SETTLE）を含め、回数・順序は共通。G4 は選択・コマンド・各 DRQ 相後の settle 区間と停止ディスク起動時の BSY/DRQ の経過を測定する。4 回が G4 でも 400 ns になるとは仮定しない。不足は対象、区間、実測値を添え PM に戻す。 |

現行範囲の設計判断は確定。ATA 区画自動探索の成功を追加する場合、または G4 の測定を受け x86 の待ち方式の変更を提案する場合はユーザー判断。実機 HDD への書き込みは対象の確認後に個別承認を受ける。

反論 0 件。

## 確定 (2026-11-20 夕、sol 第 2 回、PM が反映)
以下は本文に優先する。
- **P1 (§6-1 P1・§4 N1)**: OF の離脱の前は、channel ごとの番地の解決と `ppc_boot_alias_plan()` (platform/powermac/kernel_boot.c の ppc_boot_load_root 付近) まで。OS の側の写像 (I/O alias の設置と検証) は離脱の後の `ppc_boot_load_root()` の流れで行い (platform/powermac/kernel_boot.c の ppc_kernel_start の OF の離脱の後)、成功した channel だけを `ide_init()` に渡す。mmu_start.c の経路は対象の像の起動の道ではないので引かない。
- **P1 (§6-4 P2・§7 U2)**: P2 と D3 の書き込みの許可の入口は `pm_exec_boot()` の戻りの後、`pm_shell_boot()` の前に固定する (platform/powermac/ppc_kernel.c の platform_boot_run、公開表の初期化 exec/exec.c の exec_kapi_init の後)。ATA の `ext2_format` はこの許可の入口から呼び、区画の探索の側の拒否を確かめる (両 formatter とも同じ権限の検査、sdk/kapi.json の ext2_format・ext2_format_at)。
- **P2 (§6-1 P2・§6-2)**: `sdk/gen_kapi.py` に `--feature-link` と同じ形の `--feature-ide` を足し、build/targets/ppc-powermac.mk の KAPI 生成規則へ渡す。IDE が有効な像だけで生 ATA の 7 本を束縛し、IDE=0 の EXEC 像には実体への参照を出さない。`hdd_geom_info` の不在は両方の像で確かめる。
- **P2 (§3 D9・§4 N4・§7 U2)**: D3 で PowerMac の ATA の `find_part` (fs/ext2/ext2_medium_ide.c の ide_find_part) は区画の探索 (fs/ext2/ext2_super.c の ext2_find_partition_ex の bootinfo_part_geom を呼ぶ道) の前に `EXT2_ERR_INVAL` を返す。`ext2_format_at` と loop は既存の経路のまま。
