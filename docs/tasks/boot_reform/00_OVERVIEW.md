# ブートアーキテクチャ改善 — 全体計画

## 目的

raw-sector方式のカーネルロードを廃止し、Linux風の圧縮カーネルイメージ (`vmkernel.lz4`) を
ext2/FAT12ファイルシステムから読み込むブート方式に移行する。

## 主要変更

1. カーネル配置を `0x9000` → `0x100000` (1MB) に移動
2. カーネル+ヒープ+KAPI+SHMを1MB帯域内に統合
3. SQLite帯域を 0x200000 に移動 (1MB帯域)
4. シェル常駐は 0x300000 維持 (旧アドレス)
5. **外部プログラムは 0x400000 維持 (旧アドレス、変更なし)**
6. コンベンショナルメモリをブート後にフォントキャッシュ/Unicode/GFXバッファに再利用
7. VK32ヘッダ付きLZ4圧縮カーネルイメージ (vmkernel.lz4)
8. HDDローダーにext2最小リーダー + LZ4デコーダ内蔵
9. FDDローダーもvmkernel.lz4対応
10. デプロイパイプラインの刷新

## ステップ一覧

| Step | 内容 | 依存 | 状態 |
|------|------|------|------|
| [01](01_VMKERNEL_FORMAT.md) | vmkernel.lz4フォーマット定義 + ビルドツール | なし | ✅ 完了 |
| [02](02_MEMMAP_REFORM.md) | メモリマップ再構築 + リンカスクリプト | なし | ✅ 完了 |
| [03](03_KERNEL_RELOCATE.md) | カーネルコード修正 (1MB配置対応) | Step 02 | ✅ 完了 |
| [04](04_CONV_RECLAIM.md) | コンベンショナルメモリ再利用 | Step 03 | ✅ 完了 |
| [05](05_HDD_LOADER.md) | HDD新ローダー (ext2リーダー + LZ4) | Step 01, 03 | ✅ 完了 |
| [06](06_FDD_LOADER.md) | FDDローダー更新 (vmkernel.lz4対応) | Step 01, 03 | ✅ 完了 |
| [07](07_DEPLOY_PIPELINE.md) | デプロイパイプライン刷新 | Step 05, 06 | ✅ 完了 |

> **注**: 全ステップのコード変更完了済み。`make clean` → `make all` によるフルビルド検証が必要。

## 新メモリマップ概要

```
[コンベンショナル 0x0-0xFFFFF]
  0x00000          NOT PRESENT (NULLポインタ検出)
  0x01000-0x49FFF  フォントキャッシュ (292KB) ※ブート後に配置
  0x4A000-0x69FFF  Unicode-JIS (128KB)        ※ブート後に配置
  0x6A000-0x89FFF  GFXバックバッファ (128KB)   ※ブート後に配置
  0x8F000          スタックガード (NP)
  0x90000-0x9FFFF  カーネルスタック (64KB)
  0xA0000-0xEFFFF  VRAM
  0xF0000-0xFFFFF  BIOS ROM

[カーネル帯域 0x100000-0x1FFFFF, 1MB]
  0x100000  カーネルバイナリ (.text+.data+.bss, ~200KB)
  __bss_end カーネルヒープ (320KB, __bss_end から動的配置)
  +320KB    KAPIテーブル (4KB)
  +4KB      共有メモリ (ガード付き 264KB)
  残り      空き/予約

[SQLite帯域 0x200000-0x2FFFFF, 1MB]
  0x200000  SQLite code+BSS (~579KB)
  +code末尾 SQLite代替スタック (128KB)
  残り      空き/予約

[シェル常駐 0x300000-0x3FFFFF, 1MB]  ※旧アドレス維持
  0x300000  シェル .text+.data+.bss (~113KB)
  ガード    スタックガード (NP)
  ~0x3FFFFF スタック (下向き成長)

[プログラム空間 0x400000-メモリ上限]  ※旧アドレス維持
  0x400000  外部プログラム (最大1MB)
  ガード    sbrk/スタックガードページ
  ~mem_end  プログラムスタック (256KB)
```
