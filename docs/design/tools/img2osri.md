# tools/img2osri.py

Pillow で透明部分を黒へ合成し、比率を維持して画面へ収める。固定16色を RGB444 に丸め、整数 Floyd–Steinberg で画素を選び、OSRI v1 と比較 PNG を書く。動的パレットの最適化は後段で扱う。形式と処理は [確定設計 §3・§4](../../decisions/gui/RASTER_SPLASH_DESIGN.md#3-osri-形式-v1)。
