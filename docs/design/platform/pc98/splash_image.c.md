# platform/pc98/splash_image.c — 固定画像の所有と表示

状態: **現行 (2026-11-01)**。

`platform_splash_try_image` は begin 後の借用 BB と再生状態を使う。固定パスは config.h の SYS_SPLASH_IMAGE。記述だけを kmalloc し、osri_load が FD を所有して全経路で close する。画像記述は表示終了・失敗とも kfree する。

段1はイベント0件だけを受け入れる。検査完了まで present せず、全 dirty を nosync で転送し、次の VSYNC に初期16色を一度だけ設定する。周期/位相の照合不一致でも固定色は使用できる。入口IF=0では直接初期色を設定してIFを保つ。

期限は boot_splash が一度決めた end_tick。読み込みで期限を使い切れば終了し、成功後は時計でフレーム間隔を待つ。欠落・破損・イベント予算・メモリ不足は理由1行と FALLBACK、VSYNC喪失は理由1行と NO_SYNC。失敗時の BB は呼び手が内蔵ロゴで全面描き直す。NORMALのみへの配布登録は後段。
