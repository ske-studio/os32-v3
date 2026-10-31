# hwdev/gf/save.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/save.c`

局所退避は context の有限表に書換え前に `gf_init_save` / `gf_init_save_indexed` で登録し、失敗時は adapter の `restore(opaque, ctx)` が逆順に戻す（indexed は選択情報も保持）。RESET は184Cを、CR_INDEX_ADDRESS_LATCHED はラッチと元のindexを明示的に復元し、命令完了時に `gf_init_commit` で解除する。PLL2/IO_RESTRICT_PLL2 と P3 PLL は列全体の成功時だけ解除し、正常 stop で完了済みPLLを戻さない。
RESET 強制実行/184C 復元

## 移し替えた設計

- 前処理の根拠: `N/subdev/devinit/nv04.c:405–414`（PMC bit0・owner）、`R/xf86-video-nv/src/nv_hw.c:33–48`（CR1F=57h、CR11 bit7クリア）。元の index/owner/lock を記録する。
