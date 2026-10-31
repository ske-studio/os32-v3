# platform/pc98/pci_resources.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D3_DESIGN (D1 で移した)
ソース: `platform/pc98/pci_resources.c`

```c
u32 ledger_resource_import_pci(void) {
    /* pci_get/BAR 解釈はここだけ。現行ループをそのまま保持 */
    struct ledger_resource r = {0};
    /* r の既存各欄を設定、ledger_resource_add(&r, &rid) 成功本数を返す */
}
static u8 pc98_pci_line_bits(unsigned int irq) {
    /* irq_line_quarantined / irq_storm_masked を従来の PCI bit へ変換 */
}
/* PCI_RESOURCES wrapper: import → overflow読取/表示 → hook登録、の順 */
```
台帳の口 `ledger_resource_add` を流用し、新しい資源収集レジストリは足さない。
関数名/宣言は既存利用者のため維持
- BAR 本数 6/2/1、I/O・ゼロ・不明 header の除外、64bit の上位非ゼロ/末尾除外、RAW（幅不明、予約権限なし）、overflow と成功件数を変えない。PCI 設定書込み/サイズ probe は追加しない。
- r は呼出し中のローカル、登録後のレコードは台帳所有。line hook は常駐関数で既存呼出し文脈を継承（IRQ でも使える非待機・非確保の読取りのみ）。IRQ 状態の所有者は kernel のまま。
