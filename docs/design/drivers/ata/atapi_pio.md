# atapi_pio.inc — ATAPI の 16bit ワード転送

> 状態: **現行 (2026-10-31)**

L2 の CDB 送出と DRQ のデータ転送をそのまま分離した内部実装。
`atapi_pio_send_cdb` は 12 バイトを little-endian の 6 ワードで送る。
`atapi_pio_read` は受信語を消費し、有効バイト数を加えた終端を返す。
装置への転送方式は従来の `inpw/outpw` のまま。

偶数長かつ全体が受信バッファ内なら、非整列可の `AtapiPioWord` で格納する。
それ以外はバイトごとに応答・受信範囲を見て格納する。奇数 DRQ の上位端数と
受信容量外は読み捨て、装置が渡した有効バイト数には含める。

## 落とし穴

待ち・reset・ステータス・診断・actual・DRQ 後 settle は
`atapi.c` の責務。分離に伴って順序や値を変えない。
rep insw/outsw は D5b の別段であり、ここには導入しない。
[PIO 試験](../../tools/tests/cd_read_host_pio.md) は L2 と同じ語列・境界を確認する。
