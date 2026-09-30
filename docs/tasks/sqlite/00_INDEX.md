# SQLite 組み込みエンジン — ドキュメント索引

> 発行: PM (2026-04-24) / 状態: **受入完了 (2026-04-28)**

OS32 カーネルへの SQLite 3.53.0 統合プロジェクトの設計・実装ドキュメント。

## ドキュメント一覧

| No. | ファイル | 内容 |
|-----|---------|------|
| 01 | [ARCHITECTURE.md](01_ARCHITECTURE.md) | 全体アーキテクチャ設計 (カーネル統合方式、IPC、メモリレイアウト) |
| 02 | [SQLITE_BUILD.md](02_SQLITE_BUILD.md) | SQLite コンパイル設定 (OMIT マクロ、C89 パッチ、amalgamation) |
| 03 | [CUSTOM_VFS.md](03_CUSTOM_VFS.md) | OS32 カスタム VFS 実装仕様 (os32_sqlite_vfs.c) |
| 04 | [KAPI_IPC.md](04_KAPI_IPC.md) | KernelAPI 拡張 + 共有メモリ IPC プロトコル |
| 05 | [CLEANUP_SAFETY.md](05_CLEANUP_SAFETY.md) | リソースクリーンアップ・異常系対策・fsync |
| 06 | [PHASES.md](06_PHASES.md) | 実装フェーズ計画・TODO |
| 07 | [OBSTACLES.md](07_OBSTACLES.md) | 導入障害レポート (全11障害の記録・教訓) |

## 設計方針

- SQLite エンジンはカーネルに統合し、外部プログラムからは KAPI + 共有メモリ経由で操作
- Phase 1 で外部プログラム PoC → Phase 2 でカーネル統合の段階的アプローチ
- `SQLITE_OS_OTHER=1` でカスタム VFS を提供 (OS32 VFS レイヤー上に構築)
- シングルタスク前提: `SQLITE_THREADSAFE=0`、ファイルロック不要
- メモリ管理は MEMSYS5 固定プールでカーネルヒープ圧迫を回避
- DB 接続上限: システム 1 + アプリケーション 1 = 合計 2
- ジャーナルモード: `journal_mode=DELETE` (デフォルト)
- ターゲット FS: ext2 (メイン) + HostDrvFS (マウント時自動対応)
