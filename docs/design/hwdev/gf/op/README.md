# hwdev/gf/op/ — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/`

> [要確認] 旧静的展開の substep と現行 shared runner は一致を確認していない。

共通計算は共有する。
head/skip、SUB の復帰、REPEAT、RESET の強制実行・184C 復元、CR のラッチ復元は各命令試験に残す。skip 中の CR44 による head 更新も維持する（`O/kdebug/gfinit_decode.c:79–96`）。

F8：新規 host は実 ROM fixture の差分試験一つと REPEAT/SUB/CONDITION/MACRO/RESET/PLL の個別 case が中心。全命令へ一律 4 case を新設しない。skip/head、呼出し/反復上限、RESET 強制実行/184C 復元、PLL 26/7/1/1/2 と失敗点を重点化する。
既存試験は機能別の入口へ移し、fake bus/clock を共有する。削除後も旧 executor を試験用コピーとして残さず、製品表から期待値を作って自己一致を主張しない。
## 索引

- [hwdev/gf/internal.h](../internal.md)
- [hwdev/gf/runner.c](../runner.md)
- [hwdev/gf/save.c](../save.md)
- [hwdev/gf/pll.c](../pll.md)

### 個別設計書

- [compute_mem](compute_mem.md)
- [condition](condition.md)
- [copy](copy.md)
- [copy_nv_reg](copy_nv_reg.md)
- [cr](cr.md)
- [cr_index_address_latched](cr_index_address_latched.md)
- [done](done.md)
- [end_repeat](end_repeat.md)
- [index_io](index_io.md)
- [io_flag_condition](io_flag_condition.md)
- [io_restrict_pll2](io_restrict_pll2.md)
- [io_restrict_prog](io_restrict_prog.md)
- [macro](macro.md)
- [not](not.md)
- [nv_reg](nv_reg.md)
- [pll2](pll2.md)
- [repeat](repeat.md)
- [reset](reset.md)
- [resume](resume.md)
- [sub](sub.md)
- [sub_direct](sub_direct.md)
- [time](time.md)
- [zm_cr](zm_cr.md)
- [zm_cr_group](zm_cr_group.md)
- [zm_index_io](zm_index_io.md)
- [zm_reg](zm_reg.md)
- [zm_reg_sequence](zm_reg_sequence.md)
