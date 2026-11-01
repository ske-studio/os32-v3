# gfx/gfx_osri.c

`osri_load` は VFS からヘッダー、パレット、4 プレーン、イベントを順に読み、形式と CRC を検査して FD を閉じる。短い正の読みは継続し、失敗時は負値を返す。バッファは呼出側が失敗後に描き直し、検査前に表示しない。契約は [確定設計 §3・§5](../../decisions/gui/RASTER_SPLASH_DESIGN.md#3-osri-形式-v1)。
