# sys/platform/pc98/platform_boot_layout.h — 設計
> 状態: **現行 (2026-11-18)** — 出所: `sys/include/memmap.h` から PC-98 だけの番地を分けた (ユーザー指示 2026-11-18)
ソース: `sys/platform/pc98/platform_boot_layout.h`

PC-98 のリアルモードの起動と低位メモリの番地の正典 ([C4])。機械と起動の手順で決まる番地だけを置き、
OS の配置 (帯・鎖・池・アプリ帯) は `sys/include/memmap.h` が持つ。

## 何を置くか

| 名前 | 中身 | 使う所 |
|---|---|---|
| `MEM_IVT_PROT_START`・`MEM_IVT_PROT_END` | IVT・BIOS の作業域の保護範囲 | 地図 |
| `MEM_LOADER_START`・`MEM_LOADER_END` | loader.bin の置き場 (IPL のロード先) | `boot/`、`boot/loader.ld` |
| `MEM_BOOT_STACK_TOP`・`MEM_BOOT_RM_STACK_TOP` | ローダのスタック (1MB 未満。ディスク BIOS を呼ぶためにリアルモードへ戻る) | `boot/` |
| `MEM_BOOT_FAT_BUF`・`MEM_BOOT_COMPRESSED_BASE`・`MEM_BOOT_BIOS_PARAM_BASE`・`MEM_HDD_IPL_BASE` | ローダの作業域・圧縮カーネルの読み込み先・HDD の IPL | `boot/` |
| `MEM_BOOT_PROBE_END` | ローダの RAM probe の上限 (排他) | `boot/` |
| `MEM_AUTOPLAY_MAILBOX_BASE`・`MEM_AUTOPLAY_MAILBOX_SIZE` | 自動プレイの mailbox (低位メモリ、game 側の道具と対) | `sys/platform/pc98/legacy/v86_mem.c`、地図 |

置く基準: (1) PC-98 の機械か起動の手順で決まる番地で、(2) 共通のソースと別の機種の像が使わない。
共通のソースが 1 か所でも使う名前 (`MEM_BIOS_ROM_END`・`MEM_BOOTINFO_BASE` など) と、OS が決める配置
(装置の窓の中の割り当て、鎖の最悪の配置など) は `sys/include/memmap.h` に残す。

## 読み方

- C: `#include "platform_boot_layout.h"` (`-Iplatform/pc98`)。中で `memmap.h` を読む。V86 の側は
  `sys/platform/pc98/legacy/v86_mem.h` 経由。共通のソースは読まない (層の検査 `tools/check_layers.py`)。
- 道具: `tools/gen_memmap.py` の `read_defines()` が `sys/include/memmap.h` の後ろにこのヘッダを続けて
  1 つの定義の集まりとして解く (`PLATFORM_HDR_RELS`)。地図 `build/out/MEMMAP.md`・重なりの検査・
  生成ヘッダ `memmap_layout.h` の `LAYOUT_` の名前は、分ける前と同じ中身。
- NASM: 写し `boot/memmap_defs.inc`。値の一致は `tools/tests/test_boot_named_constants.py`。

## 番地を変える時

`sys/include/memmap.h` と同じ ([02 §2-1](../../../02_memory.md))。`make kernel` → `python3 tools/gen_memmap.py --write`
→ `--check`。ローダの番地は NASM の写しも同じ変更で直す。自動プレイの mailbox は game 側の道具と対なので、
配置や大きさを変える時は game 側も同じ変更で直す。
