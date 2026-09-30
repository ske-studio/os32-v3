# Step 04: コンベンショナルメモリ再利用

## 目的

ブート完了後、不要になったコンベンショナルメモリ (0x1000-0x8EFFF, 568KB) を
フォントキャッシュ・Unicode変換テーブル・GFXバックバッファに再利用する。

## 設計

### ブート後に不要になる領域

| 領域 | アドレス | 理由 |
|------|---------|------|
| IVT/BDA | 0x1000-0x5FFF | PM後はIDT使用、IVT不要 |
| BIOSトランポリン | 0x6000-0x7FFF | FDCは直接I/O駆動 |
| ローダー | 0x8000-0xFFFF | ブート完了後不要 |
| LZ4一時バッファ | 0x10000-0x8EFFF | ブート完了後不要 (508KB, vmkernel.lz4最大サイズを規定) |

ページ0 (0x0-0xFFF) は **NOT PRESENT** に設定し、NULLポインタ検出に活用。

> **制約**: ブート時のLZ4一時バッファは **508KB** (0x10000-0x8EFFF)。
> vmkernel.lz4 の圧縮後サイズはこの上限を超えてはならない。
> 現在の kernel.bin (~200KB) + sqlite.bin (~370KB) = 570KB の圧縮率 50% で ~285KB。
> 十分な余裕がある。

### コンベンショナルメモリ最終配置

```
0x00000-0x00FFF  NOT PRESENT (NULLポインタ検出)
0x01000-0x49FFF  フォントキャッシュ (292KB)
0x4A000-0x69FFF  Unicode-JIS変換テーブル (128KB)
0x6A000-0x89FFF  GFXバックバッファ (128KB)
0x8A000-0x8EFFF  空き (20KB, 将来用)
0x8F000-0x8FFFF  カーネルスタックガード (NP)
0x90000-0x9FFFF  カーネルスタック (64KB)
```

### カーネル帯域 (データ移動後の最終配置)

Step 02 で暫定的にカーネル帯域内に配置していたフォント/Unicode/GFX を
コンベンショナルメモリに移動した後の状態:

```
0x100000-0x131E5F  カーネル code+data+bss (~200KB)
0x132000-0x181FFF  カーネルヒープ (320KB)
0x182000-0x182FFF  KAPIテーブル (4KB)
0x183000-0x183FFF  SHM前方ガード (NP, 4KB)
0x184000-0x1C3FFF  共有メモリ本体 (256KB)
0x1C4000-0x1C4FFF  SHM後方ガード (NP, 4KB)
0x1C5000-0x1FFFFF  空き/予約 (~236KB)
```

## 実装方針

### 初期化タイミング

`kernel_main` 内で以下の順序:
1. IDT/PIC/PIT/KBD/FDC/IDE 初期化 (従来通り)
2. **ページ0 を NOT PRESENT に設定** (NULLポインタ検出)
3. **0x1000-0x8EFFF を R/W に設定** (旧R/O領域の解除)
4. フォントキャッシュをコンベンショナルにロード
5. Unicodeテーブルをコンベンショナルにロード
6. GFXバックバッファ初期化 (コンベンショナルアドレス使用)

### データ配置の2段階

- **ブート中**: ローダーがカーネルを 0x100000 に展開。
  コンベンショナルメモリはまだローダーコード等が残っている。
- **kernel_main 初期化後**: コンベンショナルメモリを再利用。
  フォント/Unicode はVFSからファイル読み込みなので問題なし。
  GFXバックバッファはゼロ初期化するだけなので問題なし。

## 変更ファイル

### [MODIFY] include/memmap.h

コンベンショナルメモリ配置の最終アドレスに更新:

```c
/* コンベンショナルメモリ再利用域 (ブート後に配置) */
#define MEM_CONV_RECLAIM_START  0x01000UL
#define MEM_CONV_RECLAIM_END    0x8EFFFUL

/* フォントキャッシュ (コンベンショナル) */
#define MEM_FONT_CACHE_BASE     0x01000UL   /* 暫定値から変更 */

/* Unicode-JIS変換テーブル (コンベンショナル) */
#define MEM_UNICODE_TABLE_BASE  0x4A000UL   /* 暫定値から変更 */
#define MEM_UNICODE_TABLE_SIZE  0x20000UL   /* 128KB */

/* GFXバックバッファ (コンベンショナル) */
#define MEM_GFX_BB_BASE         0x6A000UL   /* 暫定値から変更 */

/* NULLポインタ検出 */
#define MEM_NULL_GUARD_END      0x00FFFUL
```

### [MODIFY] kernel/paging.c

ブート後のコンベンショナルメモリ属性変更:

```c
/* paging_init 後に呼び出す新関数 */
void paging_reclaim_conventional(void)
{
    /* ページ0: NOT PRESENT (NULLポインタ検出) */
    paging_set_not_present(0x0, MEM_NULL_GUARD_END);

    /* 0x1000-0x8EFFF: R/W (旧R/O/ローダー領域を解放) */
    paging_set_rw(MEM_CONV_RECLAIM_START, MEM_CONV_RECLAIM_END);
}
```

### [MODIFY] kernel/paging.h

```c
void paging_reclaim_conventional(void);
```

### [MODIFY] kernel/kernel.c

`kernel_main` にコンベンショナルメモリ再利用の初期化を追加:

```c
/* ページング初期化の後 */
paging_init(mem_kb);

/* コンベンショナルメモリ再利用 (ブート後のローダー領域等を解放) */
paging_reclaim_conventional();

/* 以降のフォント/Unicode/GFXロードは新アドレスを使用 */
/* (memmap.h の定数が更新済みなので既存コードで動作) */
```

### [MODIFY] gfx/gfx_core.c

- `MEM_GFX_BB_BASE` の参照先が変わるだけ。コード変更不要。
- 初期化時にバックバッファをゼロクリアする処理は既存のまま。

### [MODIFY] drivers/kcg.c

- `MEM_FONT_CACHE_BASE` の参照先が変わるだけ。コード変更不要。

### [MODIFY] lib/utf8.c

- `MEM_UNICODE_TABLE_BASE` の参照先が変わるだけ。コード変更不要。

## 検証

```bash
make clean
make all
# NP21/Wで起動テスト
# 1. NULLポインタアクセスでページフォルト発生を確認
# 2. フォント表示が正常 (日本語表示テスト)
# 3. GFXモードのプログラム実行 (描画テスト)
```
