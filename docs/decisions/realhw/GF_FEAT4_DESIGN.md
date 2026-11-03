# GeForce 6200 (NV44A) 層 4 (表示) — 確定設計: 複数のモードと色深度・EDID/DDC・DPMS・HW カーソル・VBLANK と flip・ガンマ・TMDS/TV/2 画面

> 出所: ~/os32-tmp/run/gf-feat4/DESIGN.final.md (astra 起草 r1〜r2、Opus レビュー 2 回、P3 は PM が反映)。
> 状態: **現行 (2026-11-02)** — 段の状態は票 TASK_GF_FEATURES が持つ。層 1〜3 は GF_FEAT1〜3_DESIGN。

# NV44A 層4 詳細設計 r2 — 表示
## r1→r2 の変更
- ID 1: PLLSEL bit9を保持し、食い違いを0件へ訂正、変異をbit9削除へ反転。
- ID 2: PLLSELを688→153→162の順で生成、2headは両headの要求から合成。
- ID 3: counter増加後highなら即base、lowだけ立上り待ち、guard/能力bitを削除。
- ID 4: 層3 accel_outを維持しContextを拡張、OPS_VERSION 3→4、全backendを再ビルド。
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
- 補助参照は指定dispnv04内のREG・arb.c・tvnv17.c・tvmodesnv17.c。DDCは手元のMIT原典 R/haiku/src/add-ons/accelerants/nvidia/engine/nv_i2c.c と N/nvkm/subdev/bios/i2c.c を使用 (§4)。

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
struct gf4_cursor { const u32 *argb_pm; u32 bytes,pitch,w,h; s32 hot_x,hot_y; };
struct gf4_ctx; /* production所有、head[2]、mode表、route、LUT、予約、epoch */
int gf4_modes(struct gf4_ctx *, u8 connector, const struct gf4_mode **, u32 *n);
int gf4_set_mode(struct gf4_ctx *, const struct gf4_route *, u32 id,
                 const struct gf3_surface *front, u32 dl);
int gf4_ddc_read(struct gf4_ctx *, u8 bus, u8 block, u8 out[128], u32 dl);
int gf4_dpms_set(struct gf4_ctx *, u8 head, enum gf4_dpms, u32 dl);
int gf4_cursor_image(struct gf4_ctx *,u8 head,const struct gf4_cursor *,u32 dl);
int gf4_cursor_move(struct gf4_ctx *,u8 head,s32 x,s32 y);
int gf4_cursor_show(struct gf4_ctx *,u8 head,bool show);
int gf4_vblank_wait(struct gf4_ctx *,u8 head,u32 after,u32 dl,u32 *seen);
int gf4_flip(struct gf4_ctx *,u8 head,const struct gf3_surface *,bool sync,u32 dl);
int gf4_gamma(struct gf4_ctx *,u8 head,const u16 rgb[3][256]);
```
- dlはF2と同じ操作全体の絶対期限、既定2秒。DDCはprobe全体2秒/1block100ms、DPMSの10msも同じ期限内。負のF1/F2/bus rcは保存して伝播、HALで既存OS32エラーへ変換し詳細last_rcを試験opsから取得。
- 検査は境界だけ: ready/epoch、head/route、format、mode id、pitch/面末尾/予約重複。pending操作1個/head、再入EBUSY。IRQ内にalloc/待ち/serialを足さない。成功時だけselected_mode/front/DPMS状態をcommit。
- VRAM予約はF2 push開始B未満の共通台帳: 表示面1〜2枚/head→2KiB整列cursor 16KiB/head→F3試験面。末尾instmem・pushを侵さない。面末尾はoff<=limitかつbytes<=limit-offで検査。一般heapは不要、mode変更前に新予約を試算する。
- set_mode/DPMSはflip受付停止→F2 reference drain→cursor hide→表示状態変更。失敗は旧timing/PLL/base/LUT/routeを既存recover方式で復元、復元失敗ならreusable=false→既存GF停止/PC-98経路へ。旧面は表示停止が確定するまで解放しない。
- 停止順: 層4 (flip受付停止・進行中処理を完了/打切り→cursor hide→DPMS ON)→層3 fini→gf2_stop (drain→channel停止)→F1 stop→quiesce rows (局所復元)→route_to_98。F1 stop後はVBLANKを待たない。失敗時は既存の所有保持/復元規約に従う。
```c
/* HAL共有型。任意hookをGfxBackend末尾に追加、NULLはNOSYS。 */
struct GfxCursorImage { const u32 *argb_pm; u32 bytes,pitch,width,height;
                       s32 hot_x,hot_y; };
struct GfxScanout { u32 reservation_id,offset,bytes,pitch,width,height,format; };
/* offsetの所有者はreservation_idで照合、ユーザー物理番地を受け取らない。 */
int (*set_mode)(u32 id); /* default head A/既知analog、予約と記述子も更新 */
int (*dpms)(u8 head, u32 state);
int (*cursor_image)(u8 head,const struct GfxCursorImage *);
int (*cursor_move)(u8 head,s32 x,s32 y);
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
/* GeforceModuleContext末尾: F3の欄を保持し、その後へ追加。 */
const struct gf3_test_ops **accel_out; /* F3既定、重複追加しない */
const struct gf4_data_ops *display_data; /* kernel所有、version=1/size照合 */
struct gf4_state *display_state; /* A0で定義、mode/LUT/TV作業域、kernel所有 */
const struct gf4_test_ops **display_out; /* kernelのstatic slotの番地 */
struct gf4_test_ops { struct gf4_ctx *ctx;
 __typeof__(&gf4_set_mode) set_mode; __typeof__(&gf4_ddc_read) ddc_read;
 __typeof__(&gf4_flip) flip; int (*last_rc)(struct gf4_ctx *);
}; /* 通常操作はHAL hooks、route/DDC/詳細rcだけこの試験dispatchも使用。 */
```
- A0/C3: geforce_module.hのGEFORCE_MODULE_OPS_VERSIONとtools/module_schema.py:187のops_versionをF3の3→4へ統一し、生成manifest/loader/host期待値も更新、旧版拒否。export_max=1/ops_slots=1は維持しinitializeのContext経由で渡す。初期化成功後だけdisplay_out公開、stop/abort/recover開始時NULL、回復成功後再公開。kernelは呼出しごとslotを読み、unload後は保持しない。借用state/helperはmodule停止完了まで有効。
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
| R/haiku/src/add-ons/accelerants/nvidia/engine/nv_i2c.c:79–97,136–140 | drive=(old&f0)\|01からSCL bit5/SDA bit4を更新。release=1、low=0、上位の他線を保存。終了時STOP→線release→保存したowner/driveを復元。 |
- 既定drive/senseはN/nvkm/subdev/bios/i2c.c:147–155の3f/3e・37/36。DCB指定を優先。sense SCL bit2/SDA bit3はREG:268–273とB3 fixtureで照合。A0で外部取得を待つ項目は無い。
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
- 差分: GEM map/pinをF2 VRAM map＋予約へ置換。画像サイズ可変はHW形式変更ではなく透明paddingなので32/64両方を同じ入口で扱える。

## 7. (e) VBLANK待ち・ページフリップ
| 手本の列・前提 | OS32の列 / 誤りコード |
|---|---|
| F1 §5、C:1277 vblank_handler | gf4_vblank_waitはF1 snapshotのvblank[head]を読み(unsigned)(now-after)!=0で成功。IRQ有効・DPMS ON・head有効が前提。待ち中は既存協調idleを使い、IRQ/PCRTC ACKを自分では書かない。ETIME/ESTATE、epoch変化はESTATE。 |
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
- F3後97〜103KiB (bss込み)＋mod text/rodata14〜21KiB＋追加bss1〜3KiB＝**112〜127KiB**を予算とする。mode/LUT/TV作業域など10〜14KiBは最初からkernel所有、借用pointerのみmodへ。image_max/mem_maxの両方131072以下をlink mapで必須判定。VRAMは別枠: 1280×1024×4×2=10MiB/head、実VRAM・Bで判定。
- B4のEDID parser/規格表とB11のTV係数生成は初めからkernel専用、C4でhelperをbind。modにはMMIO実行・小状態・opsのみ。A/C段で実測し超過段は不合格として配置/実装を縮小、機能削除や上限拡大はしない。
- helper/state/試験slotは§2のContext経由。IRQ/time/MMIO/DMAは既存F1/F2/gf_ioを使用。停止完了後にkernel側slot/借用stateを解除。
- kernel helper text/rodataは7〜10KiB、state10〜14KiB＋試験本体を別計上。hwdev.modは現状112.5KiB＋共有計算/診断変更0〜8KiB＋snapshot等0〜4KiB＝112.5〜124.5KiB予算、bss込みmem/image上限192KiBをC3で検査。全て実測前の予算。

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
| B6 hwdev/gf/display/cursor.c 変 | 画像/offset/位置/表示 / F2 map / 150 |
| B7 hwdev/gf/display/flip.c 新 | vblank wait/base/面寿命 / F1 snapshot,F2 fence / 170 |
| B8 hwdev/gf/display/gamma.c 新、palette.c 変 | gamma shadow/LUT loader共有 / B1 / 90＋20 |
| B9 hwdev/gf/display/output.c 変 | DCB route/analog/共有DAC所有 / B0 / 100 |
| B10 hwdev/gf/display/tmds.c 新 | FP timing/TMDS/script/DPMS / B9 / 240 |
| B11 hwdev/gf/display/tv.c 新、drivers/video/geforce/tv_data.c 新 (kernel専用) | 前者はTV MMIO/DPMS、後者はnorm表/state/rescale純計算、C4経由 / B9 / 計300＋係数表 |
| B12 hwdev/gf/display/heads.c 新 | 2head状態/共有clock競合/owner / B9 / 100 |
| C0 hwdev/gf/display/modeset.c 変 | 準備→load→復元transaction / B群 / 100 |
| C1 drivers/video/geforce/backend_geforce.c 変、include/gfx_hal.h 変 | HAL adapters/記述子/既存route維持、hook追加 / C0 / 140＋35 |
| C2 hwdev/gf/production.h,service.c,display/quiesce.c 変 | ctx/予約/epoch/stop・callback解除 / C0 / 計65 |
| C3 drivers/video/geforce/geforce_module.c/.h、tools/module_schema.py、build/geforce-module.mk・hwdev.mk 変 | Context/版4/生成物更新、リンク分離・像検査、kernel・geforce/cirrus/pegcの全backendを再ビルド / C群 / 計90 |
| C4 drivers/video/geforce/gf_display_data.c 新、kernelのソース列 変 | helper ops/state所有・bind、B4/B11 kernel部分だけをkernelにlink、moduleに重複linkしない / B4,B11 / 60 |
- C3のhwdev.mkは新規ddc/dpms/flip/gamma/tmds/tv/heads.cを全てfilter-out (全てgeforce.mod専用)、edid.c/tv_data.c/gf_display_data.cはkernel専用。既存cursor.cは診断のhide列を維持しF2 mapを使う画像APIをOS32_GEFORCE_MODULE内に限定。既存modes/modeset/output/palette/blank/service/quiesceのF1〜3・helper参照も同条件に置き、hwdev側は従来診断列。timing/format/fifo/PLLは共有純計算/MMIOを維持。hwdev/R1/VPLL/GFA/非module backendの実linkで未解決参照を検出。
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
| 従来経路/容量 | 既定enter/leave/失敗復元、route呼出し回数、GF image/mem128KiB・hwdev192KiB、版4/旧版拒否、detach callback無し、停止順/F1停止後待ち無し。変異:成功前selected_mode更新、F1停止後待ち、旧版受入 | PC-98→GF→PC-98を各試験後確認、停止失敗もserialで追跡 | gf-display-host/lifecycle・既定enter/leave・正しい停止順・版4・GF-L4-Ra266-lifecycle |
- A2のCHECKはname="gf-display-host",tier=1,needs=[],budget=90.0、coversは表示部品・kernel helper・backend/ABI・build定義・gf_display試験。各変異を1箇所ずつ無変更GREEN→実行時RED→復元GREENで判定し、コンパイル失敗は検出に数えない。
- C段でdocs/tasks/DEFERRED_TESTS.mdへ表の8個のIDを各1行追加: `ID | 未実施 | Ra266 0:13.0/0221 | 当該受入の実機項目 | 担当/日付/serial/カメラ証跡は実施時記入`。DPMSのLCD反応、outputsの端子/TMDS/TV/2head、flipのtearも明示して残す。台帳欠落は失敗、登録済み未実施は未実施として保持しhost合格と区別。
- A骨格→B部品＋各変異→C結合/容量→T実機。実機は実装後でよい。カメラ http://hight-pc-vj24lfwz1srg:8080/ のcase番号付き像とserialを対応付け、実機未試験は合格としない。この設計作業では実機操作/撮影/リポジトリ編集をしていない。
- 観測だけのcameraではPLL周波数・H/V停止・全flip無tearを証明できない。OSD/レジスタreadback/IRQ件数を併記し、判断できない項目は測定未了。規格fixture追加はA/Bの実装作業でありユーザー判断待ちにしない。

## 13. 未決 (ユーザー判断が必要なものだけ)
- **0件**。推奨は上記どおり全機能のHAL/カーネル試験入口まで作る。配線・容量実測・tear確認は技術検証事項で、スコープ削減の承認条件にしない。

## 層 1〜3 への送り
- 層1: counter/ACK/IRQ設計は変更不要。wanted_headsの更新とsnapshotを表示adapterへ供給、DPMS/stop後の待ちはESTATEにする。IRQ側のflip callback/SW method処理は追加しない。
- 層2: 表示/カーソル/試験面の共通VRAM予約範囲とBを共有。reference待ちとVRAM map/posted write排出を流用、channel/subc変更なし。
- 層3: INDEX8/RGB565/XRGB8888の面format・pitch/off/bytesをそのまま受け渡す。mode変更でsurface epochを更新、古い表示面を使わない。表示が保持する旧面へ描画しない。F3の97〜103KiB/import29〜31予算に層4分を上乗せしC3で同時検査する。

反論: 0件。

## 確定 (2026-11-02、Opus 第 2 回 Approve、PM が反映)
- ID 11 (P3): DDC の I2C は NV4A では nv04_i2c_new (nvkm device/base.c:685)。§4 の表の sense SCL bit2・SDA bit3 の根拠は REG:268–273 ではなく nvkm/subdev/i2c/busnv04.c:41–70 (sense は :62・:70) に替える。drive は nvkm のとおり「読んで変えて書く、bit0=1」(`val|0x01` で他の bit を残す)。haiku の `&f0` で bit1〜3 を落とす形は採らない (§4 の該当行はこれで置き換える)。
