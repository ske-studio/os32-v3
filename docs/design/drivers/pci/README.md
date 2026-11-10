# drivers/pci — 設計
> 状態: **現行 (2026-11-10)**
ソース: `drivers/pci/pci.c`、`drivers/pci/pci_barsize.c`

BOOT_PCI_ENUM の列挙時に、各装置の BAR を一度測り、設定を戻してから記録する。
公開 `pci_dev` は生値の写しを返す。長さ・種類・prefetch・未割当状態は別表に持ち、
私的な `pci_bar_get` で BDF / config offset を指定して照会する。照会は測定しない。

- 通常 BAR は対応する IO / MEM decode を WORD Command 書込みで止める。
  Status の W1C ビットを含む DWORD RMW は使わない。
- 64bit は上下とも全ビット 1 を書いてから読み、下位・上位・Command の順に戻す。
- Type 0 / Type 1 / CardBus は 6 / 2 / 1 本。ブリッジの転送窓は sizing しない。
- ROM は Type 0 の 0x30 / Type 1 の 0x38。起動時に enable=1 のものだけ、enable を立てずに測定し、元に戻す。
- 測定1回の保存から復元までを irq_save / irq_restore で囲む。
- class 01.01 の IDE は progif の native bit が0のチャネルに対応する BAR0〜3 を測らない (FreeBSD pci_ata_maps)。
- ブリッジの公開 `pci_dev.bar[2..5]` は転送窓などを BAR として返さず、0 を返す。
- BIOS の割当てを維持する。表が満杯でも測定・復元を続け、保存できなかった記録の照会は失敗する。

手順・FreeBSD の file:line・メーカー資料・GeForce の除外契約は
[資源収集設計](../../kdebug/gfinit_resources.md) に集約する。
`pci_decode.h` は config offset / attribute / address mask の定数と純粋な復号を持つ。
