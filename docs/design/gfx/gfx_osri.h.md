# gfx/gfx_osri.h

`.osri` v1 の配置、画像記述、内部 loader 宣言を定義する。形式とイベント順序の正典は [確定設計 §3](../../decisions/gui/RASTER_SPLASH_DESIGN.md#3-osri-形式-v1)。構造体は配置の検査に使い、ファイルの直接 cast はしない。

プレーン容量はモード選択時の `gfx_mode_fits` による台帳検査を前提とする。
