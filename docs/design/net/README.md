# net/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D1_DESIGN (D1 で移した)
ソース: `net/`

### link

| `net/link.c` | `link_init()` で状態を有効化する前。失敗なら診断して return（初回は link_ready=0、既存状態を変更しない） |

link_init の登録の置き場は関数の先頭 (kmemcpy より前、登録が失敗したら状態を変えずに戻る)。
