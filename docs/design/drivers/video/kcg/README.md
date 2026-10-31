# drivers/video/kcg/README — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN (D1 で移した)
ソース: `drivers/video/kcg/README`

### kcg_module

```c
/* read_ank/read_kanji は既存 kcg_rom_read_* → public_api() → 4枠表 */
```

```c
/* drivers/video/kcg/kcg_module.c: module状態とinvalidate呼出しは元の所有者 */
void kcg_v86_restore(void) {
    platform_display_kcg_code(); /* outp(MODE_FF1_PORT,MFF1_KCG_CODE); io_wait(); */
    public_api()->invalidate();
}
```
