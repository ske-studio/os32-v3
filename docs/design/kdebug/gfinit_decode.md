# kdebug/gfinit_decode.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kdebug/gfinit_decode.c`

**解読は現行の switch と `.inc` をそのまま共有する**。実 ROM で確認済みの解読を 27 本へ配り直さない（F3、`O/kdebug/gfinit_decode.c:18–55`）。
OS32 固有差分も保持：PMC_ENABLE bit0 を立てる、RESET 後 1850 は 0、CR3C の組を確認する。根拠は `O/kdebug/gfinit_op/pll2_zm_reg_decode.inc:3`、`reset_expand.inc:10`、`D/GF6200_4A_DESIGN.md:263`。nouveau 全機種対応への拡張はしない。
下層 A の契約は `int gf_op_<name>_run(struct gf_init_context *, const struct gf_instruction *)`。解読は現行 `decode_instruction` と `.inc` の一つの実体を共有し、命令別 decode API は増やさない。
switch と既存 include 構成を保って共有。recover 型からの切離しは薄い接続だけ。27 本へ再分配しない。

## 索引

- [hwdev/gf/](../hwdev/gf/README.md)
