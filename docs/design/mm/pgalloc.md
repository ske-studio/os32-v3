# mm/pgalloc.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `mm/pgalloc.h`

SURFACE の GF 識別定数は既存 ID の末尾へ追加し、`O/mm/pgalloc.h:283`・`sdk/include/os32/os32_surface.h:11`・`exec/surface_query.c:66–68,110` を追従する。

## 落とし穴

- **RAM 上端と穴**: 高位 RAM の上端がデバイス窓より上でも、窓自体は RAM のない穴かもしれない。
  `mm/pgalloc.c:1082` の `pgalloc_range_has_ram(first, end)` に PFN 半開区間を渡し、凍結した物理地図で重複を調べる。
  未初期化や不正な範囲も安全側で扱う。`tools/tests/test_pgalloc_range.py` が窓と境界を検査する。
