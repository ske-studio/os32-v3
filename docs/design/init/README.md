# init/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D3_DESIGN・DEP_DIR_D_DESIGN (D1 で移した)
ソース: `init/`

### boot_display

tvram_print と tv_cat 系は内部 boot_display.c/.h に移して共用し、表示コードを複製しない。

```c
/* init/boot_display.c: 既存名・引数・呼出側を残す */
void tvram_print(int x, int y, const char *s, u8 attr) {
    platform_boot_puts(x, y, s, attr);
}
```

### boot_disk

```c
struct boot_disk_ops {
    int (*valid)(int drive);             /* 番号範囲。存在とは別 */
    int (*bios_da)(int drive);           /* 0/1→80h/81h、他→0 */
    int (*report_ata)(int drive, struct bootinfo_ata *out);
    int (*default_geom)(int drive, u16 *heads, u16 *spt);
    void (*fill_ata)(int drive, HddGeom *out);
};
```

### bootinfo

- bootinfo.c は表ポインタだけ保持。IDE 固有型/定数/addr_mode 判定を boot_disk.c に移し、IDE の状態は IDE が所有。照会は起動前景または KAPI の協調文脈、IRQ 内では呼ばない。
bootinfo_report は BIOS 行→IDENTIFY 行（BOOTINFO_NDRIVES=2）の順・書式を維持。
- part_geom: BIOS DA 対応→bootinfo_hdd_geom が成功なら BIOS、それ以外は default_geom（元の ide_get_info、heads/sectors 非ゼロ検査）→IDENTIFY、失敗 -1。bank1 は BIOS DA=0 でも IDENTIFY fallback 可。
- hdd_geom_info: out NULL/番号範囲を先に検査→ゼロ初期化→BIOS 情報→fill_ata。fill_ata は元の get_geom 成功かつ present の場合だけ既存 ATA 欄/addr_mode を埋める。不在でも有効番号なら成功・ATA 欄ゼロ。返値と NULL 出力の扱いを維持。
- 表未登録時の内部照会は失敗（valid=偽、part=-1、report ATA なし）。本番は bind が全照会に先行する。hdd_geom_info の ABI wrapper と slot は変更しない。

### boot_splash

```c
/* 終了のkernel側: clear_pages→gfx_shutdown→palette_init→tvram_clear */
```

- glyph配列、配置、整数HSV、gradient、palette設定、dirty矩形、3秒/tickループ、終了時期はinit/boot_splash.cに残す。

## 索引

- [init/boot_platform.c](boot_platform.md)
- [bootlog.md](bootlog.md)
- [init/kernel.c](kernel.md)
- [kernel_gfx_config.md](kernel_gfx_config.md)
- [sysconfig.md](sysconfig.md)
