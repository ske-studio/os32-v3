# kernel/irq/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_B_DESIGN・DEP_DIR_D1_DESIGN (D1 で移した)
ソース: `kernel/irq/`

### irq_math

- スプリアス処理は現行 `irq_eoi_plan` のまま。IRQ7/15 の新たな判定、EOI 統合、I/O wait の追加はしない。

`IRQ_DYN_COUNT=9`。

## 索引

- [kernel/irq/irq.c](irq.md)
- [kernel/irq/tick.c](tick.md)
