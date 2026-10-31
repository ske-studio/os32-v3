# drivers/README — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN (D1 で移した)
ソース: `drivers/README`

- PcmModuleApi/FmModuleApi/LanModuleApi 自体は kernel の既存ヘッダに残す。スロット数/順序/sizeof/offsetof と OPS_VERSION は固定。LAN の pci_dev は前方宣言、必要なPCI型は独立契約へ。

- DMA pool状態/割当/leaseはkernel、8237実行状態とPCM ringはdrivers、module_handle/candidateは常駐facade。新登録はない。

## 索引

- [ata/README.md](ata/README.md)
- [block/README.md](block/README.md)
- [block/fdc.md](block/fdc.md)
- [char/README.md](char/README.md)
- [char/serial.md](char/serial.md)
- [input/README.md](input/README.md)
- [input/kbd.md](input/kbd.md)
- [net/e82557/README.md](net/e82557/README.md)
- [net/ne2000/README.md](net/ne2000/README.md)
- [pci/README.md](pci/README.md)
- [sound/cs4231/pcm_cs4231.md](sound/cs4231/pcm_cs4231.md)
- [sound/cs4231/pcm_module.md](sound/cs4231/pcm_module.md)
- [sound/fm/README.md](sound/fm/README.md)
- [video/cirrus/README.md](video/cirrus/README.md)
- [video/cirrus/backend_cirrus.md](video/cirrus/backend_cirrus.md)
- [video/cirrus/wab_cirrus.md](video/cirrus/wab_cirrus.md)
- [video/cirrus/wab_glue.md](video/cirrus/wab_glue.md)
- [video/cirrus/wab_glue_xe10.md](video/cirrus/wab_glue_xe10.md)
- [video/kcg/README.md](video/kcg/README.md)
- [video/pc98/backend_pc98.md](video/pc98/backend_pc98.md)
- [video/pc98/backend_pegc.md](video/pc98/backend_pegc.md)
