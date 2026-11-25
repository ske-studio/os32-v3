# sys/kern/gfx/osri/gfx_osri.h — `.osri` 形式 (v1・v2) と画像記述

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `sys/kern/gfx/osri/gfx_osri.h`

## 何をするか

起動のスプラッシュとビューアーが読む静止画 `.osri` の配置と上限、読んだ結果を持つ RAM の画像記述 `struct OsriImage` を決める。
画素は 640×400・4 プレーン (16 色)・RGB444 で、行ごとのパレットの差分 (イベント) の列が続く。圧縮はせず、little endian。

## ファイルの配置

| offset | 大きさ | 中身 |
|---|---|---|
| 0 | 64B | ヘッダ (下記) |
| 64 | 32B | 初期パレット 16 × u16 (`0x0RGB`、上位 4bit は 0)。可視行 0 の色 |
| 96 | 128,000B | 画素。4 プレーン × 32,000B、1 行 80B、左端が bit7。色番号の bit0/1/2/3 をプレーン 0/1/2/3 (B/R/G/I) へ |
| 128096 | 8B × n | イベント `{u16 line; u8 index, reserved0; u16 rgb444, reserved1;}` |

ヘッダの並びは `"OSRI"`、version (1/2)、header_bytes=64、640、400、bpp=4、color_format=1、flags=0、file_bytes=128096+8n、palette_off=64、pixels_off=96、pixels_bytes=128000、events_off=128096、event_count=n、event_bytes=8、reserved_profile=0、band_lines (1/2/4)、reserved0=0、file_crc32 (offset 48)、reserved[3]=0。
CRC は offset 48〜51 を 0 にしたファイル全体の IEEE CRC-32。Python は `zlib.crc32`、C は `lib/crc32_core.inc` を使う。

## イベントの意味と版の違い

- イベントは、その行の表示が始まる前に済ませる差分。rgb444 は変えた後の色。
- 行 0 の変更は初期パレットに畳むので、line は 1〜399。
- line は非減少で、`line % band_lines == 0` を満たす。同じ行の中で同じ index は 1 回だけ。
- reserved_profile は常に 0。走査の周波数 (24/31kHz)・B・N をファイルに入れない。同じファイルを両方の周波数で使う。

| | v1 | v2 (既定) |
|---|---|---|
| 件数の上限 | 200 (`OSRI_V1_MAX_EVENTS`) | 6,384 = 399 × 16 (`OSRI_V2_MAX_EVENTS`) |
| 同じ行の順 | index の昇順 | ファイルの順。エンコーダーが決めた書き換えの順 π を表すので、任意の順でよい |
| 同値イベント | 拒否 | 受理 (OUT は 0 本) |
| 1 行の件数 | 制限なし (全体 200 まで) | 16 番号まで |
| 空の画像 | band=4 だけ | band 1/2/4 |

1 行の OUT の本数 (B) は形式では検査しない。再生の準備器が計算し直す ([osri_profile.h](osri_profile.h.md))。

## RAM の画像記述

- `struct OsriImage { u16 pal[16], count, band, version; struct OsriEvent event[6384]; }` は 51,110B (`STATIC_ASSERT`)。
- 件数の配列長は `OSRI_IMAGE_EVENTS`。B から求めた 399×min(16,(B+1)/2) と v1 の 200 の大きい方で、B31 では 6,384。
- 持ち主が heap に置く (スタックに置かない)。画素は持たず、呼び手が借りた BB へ直接読む。
- 公開 SDK の `GFX_RasterPalEntry` (r/g/b 別バイト) とは別の型で、相互に変換しない。公開 KAPI と世代は変えない。
- ファイルを構造体へ cast しない。`le_read*` で 1 項目ずつ読む。構造体は配置の静的検査 (大きさと offset 48) にだけ使う。
- プレーンの容量は、モードを選ぶときの `gfx_mode_fits` の台帳検査が前提。

## 落とし穴

- `.osri` は拡張子が 4 文字で、FAT の 8.3 に入らない。起動 FD と MINIMAL には画像を入れない。
- `OSRI_MAX_EVENTS` は v1 の 200 の別名として残っているが、使う所は無い。
