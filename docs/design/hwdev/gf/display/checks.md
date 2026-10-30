# hwdev/gf/display/checks.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/checks.c`

本番は required。診断 C の record/required・除外ファイルと二段の照合は維持する。機能 CHECK の descriptor は `O/kdebug/gfinit_plan_c.c:214–275` から共有し、診断の VRAM 模様/全域比較/REC は本番にリンクしない。
必須値：50C mask=EFFFFFFF/expected=10000204、選択後の 508 mask=0007FFFF、578 mask=8000FFFF、580 bit8、524=初回&~50000、C040=初回値。
さらに CONFIG&3=2、START=0、CR00..18（最終 CR11=AB・CR17=C3）/19/1A/25/28/44/42/1B/20/47、CR33=初回&~3B、SR01=01、MISC=E3、GENERAL=20100130、DACCLK&101=1、TEST bit20。CR1A mask=D7、ほかの列挙 CR は FF。
route01 とセルは上層の切替後に判定し、共有列の終了条件に route01 を混ぜない。510/TEST sense/scratch、50C bit28・CR1A bit3/5・680A24 全bit の照合は復活させず、AR/DAC の副作用読取りも増やさない（`O/docs/POLICY_DEBUG.md:1585–1590`）。

## 落とし穴

- **読戻しできないビット**: 書いた値と全ビットを比較すると、動作していても照合で止まることがある。
  `hwdev/gf/display/checks.c:5` は mask の範囲だけを比較し、50C bit28、CR1A bit3/5、680A24 を必須照合から外す。
  判定側も同じ mask を使う。`tools/tests/gfinit_plan_c_host.c` の CHECK 試験で照合条件を検査する。
