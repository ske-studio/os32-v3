# lib/kprintf.c — 書式つきの出力と、診断の行き先

> 状態: **現行 (2026-11-22)** — 機種依存の分離 (PowerPC の像の `-D` の名前の差し替えを口に替えた)。
ソース: `lib/kprintf.c`・`lib/kprintf_format.inc`・`include/kprintf.h`・`platform/<機種>/platform_kprintf.h`・`platform/<機種>/platform_console.h`・`platform/powermac/log_printf.c`

## 2 つの名前

| 名前 | 意味 | 行き先 |
|---|---|---|
| `kprintf`・`kvprintf` | カーネルの診断の出力 | 機種が決める。PC-98 はコンソール。PowerMac は全像で ESCC と LAN のログ (`pm_diag_write`) |
| `console_printf`・`console_vprintf` | 端末 (コンソール) に必ず出す物 — ユーザープログラムの出力 (KAPI の `kprintf`)、利用者への知らせ | 常にコンソール (`console_write`・`shell_print`・`shell_print_utf8`) |

新しく書くカーネルの中の出力は `kprintf`。端末に出なければ意味が無い物だけ `console_printf` にする。
今 `console_printf` を使うのは `net/nic.c`・`net/link_selftest.inc`・`exec/exec_finish.inc`・`exec/exec_launch.inc`、
`console_vprintf` を使うのは `kapi/kapi_print.c` (ユーザープログラムが KAPI の `kprintf` を呼ぶ道。syscall の入口が
この口だけ特別に扱う)。KAPI の表のポインタも `console_printf` を指す (`sdk/kapi.json` の `target`)。

## 仕組み

- 書式の処理の本体は雛形 `lib/kprintf_format.inc`。取り込む側が出力先 (`KPRINTF_WRITE`・`KPRINTF_PRINT`・
  `KPRINTF_PRINT_UTF8`) と関数の名前 (`KPRINTF_VNAME`・`KPRINTF_NAME`) を決める。数値の変換 (`kutoa_dec`・`kutoa_hex`) と
  書式の解析 (`kprintf_spec`) は `lib/kprintf.c` に 1 つだけ在る。
- `lib/kprintf.c` はコンソール向けの実体 (`console_printf`・`console_vprintf`) を作る。PC-98 の `platform_kprintf.h` は `kprintf`・`kvprintf` を同じ関数の別名にする (機械語は増えない)。
- PowerMac は全像に共通のソースで雛形を取り込んで `kprintf`・`kvprintf` を作る (`platform/powermac/log_printf.c`)。
- ビルドの規則は名前を差し替えない (`-Dkprintf=…` を使わない)。ソースが呼ぶ名前がそのまま行き先を決める。

## 機種ごとの方針 `platform_console.h` (固定名)

| 名前 | PC-98 | PowerMac | 使う所 |
|---|---|---|---|
| `CONSOLE_PUTCHAR_MIRROR` (1 文字の出力を遠隔の鏡へ流すか) | 0 | 1 | `kernel/console/console.c` の `shell_putchar` |
| `CONSOLE_UTF8_AS_STREAM` (`shell_print_utf8` を `console_write` の道に流すか) | 0 (漢字の描画つき) | 1 | `kernel/console/console.c` |
| `platform_fd1_write` (fd 1 の既定の出力先) | `console_write` | `pm_fd1_write` (`console_stream.c`) | `fs/vfs_fd.c` |

## 確かめ

- x86: 機械語は変わらない (別名と、定数で消える分岐だけ)。`tools/build_compare.py`・workflow `sep`。
- PowerPC: QEMU の受入 (`ppc-sh-output`・`ppc-sc`・`ppc-fb` ほか) が、端末とログの行き先を別々に見ている。
- host: `console-cui-route-host` (2 つの機種の方針で `console.c` を組む)・`kprintf-va`・`kapi-print-host`。
