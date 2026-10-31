# drivers/pci/README — 設計
> 状態: **現行 (2026-10-31)** — 出所: BARLEN_DESIGN (D1 で移した)
ソース: `drivers/pci/README`

### pci_decode

| `drivers/pci/pci_decode.h:88`、同:148 | BAR0 offset/count、IO/type/prefetch/address mask を既存定義から利用 |

### pci

- Command は `pci_cfg_write16`（`drivers/pci/pci.h:84`）を使う。Status の W1C ビットを含む DWORD RMW はしない。BAR probe は専用 phase に閉じ込める。
