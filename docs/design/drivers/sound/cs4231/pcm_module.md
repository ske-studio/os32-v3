# drivers/sound/cs4231/pcm_module.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN (D1 で移した)
ソース: `drivers/sound/cs4231/pcm_module.c`

- 常駐側は登録成功したIRQ番号を保存する。固定番号検査を外すが、引数・二重登録・module状態・irq_register自身の既存検査は残す。

```c
static unsigned int irq_line; /* pcm_module.c、irq_targetと同じ寿命 */
/* register: 既存 module_ref_acquire → irq_register の後 */
if (rc) module_ref_release(pcm_handle, MODULE_REF_IRQ);
else { irq_line = irq; irq_target = fn; }
/* unregister: irq_targetが存在するときだけirq_lineを照合 */
if (arg || !irq_target || irq_line != irq || irq_target != fn) return IRQ_ERR_INVAL;
/* detach: 既存の IF=0 内、quietとtick解除の後 */
int rc = !irq_target || !pcm_module_irq_unregister(irq_line, irq_target, 0);
```

- unregister成功時だけ irq_target/irq_line を消し参照を返す。失敗時は保持してunloadを止める。登録失敗ではirq_lineを公開しない。

| ファイル | 登録 |
|---|---|
| `drivers/sound/cs4231/pcm_module.c` | `pcm_module_load()` の先頭、同様に pcm façade を登録 |

- tick_register が失敗したときの load の戻り値は各部品の既存の初期化失敗の値 (FM/PCM/LAN の MODULE_INIT_FAILED 相当、今の load の失敗の経路と同じ)。
