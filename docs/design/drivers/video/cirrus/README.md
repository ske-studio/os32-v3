# drivers/video/cirrus/README — 設計
> 状態: **現行 (2026-10-31)** — 出所: GFX_MODES_DESIGN (D1 で移した)
ソース: `drivers/video/cirrus/README`

### cirrus_module

- 未登録 Cirrus（drivers/video/cirrus/cirrus_module.c:16）は modes=NULL のまま skip し、probe/enable を追加しない。
