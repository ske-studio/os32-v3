# kdebug/gfinit_resources/scan.inc — 設計
> 状態: **現行 (2026-11-10)**

bus 0 の fn0 不在と multifunction に従って全 function の生 config を読む。
GPU の ID を確認して除外対象から外す。MEM 有効装置の BAR / 有効 ROM を
起動時の測定記録で検査する。Type 1 は 2 BAR と転送窓を扱う。
収集中は yield・再割当て・再測定を行わず、欠けは count=0 で失敗する。
詳細は [資源収集設計](../gfinit_resources.md)。
