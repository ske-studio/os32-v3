# DEP_DIR_A — 部品の契約と専用 KAPI の呼び先を platform へ — 設計

> 状態: **現行 (2026-10-28)** — 確定 (astra 起草・Opus 2 往復・PM が B1〜B6 と世代の扱いを決定)。段の状態は票 TASK_DEP_DIR が持つ。
> 出所: ~/os32-tmp/run/depdir-a/DESIGN.final.md を 2026-10-28 に写した。冒頭の『r1→r2 の変更』は往復の記録として残す。


## r1→r2 の変更
- A1: 新契約を部品の contracts に加え、PCM/FM/LAN の ABI 世代変更・旧部品拒否・組配布を明記。
- A2: kdebug の宣言所在と hwdev の追従・再生成を追加し、世代/CRC の変化を受け入れる。
- A3: 漏れていた具体装置ヘッダ6箇所の置換・削除を各段へ追加。
- A4: sdk/kapi.json の includes 差分を段ごとに明記し、IdeInfo の宣言を保持。
- A5: DMA・IDE・disk_auth・KCG・V86 の fixture と check-ci-stab の追従を追加。
- A6: L05 を L05a（契約/KAPI）と L05b（FS幾何/fixture）に分け、L05b→L06 を明記。
- A7: L02/L08 の実機持越しを DEFERRED_TESTS の DEPDIR-A-PCI に記録。

## 1. 実物の確認
- 対象 `/home/hight/os32-v3` main **b400acb6**（確認済み）、SURVEY 基点 d0f5b0e8。2026-10-28、設計のみ。実装・ビルド・ゲスト試験は未実施。
- 単一利用者・単一タスク・協調切替、整数演算。カーネル→具体ドライバだけを外す。逆方向、既存検査、エラー、待ち、復旧を保つ。権限機構は追加しない。
- 下表の「差分」9件を食い違いとして数える（行番号移動は1群1件、追加発見は別件）。一致欄は数えない。パスは上記リポジトリ相対。

|区分|SURVEY → 今の実物／設計上の扱い|
|---|---|
|差分1 #26|`drivers/net/e82557/lan_module.c:163` → `:167` の e82557_driver。表は依然常駐。SURVEY の部品登録案は F9 により機種配線へ変更|
|差分2 #27|`drivers/sound/cs4231/pcm_module.c:74,88,154` → `:77,91,157` が PCM_IRQ 固定。汎用 lifecycle 案は採らず既存 IRQ 口を使う|
|差分3 #36/37|生成 C の954/1024等 → `kapi/kapi_generated.c:957,1027,2236`。FS→IDE は SURVEY 対象外、`fs/ext2/ext2_fmt.c:412,413,425` の format_at にも存在|
|差分4 #39/40|生成 C の904/1680 → `kapi/kapi_generated.c:907,1683`。操作表案は F5(a) の固定転送へ|
|差分5 #41/42|生成 C の2144/2273 → `kapi/kapi_generated.c:2147,2276`。PCI/serialfs も固定転送へ|
|差分6 #43|生成 C の1030/1036/1652 → `:1033,1039,1655`。`drivers/video/kcg/kcg.c:33,38,56` は scale初期化・clamp・常時NOSYS。font読込み処理ではない|
|差分7 #45|`kapi/kapi_sys.c:318,334` → `:337` PCI照会、`:353` ポートサービス。後者は解決済みで対象外|
|差分8 #28補足|`exec/exec.c:40`、`kernel/ver_collect.c:6` も pcm_cs4231.h、`sdk/kapi.json:27,58` に fm/pcm の具体 include。表の3ヘッダだけ直しても残る|
|差分9 KCG前提|`drivers/video/kcg/kcg_module.h:11–17` は read_ank/read_kanji/stats/invalidate の4枠だけ。専用3操作を入れる既存枠がない。L10は§12の判断が必要|
|一致 #15/28/30/44|`mm/dma_pool.h:30`・`dma_pool_math.c:99`、pcm/fm/lan_module.h:4、`kernel/v86/v86.c:799,810,907,917,923`、`kapi/kapi_host.c:43,54,71,86,91,96`|

- 確定境界: D2 §2–4 の入力・出力・時計の表と未登録値、D3 §3 の boot_step と IF/順序を維持。`platform/pc98/boot.c:243` の bind に新フェーズを足さない。
- B §2 のリンク時固定口・§試験の基点列固定を踏襲。C §3 の固定名ヘッダ/static const は必要な機種記述だけに使い、状態表を増やさない。
- platform 現物は boot/boot_disk/boot_names/d2_bind/pci_resources/clock/system/pic/timer/memory と記述ヘッダ。新転送は同じ常駐ビルド規則（`build/kernel.mk:74–75`）。

## 2. 共通の契約と生成経路
- 新規 `include/platform_device.h` は内部宣言だけ。実装は `platform/pc98/device_*.c`。登録関数0、操作表0、動的確保0。状態は従来の所有者に置く。
- 名前は原関数に `platform_` を付け、引数/返値/副作用を一対一で転送。公開KAPIの番号・版・署名・cdecl・out/in記述は不変（F5(a)、公開変更ではない）。
- `sdk/gen_kapi.py:569–581` は body が target より優先、target省略時は name。生成 C を直接直さず JSON の includes/target/body の呼先だけを直す。生成器の仕様は変えない。
- body付きIDE/formatは既存の write_allowed、範囲検査、cnt==0、cast の順を保ち、末尾の呼先だけ変更する。targetだけ追加して移行済みにしない。
- `sdk/kapi.json:453,543,554,1207,508,1686` が該当。IDE init のKAPIは void（実装は int）なので戻り値を従来どおり捨てる。

```c
/* include/platform_device.h: 以下は代表、全宣言は元と同じ型で列挙 */
void platform_ide_init(void);
int platform_ide_read_sector(int drv, u32 lba, void *buf);
int platform_ext2_format_at(int drv, u32 start_lba, u32 length);
/* platform/pc98/device_ide.c: 状態なし、KAPIの検査は呼出側に残す */
void platform_ide_init(void) { (void)ide_init(); }
int platform_ide_read_sector(int d, u32 l, void *b)
{ return ide_read_sector(d, l, b); }
```

|実行文脈|所有者／制約|
|---|---|
|専用KAPI、FS、host transport、V86起動準備|通常の同期呼出し。IRQ handlerからは呼ばない。IF/待ち/戻り値を変えず転送|
|LAN probe/load、PCM登録/解除|通常文脈、既存 irq_save 区間だけIF=0。PCI登録記録・module参照は常駐|
|PCM/LAN IRQ・tick|従来どおりIRQ内から部品へ。参照取得中だけ関数ポインタ有効。新転送の追加対象ではない|
|異常停止・owner終了|既存の順序・quarantine/leak判断を維持。復旧ポートはL19、ここで変更しない|

## 3. L01 — DMA と共通型（2時間）
- 新規 `include/dma_contract.h` に DMA_XFER_MIN/MAX_BYTES、DMA_BANK_SIZE/MASK/SHIFT、DMA_PHYS_LIMIT と純粋判定の宣言を移す。8237のポート/チャネル/待ち定数は drivers に残す。
- 新規 `lib/dma_contract.c` に dma_range_ok とその64KiB跨ぎ判定を移す。元の関数名・整数幅・境界式を維持し、ドライバも同じ実体を使う（重複実装なし）。

```c
/* 独立契約。poolの limit 引数と呼出し方は不変 */
int dma_range_ok(u32 pa, u32 size, u32 limit) {
    if (size == 0) return 0;
    if (pa >= limit || size > limit - pa) return 0;
    if (dma_crosses_64k(pa, size)) return 0;
    return 1;
}
/* dma_pool.h / dma8237.h は dma_contract.h を読む */
```

- `include/pcm_contract.h`・`fm_contract.h`・`lan_contract.h` に利用側が要る型/公開状態値/関数宣言だけを移す。ハードレジスタ・PCMシーケンサ内部構造は移さない。
- PcmModuleApi/FmModuleApi/LanModuleApi 自体は kernel の既存ヘッダに残す。スロット数/順序/sizeof/offsetof と OPS_VERSION は固定。LAN の pci_dev は前方宣言、必要なPCI型は独立契約へ。
- pcm_present/reclaim/open等、FMの常駐facade宣言を共通契約から供給。exec/ver_collectとJSON includesも置換する。E82557状態値・診断型は名前と値を維持してlan契約へ。
- DMA pool状態/割当/leaseはkernel、8237実行状態とPCM ringはdrivers、module_handle/candidateは常駐facade。新登録はない。
- L01のinclude追従: mm/dma_pool_math.c:14 は dma_contract.h、drivers/sound/fm/snd_engine.c:11 は fm_contract.h、fs/serialfs/serialfs_session.c:36 は lan_contract.h へ置換する。
- tools/prototypes/pcm_imports.def の contracts に include/pcm_contract.h・include/dma_contract.h、fm_imports.def に include/fm_contract.h、lan_imports.def に include/lan_contract.h を追加し、L02ではLANに include/pci_contract.h も追加。既存列の順序は保つ。生テキストhashなので型配置不変でも PCM_MODULE_ABI_GEN・FM_MODULE_ABI_GEN・LAN_MODULE_ABI_GEN は変わる。
- kernel/module/kdebug_imports.def の source を dma_crosses_64k/dma_range_ok→include/dma_contract.h、pcm_open/pcm_state→include/pcm_contract.h、e82557_send/get_mac/state→include/lan_contract.h に変更。旧ヘッダに再宣言は残さず、生成器が新所在の本文から宣言を得る。
- PM決定: KDEBUG_ABI_GEN も変わり、hwdev契約CRCも変わり得る。配置世代・KAPI版・部品のスロット配置/OPS_VERSIONは不変。旧部品はloadのpolicyで断られる。カーネルと対応する部品を再生成・再ビルドし、hsync bootで常に組で配る（実機の入れ方は不変、Cirrus/FMと同扱い）。

## 4. L02 — 82557 match の機種配線（2時間）
- `platform/pc98/lan_match.c` に現 e82557_driver をそのまま移す。外部名も残し既存利用者を壊さない。lan_probe は `lan_module_probe` として内部公開。
- 機種口は表を返すだけ。bindの時期・pci_boundの更新・失敗時のunbindを常駐facadeに残す。起動で先行bindしない。

```c
/* platform/pc98/lan_match.c: 静的・常駐、callbackも常駐 */
const struct pci_driver e82557_driver = {
    sizeof(struct pci_driver), E82557_VENDOR, E82557_DEVICE,
    PCI_CLASS_NETWORK, PCI_SUB_NET_ETHERNET, "e82557", lan_module_probe
};
const struct pci_driver *platform_lan_driver(void) { return &e82557_driver; }
/* lan_start_module: candidate検査完了後、現位置で実行 */
const struct pci_driver *const drivers[] = { platform_lan_driver() };
pci_bound = 1;
pci_bind_all(drivers, sizeof(drivers) / sizeof(drivers[0]));
```

- lan_module_probe の状態検査→REGISTRATION参照→TICK参照→candidate.probe→DECLINE時の回収/隔離を不変にする。表からmod内アドレスを直接指さない。
- L08と共有するPCI契約は `include/pci_contract.h`。pci_dev/pci_driver/診断型・公開定数のみ、config I/Oは含めない。型配置は維持し、LANのABI世代変更は§3のとおり受け入れる。drivers/net/e82557/lan_module.c:7 の e82557_regs.h は削除し、装置定数は機種配線側だけで読む。

## 5. L03 — PCM IRQ資源を部品側へ（2時間）
- PCM_IRQの値を知るのはdrivers/pcm_cs4231側だけ。既存 imports の pcm_module_irq_register(irq,fn,arg,flags) を使い、表やexportスロットは追加しない。
- 常駐側は登録成功したIRQ番号を保存する。固定番号検査を外すが、引数・二重登録・module状態・irq_register自身の既存検査は残す。

```c
static unsigned int irq_line; /* pcm_module.c、irq_targetと同じ寿命 */
/* register: 既存 module_ref_acquire → irq_register の後 */
if (rc) module_ref_release(pcm_handle, MODULE_REF_IRQ);
else { irq_line = irq; irq_target = fn; }
/* unregister: irq_targetが存在するときだけirq_lineを照合 */
if (arg || !irq_target || irq_line != irq || irq_target != fn) return IRQ_ERR_INVAL;
/* detach: 既存の IF=0 内、quietとtick解除の後 */
int rc = !irq_target || !pcm_module_irq_unregister(irq_line, irq_target, 0);
```

- unregister成功時だけ irq_target/irq_line を消し参照を返す。失敗時は保持してunloadを止める。登録失敗ではirq_lineを公開しない。
- 部品が既存PCM_IRQを渡すことをhostで固定。通常経路のIRQ番号は不変。IRQ handler自身、EOI、DMA停止根拠、期限切れ隔離は変更しない。

## 6. L04 — loop/V86（2時間）
```c
/* include/platform_device.h、slot・型・NULL出力の意味は元と同一 */
int platform_loop_dev_get_geometry(int slot, u16 *c, u8 *h, u8 *s, u16 *bps, u32 *total);
int platform_loop_dev_read_chs(int slot, u16 c, u8 h, u8 s, void *buf);
int platform_loop_dev_read_lba(int slot, u32 lba, u32 count, void *buf);
int platform_loop_dev_attach(const char *path, int slot);
void platform_loop_dev_detach(int slot);
int platform_loop_dev_status(int slot, u32 *total, int *bps);
```
- `device_loop.c` は各 loop_dev_* をそのまま呼ぶ。v86.cの5呼出しとincludeを置換。JSONのloop_attach/detach/statusのtargetも変更。
- slot/V86_DISK_SLOT、CHS/LBA選択、bps/count、attachしたfdの寿命、戻り値は既存。loop状態はdriver、V86の試験buffer/起動状態はkernel。HLE本体の依存は変更しない。

## 7. L05a → L05b → L06 — IDE照会とFS管理（各2時間）
- L05aで `include/ide_contract.h` に **96BのIdeInfo** とその寸法定数/既存エラー/選択番号定数を移す。ドライバ・platform・生成KAPIが共用。フィールド・paddingを基点fixtureに固定。
- L05bでFSはIdeInfo全体を使わず下記の値コピーへ。新規装置番号体系や汎用block管理表は作らない。

```c
struct platform_disk_geometry {
    u32 total_sectors;
    u16 cylinders, heads, sectors, phys_sector_size;
};
int platform_ide_drive_present(int drv);
int platform_ide_range_ok(int drv, u32 lba, u32 count);
int platform_disk_geometry_get(int drv, struct platform_disk_geometry *out);
/* device_ide.c: 成功時だけフィールドをコピー、エラー値はそのまま */
/* IdeInfo i; int rc=ide_get_info(drv,&i); if(rc) return rc; ...; return rc; */
```

- L05b: ext2_super/fmt、fatfs_vfs、fatfs/diskioの全照会を置換。fs/fat/fatfs/diskio_os32.h:14 の ide.h も ide_contract.h/platform_device.h へ置換。`fs/ide_limits.h` は共通契約から上限を得て mask を維持（hd名/bootinfo幾何/MBR選択は変えない）。
- FSの媒体I/Oは既存Device/VFS経由を維持。256/512B変換、part_offset控除、範囲演算、丸め、失敗時の初期値はFS側で維持。新ヘルパ内で再計算しない。
- L05aのIDE KAPIはinit/present/identify/read_sector/write_sector/write_sectors/get_infoの7本。infoコピーは96B、既存ABIのvoid*を保ち内側でcast。out検査はKAPI側のまま。
- L06は `device_fs.c` に ext2_format/format_at、serialfs_session_begin/end の4転送。VFSの既存登録表とsession状態はFS所有、二重登録なし。vfs.cの未使用kbd.hを削除。
```c
int platform_ext2_format(int drv, u32 sectors);
int platform_serialfs_session_begin(void);
int platform_serialfs_session_end(int exit_code);
/* 登録は従来のVFS登録表のみ。上の関数は登録せず直接リンクする。 */
```

## 8. L08・L09 — PCI診断、NP2、host transport（各2時間）
```c
/* L08: include/platform_device.h + pci_contract.h */
int platform_pci_count(void);
int platform_pci_get(u32 idx, void *out);
u32 platform_pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg);
int platform_pci_bind_info_get(int idx, struct pci_bind_info *out);
/* L09: include/platform_host.h、net/link.hと同じ宣言 */
i32 platform_host_open(const char *req, u32 len, int owner);
i32 platform_host_status(i32 h, u32 *status, u32 *length, int owner);
i32 platform_host_read_stage(i32 h, u32 cap, const u8 **out, int owner);
i32 platform_host_write(i32 h, const void *buf, u32 len, int owner);
i32 platform_host_close(i32 h, int owner);
void platform_host_owner_exit(int owner);
```

- L08: device_pci.cへ4転送。JSONのcount/get/cfg_read32を変更し、bind_infoは既存kapi_pci_bind_infoを維持、その内部だけplatform照会へ。
- kapi_sys.cのユーザー範囲検査→local snap→一度コピーとidx変換を維持。pci_getの40Bも固定。列挙/config/bind状態はPCI driver、BAR/台帳はD3 pci_resourcesのまま。
- L09: device_host.cにnp2_detect/get_version/get_cpu/get_clock/check_hostdrv（元の宣言と同型）と上記6転送。NP2用JSON targetを変更。
- kapi_host.cの入力検査→固定bufferコピー→owner取得→呼出し→出力コピーを保持。stageのconstポインタはlink所有の借用で、次の操作までにコピーする。platformで確保/コピー/キャッシュしない。
- host_owner_exitの呼出位置・owner番号・close順はそのまま。非NP2/未接続の返値も変更しない。L09に旧L07のKCGは含めない。

## 9. L10 — KCG専用KAPI（判断後2時間）
- 現4枠表に3操作を代入することは型・意味・配置を壊す。§12の推奨案では専用3本をplatform薄型口→常駐facadeへ寄せ、ROM読出しの既存4枠表は保持する。
```c
/* 推奨案: 状態は常駐 kcg_scale、mod不在でも従来どおり */
void platform_kcg_init(void) { kcg_init(); } /* scale=1 */
void platform_kcg_set_scale(int s) { kcg_set_scale(s); } /* 1..4へclamp */
int platform_kcg_load_font(const char *p) { return kcg_load_font(p); } /* NOSYS */
/* read_ank/read_kanji は既存 kcg_rom_read_* → public_api() → 4枠表 */
```
- drivers/video/kcg/kcg_module.c:3 の kcg.h を kcg_contract.h に置換する。JSONで3targetを向け直す。KCGの共通宣言を `include/kcg_contract.h` に分離し生成Cのkcg.hを置換。読み出しKAPIの寸法定数も同契約へ。L19のmode port/復旧は触らない。
- 「3本すべてを部品表へ」を必須にする場合の代案: KcgModuleApi末尾にinit/set_scale/load_fontの3枠を追加、4→7枠、版1→2、kernel/kcg_bodyとfacadeを同時更新。
- 代案では旧modを書込み前に拒否、未ロードstubでもscale初期化/clamp/NOSYSを維持する必要がある。import/export契約・生成器・全fixture更新を伴うため、承認なしに代案を実装しない。

## 10. 大きさの見積もりとファイルごとの変更
- i386最適化時の概算。移動は相殺、転送1本5–32B＋整列を見込む。実測値ではない。構造体複製や新操作表は含めない。

|段|常駐kernel差分 B（text/rodata、bss）|容量の判定|
|---|---|---|
|L01|−128..+128、0|契約移動と判定実体の移動。重複リンクなし|
|L02|0..+128、0|既存match表は移動のみ|
|L03|0..+96、+4|IRQ番号1個|
|L04|+32..+256、0|6転送|
|L05a|+48..+384、0|7 KAPI転送、96B契約|
|L05b|+16..+128、0|range/geometry等のFS照会、一時値はstack|
|L06|+16..+160、0|4転送|
|L08|+16..+192、0|4転送、PCI型移動|
|L09|+64..+448、0|11転送、buffer増設なし|
|L10推奨|+16..+128、0|3転送。代案は別見積もり/部品版変更|
|合計|+80..+2048、+4|整列込みの管理予算を **+4096B** とする|

- kernel上限は `include/memmap.h:230` のMEM_KERNEL_IMAGE_MAXと `build/os32.ld` ASSERT。番地・帯域・配置世代を変えず、各段でresident_endとbin実サイズを基点比較する。
- 起動FDは `build/image.mk:35` の1474560B固定。MINIMALも同じBOOT集合。圧縮率/空きクラスタは未測定なので「収まる」と断言せず、実物FD生成・MINIMAL包装・check-packages-hostで上限と集合一致を確認。
- kdebug.binの直接追加は0B見込み（共通型移動のみ）。共有判定のリンク変更で増減し得るので実サイズと現行image_max/mem_max検査を記録し上限を変更しない。容量判定未実施は未決仕様ではなく実装時測定。

|段|新規ファイル（概算行数）|変更ファイル（各概算行数、生成物は再生成）|
|---|---|---|
|L01|include/dma_contract.h 45、pcm_contract.h 60、fm_contract.h 60、lan_contract.h 80、lib/dma_contract.c 25|drivers/dma/dma8237.h 25・dma8237_math.c 20・pcm_cs4231.h 60・fm.h 60・e82557.h 80、mm/dma_pool.h 2・dma_pool_math.c 2・snd_engine.c 2・pcm_module.h/fm_module.h/lan_module.h 各3、fs/serialfs/serialfs_session.c 2、exec/exec.c 2、kernel/ver_collect.c 2、sdk/kapi.json 3、build/kernel.mk 3、tools/prototypes/{pcm,fm,lan}_imports.def 各2–5、kernel/module/kdebug_imports.def 7、hwdev/hwdev_imports.def 該当source行|
|L02|include/pci_contract.h 150、platform/pc98/lan_match.c 20|drivers/net/e82557/lan_module.c 15・lan_module.h 5、drivers/net/e82557/e82557.h 3・pci.h/pci_bind.h 各30–100、tools/prototypes/lan_imports.def 1、build/kernel.mk 2|
|L03|なし|drivers/sound/cs4231/pcm_module.c 15、drivers/sound/cs4231/pcm_cs4231.c 0–5（既存IRQ引数を維持）|
|L04|include/platform_device.h 25、platform/pc98/device_loop.c 30|kernel/v86/v86.c 12、sdk/kapi.json 5、build/kernel.mk 2|
|L05a|include/ide_contract.h 70、platform/pc98/device_ide.c 45|include/platform_device.h 15、drivers/ata/ide.h 70、sdk/kapi.json 12、build/kernel.mk 2、disk_auth_host.c 2|
|L05b|なし|platform/pc98/device_ide.c 20、include/platform_device.h 10、fs/ide_limits.h 3・ext2_super.c 15・ext2_fmt.c 20・fatfs_vfs.c 15・fatfs/diskio.c 12・fatfs/diskio_os32.h 2、§11のIDE fixture/コピー一覧各5–40|
|L06|platform/pc98/device_fs.c 25|include/platform_device.h 8、fs/vfs.c 1、sdk/kapi.json 6、build/kernel.mk 2|
|L08|platform/pc98/device_pci.c 25|include/platform_device.h 8、kapi/kapi_sys.c 5、sdk/kapi.json 5、build/kernel.mk 2|
|L09|include/platform_host.h 25、platform/pc98/device_host.c 55|kapi/kapi_host.c 8、include/platform_device.h 10、sdk/kapi.json 7、build/kernel.mk 2|
|L10推奨|include/kcg_contract.h 55、platform/pc98/device_kcg.c 15|drivers/video/kcg/kcg.h 55、drivers/video/kcg/kcg_module.c 2、sdk/kapi.json 5、build/kernel.mk 2|
|共通試験|tools/tests/test_dep_dir_a.py 180、dep_dir_a_host.c 250、fixtures/dep_dir_a/基点列/ABI寸法 80、tools/check_map.d/check-dep-dir-a-host.yaml 25|下記既存test_*.py/host.c各5–40、sdk生成のkapi_generated.c/公開ヘッダ/初期化includeは再生成差分確認|

- sdk/kapi.json の includes 差分: L01 fm.h→fm_contract.h、pcm_cs4231.h→pcm_contract.h。L04 loop_dev.h→platform_device.h（以後共用）。L05a ide.h→ide_contract.h とし :447,1204 の sizeof(IdeInfo) を維持。L05b/L06 は追加差分なし。L08 pci.h→pci_contract.h。L09 np2sysp.h を除去し platform_device.h の宣言を使用。L10 kcg.h→kcg_contract.h。L02/L03もJSON includesの追加差分なし。
- 生成追従: kdebug_imports.def の新sourceに合わせ hwdev/hwdev_imports.def の該当sourceも更新し、gen_kdebug_imports.py・gen_hwdev_contract.py・各部品のgen_sqlite_imports.py経由の出力を再生成する。世代/CRCの基点との差を記録し、make allで宣言所在・hwdev定義一致を確認。旧世代拒否と対応世代の組合せ成功を試験し、型配置/スロット列は従来値を固定する。

## 11. 受入 → 試験・変異、利用者回帰、段の分け方
- H=host、N=NP21/W。各段2時間以内は実装＋関連host検査まで。Nは段ごとに統合基点で行い証拠を保存、未実施を合格扱いしない。新試験は段ごとに追加して単一巨大試験にしない。
- 全段N: 停止→隔離画像へ配備→起動、FD/HDD、部品ロード一覧・boot.log順、入力・シリアル・LAN、kselftest、**host agent起動＋host_test**。画面の目視を機能合否にしない。
- L02/L08のNP21/WにはPCIがない。Nは不在経路のみ合格を付け、82557装置経路の実機確認は次の立会いへ明記して持越し。目視不要、シリアルで列挙/bind/通信/停止を判定。DEFERRED_TESTS に `DEPDIR-A-PCI | L02/L08 | 82557搭載PC-98で次回立会い | 列挙/config/bind→送受信→停止のシリアル基点列比較 | 未実施` の行を追加し、実機証拠が揃った時だけ解消する。
- 機能はH/Nで受入（ユーザー決定2026-10-28）。他段の実機目視も次回立会いのついでであり段の機能受入を待たせない。実機IDEはIDENTIFY/読出しだけ、書込み試験は隔離NHD。

|段／受入項目|H / N（必要時の次回実機）|殺す変異（各行1件）|
|---|---|---|
|L01 制約/ABI/include|H: 0/64KiB境界/16MiB上限/overflow、sizeof/offsetof/slot列、check-h3。N: FD/DMA/音/LAN|dma_range_okの64KiB跨ぎ拒否を無効化|
|L02 match/寿命|H: ID/class一致と不一致、DECLINE/QUARANTINE、再bind、旧世代modのpolicy拒否/対応世代の組合せ成功。N:不在。実機:bind→送受信→停止|機種表のdevice IDを1bit変更|
|L03 IRQ/ref|H:登録失敗/別IRQ解除拒否/解除失敗保持/再load。N:再生→停止→再load、停止後IRQ不在と参照回収|成功時irq_line保存を誤値へ|
|L04 loop/V86|H:全引数/NULL出力/返値/CHS/LBA列。N:attach→V86起動/試験→復帰→detach、未attach|LBA転送のslotを別値へ|
|L05a IDE契約/KAPI|H:96B ABI、7転送、out検査/write_allowed/エラー。N:隔離NHDでKAPI読書き・info照合|read_sectorのlbaを誤転送|
|L05b FS幾何/fixture|H:geometry全欄、256/512、範囲末端/不在、既存fixture回帰。N:隔離NHDで読書き・再mount・内容照合|geometryのphys_sector_sizeコピーを誤値へ|
|L06 format/session|H:write_allowed拒否/エラー転送/引数順。N:捨て画像format_at→再mount、session開始/終了/再接続|format_atのstart_lbaとlengthを交換|
|L08 PCI診断|H:40B/診断構造体、idx/config引数、snapコピー、不在。N:不在。実機:列挙/config/bindの基点列比較|cfg_read32のregを誤転送|
|L09 NP2/host|H:非NP2、owner別open/status/read/write/close/exit、stage寿命。N:NP2情報、host_test、切断/owner終了/再接続|owner_exit転送を同型の無操作stubへ|
|L10推奨 KCG|H:未load/失敗時もscale=1/clamp/NOSYS、ROM4枠stub。N:scale上下限/字体読出しbyte比較|set_scale転送を同型の無操作stubへ|
|全段 ABI/生成|H:基点の番号/版/署名/out/in/構造体列と比較、再生成差分、target/body実呼先検査。N:既存KAPI利用bin起動|bodyの呼先を旧driverへ戻し境界検査を失敗させる|
|全段 変異基盤|H:コピー先に新ヘッダ/実装を含め通常版と変異版を-Werrorでビルド。置換前にsource.count(old)==1必須|copy対象から新ヘッダを1個除外し、準備検査が欠落を検出|

- 変異は未使用変数を作らない式置換または引数を消費する同型stubで実装。コンパイル失敗を「変異を殺した」に数えず、通常版合格→変異版build成功→期待assert失敗を別々に記録する。
- 基点の期待値/呼出列はb400acb6から採取してfixtureへ固定（B方式）、試験実行時のgitや変更後コードから期待値を生成しない。列移動で旧期待値を追従改変しない。
- 既存利用者: test_dma_pool.pyのdrivers/dma/dma8237_math.c同梱/リンクを新共通実体へ更新、test_dma8237.pyにも同実体をリンク。古い定義を残して二重シンボルにしない。L01で test_h4_backing.py:19,89 と dma_boot_host.c:22 の dma8237_math.c コピー/リンク/include も新共通実体へ追従する。
- test_lan_module.pyとlan_module_host.cはe82557_driverの常駐所在検査を維持し、platform表をリンク。test_pcm_module.py/pcm_module_host.c/playback_host.cはirq_lineと新ヘッダをfixtureへ渡す。
- L10で kcg_boot_host.c とそのコピー/ビルド一覧に kcg_contract.h を追加。L04で v86_low_host.c:124 の detach stub を platform_loop_dev_detach に追従する。test_kcg_module.py、kcg_body_host.c/display_host.c、KAPI layout/audit/out/bounds/ranges、test_gen_kapi_cli.pyを回帰。KAPI body文字列依存の検査/差し替え定義は現呼先に更新し、検査目的を弱めない。
- FS/IDEのstub差し替えはplatform口で行い、別の転送試験では実device_ide.c＋旧IDE stubを使う。boot/D2/D3/L12/L13/reclogの既存fixtureコピー一覧とcheck_select依存登録にも新契約を追加して回帰。
- L05bのIDE fixture追従対象: tools/tests/{b8_open,ext2_empty_name,ext2_part,ext2_write_io,fatfs_stat,fep_standard,vfs_fd_path,fdc_track,dep_dir_d3_boot,dep_dir_d3_disk}_host.c と hdd_pt_fake.h、各test_*.pyのコピー/リンク一覧。FS照会stubはplatform口/値コピーへ変更し、実IDEや既存boot口を試すstubは維持して必要な実転送をリンクする。
- disk_auth_host.c:44–53 はL05aで書込み2本、L06でformat2本の差し替えを platform_* 名へ変更し、write_allowed/NULL/count==0の既存検査を保持。新試験の --mutate は check-ci-stab にも登録し、通常/変異の再実行安定性を確認する（変異と全体検査はPM取り込み時）。
- 各段: make all、追加/変更試験MUTATE=0、check_select.py --lint。新変異の上表実行はPM取り込み時。取り込みcheck-changed、群A終端で全体検査、FD/MINIMAL/kdebug容量を再確認。
- 順序: L01→L02→L03→L04→L05a→L05b→L06→L08→L09→L10を既定。**実依存はL05a→L05b→L06**、共通PCI契約をL02で置く本設計ではL02→L08もある。
- L01とL02/L03/L10は型編集の競合回避順。L04/L05a/L09の独立host作業は並列可、JSON/include/生成/ビルド一覧編集は直列。V86のL19とL04、KCGのL19とL10も直列統合。ゲストは常に1基点。

## 12. 未決（ユーザー判断だけ）
1. **L10の「専用3本を部品関数表へ」と「既存部品ABIを維持」の両立不可。推奨: §9の薄いplatform→常駐facadeを例外として採り、4枠表はROM読出しに限定して維持。** init/scale/fontは現状ROM部品の機能ではなく、特にfontは常時NOSYS。3枠追加案を選ぶなら部品ABI版更新・旧mod拒否・同時配備を別途承認する。専用3本も表へ通す要求を満たしたとは、推奨案の承認前には記録しない。

反論: なし（0件）。

## 確定 (2026-10-28、Opus 5.5 の第 2 回 (軽微な要修正) を PM が反映 — 本文の該当の行より優先する)

- **B1 (P2、PM 決定)**: pci の契約は **2 本に分ける** — `include/pci_dev_contract.h` (struct pci_dev と PCI_DEV_STRUCT_SIZE などの公開定数だけ、drivers/pci/pci.h が読む) と `include/pci_bind_contract.h` (struct pci_driver・struct pci_bind_info などの bind と診断の型、drivers/net/e82557/lan_module.c・platform/pc98 の機種の配線・kapi の診断が読む)。drivers/pci/pci.h は pci_dev_contract.h だけを読むので、hwdev の共有ヘッダ群 (hwdev/hwdev_imports.def の pci_cfg_*) の DWARF の型一覧は struct pci_dev だけのままで、tools/hwdev_layout.json と HWDEV_LAYOUT_CRC は変わらない (gen_hwdev_contract.py:118-119 で止まらない)。
- **B2 (P2)**: build/pcm-module.mk・fm-module.mk・lan-module.mk の `*_DEF_SOURCES` に新しい契約ヘッダ (include/pcm_contract.h・fm_contract.h・lan_contract.h など、L01/L02 で作るもの、lan は pci_bind_contract.h も) を足す。§10 の一覧に載せる。増分ビルドで契約だけ変えても世代が作り直されること を host の試験 (または make の依存の dry-run) で確かめる。
- **B3 (P3)**: L01 では hwdev/hwdev_imports.def に触らない (dma・pcm・e82557 の行は元から無い、gen_hwdev_contract.py:43)。§3:80 の「hwdev の def の source も更新」は取り消す。
- **B4 (P3)**: platform_ide_drive_present は L05a で定義する。L05b は platform_ide_range_ok と geometry の照会だけを足す。
- **B5 (P3)**: L08 で pci.h を契約に置き換えるとき、pci_get の out 検査の `PCI_DEV_STRUCT_SIZE` (今は drivers/pci/pci.h:63) を pci_dev_contract.h に移す公開定数に入れる。
- **B6 (P3)**: 順序に「群 D の L19 → 群 A の L10」(drivers/video/kcg/kcg_module.c を同時に触らない、PM)。
- 回帰の一覧に test_hdd_stage1・test_cd_read を足す (L05a/L05b の境目)。
- 世代の扱い (PM 決定、REVIEW.r1.md): PCM・FM・LAN の部品の ABI の世代と KDEBUG_ABI_GEN は変わってよい (カーネルと対の部品で hsync boot で常に組で配る)。B1 の分け方で hwdev の契約 CRC は変えない。

## L04 の実装で埋めたこと (2026-10-28、PM — §6 の「HLE の依存を変えない」を当てはめた)

- 食い違い: §11 の detach の付け替えで `platform_loop_dev_detach` を呼び先にすると、v86.mod (HLE) が import する旧名 `loop_dev_detach` が未解決になる。§6 は HLE の依存の変更を禁じている。
- 決定: **旧い呼び出しを残す** — drivers の `loop_dev_detach` はそのまま (v86.mod の import と部品の ABI は変えない)。`platform_loop_dev_detach` は KAPI の target 用の薄い関数で、中で `loop_dev_detach` を呼ぶだけ。attach・status も同じ形 (HLE が import する名前は残し、KAPI の target だけ platform の薄い関数へ)。
