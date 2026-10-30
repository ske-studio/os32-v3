# hwdev/gf/internal.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/internal.h`

head/skip、SUB の復帰、REPEAT、RESET の強制実行・184C 復元、CR のラッチ復元は各命令試験に残す。skip 中の CR44 による head 更新も維持する（`O/kdebug/gfinit_decode.c:79–96`）。

HAL に GF のレジスタ・ROM・recover token を出さない。
状態は BAR raw/base/length・写像・ROM 識別・PLL limits・初期化済み・基準値 valid・mode/palette/blank・stop 成否・再利用可否。同期・非再入で扱う。
局所 index/lock/PLL 途中の退避値は基準値と分ける。初期化失敗、bus/窓/stop 失敗、ID/BAR 変化は再利用不可。再 devinit はせず、正常 stop 後の ID/BAR/Command/legacy/PMC 照合成功時だけ C 相当の再 modeset を許す（同:78–92）。
下層 A の契約は `int gf_op_<name>_run(struct gf_init_context *, const struct gf_instruction *)`。解読は現行 `decode_instruction` と `.inc` の一つの実体を共有し、命令別 decode API は増やさない。
> [要確認] `gf_instruction` は現行実装では `gfinit_instruction`（`hwdev/gf/internal.h:115`）。
context は head/skip、有限の呼出し/反復 stack、ROM 借用参照、既存上限・時計・`gf_io`。bus は read/write（BAR/offset/width/値）、wait_us、point、局所復元。診断の記録・承認は adapter が担当する。
CLIENT は PEGC/GF のどちらか一つの 300 KiB。BAR1 の DISPLAY は MMIO で RAM の CLIENT と別。初期化中 ROM は最大既存 GF_ROM_MAX、runner の有限作業域を使い、limits/基準値を値コピーした後に ROM と作業域を解放する。本番再 modeset は ROM に依存しない。
診断の常駐 ROM/plan/lease は別の起動で既存寿命を維持する。
## 索引

- [hwdev/gf/](README.md)
- [hwdev/gf/runner.c](runner.md)
- [hwdev/gf/save.c](save.md)
- [hwdev/gf/pll.c](pll.md)
- [include/video_service.h](../../include/video_service.md)
