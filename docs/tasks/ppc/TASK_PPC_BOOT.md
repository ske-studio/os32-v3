# TASK_PPC_BOOT — PowerPC 移植: Open Firmware から 1 行出す

> 状態: **受入完了・実機確認待ち (2026-11-07)** — QEMU mac99 で 2 行を出して停止、main 72597f33。証拠 ~/os32-tmp/evidence/2026-11-07/ppc-boot/RESULT.md。実機は DEFERRED_TESTS の PPC-BOOT-RH。

## 0. ユーザー決定

- 2026-11-07 「サブストリーム用の PC を別のアーキテクチャ開発用に向ける」「PowerMAC G4 QuickSilver を予定。まだ動くかわからない」。先に QEMU `qemu-system-ppc -M mac99`。
- この PC の GitHub Actions の runner は止める。codex の sandbox のために userns の制限は解除してよい。

## 1. 詳細設計

全文は [PPC_BOOT_DESIGN](../../decisions/ppc/PPC_BOOT_DESIGN.md)。要点:

- 像は独立の `build/ppc.mk` で組み、共有のコードは変えない (`lib/kstring_c.c` は無変更で組む)。契約ヘッダ `io.h`・`cpu.h` はこの段では取り込まない。
- カーネル側は `-nostdlib`・`-msoft-float -mno-altivec`。newlib nano はユーザーランドと SDK の段。
- 手本は NetBSD の ofwboot と zs、OpenBSD の ofwboot。手本に無い物は `map` の mode (ビット定義から求めた) と drain (送信直後に止まるため) で、設計に明記した。
- 試験は QEMU のシリアル照合、readelf/objdump、libc なしの host の fake の 3 つで、`make check` の外 (`tools/ppc/run.py`)。

## 2. 環境

- `powerpc-elf` (binutils 2.41 / GCC 13.2.0 / newlib 4.4.0 nano、multilib なし) を `CROSS_DIR=~/opt/cross-ppc` に置く。QEMU 8.2.2 + OpenBIOS は apt。手本は `~/os32-tmp/ref/` の sparse clone (SHA は設計 §1)。

## 3. 実機で確かめる点

設計 §7。実機は未確認で、第一の観測路は OF の画面。ESCC は筐体の外に出ていない前提。

## 4. 実行

`make -f build/ppc.mk CROSS_DIR=$HOME/opt/cross-ppc` → `python3 tools/ppc/run.py --timeout 30 [--mutate]`。toolchain の作り方は `tools/ppc/build_cross.sh`。
