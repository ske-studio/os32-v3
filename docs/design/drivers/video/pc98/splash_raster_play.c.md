# drivers/video/pc98/splash_raster_play.c — 画像の 1 フレームを H の辺を数えて再生する

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `drivers/video/pc98/splash_raster_play.c`・`drivers/video/pc98/splash_raster_play.h`

## 口の契約

- `pl_v2_frame(state, image, plan)` は 1 回の呼び出しで新しい VBLANK を 1 回だけ使い、1 フレームを再生する。
- 戻り値は `PL_FRAME_OK` (0)、`PL_FRAME_DROP` (1、このフレームは途中で打ち切り、次のフレームで初期色から戻る)、`PL_FRAME_NO_SYNC` (-1、待ちの上限に達した)。
- `pl_v2_frame_poll(..., poll, ctx)` は VBLANK を待った後に poll を呼び、非 0 なら `PL_FRAME_CANCEL` (2) を返す。ビューアーの中止に使う。
- 前提は state の checked と dynamic_ok と、入口が IF=1 であること。違えば何もせず NO_SYNC。入口の IF はどの出口でも元に戻す。
- 起動 (`platform/pc98/splash_image.c`)・観測の像 (`platform/pc98/splash_obs.c`)・ビューアー (`drivers/video/pc98/backend_pc98_osri.c`) が同じ関数を使う。

## 1 フレームの手順

1. IF=1 のまま、bit5 が 0 になり、次に 1 になる (新しい VBLANK) まで待つ。
2. CLI。まだ VBLANK の中か確かめる (IRQ で遅れて抜けていたら DROP)。
3. 初期 16 色を 64 OUT で書く (番号→G→R→B)。書き終えてまだ VBLANK の中か確かめる (F を越えていたら原点を失ったので DROP、位置を合わせ直さない)。
4. bit5 が 0 になるのを待つ (F)。この後の H (bit6) の low→high の新しい辺を k=1 と数える。開始時に high なら辺に数えない。
5. k = active0+y の辺で行 y の列を出す。各行では、H が low になった (再武装) ときに先頭の番号を A8h へ出し、次の high で準備済みの列を出す。空の行も辺を数える。
6. 行 399 の後にもう 1 辺待って可視の終わりまで保護し、入口の IF に戻す。

- 待ちは同じ 1 回の IN の値で bit5 と bit6 の両方を見る。可視の途中で VSYNC が見えたら、そのフレームは DROP にする (1 フレームを越えない)。
- 列は `emit_table` の 0〜31 本の手書きの展開 (`PL_EMITTER`) で、H の前に選んでおく。H の中で色の展開・探索・時計・診断はしない。
- 待ちは `PL_PLAY_POLL_LIMIT` (1,000,000 回) で止まって NO_SYNC。時計は読まない。

## 時間の予算と測り方

- IF=0 の長さは VBLANK の残りと可視 400 行で、31k は約 12.71ms+α、24k は約 16.11ms+α。
- 1 行の列は HBLANK に入りきる前提 (B=31、[osri_profile.h](../../../gfx/osri_profile.h.md))。HBLANK を越えた OUT はその行の左端の色が遅れて見える。列が長すぎて H の辺を飛ばしたときの行ずれは、時計を読まないので検出できない。
- 実機の成否はカメラ (表示・崩れ) とシリアル (frames・drop) で判断する。NP21/W の成功は実機の時間の証拠にしない。

## 落とし穴

- IF=0 の間に PIT の割り込みを 1 回ぶんしか受けられないので、100Hz の tick が遅れる (31k で 1 フレーム約 1.4 tick に対して記録は約 1 tick [推])。tick は補正しない (ユーザー決定 2026-11-04「補正しない」)。期限の管理は [splash_raster_run.c](splash_raster_run.c.md) が実 tick とモデルの時間の大きい方で行う。
- 長い CLI の間はシリアルの受信を取りこぼすことがある。記録はするが、画像の採否の条件にはしない。

## 検査

- `tools/tests/test_splash_obs.py` (splash-obs-host): 独立の時計を持つ I/O の模型で、表示中の先頭番号・G→R→B の順・空の行・H の再武装・VSYNC での DROP・待ちの上限での停止・全出口での IF の復元・次フレームの初期色の復元を確かめる。通常と観測で同じ列になること、32 本以上の emit がリンクされないことも見る。先頭番号・再武装・VSYNC・IF の各削除は実行時 RED。
- `tools/tests/test_splash_wire.py`・`test_splash_v2.py`: 起動の結線の中で使う。
