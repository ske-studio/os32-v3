# hwdev/gf/display/state.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/state.h`

基準値は main4 後に CR1C/33/3C/4B/52・60081C/600850/680524、初回 C040、PMC_ENABLE を一度だけ採る。全部成功した時だけ valid、CR52=(初回+4)&FF を維持する（`docs/design/kdebug/gf_stage_state.md`）。
機能 CHECK の descriptor は `O/kdebug/gfinit_plan_c.c:214–275` から共有し、診断の VRAM 模様/全域比較/REC は本番にリンクしない。
必須値：50C mask=EFFFFFFF/expected=10000204、選択後の 508 mask=0007FFFF、578 mask=8000FFFF、580 bit8、524=初回&~50000、C040=初回値。
さらに CONFIG&3=2、START=0、CR00..18（最終 CR11=AB・CR17=C3）/19/1A/25/28/44/42/1B/20/47、CR33=初回&~3B、SR01=01、MISC=E3、GENERAL=20100130、DACCLK&101=1、TEST bit20。CR1A mask=D7、ほかの列挙 CR は FF。
