# sys/dev/pci/pci_locate.c — 1 本のバスの fn0 を ID で探す共通の口

> 状態: **現行 (2026-11-18)** — 出所: GF_BDF_DESIGN・TASK_GF_BDF (要約して移した)
ソース: `sys/dev/pci/pci_locate.c`・`sys/dev/pci/pci_locate.h`

起動時の PCI の表 (`sys/dev/pci/pci.c` の `g_pci`) を使わず、指定した 1 本のバスの dev 0〜31 の fn0 を生読みして、
vendor/device の dword が一致する装置を探す。CPU・機種・GeForce の名前を持たない。呼び手は今は GeForce の位置の解決
([sys/dev/video/geforce/diag/recover_gf_locate.c](../../kdebug/recover_gf_locate.md)) だけ。

```c
struct pci_locate { u8 found, dev, second_found, second; };
void pci_locate_fn0(u32 bus, u32 id, struct pci_locate *out); /* id = device<<16 | vendor */
```

## 契約

- `*out` を毎回 0 で初めてから、dev 0 → `PCI_DEV_COUNT - 1` の順に `pci_cfg_read32(bus, dev, 0, PCI_CFG_VENDOR_ID)` を読む。
  最初に一致した dev を `dev`、2 番目を `second` に入れる (先勝ち。3 枚目以降は記録しない)。
- 読むのは引数の `bus` の fn0 だけ。fn1〜7・他のバス・ブリッジの先は見ない。
- 頼るのは `pci_cfg_read32` だけ。書きは無い。結果を覚えない (覚えるのは呼び手)。
- 装置の無い機械 (NP21/W は mech#1 absent で全読みが FFFFFFFFh) では `found = 0` で戻る。

## なぜ起動時の表を使わないか

`g_pci` は `PCI_MAX_DEVS` (32) 件で、走査はブリッジで深さ優先に潜るため、溢れるとバス 0 の後ろの装置が落ちる。
GeForce の資源の収集 (`sys/dev/video/geforce/gfinit_resources/scan.inc`) も同じ理由で生読みにしている。

## 他の部品との境目

- PCI config の backend (PC-98 は mech#1 のポート I/O、`sys/dev/pci/pci.c`) は機種・ホストブリッジの側、
  割り込みの禁止などの CPU の原始操作は arch の側。この口は backend の上に乗るだけで、backend の分離はしない。
- PowerPC (G4) でも backend が bus → host bridge を引ければそのまま使える。ただし G4 の GeForce は AGP の host bridge の下で
  bus 番号は bus-range から決まる (0 とは限らない、未確認)。G4 で探すときは呼び手が bus を OF の bus-range から渡すか、
  全バスを順に呼ぶ。PC-98 の呼び手の `RECOVER_GF_BUS 0U` は PC-98 の値。
- この口は単一バスの探索で、PowerPC 全体の列挙や割り込み・割り当ての共通層ではない。

## 確かめ

`make check-pci-locate-host` (`test_pci_locate.py (旧版 fc3107e3e862)` + `pci_locate_host.c (旧版 61612495e760)`) が本物の
`pci_locate.c` と `sys/dev/video/geforce/diag/recover_gf_locate.c` を偽の config 空間 (bus × dev × fn) で組んで回す。

| fixture | 期待 |
|---|---|
| a. 0:12.0 = 10de:0221、0:13.0 = USB (1033:0035 fn0) | dev = 12、2 枚目なし |
| b. 0:13.0 = 10de:0221 | dev = 13 |
| c. 無し | FALLBACK (13) を返し、2 回目の呼び出しも 32 回読む (覚えない) |
| d. 0:12.0 と 0:13.0 の両方 | dev = 12、second = 13 |
| e. a の後の 2 回目 | config の読みが 0 回 (覚えている) |
| f. 10de:0221 が 0:12.1 か 1:0.0 にだけ居る | 見つからない (13) |
| g. `pci_locate_fn0(1, id, …)` | バス 1 だけを読む |

変異 (`--mutate`): M2 後勝ち → d、M5 走査を fn 0〜7 に → f、M6 bus を無視して 0 を読む → g
(M1・M3・M4 は [recover_gf_locate](../../kdebug/recover_gf_locate.md) の側)。
