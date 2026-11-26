# boot/lz4_mini.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_SERIAL_HOSTFS 部品 A-1・A-4 の契約を今のソースと照らして要約
ソース: `boot/lz4_mini.c` (`boot_lz4_decode`)・同じ規則の実装 `lib/lz4.c` (`lz4_decode`)・`boot/loader_fat_new.asm` (`pm_lz4_decode`)

LZ4 ブロック形式の展開。HDD ローダの `boot_lz4_decode` (`boot/vk32_boot.c` が呼ぶ)、カーネルの `lz4_decode`、FD ローダの ASM
`pm_lz4_decode` の 3 つが同じ入力に同じ出力を返す。生成側は LZ4 高圧縮 (`tools/mkvmkernel.py`、HC level 12) で、形式は高速の
圧縮と同じなので展開側は圧縮のモードに依らない。

## 3 実装が満たすこと

- 出力は raw ちょうどで、元とバイト一致。展開先の容量はローダと同じ raw ちょうどで、後ろを書き越さない。
- 延長 255 を跨ぐリテラル長・マッチ長 (追加バイトの連続)、offset ≥ 32768 (上位バイトを捨てない)、出力と重なるマッチを正しく扱う。
- 境界: 延長の途中で入力が尽きる・リテラルが入力より長い・offset が 0 か出力の先頭より遠い・offset の途中で入力が尽きる → 失敗。
  リテラル / マッチが出力の容量を越える → 失敗 (`BOOT_LZ4_ERR_OUTPUT`)。足し算は差で比べる (ポインタの桁あふれですり抜けない)。
- ASM: 入口で `cld` する (DF=1 で呼ばれても正しい)。どの出口もフレームを `ebp` から戻す (延長読みで入力の終端に達した出口で
  スタックを崩さない)。

失敗は呼び手 (`vk32_boot` / `pm_vk32_boot`) で `VK32_ERR_DECODE` になる (`docs/design/boot/vk32_boot.md`)。

## 確かめ

- `make check-vmkernel-lz4-host` (`test_vmkernel_lz4.py (旧版 77f7db622389)`、ハーネス `vmkernel_lz4_host.c (旧版 1fea306bc8c5)`): 合成データで上の流れを
  **含むことを数えて**一致を見る (`synthetic`)、`--real` で build/out の kernel.bin、mkvmkernel の上限 (`oversize`・`boundary`、ちょうど
  520,192 は通り +1 は落ち、古い出力も消す)。`lz4_mini.c` と `lib/lz4.c` は `--target` でローダと同じ i386-elf-gcc で組み、ASM は
  `pm_lz4_decode` を切り出して `nasm -f elf32` で組む。
- 入出力の境界の壊れた列は `make check-vk32-crc-host` の `corrupt`。
- カーネル内の Rust デコーダ (`lib/os32_lz4`) は vmkernel.lz4 を展開しないので対象外。
