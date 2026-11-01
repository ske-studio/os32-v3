# tools/tests/test_splash_raster_diag.py — 診断記録と変異

状態: **現行 (2026-11-01)**。

診断マクロ付きで実際の再生器を組んでqemu実行する。IF合計/最大・予定/終了・late・resetを消す変異は実行時に失敗する。通常ビルドのnmによる診断記号不在も検査する。
