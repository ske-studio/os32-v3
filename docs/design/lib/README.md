# lib/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_A_DESIGN・DEP_DIR_D_DESIGN・DEP_DIR_D2_DESIGN (D1 で移した)
ソース: `lib/`

### dma_contract

元の関数名・整数幅・境界式を維持し、ドライバも同じ実体を使う（重複実装なし）。

```c
/* 独立契約。poolの limit 引数と呼出し方は不変 */
int dma_range_ok(u32 pa, u32 size, u32 limit) {
    if (size == 0) return 0;
    if (pa >= limit || size > limit - pa) return 0;
    if (dma_crosses_64k(pa, size)) return 0;
    return 1;
}
```

### kprintf_attr

```c
/* lib/kprintf_attr.c: 既存公開名を維持する互換wrapper */
u8 kprintf_attr_to_pc98(u8 attr) { return con_backend_printf_attr(attr); }
```

### serial_diag

- lib/serial_diag.hの整形helperと既存呼出し名は維持。

## 索引

- [pc98_cell.md](pc98_cell.md)
- [lib/sqlite3/](sqlite3/README.md)
- [lib/utf8.c](utf8.md)
