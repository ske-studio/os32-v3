# kdebug/gf_stage_state.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kdebug/gf_stage_state.c`

baseline 値採取と局所復元だけ必要範囲を共有化。診断 recover 型の adapter を残す。

## GF_STAGES_DESIGN

常駐値は起動中だけ有効。main 4の成功直後、最初のQより前に一度採る。再起動・新しい初期化開始で無効にし、ファイルから復元しない。

全値の採取成功時だけinit完了と基準値validを立てる。採取に失敗したら公開せずS0へ進む。既存作業域の常駐量は別で、再確保・複製しない。

保存するのはinit完了／基準値valid、identity、BAR3本のraw/base/length、ROM識別・VIDEO PLL limits、`c_fields` の8値、初回C040、PMC_ENABLE観測値、直前S0結果。

入口は生ID/BAR一致、Command=0102、cfg50/54・1850/1854=0、route&3=0、BAR1範囲≧307200B、PMC_ENABLEが保存観測値と一致。表示中から直に重ねず先にS0を要求する。
