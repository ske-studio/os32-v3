# GeForce 6200 (NV44A) 層 3 (2D と M2MF) — 確定設計: 2D のオブジェクト・塗り・コピー・1bpp の展開・IFC・M2MF・SIFM・HAL の hook

> 出所: ~/os32-tmp/run/gf-feat3/DESIGN.final.md (astra 起草 r1〜r2、Opus レビュー 2 回、P3 は PM が反映)。
> 状態: **現行 (2026-11-02)** — 段の状態は票 TASK_GF_FEATURES が持つ。層 1・2 は GF_FEAT1_DESIGN・GF_FEAT2_DESIGN。

# NV44A 層3 詳細設計 r2 — 2D / M2MF
r1→r2 の変更: ID別
- ID 1: GDI 0304=1 を CGA6 と訂正、根拠を F:250、変異を1→2へ。
- ID 2: PM決定どおりNVROP表と同じROP値・CPU参照へ変更。
- ID 3: M2MF専用notifier ctxdmaと32Bの所有・寿命を追加。
- ID 4: HALのclip・戻り値をG5に合わせ、scanout描画を明記。
- ID 5: module専用gf_hook.cへadapterを分離、ctx/面の受渡しとhost/GFA追従を追加。
- ID 6: Contextの出力slotで試験opsを公開し、OPS_VERSIONを3へ更新。
- ID 7: monoは128語ずつ詰めて送ると明記。
- ID 8: CHECK・正常対照・GF-L3-Ra266台帳段を追加。
- ID 9: r1の食い違い4件を訂正し、SIFM src_pointのy上位を明記。
対象: OS32 / PC-9821 Ra266、10de:0221、PCI 0:13.0。設計のみ、2026-11-02。

## 1. 実物の確認 (file:line)
- 略号: O=/home/hight/os32-v3、X=/home/hight/os32-tmp/refs/geforce6200/xf86-video-nouveau/src、V=/home/hight/os32-tmp/refs/geforce6200/nouveau-v6.1/drivers/gpu/drm/nouveau、N=/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau (c03c1a1)。A=X/nv_accel_common.c、E=X/nv04_exa.c、F=V/nv04_fbcon.c、H=X/hwdefs/nv01_2d.xml.h、M=X/hwdefs/nv_m2mf.xml.h。
- O/docs/tasks/realhw/TASK_GF_FEATURES.md:14–41: 層3は全2D機能、GUIは別票。O/docs/decisions/realhw/GF_FEATURE_COMPARE.md:40–48を参照。r1と実物/手本の食い違い **4件** (ID 1・2・4・5) を本版で修正。未実装を実装済みとは数えない。
- O/drivers/video/geforce/backend_geforce.c:196–213: PACKED8、関数表にfill_rect/blitなし。O/include/gfx_hal.h:55–56はint戻り値、fillの色はu8。O/gfx/gfx_core_api.inc:65–76はhook呼出しまたはNOSYSのみ (自動CPU fallbackではない)。
- O/drivers/video/geforce/gf_present.c:18–60はCPUのBAR1転送。O/drivers/video/geforce/backend_geforce.c:116–120,160–163から呼ぶ。この経路は変更しない。
- F:143–165は8/16/32bpp、F:197–199はhandle 005fでもclass 009f。A:192はNV44でもSURFACE_2D **0062**、A:547はIFC **008a**。3062/308aを機械的に選ばない (308aはN/nvkm/engine/gr/nv44.c:83–100の一覧にない)。
- 確定F1=O/docs/decisions/realhw/GF_FEAT1_DESIGN.md:42–95、F2=GF_FEAT2_DESIGN.md:53–186,200–226,250以降を境界とする。シングルタスク・協調切替、整数、GNU11、公開SDK C89、kstring、番地/サイズ定数3層、P2V系集約。[C1]〜[D4]/[HW3]を維持。MITの著作権/許諾を移植部分に保持、GPLは写さない。
- 指定関数の数値化に必要なH/MとX/hwdefs/nv_object.xml.h、SIFM実行列には比較§7のX/nv04_xv_blit.c:48–160だけを補助参照。以下の数値は特記以外16進、寸法/行数/予算は10進。

## 2. 共通境界・所有・誤りコード
```c
/* 新 hwdev/gf/accel.h: module内部ABI、command.hをinclude。 */
enum gf3_rc { GF3_OK=0, GF3_EINVAL=-301, GF3_EFORMAT=-302,
  GF3_ERANGE=-303, GF3_ESTATE=-304, GF3_EUNSUPPORTED=-305 };
enum gf3_fmt { GF3_INDEX8, GF3_RGB565, GF3_XRGB8888, GF3_ARGB8888,
  GF3_Y8, GF3_YUY2, GF3_UYVY }; /* INDEX8と輝度Y8は意味を分離 */
struct gf3_surface { u32 off, bytes, pitch, width, height; enum gf3_fmt fmt; };
struct gf3_rect { u32 x,y,w,h; };
struct gf3_ram { void *ptr; u32 bytes,pitch; }; /* kernel RAM、呼出し中有効 */
struct gf3_ctx { struct gf2_chan *ch; struct gf2_object obj[12];
  struct gf2_object notify; u32 notify_off; u32 epoch; int last_rc; bool ready; u32 scratch[1792]; };
int gf3_init(struct gf3_ctx *, struct gf2_chan *, u32 dl);
int gf3_fini(struct gf3_ctx *, u32 dl);
int gf3_surface_set(struct gf3_ctx *, const struct gf3_surface *src,
                    const struct gf3_surface *dst, bool gdi, u32 dl);
int gf3_rop_set(struct gf3_ctx *, u8 subc, u8 alu, u32 mask,
                enum gf3_fmt, u32 dl); /* alu=GX 0..15 */
int gf3_fill(struct gf3_ctx *, const struct gf3_surface *, struct gf3_rect,
             u32 color, u8 alu, u32 mask, u32 dl);
int gf3_copy(struct gf3_ctx *, const struct gf3_surface *src,
             const struct gf3_surface *dst, struct gf3_rect sr,
             u32 dx,u32 dy,u8 alu,u32 mask,u32 dl);
int gf3_mono(struct gf3_ctx *, const struct gf3_surface *, struct gf3_rect,
             const struct gf3_ram *bits,u32 fg,u32 bg,struct gf3_rect clip,u32 dl);
int gf3_ifc(struct gf3_ctx *, const struct gf3_surface *,struct gf3_rect,
            const struct gf3_ram *,u32 dl);
int gf3_upload(struct gf3_ctx *,const struct gf3_surface *,struct gf3_rect,
               const struct gf3_ram *,bool force_m2mf,u32 dl);
int gf3_download(struct gf3_ctx *,const struct gf3_surface *,struct gf3_rect,
                 struct gf3_ram *,u32 dl);
int gf3_m2mf(struct gf3_ctx *,u32 in_handle,u32 out_handle,u32 in_off,
             u32 out_off,u32 in_pitch,u32 out_pitch,u32 rowbytes,u32 rows,u32 dl);
int gf3_scale(struct gf3_ctx *,const struct gf3_surface *src,
              const struct gf3_surface *dst,struct gf3_rect sr,
              struct gf3_rect dr,struct gf3_rect clip,bool bilinear,u32 dl);
```
- productionがgf3_ctxを1個所有。surfaceは所有者が予約済みlinear範囲を貸す (表示面/試験面)。offはVRAM byte offset、PCI offsetはstaging相対。push [B,B+64KiB)・末尾instmem・他所有面へ侵入しない。VRAM一般heapを層3で新設しない。
- 全公開描画/転送APIは同期完了して0。内部surface/ROP設定はemitだけ、最外APIがfence_emit→kick→fence_waitを1回、分割M2MFは各staging再利用前に待つ。dlはF2の操作全体絶対期限 (既定2秒)、途中で更新しない。
- 入り口でready/epoch/RUN/fault、format、寸法・pitch・最終byteを検査。w/h=0は無投入成功、HALは§10で切り詰めてから渡し、gf3 APIの座標は非負で面内、packする各field<=65535。pitch>=width*cpp、2D pitch<=65535、面末尾は減算/除算で桁溢れなく照合。理由: F2はVRAM全体のDMA権限を持ち、描画範囲検査はF2 §4が層3に委譲している。
- F2/F1の負rcは変えず返しlast_rcへ保存、部分投入後の失敗は再送しない。fault/timeoutはF2の停止に従う。CPU fallbackは未投入のEUNSUPPORTED/EFORMATのみ、故障後の部分描画を成功扱いしない。
```c
/* 例: surface object生成はfenceでdrain→fifo_pause済み区間。各rcを即伝播。 */
rc=gf2_gr_object_new(c->ch,0x80000101,0x0062,&c->obj[1]);
if (rc) return rc;
/* 全object登録後fifo_resume。bindと設定はRUNで。 */
h=c->obj[1].handle;
rc=gf2_emit(c->ch,2,0x0000,false,&h,1,dl); if (rc) return rc;
rc=gf2_emit(c->ch,2,0x0300,false,v,4,dl); if (rc) return rc;
rc=gf2_fence_emit(c->ch,&f,dl); if (rc) return rc;
rc=gf2_kick(c->ch); if (rc) return rc;
return gf2_fence_wait(c->ch,f,dl);
/* 試験のreadbackだけ: off,lenを4B境界に包んでmap、byteで比較、最後にunmap。 */
rc=gf2_map_vram(c->ch,aligned_off,aligned_len,&va);
/* reference完了済み、map成功時だけ参照。 */
```

## 3. (a) class・subchannel・初期化列
- handle=80000100+i、i=0 NULL0030、1 SF0062、2 BETA0012、3 BETA40072、4 PAT0044、5 ROP0043、6 GDI004a、7 BLIT009f、8 SIFM3089、9 CLIP0019、10 M2MF0039、11 IFC008a。N/nvkm/engine/gr/nv44.c:83–100に全て存在。
- subc: 0=F2 reference専用、1=M2MF、2=SF、3=GDI、4=BLIT、5=IFC、6=MISC (PAT/ROP/CLIP/BETA/SIFMを毎回bind)、7未使用。X/nv04_accel.h:23–47の0=M2MFだけ1へ移す。理由: F2がsubc0を所有するため。
- 全12個を上の順で生成、全登録成功→resume→下表順に初期化→reference待ち→ready。生成失敗はpause中に生成済みを逆順object_del、resumeしてrc。初期化emit失敗はF2停止へ送り、停止確認前にobjectを解放しない。
- finiは受付停止→最終reference→pause→逆順object_del→resume。F2 recover後は旧epochを無効化し全objectと設定を作り直す。stop失敗時は資源保持。描画のたびにformat/ROP等を設定し、前操作の状態に依存しない。
- 表の `bind` は該当subcの0000=handle。N=上記NULL objectのhandle (数値0ではない)、V=80000002、P=80000003。連続列は4B刻み、全部increment packet。

| 対象class | 手本の列 (file:line・関数) | OS32の列 / 誤りコード |
|---|---|---|
| SF0062 | A:185–209 InitContextSurfaces | bind→0180=N→0184=[V,V]。3062も許可classだがlinear 2Dは手本の0062を使用。GF2_ECLASS等。 |
| BETA0012/0072 | A:213–251 InitContextBeta1/4 | 各bind→0300=7f800000 / ffff0000。整数bit定数。GF2 rc。 |
| PAT0044 | A:313–338 InitImagePattern | bind→0180=N→0304=[1,0,1] (CGA6/8×8/MONO)。色formatとmaskは操作時設定。GF2 rc。 |
| ROP0043 | A:342–360 InitRasterOp | bind→0180=N。ROP値は§4で必ず書く。GF2 rc。 |
| GDI004a | A:364–398 InitRectangle、F:250 accel_init | bind→0180=N→0184=N (fonts)→0198=SF→018c=ROP→0188=PAT→02fc=1→0304=1 (CGA6、値の根拠はF:250のみ。A:389–393のLE=2から変更)。GF2 rc。 |
| BLIT009f | A:402–438 InitImageBlit | bind→0180=N→0184=N→019c=SF→0188=[N,PAT,ROP]→02fc=1→0120=[0,1,2]。GF2 rc。 |
| SIFM3089 | A:442–488 InitScaledImage | bind→0180=[N,V,N,N,BETA1,BETA4,SF]→02fc=0 (DITHER)→0304=3 (SRCCOPY)。GF2 rc。 |
| CLIP0019 | A:492–508 InitClipRectangle | bind→0180=N。0300/0304は使用時に設定。GF2 rc。 |
| M2MF0039 | A:512–528 InitMemFormat | bind(subc1)→0180=8000010c (専用notifier)。GF2 rc。 |
| IFC008a | A:532–578 InitImageFromCpu | bind→0180=N→0188=N→018c=N→0190=N→0194=N→0198=N→019c=SF→02fc=3。GF2 rc。 |
- 数値根拠: H:96–142,199–223,657–689,757–810,890–934,1169–1198、M:96–100、X/hwdefs/nv_object.xml.h:87–160。
- 差分: M2MFだけA:525–526、V/nouveau_bo0039.c:84,113に沿う実notifier。残りの0180はN、完了待ちはreference。0328=0はBUF_NOTIFY=0/WRITE_ONLYであり、NOTIFY割込みなしでも書込み先が必要。
- B0で表示末尾の次の32B境界に32Bを予約 (試験面より先、B未満、重複不可)、mapして0初期化。pause中に `gf2_dma_new(ch,0x8000010c,3,VRAM,WO,off,off+31,&c->notify)` (VRAM/WOはF2の定数alias)。GR生成前に登録、fini/失敗巻戻しはGR削除後notify削除→予約解放、停止失敗時は保持、recover時は再生成。GF2 rcを伝播。

## 4. (b) fill・ROP・planemask
| 手本の列 | OS32の列 (class/method/value、順序) / 誤りコード |
|---|---|
| E:46–84 SetROP | 8/16bitのmask上位を1にする。GXcopy=3かつ全maskなら対象02fc=3。それ以外はPAT bind→0300=format→0310=[0,mask,ffffffff,ffffffff] (全maskは4語全てffffffff)→ROP bind→0300=rop8→対象02fc=1。32bitで非copy/部分maskはEUNSUPPORTED、手本と同じ。 |
| E:118–129 PrepareSolid→E:153–157 Solid、F:64–71 fillrect | ROP設定→SF0062:0300=[sf_fmt,pitch<<16\|pitch,off,off]→GDI004a:0300=gdi_fmt→03fc=color→0400=[x<<16\|y,w<<16\|h]→完了。formatは§9、ERANGE/EFORMAT。 |
- rop8はX/nv_rop.h:303–318のNVROP.copy/copy_planemaskをそのまま16組で持つ (E:63,73)。全mask/部分mask: clear=00/00、copy=cc/ca、xor=66/6a、invert=55/55、set=ff/ff。PM決定によりclear/invert/setはmaskを見ない。CPU参照も同じ表のbit番号=(P<<2)|(S<<1)|Dで評価、8bpp GXclear mask=0x0fは全byte=0を期待する。
- 差分: currentRop cacheを持たず必要状態を毎回emit。理由: MISC再bindとformat切替をまたぐcache無効化を不要にする (性能最適化ではない初版)。
- PAT0300はF:219–231に沿い8/32bit=3、16bit=1、GDIの矩形だけxが上位16bit。PLANEMASK=0でも正規ROPを実行しno-op結果を検証する。

## 5. (c) VRAM copy・重なり
| 手本の列 | OS32の列 / 誤りコード |
|---|---|
| E:170–200 PrepareCopy→255–258 Copy、F:44–47 copyarea | 同format確認→ROP→SF:0300=[fmt,dst_pitch<<16\|src_pitch,src_off,dst_off]→BLIT009f:0300=[sy<<16\|sx,dy<<16\|dx,h<<16\|w]→reference待ち。EFORMAT/ERANGE。 |
- 同一面の重なりも上記1矩形で渡す。手本はdx/dy方向引数を使わず、座標比較によるIMAGE_BLITの方向決定に任せる。逆向きpitchや右下座標への書換えを追加しない。上下左右/斜め/同位置を実機でmemmove相当と照合。
- 異なるdescriptorで同じVRAMにaliasする場合は同一off/pitchの面座標へ正規化する。正規化できない重なりはEUNSUPPORTED (理由: 手本の同一pixmap条件を崩した方向推定をしない)。
- E:227–253の別pixmap大面積コピー分割はVBLANK同期のため。層3ではflip waitを発行しないので省く。非重なり別面コピーにはそのまま1矩形を使う。

## 6. (d) GDI mono / 文字
| 手本の列 | OS32の列 / 誤りコード |
|---|---|
| F:77–114 imageblit、F:246–252 accel_init | depth=1入力→SF/色format設定→GDI02fc=3→0304=1 (CGA6、F:250)→0be4=[clip左上,clip右下,bg,fg,h<<16\|align(w,8),h<<16\|w,y<<16\|x]。clip右下はexclusive、座標はy上位。EFORMAT/ERANGE。 |
| F:114–127 imageblit | dwords=ceil(align(w,8)*h/32)。0c00から最大128語をincrementで送る→残量で繰返し→reference待ち。nonincへ変えない (H:1278–1314)。 |
- 入力はfbconと同じ行ごとのMSB-first byte bitmap、RAM.pitch>=ceil(w/8)。行/byte cursorを保持しscratchへ最大128語ずつ行の有効bytesを詰めて送る (全画面9600語も同じ反復)、最後のdwordの不足byteは0、行末の無効bitはclipで除外。文字列の組版は呼出側、monoは矩形1枚を展開する。
- 差分: 端数をkernel RAMから読み越さずscratchへ詰める。理由: fbconの連続・末尾padding済み入力を任意pitch/bytes付きOS32境界で保証するため。clipは要求clip∩描画矩形∩面、空なら無投入。
- fg/bgはINDEX8ならパレット番号、truecolorならpacked値。透明monoは本票のfbcon相当にはなく、bg付き展開を提供する。

## 7. (e) IFC / RAM↔VRAM M2MF / present試験関数
| 手本の列 | OS32の列 / 誤りコード |
|---|---|
| E:296–319 UploadIFC | RGB565/32bit、rowbytes>=4、h<=1024、align(rowbytes,8)/4<=1792を直接IFC口の条件にする。その他EUNSUPPORTED→一般uploadがM2MFを選択。INDEX8をIFCに偽装しない。 |
| E:326–339→351–370 UploadIFC | CLIP bind→0300=[y<<16\|x,h<<16\|w]→SF設定→IFC008a:02fc=[3,ifc_fmt]→0304=[y<<16\|x,h<<16\|w,h<<16\|iw]→0400からid語incrementを行ごとに送る。iw=align(rowbytes,8)/cpp、id=iw*cpp/4、fmt=1/4。 |
| E:347–357 UploadIFC (ring空き不足時の再開) | 初版は1行ずつPOINT/OUT/INを再設定しh=1で送る。CLIPは全文矩形、各行の末尾をscratchで0埋め、最終列paddingはSIZE_OUTで切る。理由: F2 emitの内部wrapに依存するIFC再開状態を持ち越さない。 |
| E:399–427 RectM2MF | M2MF0039:0184=[in_handle,out_handle]→030c=[in_off,out_off,in_pitch,out_pitch,rowbytes,n,00000101,0]→0100=0 (NOP)→0310=0。n<=2047、off+=pitch*n、残りへ。M:143–171。GF2 rc/ERANGE。 |
| X/nouveau_exa.c:242–288 download、305–371 upload | F2 staging 4096Bを使用。uploadはCPU pack→M2MF P→V→reference完了、downloadはM2MF V→P→reference完了→CPU unpack。PCIのoffsetは0から、VRAMは面off+y*pitch+x*cpp。GF2_ENOSPCならPCI経路失敗を返す。 |
- 行長L<=4096ならn=min(残行,2047,floor(4096/L))、PCI pitch=L、VRAM pitch=面pitch。L>4096なら1行を4096B以下に横分割 (cpp境界)、n=1。各片完了後だけstaging再利用/読出し。rowbytesはpixel数でなくbyte数、一般M2MFはVRAM↔VRAMも同じ列で提供 (重なりはcopy口へ)。
- stagingはgf2_dma_get成功時のch->staging/pci_dma、F2の物理連続1頁、cache同期はF2のDMA mapping契約を継承。CPU copy→compiler/CPU barrier→kick、完了→barrier→CPU read。任意RAMのVAをDMA offset/PAへcastしない。
- M2MFの2047行上限と4KiB容量上限は別条件。L=1なら2047行、L=1024なら4行、L=4097なら4096+1の各1行。RAM.bytesも最後の有効byteまで検査しpaddingを転送しない。
- 一般uploadはX/nouveau_exa.c:319–339の16KiB未満ヒューリスティックでIFC、未対応条件ならM2MF。force_m2mfは試験用。故障後にCPUへ黙って逃がさない理由: 実機のDMA検証結果を失わないため。既存CPU presentは独立して存続。
```c
/* 新 gf_present_m2mf.c: 試験入口専用、GUIのpresent_rect表へ登録しない。 */
int gf3_present_m2mf(struct gf3_ctx *c,const struct gf3_surface *scan,
                    struct gf3_rect r,const struct gf3_ram *ram,u32 dl)
{ return gf3_upload(c,scan,r,ram,true,dl); }
/* ram.ptrは転送矩形の左上。bb全体から呼ぶadapterがy*pitch+x*cppを足しbytesを減らす。 */
```

## 8. (f) SIFM 拡大縮小・色変換
| 手本の列 | OS32の列 / 誤りコード |
|---|---|
| A:442–488 InitScaledImage | class3089、初期化§3。beta1/4は生成・接続済み、SRCCOPYでは合成しない。GF2 rc。 |
| X/nv04_xv_blit.c:74–93 NVPutBlitImage | du=(src_w<<20)/dst_w、dv同様。整数20bit小数、0/桁溢れはERANGE。src_point=((sr.y<<4)<<16)\|(sr.x<<4)、y上位・x下位 (同:81)、各軸12.4固定小数で各16bit内。 |
| 同:99–121 | SF:0300=[dst_fmt,dst_pitch<<16\|dst_pitch,dst_off,dst_off]→MISC bind SIFM→0184=V→0300=[src_fmt,3]。src_fmtはXRGB=4、YUY2=5、UYVY=6。 |
| 同:138–154 | GDI03fc=0→SIFM0308=[clip_point,clip_size,dst_point,dst_size,du,dv]→0400=[src_height<<16\|src_width,src_pitch\|00010000\|filter,src_off,src_point]→reference完了。filter=01000000 bilinear、0 point。 |
- RGB565=7、ARGB=3、Y8=8もH:917–925に従う入力formatとして提供、出力SFは§9。DITHER=0を維持、INDEX8はY8色変換と混同せずSIFM EFORMAT (パレット量子化ではない)。現在の表示以外の16/32bit試験面で色変換を検証。
- sr/dr/clipは面内、src全体サイズとsrの開始点/拡縮量を別に渡す。YUVはpacked 4:2:2、幅・xを偶数にしpitch>=width*2。重なりsrc/dstはEUNSUPPORTED。理由: 読みながら拡縮する順序を手本は保証しない。
- 拡縮率は `((u64)sw<<20)/dw` (高さも同様) で計算し、最終u32範囲を照合する。64bit除算helperが必要ならmodule内実装を予算化、importを増やさない。
- 差分: Xvのregion/BOを面・矩形引数に置換、VRAM入力へ限定してRAM入力はM2MFで先に格納。理由: Xvサービスは票§3の別機能、SIFM転送そのものは全て試験可能にする。

## 9. (g) 色深度・面format
| format (cpp) | SF通常 / GDI用SF / GDI・PAT / IFC / SIFM入力 | 手本 / 誤りコード |
|---|---|---|
| INDEX8 (1) | 1 / 1 / 3 / 不可 / 不可 | A:254–282、F:143–148。palette番号はそのまま、EFORMATは非対応経路のみ。 |
| RGB565 (2) | 4 / 4 / 1 / 1 / 7 | A:266–270、E:106–111、H:924。 |
| XRGB8888 (4) | 6 / 6 / 3 / 4 / 4 | F:154–165、H:260,795,921。X成分は意味なし。 |
| ARGB8888 (4) | a / b / 3 / 4 / 3 | E:97–104のGDI alpha強制ff回避としてY32=bを使う。 |
| Y8 (1) | 1 / 1 / 3 / 不可 / 8 | H:255,925、輝度専用。 |
| YUY2/UYVY (2) | 2D描画の出力は不可 / IFC不可 / SIFM 5/6 | X/nv04_xv_blit.c:83–93、M2MFはbyte列として可。 |
- 今の表示設定/PAL/GfxModeは8bitのまま。16/32bitのVRAM試験面には既に同じAPIを使用できる。層4の表示モード追加後もformat引数/色u32は変えない。任意の15/24bppモード追加はこの票の前提に含めない。

## 10. (h) HAL hook・serial入口
```c
/* 既存GfxBackend ABIをそのまま使う (gfx_hal.h:55–56)。 */
int geforce_fill_rect(int x,int y,int w,int h,u8 color); int geforce_blit(int dx,int dy,int sx,int sy,int w,int h);
/* gf_hook.cに実装、backend_geforce.c:200の表と上記extern宣言を同じ条件で囲む。 */
#ifdef OS32_GEFORCE_MODULE
.fill_rect=geforce_fill_rect, .blit=geforce_blit,
#endif
void gf_hook_ctx_bind(struct gf3_ctx *); /* C1: gf_hook_ctx_bind(&production.accel); */
void gf_hook_scan_bind(bool active, const struct gf3_surface *); /* 面を値コピー */
/* backendのmodule条件内、表示確定時 (BAR1_physは予約BAR1基底): */ struct gf3_surface scan={.off=framebuffer.phys-BAR1_phys,.bytes=framebuffer.pitch*height,.pitch=framebuffer.pitch,.width=width,.height=height,.fmt=GF3_INDEX8}; gf_hook_scan_bind(true,&scan); /* 停止/故障時false、ctxはgf_hook.c内に借用保持。 */
/* kernel試験はこの型のmodule内dispatchを取得する。常駐への新import不要。 */
struct gf3_test_ops { struct gf3_ctx *ctx;
  __typeof__(&gf3_fill) fill; __typeof__(&gf3_copy) copy;
  __typeof__(&gf3_mono) mono; __typeof__(&gf3_ifc) ifc;
  __typeof__(&gf3_upload) upload; __typeof__(&gf3_download) download;
  __typeof__(&gf3_scale) scale; __typeof__(&gf3_present_m2mf) present_m2mf;
  __typeof__(&gf2_map_vram) map; __typeof__(&gf2_unmap_vram) unmap;
}; /* 内部GNU11のみ。A0で各宣言の後に置く。 */
```
- hookは!activeならOS32_ERR_NOSYS、fillはcirrus_clip同様gfx_clip_screenで切り空なら0。blitは負座標→OS32_ERR_INVAL、面外起点→0、w/hを両矩形が収まるまで減算で縮め、非正なら0 (O/drivers/video/cirrus/backend_cirrus.c:403–406,462–504、G5)。その後借用ctx/scanでGXcopy/全maskのgf3_fill/copy、成功0、HW失敗OS32_ERR_IOかつlast_rc保持。描画先はscanoutでありG5のclient面とは異なる (票§3のGUI接続残タスク)。HALのu8色はINDEX8のまま。
- 新upload/download/monoはGfxBackend ABIを広げずgf3 APIと試験dispatchで提供。`GeforceModuleContext`末尾に `const struct gf3_test_ops **accel_out;`、kernelがstaticなslotの番地を渡し、moduleは初期化成功時 `*geforce_context->accel_out=&ops;`、停止/abort前にNULL。ctxはhookと同じproduction.accelを借用、recover中は公開解除し成功後再公開。kernelは呼出しごとslotを読みunload後に保持しない。headerのGEFORCE_MODULE_OPS_VERSIONは2→3、loader/manifest/host期待値も3へ揃え旧版を拒否。
- serial: `gf3 init|fill|copy|mono|ifc|upload|download|scale|present-m2mf|all` に対応するcase_id、開始時PCI ID/BDF/epoch/format/pitch/off、終了時rc/seq/reference/比較byte数/先頭不一致を記録。fault詳細はF2の記録を使用。
- GUI呼出し/新syscall/描画モデル変更なし。hookを利用するkernel試験中はCPU presentと競合させず、試験後は既存RAM BBからCPU presentで復元。BBが正という現契約を変えない (票§3)。

## 11. 部品の大きさ (実装前の予算)
| 部品 | text/rodata概算KiB | RAM/備考 |
|---|---:|---|
| object/面/ROP/fill/copy | 5〜7 | object state <1KiB、inst実体12×32B程度 (F2 allocator整列後) |
| mono/IFC | 3〜4 | 共用scratch 7168Bをgf3_ctx内に1個、stack/VLAなし |
| M2MF/upload/download/present試験adapter | 3〜4 | F2 staging 4KiBを借用、追加DMA pool無し |
| SIFM/format/整数helper | 2〜3 | 大表なし |
| HAL/寿命/dispatch | 1〜2 | kernel試験本体はmod外、試験ops表のみmod内 |
- text等14〜20KiB＋bss等約8KiB、image_maxへbssも保守的に算入: 層1・2後約75KiB→97〜103KiB、128KiBまで25KiB以上。link mapで実測し128KiB超なら段不合格。
- importは追加0、層2後29〜31のまま (上限32)。F2/gf_io/kstringの既存module内境界を利用、serialやDMA関数を部品ごとにimportしない。RAM/VRAM常駐予約とmodule像は別計上 (notifier VRAM 32B、DMA inst 16B、RAMHT 1枠、ctx追加分はbss約8KiB予算内)。

## 12. ファイルごとの変更一覧 (O/相対、1行=1段=1実装者)
| 段 / ファイル | 機能 / 依存 / 行目安 |
|---|---|
| A0 hwdev/gf/accel.h 新 | 上記型/API/handle/subc/定数alias契約 / F2 command.h / 180 |
| A1 tools/tests/gf_accel_host.h 新 | 偽MMIO・push記録・clock・staging・CPU参照画像の試験骨格 / A0 / 160 |
| A2 tools/tests/test_gf_accel.py 新 | 各Bのhost実行/変異登録、登録は直列統合 / A1 / 100 |
| A3 hwdev/gf/production.h 変 | gf3_ctx所有 / A0 / 4 |
| B0 hwdev/gf/accel_objects.c 新 | object開始/終了・bind・ready/epoch / A0,F2 / 180 |
| B1 hwdev/gf/accel_surface.c 新 | format表・面検査・SF設定 / A0 / 130 |
| B2 hwdev/gf/accel_rop.c 新 | PAT/ROP / A0 / 85 |
| B3 hwdev/gf/accel_fill.c 新 | GDI fill / B1,B2 / 55 |
| B4 hwdev/gf/accel_copy.c 新 | IMAGE_BLIT / B1,B2 / 75 |
| B5 hwdev/gf/accel_mono.c 新 | mono packing/clip / B1 / 120 |
| B6 hwdev/gf/accel_ifc.c 新 | IFC行packing / B1 / 130 |
| B7 hwdev/gf/accel_m2mf.c 新 | DMA→DMA矩形 / A0,F2 / 95 |
| B8 hwdev/gf/accel_transfer.c 新 | RAM転送staging分割、upload/download / B6,B7 / 170 |
| B9 hwdev/gf/accel_scale.c 新 | SIFM / B1 / 130 |
| B10 drivers/video/geforce/gf_present_m2mf.c 新 | 試験用present adapter / B8 / 25 |
| C0 drivers/video/geforce/gf_hook.c 新 | module専用adapter・ctx/scan借用口 / B3,B4 / 90 |
| C0a drivers/video/geforce/backend_geforce.c 変 | module条件内の登録・scan/active通知、非moduleはgf3参照なし / C0 / 25 |
| C0b tools/tests/backend_geforce_host.c 変 | 非module/GFAの未解決参照なし、module版はhook stubでG5境界値照合 / C0a / 35 |
| C1 hwdev/gf/service.c 変 | F2 start後gf3_init、recover後再生成 / B0 / 20 |
| C2 hwdev/gf/display/quiesce.c 変 | gf3受付停止→fini→既存F2停止、失敗時所有保持 / B0 / 15 |
| C3 build/geforce-module.mk 変 | 新部品をmoduleだけにlink・GFA別compile確認・image/import検査 / B群 / 20 |
| C4 drivers/video/geforce/geforce_module.c 変 | kernel側slotをContextへ設定・abort時解除、module側公開はC0/C1 / B群 / 25 |
| C5 drivers/video/geforce/geforce_module.h 変 | accel_out欄とOPS_VERSION=3 / A0 / 5 |
| T0 kdebug/gf_accel_test.c 新 | serial case、予約試験面、比較/復元 / C群 / 220 |
| T1 kdebug/gf_command_test.c 変 (F2予定) | 既存GF試験入口からT0をdispatch / T0 / 15 |
| D1 docs/tasks/DEFERRED_TESTS.md 変 | GF-L3-Ra266の未実施行追加、実施後に証跡 / T0 / 1 |
- 各B担当は同時にtools/tests/gf_accel_b<N>_host.cを作る (各40〜80行)。実装1ファイルを実linkし依存はA1 stub、試験登録はA2へ直列。共有Cを複数担当に渡さない。定数の正典/共通定義/alias配置はA0で既存3層に沿って確定。

## 13. 受入 → 試験・変異・段分け
| 受入条件 | host (実部品+偽MMIO/push照合) / 変異 | Ra266 serial / カメラ |
|---|---|---|
| class/寿命 | §3全handle/class/bind順、pause中だけRAMHT変更、recover epoch。変異:3062強制/008a誤記/subc0 M2MF/初期化途中成功扱い | init rc=0、first fault無し、停止再開で全case再実行 |
| fill/ROP | 8/16全16alu×mask 0/全/交互、32copy alpha、SF/GDI別pack。変異:xy交換/ca→cc/mask上位拡張削除/clearの00→0a | offscreen byte比較、8bit色帯/矩形を撮影 |
| copy | 同一面8方向・同位置・異pitch非alias、幅1/端。変異:src/dst offset/pitch交換 | CPU memmove期待像とdownload比較、斜めスクロール撮影 |
| mono | 幅1/7/8/9/31/32/33、129語、非連続pitch、clip端。変異:0304の1→2/noninc/末尾1byte読越し | 非対称文字・fg/bg・端clip撮影、byte照合 |
| IFC | 16/32bit幅奇数、8byte padding、ring wrap、1792語境界。変異:SIZE_OUT=IW/0300順逆 | 強制IFCでreadback一致、8bitは未対応rcを記録 |
| M2MF | L=1/1024/4096/4097、rows=2047/2048、双方向、RAM末尾guard、reference遅延。変異:2048行/byte数をpixel数/PCI offset=PA/notifierをNへ/待ち削除/0310後処理削除 | 強制M2MF upload→download、既知模様とCRC+byte比較、staging guard保持 |
| SIFM | 1:1/2倍/1/2/非整数、clip、RGB/YUV順序・pitch・固定小数。変異:20→16bit/filter逆/offset誤り | 565/32bit offscreen readback、色変換・bilinearは既知単色/端/許容丸めを記録、8bit表示試験と混同しない |
| hook/present/障害 | gf2失敗全段注入、HAL rc、fill(639,479,INT_MAX,INT_MAX)・blit負座標/巨大寸法・!active、無GUI経路、image/import予算。変異:GET=PUTを完了扱い/旧epoch受付 | serialにseq/ref/rc、未完了を成功にしない、CPU presentの従来表示を撮影 |
- A骨格→各B単独host/変異→C結合 (F1/F2実コード+偽MMIO)→T実機。各段は該当変異を落とせて合格、実機未試験を機能削除の根拠にしない。カメラだけでDMA正しさを判定せずserialのbyte比較と組にする。
- A2は `CHECK=dict(name="gf-accel-host",tier=1,needs=[],budget=90.0,covers=["hwdev/gf/accel*","drivers/video/geforce/*","tools/tests/gf_accel*","tools/tests/test_gf_accel.py"])`。未着手Bは未登録、登録済みBの欠落は失敗。各変異は無変更GREEN→1箇所変更で実行時RED→復元GREEN、コンパイル失敗は検出に数えない。
- D1の台帳行: `GF-L3-Ra266 | 未実施 | Ra266 0:13.0/0221 | 2D全case・PCI双方向・M2MF notifier・recover・CPU present維持 | 実施担当/日付/serialログ/カメラを実施時記入`。host合格で実機済みにしない。
- 試験面はnotifier予約末尾の次の64B境界からB未満で所有予約、pitchは64B整列。容量不足はGF2_ENOSPC、表示面・pushを流用しない。CPU期待像とVRAM readbackはreference後のgf2_map_vramで独立比較し、M2MF同士の往復だけに依存しない。

## 14. 未決 (ユーザー判断が必要なものだけ)
- **0件**。上記は既決スコープ内の実装選択。HW上の成否/性能/丸めは実装後の試験項目であり、事前の承認条件にしない。

## 層 1・2 への送り
- 仕様変更要求なし。F2は層3objectの登録/削除用に既定のfence・fifo_pause/resume・gr_object_new/object_delをそのまま供給する。stagingのCPU cache属性/同期契約はF2 DMA adapter実装時に明文化し、層3で別mappingを作らない。
反論: 0件。

## 確定 (2026-11-02、Opus 第 2 回 Approve、PM が反映)
- 指摘 10 (P3、PM 決定: 手本に合わせる): M2MF の notifier の ctxdma は、手本 (nouveau-v6.1 nouveau_drm.c:408–422) どおり `gf_inst_alloc(32)` の instmem に class 0x3d (NV_DMA_IN_MEMORY)・RDWR・VRAM で作る。§3 の 101 行付近の「表示の後ろの VRAM に 32B を予約し、class 3・WO」は**これで置き換える**。表示の後ろの予約は要らなくなり、§13 の試験面の起点は notifier の予約より前の元の位置に戻す。
