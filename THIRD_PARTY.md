# THIRD_PARTY — 第三者の部品とそのライセンス

OS32 本体は MIT ([LICENSE](LICENSE))。ここに挙げるのはリポジトリに取り込んでいる、
またはビルド時に取り込まれてリンク・配布物に含まれる第三者の部品と、その配布条件。
**原文 (ライセンス全文) は各行の「原文の所在」にある** — このファイルは一覧であって
原文の代わりではない。配布物 (ディスクイメージ・`os32-sdk`) を作るときはこのファイルと
各原文を同梱する。

| 部品 | 版 | ライセンス | 置き場 | 改変 | 原文の所在 |
|---|---|---|---|---|---|
| SQLite | 3.53.0 | Public Domain (blessing) | `lib/sqlite3/sqlite3.{c,h}` (amalgamation) + OS32 の設定・VFS (`os32_sqlite_config.h`、`os32_sqlite_vfs.c`) | amalgamation 本体は無改変。設定と VFS は OS32 の自作 | `sqlite3.c` 冒頭の blessing |
| zlib (inflate のみ) | 1.2.11 | zlib License | `lib/zlib/` (inflate.c inffast.c inftrees.c adler32.c zutil.c と対応ヘッダ) | **無改変** (条項 2 の「改変版の明示」は不要)。deflate / gzip / crc32 は取り込んでいない | `lib/zlib/zlib.h` 冒頭、経緯は `lib/zlib/README.OS32` |
| microtar (rxi) | 0.1.0 (master 27076e1、2017) | MIT | `lib/microtar/microtar.{c,h}` | `microtar.c` に 4 点 (`MTAR_NO_STDIO`、octal 読み、フィールド長の上限、512B 単位の書き込み)。`microtar.h` は無改変。詳細は `lib/microtar/README.OS32` | `lib/microtar/LICENSE` |
| microui (rxi) | 2.x 系 (Copyright 2024 rxi) | MIT | `userland/lib/ui/microui.{c,h}` | OS32 移植版 (C89 化、整数演算のみ、メモリ縮小、`OS32:` コメントの箇所) | `userland/lib/ui/microui.c` 冒頭 |
| FatFs (ChaN) | R0.15 | FatFs License (BSD 風、1 条項) | `fs/fatfs/ff.{c,h}` `diskio.h` | `ff.c` `ff.h` `diskio.h` は**無改変** (改行コードを LF に揃えたのみ。2026-09-30 に上流 ff15.zip と照合)。`ffconf.h` は OS32 向け設定、`diskio.c` `diskio_os32.h` `string.h` は OS32 の自作 | `fs/fatfs/ff.h` 冒頭 |
| LZ4 ブロック展開 | — (仕様 lz4_Block_format.md 準拠) | MIT (本リポジトリ) | `lib/lz4.c`、`boot/lz4_mini.c`、`lib/os32_lz4/` (Rust) | **自作**。公式 LZ4 のコードは使っていない (ホスト側の圧縮は Python `lz4` パッケージ) | — |
| newlib (nano) | 4.4.0 | 各ファイルの BSD 系ライセンス (newlib の `COPYING.NEWLIB`) | リポジトリには無い。クロスツールチェーン側 (`tools/ci/build_cross.sh` が sourceware から取得) — 外部プログラム・常駐シェルに**静的リンク**される | 無改変 | https://sourceware.org/git/?p=newlib-cygwin.git;a=blob;f=COPYING.NEWLIB (配布物を作るときは同梱) |
| Rust クレート `ttf-parser` | 0.21.1 | MIT OR Apache-2.0 | vendor していない (`userland/rust/Cargo.lock`、ビルド時に crates.io から取得)。`font_test` に含まれる | 無改変 | crates.io (https://crates.io/crates/ttf-parser) |
| Rust クレート `ab_glyph_rasterizer` | 0.1.10 | Apache-2.0 | 同上 | 無改変 | https://crates.io/crates/ab_glyph_rasterizer |
| Rust クレート `libm` | 0.2.16 | MIT (crates.io の表記、2026-09-30) | 同上 | 無改変 | https://crates.io/crates/libm |
| IPADIC (MeCab 版 CSV 13 本) | mecab-ipadic 2.7.0-20070801 | IPADIC の配布条件 (NAIST の表示 + ICOT Free Software の条件) | `assets/ipadic/*.csv` → 生成物 `assets/fep.db` (配布物、ゲストの `/db/fep.db`) | CSV は**無改変** (上流と sha256 一致)。辞書 DB は CSV から生成 | `assets/ipadic/COPYING` (原文、EUC-JP)、出所は `assets/ipadic/README.OS32` |
| IPAex フォント | Ver.004.01 | IPA Font License Agreement v1.0 | **リポジトリには含めない** — `ipaexg.ttf` `ipaexm.ttf` はビルド時に `tools/fetch_fonts.py` が IPA の公式配布 (`IPAexfont00401.zip`) から**同意つきで取得**する ([docs/08_build.md](docs/08_build.md) §8-1) → 生成物 `*.kcgfont` (派生プログラム)。**TTF そのものはゲストに配らない** (サブセット TTF は 2026-09-30 に廃止) | TTF は**無改変** (取得時に zip と各 ttf の sha256 を照合)。派生物は第 3 条の条件 — 名前に IPA を含めない (ホスト側の生成物名は要改名)、ライセンス同梱 | `assets/fonts/IPA_Font_License_Agreement_v1.0.txt` (原文、**同梱**)、条件の整理は `assets/fonts/README.OS32` |
| 常用漢字表 | 2010 年内閣告示 (2,136 字) | 事実の一覧 (告示) | `assets/joyo_kanji.txt` (`tools/fep_to_sqlite.py` が辞書の絞り込みに使う) | 漢字だけを 1 行に並べたもの | — |
| Python パッケージ (ホストの道具) | [requirements.txt](requirements.txt) | 各パッケージ (Pillow = HPND/MIT-CMU、PyYAML = MIT、lz4 = BSD-3、numpy = BSD-3、fontTools = MIT、zopfli = Apache-2.0、pyserial = BSD-3、pywin32 = PSF、pyautogui = BSD-3、pandas = BSD-3) | `tools/` の実行時に import。**配布物には入らない** | — | 各パッケージの配布物 |

ゲスト側 (ディスクイメージ) に**含まれるもの**は SQLite・zlib inflate・microtar・microui・FatFs・
newlib・Rust クレート 3 つ・IPADIC 由来の辞書・IPAex 由来のフォント。ホストの Python パッケージと
ツールチェーン (GCC / binutils / NASM / Rust) は含まれない。

PC-98 のハードウェア資料 (PC-9800 Bible、UNDOCUMENTED 9801/9821) は**著作権物で、リポジトリに入れない**
(`docs/hw/` は gitignore、`tools/sync_hwdocs.sh` で手元に写す)。コードと文書にあるのは出典の §番号と要旨だけ。

生成される辞書 DB にライセンスと出所を運ばせる件は
[docs/tasks/fep/TASK_DICT_META.md](docs/tasks/fep/TASK_DICT_META.md) (M5)。
