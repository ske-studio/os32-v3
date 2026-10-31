# atapi.c — ATAPI PACKET と待ち・回復

> 状態: **現行 (2026-10-31)**

装置選択、PACKET の受付待ち、DRQ ごとの状態確認、転送後 settle、完了確認を
本体に置く。ワード転送だけ [atapi_pio.inc](atapi_pio.md) に委譲する。
`actual` は成功時に有効バイト数を返し、途中エラーでは入口の 0 を維持する。
診断値はエラー時点の status/error/受信済み長を保持する。

SRST 発行前に `ide_multiple_reset_bank(1)` で同バンクの HDD 2台を失効させる。
SRST の失敗時も無効のままとし、次の HDD 要求で再設定する。
ATAPI 単体 DEVICE RESET は HDD の設定へ触れない。

## 落とし穴

L2 の短い待ち 20 回×10µs、以後 100µs、SRST 保持・回復待ちを維持する。
D5a では待ち・reset の条件や位置を変更しない。
READ(10) は最大 `ATAPI_READ_MAX_SECTORS` ごと、複数読み失敗時は既存どおり
必要な範囲を単セクタで再試行する。ISO 側はその失敗結果を受けて必要セクタを
再取得する場合がある。媒体世代は既存の UNIT ATTENTION / NOT READY 規則で進める。
