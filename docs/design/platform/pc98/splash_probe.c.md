# platform/pc98/splash_probe.c — 診断ビルドの計測

状態: **現行 (2026-11-01)**。

`kernel-splash-diag` のみが SPLASH_RASTER_DIAG を定義し、別 BUILD_OUT のオブジェクト・map・像を組む。通常の kernel/all は probe をリンクしない。begin 前には09A8hと054Chを記録し、begin 後に比較する。054Chは P2V_IO と既存PEGC定数で読み、判定には使わない。

RAMに32 VSYNC周期と両辺/テキスト状態、64 HBLANK周期/幅、時計128回、空ループと1/4/8色・8/16 OUT・初期16色、32フレームのIF最大/合計・late/drop/未適用・tickを採る。32周期のmin/median/p99/max、絶対予定/開始/終了は測定後にシリアルへ出す。時計異常・有限ポーリング・記録溢れはerrorsとして扱い、共通cleanupへ戻る。

模様は中央パレット1、16行目盛、上下枠、16色タイル、8行縞の−4〜+4副尺。4行境界を9回1行ずつずらし、その後は固定の4行帯を測る。無負荷/受信負荷は同じ診断像をPMが別々に起動する。負荷入力は0始まりの連続little endian u32列。各フレーム後に既存RXリングを読み、断絶/端数とOE/FE/PE/overflow差を記録する。欠番の原因や受信ゼロを負荷合格とは判定しない。

既存120µs版との速度/転送比較はPMが基点の像と実施する。診断内に旧再生を復活させない。時計読出しの費用・1µs量子化と診断追加の観測負荷を含むので、実機の予算採用はPMの観測ゲート。根拠はNEC Technical Databook Hardware (1993) p.350–351（PM取得済み、31k表は未掲載）と [FreeBSD gdc.c](https://github.com/freebsd/freebsd-src/blob/releng/10.4/sys/pc98/cbus/gdc.c#L655) の同期/パレットI/Oだけ。描画コマンドは使わない。
