# drivers/video/pc98/splash_raster_plan.c — 画像の全行の OUT 列を前もって作る

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `drivers/video/pc98/splash_raster_plan.c` (型と宣言は `drivers/video/pc98/splash_raster_play.h`)

## 口の契約

`int pl_v2_prepare(const struct OsriImage *image, struct PlV2Plan *plan)` は成功で 0、失敗で -1 を返す。
読み込んだ画像 (v1・v2) の全 400 行を 1 回だけ走査し、行ごとに HBLANK の中で出す OUT の列を作る。
VRAM への転送より前に呼び、失敗したら転送しない (起動はロゴへ、観測の像は status=prepare で次の画像へ)。
image と plan は呼び手が heap に確保し、この関数は借りるだけ。

## データの形

- `struct PlV2Row { u8 first, count; u8 port[31], value[31]; }` は 64B。first はこの行で最初に変える番号で、前の行の表示中に A8h へ先に出す。port/value はその後に HBLANK で出す列 (count 本、0〜31)。
- `struct PlV2Plan { struct PlV2Row row[400]; u32 max_outs, total_outs; }` は 25,608B。row[0] は常に空 (行 0 の色は初期パレット)。
- 配列の長さは B ([osri_profile.h](../../../gfx/osri_profile.h.md)) で決まり、`STATIC_ASSERT` で固定している。

## 列の作り方

- 初期パレットから始め、各行のイベントをファイルの順に適用する。
- 前の色と同じ色へのイベント (v2 の同値) は何も出さない。
- 変わる番号ごとに、2 つ目以降なら番号の OUT (A8h) を足し、続けて変わった成分だけを G (AAh)→R (ACh)→B (AEh) の順に足す。
- 項目を足す前に、累積が B (31) と配列の大きさを超えないか確かめる。超えたら -1。
- 行と行の間の空の行も `count=0` の行として持つ (再生側は空の行でも H の辺を数える)。

## 検査すること (ローダーと独立に)

- 版は 1 か 2、band は 1/2/4、v1 は件数 200 以下で空なら band 4、全体の件数は `OSRI_IMAGE_EVENTS` 以下。
- 初期パレットとイベントの rgb444 は 0xFFF 以下、index<16、予約 0、line は band の倍数。
- 同じ行で同じ index は 1 回。v1 は同じ行の index 昇順と同値の拒否も見る。
- 行の順に並んでいないイベントや、余ったイベントがあれば -1。

## 互換

- 旧方式で作った画像 (1 行 5/6 OUT・固定画像・v1) も、実 OUT が 31 以内ならそのまま再生できる。B47 で作った画像も、実 OUT が 31 以内なら受理する。
- 公開の表 (`GFX_RasterPalTable`) はこの準備器を通らない。

## 検査

- `tools/tests/test_splash_obs.py` (splash-obs-host): 実 C で全 400 行の first/count/port/value を独立の復号と照合する。16 番号 1 成分 = 31 本の許可、もう 1 成分で 32 本の拒否、47/48 本の拒否、同値 16 件 = 0 本、空の行・持ち越しを確かめる。
- `tools/tests/test_splash_v2.py`・`test_splash_wire.py`: 起動の持ち主から実際の準備器を呼ぶ。
