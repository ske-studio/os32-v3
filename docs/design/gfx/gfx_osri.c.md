# gfx/gfx_osri.c — `.osri` の読み込みと形式の検査

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `gfx/gfx_osri.c`

## 口の契約

- `osri_load_checked(path, dst, image)` は成功で 0、失敗で `OS32_ERR_*` を返す (NOTFOUND・NOSPC・INVAL・IO を分ける)。ビューアー (`gfx/gfx_osri_view.c`) が使う。
- `osri_load(path, dst, image)` は起動用。検査は同じで、成功 0・失敗 -1 を返す。
- どの出口でも FD を閉じる。メモリの確保はしない。ポート I/O・表示・時間待ちもしない。
- 失敗したとき、借りた BB は途中まで上書きされていることがある。描き直すのは呼び手 (起動は内蔵のロゴ、ビューアーは 0 で消す)。検査が全部通るまで、呼び手は提示しない。

## 手順

1. dst を確かめる。640×400・pitch 80・`GFX_BB_PLANAR4`・4 プレーンが 32,000B ずつ連続していなければ、開かずに INVAL。
2. ヘッダ 64B を読む。CRC 欄の値を控えてから欄を 0 にし、CRC を計算し直す。
3. 版ごとの件数の上限を先に検査し、それから正確な全長 128096+8n と比べる (掛け算のあふれを避ける順)。固定値・予約 0・band 1/2/4・v1 の空画像は band 4、も検査する。
4. 初期パレット (各 ≤0xFFF)、4 プレーン (BB へ直接)、イベントを順に読む。読みは 4KiB 以下の単位で、短い正の読みは残りを読み足す。CRC は読みながら更新し、final は最後に 1 回だけ。
5. イベントごとに、line 1〜399・band の倍数・index<16・予約 0・rgb444≤0xFFF・line の非減少・同じ行で index の重複なし、を検査する。v1 は同値の拒否と同じ行の index 昇順も見る。v2 は 1 行 16 件まで。
6. 末尾に 1 バイトでも余れば拒否する。CRC が合わなければ拒否する。

## 境目

- 1 行の OUT の本数と、再生できるかどうかは見ない。再生の準備器 `pl_v2_prepare` ([splash_raster_plan.c](../drivers/video/pc98/splash_raster_plan.c.md)) が全行を独立に検査し直す。
- 呼び手は起動の `platform/pc98/splash_image.c`、観測の像の `platform/pc98/splash_obs.c`、ビューアーの `gfx/gfx_osri_view.c`。

## 検査

- `tools/tests/test_osri.py` (osri-host): 実ローダーを 32bit の VFS スタブに結合する。非対称な 16 色の模様から期待プレーンを独立に組み、照合する。短い読み・破損・長さ・イベント・v1/v2 の境界 (200/201、6384/6385)・プレーンの順を拒否するか確かめる。`--mutate` は CRC・予約・末尾・プレーン順・ビット順の検査を外し、実行時 RED を確かめる。
- `tools/tests/test_splash_wire.py`: 起動の結線の中で実ローダーを通す。
