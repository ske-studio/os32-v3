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
対象: PC-9821 Ra266 / PCI バス 0 の列挙で見つけた位置 / 10de:0221 / LCD1770VM・D-SUB。設計のみ、2026-11-02。

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
- 停止順: 層4 (flip受付停止・進行中処理を完了/打切り→エンコーダーの DPMS OFF (`drm_helper_force_disable_all` 相当)→エンコーダーの restore (V/disp.c:196-197)→CRTC の restore (`nv_crtc_restore` V/crtc.c:692-706 = `nouveau_hw_load_state(saved)` V/hw.c:827-835))→層3 fini→VGA フォントの復元 (V/disp.c:202、F3 fini の fence の後、diagnostic_saved の間は保留)→gf2_stop (drain→channel停止)（内部でF1 stop）→冪等foundation_stop cleanup→quiesce rows (局所復元)→route_to_98。restore の値は module で最初の devinit の直後に 1 回だけ保存した値 (V/disp.c:245・297-302、D/hw_state.c の static)。停止の道で DPMS ON を書かない (手本に無い、ユーザー決定 2026-11-12)。F1 stop後はVBLANKを待たない。失敗時は既存の所有保持/復元規約に従う。
- 表示の保存値は module の寿命で 1 つで、svc_rom の devinit の直後に 1 回だけ取る。保存に失敗しても開始は続ける (devinit の acquire は 1 回しか試せない、hwdev/gf/devinit.c:175-176。手本も V/hw.c:330-334 で記録して続ける)。フォントが取れなければ fonts_valid=false でフォントだけ復元しない。crtc・enc が取れなければ valid=false で、停止は restore しない。valid・fonts_valid を落とすのは module の終了だけ。restore も production の io (l->io) を使う。
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
| C:510–516,938–975 nv11_cursor_upload、:980–1015 cursor_set、:1322 | HWは64×64×32bit、16KiBをVRAM予約。入力はpremultiplied ARGB8888 (LE byte B,G,R,A)。受付時に全画素の各RGB≤alphaを検査し、違反はhide/VRAM変更前にEFORMATで拒否する。32×32も64×64へ透明padding。0<alpha<255だけalpha+1、0/255は保持。hide→CPU転送→posted write排出→offset→show。EFORMAT/ERANGE/ENOSPC。 |
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
- 2026-11-04の層4統合実測: GEFORCEのmemは層3の90,112 Bから155,648 Bへ65,536 B（64 KiB）増加。text 116,767 B・rodata 14,304 B・data 288 B・BSS 21,492 B。mode/LUT/TV作業域10〜14KiBはkernel所有、借用pointerのみmodへ。全層の容量判定は[GF_FEAT3の合算予算](GF_FEAT3_DESIGN.md#合算予算)に従う。VRAMは別枠: 1280×1024×4×2=10MiB/head、実VRAM・Bで判定。
- HWDEVの主なBSSは`gf_accel_test`のtransfer 20,496 B・expected 20,480 B・before 12,288 B・control 4,004 Bで、HWDEV全体のmem実測は200,704 B（上限と内訳は[容量表](../../08_build.md#起動時に読む部品の大きさの上限と由来)）。
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
| 予算 | 層4統合実測（2026-11-04）のGEFORCE mem増分64 KiB、mem合計155,648 B（§10）。kernel helper7〜10、state10〜14KiB、cursor VRAM16KiB/headを別計上。hwdev共有変更は計12KiB以内。EDID/TV helperをhwdev.modへ移したことにはしない。 |

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
- (同日、A1 の欠け 2、PM 決定) event の記録の配列 `p[]` (pointer の引数) と `a[]` (値の引数) は、層 4 の A0 で宣言した公開口の中で一番多い引数の数に合わせる (今の宣言では pointer が gf4_mode_calc の 6 個が最大 → `p[8]`、値は宣言を数えて同じ考えで)。大きさは名前付きの定数で、根拠 (どの関数が最大か) を注記に。event を分けたり別の記録方式にしたりしない。同じ種類の機械的な大きさの決めごとは、A1 の担当が宣言を数えて決め、報告に書けばよい。

## 追記 (2026-11-04、層 4 第 1 波の欠け)

設計追記案。報告の13件（A3=3、B0=1、B2=2、B3=1、B5=3、B6=2、B7=1）と未発注段を対象とする。B1のreport.mdは欠け0件、timing.cは正規化済み入力を前提とし本追記と整合する。本追記の宣言をA0担当が追加・統合し、A1を追随させてから各実装段へ渡す。リポジトリ編集・生成・ビルド・試験実行は行っていない。
照合元: W=/home/hight/wt/os32-v3/gf4-a1、L=/home/hight/wt/os32-v3/gf-l3、N=/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau。以下C=N/dispnv04/crtc.c、H=同hw.c、I=同hw.h、D=同dac.c、F=同dfp.c、T=同tvnv17.c、M=同tvmodesnv17.c。行番号はこのローカル原典。既定の協調切替・IRQ内の待ち禁止・一般heap追加禁止・製品64bit除算禁止を維持する。

### 宣言の追加（以下は完全な新型・署名、既存型への追記欄は明記）

```c
/* data.h: B3/C2の値境界。ROM pointerをB3へ貸さない。 */
struct gf4_i2c_port { u8 drive,sense; };
i32 gf4_edid_modes(const u8 block[128],u32 block_no,struct gf4_edid_state *); i32 gf4_tv_state_calc(u32 norm,const struct gf4_timing *,struct gf4_tv_state *);
i32 gf4_tv_modes(u32 norm,struct gf4_edid_state *);
/* gf4_data_ops末尾に追加。上記3関数をC4がbindする。 */
/* i32 (*tv_modes)(u32 norm,struct gf4_edid_state *); */
/* data.h: D0の純解析。値だけ返し、ROM参照を出力に残さない。 */
struct gf4_gpio_value { bool present; u8 line,log[2]; };
struct gf4_dcb_values { struct gf4_route routes[32]; u32 nroutes; struct gf4_i2c_port i2c[16]; i32 i2c_rc[16]; struct gf4_gpio_value tvdac[2]; };
i32 gf4_dcb_parse(const u8 *rom,u32 size,u32 bmp_offset,struct gf4_dcb_values *out);
/* api.h: C2が所有してctxが借用する、値取得とproduction接続の口。 */
struct gf4_links {
 i32 (*i2c_port)(void *,u8 bus,struct gf4_i2c_port *);
 i32 (*admission)(void *,bool open);
 i32 (*failed)(void *,i32 cause,u32 dl);
 i32 (*scan_changed)(void *,u8 head,const struct gf3_surface *);
 void *owner; };
/* gf4_exec_ops末尾: C2が実装・所有、B10/B11から呼ぶ。 */
/* i32 (*external_dpms)(void *,const struct gf4_route *,enum gf4_dpms,u32 dl);
   i32 (*tv_gpio)(void *,bool on,u32 dl); */
/* gf4_head_image末尾: bool cursor_valid; （可視要求とは別の画像完成状態） */
/* gf4_flip_hold末尾: bool base_verified; （base_writtenは投入の可能性） */
/* gf4_ctx末尾: const struct gf4_links *links; struct gf_display_context *legacy;
   struct gfinit_pll_limits limits[GF4_HEADS]; bool limits_valid[GF4_HEADS];
   bool changing,saved_closed; i32 recovery_rc; u32 next_mode_id;
   struct gf_vram_reservation_node scan_next[GF4_HEADS][2]; u8 scan_bank[GF4_HEADS];
   struct gf4_dcb_values dcb; const struct gf4_saved *diagnostic_saved; */
/* api.hに struct gf_display_context; を前方宣言。internal.hでは
   gf_display_context末尾へ struct gf4_ctx *display4; を追加。相互に借用。 */
/* gf4_state末尾: struct gf4_head_image next[GF4_HEADS];
   struct gfinit_mode committed[GF4_HEADS]; struct gf4_tv_state rollback_tv[GF4_HEADS]; */
i32 gf4_detect(struct gf4_ctx *,u32 dl);
i32 gf4_pllsel_calc(const struct gf4_head_image heads[GF4_HEADS],u32 *out);
i32 gf4_change_begin(struct gf4_ctx *,u32 dl); i32 gf4_change_end(struct gf4_ctx *,i32 operation_rc);
i32 gf4_blank_set(struct gf4_ctx *,u8 head,bool blank,u32 dl);
i32 gf4_cursor_apply(struct gf4_ctx *,u8 head,bool visible); i32 gf4_cursor_close(void *owner,struct gf_vram_view *);
i32 gf4_flip_complete(struct gf4_ctx *,u8 head,u32 dl);
i32 gf4_palette_set(struct gf4_ctx *,u8 head,u32 first,u32 count,const u8 *rgb);
i32 gf4_dac_set(struct gf4_ctx *,const struct gf4_route *,bool on);
i32 gf4_scan_prepare(struct gf4_ctx *,u8 head,u32 mode_id,struct gf3_surface out[2],u32 dl); i32 gf4_scan_discard(struct gf4_ctx *,u8 head,u32 dl);
i32 gf4_service_bind(struct gf4_ctx *,struct gf_display_context *,const struct gf4_links *);
i32 gf4_service_start(struct gf4_ctx *,u32 dl); i32 gf4_service_stop(struct gf4_ctx *,u32 dl);
i32 gf4_service_recover(struct gf4_ctx *,u32 dl);
i32 gf4_diagnostic_save(struct gf4_ctx *,struct gf4_saved *,u32 dl); i32 gf4_diagnostic_restore(struct gf4_ctx *,const struct gf4_saved *,u32 dl);
i32 gf4_display_saved_release(struct gf4_ctx *,const struct gf4_saved *);
/* C4 kernel専用。貸出しはC3 initialize前、clearは完全停止後だけ。 */
i32 gf4_data_bind(const struct gf4_data_ops **,struct gf4_state **);
/* geforce_module.h（C3）: 通常slotと復元専用slotは呼出しごと再読。 */
struct gf4_test_ops; struct gf4_restore_ops;
const struct gf4_test_ops *geforce_display_ops(void);
const struct gf4_restore_ops *geforce_display_restore_ops(void);
/* api.h: module常駐、C2-save所有。restore欄はdiagnostic_restoreへbind。 */
struct gf4_restore_ops { struct gf4_ctx *ctx;
 i32 (*restore)(struct gf4_ctx *,const struct gf4_saved *,u32 dl);
 i32 (*last_rc)(struct gf4_ctx *);
 i32 (*saved_release)(struct gf4_ctx *,const struct gf4_saved *); };
/* gf4_test_ops末尾に __typeof__(&gf4_関数名) 欄を追加:
   modes,detect,dpms_set,cursor_image,cursor_move,cursor_show,vblank_wait,gamma,
   display_save,palette_set,scan_prepare,scan_discard。既存set_mode/ddc_read/flip/last_rcは保持。 */
/* kdebug/gf_display_test.h（A3） */
i32 gf_display_test(u32 session);
/* gf4_display_fake.h（A1） */
void gf4_host_write_applied_error(struct gf4_host *,u32 nth,i32 rc);
/* fixture末尾: struct gf4_links links; struct gf4_i2c_port port;
   struct gf4_edid_state tv_modes; i32 port_rc; u32 applied_write_nth; i32 applied_write_rc; */
/* GeforceModuleContext末尾へ既決display_data/state/out,feature5_outに続けて
   const struct gf4_restore_ops **display_restore_out; を追加。版5で一括統合。 */
```

rc共通: 0成功、必須NULL/範囲外head/不正列挙値EINVAL=-401、数値・寸法範囲ERANGE=-402、色形式EFORMAT=-403、PLL不成立EPLL=-404、DDCプロトコルEDDC=-405、EDID破損EEDID=-406、容量ENOSPC=-407、無効状態/世代ESTATE=-408、期限ETIME=-409、未対応EUNSUPPORTED=-410、再入/競合EBUSY=-411、端子/指定bus不在ENODEV=-412。F1/F2/V0/busの負rcは元値を保持する。getter/純計算の失敗は出力不変。副作用のある口は下記の所有保持規約を適用する。
既存宣言の変更理由: B4/B11がdata.hだけでrcを使えるようenum gf4_rcをapi.hからdata.hへ同じ値のまま移す（api.hからのincludeによる利用は不変）。追加欄を含むSTATE/SAVED/DATA_VERSIONを2へ、EDID/TV_VERSIONは1を維持。F5保存型もsizeof/versionに追随。旧構造体の先頭・既存関数署名は変更しない。大小の異なるstateをversionだけで通さずsizeも照合する。

### 関数 → ファイル → 段 → stubを置くA1のUNIT

パスのD=hwdev/gf/display、K=drivers/video/geforce。関数はgf4_を省略、callbackは欄名で示す。—はstub不要（UNIT=14で実物結合）。同じ行の関数は同じファイルが所有し、自己実装のstubは出さない。C1のHAL adapterは既存backend_geforce.cに置く。

| 関数 | ファイル | 段 | stub UNIT |
|---|---|---|---|
| detect / modes | D/modes.c | B0 | — |
| pll_calc / pllsel_calc | D/pll_program.c | B2 | 0（pll_calcのみ）,13（D_PLLSELを含む） |
| ddc_read | D/ddc.c | B3 | 0 |
| edid_modes | K/edid.c | B4 | 0（data callback） |
| dpms_set / blank_set | D/dpms.c | B5 | 13 |
| cursor_apply / cursor_close / cursor_image / cursor_move / cursor_show | D/cursor.c | B6 | 5,13（apply）、他は— |
| flip / flip_complete / vblank_wait | D/flip.c | B7 | 13（complete） |
| gamma / palette_set / lut_load | D/gamma.c | B8 | 13（lut_load） |
| route_check / dac_set / analog_load / analog_dpms | D/output.c | B9 | 5（dac/analog_dpms）,10,11（dac_set）,13 |
| tmds_load / tmds_dpms | D/tmds.c | B10 | 5,13 |
| tv_load / tv_dpms | D/tv.c | B11-MMIO | 5,13 |
| tv_modes / tv_state_calc | K/tv_data.c | B11-data | 0（tv_modes）,11（tv_state callback） |
| heads_apply | D/heads.c | B12 | 5,13 |
| mode_calc / mode_load / set_mode | D/modeset.c | C0-load | — |
| change_begin / change_end | D/transaction.c | C0-tx | 5 |
| dcb_parse（内側のroute/I2C/GPIO解析も同ファイル） | D/dcb.c | D0 | — |
| service_bind / service_start / service_stop | D/service.c | C2-service | — |
| links.i2c_port / links.admission | D/links.c | C2-links | 3（i2c_port）,5,13（admission） |
| exec.tmds_script / exec.external_mode / exec.external_dpms / exec.tv_gpio | D/links.c | C2-links | 10（TMDS/外部）,11（GPIO） |
| display_save / display_restore / diagnostic_save / diagnostic_restore / display_saved_release | D/save.c | C2-save | — |
| service_recover / links.failed | D/recover.c | C2-recover | 5,13（failed） |
| scan_prepare / scan_discard / links.scan_changed | D/scan.c | C2-scan | 7（scan_changed） |
| geforce_display_ops / geforce_display_restore_ops（gf4_なし） | K/geforce_module.c | C3 | — |
| data_bind | K/gf_display_data.c | C4 | — |
| gf_display_test（gf4_なし） | kdebug/gf_display_test.c | A3 | — |

### 段カード（行数はterraの追加・変更Cコードの目安、関数所有は上表）

- **C3-slot（100〜150行）/ C2-serviceから公開を依頼** — C3はkernel staticの通常/復元slotをContextで貸し、accessorはmodule登録中だけその値を返す（未登録NULL）。C2がstart全成功時に通常slotを公開、変更/stop/recover/unload開始前にNULL。復元slotは通常稼働中・保存済み・失敗後もmoduleと予約が生存する間だけ公開、処理中NULL。旧ops/ctxを呼出し間で保持しない。W/geforce_module.c:20–25の既存accel/foundation accessor方式を踏襲。NのDRM直呼びをmodule借用dispatchへ置換するためのOS32固有境界。
- **C2-save-a（100〜180行）: 入口・閉鎖・公開** — test_ops.display_saveは診断wrapperを通して既存gf4_display_saveを呼ぶ。保存前に受付閉・drain・窓close、成功時だけ*outへ値snapshot、saved_closed=true、通常slot閉、復元slot公開。失敗はout不変、所有保持、復元/停止可能な管理口を残す。restore_ops.restoreはgf4_diagnostic_restoreへbindし、saved_closed=falseの通常稼働中も受け付ける。内部で受付閉・drain・窓close、version/sizeと全予約matchを検査し、HW復元開始前にsaved_closed=trueとして、完全成功後だけsaved_closed=falseとして通常slotを公開する。F5保存中はESTATE。
  gf4_display_restoreはF5から直接使う低水準入口として通常slotを一切開かず、診断wrapperもこれを呼んだ後に公開する。A3はkernel static gf4_savedを所有し成功まで再試行用に保持。display_saveは診断/F5双方で成功時saved_closed=true、低水準display_restoreはこれを解除しない。診断wrapperの全成功時だけ解除・公開し、F5は最終RUN時に解除・公開する。失敗は閉鎖を維持する。C:671/692のnv_crtc_save/restoreとH:827–835のstate loadに対応、BO寿命の代わりに台帳とmodule slotを用いる。
- **A3-wire（100〜150行）: 診断入力** — 下の固定wire/case列を採用。診断復元はcase終了と明示RESTOREの双方から管理slotを再読して行う。撮影中に自動restoreしない。描画/転送はF3 opsを毎回再読し、予約・画像を診断側が勝手にreleaseしない。C:1136–1217のflipと§12の受入をOS32の段階実行へ分けるもので、nouveauに撮影protocolはない。
- **B0（130〜200行）: 値で受けた候補からmode表を構築** — gf4_detectは全connectorをまとめて構築する唯一の口。初期化か全head無効・hold無し・受付閉の明示再検出だけ許可（利用中EBUSY）、問い合わせgf4_modesは構築しない。C2がROMからhead別limitsを取得してctxへ値コピー、B0はlimits_valid/head_mask・encoder上限・CR/pitch/VRAMを検査。connector昇順の連続領域、各領域はpreferred→EDID残り→既定、各timingを8/565/32順、重複排除。96枠はまず各connectorの先頭timing×3を確保し残余を順に配る。必須分が入らなければENOSPC、余り候補の切捨てはedid.truncatedで記録。
  IDはctx.next_mode_id=1から単調増加、再検出でも再利用/折返しなし、枯渇ENOSPC。失敗時nmodes=0・非公開なので半表を返さず、再試行可。DDC/EDID失敗は理由をlast_rcに残し既定3depthで検出成功、TVは後述norm表。C:129–151と§3候補規則に対応、DRM mode listを固定96件表に置換。
- **B2-a（100〜150行）: PLLSEL純計算** — gf4_pll_calcは従来署名で自headのPLLと要求maskだけ計算。C0はstate.nextに旧head[2]をコピーして対象変更を反映し、gf4_pllsel_calc(next,&value)で最終PLLSELを一度だけ合成、両workのpllselへ同値を設定する。無効headのmaskは0、DPMS OFFでもenabledならPLL要求は保持。savedの古いhead/TV maskは使わずC:688→153→162通り、NV44はbit2＋有効headのVPLL mask＋TVの要求mask。ただしNV44内蔵nv17 TV/CTVのTV maskは0（T:403–577はPLLSELを設定しない）。
  N/dispnv04/tvnv04.c:68–100の外付け旧TV用maskを内蔵TVへ流用しない、head0のbit9を保持。未知route EUNSUPPORTED、共有出力競合EBUSY。単体B2は相手headを推測しない。
- **B2-b（100〜180行）: 共有PLL helper拡張、PMが直列化** — kdebug/gfinit_pll.hにGF_PLL_VIDEO_B=0x680520を追加、sequence.incの既存gfinit_pll_sequence署名を保持しA/Bを識別。A/Bのstageは共通0x680580、single bitは8/28、coeffは0x680578/0x68057c、gate 0xc040のmaskは3<<16 / 3<<18、VPLLは0x680508/0x680520。gate退避→stage RMW→coeff→VPLL→gate復元の5-write、他head bit保持。根拠N/nvkm/subdev/devinit/nv04.c:184–193,198–273。
  parse.inc:13もVIDEO_Bを受け、ROMの該当reg→無ければ既存先頭entry fallbackを維持。helperの不成立-1をB2がEPLLへ変換、bus rcはそのまま。B2の編集範囲をこの共有3ファイル（header/sequence/parse）とhost fixtureまで拡大し、PMがF5 PLL担当との順序を直列化。
- **B3（100〜180行）: I2C値取得からbitbang** — links.i2c_port(owner,index,&port)をB3が開始前に呼ぶ。C2-linksがctx.dcbの値から指定indexのdrive/senseだけ返す。DCB優先、BMPはent+4..7を先に読み、各byteが0のときだけindex0=3f/3e、1=37/36を使う。BMPが無い場合や他indexへ既定を流用しない。不在ENODEV、非NV04 bus EUNSUPPORTED、破損EDDC。値コピーなのでB3はROM寿命を持たない。N/nvkm/subdev/bios/i2c.c:64–76,120–155、同i2c/busnv04.c:41–70と同じhead0経路・owner/drive復元。
- **B5（100〜150行）: DPMSとblank、blank.cは旧入口の委譲だけ** — gf4_service_bindでlegacy.display4とctx.legacyを相互接続、完全停止後解除。productionの旧gf_display_blankは接続中gf4_blank_set(head0)へ委譲、成功時だけlegacy.blankを同期。非module診断列は従来通り。正本はhead.blank、物理SR1 bit5=(blank || dpms!=ON)。OFF中blank解除で点灯しない。初回startで旧blank/paletteをhead0へ取り込み、restoreも両正本を同期する。L/display/blank.c:14–33とC:175–233、H:110–124を合成、独立した二つのshadow更新はしない。
- **C0-tx（100〜200行）: begin/endと局所rollback** — B5は変更前gf4_change_begin、操作後gf4_change_end(rc)を呼ぶ。beginはchanging検査→通常slot/hook閉→両hold収束→F2 drain→rollbackへhead/hold/IRQ、rollback_tvへTV保存、nextへコピー→一時cursor hide。beginが失敗した場合はbegin自身が既に変更した受付/物理hideだけを戻す（再入EBUSYは無変更）、呼出元はendを呼ばない。end成功はnext/作成workをcommitしIRQを反映、saved_closedまたは既存F5保存・復元中ならscan hook/通常slotを公開しない（失敗後の再公開にも同条件）。
  F5状態の正本を参照し二重shadowを作らない。それ以外はscan hook・受付を最後に公開。失敗はcommittedの旧HW列・rollback TV/LUT/base/route/DPMS/blank/cursorを受付閉で再loadし、成功なら旧論理状態と公開を戻す。元rcを返し、復元rcはctx.recovery_rcへ別保存。復元失敗はreusable=false→links.failed、停止完了まで全予約保持。L/display/modeset.c:16–29の局所restore方式を両headへ拡張、F5用save/restoreを入れ子で使わない。
- **B6-a（100〜150行）: 物理可視と要求の分離** — gf4_cursor_applyは物理可視だけを変えcursor_visible要求を書換えない。trueはenabled・ON・!blank・cursor_validを満たす場合だけ許可（それ以外ESTATE）、falseは無条件hide列。公開showは要求変更を成功時だけcommit、OFF/blank中は要求を保存して実表示false。DPMS/blank復帰は要求とvalidを検査してapply。I:372–385、C:938–1015のhide/upload/show責務を分離する。A1 UNIT=5にこの口のstubを追加し、公開showの代用にしない。
- **B6-b（100〜180行）: 画像転送とvalid** — cursor_valid初期false。入力/予約検査は上書き前に済ませ、hide成功後・最初の転送前にfalse、全16KiB転送・排出・offset/位置設定成功でtrue。転送/設定失敗後はfalse/hideを維持しshow(true)はESTATE。hide自体失敗時は画像を上書きしない。次の完全image成功だけがvalidを戻す。C:938–1015、I:341–349に対応、途中像の再表示防止に必要な画像状態で、監視用能力bitではない。F5 snapshotはvalidを含むが画素はF5 extentに保存、普通のDPMSは画像を変更しない。
- **B6-c（100〜150行）: 同じcursor.cのmap/close、B6-a→b→cで直列** — gf4_cursor_closeはowner=ctx、viewがctx.cursor_view[0/1]か検査（不一致EINVAL）、CPU書込み排出→gf_vram_view_unmap、成功時のみVA借用を捨てる。GPU待ち/map/別窓操作なし、失敗は元rcと全所有を保持。gf4_cursor_imageは呼出しごとにmapし、その直前にclose/close_ownerを設定、転送・排出後にgf4_cursor_closeで閉じてから成功を返す。途中失敗もcloseを試み、失敗した窓は所有を保持して停止時に同callbackで再試行。restoreは画素を再mapせず予約offset/位置/可視だけを再設定する。
  F3 closeが既に呼んだ窓は再closeしない。L/vram_view.h:100–119、N/nouveau_bo.c:669–689のmap寿命に対応。cursor_validはunmapだけでは消さない。
- **B7（150〜200行）: pendingの確定** — gf4_flip_completeはB7実装、C2が唯一の所有確定adapterとしてscan_changedを実装。base書込みを試す直前pending/base_written=true、base_verified=false、両面保持。readbackで新offを確認してからsnapshotしafterを更新、base_verified=true。以後counter差!=0を観測したら新面でscan_changed→成功時frontを確定・pending=false（B7はreleaseしない）。sync=trueは同関数で待ち、falseは投入後に返す。vblank_waitはpendingがあればまず同関数へ委譲し、その後指定afterの待ちを満たす。
  complete内はB7の非公開counter pollを使い公開vblank_waitへ再帰しない。ETIMEはhold維持。不明投入は後続completeがbaseを読み直し、新offならそこから次VBLANK、旧offならそのreadback時点でscan_changed(old_front)→成功時old_front確定・pending=falseとし、追加VBLANKを待たずESTATEを返す（通知失敗はhold保持で再試行）。新flip成功とは扱わない。どちらでもない値はESTATE/両面保持し停止へ。停止確認後はC2だけが解放可を判断。C:1074–1087,1136–1217、H:702–710のlatchを前景で収束させ、IRQ callbackは足さない。
- **B4（100〜200行）: EDID純helper** — 上記gf4_edid_modes/gf4_tv_state_calc/gf4_tv_modesをdata.hで宣言。B4はblock0でversion/size/preferred=U32_MAX等を初期化、後続はnext_block順、不正base EEDID、拡張失敗は既存timing保持してlast_rcへ記録しnext_blockを進める。32件満杯はpreferred保持＋truncated、未知拡張は無視。interlace/doublescan正規化はparser/TV generatorだけで1回、B1は再変換しない。NではDRM EDIDが担当するがGPL移植を避け規格fixtureで独立実装する（§4の差分）。C4のbindは双方の出力を成功時のみ設定し、既存貸出し中のstateを初期化し直さない（EBUSY）。
- **B8（100〜150行）: LUT正本、palette.cは旧入口の委譲だけ** — gf4_palette_setはINDEX8 shadowを更新、gammaは16/32 rampを更新。inactiveはshadowのみ、activeではgf4_lut_loadで全量loadし成功時だけdirty=false。失敗は新shadow/dirtyを保持して次回全量retry。旧gf_display_paletteはdisplay4接続中head0の新口へ委譲しlegacy.paletteも同期、C0のloadはstate.nextのdepthに従い選択。C:778–834、H:795–810、L/display/palette.c:6–25。通常gamma要求のINDEX8はEFORMAT、paletteとgammaを混ぜない。
- **B9（100〜150行）/ B12（100〜150行）: 出力所有とIRQを上表の各ファイルへ** — gf4_route_checkはctx.routesの実エントリ全欄とhead_mask・head・OR・locationを照合、未配線ENODEV、未知外部EUNSUPPORTED、同ORの別head使用EBUSY。gf4_dac_setはB9が実装、変更中はnext、通常はheadから両head利用を導出し対象ORのDACCLKだけRMW、残利用者がいればenable保持。独立したdac_users shadowを足さない。B12 gf4_heads_apply(ctx,mask)は現在FIFO/GR要求を保持してgf_irq_enable、成功時だけch->headsを同値にする。失敗時変更済wanted値はrollbackから再設定。D:426–444、F:204–244、L/irq.c:106–117。DPMS OFFのheadはIRQ集合から外しON完了後に新snapshot。
- **B10-a/b（各100〜160行、同じtmds.cを直列）: load / DPMS** — B10はgf4_tmds_load内でworkのFP値を計算/設定、外部encoderはexec.external_mode/外部DPMS口を使う。今回の既知の外部型は無し。external_mode/external_dpmsは全外付け型にEUNSUPPORTEDをHW変更前に返し、実物のDCBが確認されるまで型別列を足さない。TMDS script callbackはhead/OR/clockに対応する既存ROM interpreterの実行をC2-linksがadapter化し、TMDS呼出し時だけ既存acquire→script選択/実行→releaseの窓内で完結させる。
  gf_init_contextのhead/出力選択とdeadlineを設定、終了時owner復元。fp_onは最後に成功したON値のみ更新、OFF maskで上書きしない。F:68–90,280–480,563–576と同じ、analogからscriptを呼ばない。
- **B11-data / B11-MMIO（各100〜200行）: 純生成 / HW列** — tv_modesはnormの許容入力timingをedid_state形式で返す純関数（preferred=先頭、base_valid=true）、B0はTVにEDID/VGA既定を流用せずこの表を列挙。tv_state_calcは同norm/timingから全enc/filter/PTV/CTVを生成、不正norm EINVAL、不成立timing ERANGE。norm値はtvnv17.h:56–70の12値、propertiesはT:804–807のoverscan/flicker/saturation=50、hue=0に固定（新property APIなし）。tv.cはdata->tv_stateを呼び、成功像だけstate.tvへ採用。
  exec.tv_gpioはC2がDCB TVDAC0/1を処理、存在するlineだけ更新、未記載はnouveau同様無操作、I/O失敗は元rc。DPMSはPTV→GPIO→gf4_dac_set、CTVはFPも処理。T:310–400,403–606、M:406–434。F5 GPIOへ依存して発注を止めず、既存GPIOアクセスをC2 adapterで使用。
- **C0-load（100〜180行）: mode計算とload** — gf4_mode_calcはC0-loadがB1/B2の純計算を呼んで合成、gf4_mode_loadはC0が既存gf_load_state/row実行をhead引数化して所有（B2にMMIO統括を重複させない）。全準備・面検査後change_begin、next/workを作成しshared PLLSEL/SEL_CLKを両現在要求で生成、encoder OFF→共通load→対象encoder列→LUT/DPMS/cursor→change_end。committedは最後の成功HW像、workは候補、rollbackは変更前論理像。新head準備失敗でも既存headを復元し、初回start失敗はlegacyの局所recoverへ。C:461–606、H:468–535,665–835。deadlineは入れ子でも据置、切れた期限で復元できなければreusable=falseとして停止経路へ。
- **D0-a/b/c（各100〜180行、display/dcb.cへ直列）: 純解析の新段** — D0-aはDCB表とroute、D0-bはI2C/BMP、D0-cはTVDAC0/1 GPIOを追加してgf4_dcb_parseを完成。outは呼出元所有、成功時だけ更新、ROM pointerを含めない。routesは有効headごとの値（最大32）、I2Cはindex別rcとdrive/sense、GPIOはpresent/line/log[0,1]。ctx.dcbをC2-serviceが所有しctx.routesをその配列へ結線する。
  原典はN/nvkm/subdev/bios/dcb.c:28–95（表）,121–193（出力値）、i2c.c:29–67（表）,71–104（NV04値）,136–163（BMPと既定）、gpio.c:30–57（表）,78–115（値）,119–149（matchとDCB2.2のTVDAC0）。MMIO・heap・script実行は純解析に入れない。DP/AUX等の新機能は移植しない。
  必須NULLはEINVAL、破損/範囲外のROM参照はEDDC、route容量超過ENOSPC、未知DCB形式EUNSUPPORTED。個別I2C不在ENODEV/非NV04 EUNSUPPORTEDはi2c_rcへ記録して他routeを残す。TVDAC未記載はpresent=falseで成功、架空lineを補わない。limitsは既存PLL parserで別途値取得、scriptはC2-linksの既存interpreterへ渡す。
- **C2-service（100〜180行）: bind/start/stopだけ** — bindはready=falseでlinks/legacyの寿命を確認、startはF3 ready・epoch→台帳移管→D0/limits値取得 (module で最初の devinit の直後に表示の保存: フォント→crtc→enc)→detect→既定head0表示/IRQ→公開。2 回目以降の開始は保存しない。stopは受付閉→pending収束（不能なら表示停止まで保持）→エンコーダー DPMS OFF→enc_restore→crtc_restore (保存値に戻す、DPMS ON は書かない)→窓close→F3 fini→フォント復元→F2 stop→foundation cleanup→局所quiesce→予約解放。完全停止後に相互借用を解除。
- **C2-links-a（100〜150行）: 値getterとproduction adapter** — i2c_portはctx.dcbを値コピー、tv_gpioはpresentなlineだけ既存GPIO口へ接続。admissionはF4 slot/scan hookを閉開するだけでF3 closeを代用しない。external_mode/external_dpmsは全外部型EUNSUPPORTED。links.failedの実体はC2-recover、scan_changedの実体はC2-scanに置き、この段はcallbackを結線するだけ。
- **C2-recover（100〜180行）: 既存serviceへの復旧結線** — links.failedは旧gf_display_stopと既存backend失敗通知へ接続、PC-98選択は既存backend責任。L/service.c:151–158,219–235のgf3_service_recoverは現状release/publishするためF4経路から直接呼ばない。gf4_service_recoverはF4保存→受付閉→同じ停止/restart部品（予約保持）→F3 objects再生成→同一台帳match→F4復元→最後に公開。F2 epochのみ進め、失敗時は窓・予約・state借用を保持。最終cleanupの解放とrecover中の保持を混同しない。
- **C2-links-b（100〜180行）: ROM/script、links-aの後に同じlinks.cへ** — routes/limits/I2C/GPIOは初回acquire中に値へコピーしてreleaseする。TMDS scriptは実行ごとに再acquireし、head/OR/clockから選択・実行してrelease、ROM/span/programを窓外へ持ち出さない。script失敗でもreleaseを試み元rcを保持、release失敗の既存所有は停止cleanupへ渡す。storage_ops/rom_storage/Context.display_storage/ROM_COPY・RELEASE eventは撤回、常駐ROM・専用頁割当を追加しない。N/dispnv04/disp.c:247–294とF:446–480の常駐BIOSをOS32の一時借用へ置換する。
- **C1（100〜150行）/ C2-scan（100〜200行）: HAL呼出し / scan所有** — set_mode(id)はhead0既知analogに限定、mode表から寸法/format/pitchを取得しgf4_scan_prepareでC2のscan[0][2]の新配置を試算・reserve→描画/drain→set_mode、成功後だけHAL記述子/selected_mode更新。scan_prepareは既存front/hold/snapshotを保持して空きnodeだけで配置し、足りなければENOSPC、成功時だけout更新。追加nodeが必要なmode変更はctxに常駐scan_next[2][2]を追加しstage、登録nodeを移動せずscan_bank（0=scan、1=scan_next）を成功時に反転して役割を交換する。
  gf4_scan_discardは未採用の新予約だけを確認して返し、使用中EBUSY、失敗所有保持。page_flipはreservation_idで既存nodeのgenerationを取得してmatch、旧面/holdを描画対象にしない。scan_changedはC2がhead0のscan hook/記述子を更新し、旧面をbackとして保持、差替え/停止時だけ所有nodeをrelease（外部所有面はreleaseしない）。legacy presentは640×480 INDEX8以外EFORMAT、既存PC-98 route順を維持。C:838–903,1136–1217のBO pinを台帳へ置換。
- **C3-ABI / C4 / A2（各100〜200行）: 登録 / kernel bind / 試験結線** — C3はABI版5・header/schema/loader/manifest期待値と2 accessor/slotを一括所有。C4はEDID/TV helperとstateだけkernel配置、DCB parserはmodule側。A2は既存test_gf_display.py/旧caseを保持して新依存linkと変異を一元登録、UNIT番号やBファイル名を推測で増殖させない。A0のprevious_calc基点ヘッダー分離を維持。sizeof増分（next/committed/rollback_tv、snapshot valid/hold欄）とDCB値配列を合算予算へ実測反映、旧17.2KiB等を固定上限にしない。hwdev非moduleリンク・旧ABI拒否・両head/復元失敗/閉slot・元rc・所有保持を対象にする。実行/生成はPM統合段。
- **C0-tx補足**: begin成功時にcommitted→workをコピーし、head/TV/IRQのrollbackを確定してから物理hideする。changing中のLUT/encoder/cursor/DAC helperはnextを読み、通常時はheadを読む。DPMS等が変更しないworkは旧像を保持する。restoreは保存論理像からmode/TVを再計算し、全成功までcommittedを上書きしない。通常の局所rollbackではepochを変えない。
- **C2-save-b（100〜180行、save-aの後に同じsave.cへ）: snapshot借用** — 診断snapshotが指す予約はA3終了まで保持する。C2-saveがdiagnostic_save/restore/saved_releaseを所有し、test_ops.display_saveにdiagnostic_saveをbindする。save失敗は出力不変、成功時にctx.diagnostic_savedへ借用を記録し、restore後もreleaseまでscan_prepare/discardはその予約を返さない。saved_releaseはpointer照合して借用だけ解除（不一致ESTATE）、現front/holdは解放しない。F5の直接saveは診断借用を作らず全予約保持をF5自身が所有する。再入診断saveはEBUSY。

### A3-case（100〜200行ずつ、wire→session→case1/2→3/4→5/6/7の順に同じ診断ファイルへ）

wireは既存GF_OP_COMMAND=17のgf2_requestを維持、enum gf2_case末尾にGF4_DISPLAY=10を追加（GF3_ACCEL=9は維持）。sessionのbits0..7=case、8..15=connector、16=head、17=phase（0=表示開始、1=次の段階/復元）、18..31は0必須。不正case/予約bit EINVAL。宣言節のgf_display_testをA3が実装、C3がgf_command_test/gfinit入力上限とshellの既存command parserへ登録する。
caseは0=列挙/DDC（非破壊）、1=mode/depth、2=DPMS、3=cursor、4=flip、5=gamma、6=outputs/2head、7=lifecycle、8=RESTORE。case1〜7のphase0は保存→必要な予約面をC1/C2経路で準備→F3描画/転送→drain→対象操作→READY(case,step,mode,head,CRC,rc)をserialへ出して戻る。同case phase1だけが次stepへ進む。寸法/format/出力を変えるstep間では先にsnapshotへ復元→前stepの未使用診断面をscan_discard→次stepを準備し、snapshot面＋診断二面を超える蓄積をしない。最終stepで復元、途中case変更はEBUSY、case8はいつでも復元再試行。自動全case巡回をしない。
step列: 1はconnectorのmode表順に色帯＋1pixel格子（各色深度で量子化）、2はON/STANDBY/ON/SUSPEND/ON/OFF/ON、3は非対称32/64矢印・alpha0/1/254/255→四隅→負座標→hide/show→mode変更、4は番号/斜線の二面をsync=true/false各々交互flip（16回/step）しcounter/base/rcを記録、5はidentity→ramp[i]=i*i*257/255→identity→INDEX8 palette帰還、6はroutes順の単独→可能なA+B別模様→片head DPMS/flip、7は既存enter→leave→再enter。未接続/未対応は理由付きSKIP、成功に数えない。
A3はstaticの行buffer/64×64cursor・保存像をkernelに持ち、画像は行単位でF3転送して一般heapを使わない。カメラ担当はREADYのcase/stepを付けて撮影後にphase1を送る（camera API/IRQ同期は追加しない）。保存後F4通常slotを閉じたままにしないよう、A3は診断開始時のsave成功後に管理restoreで元表示を復元し通常公開を確認してから画像caseを実行する。diagnostic_save内でcursor_valid=trueのheadをhideし、成功後cursor_valid=falseとしてその状態をsaveする。hide失敗なら診断を開始しない。旧cursor画素は退避せず、診断終了後も旧cursorを復活させない（cursorを書いているのはA3だけ）。
F5と異なり表示面全体のcopyはせず、診断は新予約面だけを描き元frontを保持する。保存像は終了まで保持し、終了時も再restore可、最終復元成功後saved_releaseして保存像を無効化。F5保存中にはA3開始をESTATEで拒否する。

### A1へ追加する試験APIと結線

追加するevent列はD_I2C_PORT,D_ADMISSION,D_FAILED,D_SCAN_CHANGED,D_CHANGE_BEGIN,D_CHANGE_END,D_CURSOR_APPLY,D_CURSOR_CLOSE,D_FLIP_COMPLETE,D_DETECT,D_PLLSEL,D_PALETTE,D_DAC,D_EXTERNAL_DPMS,D_TV_GPIO,D_TV_MODES,D_SCAN_PREPARE,D_SCAN_DISCARD,D_SAVED_RELEASE（D_OP_COUNTの前）。callbackはgf4_links/execの同署名で `gf4_host_`＋欄名、純helperは同署名のgf4_host_tv_modes、製品依存stubは上記api.h実宣言をそのまま使う。既存gf4_host_fail_at/reads/observeで引数・順序・第n回失敗を検証し、voidのD_SNAPSHOTを失敗注入対象にしない。
fixtureには宣言節の欄を追加し、initでlinksとexec追加欄を接続、limits/validとcursor_validは試験が明示設定。getterは成功時だけport等をコピー、状態変更callbackは本契約の成功時commit・失敗時所有保持を再現する。新getter取得口は増やさず既存のseed/observe方式を使う。
下位依存: UNIT=2はsolver/拡張sequence実物、6は実V0相当close/unmap、8/9はio、12はIRQ/SNAPSHOT。14はstubなしのD0/C1〜C4結合。A2が補助リンクを列挙し、新UNIT番号は足さない。
A1のD_WRITE注入は従来「書かずに失敗」に加え、宣言節のgf4_host_write_applied_errorを追加。指定回の書込みを偽registerへ適用してから負rcを返し、base投入不明を実契約通り試験する。readback旧/new/異値、counter wrap、scan_changed失敗、pending保存/停止、cursor部分転送後show拒否、OFF中blank解除、restore失敗後のslot/窓/予約保持を正常対照付きで追加する。
未決（ユーザー判断を要するもの）: 0件。宣言追加・共有helper範囲拡大・ABI/size追随は本案の統合対象であり実装済みとは扱わない。端子・容量・LCD応答・tearは既存の実装後検証に残す。
レビューの未確認事項（TV原典行・B4規格出典・schema/contract生成への影響）はr1の統合検証範囲を維持し、今回再調査・生成しない。
反論: 0件。

### 確定 (2026-11-04、Opus 第 2 回の条件付き承認、PM が反映)
- (a) C0-tx (transaction.c) と C0-load (modeset.c) は互いを呼ぶため並列に出さない。UNIT 13 で C0-load → C0-tx の順の直列の段とし、両方の実物を結合して host 試験する (mode_load・change_begin の stub は置かない)。
- (b) F4 の中の「F5 保存中」の判定は `saved_closed && diagnostic_saved == NULL` (F5 所有とみなす) と定義する。F4 は F5 を include しない (既決) ので、F5 の状態を見る口は足さない。C2-save-a の ESTATE と C0-tx の「公開しない」はこの述語で判定する。
- (c) 関数の表に 2 行を足す: `surface_check` → D/modes.c → B0 → 7,13 / `timing_calc`・`format_calc`・`fifo_calc` → D/timing.c・D/format.c・D/fifo.c → B1 (実装済み) → 13。
- P3 A1 の event: stub の UNIT が「—」の D_DETECT・D_PALETTE・D_CURSOR_CLOSE・D_SCAN_PREPARE・D_SCAN_DISCARD・D_SAVED_RELEASE は A1 に足さない (使う UNIT が出たらその段で足す)。
- P3 D0 の routes: DCB の 1 entry = 1 route とし、entry の heads を `head_mask` に持たせる (nouveau の dcb_output.heads と同じ)。head ごとに展開しない。B9 の route_check は head_mask で照合する。

### 層 4 第 1 波の再開で決めたこと (2026-11-04、PM 決定)
- **B0 の mode 表と head**: mode 表は connector 単位で、head は持たない (DRM の connector の mode 一覧と同じ)。候補は、その connector の route のどれかの head_mask のどれかの head で PLL limits と encoder の上限を満たせば公開する (和集合)。実際に使えるかは set_mode が選んだ route・head で判定し、満たさなければ EPLL / ERANGE を返す (mode 表の公開はやり直さない)。同じ connector に DCB の route が複数あるとき: 上限は route ごとに判定して和集合、DDC は route の順で最初に i2c が有効な route の port、TV norm は TV の route が 1 本なのでその route のもの。
- **B7 の新旧 off が等しい flip**: 投入不明からの収束で新旧の off が等しいときは、readback の判定をせず新面として即確定する (同じ面なので表示は変わらない、ESTATE にしない)。同じ面への flip は拒否しない (nouveau の nv04 の page flip も同じ fb を拒否しない、dispnv04/crtc.c:1030 以降)。
- **B5 の host 試験**: A1 の UNIT 5 に gf4_change_begin / gf4_change_end の境界の stub を置いた (35cec902)。change_end は注入した rc があればそれを、無ければ操作の rc を返す。UNIT 13 は確定 (a) のとおり stub を置かない。
- **B3 の大きさ**: カードの行数は目安で、超えても止めない。B3 (I2C の bitbang 一式) は sol に替えて書き切る。
- **B0 の route.max_khz**: DCB は encoder ごとの上限を持たない (nvkm/subdev/bios/dcb.c:121-193)。nouveau の TMDS も 165MHz を上限にせず dual link に切り替えるだけ (dispnv04/dfp.c:343-358)。だから D0 は max_khz=0 を書き、0 は「encoder 固有の上限なし」と読む。B0 の判定は PLL limits と data の limits.max_khz (data.h の limits) だけで行う。0 で候補を落とさない。
- **B5 の旧入口の期限**: 旧 gf_display_blank(ctx, blank) は期限を持たないので、委譲の前に既存の型どおり `ctx->io.timing.clock_us` で now を取り、`now + GF3_TIMEOUT_US` を dl にする (hwdev/gf/service.c:280-284 と同じ)。clock が無いか失敗したら GF2_EIO を返し、委譲しない。ctx.deadline は流用しない。
- **B5 の display4 の欄**: `struct gf_display_context` の末尾に `struct gf4_ctx *display4;` を足した (hwdev/gf/internal.h、wt/gf-l4)。結線は C2-service。
- **B0 の色深度の枠**: 色深度ごとに VRAM と pitch で判定し、収まる色深度だけ公開する (§3)。先頭の timing の「3 色深度の必須枠」は、収まる色深度の分だけ確保する。どの色深度も収まらない timing は公開しない。ENOSPC は mode 表の容量が尽きたときだけ返す。connector どうしの枠の配分は、先頭 timing の枠を先に全 connector に確保してから残りを順に詰める。
- **B7 の待ちの間の再入**: OS32 はシングルタスクで協調切替なので、fence 待ち・VBLANK 待ちの間に同じ head の flip が再入することは考えない。専用の状態は足さない。pending はカードどおり base を書く直前に立てる。
- **止める欠けの範囲 (層 4 の残りの段に共通)**: 止めるのは、宣言・試験の土台への追加か、既存の状態の持ち主の変更が要る欠けだけ。そのほかの細かな判断は §3〜§9 と手本に沿って実装者が決め、報告の「実装者が決めたこと」に 1 行ずつ書く (レビューで確かめる)。
- **B11-data と B11-MMIO の分担 (TV)**: `gf4_tv_state_calc(norm, timing, out)` は head・端子に依らない値だけを作る純関数とする (宣言は変えない)。head に依る値 (PTV200 の head bit、`tvnv17.c:482-485`) と、端子・pin mask・property に依る値 (`nv17_tv_update_properties`、`tvmodesnv17.c:470-523`) は、route と head を持つ B11-MMIO の `gf4_tv_load` が書く直前に足す。CTV の FP timing・control・debug・margin と rescaler (`nv17_ctv_update_rescaler`、`tvmodesnv17.c:541-589`、`tvnv17.c:540-574`) も B11-MMIO が `gf4_tv_load` の中で mode から求め、その head の work の FP の欄に置く。手本も 3 つとも mode_set の中で呼ぶ (`tvnv17.c:588-591`)。
- **B11-MMIO の端子と pin mask**: `struct gf4_ctx` の末尾に `u8 tv_pin_mask, tv_subconnector; bool tv_sense_valid;` を、api.h に `i32 gf4_tv_sense(struct gf4_ctx *, const struct gf4_route *, u32 dl)` を足した (wt/gf-l4 da98828b)。持ち主は B11-MMIO (tv.c): `gf4_tv_sense` は nouveau の load detect (`dispnv04/tvnv17.c:144-218`) を移し、pin_mask と subconnector を決めて `tv_sense_valid=true` にする。`gf4_tv_load` は `tv_sense_valid` が false なら先に `gf4_tv_sense` を呼ぶ。subconnector の選択は自動 (手本の既定) だけで、選ばせる口は足さない。`tv_sense_valid` は service の start で false に戻す (C2-service)。A1 の土台への追加は要らない (tv.c の中だけで呼ぶ)。
- **A3 の case7 (寿命) は保存を使わない**: case7 は既存の enter → leave → 再 enter そのものを試験し、leave で予約が解放される既存の約束を変えない。そのため case7 だけは診断の保存 (diagnostic_save) をせず、通常の稼働から始めて、再 enter の後に mode が有効で表示が戻ったことを確かめて終わる。case8 (RESTORE) は case7 に対しては何もせず成功を返す。case1〜6 は従来どおり保存から始めて最後に復元する。
- **TV の timing と B0・B1 の検査 (B11-data の欠け)**: nouveau は CRTC の水平の値を `>> 3` で切り捨てるだけで 8 の倍数を求めず、`vsync_start == vdisplay` も拒否しない (`dispnv04/crtc.c:245-256`)。B1 の `gf4_timing_calc` と B0 の `timing_fits` は、これに無い検査 (水平の 8 の倍数、vs > vd) を外して手本に合わせる (nouveau に無い守りは足さない)。TV の route の候補の可否は、VGA の検査でなく `nv17_tv_mode_valid` (`tvnv17.c:310-340`) の規則で B0 が判定する。B11-data の LD の PLL 90MHz の補正 (`tvnv17.c:360-363`) と CLKDIV2 (`crtc.c:334-335`) は、C0-load と B11-MMIO が反映する。
- **B4 の優先の mode は DRM の規則 (PM 決定、レビューの往復 3 回で決着しなかった点)**: 優先の mode は基本ブロックの最初の DTD とする (EDID 1.3 は優先の bit が立つとき、1.4 は常に)。CEA 拡張の見出しの native の DTD の数は、優先の判定に使わない (手本の DRM の drm_edid.c も使わない — nouveau に無い規則は足さない)。拡張の DTD は元の順に足すだけで、優先にしない。`gf4_edid_state.dtd_seen` (19f8f911) は基本と拡張をまたぐ DTD の通し番号として残す。範囲の記述子が無い EDID 1.4 の標準 timing の CVT の生成と、VIC の表の全欄の照合は、レビューの指摘どおり直す。

## 追記 (2026-11-04、層 3 の実物との照合 X1〜X4)

設計追記。X1〜X4について旧記述より本追記を優先する。実装済みとは扱わない。F4/F5の既決機能・協調切替を維持し、新しい待ち・監視・lock・refcount・期限引数を既存下位APIへ足さない。
照合: O=/home/hight/os32-v3、D=hwdev/gf/display、K=drivers/video/geforce。RECHECK.mdのO/serviceはhwdev/gf/service.c。Lはgf4-a0xからgit showで読んだwt/gf-l4（19f8f911）。N=/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau（c03c1a1）。
対象根拠: O/hwdev/gf/{accel_service.h,service.c,command_test.c,command.c,fifo.c,vram_view.c}、D/quiesce.c、L/D/{save,transaction,modeset}.c。段カードと末尾PM決定を維持する。

### X1 — 予約の所有と現行面

正: F4結線後はctx.scan/scan_next/cursor/testが予約を所有する。F3 serviceのscan/testは値の写しで、nodeの所有権ではない。ctx.accelはproduction.accelの借用を続ける。
追加宣言（hwdev/gf/accel_service.h、実装hwdev/gf/service.c、共有編集担当C2-service-f3）:
```c
i32 gf3_service_surfaces(struct gf3_service *s,
                         const struct gf3_surface *scan,
                         const struct gf3_surface *test);
```
scan/testのNULLはその借用なし。全非NULL面を既存gf3_surface_checkのGF3_USE_BYTESと台帳で検査してからs->scan/testへ一括コピーし、NULL分はゼロにする。surfaceのfmt/pitch/width/height/off/bytes/ID/generationを全て保持し、INDEX8へ作り直さない。登録nodeはコピーしない。
必須sのNULLはGF3_EINVAL=-301、不正service/F4未接続/旧service所有node残存はGF3_ESTATE=-304。面の検査失敗は元のGF3/V0負値を返し、値と公開を変更しない。成功0。この口は予約・待ち・公開を行わず、F3 ready後に呼ぶ。C2が各面の所有者付きmatchと表示用format検査を先に行う。
C3 initializeでF3 bind直後・最初のvideo_start前にowner_bindするため初回所有移管は不要。旧F3所有nodeが残っていればsurfacesはGF3_ESTATEとし、自動停止・移管しない。
通常video startはF1/F2を開始し、C2-scanが新配置をctxの常駐nodeへreserve、下記objects_init→surfaces→既定表示/IRQ成功→最後に公開する。旧「F3 ready→台帳移管」はこの開始列へ置換し、F3を二重initしない。
C2-scanはtestを640×480・GF3_INDEX8、pitch=align_up(640,GF3_TEST_PITCH_ALIGN)、bytes=pitch*480に固定し、表示/cursor予約後の頁整列空き範囲（off+bytes<=ch->B）へ置く。mode変更で拡大しない。旧「残りBまで全域」のservice_reserveをF4経路から呼ばない。試験面不足はGF2_ENOSPCを記録し表示が成立すれば続行、test=NULLでF3診断slotだけ閉じる。ID枯渇等をこの例外に含めない。
C2-scanのscan_changed(owner,head,surface)はB7の確定候補を使う（通知時のstate.head.frontは旧面）。head0でsurfacesとC1の記述子/hookを更新、head1でhead0を変更しない。失敗時は旧借用とholdを保持する。外部所有面をreleaseしない。
mode/rollback/restore後の公開ではC2-linksがchangingならstate.next、そうでなければstate.headを選ぶ。L/transaction.cのadmission(true)はcommit前なのでheadだけを読む実装は禁止。閉鎖中のscan_changedは値だけ更新し再公開しない。
C1は選択面の形式・寸法・pitch・offsetをHALへ反映し、旧8bit hookを16/32bit面へ無条件bindしない。legacy presentの640×480 INDEX8限定は維持。C2-recoverは同一予約を保持して再結線し、初回geometryや旧service_reserveへ戻らない。
F5 restartは再reserveを行わず、保持された面をobjects再生成後にsurfacesで結線する。手本はN/nouveau_bo.c:594–629のpin/unpinとdispnv04/crtc.c:838–903,1136–1217のscan/flip。常駐台帳・F3への値借用はOS32固有の置換。

### X2 — objects再生成、公開、窓closeを分離

正: F5 restartはnode/owner/ID/generation/offを保持し、最終RUNまでF3/F4描画slotとscan hookを閉じる。admission(false)は窓closeではない。
追加宣言（accel_service.h、service.c、C2-service-f3）:
```c
i32 gf3_service_objects_init(struct gf3_service *s, u32 dl);
i32 gf3_service_admission(struct gf3_service *s, bool open);
```
objects_initは既存startと同じRUN・fault・epoch・!ready・ch同一性を検査しgf3_initだけを呼ぶ。reserve/release・geometry取得・slot/hook公開をしない。必須NULL=-301、状態不正/ready済み=-304、下位負rcはそのまま、成功0。部分失敗のobjects/inst所有を保持し、F2 OFF確認前にinvalidateしない。
admission(false)はslot=NULLとscan hook解除だけ、ready/窓/node/面値は保持。admission(true)はRUN・ready・epochと借用面を検査してctx/scan/opsを結線し最後にslotを書き込む。testなしならslotはNULL、scanなしならhookは閉じたまま成功。F4時のhookはC1の形式別adapterを使う。
admissionの必須NULL=-301、状態/古い借用=-304（面検査の元rcは伝播）、成功0。検査失敗は公開しない。公開列に待ち・失敗可能なHW操作を入れず、ops.test_surfaceは常駐s->testを指す。C2-linksは全検査・metadata準備を先に済ませ、F3/F4公開を一つの前景区間で完了する。
既存 `i32 gf3_service_close(struct gf3_service *s);` はadmission(false)→gf_vram_views_closeに分け直すだけで既存契約を保持。F3単体のstart/recoverは従来の予約→objects_init→admission(true)を組み、試験面不足時の表示継続も維持する。
F4側の共用停止・再開口（D/api.h、D/recover.c、C2-recover）を次で凍結する:
```c
i32 gf4_service_suspend(struct gf4_ctx *c, u32 dl);
i32 gf4_service_restart(struct gf4_ctx *c, u32 dl);
```
suspendは受付閉→F4前処理（pending収束/不能時hold保持）→窓close→gf3_service_fini(s,dl)→gf2_stop→OFF確認→invalidate→legacy->owner->foundation_stop(legacy)を直接呼ぶ冪等foundation cleanup（局所restore/quiesce rowsは呼ばない）。suspend は DPMS も表示の restore も書かない (nouveau_display_suspend → nouveau_display_fini(suspend=true)、V/disp.c:56-96)。予約・snapshot・state・相互借用を保持し、通常停止用の予約解放/初期表示への局所復元を行わない。F5は表示保存・画像退避後に呼ぶ。saved_closed時の物理blank/hideは suspend が書かないのでそのまま残る。
restartは閉鎖のままBM先行→必要devinit→F1/F2開始→objects_init→保持面のmatch/surfacesまで。F4 epochを新ch.epochに合わせreadyを復元し、saved_closed/diagnostic_savedは変更しない。再開失敗でも予約と部分生成物を保持する。restart は表示の保存をし直さない (保存は svc_rom の 1 か所だけ。restart の未初期化の枝は、restart が hold_saved → suspend → ready → initialized を要するので到達しない)。理由: 実機 (2026-11-12、S0) で 2 回目の開始の値が前の停止の後の GUI の mode (CR17=c3・VPLL 21a07) だったため。OS32 は 2 回目以降に devinit を走らせないので、nouveau の init・resume の保存し直し (V/disp.c:112-121) を写すと GUI の mode を保存してしまう。
両口は必須NULL/必須結線欠落GF4_EINVAL=-401、不正状態GF4_ESTATE=-408、既存changing競合GF4_EBUSY=-411、下位負値は保存して伝播、成功0。OFFでない停止は元rcまたはGF2_ESTOP。suspend完了はF2 OFFを別途確認し、OFF＋旧fault負rcを成功に丸めない。
F4 recoverはgf4_display_saveを呼ばず、受付閉でstate.head/holdとwanted IRQ値をgf4_savedへ値コピー（version/sizeを設定、node/窓はコピーしない）→suspend→restart→同一予約match→低水準display_restore/IRQ→最終公開。fault中はpendingのfence待ち・drainを省きholdを保持、finiも既存規約でfenceを省く。OFF確認後は旧fault由来のstop負rcを記録して復旧を続ける（suspend自体の負rcは丸めず、close/cleanup失敗・非OFFなら中断）。restoreが立てるsaved_closedは成功後recover自身が入口の値へ戻す。diagnostic_savedとその保存像を上書きせず、A3保存中も同じ値コピーで復旧し、入口のsaved_closed && diagnostic_saved==NULLなら復旧後も非公開、それ以外は最終admission(true)（A3の再公開はC2-saveの管理経路）とする。失敗時は閉鎖・予約保持。F5のlinks.stop/restartは上の2口へ型付きadapterで結線し、既存gf3_service_start/recoverを丸ごと呼ばない。F5の全extent列挙/画像転送は既決C2 adapterに残す。
F5 C2は新hwdev/gf/feature5_links.cに `i32 gf5_link_stop(void *owner, u32 dl); i32 gf5_link_restart(void *owner, u32 dl); i32 gf5_link_admission(void *owner, bool open);` を所有しfeature5.hへ宣言、gf5_linksへbindする。ownerは常駐struct gf5の借用、NULLはGF5_EINVAL=-501、停止/再開はowner->displayへ上記口を委譲し下位負値を保持する。admissionは既決F5 phase/管理slot契約を適用してF4 links.admissionへ委譲する。
C2-saveはL/save.cの低水準restoreが公開もsaved_closed解除もしないことを維持。F5は性能/thermal/tile→VRAM→F4 restore/IRQまで成功後だけsaved_closedを解除し全slot/scanを公開、失敗時は閉鎖/FAULTを保持する。
C2-linksのF4 admission(false)にもF3 admission(false)を結線する（旧「F4 slotだけ」を訂正）。trueは既決のsaved_closed && diagnostic_saved==NULL中はGF4_ESTATE、診断の再公開はC2-save所有、F5の最終公開はF5統括だけが解除後に依頼する。F5 phaseはF4へ持ち込まない。
C3は既存accel/foundation slotを保持してContext版5の通常/復元slotを貸す。変更/保存/復元の処理中は管理slotも閉じ、成功・失敗後の管理口はC2-save/F5の既決寿命に従って再公開する。通常slotが閉じているだけでmodule/予約を解放しない。
手本はN/nvkm/engine/fifo/uchan.c:272–298のblock/unbindとbind/allow、N/nouveau_drm.c:944–1027の表示/objectsの停止再開順。OS32の明示objects再生成・予約ID・module slot公開には一対一のnouveau APIは無い。

### X3 — production診断もF4所有者を通す

正: productionのSTART/STOP/RECOVERをF4の予約保持経路へ、通常video開始/停止をF4の通常開始/停止へ接続する。診断専用chのF2直呼び、revoked時の既存拒否は維持する。
追加宣言（accel_service.h、service.c、C2-service-f3）:
```c
struct gf3_service_owner {
    void *owner;
    i32 (*start)(void *owner, u32 dl);
    i32 (*stop)(void *owner, u32 dl);
    i32 (*recover)(void *owner, u32 dl);
};
i32 gf3_service_owner_bind(struct gf3_service *s,
                           const struct gf3_service_owner *owner);
/* struct gf3_service末尾: const struct gf3_service_owner *owner; */
```
C2-serviceが常駐tableとctxを所有し、C3 initializeで既存gf3_service_bind後・最初のvideo_start前にowner_bindする。owner.stopはgf4_service_suspendへ、owner.startはgf4_service_restart→保持表示の低水準restore/IRQ→最終admission(true)へ、owner.recoverはgf4_service_recoverへ委譲する。STOP前の論理head/hold/IRQ値を保持し、STARTで初期modeへ戻さない。restoreのsaved_closedは成功後に入口値へ戻し、既存保存中の非公開は維持する。gf4_service_start/stopは通常video入口専用。bindはs必須NULL=-301、不完全table=-301、別所有者への差替え/停止未完の解除=-304、同table再bindは無変更0。owner=NULLは完全停止・窓/予約返却後の解除だけ。tableは停止と再STARTを跨いでmodule終了まで借用し、legacy.display4解除と区別する。
既存gf3_service_start/recoverはownerがあれば委譲し、無ければ従来単体列。期限なしgf3_service_stop(s)は単体専用とし、ownerありはGF3_ESTATEで無変更（期限生成・委譲しない）。F4 callbackはこの委譲口へ再帰せず、X2の下位口を呼ぶ。新しいF3 ctxや別の所有状態のコピーは作らない。
C2-service-stopがcommand_test.cを共有編集し、production＋ownerありの3操作はownerの対応callbackへq->deadline_usを渡す（START前のgf2_startもF4側へ移す）。ownerなしは既存F3経路、診断chは既存F2経路。診断STARTは保持済みF4表示とOFFを前提にrestartし、未開始/ready済みはESTATE。通常video_startはgf3_service_startのowner分岐を通さずgf4_service_startを呼び、OFFならF1/F2から、既にRUNならそれを借りる。gf4_service_startは完全停止で解除したlegacy.display4/ctx.legacyを公開前に再結線し、失敗時もcleanup完了までは保持する。
C2-service-stopはD/quiesce.cの旧gf_display_stopにdisplay4ありのF4 stop委譲を結線する。下位cleanupの共用口 `i32 gf_display_quiesce(struct gf_display_context *c);` を同ファイルで分離しinternal.hに宣言する（NULLは既存gf_display_stop同様OS32_ERR_INVAL、既存cleanup失敗の元rc、成功0）。これはfoundation cleanup/局所restore/quiesce rowsだけで、F3/F2停止・再委譲・待ち・予約releaseをしない。
通常gf4_service_stopはF4前処理→close→fini(dl)→F2停止/OFF→invalidate→gf_display_quiesce→表示利用終了確認→C2-scan予約release。成功して解放できたnodeだけ返し、診断snapshot借用中の予約はC2-saveの契約どおり保持する。未完cleanupと借用を残す間は相互接続を解除しない。
F3単体のgf_display_stopも同じ局所helperを利用して現行停止順/戻り値を維持する。C2-recoverのlinks.failedはこの通常停止へ入り元causeを返し、復旧rcを別保存、PC-98選択は既存backend責任。F5はreleaseする通常stopを使わずX2のsuspend/restartを使う。
手本の停止順はN/nouveau_drm.c:944–988の表示停止→fence→object tree。production診断とmoduleの振分けtableはOS32固有で、nouveauに同じ口は無い。

### X4 — 外側dlと既存下位期限の境界

正: 段の中では開始時の絶対 dl を fence・F3 fini/init・表示変更へ同値で渡し、待ちの周回で延長しない。起動・再開では devinit/F1/F2 の後の objects_init、初回 detect、その後の初期表示、restart 後の display_restore を独立した段とし、各段の入口で clock_us→now+GF2_TIMEOUT_US を作り直して c->deadline と揃える。devinit 後の F5 ROM/メモリスクリプト準備も新しい段の期限を使う。期限生成の時計欠落/失敗は GF2_EIO。下位APIの署名は変えない。
使用口は既存 `i32 gf3_service_fini(struct gf3_service *s, u32 dl);`、`int gf2_start(struct gf2_chan *ch, struct gf_foundation *f);`、`int gf2_stop(struct gf2_chan *ch);`、`int gf2_recover(struct gf2_chan *ch);`、`int gf2_fifo_pause(struct gf2_chan *ch);`、`void gf2_fifo_resume(struct gf2_chan *ch);`。
例外はpause内部の2秒だけでなく、F2 start/stop内のreference_testが作るGF2_TIMEOUT_US（recover経由も同じ）。既存HW待ち上限も維持し、外側dlへ書戻さない。したがって操作全体の壁時計時間を外側dl以内と保証する意味ではない。F2 から戻った後の F3 初期化には、その開始時の新しい dl を渡す。段内の期限失敗は従来どおり返す。
C2-service/C2-recoverは上位stop/suspendからgf3_service_stop(s)を呼ばずfini(s,dl)を直接使う。期限なしvideo_stop入口だけclock_us→now+GF3_TIMEOUT_USを一度作ってgf4_service_stopへ渡す。clock欠落/失敗はGF2_EIOを保存、fini fenceは出さず既存規約どおりF2停止cleanupを試みる。時計が読めない gf_display_stop でも、期限の要らない restore (DAC・TV の DACCLK と TV の state、crtc_restore) は下の停止の前に行う。エンコーダー OFF・TMDS の script・フォントの復元は期限か fence が要るので飛ばし、記録する。
command_testのF4 STOPはqの期限を使用。F5 links.stop/restart は gf5.deadline 由来の dl を入口へ渡す。restart 内の F3 初期化と、後の links.display_restore は独立した期限を取り、後者は F5 の性能変更・VRAM コピーの時間も持ち越さない。links.restart 完了後に続く F5 の性能復元も新しい g->deadline を取り、表示復元段では g->deadline と c->deadline を揃える。F5 の性能変更内部の絶対期限は維持する。fini失敗でもF2 stopを試み最初のrcを保存、close失敗かつ未revokedなら停止へ進まず所有保持。F1停止後のVBLANK待ちは禁止。
F3単体stopの既存期限生成を保持。GF2_EIO/ETIME/ESTOP等の下位負値をGF4へ丸めない。resumeは既決のold_rc退避・last_rc/state/fault検査adapterのまま。手本N/nvkm/engine/fifo/nv04.c:212–254にpause/resume列はあるが、OS32の絶対dl・初期reference試験には同じ期限APIは無い。

### 関数 → ファイル → 段 → A1 UNIT への追加・差替え行

新宣言はA0追記として先に凍結し、実装の共有編集は下表の担当へ直列に渡す。C2-service-f3→C2-service-stop→C2-serviceの順に直列分割（各100〜200行を目安、超過時は同ファイル内を口単位で小分け）。C2-recover/scan/links、C3、C1も未発注段の契約追加。統合済みC2-save/C0は公開callback契約への結合確認を追加し、二重実装しない。

| 関数（callbackも完全名） | ファイル | 段 | stub UNIT／結合 |
|---|---|---|---|
| gf3_service_surfaces / objects_init / admission / owner_bind、既存start/stop/recover/closeの分離 | hwdev/gf/service.c、accel_service.h | C2-service-f3（F3共有編集） | —、14で実service/V0結合 |
| gf4_service_suspend / gf4_service_restart / gf4_service_recover / links.failed | D/recover.c、api.h | C2-recover | failedは5,13、他は—/14 |
| gf4_service_start / stop / bind、owner.start/stop/recover adapter | D/service.c | C2-service | —/14 |
| geforce_module_initのgf3_service_bind直後のgf3_service_owner_bind | K/geforce_body.c | C3（tableはC2-service所有） | —/14 |
| gf_display_stop / gf_display_quiesce | D/quiesce.c、internal.h | C2-service-stop（共有編集） | —/14、F4無し既存試験も維持 |
| gf_command_testのproduction START/STOP/RECOVER | hwdev/gf/command_test.c | C2-service-stop、C3は既定wire登録のみ | —/14、診断chの既存試験維持 |
| links.scan_changed / gf4_scan_prepare / discard | D/scan.c | C2-scan | scan_changedは7、他は—/14 |
| links.admission、全slot/scanの閉開 | D/links.c | C2-links、slot貸出しはC3 | admissionは5,13、実公開は14 |
| gf4_display_restore / gf4_diagnostic_restoreの最終公開結線 | D/save.c | C2-save（統合済みへの結合） | —/14 |
| HALのmode/flip metadataと形式別scan hook | K/backend_geforce.c、gf_hook.c | C1 | —/14 |
| gf5_link_stop / gf5_link_restart / gf5_link_admission | hwdev/gf/feature5_links.c（新規、feature5.hに宣言） | F5 C2（F4境界統合後） | F5 UNIT6は既決links stub、実結合はF4 UNIT14 |

A1はUNIT番号を増やさず、既存admission/scan_changed stubを本契約へ追随。UNIT14で実service/V0と結合し、F3単体開始・試験面不足、F4初回開始/16・32bit mode/flip、F5予約キー不変・restart途中非公開、診断STOP→STARTで予約/mode保持、BAD_METHOD→RECOVERとA3保存中RECOVER、video停止→再開始→停止、test固定容量、close/fini失敗・OFF＋負rc・期限据置を正常対照付きで確認する。新口だけの架空失敗eventは足さない。
未決（ユーザー判断）: 0件。静的照合のみ。リポジトリ編集・ビルド・試験は未実施、実装後の結合/実機/容量検証は既存の受入に残す。
反論: 0件。

### 確定 (2026-11-04、Opus 第 2 回の条件付き承認、PM が反映)
- N1 (P1) 通常の video の入口: `gf_start` (hwdev/gf/service.c:248-293) を C2-service-f3 の行に足す。F4 へ回すかの判定は **display4 の 1 つ**にする。display4 は module の終了まで保持し (C2-service の完全停止でも解除しない)、`gf_start` と `gf_display_stop` は、どちらも display4 があれば `gf4_service_start` / `gf4_service_stop` へ回す。`gf3_service_start` は owner があれば GF3_ESTATE を返す (`gf3_service_stop` と同じ扱い。呼び出し元は層 3 単体の構成だけになる)。
- N2 (P2) 保持の置き場: `struct gf4_ctx` に常駐の `struct gf4_saved hold_saved` を 1 つ置き、recover の値の写しと診断の STOP→START の保持が共有する (同時に使う場面は無い、シングルタスク)。持ち主は C2-recover。`state.rollback` は使わない。宣言の追加は C2-recover の段で api.h に足す。
- N3 (P3) suspend の前の値: STOP→START で戻す論理 head・DPMS・hold・IRQ は、suspend の前処理 (pending の収束・cursor hide) より前に `hold_saved` へ取る。
- N4 (P3) test 面の pitch: `GF3_TEST_PITCH_ALIGN` は service.c の static なので D/scan.c から使わない。pitch=640 (64 の倍数) と直接書き、根拠を注記する。
- **C2 の境界の宣言 (C2-service・C2-scan の欠け、PM 決定)**: wt/gf-l4 に 3 つ足した。(1) `const struct gf3_service_owner *gf4_service_owner_table(void)` — C2-service (D/service.c) が常駐の owner の表を持ち、C3 が module の initialize で `gf3_service_owner_bind` に渡す。(2) `i32 gf4_scan_reserve_initial(struct gf4_ctx *, u32 dl)` と `i32 gf4_scan_release_all(struct gf4_ctx *, u32 dl)` — C2-scan (D/scan.c) が持ち、C2-service の start が初回の scan・cursor・test の予約に、完全停止の後に全部の返却に呼ぶ (X1・X3)。`gf4_scan_prepare/discard` は mode 変更の候補だけのまま。(3) `void gf_hook_scan_changed(u8 head, const struct gf3_surface *front, u32 mode_id)` (hwdev/gf/accel.h) — C1 (backend_geforce.c・gf_hook.c) が持ち、C2-scan の links.scan_changed が確定した scan の変更を HAL の記述子と形式別の hook に伝える。head 0 だけを扱う。
- **C 群の宣言 (C3・C1 の欠け、PM 決定)**: C 群の部品の段の前に宣言の段 A0z を置く (PORT_FLOW §3 の追記の後の宣言の段)。
  - HAL: §2 の `struct GfxCursorImage`・`struct GfxScanout` と 8 hook を `include/gfx_hal.h` の `GfxBackend` の末尾に足す (NULL は NOSYS)。
  - 形式の番号: 16/32bit の HAL の形式は公開 SDK (os32_kapi_shared.h・kapi.json) に足さず、カーネル専用の `include/gfx_format.h` に `GFX_FMT_RGB565 2`・`GFX_FMT_XRGB8888 3` を置く (今の GUI は使わないので KAPI と ABI の版は変えない)。SDK に出すときは同じ番号を使うと注記する。`GfxScanout.format` はこの番号。
  - Context の版 5: `geforce_module.h` の Context に display_data・display_state・display_out・feature5_out を末尾に足し、版を 4→5 (§310 の行)。`geforce_display_ops`・`geforce_display_restore_ops` の宣言も同じヘッダーに。
  - production の ctx: C2-service (D/service.c) が常駐の `struct gf4_ctx` を 1 つ持ち、`i32 gf4_service_init(struct gf_production *, struct gf_display_context *legacy)` で ctx・legacy・links (C2-links の `const struct gf4_links *gf4_links_table(void)`) を結ぶ。C3 は module の initialize で `gf3_service_bind` の後にこれ (`gf4_service_init`) を呼び、その後で `gf3_service_owner_bind(…, gf4_service_owner_table())` を呼ぶ (owner 表の getter は owner 未設定なら常駐 ctx を補うため、逆順の bind が必ず EINVAL になるわけではない。この順は、初期化済みの ctx・links・exec へ結線するために守る — 層 5 の照合 D4)。production.h には欄を足さない。
- **links と exec の表、初期の結線の順 (C2-links の欠け、PM 決定)**: C2-links (D/links.c) が常駐の `struct gf4_links` と `struct gf4_exec_ops` を持ち、`struct gf4_links *gf4_links_table(void)` と `const struct gf4_exec_ops *gf4_exec_table(void)` で渡す (wt/gf-l4 0a6d9b9b、A0z の const の宣言を書き換えた — 実装前)。`gf4_service_init` (C2-service) の順: 常駐 ctx を初期化 → ctx->links = gf4_links_table()、ctx->exec = gf4_exec_table() → links->owner = ctx → gf4_service_bind(ctx, legacy, links)。links.failed の実体は C2-recover の `gf4_service_failed(void *owner, i32 cause, u32 dl)` (宣言を足した)。

## 追記 — TMDS の script の実行の口（2026-11-04）

対象は NV44A の内蔵 TMDS。C2-links-b の未凍結だった interpreter 境界を以下で確定する。
N=/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau、V=同 refs/geforce6200/nouveau-v6.1/drivers/gpu/drm/nouveau、O=/home/hight/os32-v3。
N には nouveau_bios.c が無いため、その関数の出典だけ V を使う。実装済みという意味ではない。

### 宣言と持ち主
```c
/* hwdev/gf/internal.h：新段 C2-links-init が層2の共有ファイルを一括所有。 */
enum gf_init_script_rc { GF_SCRIPT_OK=0, GF_SCRIPT_EIO=-1, GF_SCRIPT_ETIME=-5 };
int gf_init_run_script(struct gf_init_context *ctx, struct gf_init_program *work,
                       u32 pc, u8 head, u8 or_mask, u32 dl);
/* kdebug/gfinit_decode.h：既存 decoder の共通本体を使う表示script用入口。 */
int gfinit_decode_script_instruction(const u8 *rom, u32 size, u32 init_table,
                                     u32 pc, struct gfinit_instruction *out);
/* hwdev/gf/display/api.h：純解析、C2-links-b、実体は display/links.c。 */
i32 gf4_tmds_script_select(const u8 *rom, u32 size,
                           const struct gf4_route *route, u32 khz, u32 *pc);
```
- C2-links-init → C2-links-b の順に直列。前者は internal.h、runner.c、kdebug/gfinit_decode.h/.c と gfinit_op/*.inc、hwdev/gf/op/ の必要な拡張・dispatch、devinit.c の借用分岐・hwdev/gf/rom.inc の共通 BIT 探索・対応する試験、docs/design/hwdev/gf/op/tmds_reg.md・zm_tmds_group.md、gen_hwdev_contract による hwdev/generated/* の再生成（layout と STATIC_ASSERT の整合）を所有する。共有ファイルの担当を他段へ分散しない。
- 宣言はこの追記で凍結。api.h の select 宣言も C2-links-init が先に置き、links.c の実装は C2-links-b。PM が既存 internal.h 編集段・B2-b と直列化する。
- ctx に `u8 or_mask; bool display_script; u32 deadline;` を追加。OR は DCB の生 mask、ffs の結果や head 番号へ変換しない。ROM/io は acquire の借用、work はその窓内の既存 scratch を再利用し、常駐 ROM・別 IR・別 interpreter を作らない。

### 単一 script の契約
- run_script は必須 pointer/callback、pc!=0 と ROM 内、saved_count==0 を入口で検査し、不正時 -1。未清算の保存表は所有保持。route 由来の head/OR の妥当性は select 済みを前提とし、再検査しない。
- `display_script=true, deadline=dl, or_mask=引数, head=引数, pc=引数`。calls/repeats/skip、steps/wait_us/elapsed_us、fail_pc/last_rc を当該実行用に初期化し、started_us は acquire が設定した値を保持する。
- runner.c の実行ループを内部共通化する。run_one は従来どおり main_pc[main_id]・head=0 で入り、新入口は指定 PC/head で入る。命令実行による head 変更は既存 opcode の意味に従う。
- run/prepare/run_one の署名、S1 span/CRC、診断承認、main0〜4 の trace・戻り値 0/-1・総予算を変更しない。旧入口では display_script=false とし OR/外側期限を参照しない。
- 新入口の prepare は選択 PC から DONE まで命令長で読み、SUB/SUB_DIRECT の到達先も既存 work に一度ずつ収める。REPEAT は各逐次列内で対応付け、calls を展開せず、SUB 表の値 0 は nouveau と同じく呼ばずに飛ばす。既存容量/深さ/step/wait 上限を共用し、第二の実行計画を作らない。
- 新 decoder は旧 decoder と opcode 本体・命令型を共用。表示用の SUB/CONDITION/IO_FLAG_CONDITION/MACRO の参照表だけ引数 init_table（BIT 'I' のデータ先頭）の +0/+6/+10/+4 から解決し、ROM 境界で検査する。'i'/'I'/'T' の探索は hwdev/gf/rom.inc の共通処理に集約し、'I' の範囲・長さを一度検証して prepare/実行へ渡す。命令ごとの探索はしない。旧 S1 の固定表・SUB 番号制限は旧入口だけに残す。PLL は既存 acquire が検証する既知 ROM の limits/helper を使い、別 ROM 世代の対応を増やさない。
- decoder の新入口は 0/-1。切れた命令/表/不整合 REPEAT、未対応 opcode/表形式、work/stack 容量超過、実行先不正はすべて -1 とし、未知命令を成功として読み飛ばさない。run_script は成功0、期限/実行予算の消費 GF_SCRIPT_ETIME、それ以外 -1。
- 0x4f/0x50 を共通 decoder と既存 dispatch に追加（長さ5 / 3+2*count）。実体はそれぞれ hwdev/gf/op/tmds_reg.c と hwdev/gf/op/zm_tmds_group.c。skip 中は MMIO なし、OR 参照は ctx->or_mask、0x80/0x81 は手本の offset 表と XOR 8、直接 selector 0..3 は同じ base 表を用いる。
- 0x4f は address|0x10000 → data=(old&mask)|value → address、0x50 は組ごと DATA → address。未知 selector は -1。head stride を OR 由来の TMDS base に重ねて足さない。
- 新経路では prepare 中と各命令の前後・待ち chunk の前後に同じ dl を検査（clock_us と `(i32)(now-dl)>=0`）。dl=0 も通常の絶対期限であり無期限ではない。期限は ETIME、既存 io の失敗は EIO=-1、既存実行上限の消費は ETIME。途中で dl を作り直さない。
- gf_init_wait の署名・旧経路の 0/-1 は維持。表示経路の期限理由を ctx->last_rc に保存し、既存 opcode が -1 へ畳んでも新入口はその理由を返す。fail_pc は失敗命令、成功 last_rc=0。
- 実行失敗時は従来の io.restore を一度試み元 rc を保持。cleanup を期限で省略しない。成功時の保存済 index/lock 復元は呼出側が io.restore を一度行う。callback の CR44 選択は saved に載せない生書きとし、呼出し前 owner の復元は B10 末尾に任せる。復元失敗は既存 reusable/停止 cleanup 契約へ渡す。

### 表の選択と C2-links-b の呼出し
- select は MMIO なし、失敗時 *pc 不変。route の head/head_mask・type=TMDS・OR を検査し、不正引数 EINVAL=-401、外付け/未知形式 EUNSUPPORTED=-410、表・選択 PC 不在 ENODEV=-412、ROM 範囲/表破損 ERANGE=-402。成功0。
- BIT directory の 'T' は長さ2、その u16 が指す TMDS 表は version 0x11。ffs(or_mask)==1 は +11、==2/3 は +13 の u16 clock 表を使う。head は選択表を変えず実行へ渡す。
- clock 表は4byteずつ ROM 順に読み、`khz >= u16_threshold*10` を最初に満たす行の u16 PC を選ぶ。0 threshold 行も比較対象、PC=0 は不在。+7/+9 の script は実行しない。pre-BIT/BMP や他世代への fallback は足さない。
- callback は既決の `i32 (*tmds_script)(void *,const struct gf4_route *,u32 khz,u32 dl)`。常駐 exec.owner から links.owner の ctx を取得し、同じ dl で acquire → select → run_script → 復元 → release を完結する。C2 の route 検査は select の一度だけとし、callback に B9 route_check を重ねない。B10 既存の入口検査と外付け排除は維持する。analog/外付けから内蔵 script を呼ばない。
- 既存 devinit.c:136–166 の acquire は起動専用なので、そのまま再呼出しは不可。C2-links-init が `c->initialized` の稼働中分岐を同じ acquire/release に追加する。validate → 同じ scratch の一時借用・ROM 読取/既存検証だけを行い、attempted の起動拒否、route98、master enable、A2、main0〜4 を再実行しない。稼働中の release は expected_pmc に触れず、validate は PMC と稼働中 expected_pmc（mc.c の 0xffffffff）との照合を維持する。未初期化側は従来どおり。
- 稼働中 PROM 読取は起動 A2/RESET 後の PBUS1850=0 が前提。validate 内の gf_production_decode_safe（bus.c:117）がこれを読取前に検査し、非零なら -1、再設定や A2 再実行はしない。
- acquire は署名に dl が無いので前後で外側 dl を検査し、返却後に新入口へそのまま渡す。借用内の既存有限読取上限は維持する（X4 と同じく全壁時計時間を dl 内とは保証しない）。失敗した acquire の部分借用も既存 cleanup/release へ渡す。
- 窓内の順序は SEL_CLK の binding bits 0x50000 保存 → CR44 生書きで対象 head 選択 → run_script →（成功時だけ）io.restore → head bind → SEL_CLK の binding bits だけ復元 → release。bind の dual 判定は khz>=165000。
- 失敗時も保存済 binding を復元して release。run_script 失敗時の io.restore は runner が一度行い、callback は重ねず、run_script 前の失敗でも B10 が CR44 owner を復元する。release は現宣言が void なので架空の戻り値を読まない。cleanup 失敗は既存 reusable/所有状態に記録し、元エラーが無いときは -1、あるときは元値を返す。ROM/PC を指す work は release 後に保持しない。
- callback は run_script の GF_SCRIPT_ETIME→GF4_ETIME、それ以外の実行失敗は -1。select の上記 GF4 エラーは保持し、acquire/bus の失敗は -1、外側期限は GF4_ETIME とする。新口を呼ばず gf_init_run を代用することは禁止。

### 試験の土台・出典と差分
- C2-links-init は gf_runner_host.c/test_gf_runner.py に合成 ROM と実 decoder/runner/0x4f/0x50 を結合。head1・OR別/0x81・SUB/DIRECT・REPEAT・skip・未知/切断・期限と待ち・元rc/restore失敗を正常対照付きで確認。旧 S1 CRC 不一致拒否と main0〜4 の trace と共用 decoder の gfinit-host 系を回帰対象に残す。
- C2-links-b は gf4_c2links_host.c を追加、A1 の既存 UNIT14 に実 select/runner を結合し A2 が依存登録。UNIT10 の B10 用 script stub は維持し、別の結合ケースで B10 と実 callback を接続して khz=165000 とその前後の bind 回数（single 1 回 / dual 各 bank 1 回）を照合する。新 UNIT 番号・ROM_COPY/RELEASE の製品 event は作らない。
- fake の owner/io に窓の貸出し/返却・PC/head/OR/dl 観測、ROM 範囲と clock 閾値の fixture、途中失敗を足す。head0 強制、OR取り違え、閾値比較逆転、期限更新、DATA/address逆転、cleanup脱落の各変異に正常対照を置く。再 acquire で A2/main/PC-98 切替なし、窓外参照なしも照合する。start → TMDS 設定 → release → TMDS 設定で expected_pmc が保たれ、2 回目の validate が通ることを確認する。
- 表選択: V/nouveau_bios.c:66–91,627–680,904–962。head/outp: N/dispnv04/disp.h:175–184、N/dispnv04/dfp.c:446–460。head binding/CR44/SEL_CLK: V/nouveau_bios.c:93–106,673–677、N/dispnv04/dfp.c:68–90。
- 表参照/命令: N/nvkm/subdev/bios/init.c:359–424,539–572,1093–1143,1663–1682,1727–1744。nouveau の outp 全体に代えて必要な OR を値渡しし、head は独立して渡す。
- head の bind は手本どおり (PM 決定 2026-11-11、手本に寄せる): TMDS の script の**後**に bind する (V/nouveau_bios.c:93–105・673–676、F:68–90 の `nv04_dfp_bind_head`、script の前後で SEL_CLK を戻す所も含む)。bind の dual-link は TMDS では `khz >= 165000`、LVDS では `route->dual_link` (V/nouveau_bios.c:120・205・674)。FP の `fp_control` は手本どおり TMDS では `khz > 165000` のまま (F:343–358)。未知 selector は nouveau の無書込み継続と異なり、既存 decoder の未知操作拒否に合わせ -1 で止める。SUB 表の値 0 は手本どおりスキップ。
- 差分理由: OS32 は既存の有限 scratch・命令上限・絶対期限・負rc・一時 ROM 借用を持つため、その契約へ接続する。nouveau に無い再入監視・能力bit・別 interpreter・常駐コピーは追加しない。ROM 境界検査は既存 decoder の責務の延長。
- 未決（ユーザー判断）0件。今回は静的照合のみ。ビルド・試験・実機確認は未実施で、上記は各実装段の受入条件。
- 反論: 0件。

### 確定 (2026-11-04、Opus 第 2 回の条件付き承認、PM が反映)
- N1: 稼働中の分岐では PMC を expected_pmc と照合しない (nouveau にこの照合は無く、0xffffffff の読み戻しは実機で確かめていない — mc.c:21-24 は書いた値を expected にしている)。gf_production_decode_safe (bus.c:117-118) と PFB の検査は残す。試験の「稼働中に TMDS を 2 回続けて設定し、2 回目の validate が通る」は残す。
- N2: C2-links-init の受け持ちに `tools/hwdev_layout.json` の gf_init_context に or_mask・display_script・deadline の 3 欄を足すことを加える (gen_hwdev_contract の再生成と合わせて)。
