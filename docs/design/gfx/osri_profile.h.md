# sys/kern/gfx/osri/osri_profile.h — 1 行の境界で出す OUT の本数の上限 (B) の正典

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `sys/kern/gfx/osri/osri_profile.h`・`tools/osri_profile.py`

## 何をするか

`OSRI_DEFAULT_OUT_BUDGET` (いまは `31u`) は、起動のスプラッシュの画像で 1 つの可視行の境界 (HBLANK) に出すパレットの OUT の本数の上限 B。
ゲスト (準備器・再生器の行の配列・形式の容量の計算) とホスト (エンコーダーの既定・資産の検査) の両方がこの 1 か所を読む ([C4] の Subsystem 層)。

## 本数の数え方 (ゲストとホストで同じ)

- 1 行の費用 = 変わった G/R/B 成分の数 C + max(M−1, 0)。M はその行で色が変わる番号 (パレットの index) の数。
- 先頭の番号の OUT (A8h) は前の行の表示中に先に出すので数えない。2 つ目以降の番号の OUT は数える。
- 成分は G→R→B の順で、変わった成分だけ出す。同じ色への書き換え (同値イベント) は 0 本。
- 毎フレームの初期 16 色 (64 OUT) は VBLANK で書くので B の外。
- 例: 1 成分 × 16 番号 = 16 + 15 = 31 本 (B31 でちょうど許可、もう 1 成分足すと拒否)。

## B=31 の根拠 (実機 Ra266・31kHz の観測)

- A0h の bit6 の周期 約 31.8µs・高の幅 約 5.9µs、IN 約 1.15µs、OUT 平均 約 0.65µs (観測上限 約 0.78µs)。
- 毎行 N 色を書く観測の像では、15 本は崩れなし、47 本で左端に筋、51 本から全体が崩れた。
- 可変の OUT の 8 枚 (最大の実 OUT 28) は各 351 フレームで drop 0。配布の画像は最大 25 本。
- 31 は実機で確かめた範囲。47 はホストの比較だけの上限で、実機での再生は保証しない。

## 不変条件と、変えるときに止まる所

- `tools/osri_profile.py` は `#define` を 1 つだけ厳密に読む。欠落・重複・1〜47 の外は例外で、31 への fallback は無い。比較の上限 `OSRI_COMPARE_OUT_LIMIT = 47` はこの Python のモジュールだけに置く。
- ゲストは `STATIC_ASSERT` で B==31 を固定している (`sys/kern/gfx/osri/gfx_osri.h` の `osri_default_budget_b31`、`sys/platform/pc98/video/splash_raster_play.h` の行 64B・plan 25,608B、`splash_raster_play.c` の emit 表の長さ B+1)。値を変えるとビルドが止まるので、行の配列と手書きの emit 表を一緒に直す。emit 表の生成器は作らない。
- 形式の上限 (行 16 番号・全体 6,384 件) は B から導かない (同値イベントは 0 本なので)。B を下げるときは容量と同値の互換を設計し直す。

## 検査

- `tools/tests/test_osri_profile.py` (osri-profile): 正典の読み取り、CLI の振り分け、無指定 = 明示 B31 の 1 本の出力。
- `tools/tests/test_osri.py`・`test_osri_enc2.py`・`test_osri_enc2_core.py` もこのヘッダを covers に持つ。
