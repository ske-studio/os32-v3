# kernel/irq/tick.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D1_DESIGN (D1 で移した)
ソース: `kernel/irq/tick.c`

## 2. 周期表の境界・順序
- 新しいスケジューラは作らず、IRQ 登録と同様の小さい固定表を常駐に置く。動的確保・時刻判定・KAPI 追加なし。
- 6 枠を昇順に走査する。空枠は飛ばす。登録順によらず、従来の前後関係を保つ。

```c
/* kernel/irq/tick.h: 内部口。TICK_SLOT_COUNT=6、エラー値も名前付き定数。 */
typedef void (*tick_fn)(void);
struct tick_slot { tick_fn fn; };
int tick_register(unsigned int slot, tick_fn fn);
void tick_dispatch(void);                 /* IRQ0 内、IF=0 */
/* kernel/irq/tick.c が static struct tick_slot slots[TICK_SLOT_COUNT] を所有。 */
```

- 登録は前景のみ。`kctx_irq_depth != 0`・範囲外・NULL・他関数の同枠登録をエラーにし、表を変えない。同一 `{slot,fn}` は成功扱い。
- `irq_save/restore` 内で検査と公開。IF の元の状態を保つ。dispatch 中に登録不可、最大6呼出し。解除口は不要（登録するのは常駐関数だけ）。
- callback は現状同様 IRQ0 内。待ち・メモリ確保・協調切替を足さない。tick_count 更新、EOI、V86 timeout/反射、IRQ 深さは ASM の現状を保持。

- 初期化前の未登録 callback は、現在の未初期化時 no-op と同じ。ne2k の RUNNING/OVW_WAIT、link_ready、反射フラグの公開より必ず先に登録する。
- façade は登録を残し、既存の tick_target=0 と参照解放で停止する。再ロードは同一登録として成功。表に `.mod` 内のアドレスを置かず、既存 ABI・import 表・参照寿命を変えない。
- snd/pcm/e82557 のモジュール内部実装には登録コードを入れない。単体 host 試験も引き続きその実装を呼べる。
