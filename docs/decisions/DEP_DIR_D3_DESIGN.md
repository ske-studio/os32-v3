# DEP_DIR_D3 — 起動の配線・root の選び方・装置名の解決を platform へ — 設計

> 状態: **現行 (2026-10-26)** — 確定 (astra 起草・Opus 1 往復 P1 なし・PM が F1〜F11 を反映)。段の状態は票 TASK_DEP_DIR が持つ。
> 出所: ~/os32-tmp/run/depdir-d3/DESIGN.final.md を 2026-10-26 に写した。末尾の「確定」が本文の該当の行より優先する。


## 1. 実物の確認
- 調査実物: `/home/hight/os32-v3` main `83c785b4`。実装基点は **D1・D2 取り込み後**の main（SHA とサイズを開始時に記録）。SURVEY 基点は `d0f5b0e8`。
- 対象行の食い違い **0 件**。以下は照合結果。未実装の D1・D2 を現物にあるとは扱わない。

|SURVEY|main の file:line と確認内容|
|---|---|
|#1–3|`kernel/kernel.c:304,308,479` kbd/mouse/kcg、`:269` DMA、`:313,316,333` FDC IRQ/init/診断。位置・内容とも一致|
|#4–7|同 `:362–368` DA/UA と 3mode、`:398–405` IDE、`:373` loop、`:375–378` FS 登録。一致|
|#8–12|同 `:435` root、`:452` HostDrv、`:466` RTC、`:393,539` PCI、`:620` LGY98。一致|
|#13–15|`kernel/bootinfo.c:121,142,162,195` IDE、`kernel/ledger_pci.c:33` PCI、`kernel/dma_pool.h:30` と `kernel/dma_pool_math.c:99` DMA。一致|
|#31–32|`fs/vfs.c:327` 名前解決、`:11,495` ide_limits/IDE_MAX_DRIVES。一致|

- SURVEY の短い説明を実装条件に具体化: root は FD「種別」判定でなく **BOOT_DRIVE_FDD/FDD_144 との完全一致**。`:673–674` の子マウントにも同じ判定がある。
- FDC 媒体指定は全起動で `fdc_set_media_by_daua(0, boot_drive)`。144 表示だけ TYPE_MASK と ALT を使う。root 判定と統一しない。
- 名前解決は hd/fd/cd の数字 0–3 の**後続文字を無視**、COM は先頭 3 文字で常に ID=1、hostdrv は完全一致。装置の存在確認とは別。
- `platform/pc98/` は現在 `platform_io.h` のみ。D1 の tick_order と D2 の d2_bind は先行設計の境目として扱う。
- D1: FDC が irq_register、kernel の IRQ11 enable は削除済みになる。D2: kstack_hw_init 直後に pc98_d2_bind、入力/出力/時計表はそのまま使う。

## 2. 範囲と所有
- #1–14・31・32 を実施。**#15 は D3 に含めない**: プール割当と 8237 の純粋演算の共通化は起動順/名前表から独立し、既存 DMA host のリンク・変異対象も変わる。現状の 16MB/64KB 制約を維持し、後段の独立票で扱う。
- カーネル→具体ドライバだけを外す。platform→ドライバ、ドライバ→カーネルの依存は許す。新しい動的探索、優先度、依存グラフ、遅延初期化は作らない。
- platform の静的 const 表を一度登録。状態は元の所有者（kbd/mouse/FDC/IDE/PCI、VFS、bootinfo、台帳）に残す。コピーするのは起動引数と root の選択結果のみ。
- D2 の kernel.c「D3 までの起動配線」例外は移した対象ヘッダについて削除。後段対象の PIC/PIT・表示/復旧等を理由なく一緒に移さない。KAPI 番号・公開構造体・ローダ wire ABI は変更しない。

## 3. 起動の登録一覧と呼ぶ境目
```c
/* kernel/boot_platform.h: 内部専用。列挙値は下表の境目に1個ずつ。 */
struct boot_context { u32 drive; const char *root_dev, *root_fs; };
typedef void (*boot_step_fn)(const struct boot_context *);
struct boot_platform { boot_step_fn step[BOOT_POINT_COUNT]; };
void boot_platform_register(const struct boot_platform *ops);
void boot_platform_run(enum boot_point point, const struct boot_context *ctx);
/* boot_platform.c が const 表ポインタを所有。表本体は platform 常駐。 */
/* kernel_main: D2 bind の直後、装置を動かす前。boot は自動変数。 */
struct boot_context boot;
pc98_boot_bind(&boot, boot_drive); /* 表登録＋root選択だけ。I/O・確保・ログなし */
/* 例: sysclk_detect の直後、pit_init の直前だけ */
boot_platform_run(BOOT_DMA, &boot);
```
- `pc98_boot_bind` は platform の入口（汎用カーネルは具体ドライバヘッダを読まない）。D2 bind は既存位置・名称を維持し、D3 bind を直後に加える。時刻表は RTC init 前でも従来どおり未初期化状態を扱う。
- 登録は起動時・IRQ 公開前、同じ const 表の再登録のみ許可。run は登録済み・全必須スロット有効を前提に bind の host 試験で検証。装置不在は各 callback が従来どおり扱う。稼働中の差し替え/解除はない。
- run は **IRQ handler 内ではない**。IF を共通 dispatcher で変えない。DMA は `_enable` 前、kbd 以後は現行の IF を引き継ぐ。FDC wrapper 内の `_enable()` を残し、IRQ11 は D1 の FDC 登録に任せる。
- 表の添字順に一括実行しない。kernel.c の元の各位置で一つずつ run し、その間の処理を残す。

|境目（表のスロット）|wrapper が持つ処理／前後関係|
|---|---|
|DMA|sysclk_detect → dma8237_init → PIT。DMA ログ位置を固定|
|INPUT|CPU 校正/表示 → KBD.../kbd_init/OK → mouse_init → FDC|
|FLOPPY|FDC 表示、enable、fdc_init、成功/失敗表示、診断行、媒体指定、144 表示。dev_init より前|
|DEV_FS|DEV... → dev_init → loop_dev_init → path_init → ext2/fatfs/hostdrvfs/serialfs の全 init → validator → OK/NG。最初の失敗でも後続 FS 登録を実行|
|PCI_ENUM|palette_init → pci_init → IDE（PCI ログを IDE より前に維持）|
|DISK|IDE 表示/init → 0..IDE_MAX_DRIVES-1 の present だけ HDD 登録 → OK/no drive|
|AUTO_MOUNT|heap 初期化 → kernel の root mount/表示 → HostDrv detect と /host mount/表示 → RTC|
|CLOCK|RTC.../rtc_init/OK → paging_init|
|FONT|paging_reclaim_conventional → kcg_init → FPU 設定。kcg_module_load は元の位置|
|PCI_RESOURCES|memory_boot_init 後、ROOT_READY/unicode 後 → BAR 取込＋ledger ログ＋line hook → LAN load/fallback|
|NIC_LEGACY|snd module のログ後 → lgy98_init → IRQ_EN/enable/services_boot|

- 表の各要素は platform/pc98/boot.c の小 wrapper。FS 初期化関数は既存 VFS 登録口をそのまま使い、二重の FS レジストリを作らない。
- 診断文字列・色・座標・改行・失敗時の継続を丸ごと移す。tvram_print と tv_cat 系は内部 boot_display.c/.h に移して共用し、表示コードを複製しない。
- root の `vfs_mount("/", boot.root_dev, boot.root_fs)` と panic/halt は kernel に残す。HostDrv は root 成功後だけ呼び、検出なし/HDRV --、mount 失敗/HDRV NG と継続を維持。
- CD load → kcg_module_load → 子マウント → ROOT_READY、kdebug_load/自己試験/exec の位置は動かさない。子マウントは引数を root 名へ変更し二重の FD 判定を除去、CD 判定は §5 の解決結果を使用（FS 試行順は維持）。

## 4. 起動元・幾何・PCI 資源の境目
```c
/* platform/pc98/boot.c: ローダ引数を保存。bootinfo source の代用にしない。 */
ctx->drive = drive;
int fd = drive == BOOT_DRIVE_FDD || drive == BOOT_DRIVE_FDD_144;
ctx->root_dev = fd ? "fd0" : "hd0";
ctx->root_fs  = fd ? "fat" : "ext2";
/* FLOPPY wrapper: unit 0 を変えない。失敗しても継続 */
fdc_set_media_by_daua(0, ctx->drive);
/* 既存の TYPE_MASK/ALT 判定、inp(FDC_IO_3MODE)、144/M/? 表示もこの側 */
```
- boot_context は kernel_main の生存期間全体で有効、他部品は保持しない。bootinfo の BIOS 写し `s_bootinfo` は引き続き bootinfo.c 所有。破損/欠落時も root を勝手に変えない。
```c
/* include/bootinfo.h に内部口を追記。HddGeom は既存の ABI 型。 */
struct boot_disk_ops {
    int (*valid)(int drive);             /* 番号範囲。存在とは別 */
    int (*bios_da)(int drive);           /* 0/1→80h/81h、他→0 */
    int (*report_ata)(int drive, struct bootinfo_ata *out);
    int (*default_geom)(int drive, u16 *heads, u16 *spt);
    void (*fill_ata)(int drive, HddGeom *out);
};
void bootinfo_disk_register(const struct boot_disk_ops *ops);
/* platform/pc98/boot_disk.c の static const 表を pc98_boot_bind で登録。 */
```
- bootinfo.c は表ポインタだけ保持。IDE 固有型/定数/addr_mode 判定を boot_disk.c に移し、IDE の状態は IDE が所有。照会は起動前景または KAPI の協調文脈、IRQ 内では呼ばない。
- report_ata: ide_get_geom 成功時の変換をそのまま移動、present 条件を追加しない。bootinfo_report は BIOS 行→IDENTIFY 行（BOOTINFO_NDRIVES=2）の順・書式を維持。
- part_geom: BIOS DA 対応→bootinfo_hdd_geom が成功なら BIOS、それ以外は default_geom（元の ide_get_info、heads/sectors 非ゼロ検査）→IDENTIFY、失敗 -1。bank1 は BIOS DA=0 でも IDENTIFY fallback 可。
- hdd_geom_info: out NULL/番号範囲を先に検査→ゼロ初期化→BIOS 情報→fill_ata。fill_ata は元の get_geom 成功かつ present の場合だけ既存 ATA 欄/addr_mode を埋める。不在でも有効番号なら成功・ATA 欄ゼロ。返値と NULL 出力の扱いを維持。
- 表未登録時の内部照会は失敗（valid=偽、part=-1、report ATA なし）。本番は bind が全照会に先行する。hdd_geom_info の ABI wrapper と slot は変更しない。
```c
/* platform/pc98/pci_resources.c: 現 ledger_pci.c の実体を移す。 */
u32 ledger_resource_import_pci(void) {
    /* pci_get/BAR 解釈はここだけ。現行ループをそのまま保持 */
    struct ledger_resource r = {0};
    /* r の既存各欄を設定、ledger_resource_add(&r, &rid) 成功本数を返す */
}
static u8 pc98_pci_line_bits(unsigned int irq) {
    /* irq_line_quarantined / irq_storm_masked を従来の PCI bit へ変換 */
}
/* PCI_RESOURCES wrapper: import → overflow読取/表示 → hook登録、の順 */
```
- 台帳の口 `ledger_resource_add` を流用し、新しい資源収集レジストリは足さない。関数名/宣言は既存利用者のため維持し、kernel/ledger_pci.c は削除、build は platform 側を採用。
- BAR 本数 6/2/1、I/O・ゼロ・不明 header の除外、64bit の上位非ゼロ/末尾除外、RAW（幅不明、予約権限なし）、overflow と成功件数を変えない。PCI 設定書込み/サイズ probe は追加しない。
- r は呼出し中のローカル、登録後のレコードは台帳所有。line hook は常駐関数で既存呼出し文脈を継承（IRQ でも使える非待機・非確保の読取りのみ）。IRQ 状態の所有者は kernel のまま。

## 5. VFS 装置名のレジストリ
```c
/* fs/vfs_device.h: VFS 内部契約。VFS_DEV_* の既存値は固定。 */
enum vfs_name_mode { VFS_NAME_DIGIT_PREFIX, VFS_NAME_PREFIX, VFS_NAME_EXACT };
struct vfs_device_name {
    const char *prefix;
    int mode, type, first, count;
};
void vfs_device_names_register(const struct vfs_device_name *rows, unsigned n);
int vfs_device_id_valid(int type, int id);
/* pc98 の const 表: 5行、実装では数値を既存/名前付き定数にする。 */
/* {"hd", DIGIT_PREFIX, HD, 0, IDE_MAX_DRIVES}, {"fd", DIGIT_PREFIX, FD, 0, 4},
   {"COM", PREFIX, SERIAL, 1, 1}, {"cd", DIGIT_PREFIX, CD, 0, IDE_MAX_DRIVES},
   {"hostdrv", EXACT, HOSTDRV, 0, 1} */
```
- rows/件数を VFS が保持、実体は platform/pc98/boot_names.c。pc98_boot_bind で一括登録、確保不要。VFS/FS の通常 init でこの表を消さない。未登録時は parse/番号検証とも失敗。
- 純粋な字句解決表であり、drivers/dev の実在 Device 表とは別。fd2/3 や不在 hd も旧来の parse は成功するため dev_find 成功を条件にしない。loop/lo は新規に受理しない。
- DIGIT_PREFIX は prefix 直後の **1 桁だけ**を範囲検査し、末尾は無視。COM、COM0、COM9、COMxyz は全て SERIAL/1。hostdrv0 は失敗。大小文字は区別。成功するまで出力ポインタへ書かない。
- `vfs_dev_parse` の署名は維持し表を順走査。`vfs_dev_mount_count(drive)` は `vfs_device_id_valid(VFS_DEV_HD, drive)` を使い、無効なら既存 VFS_ERR_INVAL、有効なら従来の mount 件数。不在なら通常 0。
- IDE_MAX_DRIVES/ide_limits.h は VFS 本体から除去し platform の行定義にだけ置く。既存上限=4を host fixture で固定確認。fd の 4 は FDC 実装台数とは別の名前受理上限定数として定義。
- 使う文脈は起動/協調/KAPI、IRQ 内で parse/mount しない。ABI・マウント衝突判定・FS 側の型検査はそのまま。

## 6. 大きさの見積もりと上限
|項目|D1+D2 後からの増減見積もり（i386、実測ではない）|
|---|---|
|移動するコード/文字列|元を消すため原則 ±0 B。表示共用で重複を作らない|
|起動表/dispatcher・引数|11 pointer=44 B＋保持 pointer 4 B、context は stack 12 B。コード +96〜256 B|
|幾何表/転送|5 pointer=20 B＋保持 4 B、コード +64〜192 B|
|名前表/走査|5行×20 B=100 B＋保持 8 B、旧分岐を削除してコード -128〜+256 B|
|PCI 移動/表示共用/配置差|コード -128〜+256 B（同じ関数の再配置を含む）|
|合計|常駐 image 約 +100〜+1,300 B、保守的受入予算 **+2,048 B**。BSS 増分約16 B、stack増分12 Bを別記|

- `include/memmap.h:230` の常駐上限は現定義で **0x275000=2,576,384 B**。D1+D2 後の `__kernel_resident_end` と比較し、ページ丸め込みのリンク ASSERT を通す。現在の余裕を推測して合格扱いしない。
- 起動 FD: `vmkernel.lz4` の増減は圧縮次第（非圧縮差と同一ではない）。2HD と 1.44MB 両方で FAT クラスタ丸め後の空きと root dir 枠を記録。2HD は 1,261,568 B の媒体容量（D88 ヘッダは別）、144 の画像全長は 1,474,560 B 固定。両方 root entry は192。新しい搭載ファイルはゼロ。
- MINIMAL は BOOT+MINIMAL と FD の同一集合を維持。build/packages.yaml/core.yaml の収録集合・パッケージ entry 上限は変更不要、実体サイズ一致を test_packages で検査する。
- kdebug.bin は本体/ops/import 数を増やさず予想 **0 B**（アドレス移動で対応する生成 fingerprint は更新し得る）。`tools/module_schema.py:55` の image/mem 各262,144 B、load_span 524,288 B 上限を同じ生成器・loader で検査し、カーネルと対の成果物で起動する。上限緩和はしない。
- 設計段階ではビルド/ゲスト未実施。差分が予算を超えたら配列・wrapper・表示の重複を削り再計測する。上限を広げる判断はこの設計に含めない。

## 7. ファイルごとの変更一覧
|新規/変更/削除|対象|おおよその行数（移動を含む）|
|---|---|---|
|新規|kernel/boot_platform.h / .c|45 / 25|
|新規|kernel/boot_display.h / .c|15 / 65（既存表示を移動）|
|新規|platform/pc98/boot.h / boot.c|12 / 190（wrapper/表/選択/HostDrv）|
|新規|platform/pc98/boot_disk.c|90（IDE 変換を移動）|
|新規|platform/pc98/boot_names.c|25（5行の const 表と登録口）|
|新規/削除|platform/pc98/pci_resources.c / kernel/ledger_pci.c|+80 / -65（line hook を含む）|
|変更|kernel/kernel.c|-240〜-290、+20〜35（境目呼出し、子 mount 引数/種別）|
|変更|include/bootinfo.h / kernel/bootinfo.c|+15 / ±80（既存処理を表転送へ）|
|新規/変更|fs/vfs_device.h / fs/vfs.c|+25 / ±60（parse/番号検証/登録保持）|
|変更|build/kernel.mk|10（新規常駐ソース、旧 ledger 行削除）|
|新規|tools/tests/test_dep_dir_d3.py / dep_dir_d3_host.c|150 / 180（表/順序/失敗注入/幾何）|
|変更|tools/tests/test_device_reservation.py / test_cd_read.py / test_dma_pool.py|各15〜35（抽出/順序/変異パス）|
|変更|VFS をリンクする既存 host/fixture と起動順試験|各5〜15、合計80〜150（下記の利用者監査で確定）|
|変更|tools/check_map.d の関連 YAML、新 D3 map、D2 設計の例外記述|合計25〜45|
- kernel/kdebug_imports.def の source が移動対象を指す場合だけ同じ symbol/slot の source を追従し再生成。生成済み C/ASM/JSON は手で編集しない。

## 8. 受入・既存利用者・2時間段
|受入項目|試験（host / ゲスト NP21/W）|変異（各1行）|
|---|---|---|
|A: 方向/登録寿命|host: kernel の対象 include/call が消え platform に1実体、bind が全使用より前、再登録・欠落を検査|names 登録を1行削除し初回 mount の失敗を検出|
|B: 全起動順/ログ|host: 実物 wrapper を stub で走らせ呼出し/表示列を比較、kernel の境目と残留処理も順序検査。NP21/W: D1+D2 基準とログ/画面比較（tick/CRC/buildのみ正規化）|PCI_ENUM と DISK の run を交換して順序試験を赤にする|
|C: FD/root/失敗|host: 0x90/0x30、ALT/非ゼロunit/HDDを含む全低位256値で旧判定と比較、FS登録失敗でも全4回、root失敗は停止、HostDrv有無/失敗は継続。ゲスト: 2HD/144 FD/HDD起動|root 判定を TYPE_MASK に置換して非ゼロunitケースを赤にする|
|D: 幾何/ABI|host: BIOS有効/欠落/不正、0..3/-1/4、不在、IDENTIFY失敗/ゼロ、reportとHddGeom全欄/戻り値比較。ゲスト: HDD幾何表示とCDインストール後のHDD起動|drive2 の bios_da を 0x82 に変え bank1 の試験を赤にする|
|E: PCI/台帳|host:既存 test_device_reservation の全 BAR/overflow/RAW ケース、import→ログ→hook→LAN順。ゲスト:NP21/W の absent と raw=0/ovf=0|64bit 上位非ゼロを除外する1条件を外しhostで検出|
|F: 名前/番号|host: hd/fd/cd 0..4・負数表現・末尾、COM variants、hostdrv完全一致、未登録/再bind、不在番号、二重mount/countを旧結果と比較|DIGIT_PREFIX を完全一致に変え hd0suffix の試験を赤にする|
|G: CD/kdebug/容量|host:既存 cd/kcg/kdebug/package/build上限。ゲスト:CDインストール、kdebug有/無と自己試験、HostDrv/入力/時計/LANの従来スモーク|子mountのroot除外を外し二重mount試行をhostで検出|
- 変異は設計した検出力の受入項目。実装者は MUTATE=0、PM が統合後に変異を一度実施。NP21/W absent は実 PCI BAR の試験証拠にしない（host が BAR の検査を担う）。
- 既存利用者: `test_device_reservation.py` は旧ファイル全文・kernel の import/ログ断片・変異先を使用。新 platform 実体を抽出し実行する形へ同時更新し、文字列だけの合格に弱めない。
- `test_cd_read.py:565–573` の root mount/子mount抽出、`test_dma_pool.py:160` の fdc_init位置を新しい境目へ追従。CD の部品/FS試行順、DMA 利用可否 gate の検証は残す。
- 順序利用者 `test_kcg_boot.py`、`test_memory_boot.py`、`test_gfx_boot.py`、`test_lan_module.py`、`test_kdebug_init.py` は残す目印も含め再実行。bootlog/stack_entry の kernel 抽出も確認。
- VFS host は自動で実機装置を probe せず、本番の boot_names 表を明示登録する fixture にする。`test_vfs_fd_path.py`、vfs_mount_dev_host、cd_read_host、cd_module_host と grep で見つかる直接 vfs.c 利用者を全追従。既存差し替え定義/コピー元/変異一覧も新ファイルを入力に加える。
- `test_bootinfo.py` は bootinfo_check の純粋試験中心なので変更不要、追加 host が runtime bootinfo.c＋実物 adapter を IDE stub で試す。check_map の選択入力へ新ヘッダ/実装/fixtureを登録し check_select --lint。

|段（各 codex 2時間以内の目安）|内容/完了点|順・並列|
|---|---|---|
|D3a 90分|名前表と VFS 置換、既存 VFS fixture/map 更新、A/F host、make kernel|D1+D2取り込み後。最初|
|D3b 100分|boot_disk 表、bootinfo変換移動と ABI/幾何 host、PCI資源移動と既存台帳試験、make kernel|aの後。kernel/build/map重複のため実装並列不可|
|D3c 110分|boot表/wrapper/表示共用、root/context/子mount、D2例外削除、既存順序/抽出追従、B/C host、make kernel|bの後。全コード境目をここで接続|
|D3d 110分|make all、全D3差分の選択host MUTATE=0/静的検査、容量表実測、NP21/W一式と証拠整理（PM変異含む）|cの後。host/容量と停止不要のログ評価のみ並列可。単一ゲストのFD/HDD/CD操作は直列|
- 各段は既存機能が起動する状態で引き渡す。前段の失敗を次へ持ち越さない。D3d のゲストは既存自動化と試験用コピー媒体を使い、元 NHD/ini/イメージ master は変更しない。時間内に実施できない外部待ちは未実施を明記し、合格にはしない。

## 9. 未決（ユーザー判断が要るもの）
- **0 件**。#15 は後段へ、現行の root/名前受理の癖は維持、4段直列を採用。実装基点 SHA・容量余裕・ゲスト結果は作業時の測定事項であり、ユーザーへの仕様判断依頼ではない。

## 確定 (2026-10-26、Opus 5.5 の第 1 回 (P1 なし、条件付き承認) を PM が反映 — 本文の該当の行より優先する)

- **F1**: §8 の追従に足す — (a) `tools/tests/test_sh_truncation.py:42` (kernel.c の `BOOT_DRIVE_FDD…{ root_dev = "fd0";` を読む) は platform/pc98/boot.c から読む形に (決め打ちに弱めない)、(b) `test_bootlog.py:186` (`vfs_mount("/", root_dev, root_fs)` の find) は新しい文字列に、(c) `test_dma_pool.py:136` (正規表現 `kernel_mount_children\(u32 boot_drive\)`) と `dma_boot_host.c:111` (`kernel_pci_line_bits` の stub) は新しい引数と置き場に、(d) `tools/check_map.d/check-bootinfo-host.yaml:5` (`kernel/boot_devices.h`) と `check-memory-host.yaml:45` (`kernel/ledger_pci.c`) の対応表。
- **F2**: PCI_RESOURCES の wrapper に入れるのは「import → [ledger] ログ → hook 登録」だけ。**lan_module_load と PCM の load は kernel.c の今の位置に残す** (kernel/ の部品の façade で具体ドライバではない。test_lan_module.py:89-90 と dma_boot_host の DMA ゲートを保つ)。
- **F3**: §3 の境目の表に「wrapper に入れる現行 kernel.c の行範囲」の欄を足したものとして読む: INPUT=302-308、FLOPPY=311-368、DEV_FS=371-381、PCI_ENUM=393、DISK=396-407、AUTO_MOUNT=452-461、CLOCK=465-467、FONT=479、PCI_RESOURCES=532-539 (F2 の範囲だけ)、NIC_LEGACY=620 (行は main 83c785b4。D1・D2 の取り込みでずれるので、実装者は関数の呼出しで照合する)。palette_init・bootinfo_report/_image・HEAP と "MOUNT..." の表示は wrapper の外 (kernel.c) に残す。
- **F4**: `kernel/boot_devices.h` (ide.h/ide_addr.h を読むだけ) は D3b で削除し、bootinfo.c と kernel.c から ide.h/ide_addr.h を外す。D3 の後に kernel.c が読まなくなるドライバのヘッダ: kbd・mouse・fdc・rtc・kcg・pci・pci_bind・dma8237・ide・lgy98・e82557 (PCI 資源の照合に要る分は platform 側へ)。diskio_os32 は FS の層なので残してよい。D2 の「D3 までの起動の配線」の例外 (kernel.c が ../drivers/kbd.h・mouse.h を読む) はここで解消する。
- **F5**: D3d (codex) は `make all` + 候補 `MUTATE=0` + `--lint` + 容量の実測まで。ゲスト (FD 起動 1232/1440・HDD 起動・CD インストール・kdebug) と変異は **D3e = PM** (D1c と同じ形)。
- **F6**: DEFERRED_TESTS の DEPDIR-D1-FD の行に「D3 の後の Ra266 の FD/HDD 起動と `[ledger] pci raw=` が BAR の本数と一致すること」を足す (NP21/W に PCI は無い)。
- **F7**: 子マウントの CD の判定は文字の判定 (`dname[0..1]=="cd"`) のまま残し、引数を root の名前に変えるだけ。
- **F8 (PM 決定)**: `pc98_boot_bind` の中から D2 の `pc98_d2_bind` を呼び、kernel.c での呼出しを 1 つにする (D2 の確定設計 §2 の「D3 で platform の起動一覧へ移す」と同じ趣旨)。
- **F9**: D3c を c1 (表示の共用・表・wrapper・試験 B) と c2 (root・子マウント・F1 の追従・試験 C) に分ける。段は D3a → D3b → D3c1 → D3c2 → D3d (codex) → D3e (PM)、直列。
- **F10**: ledger_pci.c を消す段の依頼に「消したソースの .d を消してから組み直す」を書く。
- **F11 (PM 決定)**: 名前のレジストリの登録を mount の前提にしない。装置の番号の上限は VFS 側の定数 (`VFS_DEV_UNIT_MAX` 等) にし、platform 側で `STATIC_ASSERT(IDE_MAX_DRIVES == …)` を置く (仕組みを最小に、mount を呼ぶ host 試験 11 本に boot_names.c と -Idrivers を足さない)。§5 のレジストリは名前→装置の解決だけに絞る。
- **#15 (DMA の制約)** は D3 に入れず、票の「後」の行へ (PM が票に足す)。
