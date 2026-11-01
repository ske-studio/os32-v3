# platform_splash.h — 起動スプラッシュの入口

状態: **現行 (2026-11-01)**。ソース: `include/platform_splash.h`。

begin/描画/両ページ消去の既存契約を維持する。画像側の入口 try_image(path,end_tick)
は共通終了期限を受け、DONE/FALLBACK/NO_SYNC を戻す内部宣言。実体の接続は後続段。
SPLASH_RASTER_DIAG のみ probe 宣言を公開する。再生状態はここで所有せず、
ドライバの splash_raster.h の accessor から借用する。KAPIの型・番号は変更しない。

`platform_splash_wait_frame(end_tick)` は静止fallback/固定画像の時計による歩調。sys_time_nowの逆行/失敗/停止は有限ポーリングで非0、入口IFを維持する。probe_before/probeは診断ビルドだけの前景フック。
