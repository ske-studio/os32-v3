# platform/pc98/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN・DEP_DIR_B_DESIGN・DEP_DIR_C_DESIGN・DEP_DIR_D1_DESIGN・DEP_DIR_D2_DESIGN・DEP_DIR_D3_DESIGN・DEP_DIR_D_DESIGN (D1 で移した)
ソース: `platform/pc98/`

- 単一利用者・単一タスク・協調切替、整数演算。カーネル→具体ドライバだけを外す。逆方向、既存検査、エラー、待ち、復旧を保つ。権限機構は追加しない。

- 確定境界: D2 §2–4 の入力・出力・時計の表と未登録値、D3 §3 の boot_step と IF/順序を維持。`platform/pc98/boot.c:243` の bind に新フェーズを足さない。
- B §2 のリンク時固定口・§試験の基点列固定を踏襲。C §3 の固定名ヘッダ/static const は必要な機種記述だけに使い、状態表を増やさない。

## 2. 共通の契約と生成経路

- 新規 `include/platform_device.h` は内部宣言だけ。実装は `platform/pc98/device_*.c`。登録関数0、操作表0、動的確保0。状態は従来の所有者に置く。
- 名前は原関数に `platform_` を付け、引数/返値/副作用を一対一で転送。公開KAPIの番号・版・署名・cdecl・out/in記述は不変（F5(a)、公開変更ではない）。

> [要確認: 計画の時制?]

|実行文脈|所有者／制約|
|---|---|
|専用KAPI、FS、host transport、V86起動準備|通常の同期呼出し。IRQ handlerからは呼ばない。IF/待ち/戻り値を変えず転送|
|LAN probe/load、PCM登録/解除|通常文脈、既存 irq_save 区間だけIF=0。PCI登録記録・module参照は常駐|
|PCM/LAN IRQ・tick|従来どおりIRQ内から部品へ。参照取得中だけ関数ポインタ有効。新転送の追加対象ではない|
|異常停止・owner終了|既存の順序・quarantine/leak判断を維持。復旧ポートはL19、ここで変更しない|

- PC-9821、単一利用者・単一タスク・協調切替。目的は固まらない・壊さない・戻せること。整数演算、既存 I/O と待ちを維持。
- KAPI 番号・署名・呼出規約、部品 ABI、配置世代を変更しない。CLAUDE.md [C1]–[D4] を適用。新規ハード制御方式は導入しない。

- D1 §2: `tick_register`、6枠、IRQ0/IF=0、SND→PCM→NE2K→E82557→LINK→LGY98 を維持。`arch/x86/kernel/isr_handlers.c:540` の dispatch は変更しない。
- D3 §3: `boot_platform_register(const struct boot_platform *)` と `step[BOOT_POINT_COUNT]` は既存のまま。
  `init/kernel.c:117` の bind、`:176` PIC→`:182` sysclk→BOOT_DMA→`:187` PIT の呼出位置、IF、ログを固定する。

## 2. 共通境界 — リンク時固定の小さい口

- カーネル→PC-98 の具体 I/O だけを切る。platform→kernel の irq_save/restore、pit_compute 等は普通の依存として許す。
- 早期起動・IRQ 内でも必要な口なので登録待ちを作らない。下記ヘッダの外部関数を PC-98 実装1個にリンクする。
- 新しい ops 構造体・登録関数は **0**。既存公開名は残す。新規名は内部専用で KAPI に公開しない。
- 起動操作を BOOT_DMA 等に束ねない。既存 boot 登録一覧と呼出位置をそのまま使い、新しい初期化フェーズを足さない。

- ハード定数は既存 pc98.h/idt.h の名前を使う。互換定義の一括移動は行わず、今回追加する値に裸の数値を散らさない。

- カーネル→機種実装だけを切る。逆向き依存は残す。KAPI 番号・ABI、配置世代、検出アルゴリズム、番地・帯域・PTE・台帳配置は不変。
- C11・整数演算・既存定数の三層・P2V 系を維持。新しい防御機構、動的登録、9801 対応は入れない。

- D3確定設計 §3 の `boot_platform_register` / `boot_platform_run` / boot_context / step一覧には手を加えない。
- B確定設計 §2 と同じ、リンク時固定の内部口とする。新しい登録関数0、ops表0、初期化フェーズ0。
モデル作成・台帳・ページ割当方針はkernel所有。
CPUのPTEビット生成・CR3・TLB操作はkernel所有。

## 2. 共通の境界・配線

- 共通契約を include/、薄い転送を kernel/、PC-9821固有の表・変換を drivers/ または platform/pc98/ に置く。
- 動的探索・名前解決・新しいioctl体系・所有者レジストリを作らない。各種1表、寿命は常駐全期間、解放・稼働中の差し替えなし。

- 登録の失敗・未登録を正常起動と見なさない。登録は静的な必須配線なので割当失敗経路は作らず、未登録試験は「固まらない」の確認に限る。

- カーネル→具体ドライバだけを外す。platform→ドライバ、ドライバ→カーネルの依存は許す。新しい動的探索、優先度、依存グラフ、遅延初期化は作らない。
- platform の静的 const 表を一度登録。状態は元の所有者（kbd/mouse/FDC/IDE/PCI、VFS、bootinfo、台帳）に残す。コピーするのは起動引数と root の選択結果のみ。

- 単一利用者・単一タスク・協調切替。バグ・機器誤動作からの復帰を維持。権限分離、攻撃者対策、9801互換は追加しない。
- C11、整数演算、既存定数の三層、P2V系を維持。KAPI番号・ABI・モジュールのimport契約は不変。

- 確定境界だけ継承: D2 §2/4のConOutputOps・EmergencyOps、D3 §3のboot_context/step、B §2/8の固定口・基点列、C §2の固定名ヘッダstatic const。

## 2. 共通境界・所有・呼出文脈

- 今回はBの形を採用し、外部関数をPC98実装1個へリンクする。新規登録関数0、ops構造体0、起動step追加0。
- 通常consoleにも固定口を使う。初期clear/カーソルとkprintf純粋変換が登録状態に依存せず、既存利用者のリンク差し替えも容易。
- D2の`con_output_register(const ConOutputOps *)`と固定`const EmergencyOps emergency_output`はそのまま。TVRAM障害口をどちらにも混ぜない。
- D3の`boot_platform_register(const struct boot_platform *)`／`boot_platform_run(...)`、PC98 bindの呼出位置・順序は変更しない。
- 新ヘッダに機種の物理定数を漏らさない。表示用の新しいstatic const記述表は不要。既存Cのmemory記述は変更しない。

> [要確認: 計画の時制?]

### device_ide

```c
/* platform/pc98/device_ide.c: 状態なし、KAPIの検査は呼出側に残す */
void platform_ide_init(void) { (void)ide_init(); }
int platform_ide_read_sector(int d, u32 l, void *b)
{ return ide_read_sector(d, l, b); }
/* device_ide.c: 成功時だけフィールドをコピー、エラー値はそのまま */
/* IdeInfo i; int rc=ide_get_info(drv,&i); if(rc) return rc; ...; return rc; */
```

- L05aのIDE KAPIはinit/present/identify/read_sector/write_sector/write_sectors/get_infoの7本。infoコピーは96B、既存ABIのvoid*を保ち内側でcast。out検査はKAPI側のまま。

### lan_match

- `platform/pc98/lan_match.c` に現 e82557_driver をそのまま移す。外部名も残し既存利用者を壊さない。lan_probe は `lan_module_probe` として内部公開。

> [要確認: 計画の時制?]

- 機種口は表を返すだけ。bindの時期・pci_boundの更新・失敗時のunbindを常駐facadeに残す。起動で先行bindしない。

```c
/* platform/pc98/lan_match.c: 静的・常駐、callbackも常駐 */
const struct pci_driver e82557_driver = {
    sizeof(struct pci_driver), E82557_VENDOR, E82557_DEVICE,
    PCI_CLASS_NETWORK, PCI_SUB_NET_ETHERNET, "e82557", lan_module_probe
};
const struct pci_driver *platform_lan_driver(void) { return &e82557_driver; }
```

### device_loop

- `device_loop.c` は各 loop_dev_* をそのまま呼ぶ。

- slot/V86_DISK_SLOT、CHS/LBA選択、bps/count、attachしたfdの寿命、戻り値は既存。loop状態はdriver、V86の試験buffer/起動状態はkernel。HLE本体の依存は変更しない。

- 決定: **旧い呼び出しを残す** — drivers の `loop_dev_detach` はそのまま (v86.mod の import と部品の ABI は変えない)。`platform_loop_dev_detach` は KAPI の target 用の薄い関数で、中で `loop_dev_detach` を呼ぶだけ。attach・status も同じ形 (HLE が import する名前は残し、KAPI の target だけ platform の薄い関数へ)。

### device_fs

- L06は `device_fs.c` に ext2_format/format_at、serialfs_session_begin/end の4転送。VFSの既存登録表とsession状態はFS所有、二重登録なし。

### device_pci

device_pci.cへ4転送。
bind_infoは既存kapi_pci_bind_infoを維持、その内部だけplatform照会へ。

### device_host

device_host.cにnp2_detect/get_version/get_cpu/get_clock/check_hostdrv（元の宣言と同型）と上記6転送。

> [要確認] 「上記6転送」は元文書の host_readfile/writefile/listdir/delete/mkdir/getconfig。platform/pc98/device_host.c の同名 platform_host_* に対応する。

### device_kcg

ROM読出しの既存4枠表は保持する。

```c
void platform_kcg_init(void) { kcg_init(); } /* scale=1 */
void platform_kcg_set_scale(int s) { kcg_set_scale(s); } /* 1..4へclamp */
int platform_kcg_load_font(const char *p) { return kcg_load_font(p); } /* NOSYS */
```

### system

```c
/* platform/pc98/system.c */
void platform_reset(void) {
    outp(SYSPORT_C_BSR, BSR_SHUT0_SET);
    outp(SYSPORT_C_BSR, BSR_SHUT1_SET);
    outp(CPU_RESET_PORT, 0x00); /* 現行値を定数化する場合も値は固定 */
}
```

- ブザーの極性は現行の BSR_BUZ_ON/OFF に従う。時長・周波数をこの口に持たせない。beep の状態変数も増やさない。

### clock

|platform/pc98/clock.c、起動前景|`v86_bios_real_lowmem()[BIOS_WORK_SYSCLK]` を1回読む。bit7=1:1996800Hz/8MHz系、0:2457600Hz。既存低位メモリ変換経路を維持|

### memory

|probe のreported/accepted/saved等|platform関数の自動変数|paging前、IF=0、起動1回、IRQ内ではない|

- BIOSのlow byte→high byte合成、上限clamp、1MiB刻み、addr XOR、24bit mask、volatile、P2V_BOOTを保存。probe語の新規保存やcache操作は追加しない。
- 固定するアクセス列: `low読(saved) → cell書(pattern) → low読比較 → [差ならlow復元してbreak] → cell読比較 → accepted++`、その後 `accepted` 個を先頭から再読。早期break後のアクセスも増やさない。

### tick_order

```c
/* platform/pc98/tick_order.h: 配線の順序。kernel/irq/tick.c は読まない。 */
enum { PC98_TICK_SND, PC98_TICK_PCM, PC98_TICK_NE2K,
       PC98_TICK_E82557, PC98_TICK_LINK, PC98_TICK_LGY98 };
```

platform の enum に `PC98_TICK_COUNT` を足し、両方が見える登録側で `_Static_assert(PC98_TICK_COUNT <= TICK_SLOT_COUNT, ...)`。

> [要確認: 計画の時制?]

### boot_names

- 名前解決は hd/fd/cd の数字 0–3 の**後続文字を無視**、COM は先頭 3 文字で常に ID=1、hostdrv は完全一致。装置の存在確認とは別。

```c
/* {"hd", DIGIT_PREFIX, HD, 0, IDE_MAX_DRIVES}, {"fd", DIGIT_PREFIX, FD, 0, 4},
   {"COM", PREFIX, SERIAL, 1, 1}, {"cd", DIGIT_PREFIX, CD, 0, IDE_MAX_DRIVES},
   {"hostdrv", EXACT, HOSTDRV, 0, 1} */
```

platform 側で `STATIC_ASSERT(IDE_MAX_DRIVES == …)` を置く

### boot_disk

```c
/* platform/pc98/boot_disk.c の static const 表を pc98_boot_bind で登録。 */
```
IDE 固有型/定数/addr_mode 判定を boot_disk.c に移し、IDE の状態は IDE が所有。照会は起動前景または KAPI の協調文脈、IRQ 内では呼ばない。

> [要確認: 計画の時制?]

- report_ata: ide_get_geom 成功時の変換をそのまま移動、present 条件を追加しない。bootinfo_report は BIOS 行→IDENTIFY 行（BOOTINFO_NDRIVES=2）の順・書式を維持。

default_geom（元の ide_get_info、heads/sectors 非ゼロ検査）

fill_ata は元の get_geom 成功かつ present の場合だけ既存 ATA 欄/addr_mode を埋める。

### console

|PC98 console backend|TVRAM表現、漢字2セル、反転ビット、GDCカーソル列。追加可変状態なし|呼出側のIFを維持、IRQ禁止化・確保・登録待ちを追加しない|

- clear/scroll/putchar/readchar/putkanji/reverseのメモリ操作・既存境界検査をPC98側へ。kernelのtvram_*署名を維持。

> [要確認: 計画の時制?]

- reverseの右半分判定・左への調整・1/2セル返値、JIS高位補正をbackend所有にする。UTF-8→ANK/JISと論理文字幅は現consoleに残す。
- scrollは文字u16・属性u16の読書き、clearは文字u16・属性u8。高位属性byteの保持／上書きまで基点と同じにする。

> [要確認: 計画の時制?]

- GDCのCSRWとCSRFORMだけをbackendへ。shape(1)→sync、stop時sink_enable→V86判定→shape(0)を保存。

> [要確認: 計画の時制?]

- gridは現TVRAM_COLS/ROWSの契約を維持し可変解像度化しない。既存の画面サイズAPI・KAPI・sdkは変更なし。

### emergency

|PC98 emergency backend|文字/属性面へのアクセスだけ。可変大域状態なし|paging前の起動、例外、DF専用スタック、V86 teardown（IRQ/例外内を含む）|

```c
/* platform/pc98/emergency.c: 通常console/sink/kprintfに依存しない */
void platform_fault_cell(u32 pos, u16 code, u8 attr) {
    ((volatile u16 *)P2V_IO(TVRAM_BASE))[pos] = code;
    ((volatile u16 *)P2V_IO(TVRAM_ATTR))[pos] = (u16)attr;
}
```

文字u16→属性u8、80列折返し・25行打切り。新しい座標補正をしない。

- 固定関数・P2V_IO・既存メモリだけで成立。ロック、待ち、動的登録、ヒープ、V86、モジュール、通常consoleへの呼戻しなし。

### splash

|PC98 splash backend|既存struct gfx_kernel_fbの記述子1個（借用ポインタ）|起動前景のみ、IRQから呼ばない。バッファ自体はgfx所有|

```c
/* platform/pc98/splash.c: 旧記述子を移動、バッファを複製しない */
static struct gfx_kernel_fb splash_fb;
/* begin: pref保存→PC98指定→gfx_init→pref復元→framebuffer/format/4面確認 */
/* 失敗: gfx_shutdown→非0。kernel側がtvram_clearしてreturn。 */
```

- bb_clear/bb_fill_rectの本体を移動し呼出名を置換。clip、端byteのmask、4面のbit順、GFX_PLANE_SZ/BPLはPC98側へ。

> [要確認: 計画の時制?]

- planar確認もbeginに移す。異常時に別backendをprobeしない。prefは成功・失敗とも検証前に元に戻す。

- `platform_splash_clear_pages`は現`gfx_clear_planar_pages(GFX_PLANE_SZ)`だけ。CPU描画→既存gfx_present_rasterの経路を保つ。

- splash_fbはbegin毎に既存取得経路で更新、gfx停止後には使わない。一般描画API・複数backend対応・新しいメモリ確保は追加しない。

### console_attr

- 色ビット判定・visible補完・旧来の青緑赤変換・黒→白を`platform/pc98/console_attr.c`へ全移動。単独hostリンクできる純粋関数に分離。

> [要確認: 計画の時制?]

### display

既存名そのものが固定platform口なので転送層を増やさない。

- 保存済みBDAの所有はv86側、`v86_bios_real_lowmem()`で取得。ゲストBDAに替えない。v86.mod・kcg.mod・登録が無くてもハード復旧は成立。
- 列を固定: GFX STOP→保存BDA条件別EGC/GRCG/PEGC解除→16色/HIRES→400lineパラメータ→PRAM8byte→text_start→表示/アクセスpage0→palette_init→console_hw_cursor_enable。
- text_start内部は高さ/flip更新→GFX STOP→DISP_ON→TEXT START、各wait位置も保存。新しい再試行や同期待ちを足さない。
- readyはsink/高さ/flipで先に拒否し、次にHSYNCの31kHzを判定。readのNULL、writeの0xffff超はI/O前に拒否。専用セルu16読書きと返値を維持。
- gfx_current_height/flipはgfx所有のまま、platformが現順序で更新。回復用セルの貸出管理を新設しない。
- 復旧の通常前景と既存異常復帰文脈を保存。IF操作を追加しない。KCG invalidateは既存関数表経由（L10との共有点はこの1行）。
- [HW1]の新規描画命令なし。既存mode解除、START/STOP、SCROLL設定を移すだけで復旧アルゴリズムを変えない。

### display_private

- `DISPLAY_RECOVER_CELL`はPC98側内部ヘッダへ移す。既存テスト等の利用があればそのincludeを追従。公開の物理アドレスAPIを新設しない。

> [要確認: 計画の時制?]

## 索引

- [platform/pc98/boot](boot.md)

- [platform/pc98/d2_bind](d2_bind.md)

- [platform/pc98/pci_resources](pci_resources.md)

- [platform/pc98/pic](pic.md)

- [platform/pc98/platform_memory_desc](platform_memory_desc.md)

- [platform/pc98/timer](timer.md)
