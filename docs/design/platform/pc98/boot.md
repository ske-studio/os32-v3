# platform/pc98/boot.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D3_DESIGN (D1 で移した)
ソース: `platform/pc98/boot.c`

root は FD「種別」判定でなく **BOOT_DRIVE_FDD/FDD_144 との完全一致**。
- FDC 媒体指定は全起動で `fdc_set_media_by_daua(0, boot_drive)`。144 表示だけ TYPE_MASK と ALT を使う。root 判定と統一しない。

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

HostDrv は root 成功後だけ呼び、検出なし/HDRV --、mount 失敗/HDRV NG と継続を維持。

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

`pc98_boot_bind` の中から D2 の `pc98_d2_bind` を呼び、kernel.c での呼出しを 1 つにする
