# NHD r0形式ファイル構造仕様

出典: T98-Next (2001/01/22 LED)

## 概要

PC9821エミュレータ T98-Next のハードディスクイメージファイル NHD r0形式の構造仕様。
NP21/W でも同形式を使用する。

## 構造

NHD形式の構造は大きくヘッダ部とデータ部に分けられる。
ファイルの先頭からヘッダ部が存在し、その後ろにデータ部が存在する。

### ヘッダ部

```c
typedef struct {
    char  szFileID[15];                 /* 識別ID "T98HDDIMAGE.R0" */
    char  Reserve1[1];                  /* 予約 */
    char  szComment[0x100];             /* イメージコメント(ASCIIz) */
    DWORD dwHeadSize;                   /* ヘッダ部のサイズ */
    DWORD dwCylinder;                   /* シリンダ数 */
    WORD  wHead;                        /* ヘッド数 */
    WORD  wSect;                        /* 1トラックあたりのセクタ数 */
    WORD  wSectLen;                     /* セクタ長 */
    char  Reserve2[2];                  /* 予約 */
    char  Reserve3[0xe0];              /* 予約 */
} NHD_FILE_HEAD, *LP_NHD_FILE_HEAD;
```

**注意:**
- 構造体の境界は1バイト単位 (パック構造体)
- 予約領域は0で埋めること

### フィールドオフセット表

| オフセット | サイズ | フィールド | 説明 |
|:---|:---|:---|:---|
| 0 | 15 | szFileID | `"T98HDDIMAGE.R0"` |
| 15 | 1 | Reserve1 | 予約 (0) |
| 16 | 256 | szComment | イメージコメント (ASCIIz) |
| 272 | 4 | dwHeadSize | ヘッダ部のサイズ (通常 512) |
| 276 | 4 | dwCylinder | シリンダ数 |
| 280 | 2 | wHead | ヘッド数 |
| 282 | 2 | wSect | 1トラックあたりのセクタ数 |
| 284 | 2 | wSectLen | セクタ長 (バイト) |
| 286 | 2 | Reserve2 | 予約 (0) |
| 288 | 224 | Reserve3 | 予約 (0) |
| **512** | | | **ヘッダ部合計** |

### データ部

ファイルの先頭からヘッダ部の `dwHeadSize` バイト以降からデータ部となる。
データ部はシリンダ、ヘッド、セクタの小さい順にデータを連続に配置する。

```
データオフセット = dwHeadSize + ((C * wHead + H) * wSect + S) * wSectLen
```

### OS32 での使用例

| パラメータ | 値 |
|:---|:---|
| dwHeadSize | 512 |
| dwCylinder | 3011 |
| wHead | 8 |
| wSect | 17 |
| wSectLen | 512 |
| ディスク総容量 | 3011 × 8 × 17 × 512 = 約 200MB |

#### OS32 パーティションレイアウト

現行レイアウト (`tools/nhd_deploy.py` が管理。1シリンダ = 8H × 17S = 136セクタ):

| 領域 | CHS | LBA相当 | 内容 |
|:---|:---|:---|:---|
| シリンダ 0 先頭 | C=0 | LBA 0 | IPL (`boot_hdd.bin`) |
| | | LBA 1 | PC-98 パーティションテーブル |
| | | LBA 2-17 | 第2ステージローダー (`loader_hdd.bin`, 最大16セクタ = 8KB) |
| | | LBA 18〜261 | カーネル直接配置領域 (`kernel.bin`, 最大122KB) |
| シリンダ 1〜11 | C=1-11 | LBA 262〜1631 | `sqlite.bin` 直接配置領域 + ブート予約 |
| シリンダ 12〜 | C=12+ | LBA 1632〜 | ext2 パーティション |

※ 通常ブートは ext2 内の `/VMKRNL.LZ4` (LZ4圧縮カーネル) を第2ステージローダーが
展開する方式。LBA 18/262 の直接配置領域はレガシー/フォールバック用途で、
これを書き込んでいた `write-kernel` サブコマンドは削除済み (領域自体は互換のため確保)。

ext2パーティションのNHDファイル上のオフセット:
```
(1 + 1632) * 512 = 836,096 バイト
  1    … NHD ヘッダ (NHD_HEADER_SECTORS)
  1632 … ブート領域 (HDD_PARTITION_LBA)
```
(`nhd_deploy.py` の `PARTITION_SKIP = 1633` / `PARTITION_OFFSET = 836096`。
`losetup --offset` にこの値を使用する。1632 × 512 = 835,584 は NHD ヘッダ
512 バイトを足す前の値なので、これを渡すとパーティションの手前を指す)

ext2 ファイルシステムのフォーマットパラメータ:
```
mkfs.ext2 -b 1024 -I 128 -L OS32_HDD    # ブロック1024B / inode 128B
```

パーティションテーブル (LBA 1) の開始CHSは
`開始シリンダ = 1632 / (8×17) = 12` として `nhd_deploy.py update_partition_table()` が書き込む。
