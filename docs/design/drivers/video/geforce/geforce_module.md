# drivers/video/geforce/geforce_module.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `drivers/video/geforce/geforce_module.c`

RAM CLIENT 300 KiB と BAR1+[0,4B000h) の DISPLAY を SURFACE 台帳へ別登録し、`bb_base/bb_size/bb_pitch` と framebuffer API は CLIENT を返す。**PEGC と GF の CLIENT は一方だけ予約**し、600 KiB の二重確保を避ける（F10）。
既存 loader/Cirrus 登録を使い、NULL 加速 callback と非NULL prepare を許す（`O/drivers/video/cirrus/cirrus_module.c:22–43`）。kdebug 依存の hwdev.mod を名称変更するだけでは済ませない（`O/kernel/module/hwdev_module.c:87–96`）。
本番 context/復帰 callback は起動中保持して module を pin し、正常終了後も予約は保持する。owner の核移設、共通装置 registry、N1 lease の再設計は行わない。
CLIENT は PEGC/GF のどちらか一つの 300 KiB。BAR1 の DISPLAY は MMIO で RAM の CLIENT と別。

CLIENT は PEGC/GF 一方だけ、pc98 fallback、decode不明時98無書込み、shutdown→再init、部品欠落/KDEBUG=0。
