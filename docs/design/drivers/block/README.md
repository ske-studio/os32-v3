# drivers/block/README — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D1_DESIGN (D1 で移した)
ソース: `drivers/block/README`

- 起動 FD は kernel の fdc_init→媒体設定→mount を維持。V86 FD は INT 1Bh→loop→VFS を維持し、背後のファイルが物理 FD 上ならこの FDC 経路で読む。V86 中の IRQ11 でもホスト通知だけ行う。
