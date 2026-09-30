# Step 05: HDD新ローダー (ext2リーダー + LZ4)

## 目的

HDDローダーにext2最小リーダーとLZ4デコーダを内蔵し、
ext2パーティション上の `/boot/vmkernel.lz4` を読み込んで展開・起動する。

## ブートフロー

```
1. IPL (boot_hdd.asm, LBA 0, 512B)
   → ローダーを LBA 2-17 (8KB) からメモリ 0x8000 にロード
   → far jmp 0000:8000

2. Mini-Loader (0x8000, ~6KB)
   a) IPLからのジオメトリ情報保存
   b) PM移行 (A20, GDT, CR0.PE)
   c) IDE SRST + BSY待ち
   d) ext2スーパーブロック読み込み (パーティション開始LBA)
   e) ルートinode → /boot → vmkernel.lz4 のinode特定
   f) データブロックを順次PIOで 0x10000 に読み込み
   g) VK32ヘッダ解析
   h) 各エントリをLZ4展開 → entry[i].load_addr
   i) メモリプローブ
   j) ESP = 0x9FFFC
   k) far jmp 0x100000 (kentry)
```

## 新規ファイル

### [NEW] boot/loader_hdd_new.asm

新HDDローダー本体。既存の `loader_hdd.asm` から以下を流用:
- PM移行コード (GDT, CR0.PE, セグメント初期化)
- IDE CHS PIO `pm_read_sector` ルーチン
- `pm_wait_bsy` / `pm_wait_drq`
- `pm_print` (TVRAM表示)
- メモリプローブ

新規追加:
- ext2最小リーダーへのC呼び出しブリッジ
- LZ4デコーダへのC呼び出しブリッジ
- VK32ヘッダ解析

構造:
```asm
;; ASMエントリ (16bit)
loader_entry:
    ;; ... PM移行 (既存コード流用) ...

;; 32bit PMコード
pm_entry:
    ;; ... IDE初期化 (既存コード流用) ...
    ;; C関数呼び出し: boot_main()
    call boot_main
    ;; boot_main から戻らない (カーネルへジャンプ)
```

### [NEW] boot/boot_main.c

ローダーのメインロジック (C言語):

```c
/* ext2からvmkernel.lz4を読み込み、LZ4展開してカーネルにジャンプ */
#define LOAD_BUF  0x10000UL
#define MAX_IMAGE_SIZE  (508 * 1024)  /* 508KB: 0x10000-0x8EFFF */

void boot_main(void)
{
    /* 1. ext2初期化 (パーティション開始LBA) */
    ext2m_init(HDD_PARTITION_LBA);

    /* 2. /boot/vmkernel.lz4 を検索 */
    u32 ino = ext2m_lookup("/boot/vmkernel.lz4");

    /* 3. ファイルを LOAD_BUF に読み込み */
    int size = ext2m_read_file(ino, (u8 *)LOAD_BUF, MAX_IMAGE_SIZE);

    /* 4. VK32ヘッダ解析 + LZ4展開 */
    VK32Header *hdr = (VK32Header *)LOAD_BUF;
    u8 *file_base = (u8 *)LOAD_BUF;
    int i;
    for (i = 0; i < (int)hdr->entry_count; i++) {
        /* data_offset はファイル先頭からの絶対オフセット */
        u8 *src = file_base + hdr->entries[i].data_offset;
        u32 csz = hdr->entries[i].compressed_size;
        u32 raw_sz = hdr->entries[i].raw_size;
        lz4_decode(src, csz, (u8 *)hdr->entries[i].load_addr, raw_sz);
    }

    /* 5. カーネルにジャンプ (ASM側で実行) */
}
```

### [NEW] boot/ext2_mini.c

ブートローダー用ext2最小読み取りドライバ。
カーネルの `fs/ext2_*.c` から必要最小限を抽出。

関数:
- `ext2m_init(u32 partition_lba)` — スーパーブロック読み込み
- `ext2m_read_inode(u32 ino, Ext2Inode *out)` — inode読み込み
- `ext2m_lookup(const char *path)` — パス解決 (inode番号返却)
- `ext2m_read_file(u32 ino, u8 *buf, u32 max)` — ファイル全体読み込み

制約:
- ブロックサイズ 1024B 固定 (OS32のext2仕様)
- 直接ブロック (12個) + 単一間接ブロック対応
- 二重間接ブロック不要 (vmkernel.lz4 < 12MB)
- 読み取り専用 (書き込み不要)

推定コードサイズ: ~1.5KB (Cソース) → ~2KB (コンパイル後)

### [NEW] boot/lz4_mini.c

ブートローダー用LZ4デコーダ。
`lib/lz4.c` からそのまま移植 (ヒープ/BSS不使用)。

推定コードサイズ: ~400B

### [NEW] boot/boot_io.c (or .asm)

Cコードからアセンブリの `pm_read_sector` を呼ぶブリッジ:

```c
/* 1セクタ(512B)読み込み — ASM pm_read_sector のラッパー */
extern void boot_read_sector(u32 lba, void *buf);

/* 連続セクタ読み込み */
void boot_read_sectors(u32 lba, u32 count, void *buf);
```

## 変更ファイル

### [MODIFY] boot/boot_hdd.asm

IPLのローダー読み込み範囲を拡張:

```asm
;; 旧: LBA 2 から 4セクタをロード (2KB)
;; 新: LBA 2 から 16セクタをロード (8KB)
mov     ax, 2               ;; 開始LBA (変更なし)
mov     cx, 16              ;; 旧: 4 → 新: 16
```

### [MODIFY] build/boot.mk

新ローダーのビルドルールを追加:

```makefile
# 新HDDローダー (ASM + C混在)
boot/loader_hdd_new.bin: boot/loader_hdd_new.o boot/boot_main.o \
                          boot/ext2_mini.o boot/lz4_mini.o boot/boot_io.o
    $(LD) -m elf_i386 -T boot/loader.ld -o boot/loader_hdd_new.elf $^
    $(OBJCOPY) -O binary boot/loader_hdd_new.elf $@
```

### [NEW] boot/loader.ld

ローダー用リンカスクリプト:

```
SECTIONS {
    . = 0x8000;
    .text : { boot/loader_hdd_new.o(.text) *(.text) }
    .data : { *(.data) *(.rodata) }
    .bss  : { *(.bss) }
    __bss_start = ADDR(.bss);
    __bss_end   = __bss_start + SIZEOF(.bss);
}
```

> **注意**: `objcopy -O binary` ではBSS領域はゼロクリアされない。
> `loader_hdd_new.asm` のPMエントリコードで `__bss_start` 〜 `__bss_end` を
> 明示的にゼロクリアしてから `boot_main()` を呼び出すこと。

## HDDレイアウト (改善後)

```
LBA 0        IPL (512B)
LBA 1        未使用
LBA 2-17     Mini-Loader (8KB)
LBA 18-1631  未使用 (旧カーネル/SQLite領域が不要に)
LBA 1632~    ext2パーティション
               /boot/vmkernel.lz4  ← カーネル圧縮イメージ
               /bin/...
               /sys/...
```

## 検証

```bash
make clean
make all
make deploy-kernel   # 新ローダーをrawセクタに書き込み
                     # vmkernel.lz4 を ext2 /boot/ にコピー
# NP21/W再起動
# HDDブートでカーネル起動確認
```
