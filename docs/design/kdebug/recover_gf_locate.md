# sys/dev/video/geforce/diag/recover_gf_locate.c — GeForce の PCI の位置を起動時の列挙で決める

> 状態: **現行 (2026-11-18)** — 出所: GF_BDF_DESIGN・TASK_GF_BDF (要約して移した)
ソース: `sys/dev/video/geforce/diag/recover_gf_locate.c`・`sys/dev/video/geforce/diag/recover.h` (`RECOVER_GF_*`)・`sys/dev/video/geforce/geforce_platform.c` (表示)・`sys/dev/video/geforce/diag/gfinit.c` (owner の bdf)・`sys/dev/video/geforce/diag/gf5_test.c` (表示)

GeForce 6200 (10de:0221、NV44A) の位置は決め打ちせず、バス 0 の fn0 を pci_locate_fn0 (`docs/design/drivers/pci/pci_locate.md`) で探した
最初の 1 枚にする。2 枚目は記録して表示するだけで使わない。PCI の書きは見つけた位置にだけ行く。

```c
/* sys/dev/video/geforce/diag/recover.h */
#define RECOVER_GF_BUS 0U
#define RECOVER_GF_FN 0U
#define RECOVER_GF_DEV_FALLBACK 13U          /* 見つからない時 */
#define RECOVER_GF_DEV (recover_gf_dev())
#define RECOVER_GF_BDF (RECOVER_GF_DEV << 3)
#define RECOVER_GF_DEVICE ((PCI_DEVICE_NV44A << 16) | PCI_VENDOR_NVIDIA)
u32 recover_gf_dev(void);
u32 recover_gf_second(void);                 /* U32_MAX = 2 枚目なし */
```

## 契約

- `recover_gf_dev()` は static の `gf_loc` が未解決 (`found = 0`) のときだけ探す。見つかれば覚えて以後は config を読まない。
  **見つからない時は覚えない** (`pci_init` の前に呼ばれても固まらず、後で呼べば探し直す) で、`RECOVER_GF_DEV_FALLBACK` (13) を返す。
- FALLBACK の 13 を返しても安全なのは、どの経路も 0:13.0 の ID を照合して降りる (identify が -1) から。書きの経路
  (`sys/dev/video/geforce/master.c`・`devinit.c`) は identify に合格した後だけ通るので、GPU の無い機械では PCI の書きは 0 回。
- 名前 `RECOVER_GF_*` は呼び出し側の約 70 か所でそのまま使う (マクロが関数を呼ぶ)。`GF_RES_GPU_DEV`
  (`sys/dev/video/geforce/diag/gfinit_resources.h`) も定義の連鎖でこの値になる。
- 静的初期化子では使えない (関数呼び出し) ので、`sys/dev/video/geforce/diag/gfinit.c` の `gf_device` は `.bdf = 0` で置き、
  `gf_inspect()` の `hwdev_claim` の直前で `gf_device.bdf = RECOVER_GF_BDF;` を代入する。

## 4 つの像と値の一致

`pci_locate.c` と `recover_gf_locate.c` は同じ源を次の像に別々に組み込み、像ごとに `gf_loc` を持つ。

| 像 | 組み込み (build) | 最初に呼ぶ所 |
|---|---|---|
| vmkernel | `build/geforce-module.mk` の `GEFORCE_BOOT_OBJS` (`boot_locate.o`・`boot_pci_locate.o`) | `geforce_platform.c` の `reserve()` の identify (`pci_init` の後) |
| geforce.mod | `GEFORCE_SOURCES` | master / devinit / bus の最初の config の読み書き |
| kdebug.bin | `build/kdebug.mk` の `KDEBUG_OBJS` | gfinit・gf1〜gf5 の診断の最初 |
| hwdev.mod (+ VPLL probe の 2 像) | `build/hwdev.mk` の `HWDEV_SHARED` | HWDEV_GF の診断の最初 |

像のあいだで値が一致する根拠: 同じ起動のあいだ PCI の配置は変わらない (抜き差しは電源を切って)、対象は 1 枚、
同じ源・同じ順序 (dev 0→31) で同じ生 config を読む。像のあいだを突き合わせる検査は無く、足さない
(`gf_stage_state.c`・`gfinit_bus.c` の照合は 1 つの像の中の照合)。kexport・KAPI の変更は無い。

## 見つけた位置を使う所

- `geforce_platform.c` の `reserve()`: identify に合格した直後に `[geforce] pci 0:<dev>.0`、2 枚目があれば
  `[geforce] second 0:<dev>.0 ignored` を出す。台帳の `ledger_resource.bdf` も `RECOVER_GF_BDF`。
- `sys/dev/video/geforce/irq.c` の `gf_irq_attach()`: 割り込み線は見つけた BDF の `PCI_CFG_INT_LINE` (0x3C) を読む (固定の線は持たない)。
- `sys/dev/video/geforce/diag/gf5_test.c` の `info()`: `GF5 DATA stage=info target=0:<dev>.0/10de:0221` (行の形は固定)。
- 位置ではなく ID + subsys に結びつく決まり (BAR 長の定数、[production](../hwdev/gf/production.md)・[resources](../hwdev/gf/resources.md)) は
  位置が変わっても崩れない。資源の門の「ブリッジは GPU の経路外」はバス 0 に限ることで保たれる。

## 確かめ

| 検査 | 見ているもの |
|---|---|
| `make check-pci-locate-host` | a〜g の fixture (`docs/design/drivers/pci/pci_locate.md`)。変異 M1 常に FALLBACK → a・d、M3 毎回探す → e、M4 見つからない時も覚える → c |
| `test_gf_production.py (旧版 2dbef53dc1b3)` (`make check-gf-production-host`) | 本物の探索 + identify + `sys/dev/video/geforce/master.c` の実体を 3 構成で: GPU = 13、GPU = 12 + USB = 13 (identify・BAR の読み・Command 04h と cache line / latency 0Ch・0Dh の宛先がすべて 0:12.0)、GPU 無し (`pci_cfg_write16`/`write32` が 0 回)。変異「master の宛先を 13 の直値」は dev = 12 で落ちる。`nm` の検査は ` recover_` のうち `recover_gf_dev`・`recover_gf_second` だけを許し、` kpeek_`・` trial_` の混入を禁じる |
| `test_gfinit_bus.py (旧版 eff4ca908c50)` (`gfinit_bus_host.c`) | 偽 PCI が `GF_T_DEV` (= `recover_gf_test_dev`) だけを受ける。実測の Ra266 fixture (GPU = 13・別カード = 12) と、USB の OHCI/EHCI を 13 の fn0〜2 に置いて `GF_T_DEV = 12` に切り替えた資源の門 |
| `test_gfinit_owner.py (旧版 e9e4a225c2c9)` | claim の後の `gf_device.bdf` が `recover_gf_test_dev << 3` (変異 `bdf_claim`) |
| `test_gf5.py (旧版 e11bfb5c2301)` | `target=0:` が dev に追従する (変異 `bdf-display`) |

偽 PCI が `dev == RECOVER_GF_DEV` を assert する host 試験は、本物を組むと探索が偽 PCI を読んで再帰するので
`recover_gf_dev_stub.c (旧版 1db0aad791f0)` (`recover_gf_test_dev` を返すだけ) を組む。

NP21/W (PCI 無し): `[pci] mech#1 absent`、`[geforce] boot_reserve fail stage=` が位置の解決を入れる前と同じ段、`[geforce] pci` の行は出ない、kdebug の gfinit は拒む。

## 実機で確かめた事 (Ra266)

列挙で見つかり、register = 0 で上がった構成:

- GeForce 0:13.0 (USB カードを 0:12)
- GeForce 0:12.0 (USB カードを 0:13)
- GeForce 0:12.0 (SCSI 1000:000f を 0:13)

未採取: GeForce を抜いた構成 (identify が -1、98 の表示のまま起動、PCI の書き無し)、GeForce・USB・HighPoint を全部挿した構成。

## 残り

- `sys/dev/video/geforce/gf_accept.c` (受入版) は fn に `RECOVER_GF_FN` でなく直値 0 を渡している (値は同じ)。
- 実機の基準構成の表 (`docs/decisions/realhw/RA266.md` §1-1 の PCI の行) を今のスロットの並びに直すかはユーザーの判断待ち。
