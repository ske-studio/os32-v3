# GeForce 6200 (NV44A) 層 4 (表示) — 確定設計: 複数のモードと色深度・EDID/DDC・DPMS・HW カーソル・VBLANK と flip・ガンマ・TMDS/TV/2 画面

> 出所: ~/os32-tmp/run/gf-feat4/DESIGN.final.md (astra 起草 r1〜r2、Opus レビュー 2 回、P3 は PM が反映)。
> 状態: **現行 (2026-11-02)** — 段の状態は票 TASK_GF_FEATURES が持つ。層 1〜3 は GF_FEAT1〜3_DESIGN。

# NV44A 層4 詳細設計 r2 — 表示
## r1→r2 の変更
- ID 1: PLLSEL bit9を保持し、食い違いを0件へ訂正、変異をbit9削除へ反転。
- ID 2: PLLSELを688→153→162の順で生成、2headは両headの要求から合成。
- ID 3: counter増加後highなら即base、lowだけ立上り待ち、guard/能力bitを削除。
- ID 4: 当時は層3 accel_outを維持するContext拡張を計画。現行版4のaccel_out/foundation_outを保持して版5へ一度だけ拡張し、全backendを再ビルド。
- ID 5: hwdevの新規表示部品をfilter-out、既存診断経路を分離し容量を計上。
- ID 6: EDID/TV表・生成と大型stateは初めからkernel配置、B4/B11/C4を確定。
- ID 7: CHECK名・変異の正常対照・実機未実施台帳IDを受入表に追加。
- ID 8: 層4→F2→F1→局所復元→PC-98の停止順、F1停止後の待ち禁止を明記。
- ID 9: DDC低層の根拠を手元のMIT原典と行番号へ置換。
- ID 10: head別snapshot変更先にkdebug/gfinit_*とVPLL probeを追加。
対象: PC-9821 Ra266 / PCI 0:13.0 / 10de:0221 / LCD1770VM・D-SUB。設計のみ、2026-11-02。

## 1. 実物の確認 (file:line)
- O=/home/hight/os32-v3、R=/home/hight/os32-tmp/refs/geforce6200、N=R/nouveau/drivers/gpu/drm/nouveau (c03c1a1)、V=R/nouveau-v6.1/drivers/gpu/drm/nouveau。以下 C=N/dispnv04/crtc.c、H=同hw.c、I=同hw.h、D=同dac.c、F=同dfp.c、U=同cursor.c、REG=同nvreg.h。数値は特記以外16進、寸法/予算は10進。
- 票 O/docs/tasks/realhw/TASK_GF_FEATURES.md:14–31、比較 GF_FEATURE_COMPARE.md:18–31 を確認。比較表と現物の食い違いは0件。未実装を実装済みとは数えない。
- O/hwdev/gf/display/modes.c:23–44 は1モード・offset0だけ、timing.c:28–67/format.c:28–43 は固定寸法/8bpp、fifo.c:28–32 は走査FIFO (PFIFOではない)、output.c:28–35 はhead A/analog、cursor.c:28–33 は停止のみ、palette.c:6–25 はbyte LUT、blank.c:14–33 は画面blankのみ。
- O/drivers/video/geforce/backend_geforce.c:123–144 は固定start→転送→pc98_route_to_gf、:99–107 はstop→pc98_route_to_98。:196–210 はPACKED8だけ。O/include/gfx_hal.h:37–86 にmodesはあるがset_mode/cursor/vsync/flip/gamma hookはまだ無い。
- **現物と手本の食い違い0件**: O/hwdev/gfinit_mode.h:62–65 のbit28|bit2|bit9は正しい。bit9はSOURCE_PROG_VPLL (REG:314)、PLLSEL_VPLL1_MASKの一部 (C:91–93,162)。chipset<41限定はbit8/bit10 (C:160–161)。
- F1=O/docs/decisions/realhw/GF_FEAT1_DESIGN.md §5、F2=GF_FEAT2_DESIGN.md、F3=O/docs/decisions/realhw/GF_FEAT3_DESIGN.md (確定版) の境界を採用。VBLANKはu32観測回数、自然wrap、欠落補間なし。channel0/subc0 referenceを変更しない。
- 内部GNU11、公開SDK C89、kstring、番地/サイズ/パスは定数3層、P2V系集約、整数、シングルタスク・協調切替。[C1]〜[D4]/[HW3]厳守。MIT許諾/著作権を移植部に保存、DRM/GPLのEDID実装は写さない。
- 補助参照は指定dispnv04内のREG・arb.c・tvnv17.c・tvmodesnv17.c。DDCはMITの N/nvkm/subdev/i2c/busnv04.c と N/nvkm/subdev/bios/i2c.c を使用 (§4)。

## 2. 共通境界・所有・誤りコード
```c
/* 新 hwdev/gf/display/api.h。surfaceはF3 accel.hを共用、offはVRAM byte。 */
enum gf4_rc { GF4_OK=0, GF4_EINVAL=-401, GF4_ERANGE=-402,
 GF4_EFORMAT=-403, GF4_EPLL=-404, GF4_EDDC=-405, GF4_EEDID=-406,
 GF4_ENOSPC=-407, GF4_ESTATE=-408, GF4_ETIME=-409,
 GF4_EUNSUPPORTED=-410, GF4_EBUSY=-411, GF4_ENODEV=-412 };
enum gf4_dpms { GF4_ON, GF4_STANDBY, GF4_SUSPEND, GF4_OFF };
struct gf4_timing { u32 khz; u16 hd,hs,he,ht,vd,vs,ve,vt; u32 flags; };
struct gf4_mode { u32 id; struct gf4_timing t; enum gf3_fmt fmt; u32 pitch; };
struct gf4_route { u8 connector, head, type, or_mask, i2c_index, location; };
struct gf4_cursor { const u32 *argb_pm; u32 bytes,pitch,w,h; i32 hot_x,hot_y; };
struct gf4_ctx; /* production所有、head[2]、mode表、route、LUT、予約、epoch */
int gf4_modes(struct gf4_ctx *, u8 connector, const struct gf4_mode **, u32 *n);
int gf4_set_mode(struct gf4_ctx *, const struct gf4_route *, u32 id,
                 const struct gf3_surface *front, u32 dl);
int gf4_ddc_read(struct gf4_ctx *, u8 bus, u8 block, u8 out[128], u32 dl);
int gf4_dpms_set(struct gf4_ctx *, u8 head, enum gf4_dpms, u32 dl);
int gf4_cursor_image(struct gf4_ctx *,u8 head,const struct gf4_cursor *,u32 dl);
int gf4_cursor_move(struct gf4_ctx *,u8 head,i32 x,i32 y);
int gf4_cursor_show(struct gf4_ctx *,u8 head,bool show);
int gf4_vblank_wait(struct gf4_ctx *,u8 head,u32 after,u32 dl,u32 *seen);
int gf4_flip(struct gf4_ctx *,u8 head,const struct gf3_surface *,bool sync,u32 dl);
int gf4_gamma(struct gf4_ctx *,u8 head,const u16 rgb[3][256]);
```
- dlはfence・描画と同じ操作全体の絶対期限、既定2秒。FIFO pauseはF2独自の2秒で、dlを途中更新しない。fence_emit内でkick済みなので追加kickは行わない。DDCはprobe全体2秒/1block100ms、DPMSの10msも同じ期限内。負のF1/F2/bus rcは保存して伝播、HALで既存OS32エラーへ変換し詳細last_rcを試験opsから取得。
- 検査は境界だけ: ready/epoch、head/route、format、mode id、pitch/面末尾/予約重複。pending操作1個/head、再入EBUSY。IRQ内にalloc/待ち/serialを足さない。成功時だけselected_mode/front/DPMS状態をcommit。
- VRAM予約はF2 push開始B未満の共通台帳: 表示面1〜2枚/head→2KiB整列cursor 16KiB/head→F3試験面。末尾instmem・pushを侵さない。面末尾はoff<=limitかつbytes<=limit-offで検査。一般heapは不要、mode変更前に新予約を試算する。
- set_mode/DPMSはflip受付停止→F2 reference drain→cursor hide→表示状態変更。失敗は旧timing/PLL/base/LUT/routeを既存recover方式で復元、復元失敗ならreusable=false→既存GF停止/PC-98経路へ。旧面は表示停止が確定するまで解放しない。
- 停止順: 層4 (flip受付停止・進行中処理を完了/打切り→cursor hide→DPMS ON)→層3 fini→gf2_stop (drain→channel停止)（内部でF1 stop）→冪等foundation_stop cleanup→quiesce rows (局所復元)→route_to_98。F1 stop後はVBLANKを待たない。失敗時は既存の所有保持/復元規約に従う。
```c
/* HAL共有型。任意hookをGfxBackend末尾に追加、NULLはNOSYS。 */
struct GfxCursorImage { const u32 *argb_pm; u32 bytes,pitch,width,height;
                       i32 hot_x,hot_y; };
struct GfxScanout { u32 reservation_id,offset,bytes,pitch,width,height,format; };
/* offsetの所有者はreservation_idで照合、ユーザー物理番地を受け取らない。 */
int (*set_mode)(u32 id); /* default head A/既知analog、予約と記述子も更新 */
int (*dpms)(u8 head, u32 state);
int (*cursor_image)(u8 head,const struct GfxCursorImage *);
int (*cursor_move)(u8 head,i32 x,i32 y);
int (*cursor_show)(u8 head,int show);
int (*vsync)(u8 head,u32 after,u32 deadline,u32 *seen);
int (*page_flip)(u8 head,const struct GfxScanout *,int sync,u32 deadline);
int (*gamma)(u8 head,const u16 rgb[3][256]);
/* GfxCursorImage/GfxScanoutはHAL型。gf3_surfaceへの変換はGF adapter内だけ。 */
/* kernel試験: ops->set_mode(id); ops->cursor_image(...); ops->page_flip(...);
   module内部試験opsはroute指定/DDC生block/last_rcも公開。GUIからの呼出しは作らない。 */
```
```c
/* 純計算とMMIO実行を分離。gfinit_mode_savedはhead別snapshotへ拡張。 */
int gf4_mode_calc(const struct gf4_mode *,const struct gf4_route *,
                 const struct gfinit_mode_saved *,const struct gfinit_pll_limits *,
                 const struct gf3_surface *,struct gfinit_mode *out);
int gf4_mode_load(struct gf4_ctx *,u8 head,const struct gfinit_mode *,u32 dl);
struct gf4_data_ops { u32 version,size;
 int (*edid_modes)(const u8 block[128],u32 block_no,void *parser);
 int (*tv_state)(u32 norm,const struct gf4_timing *,void *state,u32 bytes);
}; /* helperはMMIOを触らず、状態schemaはA0でversion固定。 */
/* GeforceModuleContextの既存accel_out/foundation_outは保持し、末尾へ追加。 */
struct gf5_test_ops; /* 不完全型の前方宣言。F5 A0の定義完了を待たない。 */
const struct gf4_data_ops *display_data; /* kernel所有、version=1/size照合 */
struct gf4_state *display_state; /* A0で定義、mode/LUT/TV作業域、kernel所有 */
const struct gf4_test_ops **display_out; /* kernelのstatic slotの番地 */
const struct gf5_test_ops **feature5_out; /* F5診断用のkernel static slot */
struct gf4_test_ops { struct gf4_ctx *ctx;
 __typeof__(&gf4_set_mode) set_mode; __typeof__(&gf4_ddc_read) ddc_read;
 __typeof__(&gf4_flip) flip; int (*last_rc)(struct gf4_ctx *);
}; /* 通常操作はHAL hooks、route/DDC/詳細rcだけこの試験dispatchも使用。 */
```
- A0/C3: geforce_module.hのGEFORCE_MODULE_OPS_VERSIONとtools/module_schema.py:187のops_versionを既存4→5へ一度だけ更新し、生成manifest/loader/host期待値も更新、旧版拒否。export_max=1/ops_slots=1は維持しinitializeのContext経由で渡す。初期化成功後だけdisplay_out公開、stop/abort/recover開始時NULL、回復成功後再公開。kernelは呼出しごとslotを読み、unload後は保持しない。借用state/helperはmodule停止完了まで有効。
- mode表は初期化または明示再検出で作成、利用中は不変。GfxMode既存配列に対応する詳細timing/id表をGFが所有。HAL queryのbpp/format/pitchを選択面から返す。prepareはDDCを含む表示変更をせず、実際の検出は試験入口/enter時の初期化へ。
- 旧8bpp RAM presentは既定モードで存続。別modeのHAL試験は予約VRAM面を直接描きflip、legacy presentは非対応formatを黙って転送せず診断を記録。GUIのRAM backing/描画形式変更は残タスクで、GUIから新set_modeを呼ばない。

## 3. (a) 複数モード・8/16/32bpp
| 手本の列・前提 | OS32の列 / 誤りコード |
|---|---|
| C:237–413 nv_crtc_mode_set_vga | timingを引数化。hd/hbs=(hd>>3)-1、hs/he=(hs/he>>3)+1、ht=(ht>>3)-5、hbe=(ht>>3)-1、vd/vbs=vd-1、vs/ve=vs/ve-1、vt=vt-2、vbe=vt-1。既存timing.cのCR00–07/09/10–12/15–18/25/2d/41のbit配置を保持。CR1a LARGE=hd<1280 ? 04 : 00。寸法順序・横8pixel単位・各field表現範囲はERANGE。 |
| C:308–340,408–413 | MISC=23＋negative H:40/V:80、SR00–04の初期列を維持。入力timingはCRTC用に正規化済み (interlaceの垂直field化/doublescanの2倍化をgeneratorで1回だけ行う)。interlaceはCR39=(ht_encoded>>1)&~1とCR2d bit4、doublescanはCR09 bit7/CSR config bit4を反映。EDID timingを丸めて別modeにしない。 |
| C:838–903 nv04_crtc_do_mode_set_base、REG:230,281,341–347 | INDEX8 depth8/cpp1→CR28低2bit=1、RGB565 depth16/cpp2→2、XRGB8888 depth24/cpp4→3 (**32をdepthにしない**)。680600=20100130、565のみbit12追加。q=pitch/8、CR19=(q>>8&7)<<5→CR13=q&ff→CR42=(q>>11&1)<<6→PCRTC_START=off。pitch=align(width*cpp,64)、q<=fff、off%4=0。EFORMAT/ERANGE。 |
| C:688→153→162 calc_state_ext、O/kdebug/gfinit_pll/parse.inc:1–43,solve.inc:8–60,sequence.inc:5–19 | ROM limits→整数solve(khz)→error検査→既存video gate/VIDEO_STAGE/VIDEO_COEFF/VPLL/gate復元の5-write順。clk<=vco1.max/2ならvco2無効、その他2段。PLLSELはsavedからVPLL1/VPLL2/TV maskを全て除去(:688)→mask内に限定(:153)→Curie bit2と自head mask(:157–162)。head0はbit9を含むVPLL1 mask (bit28含む)。2head合成は§9。EPLL、旧PLL復元。 |
| C:461–606 mode_set_regs、H:468–535,665–772,827–835 load_state | 先に全stateを計算。blank/protect→RAMDAC(PLL→CURSYNC→PLLSEL/SEL_CLK→GENERAL→FP等)→ext(ENGINE_CTRL→CURSOR_CONFIG→830/834→GPIO/850→RAMDAC900 bit16→CONFIG→拡張CR)→LUT→VGA(MISC→SR→CR→GR→AR)→protect解除。ENGINE_CTRLはCR33より前。旧gf_load_stateの順序責務を維持、mode/headを渡す。 |
| C:557–570,878–903、N/dispnv04/arb.c:239–264 nouveau_calc_arb | 600830=vd-3、600834=vd-1、CONFIG=2、RAMDAC900 bit16=1。NV44はburst=4/LWM=c0を全modeで使用、CR1b=4→CR20=c0→CR47=0。旧世代帯域式やPFIFO設定を混入させない。 |
| D:364–409 prepare/mode_set、:412–444 commit/dacclk | encoder OFF→DCBのORに応じDACCLK=head<<8\|1→TEST_CONTROL=00100000→encoder ON。初期analogは既知OR/head A、FP offを維持。他head使用時は共有DACCLKの利用者bitと選択を保持。ENODEV/EBUSY。 |
- mode候補はEDID優先timing→その他EDID→既定640×480、各8/16/32を同じ水準で列挙。既定timingは(25175kHz;640,656,752,800;480,490,492,525;−H/−V)。EDID無効時はこの3色深度のみを自動提示。
- 実機の上位候補は800×600/1024×768/1280×1024等、EDIDが提示/許容したものを使う。固定1280上限を置かない。VRAM・pitch/CR表現幅・ROM PLL・encoder最大clock・EDID範囲をすべて満たすものが実用上限。PLL誤差は目標比0.5%以内 (u64整数) とする。
- 差分: DRMのBO pin/TTMを予約台帳に置換する理由は単一装置・協調切替で面の所有者が確定しているため。自動選択は既存640×480×8を維持し、上位modeは明示選択する。
- PC-98経路はbackend_geforce.c:134–144/99–107の順を保持。GF内mode変更にpc98_route_to_gf/98を重複発行しない。新mode描画完了後に必要な場合だけ既存routeを呼び、leave/失敗で既存route_to_98へ戻す。

## 4. (b) EDID/DDC
| 手本の列・前提 | OS32の列 / 誤りコード |
|---|---|
| V/nouveau_connector.c:412–444 ddc_detect、:579–612 detect、:938–955 get_modes | DCB connector→encoder.i2c_index→該当busだけをprobe(7bit50)→EDID→analog/digital判別→mode表。全CRTC I2C portの総当たりをしない。読取失敗はEDDC、header/checksum不正はEEDIDとして診断し、既定modeを作って検出処理自体は成功。 |
| C:496–499、H:674–680、I:119–124、REG:268–273 | NV40ではprimary CRTC I2C経路を利用。ENGINE_CTRLのI2C bit4を保持、CRTC indexedアクセスはhead0の6013d4/6013d5。既定候補CR36/37、3e/3fはDCBのsense/drive指定で選び、head B表示でもI2CをBに移さない。 |
| N/nvkm/subdev/i2c/busnv04.c:41–70 | driveは読んだ値のSCL bit5/SDA bit4だけを更新しbit0=1 (`val\|0x01`)。release=1、low=0、他bitを保存。終了時STOP→線release→保存したowner/driveを復元。 |
- 既定drive/senseはN/nvkm/subdev/bios/i2c.c:147–155の3f/3e・37/36。DCB指定を優先。sense SCL bit2/SDA bit3はN/nvkm/subdev/i2c/busnv04.c:62,70とB3 fixtureで照合。A0で外部取得を待つ項目は無い。
- bitbang部品は5µs半周期、SCL release後にsense=1を期限まで待つ (stretch)。START→50/W→offset→repeated START→50/R→128byte、byteごとACK・最後NACK→STOP。block>=2は30/Wへsegment=block/2を書いてからoffset=(block&1)*128。NACK/timeoutでもSTOPを試みる、無限retryなし。
- base header=00 ff ff ff ff ff ff 00、128byte checksum=0、version/extension count/descriptor境界を検査。1block bufferで逐次読むためEDID全体の巨大配列不要、読取数はcountとdeadlineで制限。拡張不良でも正常baseは残す。
- edid.cは独立整数parser: established/standard timing、base DTD、range descriptor、CTA video data/DTDを解析し重複排除。未知extensionを無視し理由記録。DMT/CTA識別子は小表、標準timingで表にないものはGTF/CVT整数生成と範囲検査。Linux drm_edid.cは依存・移植しない。
- 差分: drm_get_edid/drm_add_edid_modesをOS32独立parserに置換する理由はGPL非移植。DMT/GTF/CVT値は規格出典をB4のfixtureに記録して照合する。未解釈modeを「接続なし」にしない。

## 5. (c) DPMS
| 手本の列 | OS32の列 / 誤りコード |
|---|---|
| C:175–233 nv_crtc_dpms、I:238–240 SeqReset | owner選択→CR1a&~c0→SR0=1→SR1 bit5を更新→10ms待ち→CR17 bit7を更新→SR0=3→CR1a sync inhibit更新。順序は固定、他bit保存。EINVAL/ETIME/bus rc。 |
| 同:198–221 | ON:SR1/CR17/CR1a対象bit=00/80/00、STANDBY=20/80/80 (H off)、SUSPEND=20/80/40 (V off)、OFF=20/00/c0。 |
| D:460以降 dac_dpms/426–444 update_dacclk、F:113–148,563–576 | CRTCに加えencoder側ON/OFFも処理。共有DAC利用者が残る間はDACCLK enableを落とさない。TMDSのFP_CONTROLは最後のON値保存→OFF mask→ON復元。 |
- 既存blankは画面だけを隠す独立状態、DPMS ON復帰時もblank要求が残ればSR1 bit5を保持。cursor可視要求/LUT/baseは保持し、OFF中のvsync/同期flipはESTATEで即時終了。ON完了後から新しいcounter snapshotを取る。
- 手本のstandby/suspendコメントは「Not Supported」だが列自体は存在する。API/列は全4状態を実装し、LCDの実反応は試験記録にする。勝手にOFFへ丸めない。

## 6. (d) ハードウェアカーソル
| 手本の列・前提 | OS32の列 / 誤りコード |
|---|---|
| C:510–516,938–975 nv11_cursor_upload、:980–1015 cursor_set、:1322 | HWは64×64×32bit、16KiBをVRAM予約。入力はpremultiplied ARGB8888 (LE byte B,G,R,A)、32×32も64×64へ透明padding。0<alpha<255だけalpha+1、0/255は保持。hide→CPU転送→posted write排出→offset→show。EFORMAT/ERANGE/ENOSPC。 |
| U:39–62 set_offset、REG:257–264、H:692 | CONFIG=04011100 (alpha blend bit28=0、PM)。CR30=80\|((off>>17)&7f)→CR31=((off>>11)&3f)<<2＋dblscan bit1→CR2f=off>>24。offは2KiB整列。I:341–349に従い680300をread→同値writeしてNV40 workaround。 |
| U:21–28 set_pos、I:372–385 show、REG:301–303 | 位置=(x-hot_x,y-hot_y)、680300=((u16)y<<16)\|(u16)x。CR31 bit0だけshow/hideでRMW→位置再write。符号16bit範囲外ERANGE、画面端はHW clipping。 |
- CPU画像寿命は呼出し終了まで、cursor VRAMはdisable/停止まで。画像更新は一時hideして上書き、失敗時hideのままrc、途中像を表示しない。完全透明・半透明・負座標・四隅・hotspotを試験する。
- 差分: GEM map/pinをF3 V0の独立VRAM窓＋共通面台帳へ置換。画像サイズ可変はHW形式変更ではなく透明paddingなので32/64両方を同じ入口で扱える。

## 7. (e) VBLANK待ち・ページフリップ
| 手本の列・前提 | OS32の列 / 誤りコード |
|---|---|
| F1 §5、C:1277 vblank_handler | gf4_vblank_waitはF1 snapshotのvblank[head]を読み(unsigned)(now-after)!=0で成功。IRQ有効・DPMS ON・head有効が前提。待ち中はgf_ioのµs時計・短waitを使う (native.c:62–79はbusy pollで、協調idle提供済みとは扱わない)。IRQ/PCRTC ACKを自分では書かない。ETIME/ESTATE、epoch変化はESTATE。 |
| C:1136–1217 page_flip、:1092–1133 emit、:1074–1087 complete、I:353–357 base | 新旧面のrender完了→表示同期→600800+head*2000=新off→完了/旧面解放。OS32はF2 reference drainでrender完了、baseはCPUから書く。flipは同mode/format/pitch、寸法とVRAM予約を検査、再入EBUSY。 |
- 差分: nouveauの009f:012c=0→0134=head→0100=0→0130=0→NV_SW_PAGE_FLIP→software eventを移植しない。理由はF2の固定reference完了とF1のACK専用handlerに、未設計SW method割込みを紛れ込ませないため。同期APIを前景で完了させる。
- F1のcounter増加後、6013da+head*2000のretrace bit3がhighなら即PCRTC_STARTへ新baseを書込み/readback。lowなら期限内で次のlow→highを待って書く。counter増加後に必ず新edgeを待つことはしない。status読出しはAR flipflopをresetするため同時AR操作禁止。
- retrace statusの番地はREG:299、bit3はB7 fixtureで照合。前景でIRQ有効のpoll、high検出からbase書込みまでyieldしない。手本はC:1205–1208の0130によるvblank待ち→:1082のbase書込み。guard測定や追加能力bitは設けない。
- baseはHSYNC latch (C:568/H:702–710) なので書込み後さらに次のVBLANK観測まで新旧面をpinし、そこで成功・front更新。timeout後にbaseを書いた可能性があれば両面保持、次の待ちで収束または表示停止して解放。ETIMEを未投入成功と扱わない。
- sync=falseはrender完了後即base→readback、tearing許容を明示。旧面解放は次回VBLANKまたは停止後。sync=trueの実機tear確認を受入に置く。待ちがtick代行で遅れても期限内でhighを確認し、全フレーム数は推定しない。

## 8. (f) ガンマ
| 手本の列・前提 | OS32の列 / 誤りコード |
|---|---|
| C:778–797 gamma_load、:816–834 gamma_set、H:795–810 palette | 16/32bppにu16×256×RGB rampを保存→各値>>8→DAC MASK=ff→INDEX=0→DATAへRGB順768byte→palette gate解除。番地6813c6/8/9＋head stride。EINVAL/EFORMAT/bus rc。 |
- INDEX8は既存palette APIに残しgammaと別shadow。16/32初期ramp[i]=i*257、mode/depth変更時に該当LUTをreload。565にも同じ256entryを渡す (手本にない32/64entryの手動複製をしない)。inactiveはshadow更新だけ、次startでload。
- LUT部分失敗はdirtyを保持、次回全量reload。途中LUTを成功扱いしない。浮動小数のpowをdriverへ入れず、試験用非線形rampは整数表で渡す。

## 9. (g) TMDS・TV・2画面
| 手本の列・前提 | OS32の列 / 誤りコード |
|---|---|
| N/dispnv04/disp.c:247–294 create | DCBのconnector/type/head mask/OR/location/I2Cからroute一覧作成。ANALOG→dac、TMDS/LVDS→dfp、TV→nv17_tv (NV44)。head[2]のtiming/front/LUT/cursor/DPMSを分離。未配線ENODEV、同じOR競合EBUSY。 |
| F:204–244 prepare_sel_clk、:245–278 prepare、:68–90 bind_head、I:109–116 | encoder OFF→SEL_CLK対象bit16/18をheadに合わせ更新→TMDS DATA→CONTROL(index04)へ80/88、dual-linkは次bankにxor08。CR28 bit7 slaved、CR33/LCDはDCB出力に従う。共有SEL_CLKは対象bitだけ更新。PLLSELは§3の順で生成したOS32の有効headごとのVPLL/TV要求maskをORし、Curie bit2を加える。savedの他head/TV maskを温存せず、片head変更時ももう一方の現在stateから再合成。 |
| F:280–445 mode_set、H:503–523 load_ramdac | FP水平/垂直7項を同順で計算: display/total/sync_endは元値−1、hvalid=hskew..hd−1、vvalid=0..vd−1、vcrtc=vt−6。FP TG polarity/scale/ditherをFの条件で生成→Hのvert→horiz→dither→TG→DEBUG0/1/2→margin順でload。 |
| F:446–480 commit、:563–576 dpms | run_tmds_table相当を既存ROM interpreterへ渡す→FP_CONTROL再読→TEST_CONTROL=00100000→外付けencoderがあればI2C mode_set→ON。内部/外付けを同一レジスタ列にしない。未知外付け種類EUNSUPPORTED、配線不明とは区別。 |
| N/dispnv04/tvnv17.c:310–366 valid/fixup、:403–577 prepare/mode_set、:579–606 commit | TV norm/connector指定→OFF→DACCLK/CRTC route→norm別state生成→rescaler/properties→tv state load→TEST_CONTROL=00100000→ON。NTSC/PAL/CTVはnormを引数に持ち、VGA timingを流用しない。 |
| N/dispnv04/tvmodesnv17.c:406–434 state_load、tvnv17.c:368–400 dpms | TV encoder index00..3f→H/H2/V filter→PTV offsets200,204,208,20c,304,500,504,508,600,604,608,60c,610,614→encoder3e=1→0。DPMSはPTV200 bit0→DCB TVDAC GPIO→DACCLK利用者更新、CTVならFP_CONTROLも更新。norm表/scale値は同MITソースから必要分を移す。 |
- 全head MMIOはI:70–96,119–124に沿いPCRTC/PRAMDAC/PRMCIOの各strideを使う。SEQ ownerはH:75–106のNVSetOwnerで保存/選択/復元。共有I2Cはhead0。両headのenable要求はF1 wanted_headsに渡し、Bのcounter/ACK方式を作り直さない。
- 出力能力は「コードあり/端子あり/接続あり/試験済み」を別に記録。Ra266を目視・写真でDVI/TV端子の有無確認→DCB照合→対応ケーブル/表示器で単独→A+B別模様/異mode/独立DPMS/flip。使えなければ端子なし・未配線・未対応encoder等の理由を記録、未試験を不存在と断定しない。
- 差分: 現在のanalog経路にはTMDS/TV scriptを発行しない理由はDCBで選択されたencoderだけが自分の出力を所有するため。GUIに2画面の概念を追加する作業は含めない。

## 10. 部品の大きさ (実装前の予算)
| 部品 | text/rodata増分KiB | RAM/VRAM |
|---|---:|---|
| mode/timing/PLL/route/HAL | 4〜6 | mode表はRAM、最大32timing×3format、満杯はpreferred優先・診断 |
| DDC (mod) / EDID・DMT・GTF・CVT・CTA (kernel) | mod 1〜2 / kernel 4〜6 | 128byte buffer、parserとmode表はkernel所有 |
| DPMS/cursor/flip/gamma | 3〜5 | cursorは16KiB/head、ramp1536byte/head、palette768byte/head |
| TMDS/TV実行/2head | 6〜8 | norm表・係数生成はkernel 3〜4KiB、最大2head |
- mod text/rodata14〜21KiB＋BSS1〜3KiB＝mem増分15〜24KiB。mode/LUT/TV作業域10〜14KiBはkernel所有、借用pointerのみmodへ。全層の容量判定は[GF_FEAT3の合算予算](GF_FEAT3_DESIGN.md#合算予算)に従う。VRAMは別枠: 1280×1024×4×2=10MiB/head、実VRAM・Bで判定。
- B4のEDID parser/規格表とB11のTV係数生成は初めからkernel専用、C4でhelperをbind。modにはMMIO実行・小状態・opsのみ。統合時に実測し、PM決定の合算予算と新上限で判定する。圧縮を発注条件にせず、機能を削除しない。
- helper/state/試験slotは§2のContext経由。IRQ/time/MMIO/DMAは既存F1/F2/gf_ioを使用。停止完了後にkernel側slot/借用stateを解除。
- kernel helper text/rodata7〜10KiB、state10〜14KiB＋試験本体を別計上。hwdev共有計算/診断変更は計12KiB以内とし、層2後の実測とF5共有decoder増分を含む容量は[共通合算表](GF_FEAT3_DESIGN.md#合算予算)で判定する。

## 11. ファイルごとの変更一覧 (O/相対、1機能1ファイル)
| 段 / ファイル | 機能・依存・行目安 |
|---|---|
| A0 hwdev/gf/display/api.h 新、hwdev/gfinit_mode.h・kdebug/gfinit_* 変 | API/型/定数/出典表、head別snapshotに全呼出し/fixtureを追随、hwdev.mk:126のVPLL probeも再compile / F1〜3 / 180＋追随分 |
| A1 tools/tests/gf_display_host.h 新 | 偽MMIO indexed/AR/LUT/時刻/IRQ counter/VRAMとI2C slave骨格 / A0 / 220 |
| A2 tools/tests/test_gf_display.py 新 | 部品link・fixture・変異登録・image/import計測骨格 / A1 / 100 |
| A3 kdebug/gf_display_test.c 新 | serial dispatch/復元/カメラ用case番号の骨格、試験ops bind / A0 / 100 |
| B0 hwdev/gf/display/modes.c 変 | 候補/ID/能力/容量検査 / A0 / 130 |
| B1 hwdev/gf/display/timing.c,format.c,fifo.c 変 | 各既存責務の引数化・pitch/depth/FIFO / B0 / 計180 |
| B2 hwdev/gf/display/pll_program.c 変 | 目標clock/head・共有PLLSEL再生成・bit9保持 / B0 / 70 |
| B3 hwdev/gf/display/ddc.c 新 | bitbang/EDID block / A0 / 180 |
| B4 drivers/video/geforce/edid.c 新 (kernel専用) | EDID解析・mode生成、C4のdata ops経由 / B3 / 350 |
| B5 hwdev/gf/display/dpms.c 新、blank.c 変 | sync状態機械 / B1 / 100＋15 |
| B6 hwdev/gf/display/cursor.c 変 | 画像/offset/位置/表示 / F3 V0の独立VRAM窓 / 150 |
| B7 hwdev/gf/display/flip.c 新 | vblank wait/base/面寿命 / F1 snapshot,F2 fence / 170 |
| B8 hwdev/gf/display/gamma.c 新、palette.c 変 | gamma shadow/LUT loader共有 / B1 / 90＋20 |
| B9 hwdev/gf/display/output.c 変 | DCB route/analog/共有DAC所有 / B0 / 100 |
| B10 hwdev/gf/display/tmds.c 新 | FP timing/TMDS/script/DPMS / B9 / 240 |
| B11 hwdev/gf/display/tv.c 新、drivers/video/geforce/tv_data.c 新 (kernel専用) | 前者はTV MMIO/DPMS、後者はnorm表/state/rescale純計算、C4経由 / B9 / 計300＋係数表 |
| B12 hwdev/gf/display/heads.c 新 | 2head状態/共有clock競合/owner / B9 / 100 |
| C0 hwdev/gf/display/modeset.c 変 | 準備→load→復元transaction / B群 / 100 |
| C1 drivers/video/geforce/backend_geforce.c 変、include/gfx_hal.h 変 | HAL adapters/記述子/既存route維持、hook追加 / C0 / 140＋35 |
| C2 hwdev/gf/production.h,service.c,display/quiesce.c 変 | ctx/予約/epoch/stop・callback解除 / C0 / 計65 |
| C3 drivers/video/geforce/geforce_module.c/.h、tools/module_schema.py、build/geforce-module.mk・hwdev.mk、docs/08_build.md 変 | ABI宣言を前倒し: accel_out/foundation_outを保持して版4→5、display欄と前方宣言のfeature5_outを追加 (F5 A0待ちなし)。PM決定（1）: schema:188のimage_max/mem_maxを各163840B (160KiB)、load_span_maxを212992B (208KiB)へ拡大し容量表へGEFORCE行を同期。根拠はmodule_runtime.c:73–78の一般割当域からの連続頁取得で、固定帯域・常駐予約は無い。header/schema/loader/manifest期待値を同一所有者が揃え、生成物再作成はPM登録段。リンク分離・像検査・全backend再ビルドは統合後 / 宣言を先行、登録はF3 C3後 / 計90＋容量表 |
| C4 drivers/video/geforce/gf_display_data.c 新、kernelのソース列 変 | helper ops/state所有・bind、B4/B11 kernel部分だけをkernelにlink、moduleに重複linkしない / B4,B11 / 60 |
- C3のhwdev.mkは新規ddc/dpms/flip/gamma/tmds/tv/heads.cを全てfilter-out (全てgeforce.mod専用)、edid.c/tv_data.c/gf_display_data.cはkernel専用。既存cursor.cは診断のhide列を維持し独立VRAM窓を使う画像APIをGF_PRODUCTION_MODULE内に限定。既存modes/modeset/output/palette/blank/service/quiesceのF1〜3・helper参照も同条件に置き、hwdev側は従来診断列。timing/format/fifo/PLLは共有純計算/MMIOを維持。hwdev/R1/VPLL/GFA/非module backendの実linkで未解決参照を検出。
- 各Bでtools/tests/gf_display_b<N>_host.cを同時作成、実部品をlink、A1で依存を偽装し各40〜100行。A3へ各caseを追加、共有登録はC段で直列統合。kdebug/gfinit_pll/は既存純整数solverを再利用、同じ計算の別実装を増やさない。

## 12. 受入 → 試験・変異・段分け
| 受入 | ホスト偽MMIO/変異 (変異時にRED必須) | 実機serial / カメラ | CHECK名・正常対照・DEFERRED_TESTS行ID |
|---|---|---|---|
| 全mode/depth | 640/800/1024/1280のtiming/pitch/overflow、32bpp depth24。変異:depth32、CR42欠落、CR1a固定、PLLbit9削除、別head PLLSEL破壊 | 8/565/32の色帯・1pixel格子、EDID内の上限まで。pitch/clock/PLL誤差/CRCと撮影 | gf-display-host/mode・正しいbit9/両head mask・各depthの期待列・GF-L4-Ra266-mode |
| DDC | ACK/NACK/stretch/repeated START/segment/checksum/DTD/範囲/壊れた拡張。変異:SCL/SDA交換、最後ACK、checksum省略、offset wrap | LCD1770VM EDID block/型番/優先modeをserial、抜線で既定3depth。成功/不読の理由を残す | gf-display-host/ddc・正常EDIDと最終NACK/正常segment・GF-L4-Ra266-ddc |
| DPMS | 4状態のRMW/10ms/blank重畳/共有DAC。変異:H/V逆、CR17先行、別head enable消去 | on→standby→on→suspend→on→off→on、モニタ反応/OSDと帰還画面を撮影 | gf-display-host/dpms・4状態のH/VとCR17順の期待列・GF-L4-Ra266-dpms |
| cursor | 32padding/64、PM alpha0/1/254/255、負座標/offset2KiB、workaround。変異:enable誤bit、PM補正省略、pos再write削除 | 非対称色矢印、半透明、四辺clip/移動/show/hide、mode/depth変更後 | gf-display-host/cursor・既知ARGB/位置/enable列・GF-L4-Ra266-cursor |
| VBLANK/flip | counter wrap/欠落/tick代行/IRQ停止、render未完了、stale epoch、書込後timeoutの面保持。変異:highでも次edge待ち、lowで即書込、drain削除、旧面早期解放 | 二面に番号/斜線、連続flip、serialにbefore/after/base/rc/edge→write時間。カメラでtear確認、high直後/low遅延の両経路を記録 | gf-display-host/flip・high即書込・low立上り待ち、drainとpin保持・GF-L4-Ra266-flip |
| gamma | RGB順768byte/>>8/identity/565/32/INDEX8分離。変異:>>6、G/B交換、depth変更reloadなし | identity/整数暗化/復元、8bit palette帰還、serial LUT checksum＋撮影 | gf-display-host/gamma・identityとRGB rampの期待列・GF-L4-Ra266-gamma |
| TMDS/TV/2head | DCB各route/OR競合、FP/PTV書込順、別head保存。変異:head stride無し、TMDS DATA/CONTROL逆、PTV3e toggle無し | 端子写真＋DCB、接続可能な出力を単独/同時表示、不可なら理由。片head DPMSが他方を消さない | gf-display-host/outputs・両head独立stateとFP/PTV期待列・GF-L4-Ra266-outputs |
| 従来経路/容量 | 既定enter/leave/失敗復元、route呼出し回数、GF image/mem160KiB・load_span208KiB・hwdev192KiB、版5/旧版拒否、detach callback無し、停止順/F1停止後待ち無し。変異:成功前selected_mode更新、F1停止後待ち、旧版受入 | PC-98→GF→PC-98を各試験後確認、停止失敗もserialで追跡 | gf-display-host/lifecycle・既定enter/leave・正しい停止順・版5・GF-L4-Ra266-lifecycle |
- A2のCHECKはname="gf-display-host",tier=1,needs=[],budget=90.0、coversは表示部品・kernel helper・backend/ABI・build定義・gf_display試験。各変異を1箇所ずつ無変更GREEN→実行時RED→復元GREENで判定し、コンパイル失敗は検出に数えない。
- C段でdocs/tasks/DEFERRED_TESTS.mdへ表の8個のIDを各1行追加: `ID | 未実施 | Ra266 0:13.0/0221 | 当該受入の実機項目 | 担当/日付/serial/カメラ証跡は実施時記入`。DPMSのLCD反応、outputsの端子/TMDS/TV/2head、flipのtearも明示して残す。台帳欠落は失敗、登録済み未実施は未実施として保持しhost合格と区別。
- A骨格→B部品＋各変異→C結合/容量→T実機。実機は実装後でよい。カメラ http://hight-pc-vj24lfwz1srg:8080/ のcase番号付き像とserialを対応付け、実機未試験は合格としない。この設計作業では実機操作/撮影/リポジトリ編集をしていない。
- 観測だけのcameraではPLL周波数・H/V停止・全flip無tearを証明できない。OSD/レジスタreadback/IRQ件数を併記し、判断できない項目は測定未了。規格fixture追加はA/Bの実装作業でありユーザー判断待ちにしない。

## 13. 未決 (ユーザー判断が必要なものだけ)
- **0件**。推奨は上記どおり全機能のHAL/カーネル試験入口まで作る。配線・容量実測・tear確認は技術検証事項で、スコープ削減の承認条件にしない。

## 層 1〜3 への送り
- 層1: counter/ACK/IRQ設計は変更不要。wanted_headsの更新とsnapshotを表示adapterへ供給、DPMS/stop後の待ちはESTATEにする。IRQ側のflip callback/SW method処理は追加しない。
- 層2: 表示/カーソル/試験面の共通VRAM予約範囲とBを共有。reference待ちとposted write排出を利用し、CPU mapはF3 V0の独立VRAM窓を使う。channel/subc変更なし。
- 層3: INDEX8/RGB565/XRGB8888の面format・pitch/off/bytesをそのまま受け渡す。surface自体にepoch欄は無く、面台帳のreservation_id/世代で古い面を排除する。mode変更だけでF2 fence epochを増やさない。表示が保持する旧面へ描画しない。容量は[共通合算表](GF_FEAT3_DESIGN.md#合算予算)で同時検査し、必要importは生成元へ追加する。

反論: 0件。

## 確定 (2026-11-02、Opus 第 2 回 Approve、PM が反映)
- ID11 (P3): NV4Aのnv04 I2Cの対象bit RMW・bit0=1とsense bit2/3の根拠を§4の本文に反映済み。

## 追記 (2026-11-04、段に出す前の点検)

| 境目／持ち主 | 宣言段で凍結する追記 |
|---|---|
| ABI／C3を宣言段へ前倒し | 現行`geforce_module.h:8,44–45`・`module_schema.py:187`は版4、末尾はaccel_out,foundation_out。**PM 決定（5）**: 両欄を保持してdisplay_data/state/outと`feature5_out`を末尾追加し、版4→5を一度だけ行う。F5診断A2/C4はこのContext slotから呼び、F5では版を上げない。geforce_module.h:11の`struct gf3_test_ops;`と同じく、`struct gf5_test_ops;`を不完全型として前方宣言し、`const struct gf5_test_ops **feature5_out`を置く。F4 C3はF5 A0を待たず宣言を凍結し、F5 A0がその型に整合させる。header/schema/loader/manifest期待値を同一所有者が揃える。F3は版を上げない。 |
| 状態と寿命／A0・C2・C4 | productionがgf4_ctxと予約を所有、kernelのgf_display_data.cがhelper/stateを所有しinitializeへ貸す。mode/flip成功時だけfrontとscan hookを更新。変更中はhook受付を閉じる。surface自体にはepoch欄が無いので台帳のreservation_id/世代で古い面を排除し、F2 fence epochをmode変更だけで増やさない。 |
| 初期化・停止／C0・C2 | F3 readyと表示state/helper借用成立後にdisplay_out公開。停止はF4のflip整理/cursor hide/DPMS処理→F3 fini→F2 stop→既存局所復元。F2 stop自身がF1を停止する（command.c:60–72）；その後の既存foundation_stopは冪等cleanupとして残す。失敗時は面・stateを保持する。 |
| VBLANK／B7・B12 | F1のwanted_headsとch->headsを同じhead集合へ更新し、F2のIRQ要求更新でBを落とさない。counter観測/ACKはF1のみ。前景待ちはgf_ioのµs時計・短いwaitを使用；既存waitはbusy pollであり協調idle関数が提供済みとは扱わない（native.c:62–79）。 |
| CPU窓・復旧／B6・C2 | cursor画像はF3追加VRAM窓を使いpush mapを借りない。F5向けdisplay_save/restoreは両headのmode/front/palette/gamma/cursor/DPMS/route/IRQ要求と未完了flipの保持面を渡す。受付閉のまま復元し、F5最終成功時だけslot/hookを開く。 |
| コンパイル条件／C3 | hwdev/gf配下は現行`build/geforce-module.mk:22`のGF_PRODUCTION_MODULEで分岐する。backend/gf_hookはOS32_GEFORCE_MODULE。両者を混同しない。`build/hwdev.mk:122`のwildcardから新表示部品を除外し、共有計算と従来診断列だけ残す。 |
| 配置・試験環境 | B0〜B3/B5〜B10/B12およびB11のtv.cはゲストmod＋libcなしhost実部品試験。B1/B2等の共有計算はhwdev/VPLLにも残る。B4 edid.c・B11 tv_data.c・C4 helper/stateはゲストkernel専用＋libcなしhost試験。A3はゲスト診断、A2はhost Python登録。 |
| 予算 | mod text/rodata14〜21＋BSS1〜3＝15〜24KiB。kernel helper7〜10、state10〜14KiB、cursor VRAM16KiB/headを別計上。hwdev共有変更は計12KiB以内。EDID/TV helperをhwdev.modへ移したことにはしない。 |

共通の[合算予算](GF_FEAT3_DESIGN.md#合算予算)・[共有ファイルの衝突表と所有段](GF_FEAT3_DESIGN.md#共有ファイルの衝突表所有段)・[依存と並列発注](GF_FEAT3_DESIGN.md#依存と並列発注)はGF_FEAT3を正とする。

F4 A0はF3の面/窓契約後、C3のABI宣言はF5 A0を待たず先行する。宣言統合後のB0〜B12は異なるファイルならF3/F5 B群と並列にコーディングできる。B11はtv.c/tv_data.cの2段、B1等も1ファイルごとに分割する。C0/C1/C2とB6の実map・B7の実drain結合はF3を待つ。共有登録と結線は直列、試験コードは各Bで完成させ、実行は全実装後のPMの生成→まとめビルド→検査にまとめる。

## 追記 (2026-11-04、層 4・5 の先回りの点検 — 層 3 への依存・宣言の段の中身)

> 出所: ~/os32-tmp/run/gf45-pre/ADDENDUM.r3.md (astra 起草 r1〜r3、Opus レビュー 3 回で確定、補いは PM)。層 5 の分もここに置き、GF_FEAT5_DESIGN からリンクする。ユーザー指示「geforceの層４と層５は層３の完成を前提にしていますか？並列で設計できるのであればこれも並行で設計してください。」

- N1: previous_calc用の基点ヘッダー分離をF4 A0が担当し、A2が登録を引き継ぐ。
- N2: ctv[38]をgf4_tv_stateへ戻し、gfinit_modeから外してdata_ops経由で渡す。
- N3: data.hをtiming・route・edid・tv・data_opsに限定し、modeとgf_display_data.cはapi.hに依存させる。
- N4: 製品コードの64bit除算・剰余とlibgcc依存を禁止し、host除算helperは試験コード専用とする。

## GF_FEAT4_DESIGN 末尾追記案 — 層3と並行する宣言・部品設計（2026-11-04）

状態: r3提案。F3 A0・A1・A3の取り込み後（統合ブランチwt/gf-l3には取り込み済み）を着手前提とし、F3 A0凍結済みの `accel.h` / `vram_view.h` とF3末尾G1〜G3・N1を前提とする。層3全体の完成・実機受入はA0/Bの着手条件ではない。以下の宣言をレビュー・統合してから部品を出す。リポジトリ編集・生成・ビルド・試験実行はしていない。
照合元: O=`/home/hight/os32-v3`、A0=`/home/hight/wt/os32-v3/gf3-a0`、A1=`/home/hight/wt/os32-v3/gf3-a1`、N=`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau`。本節はF4へ、次節はF5へ写す。既存節のHW列・誤り値は維持し、下記は共有境界の補足／明示した食い違いの訂正。

| F4段（元設計の段ID単位） | 層3への依存 | 今できること／完了を待つもの |
|---|---|---|
| A0、前倒しC3のABI宣言 | 宣言のみ: surface・窓・台帳、F3 production宣言 | F3 A0・A3取り込み後に着手可。F4保存型もここで凍結、feature5_outは不完全型でよい。production.hの変更順はF3 A3→F4 A0。 |
| A1/A2/A3 | F3宣言＋本節のhost契約 | F4 A0およびF3 A1（gf_accel_host.h）取り込み後に骨格・試験コードを作れる。A1実装が未統合ならBは仕様に対して書き、勝手なstubを増やさない。 |
| B0〜B5、B8〜B12 | 宣言のみ（F4内依存も宣言で代行） | F3部品群と同時にコード作成可。B1をtiming/format/fifo、B11をtv/tv_data等の1ファイル1段へ分割。 |
| B6、B7 | コードは宣言のみ／実結合はF3 V0・B1・C1/C2 | cursorの実map・所有移管、flipの生存面＋F2 drain＋hook再bindの結合完了を待つ【2段】。F2 fence実装自体は既存。 |
| C0/C1/C2 | F3 V0・B0/B1・C0/C1/C2の実装 | transaction、HAL/scan、production寿命の実結線を待つ【3段】。下書きは可。層3全描画機能の実機合格は不要。 |
| C3の登録・結線／C4 | C3はF3 C3/C4/C5等の共有変更／C4は宣言のみ | C3は先行登録の取り込み後【1段】。C4 helper/stateコードはF4 B4/B11の宣言で先行可。 |
| T（実機受入） | 層3の稼働する実装と必要な描画・fence・停止の実機確認 | F3全機能受入を一括の前提にしない。実機上で前提経路を確認してから各F4受入を行い、未確認をhost合格で代用しない。 |

### A0で埋める共有宣言（G2）

以下のフィールド一覧は記載順の完全定義、`T a,b` は同じ型の複数欄。整数の符号付き32bitはi32（s32禁止）、boolはstdbool.h。api.hはdata.h・accel.h・gfinit_mode.hをinclude、F5へのincludeは作らない。A0でdisplay/data.hにgf4_timing・gf4_route・gf4_edid_state・gf4_tv_state・gf4_data_opsとそれらの純データ定数だけを置き、types.h/stdbool.hだけに依存させる。gf4_rc/dpms/mode/cursorはapi.hに残す（gf4_modeのenum gf3_fmtにはaccel.hが必要）。edid.c・tv_data.cはdata.hだけでコンパイルでき、accel.h/command.hを取り込まない。gf_display_data.cはgf4_stateを定義するためapi.hをincludeする。下表の「同」はapi.hを指し、data.h所属は明記する。別ファイルに分ける宣言もA0の同一契約として先に凍結する。

| ファイル／型・定数 | A0に書く定義・責任 |
|---|---|
| `hwdev/gf/display/api.h` 既決公開型・関数 | §2のgf4_rc/dpms/mode/cursor（timingはdata.hからinclude）、gf4_modes/set_mode/ddc_read/dpms_set/cursor_image/move/show/vblank_wait/flip/gamma/mode_calc/mode_load/test_opsをその型のまま収録。timing.flagsはNEG_H=1、NEG_V=2、INTERLACE=4、DBLSCAN=8。HEADS=2、TIMINGS=32、MODES=96、CURSOR_BYTES=16384、CURSOR_ALIGN=4096（HW最小は2048）。 |
| `hwdev/gf/display/data.h` `gf4_route` | 既決u8 connector,head,type,or_mask,i2c_index,locationに、u8 head_mask,ext_type; bool dual_link; u32 max_khz,tv_normを追加。typeはDCB値のANALOG=0/TV=1/TMDS=2/LVDS=3。tv_normはN/dispnv04/tvnv17.h:56–70の12値順、非TVは0。DCB route配列はproductionが解析して貸し、未配線と未対応encoderを区別する。 |
| `hwdev/gf/display/data.h` `gf4_edid_state` | u32 version,size,ntiming,preferred,extensions,next_block,min_v_hz,max_v_hz,min_h_khz,max_h_khz,max_khz; bool base_valid,digital,range_valid,truncated; i32 last_rc; struct gf4_timing timing[32]。version=1、preferredは添字／不在U32_MAX。B4が所有する逐次parserの値状態であり、MMIO・pointer・予約を持たない。 |
| `hwdev/gf/display/data.h` `gf4_tv_state` | u32 version,size,norm; u8 enc[64]; u32 hfilter[4][7],hfilter2[4][7],vfilter[4][7],ptv[14],ctv[38]。ctvはTV規格表の値をkernelのtv_data.cで埋め、data_ops.tv_stateの出力としてmoduleへ渡す。gfinit_modeには重複保持しない。ptv順=200,204,208,20c,304,500,504,508,600,604,608,60c,610,614（hex）。version=1。N/dispnv04/tvnv17.h:30–54、tvmodesnv17.c:406–434。 |
| `hwdev/gf/display/data.h` `gf4_data_ops` | u32 version,size; i32 (*edid_modes)(const u8 block[128],u32 block_no,struct gf4_edid_state *); i32 (*tv_state)(u32 norm,const struct gf4_timing *,struct gf4_tv_state *)。旧void *parser/stateをこの型へ置換しversion=1で固定。block0でparserを初期化、以後順次追加、失敗blockは既存正常timingを保持。TVは成功時だけ全出力確定。 |
| 同 `gf4_head_image` | bool enabled,blank,cursor_visible,lut_dirty; struct gf4_mode mode; struct gf4_route route; struct gf3_surface front,back; struct gf_vram_reservation cursor; i32 x,y,hot_x,hot_y; u32 cursor_w,cursor_h,fp_on; enum gf4_dpms dpms; u8 palette[768]; u16 gamma[3][256]。CPU画像pointerは保存せずcursor予約内のGPU形式像をF5が退避。 |
| 同 `gf4_flip_hold` / `gf4_saved` | hold={bool pending,base_written; struct gf3_surface old_front,new_front; u32 after;}。saved={u32 version,size,wanted_fifo,wanted_graph,wanted_heads; struct gf4_head_image head[2]; struct gf4_flip_hold hold[2];}。version=1。node/窓/opsを含まない値コピー。表示保存後もhold両面は所有者ctxに残す。 |
| 同 `gf4_state`（kernel所有） | u32 version,size,nmodes; struct gf4_mode modes[96]; u8 mode_connector[96]; struct gf4_edid_state edid; struct gf4_head_image head[2]; struct gf4_tv_state tv[2]; struct gfinit_mode work[2]; struct gf4_saved rollback。version=1。mode列はconnector別に連続、1表合計32timing×3format、満杯時preferred優先。kernel RAM見積りは約17.2KiBへ訂正（ctv重複除去前の保守値、除去分304B、最終sizeofで確定）。rollbackのpalette/gammaは保持する。 |
| 同 `gf4_ctx`（production所有・移動禁止） | struct gf2_chan *ch; struct gf3_ctx *accel; struct gf4_state *state; const struct gf4_data_ops *data; const struct gf4_exec_ops *exec; const struct gf4_route *routes; u32 nroutes,epoch,deadline; i32 last_rc; bool ready,accepting,reusable; struct gf_vram_reservation_node scan[2][2],cursor[2],test; struct gf_vram_view cursor_view[2]; struct gf4_flip_hold hold[2]。既存のboot表示mapはproductionから借用、窓として所有しない。 |
| 同 ROM／外付けencoder結線 | `struct gf4_exec_ops { i32 (*tmds_script)(void *,const struct gf4_route *,u32 khz,u32 dl); i32 (*external_mode)(void *,const struct gf4_route *,const struct gf4_mode *,u32 dl); void *owner; };` C2がROM既存runner／既知encoderの実装を結線、B10はこの宣言でコーディング。analogでは呼ばず、未知encoderはEUNSUPPORTED。ROM借用寿命はF5の有無によらずdisplay停止までC2が保持する。 |
| 同 F4→F5保存口（C2実装） | `i32 gf4_display_save(struct gf4_ctx *,struct gf4_saved *,u32 dl); i32 gf4_display_restore(struct gf4_ctx *,const struct gf4_saved *,u32 dl);` saveは受付閉・drain後に論理値保存→blank/IRQ抑止→全cursor_viewをunmap（予約保持）。unmap失敗は所有保持で返しF2停止へ進まない。restoreは受付閉のままcursor_viewを再mapしてから保存されたshowを再適用し、map失敗では公開しない。最終公開はF5統括だけ。F4単体停止も同じ寿命規約。 |
| 同 部品間口（定義は各所有B/C） | `i32 gf4_surface_check(struct gf4_ctx *,const struct gf3_surface *,const struct gf4_mode *);` B0所有、BYTESのF3検査後に表示3format/pitch64B/開始4B/CR field幅を検査。`i32 gf4_route_check(struct gf4_ctx *,const struct gf4_route *);` B9所有。`i32 gf4_lut_load(struct gf4_ctx *,u8);` B8所有、palette/gammaが共用。`i32 gf4_heads_apply(struct gf4_ctx *,u32 heads);` B12所有、F1 wanted_headsとch->headsを一致させる。 |
| 同 計算／encoder口 | `i32 gf4_timing_calc(const struct gf4_mode *,struct gfinit_mode *); i32 gf4_format_calc(const struct gf4_mode *,const struct gf3_surface *,struct gfinit_mode *); void gf4_fifo_calc(struct gfinit_mode *); i32 gf4_pll_calc(const struct gf4_mode *,const struct gf4_route *,const struct gfinit_mode_saved *,const struct gfinit_pll_limits *,struct gfinit_mode *);` 各B1/B2所有。B9/B10/B11は `i32 gf4_{analog,tmds,tv}_load(struct gf4_ctx *,const struct gf4_route *,const struct gfinit_mode *,u32 dl)` と同形の `_dpms`（mode引数をenum gf4_dpmsへ置換）。 |
| `hwdev/gfinit_mode.h`、`display/calc.h`、`kdebug/gfinit_*` | `gfinit_mode_head_saved={u8 cr1c,cr33,cr3c,cr4b,cr52; u32 gpio_ext,crtc850;}`、`gfinit_mode_saved={struct gfinit_mode_head_saved head[2]; u32 pllsel,sel_clk;}`へ置換。gfinit_modeにu8 head; u32 fp_horiz[7],fp_vert[7],dither,dither_regs[6],fp_debug[3],fp_margin,tv_vtotal,tv_vskew,tv_vsync_delay,tv_htotal,tv_hskew,tv_hsync_delay,tv_hsync_delay2を追加（N/dispnv04/disp.h:30–75）。rollbackの全体像はstate.work/rollbackと既存recover機構が持つ。旧診断gfinit_mode_calcはhead0既定modeのラッパー、旧gf_mode_*呼出しも既定ラッパーを保持し全fixtureを追随。 |
| gfinit_mode追従の担当 | F4 A0: hwdev/gfinit_mode.h、hwdev/gf/display/{calc.h,state.h,baseline.c,extended.c,pll_program.c,calc.c}、hwdev/gf/internal.h、kdebug/gfinit_*（特にgfinit_plan_c.c:225）のhead別参照・offsetofを更新。tools/tests/{gf_display_host.c,gfinit_mode_host.c}の位置指定初期化を欄名指定へ変更。同じF4 A0がtools/tests/test_gf_display.pyのprevious_calcを修正し、基点の源とともに `git show 13bc5619:hwdev/gfinit_mode.h` で得たヘッダーを一時include木へ配置して基点TUだけに優先適用する（現行TUは現行ヘッダー）。A0のhead[2]変更と同時に取り込み、A2待ちにしない。A2はこの分離を維持して登録を引き継ぐ。PM登録段: tools/hwdev_layout.jsonの旧684/20と生成hwdev_contract.hのsizeof/offset契約を再生成・同期。旧定数を手で残さない。 |
| `include/gfx_hal.h` / `geforce_module.h`・schema | §2のGfxCursorImage/GfxScanout・8hookを末尾に追加。GfxScanoutはID不再利用を使いctx内の同IDからgenerationを得る。Contextは既存accel_out/foundation_outを保持してdisplay_data/state/out,feature5_out追加、版4→5はF4 C3宣言担当だけ。F5で再変更しない。 |
| `production.h` / `gf_display_data.c` | productionにはgf4_ctxのみ埋込み、state/helperはkernelが貸す。大きいmode/LUT/TV/rollbackをmoduleへ複製しない。C4がversion/size検査してbind、C2がready成功後公開。全backend追随と生成はPM統合段。 |

### 所有・範囲の欠けを埋める（G3）

| 問題／根拠 | 訂正と所有者・失敗契約 |
|---|---|
| cursorの2KiB整列とV0保持窓の頁境界が不一致 | F4 cursor予約は4KiB整列・16KiB（HW要求も満たす）。F4 C2がF3表示/試験利用を止め、窓解除・旧予約release後にctxのnodeへ移管。生存nodeをmemcpyしない。map失敗・unmap失敗・停止失敗では既存所有を保持。 |
| HAL面IDにgenerationがない | A0/vram_view.hとF3末尾の既決どおり、F4 C1がctxの同IDのキーを取りowner付きmatch。off/bytesだけで最新予約を検索しない。B0はBYTES＋表示固有条件、B6はcursor入力のRAM範囲・座標減算をi64で検査してからi32/i16へ、B7は同modeと両面の生存を投入前に検査。 |
| flipの「解放」と台帳の所有 | B7はholdを記録し新旧予約を借りるだけ、releaseしない。C2だけが次VBLANK確認／表示停止後に解放可能と判断。base書込みの失敗も投入不明ならbase_written=true相当で両面保持。sync=falseでも旧面は保持。保存中の未完了flipはholdへ値保存し、restoreで旧frontを確定してからholdを整理（未完了要求を再送しない）。 |
| F1/F2 IRQ・epoch | mode変更ではF2 epochを増やさない。F4 epochはch->epochの照合値。B12はgf_irq_enableに現在FIFO/GR要求を残しhead集合を渡す。snapshot/ACKはF1のみ。F1停止後のVBLANK待ちなし。 |

### A1 host土台の完全な受渡し規約（G1）

| 共通API／F4追加 | 宣言と意味（tools/tests/gf4_display_fake.hが唯一の所有者（既存gf_display_host.cを土台名に使わない）） |
|---|---|
| F3から再利用できる範囲 | A1/gf_accel_host.h:7–11,63–103を確認。`GF3_HOST_UNIT=12`でincludeし、gf3_host_init/trace_clear/fail_at/ref_*と記録型だけ借りる。12は依存stubを一切定義しない。既存B0等の選択をF4に流用しない。gf_command_host.hとの同時includeなし。 |
| 新型 | `enum gf4_host_op { D_READ,D_WRITE,D_CLOCK,D_SLEEP,D_SNAPSHOT,D_IRQ,D_FENCE,D_FENCE_WAIT,D_FAULT,D_MATCH,D_MAP,D_UNMAP,D_FORMAT,D_SURFACE,D_ROUTE,D_LUT,D_HEADS,D_EDID,D_TV,D_LOAD,D_SAVE,D_RESTORE,D_SCRIPT,D_EXTERNAL,D_DPMS,D_OP_COUNT }; struct gf4_host_event { enum gf4_host_op op; u32 nth,a[12]; const void *p[4]; i32 rc; }; struct gf4_host_read { u32 bar,off,width,value; };` |
| fixture | `struct gf4_host { struct gf3_host base; u64 local_now; u64 *now; struct gf4_ctx ctx; struct gf4_state *state; struct gf4_host_event *events; u32 cevents,nevents,calls[D_OP_COUNT],fail_at[D_OP_COUNT]; i32 fail_rc[D_OP_COUNT]; const struct gf4_host_read *reads; u32 nreads,next_read; u8 cr[2][256],sr[2][256],gr[2][256],ar[2][32],lut[2][768],index[2][4]; u32 lut_pos[2]; bool ar_data[2]; struct gf_irq_stats irq; void (*observe)(struct gf4_host *,const struct gf4_host_event *); };` 大型状態・trace/read scriptは呼出側static領域を借りる。 |
| helper一覧 | `void gf4_host_init(struct gf4_host *,struct gf4_state *); void gf4_host_trace_clear(struct gf4_host *); void gf4_host_fail_at(struct gf4_host *,enum gf4_host_op,u32 nth,i32 rc); void gf4_host_reads(struct gf4_host *,const struct gf4_host_read *,u32 n);` read/write/clock/waitのcallback型はgf_ioと完全一致、名前はgf4_host_read/write/clock/wait。gf3_host_init後にio callbackを差替え、opaqueをgf4_hostへ向け、now=&local_now（初期値0）とする。F4 A1がclock/waitの共通分岐を実装: clockは*nowのu32下位、waitは同じ*nowを既存wait規約どおり進め、base.nowも下位に同期。F5型を参照しない。 |
| MMIO・I2C | width=1/2/4を記録、indexed SR/CR/GR・AR status読出しでのflipflop reset・DAC index/dataをモデル化。read scriptは該当readの番地/幅/順を照合して消費し、未該当はregister model。I2CはscriptでACK/NACK/stretch/segmentとsense bit2/3を供給し、drive bit4/5・bit0=1の全列を独立fixtureと照合（slave全体のemulator不要）。 |
| 失敗・状態 | nth=1始まり、0は全回、rc=0解除。成功失敗とも引数/返却rcを記録しobserveは帰還前。trace_clearは記録数・callsだけ、失敗設定/VRAM/時刻を保持。バッファ不足・script不一致は常時有効assert→exit(1)、製品ENOSPCへ偽装しない。map/unmapはV0の変更なし／失敗時保持を模す。 |
| 引数記録と対応名 | event.aは宣言順の非pointer引数（structは宣言順へ展開）、event.pはpointer順、余り0。構造体内容はobserveで即時照合。FENCE/WAIT=gf2_fence_emit/wait、SNAPSHOT/IRQ=gf_irq_snapshot/enable、MATCH/MAP/UNMAP=gf_vram_match/view_map/view_unmap、FORMAT/SURFACE=gf3_format_get/surface_check、ROUTE/LUT/HEADS=gf4_route_check/lut_load/heads_apply、EDID/TV=gf4_data_ops、SCRIPT/EXTERNAL=gf4_exec_ops。typed callbackのctx/ownerも記録する。 |
| stub選択 | 必須GF4_HOST_UNIT=0..12（B番号）、13=C0、14=依存stubなし。製品TU自身の関数は定義しない。B0:FORMAT/SURFACE/MATCH/EDID、B1/B3/B4:外部依存なし＋io、B2:既存PLL parser/solver実物、B5:FENCE/WAIT/encoder DPMS、B6:MATCH/MAP/UNMAP、B7:FENCE/WAIT/FAULT＋gf4_surface_check＋SNAPSHOT、B8:ioのみ、B9〜B11:ROUTE/TV/SCRIPT/EXTERNAL（製品自身除外）、B12:IRQ/SNAPSHOT、C0:各Bの公開口。B1/B11のファイル分割は補助リンク一覧で選択し重複stubを禁止。全stubはapi.h/F1〜3の実宣言を使う。 |
| oracle・登録 | B単独はgolden MMIO／呼出し順／所有遷移で合否判定。GPU画像の生成を仮定しない。packet/画像参照は必要な結合に限定。read script・EDID規格fixture・MIT列の出典と再生成法をrepoに置きCIはrefsを読まない。A2は既存tools/tests/test_gf_display.pyへの追記（CHECK名gf-display-host、BASE 13bc5619照合、previous_calcの基点ヘッダー分離はA0担当）で、層2caseとgf_display_{host,modes_host,blank_host,palette_host}.cを保持。B0/B5/B8がmodes.c/blank.c/palette.c変更に伴う既存試験追従を担当。A2が依存link/CHECK/変異登録を一元所有、-m32/-nostdlib・自前_start、heap/VLA/libcなし。 |

| 64bit型・除算（F4/F5共通） | PMが統合段でinclude/types.hへtypedef unsigned long long u64; typedef long long i64;を1回追加。os32_kapi_shared.hは変更なし、hostdrvfs_proto.hの同一typedefはC11で許容。製品TUでは64bitの除算・剰余を書かず、PLL誤差0.5%等は範囲を確認した乗算比較で表現し、vmap符号付き式・cursor・tile計算もこの制約を守る。各Bが製品側の計算を所有し、hostの-m32/-nostdlibとlibgccをリンクしない.modの双方で__udivdi3/__divdi3等を要求しない。2026-11-04ユーザー決定に従いkernelもlibgccを前提にしない。F4 A1がF3 A1の1ff4e2d2（gf_accel_host.h）の64bit除算helperを試験コード専用として共通土台へ再利用し、F5 A1も同じ定義を使う（1リンク1定義）。host helperで製品側の依存を隠さず、A2/A3のリンク確認では製品TUの当該未解決symbolが無いことも確認する。 |

F4のF3待ちは機能結合5段＋共有登録1段＝6段（上表）。A0/Bコード作成には0段。実機受入は別枠。PEGC/EGC、新たなIRQ callback・lock/refcount・nouveauに無い監視や能力bitは追加しない。

## GF_FEAT5_DESIGN 末尾追記案 — 共有宣言・停止・snapshotの凍結（2026-11-04）

前提はF3 A0（accel.h/vram_view.h＋G1〜G3）とF4 A0のgf4_saved。参照元O/A0/A1/Nは前節と同じパス。F5 A0はF4の保存型レビュー・統合後に凍結でき、F3の実装・実機完成は待たない。本案は内部宣言だけを補い、元設計のレジスタ列・性能切替順・スコープを拡張しない。

| F5段 | 層3への依存 | 今できること／完了を待つもの |
|---|---|---|
| A0 | F3宣言＋F4保存型＋既存F2停止口 | F4 A0後に着手可。F4 feature5_outの前方宣言と合わせる。前景fanの呼出し箇所は下記PM決定済み。 |
| A1/A2/A3、B0〜B6 | 宣言のみ | F5 A0後に全部品と試験コードを並行作成可。B3はB4のGPIO宣言、B6は下記の型付きlinksを使う。 |
| B2/B5/B6の実結合 | F3受付閉・V0・objects・F2 drain/stopの実装 | RAM/tile停止と全extent退避・再生成の結合完了を待つ【3段】。B6表示復元はさらにF4 C2/C4。 |
| C0/C1、C2-ROM/C2-service | 宣言のみ／共有ファイルは先行段取り込み | C0はB1と同じ所有者、C1はF4 A0後。ROM取得／poll部品本体は先行可、呼出し元の結線はC2。 |
| C2／C3／C4 | C2はF3実装・F4 C2/C4、C3はF3→F4共有登録 | C2結線【1段】、C3登録【1段】を待つ。C4台本・dispatchコードは宣言で先行可、公開の実検証はC2後。 |
| 実機受入 | F3の必要転送・停止・再生成の実機確認 | ROM perf間/boot復帰、CRC、save/restore等の前提経路を同じ実機受入で先に確認。層3の全描画受入を一括の開始条件にしない。 |

### A0完全共有境界（G2、feature5.h）

`feature5.h`は`display/api.h`をinclude。既決のrc/clock/plan/thermal/fan_mode/emergency_ops/vid_opsとclock_read/calc/prog,ram_prog,perf_select,therm_read,fan_mode_set/get/cstate/tick,volt_get/set,tile_set/clear,state_save/restore/freeは§2〜7の型を採用（署名中intはi32、unsigned slotはu32に表記統一）。以下を同じA0に追加・置換し、B間で局所宣言しない。

| 型・関数／所有者 | 完全な欄・引数と契約 |
|---|---|
| `gf5_perf_entry` / `gf5_rom_view` | entry={struct gf5_clock clock; u8 voltage,fanspeed;}。view={const u8 *bytes; u32 size; const struct gf_init_program *memory_program; u16 memory_pc;}。programは既存内部型を再利用。ROM/span/programの動的所有はC2-ROM、viewは借用、scratch解放後も存続。 |
| B0表列挙／B4共用GPIO | `i32 gf5_perf_count(struct gf5 *,u32 *); i32 gf5_perf_get(struct gf5 *,u32 index,struct gf5_perf_entry *); i32 gf5_gpio_find(struct gf5 *,u8 tag,u8 *line,u8 log[2]); i32 gf5_gpio_get(struct gf5 *,u8 line,const u8 log[2],bool *); i32 gf5_gpio_set(struct gf5 *,u8 line,const u8 log[2],bool); i32 gf5_volt_id(struct gf5 *,u8 id,u8 min_id,i32 condition);` gpioはB4だけが実装、戻りはgf5_rc、非存在ENOTSUP、破損EBIOS、I/O EIO。 |
| `gf5_tile` | 既存u32 base,bytes,pitchにu32 reservation_id,generationを追加。配置の所有者が切上げ後全範囲をreserve済みで渡す。B5はID/世代付きmatch→base64KiB整列/pitch>0/64*pitch切上げをu64計算→範囲と他tile・表示/cursor非重複を検査。B5は予約を解放しない。空き昇順選択は `i32 gf5_tile_find(struct gf5 *,u32 *slot);`、clear成功は枠だけ無効化。 |
| `gf5_fan_trip` / `gf5_fan_saved` | trip={i32 temp,hyst; u32 duty;}。saved={enum gf5_fan_mode mode; u32 kind,method,min_duty,max_duty,freq,divisor,target,cstate,duty,bump_ms,slow_ms; i32 last_trip,linear_min,linear_max; u32 ntrip; struct gf5_fan_trip *trip; i32 threshold[4],hyst[4]; bool active[4],sensor_poll,toggle_on; u8 gpio_line,gpio_log[2]; i32 slope_mult,slope_div,offset_num,offset_den,offset_constant; u32 sensor_setup;}。kind=TRIP/LINEAR/OTHER、method=NONE/PWM/TOGGLEを各0始まりで宣言、温度単位は℃。tripは動的配列でsnapshotが深くコピー、GPU pointerではない。 |
| `gf5_extent` / `gf5_image` | extent={struct gf_vram_reservation key; u8 *host_copy;}。image={u32 version,bytes,nextent,saved_epoch; struct gf5_clock clock; u32 requested_uv; u8 vid; struct gf5_fan_saved fan; struct gf5_tile tile[12]; bool tile_live[12]; struct gf4_saved display; struct gf5_extent *extent; bool complete;}。version=1、bytesはmetadata/trip/extent/画像を含む確保量。keyのownerは同一性tokenのみ、node/next/map VA/staging/instmemは保存禁止。 |
| `gf5_links`（既決5口を具体化） | `i32 (*drain_pause)(void *); i32 (*unpause)(void *); i32 (*display_save)(void *,struct gf5_image *); i32 (*display_restore)(void *,const struct gf5_image *); i32 (*memory_script)(void *,u16 pc); void *owner;` 保持。display adapterはimage.displayとF4保存口を対応。dlはgf5.deadlineを読む。 |
| 同 linksへの必要な資源・結線口 | `i32 (*admission)(void *,bool open); i32 (*drain)(void *,u32 dl); i32 (*stop)(void *,u32 dl); i32 (*restart)(void *,u32 dl); i32 (*copy_extent)(void *,const struct gf_vram_reservation *,u8 *ram,bool to_vram); i32 (*alloc)(void *,u32 bytes,void **out); void (*free)(void *,void *); i32 (*clock64)(void *,u64 *);` 追加。alloc成功時だけ出力更新、freeは非NULLの成功確保に1回。stop/restart/copyはC2、malloc汎用dispatcherにしない。 |
| `enum gf5_phase` / `struct gf5` | phase={GF5_RUN,GF5_SAVING,GF5_SAVED,GF5_RESTORING,GF5_FAULT}。gf5={struct gf2_chan *ch; struct gf3_ctx *accel; struct gf4_ctx *display; struct gf5_links links; struct gf5_rom_view rom; struct gf5_vid_ops vid; struct gf5_emergency_ops emergency; enum gf5_phase phase; u32 deadline; i32 last_rc,source_rc; bool accepting,busy; struct gf5_fan_saved fan; u64 next_auto,next_step,next_sensor,next_toggle; struct gf5_tile tile[12]; bool tile_live[12]; struct gf5_image *image;}。productionが所有、imageは借用。常駐1KiB目安に収まるか実測し、trip/ROMは動的RAMを別計上。 |
| ROM／前景service宣言 | `i32 gf5_rom_acquire(struct gf5 *,const u8 *,u32,const struct gf_init_program *); void gf5_rom_release(struct gf5 *); i32 gf5_service_poll(struct gf5 *);` acquireは成功時に必要ROM/span/programを別所有、releaseは停止とsnapshot破棄後だけ。pollはclock64→fan_tick、busy/非RUNなら無処理、IRQから呼ばない。PM決定: drivers/video/geforce/backend_geforce.c:168のgeforce_presentとHAL hookの前景入口、およびkdebug/gf5_test.cの診断待ちから呼ぶ。kernel idle hookは新設せず、geforce_tick.c/IRQから呼ばない。描画・診断待ちが無い間はAUTOの見直しが止まる。 |
| `gf5_test_ops` | struct gf5 *ctx; 上記の全公開操作とperf_count/get,tile_find,service_pollを `__typeof__(&gf5_関数名)` の同名（gf5_を除く）欄で列挙。`i32 (*last_rc)(struct gf5 *);` 追加。gf5_linksの内側やraw MMIOは公開しない。save時slotは閉じるため、診断側がimageを保持しrestore/freeだけを許す専用管理dispatchを再読する（次行）。 |
| 診断slotの保存中の寿命 | feature5_outはmodule生存中の管理口。SAVING/RESTORING中はNULL、SAVEDで再公開しphase検査でrestore/free/last_rc以外をESTATEにする。FAIL後も資源を持つ間は管理口を保持し、再restore/破棄の受付可否をphaseで判断。unload前は必ずNULL、slotを跨ぐops pointer保持禁止。F3/F4描画slotは最終RUN成功までNULL。 |

### 実物と食い違う所有・停止を修正する（G3）

| 問題／照合元 | 凍結する責任と失敗時の動き |
|---|---|
| 本文§7の「同一offへ再予約」はF3確定追記と矛盾 | **予約保持へ訂正**。受付閉のままgf_vram_nextをcursor=0で列挙（1=item、0=end、負=失敗）、全生存予約を保存。node/owner/ID/generationはF4/F3/呼出元の同じ常駐位置で維持し、台帳を初期化し直さない。restoreはmatchで同一配置を確認し再reserveしない。変更された配置はESTATE、勝手に別offへ移さない。 |
| extentの上限・重複・失敗snapshot | 生存予約は非重複なので1予約1extent。front/hold等が同予約を指しても別コピーにしない。合計はu64で溢れを検査して確保可能サイズへ、固定件数/8MiB上限なし。確保前はout保持、全確保後かつ破壊操作前に*outへ渡しgf5.imageも借用、以降失敗してもfreeしない。completeは全画像コピー成功後だけtrue。stop失敗でも完全像は保持、未完成像restoreはESTATE。 |
| 既存75頁表示mapとV0 | copy_extentのC2 adapterがboot表示範囲をbar1_mapで借用、境界をまたぐ予約はそこで分割、残りはV0窓。off/lenの4B条件を予約内で満たす。F4新規面/試験/tileは4B倍数以上、未整列の保存要求は停止前ERANGE。CPU書込み排出→窓unmap、失敗時は予約・窓・adapter保持、停止へ進まない。push mapを借りない。 |
| F2 fifo.c:204–238 | drain_pauseは受付閉→gf2_fence_emit/wait（gf5.deadline）→既存gf2_fifo_pause（独自2秒）→GR idle。pauseは途中失敗でもHWが閉じかけるので開いた扱いにしない。unpauseはold_rc=ch->last_rcを退避して0にし、void gf2_fifo_resume後のlast_rc/state/faultを検査。新しい誤りがあればそれを保持してi32で返し、無ければold_rcを戻してunpause自体は成功を返す（以前のF1/F2エラーをF3から見えなくしない）。失敗時FAULT・受付閉、resume失敗をF2自身がFAULT化すると仮定しない。 |
| deadline／RAMだけ停止 | 外側の公開操作開始でu32絶対期限を一度設定しlinksが参照、入れ子のperf/restoreで更新しない。既存RAMの各HW待ち上限は維持、F2 pauseの期限口は追加しない。core/volt/fan単独はpauseなし、RAM/tile/save固有区間のみ閉じる。64bit fan時計はhi/lo由来、u32 wrapをゼロ拡張しない。 |
| F2 dmaobj.c:42–46、command.c:60–72 | DMA生成失敗はhandle=0だがinst.sizeは残る。F3 objects再生成の失敗試験もこの形にし、sizeだけで二重解放しない。stop adapterは窓解除→F3 fini→F2 stop（F1停止含む）→冪等cleanup。F1停止後は待たず、F2 rcとstate/OFFを別々に確認、失敗時所有を保存。 |
| restartの公開順／fence世代 | restartはBM先行→必要devinit→F1/F2→F3 objectsまで受付閉。性能/thermal/tile→VRAM→F4 restore→IRQ要求→全slot/scan hookを再bindして公開。F2 startがepochを進めるので二重加算しない。旧ticketは新epochの完了として通さず、未完了要求は再送しない。mode変更だけならepochは不変。 |
| snapshot free | 呼出元が所有しgf5は借用。確定設計のfree契約に従い、FAULTを理由とするEBUSYや「唯一の復旧手段」判定は追加しない。free時は借用image参照を外し、所有するextent画像・trip・metadataを解放する。ROM/借用helperは最後のsnapshotとcleanupが終わるまで保持。 |

### A1で共有するhost API（G1、tools/tests/gf5_fake.h）

| 型・API／部品選択 | 実装すべき契約 |
|---|---|
| F3土台再利用の可否 | GF3_HOST_UNIT=12＋gf_accel_host.hのruntime/画像参照を再利用可。ただし32bit MMIOのみ・now=u32・MATCHのみでNEXT/RESERVE/MAPなし・F4保存型なし。F5全試験の完成した土台とは扱わない。F4 A1のindexed MMIO/read scriptは共用し、gf_command_host.hやF3 B0 stubを重ねない。 |
| fixture/event | `enum gf5_host_op { F5_ADMIT,F5_DRAIN,F5_PAUSE,F5_RESUME,F5_STOP,F5_RESTART,F5_SAVE,F5_RESTORE,F5_SCRIPT,F5_ALLOC,F5_FREE,F5_COPY,F5_NEXT,F5_MATCH,F5_CLOCK64,F5_GPIO_FIND,F5_GPIO_GET,F5_GPIO_SET,F5_PERF,F5_VOLT,F5_FAN,F5_CALC,F5_PROG,F5_RAM,F5_OP_COUNT }; struct gf5_host_event { enum gf5_host_op op; u32 nth,a[12]; const void *p[4]; i32 rc; }; struct gf5_host { struct gf4_host display; struct gf5 ctx; u64 now; struct gf5_host_event *events; u32 cevents,nevents,calls[F5_OP_COUNT],fail_at[F5_OP_COUNT]; i32 fail_rc[F5_OP_COUNT]; u8 *arena; u32 arena_bytes,arena_used,live_allocs; void (*observe)(struct gf5_host *,const struct gf5_host_event *); };` |
| helper全口 | `void gf5_host_init(struct gf5_host *,struct gf4_state *); void gf5_host_trace_clear(struct gf5_host *); void gf5_host_fail_at(struct gf5_host *,enum gf5_host_op,u32 nth,i32 rc); void gf5_host_arena(struct gf5_host *,u8 *,u32);` callbackはlinksと同型、各欄名にgf5_host_を付ける。initはfixture零化→共通io/links接続、借用buf/ROM/予約は試験が設定。trace/fail/observeはF4と同じ規約。 |
| 時刻・資源・保存 | F5 A1のinitがF4初期化後にdisplay.now=&host->nowを設定するだけで、F4 A1所有のclock/waitが共有時刻を使用。clock64は同じnowの全幅。試験側static arena（8MiB超ケース含む）からalloc、freeを追跡し失敗注入、libc malloc禁止。map/unmap、NEXT/MATCH、COPYは実キーと借用VRAM像に作用。NEXT列挙中変更禁止・終端出力保持、COPYは成功時のみ像更新、stop/restartはepochとready/所有を実契約どおり変更。 |
| UNIT別依存stub | 必須GF5_HOST_UNIT=0..6（B番号）、7=なし。F4共用はGF4_HOST_UNIT=14。B0:PERF表は製品実物、CALC/RAM/FAN/VOLT/PROGを偽装して統括列照合。B1:既存PLL parser/solver実物＋偽MMIO。B2:links PAUSE/RESUME/SCRIPT＋indexed MMIO。B3:GPIO_FIND/GET/SET・clock64、B4:ROM＋MMIO（GPIO/vmapは製品実物）。B5:MATCH/PAUSE/RESUME、B6:NEXT/MATCH/ALLOC/FREE/COPY/DRAIN/STOP/RESTART/SAVE/RESTORE＋性能復元の型付きstub。未選択外部定義は出さず各Bの局所stub禁止。 |
| 試験と後続登録 | 全Bが正常対照・途中I/O失敗・所有保持・元rc・deadlineを観測。B2のself-refresh中CPUアクセス禁止、B5の予約外/他面侵入、B6の境界分割/確保失敗前停止なし/失敗像保持/古いepoch拒否を必須化。A3がCHECK/covers/link/一意anchor変異を集約。F4共通clock/wait・MMIO分岐の変更はF4 A1だけ、F5 A1は接続とF5専用stubだけ、host実行は全実装後のPM段。 |

F5のF3待ちは機能結合4段＋共有登録1段＝5段。F4と合計 **11段**（元の段IDで数え、C2のファイル分割数・実機T・A0レビュー待ちは加算しない）。うち機能結合9段、共有登録直列2段。A0/Bのコーディングが層3全体の完成を待つ段は0。層3実機全項目合格を一括で待つ段も0。
未決は **0件**、ユーザー判断は0件。本案の型・署名はレビュー対象であり確定済みとはしない。端子/ROM有無・tear・容量実測は既存の実装後検証であり未決数に含めない。PORT_FLOW §3/§5の共有編集者、像/動的RAM予算、宣言/include、libcなしhostの点検を上記に反映した。

### ID 6 — GF_FEAT3合算予算への差替え（PM統合段で反映）
| 区分 | r3の再計上（KiB、宣言のsizeofを正とする） |
|---|---|
| module mem | F3表の64＋(22〜28)＋(15〜24)＋20＋(0〜4)=121〜140を基準とし、F4 ctx等・F5常駐構造体の実測増減（libgccは含めない）を各内数へ差し替える。旧F4≤24/F5≤24やmem_max=160を固定条件にしない。PMが合算上側＋余裕でmem_maxとprefix込みload_span_maxを決定しschema/検査を同期する。 |
| kernel所有・総RAM | F4 stateは10〜14から約17.2へ更新、kernel側増分は約3.2〜7.2。module memとF4 stateだけの合算は138.2〜157.2（ctv除去前の保守値）、さらに既存kernel helper/診断・動的ROM/trip/snapshotを別途加算。kernel所有stateをmodule BSSへ二重計上しない。 |
| 動的snapshot | 旧metadata約2KiBを廃止。固定部はsizeof(gf5_image)（gf4_saved約5.1KiBを含む）で算定し、概算枠は旧2＋5.1≒7.1KiB。総量は固定部＋trip深いコピー＋nextent*sizeof(gf5_extent)＋全extent画像＋割当丸め。module mem外の動的RAMとしてGF_FEAT3の別台帳へ加算、実測で確定。 |

反論: 0件。

### 確定の補い (2026-11-04、claude-opus-5-5 の第 3 回、PM が反映)
- P3-a: timing.flags (NEG_H 等) と TIMINGS=32 は data.h に置く (gf4_timing と gf4_edid_state.timing[32] が data.h にあり、edid.c は data.h だけでコンパイルする)。
- P3-b: 「ctv 重複除去前の保守値」の注記は、r3 で重複していた分 (304B) を gfinit_mode の work[2] から外した、と読む。数字は最終の sizeof で確定する。

## 追記 (2026-11-04、A1 の欠け — IRQ の snapshot の失敗の注入、PM 決定)
- `gf_irq_snapshot` は実物で `void`、出力を必ず写す (hwdev/gf/irq.c:233-238)。試験の土台の失敗の注入は実物の契約に合わせる (先回りの点検の決まり) ので、`D_SNAPSHOT` は `gf4_host_fail_at` の対象から外す。土台の snapshot は常に出力を写し、rc を持たない。snapshot の中身を変えて試したい場合は、注入ではなく偽の IRQ の状態 (counter・pending) を先に設定して表す。実物に無い失敗を作らない。
