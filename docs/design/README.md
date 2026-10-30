# 設計書の入口

> 状態: **現行 (2026-10-30)**

[POLICY_DEV §9 の 2a](../POLICY_DEV.md) に従い、設計書は `docs/design/<ソースと同じパス>.md` に置き、1 ソース・1 機能につき 1 本とする。複数ファイルに共通の契約は共有ソースの設計書に置き、ディレクトリ全体の決まりはその README に置く。既存の `docs/decisions/` は移すまで現行。

## ディレクトリの入口

- [drivers/video/geforce/](drivers/video/geforce/README.md)
- [hwdev/gf/](hwdev/gf/README.md)
- [hwdev/gf/display/](hwdev/gf/display/README.md)
- [hwdev/gf/op/](hwdev/gf/op/README.md)

同じディレクトリに README がない設計書も、上記の関連する入口またはこのページから辿る。

## その他の設計書

- [drivers/video/pc98/pc98_route.md](drivers/video/pc98/pc98_route.md)
- [exec/surface_query.md](exec/surface_query.md)
- [gfx/gfx_core.md](gfx/gfx_core.md)
- [hwdev/gf_load_state.md](hwdev/gf_load_state.md)
- [hwdev/gfinit_mode.md](hwdev/gfinit_mode.md)
- [include/gf_production.md](include/gf_production.md)
- [include/gfx_hal.md](include/gfx_hal.md)
- [include/video_service.md](include/video_service.md)
- [init/kernel.md](init/kernel.md)
- [init/kernel_gfx_config.md](init/kernel_gfx_config.md)
- [init/sysconfig.md](init/sysconfig.md)
- [kdebug/gf_stage_state.md](kdebug/gf_stage_state.md)
- [kdebug/gfinit.md](kdebug/gfinit.md)
- [kdebug/gfinit_bus.md](kdebug/gfinit_bus.md)
- [kdebug/gfinit_decode.md](kdebug/gfinit_decode.md)
- [kdebug/gfinit_exec.md](kdebug/gfinit_exec.md)
- [kdebug/gfinit_mode.md](kdebug/gfinit_mode.md)
- [kdebug/gfinit_plan_c.md](kdebug/gfinit_plan_c.md)
- [kdebug/gfinit_pll.md](kdebug/gfinit_pll.md)
- [kdebug/gfinit_resources.md](kdebug/gfinit_resources.md)
- [kernel/sys.md](kernel/sys.md)
- [lib/pc98_cell.md](lib/pc98_cell.md)
- [mm/pgalloc.md](mm/pgalloc.md)
- [sdk/include/os32/os32_surface.md](sdk/include/os32/os32_surface.md)
- [tools/gfinit_mode_tables.md](tools/gfinit_mode_tables.md)
