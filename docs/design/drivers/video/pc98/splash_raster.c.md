# sys/platform/pc98/video/splash_raster.c — 走査の判定・固定色・公開の表の再生

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `sys/platform/pc98/video/splash_raster.c`

## 何をするか

1. `pc98_splash_raster_begin`: 24/31kHz と、VSYNC の後の可視行の位置 (active0) を判定する。
2. `pc98_splash_raster_frame`: 固定画像 (イベント 0 件) の 16 色を新しい VSYNC で 1 回書く。
3. `pc98_splash_raster_table`: 公開 KAPI `gfx_present_raster` と内蔵のロゴの表 (`GFX_RasterPalTable`) を 1 フレーム再生する。

VRAM・FS には触らない。VRAM の転送は `gfx_vram.c`、画像の行ごとの再生は [splash_raster_play.c](splash_raster_play.c.md) が持つ。

## begin の判定

- 入口が IF=0 なら NO_SYNC を返し、checked を立てない (次に IF=1 で使うときに判定し直す)。
- 09A8h (`PEGC_HSYNC_PORT`) の D0 (`PEGC_HSYNC_READ_HF`) で 31k か 24k かを決める。D0 は周波数だけで、HBLANK ではない。
- 00A0h (`GDC_GFX_STAT`) の bit5 の新しい立上りを 9 回採って 8 周期を測る。期待の周期と ±1% で照合する (64bit の交差積)。合わなければ dynamic_ok=0 で OK を返す。測った値を周期そのものには使わない。
- 8 回の高の期間の合計を「総行数−400」(非表示全体型) と「VS 行数」(VS 型) の両方と ±1 行で比べ、active0 を 0 か VBP に決める。どちらでもなければ dynamic_ok=0。
- 時計は `sys_time_now` (PIT 補間の µs)。待ちはどれも時刻の期限 (VSYNC は 100ms) と `POLL_LIMIT` (1,000,000 回) の両方で止まる。時計の逆行・停止も NO_SYNC。
- begin は 8〜9 フレーム (約 130〜160ms) かかる。`platform_splash_begin` が初回の判定を済ませる。

## 固定色 (frame)

`image->count != 0` は NO_SYNC で拒否する。新しい VSYNC の立上りの後、16 色を番号→G→R→B の 4 OUT ずつ書く。dynamic_ok に依らない (周期が合わなくても固定画像は出せる)。

## 公開の表 (table) の再生

- 入口の検査: IF=1・dynamic_ok・count 1〜200・line<400・index<16・異なる行の間隔 2 行以上 (昇順でない表もここで拒否)。どれかが違えば何も書かずに NO_SYNC。呼び手は転送済みの像を静止のまま出す。
- line=0 の項目は VSYNC の立上りで初期色として書く。
- VSYNC の立下り F の捕捉: 予測 (立上り+高の期間) の 1 行前まで IF=1 で待ち、その後の最大 1 行だけ IF=0 で bit5 の 1→0 を待つ。予測から半行以上遅れたら、そのフレームを捨てる (dropped を数え、次のフレームで戻る)。
- 各項目の目標は `F + (active0+line) 行` の時刻。目標の 1 行前から IF=0 にし、HBLANK (bit6) の新しい立上りを最長 2 行待って 4 OUT を書く。保護の後は切り上げて 1 行ぶん以上 IF=1 にする。
- 遅れたら次の HBLANK へ順に繰り越す。期限を過ぎた同じ index の値は最新の値に畳む。late は各項目を 1 回だけ数え、再生を止める理由にしない。可視区間を越えた分は unapplied に数えて捨て、次フレームの初期色で戻る。
- この方式は時刻で行を決める。H の辺を数えて行を決める方式 (画像の再生が採った方式) にはまだ置き換えていない。

## 落とし穴

- NP21/W の bit6 の高の期間は H の約 8 割で、実機 (31k で約 5.9µs) と違う。NP21/W で幅や位相が通っても、実機の時間の証拠にはならない。
- 054Ch bit5 は BIOS の起動時の写しで、begin の前後で変わらない。判定には使わない。
- `lines_us` と high_us の計算は 64bit の割り算 (libgcc) を使う。
- 参考にしたのは [FreeBSD releng/10.4 sys/pc98/cbus/gdc.c](https://github.com/freebsd/freebsd-src/blob/releng/10.4/sys/pc98/cbus/gdc.c#L655) の VSYNC の待ちと 16 色のパレットの設定 (BSD 3 条項)。描画コマンドと実装の構造は取り込まない ([HW1]・[HW3])。

## 診断の像

`SPLASH_RASTER_DIAG` のときだけ、フレームごとに先頭 128 件の予定・開始・終了・適用、IF=0 の区間の最大と合計、late/drop/unapplied を RAM に残す。通常の像では消え、`.text` は変わらない。

## 検査

- `tools/tests/test_splash_raster.py` (splash-raster-host): 有理数の走査の模型で、両周波数・両方の型・周期の不一致・IRQ の窓・遅れ・有限の待ちを確かめる。省略の変異は実行時 RED。
- `tools/tests/test_boot_splash_native.py` (boot-splash-host): 実際の gfx の核・VRAM・この再生器を組み、flip の有無・初回と継続・同期失敗・IF=0・予算違反・再初期化での判定のやり直しを確かめる。120µs の相対待ちの復活も RED。
- `tools/tests/test_splash_raster_diag.py`: 診断の記録が動くこと、通常の像の `.text` が変わらないこと。
