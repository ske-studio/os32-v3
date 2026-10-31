# kdebug/gfinit_resources.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kdebug/gfinit_resources.c`

読取り検査を共有し、F6 の ID/定数長接続を加える。A1 sizing は診断専用。

## BARLEN_DESIGN

| 実物と設計上の扱い |
| --- |
| resources は除外区間の収集。GPU 測定後の重なり検査は別関数 |
| 82557 の通信・DMA・割込み・BAR を変更せず、Trident は保守的上限で照合 |

既知長を優先し、上限との矛盾は拒否

| vendor:device / BAR | 長さ・属性 | 根拠と確度 |
| --- | --- | --- |
| 8086:1229 / 0 | **0x1000 B**、memory32、prefetchable | Intel rev.1.3 §4.1.10 Table 3、確認メモ `/home/hight/os32-tmp/run/barlen/sources/REFERENCES.txt:12` |
| 8086:1229 / 2 | **0x100000 B**、memory32、non-prefetchable | 同 Table 3、`/home/hight/os32-tmp/run/barlen/sources/REFERENCES.txt:14`。Flash 実容量ではなく aperture |
| 8086:1229 / 1 | I/O 32 B、メモリ除外には入れない | 同 Table 3、`/home/hight/os32-tmp/run/barlen/sources/REFERENCES.txt:13` |

- Intel の一次資料: [8255x Software Developer Manual](https://www.intel.com/content/dam/doc/manual/8255x-10-100-mbps-ethernet-controller-software-dev-manual.pdf)、印刷頁15–17。 BSD 対照: `/home/hight/os32-tmp/ref/freebsd-main/sys/dev/fxp/if_fxpreg.h:31` は MMBA=10h/IOBA=14h、同:37 は CSR。**このヘッダ単独では BAR 長を証明しない**。

- `/home/hight/os32-tmp/ref/freebsd-main/sys/dev/fxp/if_fxp.c:310` は MMBA を memory resource として確保する。固定長の根拠は上の Intel Table 3。

- Trident のメーカー資料所在は `/home/hight/os32-tmp/run/barlen/sources/REFERENCES.txt:17`。HTTP 403 で本文未確認。正確な長さの値の根拠が未取得。 `/home/hight/os32-tmp/run/barlen/sources/netbsd-trident.c:176` は同じ PCI ID を複数型で使う。ID だけで全系列の同一 aperture を保証しない。

- 公開入口 `gfinit_resources(exclude,count)` は不変。成功時だけ count を確定し、失敗時 count=0。

- 既知長表と helper は `gfinit_resources.c` の static。新しい object・KAPI・core import slot は不要。

- 下は境目の設計コード。数値定数は同ファイルの Local Layer に名称付きで定義する。

```c
/* id=device<<16|vendor、bar=0-based、len=正確長、attr=下位nibble */
struct resource_bar_spec { u32 id, bar, len, attr; };
static const struct resource_bar_spec resource_bar_specs[] = {
    {GF_RES_FXP_ID, GF_RES_BAR0, GF_RES_FXP_CSR_LEN, PCI_BAR_MEM_PREFETCH},
    {GF_RES_FXP_ID, GF_RES_BAR2, GF_RES_FXP_FLASH_LEN, GF_RES_ATTR_MEM32}
    /* Trident は正確長の表に入れず、別の許可判定で上限を使用。 */
};
struct resource_bounds { u32 base[RECOVER_RANGE_MAX]; u32 count; };
static int resource_collect_bounds(struct resource_bounds *b); /* 第1走査 */
static u32 resource_cap(const struct resource_bounds *b, u32 base);
static const struct resource_bar_spec *resource_spec(u32 id, u32 bar);
static int resource_memory_bar(struct recover_range *exclude, u32 *used, const struct resource_bounds *b, u32 id, u32 dev, u32 fn, u32 header, u32 bar, u32 raw);
```

| 定数の置き場 | 内容 |
| --- | --- |
| `kdebug/gfinit_resources.c` Local | FXP_ID=0x12298086、BAR0=0、BAR2=2、CSR_LEN=0x1000、FLASH_LEN=0x100000 |
| 同 Local | ATTR_MASK=0xf、ATTR_MEM32=0、BAR_STRIDE=4、TRIDENT_ID=0x96601023、REV=0xd3、ROM_ADDR_MASK=0xfffff800。revision offsetは既存PCI定義を利用 |

既存 GPU ID/BDF と入口。

- 境目の設計コード (`resource_bar_specs`) の `GF_RES_FXP_ID` は `((PCI_DEVICE_82557 << 16) | PCI_VENDOR_INTEL)`、`GF_RES_ATTR_MEM32` は `PCI_BAR_MEM_TYPE32` (どちらも `drivers/pci/pci_decode.h`) に置き換えた。値と動作は同じ。

## HWDEV_DESIGN

PCI 資源収集
