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

LC-A は内部の整数比予算を追加する。`SplashRasterBudget` の全分子は共通の
`den` に従い、H/B/Q/C/W の範囲、成功 IN から OUT までの分岐、時計誤差、
再入口費用を分ける。Q は IN 単命令の値ではなく比較・計数を含むサンプル間隔。
24k の B 参考値は NEC の9.88µs、31k は SYNC 由来20000/3147µsであり、
実測した bit6 の幅と取り違えない。採否式はヘッダーコメントの厳密な不等式。

`SplashRasterEntryBudget` は内側へ丸めた二つの入口区間 S と予定入口 r、
追加費用 D、J_max を持つ。両入口なら U_(j+1)−r−D、一入口なら U−r。
フレームは全イベント/中継・F 捕捉・初期色完了余裕の最小値を取る。
負の余裕を符号付きで残し、未測定を正の予算にしない。J は粗待ち末尾から
入口までの累積遅延であり、単一 ISR の最長時間ではない。

診断専用の費用・入口記録と4型分類は同じ内部ヘッダーで共有する。
費用は IF=0 の total_us/samples と上限 upper_num_us/upper_samples、束の回数 batch、
IF=1 の open_total_us/open_max_us を別々に持つ。upper_us は粗待ち用の切上げ値。
Q/W の上限は端点時計Cの束平均を差し引くため、上限の分母と束の回数は異なり得る。
通常版の `SplashRaster`、公開 SDK/KAPI と再生手順は LC-A では変更しない。
