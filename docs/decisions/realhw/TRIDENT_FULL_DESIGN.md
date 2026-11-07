# TRIDENT_FULL_DESIGN — Trident ProVidia 9682 の手本の全経路の移植

> 出所: ~/os32-tmp/run/tri-full/DESIGN.final.md (astra 起草 r2、Opus レビュー 2 回、PM が N1・N2・R10 を反映)。
> 状態: **現行 (2026-11-07)** — 段の状態は票 TASK_TRIDENT_DRIVER が持つ。前の段の設計は [TRIDENT_S3_DESIGN](TRIDENT_S3_DESIGN.md)。

## r1 → r2 の変更
- R1: module hook検査とimportの段を追加。
- R2: 加速pitch1024とNoAccel640を書き分け、各層へ反映。
- R3: 案3のVRAM直接描画・アプリ写像・同期presentを採用。
- R4: kdebugからmode/DPMS/cursorを実機受入。
- R5: Mono8x8の8byte配置とCPU uploadを明記。
- R6: HALの空矩形・座標上限guardを明記。
- R7: 水平/垂直線のSOLIDFILLを明記。
- R8: cursor uploadのCPU書込み・近似色・深度別packを明記。
- R9: 動画tickの呼び元と時刻importを追加。
- R10: 既存試験の追従を段へ割当。
- R11: 透明copyのDST_ENABLEを次のSyncまで保持。
- R12: 単精度PLLをfixture化して整数版と照合。
- R13: TRIDENT_PARTSとmodule/FD容量の内訳を訂正。
- R14: DPMS stateをgf4の0/1/2/3へ統一。
2026-11-07。設計のみ。対象は Ra266、PCI/MMIO、単一アプリ・協調切替。9801互換・並行再入・所有者別制御は追加しない。
出典略号: D=`/home/hight/os32-tmp/refs/trident/trident_driver-1.4.0.c`、X=`/home/hight/os32-tmp/refs/trident/xf86-video-trident/src/`。
X13=`/home/hight/os32-tmp/refs/trident/xf86-video-trident-1.3.8/xf86-video-trident-xf86-video-trident-1.3.8/src/`。A=X13/trident_accel.c、R=X13/trident_regs.h、C=X/trident_dac.c、V=X/trident_video.c、P=X/trident_pll.c。O=`drivers/video/trident/`。
以下の矢印は実行順。範囲引用内の9682条件枝を採り、他チップのレジスタを混ぜない。MIT/X11の著作権・許諾・免責全文は移植先各ファイルに保持する。

## 1. 実物の確認
| # | 依頼の想定と実物の食い違い | 設計への反映 |
|---|---|---|
| 1 | HALのset_mode/DPMS/cursor欄は存在する (`include/gfx_hal.h:103-107`) が実機の呼び手は未接続 (`include/video_service.h:61`) | 登録に加えkdebugへgf2型のTrident診断口を作る。公開口はTASK_VIDEO_HALで決める |
| 2 | mono expandはA:196-216,607-670で `#if 0`（手本自身がbuggyと記載） | 9682で実行される機能ではない。文字は既存CPU経路。勝手に有効化しない |
| 3 | パターンはmono8×8が有効だが、color8×8はA:183-194,564-605で `#if 0` | monoだけ移す。カラーはCPU処理 |
| 4 | 任意のHW clip callbackは無い。A:101-111とR:280-287は9682でクリップレジスタを書かず、OPERMODE bit8で無効化する | HALが矩形を切る。別チップのclip手順を9682へ追加しない |
確認一致: O/backend_trident.c:122-130は640×480×8の1モード、O/mode_640.c:165はCR36=00、O/lifecycle.c:106-111にblank/viewport/video resetが既にある。
`TRIDENT_S3_DESIGN.md:79-89` のNoAccel/SWCursor・present byte例外を基点とする。前回REPORTの「欠落」は調査時点の記録で、現行欠落として再計上しない。
追加観測: HAL cursorはARGB PM型 (`include/gfx_hal.h:30-34`)。9682は64×64の2色source/mask型で、直接渡せない。HALの全色対応を申告しない。

## 2. 接続契約・面・共通手順
```c
/* trident.hで先に固定する内部型。公開KAPIではない。 */
struct tri_surface { u32 off, bytes, pitch; u16 width, height; u8 cpp, format; };
struct tri_mode { u32 id, dot_khz; u16 hd,hs,he,ht,vd,vs,ve,vt;
                  u16 display_width; u8 depth,bpp; u32 flags; };
int tri_accel_init(struct trident_device *, const struct tri_mode *);
int tri_sync(struct trident_device *); /* ClearSyncはOPERMODEを書かない別関数 */
int tri_mode_set(struct trident_device *, const struct tri_mode *);
/* HAL既存署名そのまま: tri_hal_fill(int,int,int,int,u8), tri_hal_blit(int×6),
   tri_hal_mode(u32), tri_hal_dpms(u8,u32), cursor_image/move/show。 */
```
- HAL §9: 面作成時だけ形式・pitch・offset・容量を照合、呼出時は世代と矩形clip。表の寸法は画素、pitchはbyteと明示する。GPU命令への座標はoff/pitchを加えたVRAM全体座標。
- 加速時のdisplayWidth（画素）はD:GetAccelPitchValues:1691-1711,3155-3162の512/1024/2048/4096から選ぶ。byte pitchはdisplayWidth×cpp。640×480×8は加速時1024（CR13=128）、NoAccel時640（CR13=80）。D:3263-3276に640のGE符号は無い。bb_pitch・mode.pitch・ledger.pitchを選択枝のbyte pitchに揃える。
- D:3534-3546,3637-3661に合わせ、加速経路は表示VRAMをCLIENTとしてアプリにも写像する（R3決定）。表示/CLIENTは同一描き先、非表示CLIENTとGE commitコピーは作らない。パターン・カーソル・動画はその後方に確保し、2D領域の高さはmin(VRAM/pitch,2047)、残りはlinear領域。
- 2D面は同じcpp/pitchで全行単位に配置。patternは64byte単位(R:419-420)、cursorは1024byte、動画linearは16画素単位(V:588-602)。HALが配置し、呼び手に物理番地を指定させない。
- ledgerはF12で同一所有者/Trident/同一VRAM範囲・形状・UCのDISPLAY/CLIENT対だけ重複を認める。既存mm/pgalloc_surface.inc:63-66は重複を拒否するため変更が必要。leaseはCLIENT、表示参照はDISPLAYとし資源を二重解放しない。mode変更は既存lease回収後に両面を更新し世代を進める。
- 容量はSPR申告・既存の構成値・写像範囲を使う。2MiB実測と4MiB申告を混同せず、全域VERIFYを復活させない。領域不足なら手本のoffscreen確保失敗と同様に該当機能を使わない。
- 現在はRAM CLIENT (`O/resources.c:129-150`)。F6で加速経路をVRAM CLIENTへ変更する。アプリの32bit CPU直接描画が動く前提で、commit前非表示契約はこの経路では外す（R3の明示決定）。NoAccelのモード（packed24を含む）も同じくVRAM CLIENTへ直接描く（GEを使わないだけ。手本のNoAccelはShadowFBではなくfb層がLFBへ直接書く D:3534-3546、ShadowFBはoption時だけ D:2226）。旧RAM CLIENT＋byte presentは、tri-w32の結論で32bit直接書きが不可と決まった場合の起動全体の退避構成に限る（mode単位でbackingを切り替えない）。
- 同期はHAL hookが返る前に手本Sync相当を完了する形を選ぶ。A:141,226-277の「次にCPUが触る前」の待ちを、アプリが戻り直後に直接storeできるOS32ではhook出口に置く。手本の各操作末尾のSync/ClearSyncを保ち、ClearSync済copyはOPERMODEを変えず完了済として返す。CPU uploadの前もSyncで排他する。旧u8 fillはINDEX8のみ登録、新depth primitiveはHで決める。
- presentは加速・NoAccelとも直接描画経路では同期だけ（転送なし、APIは保持）。RAM→VRAMのbyte store（O/present.c:12,56）は上記の退避構成でだけ使う。CPU直接描画やcursor/pattern uploadへ8bit制限を拡張しない。0FACh/68h/6Ah経路切替は従来どおり。
- ScreenInitはD:3472-3748どおりSave→PC98 Init→Mode/Enable→blank→AdjustFrame→面→Accel→Cursor→palette→DPMS登録→Video reset。O/lifecycle.cのclient_initは面初期化とpaletteを分け、この間にAccel/Cursorを入れる。
- EnterはD:1206-1213のMode/Enable→InitializeAccelerator、LeaveはSync→Restore/lock→PC98 Disable、Closeはactive時Sync→PC98 Disable→Restore/lock（D:1234-1250,3333-3349）。EnterへSave/Video resetを追加しない。
- 動画Stop要求は上位の終了処理でV:390-415へ渡す。Save/Restoreに独自のoverlayレジスタ保存列を足さない。Xのfree/unmapはOS32の既存常駐資源寿命へ対応させる。

## 3. 2D加速（accel.c）
| 手本の関数・前提 | 手本の書込み順 → OS32で同順に移す列 |
|---|---|
| D:3199-3211,3263-3276 / A:97-114 InitializeAccelerator | bpp符号(8:0,16:1,32:2) OR pitch符号(512/8192:0,1024:4,2048:8,4096:C) OR100h→OPERMODE。DrawFlagはゼロ初期値。PatternLocation=pitch。24bppはD:3131-3134でNoAccel |
| A:118-181 AccelInit | 初期化→sync/実線/破線/fill/copy/mono pattern登録。NO_PLANEMASKを申告。HW clip/CPU mono expand/color patternは登録しない |
| A:449-475 FillRectSolid | REPLICATE(color)→FMIX=pattern ROP→FCOLOUR→DRAWFLAG=SOLIDFILL\|PATMONO→DIM(w-1,h-1)→DEST→COMMAND=BLT(1)→Sync |
| A:280-324 ScreenToScreenCopy | xdir/ydirからXNEG/YNEG。透明色ありならOPERMODE\|DST_ENABLE→CKEY（複製/比較は原文どおり）→DRAWFLAG=SCR2SCR\|方向→FMIX→負方向終端補正→SRC→DEST→DIM→BLT→ClearSync。DST_ENABLEは次のSyncまで残す |
| A:509-561 Mono8x8Pattern | FCOLOUR→透明bgならBCOLOUR=~fg/flag bit12、他は複製bg→DRAWFLAG=PAT2SCR\|PATMONO\|透過→PATLOC=pattern byte offset>>6→FMIX→DEST→DIM→BLT→Sync。screen origin/MSB first |
| A:328-379 実線 | FMIX→FCOLOUR→DRAWFLAG(SOLIDFILL/STENCIL/方向)→SRC(dmin-dmaj,dmin)→DEST→DIM(dmin+e,len)→BRESLINE(4)→Sync。水平/垂直はA:367のDRAWFLAG=SOLIDFILLからDIM→DEST→BLT→Sync |
| A:383-445 破線 | 長さ2/4/8/16を16bitへ複製→fg/bg/透過→FMIX→phase回転したSTYLE→DRAWFLAG→SRC→DEST→DIM→BRESLINE→Sync。誤差項12bit、NO_PLANEMASK |
| A:226-277 Sync/ClearSync | SyncのみOPERMODEを書き、STATUS bit80hをpoll。count=10000000で通知→9990000へ戻す→8回目にSTATUS=00→return。独自deadlineや永久失敗ラッチを足さない |
| A:101-111 / R:280-287 clip | ソフト状態Clipping=false、9682ではclip窓書込みなし、OPERMODE\|100h。矩形の切取りはHAL |

レジスタ対応（R:147-189,350-361,371-434）: STATUS=2120h/8、OPERMODE=2122h/16、COMMAND=2124h/8、FMIX=2127h/8、DRAWFLAG=2128h/32、FCOLOUR=212Ch/32、BCOLOUR=2130h/32、PATLOC=2134h/16、DEST=2138h/32、SRC=213Ch/32、DIM=2140h/32、STYLE=2144h/32、CKEY=2168h/32。
```c
int tri_fill(struct trident_device *, const struct tri_surface *,
             int x,int y,int w,int h,u32 color,u8 rop);
int tri_copy(struct trident_device *, const struct tri_surface *src,
             const struct tri_surface *dst,int sx,int sy,int dx,int dy,
             int w,int h,u8 rop,int transparent,u32 key);
int tri_pattern(struct trident_device *,const struct tri_surface *,u32 pattern_off,
                int x,int y,int w,int h,u32 fg,i32 bg,u8 rop);
int tri_line(struct trident_device *,const struct tri_surface *,int x,int y,
             int dmaj,int dmin,int error,int len,u8 octant,u16 dash,u8 phase);
```
- copyは重なり方向をHALアダプタで選ぶ。ROPはXAAGetCopyROP/PatternROPの16論理演算のROP3表と等価な小表（copy GXcopy=CC、pattern GXcopy=F0）。手本の未対応planemaskを独自実装しない。
- line/pattern/透明copyの新上位口はTASK_VIDEO_HALへ制約表と上記型を渡す。既存fill/blitはGXcopy/全planes/非透過へ固定。追加口が決まるまでドライバ試験から呼べる。
- MMIOの幅は既存 `O/trident.h:149-152,214-217` で足りる。CPUアップロードとGEの切替はSyncを使う。命令発行でhw_ops、実MMIOでio_accessesを数える。
- HALアダプタはclip後w<=0/h<=0を無発行で返す。off/pitchを含む座標と終端が16bit、y<=2047に収まることを保証し、DIMのw-1/h-1の回り込みを防ぐ。これはXAAの暗黙契約の移植で新たな装置制限ではない。
- `tri_pattern_upload(dev,const u8 rows[8],u32 *off)` は64byte整列の予約領域へSync後に8byteを連続転送する（各行1byte、bit7が左端、上から8行）。A:173-180,509-550/R:419-420のMSB-first/screen-origin/PATLOC=off>>6へ渡す。CPU配置helperはXAA側の役割でA本体に無い。再利用時も先にSyncする。

## 4. HWカーソル（cursor.c）
| 手本・前提 | OS32の手順 |
|---|---|
| C:1143-1214 HWCursorInit | 64×64、予約はsize_bytes=64×4×64=16384からceil(size/pitch)行（画像転送量とは別）→1024byte整列→offset<4MiB-4096の判定→callback登録。確保不能はSW |
| C:1091-1103 SetCursorColors | fgをCR48..4B、bgをCR4C..4Fへ下位byte順。8bppは近似palette index。16bppはRGB555/565へpackしたpixelの下位/上位/0/0、32bppはB/G/R/0。ドライバへ渡す前の色変換はX cursor frameworkに対応するHALの責務 |
| C:1066-1087 SetCursorPosition | x<0ならCR46=-x、x=0、他はCR46=0→yもCR47→CR40/41=x下位8/上位4→CR42/43=y。hotspotはHAL座標から先に引く |
| C:1107-1122 LoadCursorImage | SyncでCPU書込みへ移る→手本のLFB memcpyと同じく1024byte転送（32bit書込み可）→offset/1024をCR44/45。MSB-first、source/mask swap、32bit interleave(C:1199-1201)。予約量とは別 |
| C:1046-1062 Show/Hide | CR50=C1/41。Mode画像のCR50=00とは別。初期化時の独自Hide手順は追加しない |
| C:1126-1135 UseHWCursor | MUXかつ8bppならSW。サイズ/2色に収まらないARGBもSWへ（Xのcursor frameworkに相当） |
```c
int tri_cursor_image(struct trident_device *,const struct GfxCursorImage *);
int tri_cursor_colors(struct trident_device *,u32 bg,u32 fg);
int tri_cursor_move(struct trident_device *,i32 x,i32 y);
int tri_cursor_show(struct trident_device *,bool show);
```
- ARGB PMのalpha=0/255・非透明2色以下をsource/maskへ変換し64×64まで透明padding。半透明/多色はNOSYSでSWへ。8bppはpaletteからRGB距離最小のindexを各色に選ぶ（同距離は先頭）。完全一致を要求しない。手本にARGB変換関数は無く、X cursor framework相当のHAL変換。
- HALではhead=0。画像変換→colors→image、move/showは各要求時に呼ぶ。この組立順はOS32側の呼出契約で、手本にcallback全体の固定順は無い。

## 5. モード・深度・PLL/FIFO（mode_640.cを一般化）
| 手本の枝 | OS32で構築する画像・順 |
|---|---|
| D:1910-1956,2525-2529,2874-2884 / C:572-629 | 8bpp INDEX8、depth15/16を16bpp RGB555/565、depth24はpacked24を既定とする。32bppも手本の選択肢なのでXRGB8888を別モードとして収録。24を32と同一視しない |
| C:573-604 | 8:GR0F\|02,offset=displayWidth/8。16:GR0F\|02,offset=width/4,DAC=10(depth15)/30(depth16),CR38=05。24:GR0F\|02,offset=width*3/8,CR38=29,DAC=D0 |
| C:605-629 | 32:GR0F\|0A、PLL入力clock×2、offset=width/2、CR38=09、DAC=D0。24bppだけNoAccel、32は手本どおり加速可能 |
| D:2179 / P:143-199 / D:952-988 / C:564-568 | clock>90000でMUX、8bppだけ水平display/sync/blank/totalを一度だけ半分→CR38\|01,SR0D\|02,DAC\|20。31kHz候補では通常不成立だがhostで枝を受入 |
| P:42-135 TGUISetClock | CR C0 bit7でPAL/NTSCを初回cache→旧PLL探索k,n,m順（n=0..121,m=1..31,kはclock>50000なら1、他0から1）→差750kHz未満をstrictに更新→a=((m&1)<<7)\|n、b=(m>>1)\|(k<<4) |
| D:661-673 PC98 Init | CR20=38→20、CR30=0E→31=00→32=00→3B=21→43C6=AF→43C7=00を保持。これが9682のFIFO/MCLK列で、別チップの閾値調整を足さない |
| C:253-271,631-740 TridentInit | New選択/GR0F上位保存→CR27/2Bのoverflowとoffset→PLL画像→CR2A,2F,3A,29,GR2F,CR39等の既存読出順。SR20/12は画像を読むが9682 restoreでは書かない |
| C:779-809,883-908 TridentRestore | New→SR0E=C2→hidden DAC→CR1E→21→SR0D→CR50→27→2B→29→36→2F→2A→3A→38→39→55→GR2F→0F→CR13→43C8/9→MISC→SR0E XOR02。CR36はNoAccelなら00、加速なら80 |

- PLL整数化: 基準Hz=14318180/17734480を出発点に、探索順/strict比較を保持。P:48-55,93のfrequencyはfloat（X/trident.h:132）、NTSC=14.31818。hostで手本の単精度格納・式の評価/整数変換を再現して候補全域の(a,b)をfixture化し整数実装と照合。単なる有理数floorとの差は整数で丸めを再現し一致させる（kernelへfloatは入れない）。見つからなければモード失敗。
- D:3057,3088-3100の範囲は9682列で12MHz〜8/16:135MHz、24/32:70MHz（D:272,318,362,406、enumはX/trident.h:294-312）。出力水平31kHz制約とPLLへ渡す倍クロックを別に判定する。
- 初期モード表は下記（640×400/350もPM採用決定）。追加timingはD内蔵値でなくXのmonitor Modes相当のOS32入力。9682のPLL/容量/31kHz条件で生成できる候補のみ登録する。800×600@60/1024×768@60は登録しない。
| 論理画面 | dot MHz | H display/sync start/end/total | V display/sync start/end/total | 極性、水平/垂直 |
|---|---:|---|---|---|
| 640×480（既定） | 25.175 | 640/656/752/800 | 480/490/492/525 | -/-、31.469kHz/59.94Hz |
| 640×400 | 25.175 | 640/656/752/800 | 400/412/414/449 | -/+、31.469kHz/70.09Hz |
| 640×350 | 25.175 | 640/656/752/800 | 350/387/389/449 | +/-、31.469kHz/70.09Hz |
- 各timing×INDEX8/RGB555/RGB565/packed24/XRGB8888を候補にし容量で選別。640画素の加速時displayWidth=1024、byte pitch=1024/2048/4096（8/16/32bpp）。NoAccel時displayWidth=640、pitch=640/1280/1920/2560（8/16/24/32bpp）。packed24は常にNoAccel。既定解像度/深度は640×480×8、加速時のpitchは1024へ変わる。
- 手本のvgaHWInit相当の標準CRTC生成を既存640画像から一般化する。標準CRTCはhorizontalを8画素単位、vertical overflowをCR07/09とC:264-271の拡張へ分配し、sync極性はMISCへ。PC-98 routing列は変更しない。
- set_modeは面割当/PLL画像作成→ModeInit/Enable→選択モード・query・pitch/CLIENT・能力更新。モード切替後のpalette再loadはD:3705-3719。既存palette callbackのdepth8制限を外す際はdepth15/16のDAC表もC:1259-1282に合わせる。
- `include/gfx_hal.h:98-107`へ静的mode表・selected_mode・set_modeを接続。RGB555/packed24の形式IDやCPU描画はTASK_VIDEO_HAL側で追加、ドライバ固有IDを公開ABIへ流さない。

## 6. DPMS（既存dpms.c＋HAL登録）
| 手本 D:1299-1340 / O/dpms.c:52-73 | OS32 |
|---|---|
| active判定→SR0E保存/C2→拡張DAC[4],GR23読出→下位2bitクリア→状態設定→GR23→DAC[4]→SR0E復元 | 既存関数をそのまま呼ぶ。On=(3,0)、Standby=(2,1)、Suspend=(2,2)、Off=(0,3)（DAC,GRの順の値） |
```c
/* backend_trident.c */
int tri_hal_dpms(u8 head,u32 state); /* head0、On=0/Standby=1/Suspend=2/Off=3 (hwdev/gf/display/api.h:18) を検査→既存関数へ変換 */
/* .dpms=tri_hal_dpms。inactiveは手本どおり無操作。登録時に自動Onは送らない。 */
```

## 7. Xv overlay（既存video.cを拡張）
9682はD:3744-3748、V:75-114,378-383の経路、videoFlags=0/keyOffset=50h。広告形式はRV16(RGB565),YV12,YUY2の3種(V:182-204)。分岐だけあるI420/UYVY/RGB15を広告しない。
| 手本の関数・前提 | OS32で同順に移す手順 |
|---|---|
| V:225-300 ResetVideo | VSYNC low→high×2→CR8E=84→DAC[57]=C0→CRBE=26→key50..52/mask54..56。8bppはkey/00/00,FF/00/00、15/16はpacked key下位2byte/maskFFFF、24はBGR/maskFFFFFF |
| V:779-899 PutImage | src/dstを16.16でclip→viewportを減算→dstPitch=ALIGN(width*2,16)→領域確保→top/leftを偶数境界に補正→YV12をYUY2へ変換、RV16/YUY2はpacked copy→変化したclipへ色key塗り→top offset→doublescan補正→FixFrame→DisplayVideo |
| V:568-605 / 907-940 | 16画素整列の再利用→resize→解放/再確保→必要ならキャッシュpurge。QueryImageは最大1024×1024、幅偶数、YV12高さ偶数、Y pitch ALIGN(w,4)、UV pitch ALIGN(w/2,4) |
| V:1186-1276 FixFrame | 初回とcounter周期でGR30読出→shadow条件時のみ切替→CR00/04/05/06/07/10とGR53/52読出→restore→生CR値からHTotal/HSyncStart/VTotal/VSyncStartを原式で復号→hsync=HT-HS+23+7、vsync=VT-VS-2。独自の「補正した標準total」へ置換しない |
| V:609-675 DisplayVideo先頭 | RV16はCR8F=11、YUVは10→CR86/87=x1+hsync→88/89=y1+vsync-2→8A/8B=x2+hsync→8C/8D=y2+vsync+2→CR90/91=width*2→92/93/94=((offset+((x1>>15)&~1))>>3)、94は上位3bit |
| V:688-744 倍率 | 等倍CR80/81=0。拡大q=floor(dst*1024/src)-1024、80=q下位8、81=((q>>10)&15)<<2\|((q>>8)&3)。縮小q=floor(dst*1024/src)、81=80h\|((floor(src/dst)-1)&7)<<2\|((q>>8)&3)。垂直もCR82/83へ同式。手本floatの切捨てを整数化しhost比較 |
| V:765-774 DisplayVideo末尾 | YUVはCR95=width>>2,96=(width+2)>>2、RV16は各>>6→CR8E=94→拡張DAC[00]=95。上位byteをレジスタへ入れる原文の幅を保持 |
| V:453-525 SetPortAttribute | colorKey変更をdepth別にDAC50..52へ→clip無効化。9682にbrightness/hue/saturation/contrastの登録は無い(V:371-375) |
| V:390-415,1153-1178 Stop/Timer | shutdown時はONならVBlank×2→CR8E=84→CR91=00→領域解放。通常stopはOFF_TIMER、800ms後に同じ停止列→FREE_TIMERで60000ms後に解放。比較は原文の期限<現在、定数はV:42-43 |
| V:953-1150 offscreen surface | allocate/query/display/stop/freeを同じ領域管理とDisplayVideoへ接続。StopSurfaceは8E=84→91=00。X adaptor登録だけはHAL能力表へ置換 |
```c
struct tri_video_image { u32 format,width,height,pitch[3],offset[3],bytes; const u8 *data; };
int tri_video_put(struct trident_device *,const struct tri_video_image *,
                  const struct tri_rect *src,const struct tri_rect *dst,u32 key);
int tri_video_stop(struct trident_device *,bool shutdown);
void tri_video_tick(struct trident_device *,u32 now_ms);
```
- ドライバ内の口の案。**上位の口はHALの設計(TASK_VIDEO_HAL)で決める**。Hが面/clip/colorKeyと協調pollを供給する。直接描画経路は色key塗り→Sync→overlay表示、NoAccel経路はbyte present後に表示。Xのregion/adaptor/atomは持ち込まない。
- uploadはCPUアクセス前にSyncし、手本Vのhelperと同じ転送/変換をする（32bit書込み可）。byte制約は残すpresent経路だけ。倍率補正や独自タイムアウトは追加しない。
- V:1144-1182のtimer callbackに対応し、`tri_video_poll()`が新import `int sys_time_now(u32 *lo,u32 *hi)`（kernel/time/ktime.c:56）で64bit単調usを取り整数helperでmsへ変換してtri_video_tickへ渡す。F17診断の協調ループがPut/Stop後もOFF→FREE→解放までpollする。公開後はHのイベントループが同じpollを担当（IRQから呼ばない）。Leave/Closeはpollを外し手本のshutdown Stopで即時停止/解放、timerのためだけにpresentを要求しない。

## 8. I2C/DDCと残りの経路
| 経路と根拠 | 移す/対象外・理由 |
|---|---|
| X/trident_i2c.c:18-31,38-51,58-78 | PutBitsはCR37=0C\|clock*2\|data、GetBitsはCR37読出bit1/0、bus登録。だがD:3759-3763のI2CInit/DDC2呼出は `#if 0`。有効経路ではないのでEDID読出を追加しない |
| D:2504-2529 / C:1218-1242 DDC1 | 9682への識別前にddc1Read関数を設定するが、D内には呼出がない。関数はNew→SR0E保存/bit7→CR37=04→SR0E復元→vsync待ち→CR37 bit0読取。callback定義とEDID取得成功を区別する |
| D:1716のProbeDDC / D:3444-3460 | VBE/int10側。PC98では通らず、I2C無効呼出の代用にならない。EDID必須ゲートを追加しない |
| D:3597-3598 DGA | Xのdirect graphics API登録は対象外。mode/viewport/fill/copy自体は各章へ移す。OS32のleaseが相当し、X DGA ABIは無い |
| D:3600-3635 banking / tvga_dac.c | ISA/!LINEAR・旧TVGAの枝は今回のPCI LFB 9682では通らない。bank関数の追加なし |
| D:3525-3542,3721-3739 ShadowFB/Rotate | RAM shadow転送はpresentで移植済み。画面回転・pointer回転はGUI/合成層の機能としてTASK_VIDEO_HALへ渡す。GPU固有レジスタ操作なし |
| D:3664-3693 / X/trident.h:348-365 | image_accel/xp_accel/blade_accelはIs3D等の別チップ枝。9682へ流用しない。非3D TridentAccelInitをX13から移す |
| C:1245-1282 palette/overscan | paletteは既存維持、overscan callback相当はpalette.cへ追加。8/15/16の既定色管理をHALに返す。X visuals/colormap object登録はOS32の形式・palette表で代替 |
| D:1182-1190 AdjustFrame、3388-3390 SaveScreen | 既存viewportゼロ/blankを維持し、mode寸法一般化時もCR0C/D→1E bit5→27下位3bitの順。panを上位へ出す場合はHALのscanout口で規定 |
| C:724-732 gamma、D:1045-1048 TVInit | gamma回路は>=CYBER9388、TVはTVChipset!=0の条件。今回9682既定構成に無い。HAL gamma=NULL、独自TV検出を足さない |
| D:2051-2073,2179-2195 の設定 | NoAccel/SWCursor・MUXThreshold・Xv位置調整の意味は内部設定に保持。X config解析/ログ/option登録そのものは移さない |

## 9. ファイル別の段・規模
製品ソースは1ファイル=1段=1実装者。下表の共通宣言段を先に確定してから並列化し、他人の担当ファイルを編集しない。共通glueは後段の専任だけが編集する。
| 段 | 所有ファイル（O相対、他は明記） | 責務・依存 |
|---|---|---|
| F0 | trident.h | 上記型/宣言/能力表、mode状態・accel/cursor/video状態。先行固定。presentの古いspan32説明(:260)も訂正 |
| F1 | accel.c（新） | 2D全命令/同期。F0後、F2/F3/F4と並列 |
| F2 | cursor.c（新） | 2色変換/画像/色/位置/表示。F0後 |
| F3 | mode_640.c | 汎用モード画像/PLL/MUX、640互換entryを残す。F0後 |
| F4 | video.c | overlay全経路。F0後 |
| F5 | vram.c | offscreen割当/解放と制約表、容量処理維持。F0後 |
| F6 | resources.c（core kernel） | F5後。加速時RAM確保を外し、表示とCLIENTの同じLFB範囲をVRAM/UCで登録、bb_base/size/pitch更新。NoAccel（packed24含む）も同じVRAM/UC CLIENT。RAM/WB CLIENTは退避構成でだけ確保。F12のalias契約後に結線、lease解除→surface解放→BAR解放順を保持 |
| F7 | present.c | F1/F3/F6後。直接VRAM（加速・NoAccel共通）は同期のみ、退避構成はdepth/cpp・選択pitchを使うbyte転送。TRI_PITCH固定を外しsource/destination pitchを別に使う |
| F8 | palette.c | depth別palette/overscan。F0後 |
| F9 | lifecycle.c | 初期化順/Enter/Leave/Close同期・palette分離。F1〜F8後 |
| F10 | backend_trident.c | F9後。HAL登録/query/caps/mode、選択modeのbb_pitch、u8 fill/blitとCPU復帰前同期、DPMS/cursor接続。診断ops/tickも出力する |
| F11 | build/trident-module.mk | TRIDENT_PARTSへaccel/cursor追加。F1/F2後。resources.cとtrident_module.cはbuild/kernel.mk:39,301のcore側でありmod増分へ算入しない |
| F12 | mm/pgalloc_surface.inc | F0後。上記の同一描き先alias登録を許可し既存の無関係な重複拒否を維持。pitchは可変値。ledger試験段で解放/世代も確認 |
| F12a | gfx/gfx_core_surface.inc | F12後。直接描画のCLIENT/DISPLAY同一範囲と選択pitchをquery/attach/leaseへ通し、NoAccelと他backendの契約を維持 |
| F13 | trident_module.c（core kernel） | F10後。:63-66の任意hook禁止を各hook NULLまたはmodule text内の検査へ変更。:68-77の1mode/選択0/640固定をmode配列範囲・選択index・depth別pitch/容量とbb整合の検査へ置換。必須hook・data境界検査は維持 |
| F14 | tools/prototypes/trident_imports.def | sys_time_nowを追加し時刻importの型を既存GeForce importと揃える。F4のpoll用、公開KAPIは追加しない |
| F15 | tools/module_schema.py | F14後。新import/診断ops契約に必要なschemaを更新し生成器で派生物を再生成（生成物は手編集しない） |
| F16 | trident_module.h | F0後。診断ops/pollのcontext契約と版・サイズを宣言、F13がtext内検査して保持、F17から利用 |
| F17 | kdebug/trident_command_test.c（新） | F13/F16後。gf_command_test.c:42-57型の診断。mode/depth・DPMS・cursor・動画poll・CPU32/GE交互描画を呼び結果を記録 |
| F18 | kdebug/kdebug_main.c | F17後。Trident診断コマンドdispatchを追加。診断のビルド列はF18aで更新 |
| F18a | build/kdebug.mk | F17後。KDEBUG_OBJS (:12-14) へ診断object追加 |
| F18b | kernel/module/kdebug_imports.def | F13後。coreが保持する診断ops取得関数をkdebugへimportし、既存生成器で派生物を再生成。module未load時は診断不可として返す |
| H | TASK_VIDEO_HALの担当ファイルごとに別段 | RGB555/packed24形式、16/32bit primitive、line/pattern/overlay口、SW cursor fallback。F0の型を入力として別設計で確定 |
- dpms.c、state_save/restore.c、pc98_init.c、route_ra266.c、regs.cは既存処理を使う。無関係な再編をしない。試験追加も試験ファイルごとの独立段にし、製品実装者の同時編集を避ける。
- trident.modは現状18.9KiB、GUIパッケージ（起動FDのMINIMALには無い）。増分見積: accel 4〜7KiB、cursor 2〜4KiB、mode/PLL 3〜6KiB、video 6〜10KiB、module内glue 2〜4KiB、計17〜31KiB。未ビルドの概算、画像領域を含めない。
- 起動FD増分はcoreのresources/module検査/ledger/接続分のみ（概算2〜5KiB、未ビルド）。統合時にkernelサイズとFD空き容量を実測して上限内を確かめる。trident.mod増分をFDへ加算しない。

## 10. 受入・試験・変異
hostはMMIO記録fakeで値/幅/順/分岐を比較し、GPU描画は実機で確認する。NP21/WをTrident実装の代用としない。全試験は最終変更後のログだけを合格とする。
| 項目/対象段 | host / 実機 | REDになる一意の変異 |
|---|---|---|
| F1 fill/copy | ROP・1画素・重なり4方向・透明色・8/16/32をhost照合／実機でCPU期待画像と比較 | DIMの-1削除、XNEG反転、ClearSyncへOPERMODE追加を別anchor |
| F1 pattern/line | 8byte upload・64byte整列・MSB/原点/bg透明・8 octant/dashをhost／実機で端と周期 | PATLOC>>6→5、bit順反転、水平線SOLIDFILL削除 |
| F1 clip/同期 | 空/負寸法は無発行、16bit座標/y上限、9682 clip書込みなし、poll/reset/CPU復帰順をhost／実機でGE→CPU32連続描画 | 寸法guard削除、resetを7回へ、hook出口の待ち（=各操作末尾のSync・copyのClearSyncそのもの、別の待ちは足さない）の削除 |
| F2 cursor | 1024転送/16KiB由来予約・負座標・mask・近似index・555/565/32色packをhost／F17診断で四隅/透明/2色/SW復帰 | 予約縮小、CR46符号、CR44 shift、色byte順反転 |
| F3 modes | depth別全画像/PLL両基準/MUX両側をhost／実機で各モード色帯・復帰・pitch端 | 24→32誤選択、PLL同点更新、CR29上位offset欠落 |
| F3/F9 lifecycle | ScreenInit/Enter/Leave/Closeの各列をhost／実機で2回起動と切替 | EnterへSave追加、Close同期位置をRestore後へ |
| F4 video | 3形式/偶数clip/拡縮/等倍/key/位置/停止timerをhost／実機で移動・縮小・画面端・色key一致 | offset>>3→4、9682 hsync+7削除、CR91停止削除 |
| F5/F6/F12 面 | pitch選択・VRAM/UC CLIENT写像・表示alias・世代/解除・容量をhost／実機でCPU32とGE、cursor/video併用 | pitch640固定、WB化、CLIENTを別PFN化、解除漏れ |
| F7 present | 直接VRAM（加速・NoAccel）で転送なし/同期のみ、退避構成の8/16/24/32 byte転送/各行pitchをhost／実機格子と色帯 | 直接経路にコピー追加、NoAccelを32bit転送、pitch固定 |
| F8 palette | depth別DAC値/遅延/overscanをhost／実機で色表とカーソル色 | DACDelayのstatus読出を1回削除 |
| F10/F17 DPMS/HAL | head/state/非active/4状態をhost／kdebug診断からmode・cursor・DPMS On↔省電力を実機確認 | stateのStandby/Suspend交換、診断callback未接続 |
| 対象外枝 | mono expand/color pattern/DDC2の登録・I/Oが無いことをhost確認 | 無効callbackの登録を挿入 |
| 統合 | 全製品実装後make all、追加/変更試験--mutate、checks/run.py --lint／実機で全段一巡。実装中はPORT_FLOWに従いビルドをまとめる | module登録/import漏れをlink/能力試験で検出 |
- 追従試験（`tools/tests/`、各ファイル別の試験段、対応製品段後）: F3=test_trident_mode、F5=test_trident_vram、F6=test_trident_resources、F7=test_trident_present、F8=test_trident_palette、F9=test_trident_lifecycle、F10=test_trident_contracts、F13〜F16=test_trident_module、F6/F12=test_device_reservation（:281-294のtrident本体）。各.pyと同名_host.cは別所有段にし、固定640/単一mode/NULL hook/RAM CLIENT期待と変異anchorを更新する。
- F12/F12aはtest_ledger/test_ledger_surface_mapと対応hostを追従。F1/F2/F4/F17は新規accel/cursor/video/診断試験を別ファイル段にする。既存module/contracts/lifecycleの正常起動・失敗解放・復帰経路を残し、新機能有効時とNoAccelの双方を追加する。
- 順序: F0→F1〜F5/F8並列→F12系列→F6→F7/F9→F10→F13→F17（F16はF0後に先行）。F14→F15はimport生成の直列段。F11と試験ファイル段は所有重複なく実施。Hの公開口確定前もF17診断で受入し、公開機能の完成とは区別する。
- 実装者はbase SHA指定のpreflightとCROSS_DIR/TMPDIR/HOST32_RUNNERS設定を行う。変更anchorを移した既存変異も追従。全体変異/check-changedは実装者では回さず、取り込みはCI check/build、full-checkは既定の運用に従う。

## 11. 未決（ユーザー判断0件）
- R3は2026-11-07の案3で決定済み。32bit直接書込みは動く前提で設計し、実機の原因調査はtri-w32へ分離する。加速経路は直接表示でpresent同期のみ、NoAccelもVRAM直接、byte presentは32bit不可と決まったときの退避構成だけ。
- 640×400/350、8bpp近似カーソル色、kdebug実機診断はPM決定を採用。Hの公開口はTASK_VIDEO_HALの設計事項。mono expand/color pattern/DDC2の無効枝は従来どおり対象外。

## 反論
なし。

## 確定 (2026-11-07、Opus 第 2 回、PM が反映)
- N1: NoAccel (packed24 含む) も VRAM CLIENT に直接描く。RAM CLIENT と byte の present は tri-w32 で 32 ビット書き不可と決まったときの起動全体の退避構成だけ (§2・F6・F7・§10・§11)。
- N2: hook の出口の待ち = 各操作末尾の Sync・ClearSync そのもの、足し増ししない (§10 F1 clip/同期)。
- R10 残り: test_device_reservation を F6/F12 の追従試験に追加 (§10)。

## 追記 (2026-11-07、表示と CLIENT の面の寿命 — astra 起草、Opus 第 1 回、PM が反映。実装中の PM 決定 F6 の 1〜3 を置き換える)

2026-11-07。案3・単一タスク・協調切替を維持。共有 gfx/gfx_core.c は変更しない。
根拠: DESIGN.final.md 末尾 F6 1〜4・F10・tri-w32、tt-life/report.md、tri-fixsel/report.md。
手本 D は確定設計の略号。D:3534-3546 の直接描画、D:3637-3661 の固定仮想画面と後方 offscreen に対応する。

### 決定と時点
「起動時に一度」を「初回 ScreenInit で確定して以後固定」に替える。選択用の暫定面だけをその前に置く。
DISPLAY / CLIENT は常に同一 VRAM 先頭の UC 別名。暫定面も RAM を持たず、内容の読書き・lease 公開をしない。
| 時点 | 容量・既定 mode の知識 | 面の操作 | lease |
|---|---|---|---|
| 起動資源 | mapping/decode 上限のみ、既定 ID は静的既知 | 予約・写像のみ、面なし | なし |
| 初回 prepare | SPR 未知、既定の暫定 pitch/format は HAL 静的記述から既知 | 既定の必要ページだけ両面 create、unready | なし |
| gfx_core 選択 | 同上、加速 pitch は未確定 | CLIENT の format/planes/npages で既定 mode を検査 | なし |
| init/ScreenInit | arm 後 PreInit で SPR・識別、既定 mode の実 pitch が確定 | 全 HAL 候補の最大領域 B を算出し両面を確定 | 初回はなし |
| ScreenInit 完了 | 容量・mode とも確定 | bb・query・mode 表を更新し、成功後に利用可能化 | ここから許可 |
| mode 切替 | 容量 B は既知・固定、要求 mode を検証 | Sync/利用停止・lease revoke 後、両面 regen | 操作中はゼロ |
| Close | 容量 B・最終 mode を保持 | unready・lease revoke、描画停止と既存復帰順、面は保持 | 無効化 |
再 prepare/init は確定面を縮小せず、現在の形を使う。再 ScreenInit は既存の固定 B のまま regen する。
Close は映像復帰であり資源解放ではない。最終 rollback は disarm・lease ゼロを確認して面 release → unmap、確定状態も消す。

### configure の契約（擬似 C）
```c
/* trident.h の callback 宣言を1件変更。core は module 関数を呼ばない。 */
int (*configure)(const struct tri_mode *mode, u32 display_bytes, bool final);
/* resources.c: static bool capacity_final; 内部状態のみ追加 */
prepare: /* I/O なし。初回だけ。declared_bytes を仮値で埋めない */
    C0 = min(mapped_bytes, decode_bytes, nonzero_setting_or_unlimited);
    P = page_up(hal_default_pitch * hal_default_height);
    require(P != 0 && P <= C0);
    configure(&provisional_default, P, false); /* 両面が既存なら何もしない */
ScreenInit: /* PreInit の SPR 読取・識別後、実 mode の検証後 */
    B = tri_vram_display_max_bytes(d);
    require(B != 0 && B <= min(SPR_bytes, C0));
    configure(&prepared_mode, B, true);
set_mode:
    require(mode_bytes <= fixed_B);
    configure(&prepared_mode, fixed_B, true);
```
暫定 mode は既定 HAL 記述の pitch と同じ display_width を持たせる。識別前の tri_mode_prepare/max_bytes は呼ばない。
prepare は resources の ready/regs/lfb/境界/configure を先に検査し、configure 成功後に client と bb を検査・公開する。
configure は format を検査し、display_bytes の整列・境界・pitch×height の収容を検証する。npages=display_bytes/PAGE_SIZE。
未確定→確定では両面 unready、lease_count=0 を必須とし、サイズが異なれば ledger_surface_release（free）+create。
同サイズなら regen でよい。regen に npages 変更を期待しない（mm/pgalloc_surface.inc:107-130）。
両面成功時だけ capacity_final=true と descriptor/SID/gen を公開する。途中失敗は候補を利用不能にし、片面を片付けて再試行可能にする。
作り直し時の SID/世代の連続性は要求しない。lease は未公開なので失効したアプリの参照は存在しない。
確定後は異なる display_bytes を拒否し、同じ値での形変更だけを許す。false の再呼出しは確定状態を戻さない。
B は HAL 対応形式の有効候補について、実際の加速/NoAccel pitch×height の最大をページ切上げした値。
F5 first_off=B と両面 npages を一致させ、候補のページ切上げ後も実容量以内とする。容量・設定は確定後に変更しない。
手本の ScreenInit で固定する仮想画面に相当するのは B。OS32 では mode 群の最大領域を固定し、形だけ切り替える。
選択用 P は OS32 固有の前段で offscreen を持たない。SPR を prepare で読む案や gfx_core の CLIENT 検査を緩める案は採らない。
GeForce の boot 面作成、Cirrus/PEGC の選択前 CLIENT と同じ選択契約を、Trident は暫定 VRAM 面で満たす。

### PM 決定の置換文
1: 「両面は初回 prepare で暫定作成し、初回 ScreenInit で容量を確定する。以後は固定 npages/SID のまま regen し世代を進める」。起動時一度だけ、を削除。
2: 「表示領域は PreInit 後の初回 ScreenInit で B に固定する。offscreen は B 以後、両面の npages は B/PAGE_SIZE」。固定領域の考え方は残す。
3: 「core の configure(mode, bytes, final) に module が暫定 P または確定 B を渡す。boot 資源段では面を作らず prepare で作る」。core/module 分離は残す。
4: 変更なし。INDEX8/RGB565/XRGB8888 のみ。RGB555/packed24 は TRI_E_NOSYS、HAL ID 追加まで延期。F10 overscan 決定も維持。

### 修正する段と検証
F6h drivers/video/trident/trident.h: configure callback に final を追加（宣言変更は1件、公開 KAPI 変更なし）。
F6 drivers/video/trident/resources.c: 暫定/確定状態、容量変更時の release+create、両面失敗処理、rollback の状態消去。
F10 drivers/video/trident/backend_trident.c: 選択前 prepare の暫定作成・再呼出しの無変更、確定容量を渡す mode 切替、bb の更新。
F9 drivers/video/trident/lifecycle.c: PreInit 後の確定 configure、Close の lease 無効化、再 init の固定容量維持。
F5 drivers/video/trident/vram.c: usable に decode 上限も反映し、ページ切上げ後の収容検査、確定 B と first_off の一致を保証。
試験: 実 resources と gfx_core を通す初回選択→init（prepare I/O=0）、再 init、8/16/32bit・NoAccel 切替、SPR で P と B が増減/一致する条件・容量不足・第二 create 失敗・lease 失効・offscreen 非重複を検査する。
変異: prepare configure 削除、final 指定削除、再作成を regen に置換、npages/first_off を現 mode サイズに変更、SPR 制限無視、revoke 削除を各一意 anchor とし、それぞれ正常 GREEN→実行時 RED→復元 GREEN を確認する。
本作業は設計追記のみ。製品・試験は未変更。基点 SHA の指定なしのため preflight、ビルド、実行試験、変異、lint は未実施。

### 確定 (2026-11-07、Opus 第 1 回、PM が反映)
- A1 (P1): Close の後の再 prepare (lease ゼロ・I/O なし) は、両面を既定 mode の形に regen し、device.mode.selected も既定に戻す (前の mode のまま再 init しない)。上の「再 prepare/init は…現在の形を使う」はこの文で置き換える。試験に「16bpp に切り替え → Close → 再選択 → init が Trident で通る」。
- A2 (P2): 大きさが違うときの作り直しは、両面を release してから DISPLAY → CLIENT の順に create する (片面だけ解放すると重なりの検査 pgalloc_surface.inc:75 で落ちる)。
- A3 (P2): 作り直しの後の SID は暫定の面と同じであることを要求し、違えば失敗にする (gfx_core_surface.inc:331-343 の pending の印は SID ごと)。試験で確かめる。
- A4 (P3): F6h で configure の宣言を変えたら、試験の複製も追従する: tools/tests/trident_resources_host.c、test_trident_contracts.py、test_trident_resources.py、test_trim_flow.py。
- 実装中の欠け (PM 決定 2026-11-07): Close (F9) から core の面の unready・lease の revoke は呼ばない。lease の回収と面の再公開は既存の gfx_shutdown (gfx_core) が持つ経路のまま。gfx_return_emergency は緊急の戻りで lease を考えない (シングルタスク、戻った先は CUI)。表の Close の行の「unready・lease revoke」は削る。configure に Close 用の呼び出しも別の callback も足さない。Close の後の再入場は A1 (再 prepare で既定の形に regen) で整える。

## 追記 (2026-11-07、退避の構成を既定に): RAM CLIENT + byte present を Trident の既定にする

ユーザー決定「退避の構成にして調べを続ける」により、§2 N1・§9 F6/F7 の既定を置き換える。
シングルタスク・協調切替。burst・FIFO・ブリッジの調査は HW-BASIC で継続し、修復後は設定だけで案3へ戻す。
根拠: 手本 D=xf86-video-trident 1.4.0 の trident_driver.c:2220-2230 (ShadowFB→NoAccel)、3534-3546 (RAM shadow/直接の分岐)、3721-3739 (refresh)。
byte 転送は実機で映った `5a6d64a2:drivers/video/trident/present.c` の write8 列を使い、固定 TRI_PITCH/寸法は復活させない。

### 描画と切替設定
- 退避時は CLIENT=独立した RAM/WB、DISPLAY=LFB 先頭の VRAM/UC。アプリと CPU fill/blit は CLIENT に描く。
- `trident_resources.ram_fallback` を起動全体の唯一の選択値とし、resources.c の初期設定を true にする。prepare で既存 `device.settings.ram_fallback` へ写し、最初の mode 選択より前に揃える。
- 有効 NoAccel は `settings.no_accel || settings.ram_fallback`。mode_prepare と accel_init の両方で扱い、ScreenInit・Enter・mode 切替で GE を再有効化しない。
- 退避時は GE fill/blit/pattern の初期化・機能広告・HAL hook を無効にする。DISPLAY だけを GE で更新する経路は作らない。既存 HAL の CPU 代替描画→present で CLIENT と表示を一致させる。
- cursor・overlay・palette は今の手順・機能・VRAM 配置を維持する。present の対象は DISPLAY の画素領域だけで、後方の cursor/video 領域へ書かない。
- 案3に戻す操作は resources.c の同じ起動設定を false に変更して再ビルド・再起動するだけ。CLIENT は従来の VRAM/UC alias、present は tri_sync のみ、NoAccel の独立設定は引き続き有効。
- 起動途中や mode 単位では backing を変更しない。RAM 確保失敗時に案3へ自動退避せず、候補を利用不能として既存の選択失敗経路へ返す。

### present の契約
- 退避時は同期成功後、現在の CLIENT の width/height で矩形を安全に clip し、各行 `src=client+y*client.pitch+x*cpp` から `dst=display.off+y*display.pitch+x*cpp` へ `w*cpp` 回 write8 する。
- pitch は選択 mode の `display_width*cpp`。CLIENT/DISPLAY/ledger/bb の形を一致させる。8bit=1、RGB565=2、depth24/bpp32=4 byte を画素変換なしで写す。
- RGB555/packed24 は確定決定4を保ち TRI_E_NOSYS。packed24 を新規公開しない。現行の 3 形式すべてを試験する。
- 行末は CLIENT 有効 bytes と DISPLAY 有効 bytes、および mapped/decode/申告容量内であることを検査する。空矩形は無転送、書込み失敗は直ちに返す。
- 32bit store・memcpy・write_span32 を VRAM 転送に使わない。RAM の通常の CPU 描画は32bitのまま。成功時の present_bytes は clip 後の w*h*cpp。
- ScreenInit の RAM クリア後は全画面を byte present して旧 VRAM の画素を消す。mode 切替成功後・Enter 後も現在の CLIENT 全画面を転送してから利用可能とする。

### 面の寿命（確定 A1〜A4 と Close の PM 決定を維持）
- configure(mode, display_bytes, final) の宣言・呼出し契約は変更しない。display_bytes は両面に確保する共通容量であり、RAM の物理番地は別になる。
- boot は予約・写像と構成値の固定だけ。初回 prepare は I/O なしで P=page_up(既定 pitch*height) を渡し、DISPLAY P byte と RAM CLIENT P byte を暫定作成して unready。lease・画素転送はしない。
- ScreenInit は SPR 確定後、選択した構成の有効 HAL mode 全候補の実 pitch*height の最大をページ切上げした B を渡す。NoAccel pitch を使い、B は SPR/設定/mapped/decode の最小以下。
- DISPLAY.npages=CLIENT.npages=B/PAGE_SIZE、F5 first_off=B。RAM 側も B を固定確保し、mode 切替では npages/PFN/SID を変えず両面 regen・世代更新だけを行う。
- 暫定 P→確定 B が異なるときは lease ゼロ・両面 unready を確認し、両面 release 後 DISPLAY→CLIENT を create。同サイズなら regen。A3 の同一 SID 条件を検査し、不一致は失敗。
- RAM は S3 同様 LEDGER_OWNER_BOOT で連続ページを確保・ゼロ化し、CLIENT は LEDGER_SB_RAM/WB、ポインタは P2V。DISPLAY の DEVICE owner を RAM 確保に使わない。公開後の owner 移譲は既存 ledger の経路を使う。
- 成功時だけ descriptor/client ポインタ/gen/確定フラグを公開。失敗時は候補を unready にし、登録済み RAM は surface_release、未登録の確保だけは元 owner で free して二重解放を避け、再試行可能にする。
- 確定後は異なる B を拒否。Close は面と RAM を保持し、復帰順も維持する。Close に revoke や configure を追加しない。通常の lease 回収・再公開は gfx_shutdown 等の既存 gfx_core 経路が担う。
- Close 後の prepare は A1 通り既定 mode の形へ両面 regen、device.mode.selected も既定へ戻す。B と backing は維持。最終 rollback だけ disarm/lease ゼロ後に面 release→unmap、RAM と確定状態を解放する。
- mm/pgalloc_surface.inc は既存 RAM/WB 検査・release 時 free・regen の容量不変をそのまま使う。案3用 VRAM alias の許可も保持する。共有 gfx_core と公開 KAPI は変更不要。

### 実装の段（各行1ファイル、同じ取り込みで結線）
1. drivers/video/trident/resources.c: 既定設定、RAM CLIENT の P/B 確保・再作成・失敗回収・rollback、案3分岐を保持。
2. drivers/video/trident/backend_trident.c: prepare の設定同期、RAM bb/モード表/GE hook の一致、mode 切替・Enter 完了時の全画面転送。
3. drivers/video/trident/mode_640.c: ram_fallback を有効 NoAccel に含め、NoAccel pitch と機能へ反映。
4. drivers/video/trident/accel.c: ram_fallback 時は初期化を無効にし、GE/pattern を使わせない。
5. drivers/video/trident/present.c: 退避の可変 pitch/cpp byte 転送と案3の同期のみを分岐。
6. drivers/video/trident/lifecycle.c: RAM 初期化後の全画面 byte present、既存 Close/再入場の列を保持。
7. drivers/video/trident/trident.h: ram_fallback の既定・設定同期のコメントだけ追従（宣言変更0件）。
試験: 各試験ファイルを独立段で追従（test_trident_resources/present/mode/lifecycle、GE/HAL の既存 harness）。両構成の選択→init、3形式の端・dirty・非表示領域保持、P/B増減/一致、RAM不足/第二create失敗/SID不一致/解放、16bit→Close→再選択、Enter、GE不使用と案3無転送をhostで検査し、Ra266でGUI・CPU fill/blit・dirty更新・cursor/overlay/paletteの回帰を観測する。
変異: RAMをVRAMへ置換、WB→UC、退避NoAccel判定削除、write8→32bit転送、固定pitch化、cpp乗算削除、初回全画面転送削除、regenで現mode容量化、RAM解放漏れを各一意anchorとし、正常GREEN→実行時RED→復元GREENを確認する。
本作業は追記案だけ。製品・試験未変更、ビルド・実行試験・変異・lintは未実施。宣言の追加・変更は0件。

### 確定 (2026-11-07、Opus 第 1 回の指摘を PM が反映、r2 なし)
- R1 (PM 決定): ram_fallback が有効なときは cursor の像 (cursor.c:190 の trident_write32 の列) と overlay の書き込み (video.c:372,379) も VRAM へは write8 にする。32 ビット書きは案 3 側だけに残す。段 8 = cursor.c、段 9 = video.c を足す。変異に「cursor の像を 32 ビットに戻す」「overlay を 32 ビットに戻す」を足す。
- R2: resources.c に `#ifndef TRI_RAM_FALLBACK_DEFAULT` / `#define TRI_RAM_FALLBACK_DEFAULT 1` を置き、resources.c:276 の固定の代入をこれに替える。host 試験は 0 と 1 の両方で組む (trident_resources_host.c:411 の期待を 2 通りに)。ヘッダの宣言は 0 件のまま。
- R3: 既にある分岐 (backend_trident.c:141-142 の GE hook を外す、:319 の present_bytes、lifecycle.c:60-61 の RAM ゼロ化、video.c:307-315 の color key、mode_640.c:387 の GE 機能を落とす、accel.c:168 の早期 return) は作り直さない。各段は「既にある所を確かめる + 足す差分」だけ。
- R4: 段 4 (accel.c) は変更なし。mode_640.c:215 で ram_fallback のときも NOACCEL を立て、accel.c:168 の既存の return で止める。試験で確かめるだけ。
- R5: settings.ram_fallback へ写すのは prepare の中で `r->configure(&provisional…)` (backend の 202 行目付近) と build_modes・tri_vram_display_max_bytes より前。
- R6: 段 5 で present.c:9 のコメントを「present の中で clip する」に直す (backend_trident.c:315-316 は clip 前の座標で呼ぶ)。
- 確かめ済み (Opus): RAM CLIENT は今の configure・create の重なりの規則・regen・release にそのまま入る。B は 640×480×4 を切り上げた 300 ページ、P は 75 ページで毎回作り直しの経路。GE を止める判断は手本の ShadowFB→NoAccel と合い、手本も HW cursor を残す。
