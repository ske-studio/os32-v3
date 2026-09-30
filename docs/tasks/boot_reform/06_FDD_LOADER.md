# Step 06: FDDローダー更新 (vmkernel.lz4 対応)

## 目的

FDDブートローダーを更新し、FAT12上の `VMKRNL.LZ4` を読み込んで
LZ4展開 → 0x100000 にカーネルを配置して起動する。

## ブートフロー

```
1. boot_fat.asm (セクタ0, 1024B)
   → FAT12からLOADER.BINを検索・0x8000にロード
   → far jmp 0000:8000

2. loader_fat_new (LOADER.BIN, FAT12ファイル)
   a) FAT12からVMKRNL.LZ4を検索
   b) セグメント切替しながらリアルモードで全データ読み込み
      → 0:C000h (16KB) + 1000:0000h〜 (64KB境界毎にセグメント切替)
   c) PM移行 (A20, GDT, CR0.PE)
   d) PM後: リアルモードで読んだデータを 0x10000 にコピー
      (セグメント跨ぎの断片を線形アドレスに再配置)
   e) VK32ヘッダ解析 + LZ4展開 → entry[i].load_addr
   f) メモリプローブ
   g) ESP = 0x9FFFC
   h) far jmp 0x100000 (kentry)
```

## 変更ファイル

### [MODIFY] boot/loader_fat.asm → loader_fat_new.asm

主な変更点:

1. **検索ファイル名変更**:
   ```asm
   kern_name:  db 'VMKRNL  LZ4'  ;; 旧: 'KERNEL  BIN'
   ```

2. **PM移行後の処理追加**:
   - リアルモードで読み込んだデータ断片を 0x10000 に統合
   - VK32ヘッダ解析
   - LZ4展開ループ (HDDローダーと同じロジック)

3. **カーネルジャンプ先変更**:
   ```asm
   dd  00100000h    ;; 旧: 00009000h
   ```

4. **LZ4デコーダ埋め込み**:
   - `lz4_mini` のコードをローダー内に含める
   - LOADER.BINはFAT12ファイルなのでサイズ制限が緩い

### [MODIFY] boot/boot_fat.asm

- `LOADER.BIN` の検索名は変更なし (同じファイル名)
- コード変更不要

### [MODIFY] build/image.mk

FDDイメージにVMKRNL.LZ4を含める:

```makefile
# 旧: /kernel.bin=kernel.bin
# 新: /VMKRNL.LZ4=vmkernel.lz4
args="$$args /VMKRNL.LZ4=vmkernel.lz4"; \
```

### [MODIFY] build/boot.mk

新FDDローダーのビルドルール追加。

## FDDイメージレイアウト

```
FAT12 (PC-98 2HD, 1232セクタ × 1024B = 1.2MB)
  /LOADER.BIN      新ローダー (~4KB)
  /VMKRNL.LZ4      圧縮カーネルイメージ (~280KB)
  /sys/shell.bin    シェル
  /sys/unicode.bin  Unicodeテーブル
  /bin/...          コマンド群
  /sbin/install.bin HDDインストーラー
```

## 検証

```bash
make clean
make all
make images/os32_boot.d88
# NP21/WでFDDブートテスト
# カーネル起動 + シェル動作確認
```
