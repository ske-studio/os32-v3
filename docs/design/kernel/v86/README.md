# kernel/v86/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_C_DESIGN・DEP_DIR_D1_DESIGN・DEP_DIR_D_DESIGN・MEMMAP_V3 (D1 で移した)
ソース: `kernel/v86/`

### v86_bios

|状態・値|所有者|文脈|
|---|---|---|
|BDAの保存領域|既存v86側|platformはv86_bios_real_lowmemを呼ぶだけ|

- 起動 FD は kernel の fdc_init→媒体設定→mount を維持。V86 FD は INT 1Bh→loop→VFS を維持し、背後のファイルが物理 FD 上ならこの FDC 経路で読む。V86 中の IRQ11 でもホスト通知だけ行う。

### v86_mem

V86失敗はline→残列空白の順で同口を使う。

低位 640KB は V86 用などに解放してよい

**V86 セッション中だけ** `v86_mem.c` の既存機構どおり master の低位 (ページ 0 は実物、アリーナ 0x1000〜0x9FFFF、VRAM 0xA0000〜0xBFFFF、ROM) に USER を立て、**終了時に supervisor へ復元する** (`v86_mem.c:73,99`、`v86.c:93`)。

| 何 | なぜ連続 | 対処 |
|---|---|---|
| V86 のゲスト空間 | 実アドレス | 低位の固定アリーナ 0x1000〜0x9FFFF (D13、T7a) |

## 索引

- [kernel/v86/v86.c](v86.md)
