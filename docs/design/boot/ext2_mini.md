# boot/ext2_mini.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_HDD_INSTALL §1-v3 N8 の契約を今のソースと照らして要約
ソース: `boot/ext2_mini.c` (`ext2m_init`・`ext2m_lookup`・`ext2m_read_file`)・宣言 `boot/boot_defs.h` (`EXT2M_ERR_TOO_BIG`・`EXT2M_ERR_SHORT`)・呼び手 `boot/boot_main.c`

HDD ローダが ext2 の `/boot/vmkernel.lz4` を読むための最小の読み手。

## 上限 (`ext2m_read_file(ino, buf, max_size)`)

- i_size が `max_size` を超えるファイルは**切り詰めずに** `EXT2M_ERR_TOO_BIG` を返し、読み先に 1 バイトも書かない。
- `max_size` ちょうどまでは読み、i_size より後ろを書かない。ブロックの穴で最後まで読めなければ `EXT2M_ERR_SHORT`。
- `boot/boot_main.c` は `MAX_IMAGE_SIZE` (508KiB、`boot/boot_defs.h`) を渡し、`EXT2M_ERR_TOO_BIG` なら画面に
  `vmkernel.lz4 too large (> 508KiB)` を出して止まる。読めた後の検査は `boot/vk32_boot.c` (`docs/design/boot/vk32_boot.md`)。

## 確かめ

`make check-hdd-stage2-host` (`tools/tests/test_hdd_stage2.py`、`tools/tests/ext2_mini_host.c`) のケース `mini`:
1,000B と 508KiB ちょうどは読んで後ろに書かない、508KiB + 1 と 600KiB は `EXT2M_ERR_TOO_BIG` で読み先が変わらない。
