# kernel/irq/irq.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_B_DESIGN・DEP_DIR_D1_DESIGN (D1 で移した)
ソース: `kernel/irq/irq.c`

- IRQ 表 `irq_register/irq_unregister`、登録数、共有配送、storm/quarantine、EOI の判断は kernel 所有。

```c
/* kernel/irq/irq.c: irq_finish の既存 plan と報告条件を維持 */
if (irq == PIC_SLAVE_LAST_IRQ)
    bit7 = platform_irq_slave_last_in_service();
plan = irq_eoi_plan(irq, bit7);
if (plan == IRQ_EOI_SLAVE_MASTER) pic_eoi(irq);
else if (plan == IRQ_EOI_MASTER) platform_irq_eoi_master();
/* IRQ 表・件数・隔離・試験状態は kernel/irq/irq.c に残す。 */
```

- IRQ11 の EOI は `irq_finish` だけから slave → master 各1回。
- irq.h の「全登録者が tick 回収」は **FDC の排他完了通知だけ例外**と明記。FDC は既存 timeout/retry で戻す。NIC 等の tick 保証は緩めず、FDC の新規 poll 処理は作らない。

常駐で解除しない FDC を例外
