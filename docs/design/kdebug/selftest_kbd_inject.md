# kdebug/selftest_kbd_inject.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: SLIM_KERNEL_DESIGN.md・SLIM_KERNEL_IMPORTS.md (D1 で移した)
ソース: `kdebug/selftest_kbd_inject.c`

## SLIM_KERNEL_DESIGN

既存のdisable/reset/drop復元手順はそのまま移す。

## SLIM_KERNEL_IMPORTS

| 記号 | 種類 | 呼出し名 |
| --- | --- | --- |
| `appslot_cur` | FUNC | `appslot_cur` |
| `appslot_gfx_owner` | FUNC | `appslot_gfx_owner` |
| `con_sink_reader_get` | FUNC | `con_sink_reader_get` |
| `kbd_inject` | FUNC | `kbd_inject` |
| `kbd_inject_pending` | FUNC | `kbd_inject_pending` |
| `kbd_inject_take` | FUNC | `kbd_inject_take` |
| `kbd_inject_peek` | FUNC | `kbd_inject_peek` |
| `kbd_inject_discard` | FUNC | `kbd_inject_discard` |
| `kdi_selftest_inj_push` | FUNC | `kdi_selftest_inj_push` |
| `kbd_inject_drop_count` | GETTER | `kdi_get_kbd_inject_drop_count` |
| `kbd_inject_drop_count` | SETTER | `kdi_set_kbd_inject_drop_count` |
