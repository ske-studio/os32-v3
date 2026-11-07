# kernel/irq/ — 設計
> 状態: **現行 (2026-11-07)** — 出所: DEP_DIR_B_DESIGN・DEP_DIR_D1_DESIGN (D1 で移した)
ソース: `kernel/irq/`

### irq_math

- スプリアス処理は現行 `irq_eoi_plan` のまま。IRQ7/15 の新たな判定、EOI 統合、I/O wait の追加はしない。

容量は選択先 `platform_irq_limits.h`、PC98 は16線/動的9線。controller 契約は [PPC_SEP2_DESIGN §2](../../../decisions/ppc/PPC_SEP2_DESIGN.md#irqc段)。

## 索引

- [kernel/irq/irq.c](irq.md)
- [kernel/irq/tick.c](tick.md)
