# タイルマップエンジン — ドキュメント索引

> 発行: PM (2026-04-23) / 状態: **受入完了 (2026-04-24)**

SFC風4プレーンBGタイルマップ合成エンジンの設計・実装ドキュメント。

## ドキュメント一覧

| No. | ファイル | 内容 |
|-----|---------|------|
| 01 | [TILEMAP_DESIGN.md](01_TILEMAP_DESIGN.md) | タイルマップエンジン設計仕様 |
| 02 | [BLIT_COLORKEY_OPT.md](02_BLIT_COLORKEY_OPT.md) | gfx_blit_colorkey 高速化リファクタリング |
| 03 | [ROTATE_BLIT.md](03_ROTATE_BLIT.md) | 回転ブリット (gfx_blit_rotated) 設計仕様 |
| 04 | [SCROLL_OPT.md](04_SCROLL_OPT.md) | スクロール差分最適化 Phase 1〜3 (完了) |
| 05 | [SCROLL_ASM_OPT.md](05_SCROLL_ASM_OPT.md) | スクロールエンジン NASM 高速化 Phase 4〜5.5b (完了) |
| 06 | [REFACTOR_PLAN.md](06_REFACTOR_PLAN.md) | libtilemap リファクタリング調査・実装 (完了) |
| 07 | [TODO.md](07_TODO.md) | 今後のTODO・性能分析・拡張候補 |

## コンセプト

- 16×16タイル × 24×24グリッド = 384×384 論理解像度
- 4枚のBGプレーン（BG0=最背面 〜 BG3=最前面）
- **Back-to-Front** と **Front-to-Back** の2方式を提供し、ユーザーが選択可能
- 既存GFXインフラ（`gfx_get_framebuffer`, `gfx_blit`, `gfx_add_dirty_rect`, `gfx_present_dirty`）を最大限活用
- 16色パレット共有はハードウェア制約として受容
