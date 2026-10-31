# include/kselftest_probe.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: SLIM_KERNEL_DESIGN (D1 で移した)
ソース: `include/kselftest_probe.h`

```c
#define KSELFTEST_PROBE_VERSION 1u
struct kselftest_exec_view {
    u32 tramp_page;              /* ring3_tramp_pageそのもの。未初期化なら0 */
    const KernelAPI *api;        /* kapiそのもの。読取用、所有権を渡さない */
};
void kdi_selftest_exec_view(struct kselftest_exec_view *out);
u32 kdi_selftest_inj_push(const u8 *p, u32 len, int dest);
void kdi_selftest_lease_result(u32 result);
```

- 新口から試験を実行しない。汎用のread/writeメモリ口・状態識別子switch・新規KAPIスロットは作らない。
- 口は起動自己診断の既存文脈だけで使用。IRQマスクや待機を新たに足さず、元の状態復元順序を保持する。

実体は元の `inj_push` を直返しする。
