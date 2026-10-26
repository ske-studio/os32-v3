# TASK_VER_INFO — ver と About の表示を、実際の版と装置の状態から出す

> 上位: [ROADMAP](../../ROADMAP.md)・版数の正典 [VERSIONS](../../decisions/VERSIONS.md)・規則 [C4] ([POLICY_DEV](../../POLICY_DEV.md) §2)

> 状態: **受入完了 (2026-10-26)** — V1〜V12 が main (6741ab19 ほか)。ver と About の 1 行目は「PC-9821 OS32 v3.0-dev (Ring3 Native)」、装置の行はカーネルが実際の状態から作り (formatter 1 つ、上限 20 行・52 桁)、新しい KAPI sys_ver_info (v79) で CUI と GUI が同じ行を読む。PM のゲスト (98 の 16 色と PEGC 480 の CUI・GUI) 合格、証拠 ~/os32-tmp/evidence/2026-10-26/ver-info/RESULT.md。KAPI が上がったので実機は boot を先に入れる。

## 0. ユーザー指示 (2026-10-26)

「ver の表示が最新にならないので設計から見直し。ver 2.0 にでるので。これは CUI と GUI 両方です。」

- 開発中の v3 の版数の表記は **`3.0-dev`** (1 行目は「PC-9801 OS32 v3.0-dev」、リリースのときに 3.0 にする)。設計の未決 §8 にユーザーが推奨どおりと答えた (2026-10-26)。

## 1. 今の形と問題

- 版数: `include/config.h` の `SYS_VERSION "2.0"` を直に書いている。v3 (VERSIONS §1) になっても更新されない。
- 装置の行: CPU・PIC・PIT・KBD・SER・SND・GFX が固定の文字列 (`userland/shell/cmd_base.c` の cmd_ver)。実際の状態と食い違う (例: PEGC の 640×480・256 色でも「GFX: 640x400x16 CPU direct」)。
- CUI と GUI で二重に持つ: GUI の About (`userland/rust/about/src/info.rs`) が同じ文字列の写しを持ち、版数は config.h の文字列を読み取っている。試験 `tools/tests/test_about_info.py` が両方の文字列を照合している。
- 正しく出ているのは API の版・Build・Commit・Image CRC・kdebug の行 (カーネルから取っている)。

## 2. 段

| 段 | 中身 | 設計 | 実装 | 状態 |
|---|---|---|---|---|
| V1・V3・V4・V5 | 版数 3.0-dev と VERSIONS の注記、公開の型 (os32_ver_info.h)、kdebug の状態名の範囲検査、services の状態を取る口 | [VER_INFO_DESIGN](../../decisions/VER_INFO_DESIGN.md) | sol | **main** (astra 取り込み可、試験 2 本を直してから) |
| V6 | GFX の保存の状態を取る口 (gfx_hal.h・gfx_core.c) | 同上 | sol | **main** (astra 取り込み可、Cirrus の ABI 値が変わる — kernel と cirrus.mod を対で配る) |
| V7 | 装置の状態を集める (kernel/ver_collect.c、kernel/ver_info.h、CPU の既定かどうかの口は cpu_calibrate) | 同上 | sol | **main** |
| V8 | 行の formatter (kernel/ver_format.c、上限 20 行・52 桁) | 同上 | sol | **main** |
| V9・V2 | 新しい KAPI と版の文書 (一緒に取り込む) | 同上 | sol | **main** |
| V10・V11 | CUI の ver と GUI の About を新しい行の読み手に (直列、取り込みは 1 回) | 同上 | sol | **main** |
| V12 | 試験の注記と全体の受入の登録 | 同上 | sol | **main** |

新しいユーザーランドは新しいカーネル専用 (KAPI の版が上がる)。旧ユーザーランドは新しいカーネルで動く。実機は boot を先に入れる順 (スキル os32-realhw-update)。ver の `services: SERVICES_READY` の行は残す (実機の更新の判定に使う)。
