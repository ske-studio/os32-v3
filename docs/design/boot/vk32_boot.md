# boot/vk32_boot.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_SERIAL_HOSTFS 部品 A-4 (§1-v3「`.old` の識別はカーネルイメージの CRC」「FD ローダ」) の契約を今のソースと照らして要約
ソース: `boot/vk32_boot.c` (`vk32_boot`)・形式 `boot/boot_defs.h` (「VK32 ヘッダ (v2)」・`VK32_ERR_*`・`VK32_LOAD_MIN`・`VK32_LOAD_END`)・呼び手 `boot/boot_main.c`・FD の同じ手続き `boot/loader_fat_new.asm` (`pm_vk32_boot`)・生成 `tools/mkvmkernel.py`

`vmkernel.lz4` (VK32 v2) を検査して展開する HDD ローダの手続き。FD ローダの `pm_vk32_boot` は**同じ順序・同じ `VK32_ERR_*`**で
検査する (同じ壊れた像に同じ番号を返す)。生成側 (HC level 12・`MAX_IMAGE_SIZE` の上限) は `docs/08_build.md` §8-2。

## 形式 (VK32 v2、リトルエンディアン)

| オフセット | 中身 |
|---|---|
| 0x00 | magic `VK32` |
| 0x04 | header_size = 16 + 20n + 8 (`VK32_HEADER_SIZE(n)`) |
| 0x08 | version = 2 |
| 0x0C | entry_count n (1〜`VK32_MAX_ENTRIES`) |
| 0x10 | entry[n] (16B: load_addr / raw_size / data_offset / compressed_size) |
| 0x10 + 16n | entry_crc[n] (展開後のデータの CRC32) |
| 0x10 + 20n | image_size (ファイル全体の長さ) |
| 0x14 + 20n | image_crc (ファイル全体の CRC32。**この欄を 0 として**計算) |

CRC32 は zlib.crc32 と同じ。v1 の像はどちらのローダも `VK32_ERR_VERSION` で断る (旧ローダとの互換は取らない。
ローダと vmkernel.lz4 は同時に入れ替える)。今の像は core 1 本 (kernel.bin → `VK32_LOAD_MIN`)。

## 検査の順序と戻り値

1. 長さ: 共通部 16B 未満か `MAX_IMAGE_SIZE` 超 → `VK32_ERR_SIZE` (-1)
2. magic → `ERR_MAGIC` (-2)、version → `ERR_VERSION` (-3)、entry_count が 0 か上限超 → `ERR_COUNT` (-4)
3. header_size ≠ `VK32_HEADER_SIZE(n)` かファイルより長い → `ERR_HEADER` (-5)
4. image_size ≠ 読んだ長さ (完全長。切り詰め・1B 足す) → `ERR_LENGTH` (-6)
5. ファイル全体の CRC32 → `ERR_FILE_CRC` (-7)
6. **展開の前に全エントリの範囲**: data_offset ≥ header_size かつ data_offset + compressed_size ≤ ファイル長 (桁あふれも) →
   外れたら `ERR_SRC` (-8)。展開先が [`VK32_LOAD_MIN`, `VK32_LOAD_END`) に収まる・raw_size ≠ 0・エントリ同士が重ならない →
   外れたら `ERR_DST` (-9)
7. エントリごとに展開: LZ4 の失敗 (入力・出力の境界) → `ERR_DECODE` (-10)、decoded ≠ raw_size → `ERR_RAW_SIZE` (-11)、
   展開後の CRC32 → `ERR_ENTRY_CRC` (-12)

足し算は桁あふれしない形 (差で比べる) で書く。1〜6 で止まる像は展開先を 1 バイトも書かない。7 で止まる像は、ヘッダの
エントリ (壊した後の値) の外を 1 バイトも書かない。外れたらローダは理由 (`vk32_strerror`) を画面に出して止まる。

## 起動したイメージの記録

通ったら HDD ローダは `bootinfo_set_image(image_crc, 長さ)` でブート情報域のイメージ欄に書く (形式は
`docs/design/platform/pc98/bootinfo_check.md`)。FD ローダも同じ欄を書く。カーネルは KAPI `boot_image_info` と `ver`・起動画面の
`Image CRC:` で見せ、hsync はこの値と `/boot/vmkernel.lz4` の image_crc を比べて `.old` を作るかを決める
(`docs/manpages/hsync.1`「カーネルの .old」)。

## 写しの一致

`boot/loader_fat_new.asm` の `VK32_*`・`MAX_IMAGE_SIZE` の EQU と `boot/boot_defs.h`、`boot_defs.h` のイメージ欄の写しと
`platform/pc98/bootinfo_wire.h` は名前ごとに一致させる (試験 `mirror`)。

## 確かめ

`make check-vk32-crc-host` (`tools/tests/test_vk32_crc.py`、ハーネス `tools/tests/vk32_host.c`)。C (`--target` で i386-elf-gcc -Os) と
ASM (`VK32_HOST_BEGIN`〜`END` を切り出して `nasm -f elf32`) に同じ像を渡す: `format`・`good`・`corrupt` (43 通り)・`edge` (末尾が
ちょうど帯の上端)・`real` (build/out と images/ の像)。コミット ID は `make check-build-id-host`。
NP21/W・実機の起動と壊した像で止まることはホストでは見ていない。
