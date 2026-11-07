# kernel/irq/irq.c — 設計
> 状態: **現行 (2026-11-07)** — 出所: DEP_DIR_B_DESIGN・DEP_DIR_D1_DESIGN (D1 で移した)
ソース: `kernel/irq/irq.c`

- IRQ 表、登録数、共有配送、storm/quarantine は kernel 所有。線一覧・予約・EOI は platform 所有。
- `irq_dispatch` は claim → deliver 時だけ共有配送 → needs_finish 時だけ complete → 未処理報告。claim 失敗時は配送も完了もしない。
- 互換 `irq_finish` は claim → complete → 報告で、dispatch と併用しない。PC98 は固定線を claim せず、IRQ0/7 等の既存 ASM だけが完了する。
- PC98 IRQ15 は配送後に ISR を読み、IRR 選択へ戻し、実 IRQ なら slave → master、spurious なら master のみ。handled から EOI を決めない。
- `irq_topology_check` を PIC 初期化直後・unmask 前に実行。不一致は診断後停止。32線以上の状態は irq_line が正、u32 診断は下位32線だけ。

- irq.h の「全登録者が tick 回収」は **FDC の排他完了通知だけ例外**と明記。FDC は既存 timeout/retry で戻す。NIC 等の tick 保証は緩めず、FDC の新規 poll 処理は作らない。

常駐で解除しない FDC を例外
