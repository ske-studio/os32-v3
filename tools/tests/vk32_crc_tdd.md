# vk32_crc 検証記録 (VK32 v2 の CRC32 表とローダの検査、コミット ID)

票: [TASK_SERIAL_HOSTFS](../../docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md) 部品 A-4、
§1-v3「`.old` の識別はカーネルイメージの CRC」「FD ローダ」、ユーザー決裁 (2026-09-24) の 4
(旧ローダとの互換は取らない)。

## この文書の性格

実装を先に書き、試験は後から足した。RED の根拠は**変異がすべて RED になること**
(下の表) と、**基点 3e22825 の旧 `pm_lz4_decode` をそのまま差し戻すとハーネスが落ちる**
こと (延長読みで入力の終端に達すると `.lz4_err` がスタックを崩す) で、変更前のコードで
試験を回した RED→GREEN の経過ではない ([V4])。

## 実行

```
python3 -B tools/tests/test_vk32_crc.py --real --target --mutate   # make check-vk32-crc-host
python3 -B tools/tests/test_build_id.py --mutate                   # make check-build-id-host
```

ハーネス `vk32_host.c` は libc なしの 32bit 静的 ELF (int 0x80 で入出力)。
`boot/vk32_boot.c` + `boot/lz4_mini.c` は `--target` で**ローダと同じ i386-elf-gcc -Os**
で組む。FD ローダは `boot/loader_fat_new.asm` の `VK32_HOST_BEGIN`〜`END` を**そのまま
切り出して** `nasm -f elf32` で組む。展開先は [0x100000, 0x2E8000) と同じ大きさの窓で、
0xCC で埋めて後ろに番兵 4KB を置く。ASM は **DF=1 で呼ぶ** (自分で `cld` すること)。

FD ローダの実モード部のうち FAT チェーンの検査 (`fat_chain_check` / `fat12_next32`) は、
**32 ビットのレジスタと番地だけ**で書いてある (実モードでは 66h/67h 前置で同じ意味、
番地は 6000h + 最大 4.3KB で 64KB を越えない)。同じソース (`FAT_HOST_BEGIN`〜`END`) を
bits 32 で組み、2HD / 1.44MB の両ジオメトリの `MAX_CLUSTER` で回す。

## 検査している振る舞い

| ケース | 内容 |
|---|---|
| mirror | ASM の `VK32_*` / `MAX_IMAGE_SIZE` の EQU と `boot/boot_defs.h`、`boot_defs.h` のイメージ欄と `include/bootinfo.h` が名前ごとに一致 (番地の 3 つは `tools/gen_memmap.py --check` の MIRRORS) |
| format | 生成物の `entry_crc` / `image_crc` / `image_size` / `header_size` を `zlib.crc32` と長さで突き合わせる |
| good | C と ASM がどちらも 0 と同じ `image_crc` を返し、窓のエントリ部が元の kernel / sqlite と一致、外は 0xCC のまま |
| corrupt (43 通り) | 空・15B・上限 +1・magic・version 1・entry_count 0 / 5・header_size (+1 / 旧形式 / 途中まで)・切り詰め (header だけ / 末尾 1B)・1B 足す・image_size +1・1 ビット反転 5 か所・data_offset (ファイル長 / +1 / header の中 / 桁あふれ)・compressed_size (+1 / 0xFFFFFFFF)・load_addr (帯の下 / 読み込み域 / 上端 / 末尾が上端 +1 / 桁あふれ)・重なり・raw_size 0 / +1 / -1・壊れた LZ4 列 7 種 (リテラル延長・マッチ延長の途中で尽きる、リテラルが入力より長い、offset が遠い / 0 / 途中で尽きる、マッチが出力を越える)・entry_crc 2 種。**C と ASM が同じ `VK32_ERR_*`** を返し、展開前に止まる 32 通りは窓を 1 バイトも書かない。展開の後に止まる 11 通りは、ヘッダのエントリ (壊した後の値) の外を 1 バイトも書かない (2026-09-24、Fable の指摘) |
| edge | 末尾がちょうど 0x2E8000 のエントリは通る |
| fat (2HD / 1.44MB) | 連続・飛び飛び・1 クラスタ・最後の有効クラスタ・上限ちょうど (508KiB) は通る。長さ 0 / 上限 +1、早期終端 (EOC 0xFFF / 0xFF8)、長すぎる、循環 (13→11)、自己循環、**開始クラスタの壊れ** (0 / 1 / MAX_CLUSTER / 0xFF0 / 0xFF7 / EOC 0xFF8・0xFFF → FATCHK_START、文言「bad start cluster」)、途中の範囲外 (同じ値 → FATCHK_RANGE)、範囲外なのに欄が EOC (開始と途中) を区別する。EBX / EDX / ESI を保つ |
| real (`--real`) | build/out/vmkernel.lz4 で format (中身は python-lz4 で展開したものを正とする) / good / corrupt。images/ の 2HD / 1.44MB から `VMKRNL.LZ4` と `LOADER.BIN` を FAT で辿り、`fat_chain_check` が通すこと・FD の `VMKRNL.LZ4` を C / ASM が通すこと・`LOADER.BIN` が boot/ の実物と一致すること。build/out の kernel.bin とは比べない (make check の並列の中で kernel が組み直されて一瞬食い違うため) (ローダが 1 クラスタを超えても IPL が辿る形になっている) |

`test_build_id.py`: 一時の git リポジトリで clean = `rev-parse --short=7`、未追跡だけなら
dirty にしない、追跡中の変更・index に載せた変更で `-dirty`、リポジトリでない / git が無い
で `unknown`、同じ中身なら書き直さない (mtime が動かない)、生成した C を組んで文字列が一致、
長さが `BUILD_COMMIT_MAX` 未満。サブモジュールの中の変更・生成物では dirty にせず (`--ignore-submodules=dirty`)、サブモジュールの指すコミットが記録と違えば `-dirty`。

## 変異

`test_vk32_crc.py --mutate` は 39 件 (対照 1 を含む)。組めない変異は ERROR に数える。
2026-09-24 (Fable の minor の後): **RED 38 / 対照 GREEN 1 / 生き残り・ERROR 0**。

| 対象 | 変異 |
|---|---|
| C (`vk32_boot.c`) | 完全長を見ない、ファイル CRC を見ない、展開後 CRC を見ない、末尾が帯を越えるのを見ない、重なりを見ない、decoded == raw_size を見ない、entry_count 0 を通す、compressed_size の範囲を見ない、data_offset が header の中を通す、CRC の欄を 0 として計算しない |
| C (`lz4_mini.c`) | リテラル / マッチの出力境界を見ない (マッチはエントリの外を書くことで捕まる) |
| ASM (`pm_vk32_boot`) | ファイル CRC・展開後 CRC・decoded == raw_size・完全長・重なり・末尾の帯越え・compressed_size の範囲・entry_count 0 のそれぞれを見ない |
| ASM (`pm_lz4_decode`) | リテラル / マッチの出力境界、リテラルの入力境界、offset の遠さを見ない、**基点の旧版に差し戻す** (SIGSEGV で落ちる)、`cld` しない |
| ASM (FAT) | 開始 / 途中それぞれで MAX_CLUSTER 以上を通す・クラスタ 0 / 1 を通す、開始の壊れを範囲外と同じ文言にする、必要数の次が EOC でなくても通す (循環)、奇数クラスタの上位 12 ビットを取らない、長さ 0 を通す |
| mkvmkernel | CRC の欄を 0 にせずに計算、image_size を 1 ずらす、エントリ CRC を 1 バイト抜きで計算、header_size を 4 多く書く |

`test_build_id.py --mutate` は 8 件 (対照 1): RED 7 / 対照 GREEN 1 (サブモジュールの中の変更で dirty にする変異を含む)。

## 未検証

- **NP21/W と実機での起動** (FD 2HD / 1.44MB、HDD)、壊したイメージで両ローダが画面に
  理由を出して止まること、`ver` / 起動画面の `Commit:` `Image CRC:`、kselftest。
  ホストでは同じ手続きを同じ入力で回しただけで、BIOS の読み込み・実モードでの 66h/67h 前置の
  実行・実メモリへの展開・ブート情報域の書き込みとカーネルの写しは見ていない。
- HDD ローダの `boot_main.c` (`bootinfo_set_image`) と FD ローダの PM 入口
  (`pm_entry32` のイメージ欄の書き込み、`.vk32_fail` の文言表) はホストで回していない。
- IPL (`boot_fat.asm`) は変えていない。複数クラスタのローダを読むことは images/ の FAT で
  チェーンが正しいことまでを見た (IPL のコードを実行したわけではない)。
