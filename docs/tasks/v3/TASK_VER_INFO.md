# TASK_VER_INFO — ver と About の表示を、実際の版と装置の状態から出す

> 上位: [ROADMAP](../../ROADMAP.md)・版数の正典 [VERSIONS](../../decisions/VERSIONS.md)・規則 [C4] ([POLICY_DEV](../../POLICY_DEV.md) §2)

> 状態: **設計中 (2026-10-26)** — 設計の往復 (起草 astra → レビュー Opus) の後に実装。

## 0. ユーザー指示 (2026-10-26)

「ver の表示が最新にならないので設計から見直し。ver 2.0 にでるので。これは CUI と GUI 両方です。」

## 1. 今の形と問題

- 版数: `include/config.h` の `SYS_VERSION "2.0"` を直に書いている。v3 (VERSIONS §1) になっても更新されない。
- 装置の行: CPU・PIC・PIT・KBD・SER・SND・GFX が固定の文字列 (`userland/shell/cmd_base.c` の cmd_ver)。実際の状態と食い違う (例: PEGC の 640×480・256 色でも「GFX: 640x400x16 CPU direct」)。
- CUI と GUI で二重に持つ: GUI の About (`userland/rust/about/src/info.rs`) が同じ文字列の写しを持ち、版数は config.h の文字列を読み取っている。試験 `tools/tests/test_about_info.py` が両方の文字列を照合している。
- 正しく出ているのは API の版・Build・Commit・Image CRC・kdebug の行 (カーネルから取っている)。

## 2. 段

| 段 | 中身 | 設計 | 実装 | 状態 |
|---|---|---|---|---|
| V1〜 | 版数の正典と、装置の行を実際の状態から出す口、CUI と GUI で同じ出所を使う形。段の分け方は確定設計で決める | VER_INFO (astra 起草) | 設計で決める | 設計中 |
