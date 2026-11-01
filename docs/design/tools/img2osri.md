# tools/img2osri.py

Pillow で透明部分を黒へ合成し、比率を維持して画面へ収める。固定16色または帯ごとに差分を選ぶ A/B/C を RGB444 に丸め、整数 Floyd–Steinberg で画素を選び、OSRI v1 と比較 PNG を書く。既定は B、固定8色、4行、境界1件、全体100件。`--mode A` と `--mode C` は全16色を可変とし、`--band` が共有範囲を指定する。予算 JSON は `{"events": 100, "per_line": 1}` の形式で、CLI の上限との小さい方を使う。プレビューは出力ファイルを読み直し、行ごとのイベントを適用する。形式と処理は [確定設計 §3・§4](../../decisions/gui/RASTER_SPLASH_DESIGN.md#3-osri-形式-v1)。
