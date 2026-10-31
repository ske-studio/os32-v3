# drivers/net/ne2000/README — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D1_DESIGN (D1 で移した)
ソース: `drivers/net/ne2000/README`

### ne2000

| ファイル | 登録 |
|---|---|
| `drivers/net/ne2000/ne2000.c` | `ne2k_init()` の引数検査後・bring_up 前。失敗なら既存エラーで返し、装置を起動しない |

- ne2k_init は既存の NE2K_ERR_* のうち初期化失敗の値を返す。

### lgy98

| ファイル | 登録 |
|---|---|
| `drivers/net/ne2000/lgy98.c` | 公開 `lgy98_attach()` の検査後・装置操作前。init 経由以外の attach も覆う |
