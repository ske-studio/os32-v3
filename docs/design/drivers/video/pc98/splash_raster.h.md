# splash_raster.h — 共通再生の内部契約

状態: **現行 (2026-11-01)**。ソース: `drivers/video/pc98/splash_raster.h`。

`SplashRaster` は再生器が所有する。`state()` の初回利用時に判定し、以後は同じ
状態を借用する。`reset()` は gfx_init/shutdown の寿命境界で呼ぶ内部フックで、
接続は後続 L0 が担当する。直接 begin を呼ぶ場合はゼロ初期化した状態を渡す。
begin/frame/table は OK/NO_SYNC を戻し、last_result にも保存する。周期・位相の
不一致は begin 自体の成功と dynamic_ok=0 で区別する。

周期は µs の分子100000・分母2483/3147。SYNC は pegc.h の400行マスタ設定から
総行数・VS幅・VBPを取り出す。単色ロゴの上限は1件/行、行間隔は2行以上。
実機の複数色予算は未測定であり、この定数は段0の表再生の制限である。
frame は段0では差分0の初期16色だけを扱い、OSRI差分は段2で追加する。

診断記述型と `pc98_splash_raster_diag()` は SPLASH_RASTER_DIAG の内側だけに置く。配列長を公開定数にせず、count/capacityと借用のconst eventsポインタで読む。通常のSplashRaster配置と公開KAPIは維持する。
