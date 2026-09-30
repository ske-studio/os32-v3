# Step 01: vmkernel.lz4 フォーマット定義 + ビルドツール

## 目的

kernel.bin と sqlite.bin を個別にLZ4圧縮し、自己記述的なヘッダ付き
イメージファイル `vmkernel.lz4` を生成するフォーマットとツールを作成する。

## VK32ヘッダ仕様 (2エントリ方式)

```
Offset  Size  Field
------  ----  -----
0x00    4     magic: 'VK32' (0x32334B56 LE)
0x04    4     header_size: 共通ヘッダ + 使用エントリ分のサイズ (バイト)
                          = 16 + entry_count * 16
                          (2エントリ時 = 48, データ開始オフセットとしても使用)
0x08    4     version: 1
0x0C    4     entry_count: 2 (kernel + sqlite)
--- entry[0]: kernel ---
0x10    4     load_addr: 0x100000 (展開先アドレス)
0x14    4     raw_size: 展開後サイズ (バイト)
0x18    4     data_offset: LZ4圧縮データの位置 (ファイル先頭からの絶対オフセット)
0x1C    4     compressed_size: LZ4圧縮データサイズ
--- entry[1]: sqlite ---
0x20    4     load_addr: 0x200000
0x24    4     raw_size: 展開後サイズ
0x28    4     data_offset: ファイル先頭からの絶対オフセット
0x2C    4     compressed_size: LZ4圧縮データサイズ
--- data ---
0x30    ...   LZ4圧縮データ[0] (kernel.bin)  ← entry[0].data_offset = 0x30
...     ...   LZ4圧縮データ[1] (sqlite.bin)  ← entry[1].data_offset = 0x30 + entry[0].compressed_size
```

設計メモ:
- `header_size` = データ開始位置。ローダーは `header_size` 以降をデータ領域として扱う
- `data_offset` はファイル先頭からの絶対オフセット (相対ではない)
- entry_count を可変にすることで将来 initrd 等の追加が容易
- CDインストーラーもこのヘッダを解析して `/boot/` にコピーするだけ

## 新規ファイル

### [NEW] tools/mkvmkernel.py

vmkernel.lz4 生成ツール (Python)。

```
Usage: python3 tools/mkvmkernel.py \
         --kernel kernel.bin --kernel-addr 0x100000 \
         --sqlite sqlite.bin --sqlite-addr 0x200000 \
         -o vmkernel.lz4
```

処理:
1. kernel.bin, sqlite.bin を読み込み
2. 各ファイルを Python `lz4.block` で圧縮
3. VK32ヘッダを構築
4. ヘッダ + 圧縮データを結合して出力

依存: `pip install lz4` (ホスト側のみ)

### [NEW] include/vk32.h

VK32ヘッダ構造体の共通定義 (カーネル・ローダー・ツール共用)。

```c
#define VK32_MAGIC  0x32334B56UL  /* 'VK32' LE */
#define VK32_VERSION 1
#define VK32_MAX_ENTRIES 4

typedef struct {
    u32 load_addr;         /* 展開先アドレス */
    u32 raw_size;          /* 展開後サイズ (バイト) */
    u32 data_offset;       /* LZ4データ位置 (ファイル先頭からの絶対オフセット) */
    u32 compressed_size;   /* LZ4圧縮データサイズ */
} VK32Entry;

typedef struct {
    u32 magic;             /* VK32_MAGIC */
    u32 header_size;       /* = 16 + entry_count * 16 (データ開始位置) */
    u32 version;           /* VK32_VERSION */
    u32 entry_count;       /* エントリ数 */
    /* 実際のエントリ数は entry_count 個。
     * ファイル上のヘッダサイズは header_size で決まり、
     * sizeof(VK32Header) とは一致しない場合がある。
     * ローダーは header_size を基準にデータ開始位置を決定すること。 */
    VK32Entry entries[VK32_MAX_ENTRIES];
} VK32Header;
```

## 変更ファイル

### [MODIFY] build/kernel.mk

- `vmkernel.lz4` ビルドターゲット追加:
  ```makefile
  vmkernel.lz4: kernel.bin sqlite.bin
      python3 tools/mkvmkernel.py \
          --kernel kernel.bin --kernel-addr 0x100000 \
          --sqlite sqlite.bin --sqlite-addr 0x200000 \
          -o vmkernel.lz4
  ```
- `kernel` ターゲットに `vmkernel.lz4` を追加
- `clean-kernel` に `vmkernel.lz4` を追加

## 検証

```bash
# ビルド
make vmkernel.lz4

# ヘッダ検証
python3 -c "
import struct
d = open('vmkernel.lz4','rb').read()
magic, hsz, ver, cnt = struct.unpack('<4I', d[:16])
print(f'magic=0x{magic:08X} ver={ver} entries={cnt}')
for i in range(cnt):
    off = 16 + i*16
    addr, raw, doff, csz = struct.unpack('<4I', d[off:off+16])
    print(f'  [{i}] addr=0x{addr:X} raw={raw} data_offset={doff} compressed={csz}')
"
```
