# Step 03: カーネルコード修正 (1MB配置対応)

## 目的

カーネルが 0x100000 で正しく動作するよう、
アドレス依存コードとページング設定を全て更新する。

## 変更ファイル

### [MODIFY] kernel/kentry.asm

- リンカがエントリポイントを 0x100000 に解決するため、コード変更は不要
- `__bss_start` / `__bss_end` / `__sqlite_*` シンボルはリンカが自動更新
- 変更なし (確認のみ)

### [MODIFY] kernel/paging.c

ページング保護属性を新メモリマップに全面更新:

```c
/* カーネル帯域 (0x100000-0x1FFFFF): R/W */
/* → 初期ループで既にR/Wが設定される。追加変更不要 */

/* カーネル帯域内の空き/予約: NOT PRESENT (動的計算) */
/* → SHM後方ガード以降 〜 0x1FFFFF をNPに */

/* シェルスタックガード (0x300000帯域内) */
paging_set_not_present(MEM_SHELL_GUARD, MEM_SHELL_GUARD + PAGE_SIZE - 1);
/* 旧アドレスと同じ帯域のため大きな変更なし */

/* SQLite拡張域 + 代替スタック (0x200000-0x2FFFFF): 強制R/W */
/* 新アドレス: 0x200000-0x2ADFFF */
```

### [MODIFY] kernel/paging.h

```c
#define PAGING_PT_COUNT  4  /* 維持: 16MBカバーで十分 */
```

### [MODIFY] kernel/kernel.c

- `kernel_main` の引数・初期化フローは変更なし
- `tvram_print` は TVRAM (0xA0000) 直接アクセスのため変更なし
- `kmalloc_init` の引数は `memmap.h` マクロ経由のため自動対応
- `boot_drive` パラメータの判定ロジックは変更なし

### [MODIFY] kernel/shm.c

- `MEM_SHM_*` 定数は memmap.h 経由のため自動対応
- SHMがカーネル帯域 (0x100000-0x1FFFFF) 内に移動するため、
  アドレス定数のみ変更。ロジック変更不要。

### [MODIFY] exec/exec.c

- `MEM_EXEC_LOAD_ADDR` = 0x400000 → **変更なし**
- `MEM_SHELL_LOAD_ADDR` = 0x300000 → **変更なし**
- exec_run のメモリレイアウト計算は従来通り
- **コード変更なし** (アドレス定数が旧値維持のため)

### [MODIFY] exec/exec.h

- コメント中の KAPIテーブルアドレス (0x3F0000) を修正
- EXEC_LOAD_ADDR は MEM_EXEC_LOAD_ADDR (0x400000) → 変更なし

### [MODIFY] exec/exec_heap.c

- ヒープベースアドレスは exec.c から動的に渡されるため変更不要

### [MODIFY] gfx/gfx_core.c

- `MEM_GFX_BB_BASE` は memmap.h 経由。暫定アドレスで自動対応。

### [MODIFY] drivers/kcg.c

- `MEM_FONT_CACHE_BASE` は memmap.h 経由。暫定アドレスで自動対応。

### [MODIFY] lib/utf8.c

- `MEM_UNICODE_TABLE_BASE` は memmap.h 経由。暫定アドレスで自動対応。

### [MODIFY] kapi/kapi_generated.c (自動生成)

- `KAPI_ADDR` は os32_kapi_shared.h 経由。`make kapi` で再生成。

### [MODIFY] programs/libos32text/text_core.c

- KAPI_ADDR の直接参照を main() 引数の KernelAPI* に変更する
  (KAPI_ADDR が動的配置になるため)

## ブートローダー暫定対応

Step 05 で新ローダーを作成するまでの暫定措置として、
既存ローダー (`loader_hdd.asm`) のカーネルロード先を変更:

### [MODIFY] boot/loader_hdd.asm (暫定)

```asm
;; Phase 1: カーネルロード先を 0x100000 に変更
mov     edi, 00100000h       ;; 旧: 00009000h

;; Phase 2: SQLiteロード先を 0x200000 に変更
mov     edi, 00200000h       ;; 旧: 0018A000h

;; カーネルジャンプ先を 0x100000 に変更
db      0EAh
dd      00100000h            ;; 旧: 00009000h
dw      0008h
```

### [MODIFY] boot/loader_fat.asm (暫定)

FDDローダーも同様にロード先とジャンプ先を変更:
- カーネルロード先: 0x100000 (PM移行後にコピー or 直接ロード)
- カーネルジャンプ先: 0x100000

注意: FDDローダーはリアルモードで 0:9000h に読み込み後PMに遷移する。
PM移行後に 0x9000 → 0x100000 にコピーする処理を追加。

※ この暫定方式は Step 06 (06_FDD_LOADER.md) で最終仕様に置換される。
  Step 06 では 0xC000+セグメント切替方式で直接読み込み、PM移行後に統合する。

## 検証

```bash
make clean
make all
# NP21/Wで起動テスト (既存ローダーの暫定修正で動作確認)
# ver コマンドでカーネル動作確認
# shell起動、基本コマンド実行
```
