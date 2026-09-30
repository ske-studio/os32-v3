# OS32 v4 草案

> 状態: **草案 (2026-09-07)** — 実装契約ではない。

v4 世代の長期設計に関する草案群を置く。

現時点では実装契約ではなく、v1〜v3 の開発を妨げずに将来の portability / multi-architecture / distributed device 構成を検討するための記録である。

- [Portable Game OS / Multi-Architecture / OS32 Fabric 草案](../../V4_GAME_PLATFORM_DRAFT.md)

主題:

- Source-level compatibility を中心とした multi-platform 化
- Portable API と platform-specific API の分離
- `arch/` と `platform/` の責務分離
- Native / Hosted / Game Runtime profile
- Game Engine Core / Module / Template の境界
- 上位層からの段階的 Rust 導入
- OS32 Fabric / OS32 Link
- TCP/IP 非依存の閉域 capability-only device network
- Raspberry Pi 外部 GPU を最初の Fabric PoC とする案
