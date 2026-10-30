# hwdev/gf/op/ — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/op/`

最終の部品は `hwdev/gf/op/<命令名の小文字>.c` の **run だけ 27 本、各 5〜30 行、計 300〜500 行目標**。共通計算は共有する。下表 E は移行元で、P6 で診断 A2/main も同じ runner に移し、静的展開を削除する（F4）。
head/skip、SUB の復帰、REPEAT、RESET の強制実行・184C 復元、CR のラッチ復元は各命令試験に残す。skip 中の CR44 による head 更新も維持する（`O/kdebug/gfinit_decode.c:79–96`）。

F8：新規 host は実 ROM fixture の差分試験一つと REPEAT/SUB/CONDITION/MACRO/RESET/PLL の個別 case が中心。全命令へ一律 4 case を新設しない。skip/head、呼出し/反復上限、RESET 強制実行/184C 復元、PLL 26/7/1/1/2 と失敗点を重点化する。
既存試験は機能別の入口へ移し、fake bus/clock を共有する。P6 削除前に旧経路の trace を fixture 化し、手本由来の独立期待値と併用する。削除後も旧 executor を試験用コピーとして残さず、製品表から期待値を作って自己一致を主張しない。
## 索引

- [hwdev/gf/](../README.md)
- [hwdev/gf/internal.h](../internal.md)
- [hwdev/gf/runner.c](../runner.md)
- [hwdev/gf/save.c](../save.md)
- [hwdev/gf/pll.c](../pll.md)
