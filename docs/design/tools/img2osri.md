# tools/img2osri.py — 1 枚の絵を `.osri` v2 に変換する入口

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `tools/img2osri.py`

## 使い方

- 既定: `python3 tools/img2osri.py INPUT -o OUTPUT.osri` で、enc2・B=31・band=1 の v2 を 1 本出す。明示する形は `--encoder enc2 --outs 31 --band 1 --threads N` (threads の既定は論理 CPU 数、1 は単一スレッド)。
- B は `--outs` で 15/23/31/47 から選べる (`osri_enc2_spec.BUDGETS`)。既定の 31 は [sys/kern/gfx/osri/osri_profile.h](../gfx/osri_profile.h.md) を読む。47 は比較用で、実機の再生は保証しない。絵を見て B や方式を自動で変えることはしない。
- enc2 の本体は [osri_enc2](osri_enc2.md)。この入口は引数を検査して `osri_enc2_native.encode_image` を呼ぶだけ。

## 旧方式 (互換のために残す)

- `--hblank-outs 5|6` か `--encoder pl-b`: 1 行ごとの貪欲な探索 (案 B、`optimize_row`) で、1 行 5/6 OUT の画像を作る。旧画像とバイト一致を保つ。
- `--obs-colors N` (1〜16): 観測の像のための画像。毎行ちょうど N 枠を選んで書き換える (同値の書き換えも残す)。band=1 だけ。
- 混ぜられない組: enc2 と `--hblank-outs`/`--obs-colors`、enc2 と band≠1、`--outs` と `--obs-outs`、旧方式と `--outs`/`--obs-outs`/`--threads`。どれも引数の誤りで止まる。

## 共通の前処理と出力

- 透明な部分を黒に合成し、縦横比を保って 640×400 に縮め (LANCZOS)、黒の余白で中央に置く (`resize_image`)。
- RGB888 → RGB444 は各成分 `(v*15+127)//255` の整数の丸め。
- 画素は、確定した各行のパレットへ整数の Floyd–Steinberg (右 7・左下 3・下 5・右下 1、分母 16、負は 0 の方向へ切る、左→右、行の間で持ち越す) で割り当てる。最近色の同点は小さい index。
- 出力は v2 のヘッダ・初期パレット・4 プレーン・イベント。CRC は `zlib.crc32`。形式は [sys/kern/gfx/osri/gfx_osri.h](../gfx/gfx_osri.h.md)。
- 標準出力に件数・最大 OUT・B か N・CRC・Pillow/NumPy の版・引数を 1 行出す。プレビューの PNG は作らない (ユーザー決定「プレビューは消して良い」)。

## 検査

- `test_osri_profile.py (旧版 158bfa11af3e)`: 引数の振り分けと、無指定 = 明示 B31 の出力。
- `test_osri.py (旧版 5d240bea3019)`: 旧方式の変換と、誤差拡散・プレーン配置・CRC。
