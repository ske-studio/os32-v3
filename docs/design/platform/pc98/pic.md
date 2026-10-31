# platform/pc98/pic.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_B_DESIGN (D1 で移した)
ソース: `platform/pc98/pic.c`

```c
void pic_init(void);
void irq_enable(unsigned int irq);
void irq_disable(unsigned int irq);
void pic_eoi(unsigned int irq);
```

|操作|固定する I/O と文脈|
|---|---|
|pic_init|前景・起動時 IF=0。master ICW1→offset→ICW3→ICW4、slave 同4件、master mask FF→slave mask FF→master IRR→slave IRR。合計12書込み|
|irq_enable/disable|前景／IRQ 両方。port 選択→irq_save→対象 IMR read→bit clear/set→write→irq_restore。既存範囲・カスケードの扱いを変えない|
|pic_eoi|IRQ 内。irq<8 は master のみ、irq>=8 は slave→master。追加の読み・待ちなし|
|slave_last_in_service|IRQ15 の finish 内 IF=0。slave OCW3_ISR write→ISR read→OCW3_IRR write→bit7 を boolean 化|
|eoi_master|IRQ 内。master OCW2_EOI の1書込み。IRQ15 スプリアスで slave へ送らない|

timer_pending
IF=0 区間で master OCW3_IRR write→read→bit0 を boolean 化。

- PIC 実装はソフト mask の写しを作らない。現物 IMR を毎回読む。IRQ 番号からの plan、登録数と隔離の判定を platform に渡さない。
