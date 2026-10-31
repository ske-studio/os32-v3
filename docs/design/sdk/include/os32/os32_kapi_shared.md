# sdk/include/os32/os32_kapi_shared.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `sdk/include/os32/os32_kapi_shared.h`

### G5. 能力の問い合わせ

`screen_info() -> GFX_ScreenInfo` (KAPI v40 `gfx_screen_info`、`os32_kapi_shared.h`):
`{width, height, bpp, format: GFX_FMT_PLANAR4 / PACKED8, flags: GFX_CAP_TEXT_OVERLAY /
HW_FILL / HW_BLT / PAGE_FLIP, lease_mask, lease_first, lease_count}`。GUI とアプリはこれを
信じ、400 ライン・16 色を決め打ちしない。

### G7. カウンタ (DESIGN §8)

`stats() -> {present_bytes, hw_ops, io_accesses, commits}`。累積。デバッグ用で契約の一部
(NP21/W ではこれで性能を見積もる)。
