# Step 02: メモリマップ再構築 + リンカスクリプト

## 目的

カーネルを1MB (0x100000) に移動し、カーネル+ヒープ+KAPI+SHMを
1MB帯域に統合。外部プログラムのロードアドレス (0x400000) は変更しない。

## 設計方針

- カーネル帯域 (0x100000-0x1FFFFF): カーネル+ヒープ+KAPI+SHMを統合
- SQLite帯域 (0x200000-0x2FFFFF): SQLite code/BSS + 代替スタック
- シェル常駐 (0x300000-0x3FFFFF): 旧アドレス維持
- プログラム空間 (0x400000-mem_end): **旧アドレス維持、変更なし**

## 新旧アドレス対照表

| 領域 | 旧アドレス | 新アドレス | サイズ枠 | 備考 |
|------|-----------|-----------|---------|------|
| カーネル本体 | 0x09000 | 0x100000 | 1MB | 変更 |
| カーネルヒープ | 0x40000 | __bss_end動的 | 320KB | 動的配置 |
| KAPIテーブル | 0x189000 | ヒープ直後 | 4KB | 動的配置 |
| 共有メモリ | 0x381000 | KAPI直後 | 264KB | カーネル帯域内 |
| SQLite拡張域 | 0x18A000 | 0x200000 | 768KB | 変更 |
| SQLite代替スタック | 0x220000 | SQLite直後 | 128KB | 変更 |
| シェル常駐 | 0x300000 | **0x300000** | 1MB | **変更なし** |
| プログラム空間 | 0x400000 | **0x400000** | 1MB+ | **変更なし** |

## カーネル帯域レイアウト (0x100000-0x1FFFFF)

カーネルBSS末尾 (__bss_end) から動的にヒープ等を配置:

```
0x100000           カーネル .text+.data+.bss (現在 ~200KB)
__bss_end (align)  カーネルヒープ (320KB)
+0x50000           KAPIテーブル (4KB)
+0x1000            SHM前方ガード (NP, 4KB)
+0x1000            共有メモリ本体 (256KB)
+0x40000           SHM後方ガード (NP, 4KB)
+0x1000            空き/予約
0x1FFFFF           カーネル帯域終端
```

現在の __bss_end = 0x131E60 の場合の具体的配置:

```
0x100000-0x131E5F  カーネル code+data+bss (~200KB)
0x132000-0x181FFF  カーネルヒープ (320KB)
0x182000-0x182FFF  KAPIテーブル (4KB)
0x183000-0x183FFF  SHM前方ガード (NP, 4KB)
0x184000-0x1C3FFF  共有メモリ本体 (256KB)
0x1C4000-0x1C4FFF  SHM後方ガード (NP, 4KB)
0x1C5000-0x1FFFFF  空き/予約 (~236KB)
```

## SQLite帯域レイアウト (0x200000-0x2FFFFF)

```
0x200000-0x28D45F  SQLite code+data+bss (~579KB)
0x28E000-0x2ADFFF  SQLite代替スタック (128KB)
0x2AE000-0x2FFFFF  空き/予約 (~328KB)
```

## 変更ファイル

### [MODIFY] include/memmap.h

全アドレス定数を新メモリマップに更新。主な変更:

```c
#define KERNEL_LOAD_ADDR      0x100000UL  /* 旧: 0x9000 */
#define KHEAP_SIZE            0x050000UL  /* 320KB 維持 */

/* ★ KHEAP_BASE は __bss_end から動的算出 */
extern u32 __bss_end;
#define KHEAP_BASE  ((((u32)&__bss_end) + 0xFFF) & ~0xFFFUL)

/* ブート後にコンベンショナルメモリに再配置 (Step 04) */
/* 暫定値: KHEAP 直後の固定オフセット */
#define MEM_FONT_CACHE_BASE   (KHEAP_BASE + KHEAP_SIZE + 0x1000)  /* 暫定 */
#define MEM_UNICODE_TABLE_BASE (MEM_FONT_CACHE_BASE + 0x49000)    /* 暫定 */
#define MEM_GFX_BB_BASE       (MEM_UNICODE_TABLE_BASE + 0x20000)  /* 暫定 */

#define MEM_SQLITE_STACK_SIZE  0x020000UL  /* 128KB 維持 */

/* ★ シェル・プログラム空間は旧アドレス維持 */
#define MEM_SHELL_LOAD_ADDR   0x300000UL  /* 変更なし */
#define MEM_EXEC_LOAD_ADDR    0x400000UL  /* 変更なし */
#define MEM_EXEC_MAX_SIZE     (0x100000UL)
#define MEM_EXEC_STACK_SIZE   0x40000UL
```

### [MODIFY] include/os32_kapi_shared.h

```c
/* KAPI_ADDR は __bss_end から動的算出された値を使用 */
/* → kernel.c の exec_init() で実行時に計算・書き込み */
/* 外部プログラム側: crt0 の引数として KernelAPI* を受け取るため、 */
/* KAPI_ADDR の定数定義は不要化を検討。暫定的に KHEAP 末尾固定: */
#define KAPI_ADDR  (KHEAP_BASE + KHEAP_SIZE)
```

> **注意**: KAPI_ADDR を完全動的にする場合、外部プログラムは
> main() の第3引数 (KernelAPI *api) のみを使用し、KAPI_ADDR 定数を
> 直接参照しないよう統一する必要がある。現在 libos32text が
> KAPI_ADDR を直接参照しているため修正が必要。

### [MODIFY] build/os32.ld

カーネルリンク開始アドレスとSQLite配置を変更:

```
SECTIONS {
    . = 0x100000;           /* 旧: 0x9000 */
    .text : { ... }
    ...
    . = 0x200000;           /* 旧: 0x18A000 → SQLite帯域 */
    .sqlite_text : { ... }
    ...
}
```

### [MODIFY] build/app.ld

**変更なし** (0x400000 のまま):

```
. = 0x400000;              /* 旧値維持 */
```

### [MODIFY] kernel/pgalloc.h / pgalloc.c

**変更なし**: `PGALLOC_BASE = MEM_EXEC_LOAD_ADDR = 0x400000` のまま。

## 検証

```bash
make clean
make kernel
i386-elf-nm kernel.elf | grep -E "kentry|__bss|__sqlite"
# kentry が 0x100000、__sqlite_start が 0x200000 であることを確認
```
