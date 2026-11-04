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
- ID 6: 当時はContextの出力slotで試験opsを公開し、OPS_VERSIONを3へ更新。現行は既存版4とslotを維持する。
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
  struct gf_inst notify_storage; struct gf2_object notify; u32 notify_off; u32 epoch; int last_rc; bool ready; u32 scratch[1792]; };
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
- 全公開描画/転送APIは同期完了して0。内部surface/ROP設定はemitだけ、最外APIがfence_emit（内部でkick済み）→fence_waitを1回、分割M2MFは各staging再利用前に待つ。dlはfenceと描画の操作全体絶対期限 (既定2秒)、途中で更新しない。FIFO pauseはF2独自の2秒を使い、期限付きpause口は追加しない。resume後はadapterがlast_rc/stateを照合してrcを返す。
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
return gf2_fence_wait(c->ch,f,dl);
/* 試験のreadbackだけ: off,lenを4B境界に包んでmap、byteで比較、最後にunmap。 */
/* vram_view.hで凍結する独立した窓の型付きmap口を使用する。 */
rc=gf_vram_view_map(&view,c->ch,aligned_off,aligned_len,&va);
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
- B0で別所有の`notify_storage`へinstmem 32Bを確保し、零化しない。VRAM絶対off=`vram_bytes-reserved+notify_storage.off`を求め、pause中に `gf2_dma_new(ch,0x8000010c,0x003d,VRAM,RW,off,off+31,&c->notify)` で別の16B ctxdmaを作る (VRAM/RWはF2の定数alias)。GR生成前に登録、fini/失敗巻戻しはGR削除→ctxdma削除→storage解放。停止失敗時は保持しrecover時は再生成、instを値コピーしない。GF2 rcを伝播。

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
- stagingは起動時に確保済みのstaging_liveとch->staging/pci_dmaを借用し、毎回gf2_dma_getしない。確保失敗でもVRAM専用RUNは有効、M2MF PCI経路だけENOSPC。解放はF2だけ。F2の物理連続1頁、cache同期はF2のDMA mapping契約を継承。CPU copy→compiler/CPU barrier→kick、完了→barrier→CPU read。任意RAMのVAをDMA offset/PAへcastしない。
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
- 拡縮率は `((u64)sw<<20)/dw` (高さも同様) で計算し、最終u32範囲を照合する。64bit除算helperが必要ならmodule内実装を予算化する。
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
  __typeof__(&gf_vram_view_map) map; __typeof__(&gf_vram_view_unmap) unmap;
}; /* 内部GNU11のみ。A0で各宣言の後に置く。 */
```
- hookは!activeならOS32_ERR_NOSYS、fillはcirrus_clip同様gfx_clip_screenで切り空なら0。blitは負座標→OS32_ERR_INVAL、面外起点→0、w/hを両矩形が収まるまで減算で縮め、非正なら0 (O/drivers/video/cirrus/backend_cirrus.c:403–406,462–504、G5)。その後借用ctx/scanでGXcopy/全maskのgf3_fill/copy、成功0、HW失敗OS32_ERR_IOかつlast_rc保持。描画先はscanoutでありG5のclient面とは異なる (票§3のGUI接続残タスク)。HALのu8色はINDEX8のまま。
- 新upload/download/monoはGfxBackend ABIを広げずgf3 APIと試験dispatchで提供。`GeforceModuleContext`の既存 `const struct gf3_test_ops **accel_out;` とfoundation_outを保持し、kernelがstaticなslotの番地を渡し、moduleは初期化成功時 `*geforce_context->accel_out=&ops;`、停止/abort前にNULL。ctxはhookと同じproduction.accelを借用、recover中は公開解除し成功後再公開。kernelは呼出しごとslotを読みunload後に保持しない。GEFORCE_MODULE_OPS_VERSIONは既存版4を維持し、F3では版を上げない。F4 C3の版5宣言は共有登録順に従う。
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
- text/rodata14〜20KiB＋BSS等8KiB＝mem増分22〜28KiB。V0の窓/台帳と整数helperも内数とする。imageとmemは分離して実測し、全層の判定は末尾の[合算予算](#合算予算)に従う。
- F2/gf_io/kstringの既存の型付き口を優先し、必要importは生成元へ追加する。本数上限を理由にopsへ集約しない。RAM/VRAM予約とmodule像は別計上 (notifier storage inst32B、ctxdma16B、RAMHT 1枠、ctx追加分はBSS約8KiB予算内)。

## 12. ファイルごとの変更一覧 (O/相対、1行=1段=1実装者)
| 段 / ファイル | 機能 / 依存 / 行目安 |
|---|---|
| A0 hwdev/gf/accel.h 新、hwdev/gf/vram_view.h 新 (宣言のみ) | 上記型/API/handle/subc/定数alias・窓/面台帳の型と列挙契約 / F2 command.h / 180 |
| A1 tools/tests/gf_accel_host.h 新 | 偽MMIO・push記録・clock・staging・CPU参照画像の試験骨格 / A0 / 160 |
| A2 tools/tests/test_gf_accel.py 新 | 各Bのhost実行/変異登録、登録は直列統合 / A1 / 100 |
| A3 hwdev/gf/production.h 変 | gf3_ctx所有 / A0 / 4 |
| V0 hwdev/gf/vram_view.c 新 | 独立VRAM窓/面台帳実装・対応host試験 / A0の凍結済み宣言 / 行数は実装時報告 |
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
| C3 build/geforce-module.mk 変 | 新部品をmoduleだけにlink・GFA別compile確認・image検査 / B群 / 20 |
| C4 drivers/video/geforce/geforce_module.c 変 | kernel側slotをContextへ設定・abort時解除、module側公開はC0/C1 / B群 / 25 |
| C5 drivers/video/geforce/geforce_module.h 確認のみ | 既存accel_out/foundation_out・版4を維持、欄の追加と版上げなし / A0 / 0 |
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
| hook/present/障害 | gf2失敗全段注入、HAL rc、fill(639,479,INT_MAX,INT_MAX)・blit負座標/巨大寸法・!active、無GUI経路、image予算。変異:GET=PUTを完了扱い/旧epoch受付 | serialにseq/ref/rc、未完了を成功にしない、CPU presentの従来表示を撮影 |
- A骨格→各B単独host/変異→C結合 (F1/F2実コード+偽MMIO)→T実機。各段で試験コードを完成させ、実行は全実装後にPMがまとめる。該当変異を実行時REDにして合格とし、実機未試験を機能削除の根拠にしない。カメラだけでDMA正しさを判定せずserialのbyte比較と組にする。
- A2は `CHECK=dict(name="gf-accel-host",tier=1,needs=[],budget=90.0,covers=["hwdev/gf/accel*","drivers/video/geforce/*","tools/tests/gf_accel*","tools/tests/test_gf_accel.py"])`。未着手Bは未登録、登録済みBの欠落は失敗。各変異は無変更GREEN→1箇所変更で実行時RED→復元GREEN、コンパイル失敗は検出に数えない。
- D1の台帳行: `GF-L3-Ra266 | 未実施 | Ra266 0:13.0/0221 | 2D全case・PCI双方向・M2MF notifier・recover・CPU present維持 | 実施担当/日付/serialログ/カメラを実施時記入`。host合格で実機済みにしない。
- 試験面は表示予約末尾の次の64B境界からB未満に台帳で所有予約し、pitchは64B整列。notifierは末尾instmem所有なので試験面起点から除く。容量不足はGF2_ENOSPC、表示面・pushを流用しない。CPU期待像とVRAM readbackはreference完了後の独立VRAM窓で比較し、M2MF同士の往復だけに依存しない。

## 14. 未決 (ユーザー判断が必要なものだけ)
- **0件**。上記は既決スコープ内の実装選択。HW上の成否/性能/丸めは実装後の試験項目であり、事前の承認条件にしない。

## 層 1・2 への送り
- 仕様変更要求なし。F2は層3objectの登録/削除用に既定のfence・fifo_pause/resume・gr_object_new/object_delをそのまま供給する。stagingのCPU cache属性/同期契約はF2 DMA adapter実装時に明文化し、層3で別mappingを作らない。
反論: 0件。

## 確定 (2026-11-02、Opus 第 2 回 Approve、PM が反映)
- 指摘10 (P3、PM決定): notifierはinstmem32Bと別ctxdma16B/class003d/RWで所有する。確定内容は§2・§3・§13の本文に反映済み。

## 層 1 の追記との関係 (2026-11-02、PM)
- 当時、GeforceModuleContextのaccel_outとOPS_VERSION 3は層1の確定設計で先に確保された。現行はfoundation_outを含む版4で、層3のC5は確認のみ0行とし、二重追加・版上げを行わない。

## 追記 (2026-11-04、段に出す前の点検)

| 境目／持ち主 | 宣言段で凍結する追記 |
|---|---|
| 起動・停止／A3・C1・C2 | `production.accel`は移動不可の常駐所有。現行`service.c:25–42`はdevinit→表示設定→F1→F2。F2 RUN後にgf3_init、成功後だけhook/accel_outを公開。通常停止は受付・slot閉→gf3_fini→既存gf2_stop。FAULT時はfiniのfenceを無理に発行せずF2停止へ渡し、停止成功後に上位記述子を無効化する。 |
| 復旧／C1 | `command.c:43–72,159–169`のstopは層3を含む可変instを解放し、recoverはF1/F2だけを再構成する。上位所有者がslot閉→gf2_recover→旧gf3記述子無効化→gf3_init→scan再bind→公開を直列実行。失敗時は公開せず、解放済みobjectを再度object_delしない。 |
| notifier／A0・B0 | 既決P3どおり`struct gf_inst notify_storage`をgf3_ctxへ追加し32B確保（零化しない）、`notify`は別の16B ctxdma。差分: 手本のPRAMIN相対off（N/nvkm/subdev/instmem/nv40.c:89）をVRAM絶対off=`vram_bytes-reserved+notify_storage.off`へ変換。理由: 相対offのままVRAM宛先にすると0x40000以上のinstが表示面（0x4B000）と重なる。N/nouveau_drm.c:429のzero=falseを維持、class003d/RW、limit=off+31。GR削除→ctxdma削除→storage解放。instを値コピーしない。現行`dmaobj.c:31`・`instmem.c:40`に合わせる。 |
| CPUのVRAM窓／A0＋追加V0 | `command_platform.c:5–43`はpush専用でRUN中の追加mapを拒否する。新`hwdev/gf/vram_view.h/.c`に独立したextent/va/bytesを所有する型付きmap/unmap口を置く。既存map_ops/map_ownerを借用し、ch->va/mapped/push_extentを変更しない。試験readback、F4 cursor、F5退避が共用し、予約照合・完了後アクセス・unmap失敗時保持を引き継ぐ。F3試験opsのmap/unmap型もこの口へ変更。 |
| 絶対期限／A0 | **PM 決定（4）**: 手本N/nvkm/engine/fifo/nv04.c:233と現行fifo.c:216どおり、pauseはF2独自の2秒、fenceと描画はdl。期限付きpause追加A0a/B12は削除。fence_emit内でkick済みなので二重kickを削除。resumeはvoidでlast_rcを残すため、上位adapterがlast_rc/stateを照合しrcを返す。 |
| DMA staging／B8 | 起動時に確保済みの`staging_live`とpci_dmaを借用し、毎回dma_getしない。確保失敗のVRAM専用RUNは有効、M2MF PCI経路だけENOSPC。stagingの解放はF2だけ。 |
| 面台帳／A0・V0・T0 | **PM 決定（6）**: F3 A0が`hwdev/gf/vram_view.h`に`struct gf_vram_reservation`（owner/off/bytes/reservation_id/generation）と列挙契約を宣言・凍結、V0が窓/台帳実装を所有。F3 B1・F4 B6/B7・F5 B6は同型を利用。表示・試験面とpush B・末尾instmemを分離し、F4がcursor/front/backを登録、F5が退避extentを列挙。一般heapは追加せず、notifierは表示直後に置かない。 |
| 配置・試験環境 | B0〜B10・追加V0・C0はゲストgeforce.mod。各実部品を対応host.cでもlinkし、依存を偽装する。A1は`gf_command_host.h`の方式と`gf_command_runtime_host.h`を再用する（GF2_HOST_B選択付きヘッダーをそのまま全試験にincludeしない）。A2はhost Python登録、T0/T1はゲスト診断。 |
| 予算 | F3 text/rodata14〜20＋BSS等8＝22〜28KiB。追加窓・台帳・libgcc helperもこの内数、独立した無予算の追加にはしない。全層合算は下記の共通表。 |

### 合算予算

実測はmain 66c1b351 を組んだ `build/out/{geforce,hwdev}.mod`のheader（2026-11-04読取）。単位KiB、1KiB=1024B。増分は見積り。
| 対象／増分 | image（ファイル） | mem（BSS込みbody） | 判定／配分 |
|---|---:|---:|---|
| geforce層2後 | 61,276B = 59.84 | 65,536B = 64 | BSS 5,496B、ops 4、payload_off 4KiB、load_span 68KiB |
| F3 | +14〜20 | +22〜28 | text/rodata＋BSS等8、V0含む |
| F4 | +14〜21 | +15〜24 | text/rodata＋BSS1〜3 |
| F5本体 | +18 | +20 | BSS/alignment2 |
| F5 C1共有decoder | +0〜4 | +0〜4 | tables.incを両modに計上 |
| geforce全層小計 | 105.84〜122.84 | 121〜140 | F1/F2は再加算しない。旧mem上限128を最大12超過 |
| metadata/頁丸めの余裕 | +12を配分 → 上側134.84 | 上側140から160まで20 | reloc/import等の増分は未実測。image160まで残25.16、memの頁丸めも余裕内 |
| geforce新上限 | **160 = 163,840B** | **160 = 163,840B** | **PM 決定（1）**: load_span_maxは**208KiB = 212,992B**。上側見積り＋余裕で設定 |
| hwdev層2後 | 129,632B = 126.59 | 126,976B = 124 | BSS 4,324B、payload_off 8KiB、load_span 132KiB |
| hwdev全層 | 126.59＋0＋(0〜12)＋(0〜4) = 126.59〜142.59 | 124＋0＋(0〜12)＋(0〜4) = 124〜140 | 上限192KiB内。image残49.41、mem残52をmetadata/丸めにも充てる |
| 媒体・別台帳 | geforceは起動FD対象外 | kernel helper/state/診断・VRAM予約・動的snapshot/ROMは別 | まとめビルド後にKDEBUG=0/1とhwdev/R1を計測 |
- **PM 決定（1、上限の広げ方）**: F3≤28、F4≤24、F5≤24KiB（mem増分、C1込み）を配分し、圧縮を発注条件にしない。load_spanはprefix想定16＋mem上側140＝156KiBに対し52KiBの余裕；memを160まで使ってもprefixに48KiBを残す。import本数制限は復活させず、統合時にimage/mem/prefix/load_spanを独立採取する。
- 物理の根拠: `kernel/module/module_runtime.c:70–86`はload_span分の物理頁をpgallocから動的取得しP2Vで配置・再配置する。geforce専用の固定読込番地帯や128KiBの常駐RAM枠、KHEAPからの像確保は無い。上限は予約量ではなく検査値で、拡大だけで208KiBを消費しない。従って今回の拡大を妨げる固定の物理制約は無い（実機空きRAMの実測保証ではなく、取得失敗は既存ローダーで処理）。
- `docs/08_build.md:181–202`の容量表の正はschema。F4 C3が`tools/module_schema.py:188`を更新し同表にGEFORCE行を追加して同期し、PM登録段が生成物を更新。生成物は手編集しない。

### 共有ファイルの衝突表・所有段

| 衝突するファイル | 関係段 | 唯一の編集者／他段の報告 |
|---|---|---|
| build/geforce-module.mk・tools/prototypes/geforce_imports.def | F3 C3、F4 C3、F5 C3 | 各層の登録担当をF3 C3→F4 C3→F5 C3で直列。B担当はsource/import行だけ報告。 |
| drivers/video/geforce/geforce_module.h/.c・tools/module_schema.py | F3 C4/C5、F4 C3、F5試験公開口 | F4 C3のABI宣言部分を先に凍結；F3は既存slotを使用。PM 決定（5）: feature5_outを必ずこの宣言段に含める。F5 A2/C4はslot経由、F5独自の版上げなし。 |
| hwdev/gf/production.h・service.c・display/quiesce.c・devinit.c | F3 A3/C1/C2、F4 C2、F5 C2 | 宣言はF3 A3→F4 A0→F5 A0の順。結線はF3→F4→F5、ファイルごとに段を分割して直列。 |
| 新hwdev/gf/vram_view.h/.c | F3 A0/V0、F3 B1、F4 B6/B7、F5 B6 | PM 決定（4・6）: F3 A0が窓/面台帳型、V0が実装を所有。F4/F5は利用のみ。command.h/fifo.cへの期限口追加なし。 |
| backend_geforce.c・include/gfx_hal.h・kernelソース列 | F3 C0a、F4 C1/C4、F5前景結線 | F3 C0a→F4 C1/C4→F5前景段。HAL宣言だけ先に独立凍結、各backendの追随も単一担当。 |
| build/hwdev.mk・hwdev/hwdev_imports.def・docs/design/hwdev/hwdev_imports.md | 各層共有計算/診断変更 | 各層登録担当の直列列。import変更は表/件数も同時、他段は必要性と取得元ヘッダーを報告。 |
| tools/gen_hwdev_contract.py・tools/hwdev_layout.json・契約/schema/import生成物（kernel/module/module_schema.hを含む） | 全層の配置/契約変更 | PMの登録段だけ。生成器の件数/台帳を追随し、生成は全部品統合後に1回。kernel/module/module_schema.hはtools/gen_module_schema.pyの生成物で、点検時:259–260にgeforceの上限131072がある。F4 C3がtools/module_schema.py:188を変更したらPM登録段で再生成する。B担当による生成/生成物手編集なし。 |
| tools/tests/test_gf_command.py・test_gf_accel.py・test_gf_display.py・test_gf5.py | F2追随、各層B群 | 前者はPM登録段、残りはF3 A2/F4 A2/F5 A3が各1人で所有。BはCHECK/case/変異tupleを報告。 |
| hwdev_contract_fixture.py・test_dep_dir_a_l02.py・sh_{status,shell,truncation}_host.c・recover_virtual_fixture_host.inc・hwdev_audit.py（tools/tests等） | 契約/ソース追加の波及 | PM登録段が該当分を追随。各実装者は当たる一覧を報告、件数/CRCは可能なら元から導出。 |
| kdebug/gfinit_*・hwdev/gfinit_mode.h・kdebug/gf_command_test.c・DEFERRED_TESTS.md | F4 A0、F5 C0/C1、F3 T1、全層台帳 | F4 A0を先行。F5 C0はB1と同一所有者で直列、C1 tables.incはF4 A0後。診断dispatch/台帳はPM登録段へ追加行報告。 |

### 依存と並列発注

| 時点 | 出せる段の組／待つもの |
|---|---|
| 今すぐ | F3 A0のみ（accel・vram_view.hの窓/面台帳宣言、既存pauseの独自期限を明記）。F5 A1/A2はF5 A0の型に依存するためまだ出さない。 |
| F3宣言レビュー・統合後 | F3 B0〜B10＋追加V0は宣言が全て凍結済みなら、依存実装をstubとするコーディングを並列可。B3/B4→B1/B2等は結合時の依存。共有登録・結線は渡さない。F4 A0はF3面/窓契約後、F5 A0はF2停止口とF4保存型の凍結後。 |
| F4/F5宣言統合後 | F5 A0後にA1（gf5_fake.h）/A2（gf5_test.c）を発注可。F4 B0〜B12、F5 B0〜B6は互いに異なるファイルならF3 B群と並列可。F5 B3はB4所有GPIO宣言を使い、実装を複製しない。F4 B11はtv.cとtv_data.cの2段、B1等も1ファイルごとに分割。 |
| F3の後でないと結線できない段 | F4 C0/C1/C2、B6の実map結合、B7の実drain結合。F5 B2/B5の停止結合、B6の全extent/再生成結合、C2。F5 state/display結線はF4 C2/C4も待つ。 |
| 統合順 | 同じファイル・ABI世代はPARALLEL_BRANCHES §1どおり先のmain取り込み後に切る。機能ごとのworktree、触る一覧外は報告。C結線は全部品後に直列、全実装後にPMが生成→まとめビルド→検査。設計にある「各Bで試験」は試験コードを完成させる意味で、実行は先送り。 |
| 全依頼の共通条件 | PORT_FLOW §4・§5：i32、必要include、kstring、hostは-nostdlib/自前_start・32bit glibc導入なし、assertの最適化消失防止、変異は一意anchorと実行時RED。共通誤りは全担当へ展開し統合前にgrep。ゲストは既定newlib nanoツールチェーン、module/kernel自体にlibc依存を追加しない。 |

## 追記 (2026-11-04、A0 の欠け — 独立 VRAM 窓と面予約台帳の宣言)

> 出所: ~/os32-tmp/run/gf3-a0/ADDENDUM.r2.md (astra 起草 r1〜r2、Opus レビュー 2 回、確定の補いは PM)。

- ID 1: 窓に map_base/map_bytes を追加し、頁丸めと解除範囲を一致させる。
- ID 2: C1 で表示面・試験面を登録し、scan の識別値と試験用 surface を module から渡す。
- ID 3: PM 決定 — 既存表示範囲は bar1_map を借用し、窓は EMAP で拒否する。
- ID 4: PM 決定 — F5 は save/restore をまたいで予約を保持する。
- ID 5: 保持窓と試験面の予約を 4KiB 整列にする。
- ID 6: C1 の登録前に台帳を初期化し、host 試験は 1 プロセス 1 ch とする。
- ID 7: include の向きと V0 CHECK の vram_view.* covers を明記する。

## 追記 (2026-11-04、A0 の欠け)

本追記は F3 A0/V0 と F4/F5 の共通契約。宣言は `hwdev/gf/vram_view.h`（`command.h` を include）、`accel.h` は `vram_view.h` を include し、`vram_view.c` は誤り値定義のため `accel.h` を include、実装は V0。前景のシングルタスク・協調切替を前提とし、IRQ・再入・列挙中の台帳変更は呼出側で行わない。一般 heap、ロック、参照カウント、自動退避は追加しない。

### 1. 独立 VRAM 窓
```c
struct gf_vram_view {
    struct recover_bar_extent extent;
    volatile u8 *va;
    volatile u8 *map_base;
    u32 map_bytes;
    u32 bytes;
    bool mapped;
    const struct gf_instmem_map_ops *map_ops;
    void *map_owner;
};
int gf_vram_view_map(struct gf_vram_view *view, struct gf2_chan *ch,
                     u32 off, u32 len, volatile u8 **out);
int gf_vram_view_unmap(struct gf_vram_view *view);
```
- 初期値は全欄 0。窓は呼出側所有、成功 map から成功 unmap まで同じ記憶域を維持し、値コピーしない。F3 readback/F5 一時コピーは呼出し内の局所記憶域、F4 の保持窓は常駐 ctx 内に置ける。借用 ops/owner は解除成功まで存続する。
- map は `ch->bar1` の頁丸めした部分 extent を UC で写す。start=off&~4095U、end=ceil((off+len)/4096)*4096 を桁あふれなく求め、extent は [start,end)、map_bytes=end-start、map_base は下位 map の返す基底、va=map_base+(off-start)、bytes=len。成功時だけ全所有欄と借用 ops/owner を保存して `*out=va`、0 を返す。`ch->va/mapped/push_extent` は変更せず、push が map 済みでも利用できる。
- off/len は byte、非零 len と 4B 整列を要求。BAR1・物理番地の加算範囲は `command_platform.c:11–15` と同じ条件。加えて範囲全体が共通台帳の生存予約 1 件に含まれることを照合する。4B に丸めた readback 要求範囲も予約内であること。頁丸め後の extent は BAR1・物理番地の有効範囲内を要求するが、予約への包含照合は要求範囲 off/len に行い、CPU はその範囲外にアクセスしない。
- NULL 引数・ops 不備・二重 map・範囲/整列不正・下位 map 失敗は `GF2_EMAP=-203`、予約不存在は `GF3_ESTATE=-304`。失敗時は窓と出力を変更しない。台帳/ch の対応不成立も ESTATE。
- unmap は保存した ops/owner/map_base/map_bytes を使用し、成功時に全欄を 0 に戻して 0。NULL・未 map・下位失敗は GF2_EMAP、全所有欄を保持して再試行可能とする。
- map は GPU 完了を待たない。呼出側が reference 完了（restore は GPU がその領域を使わない区間）を保証し、CPU アクセス後は必要な posted write 排出を済ませて unmap。窓を解除するまで対応予約を解除せず、停止/復旧前に全窓を閉じる。解除失敗時は予約・adapter の所有を保持する。
- PM 決定 (ID 3): 起動時の BAR1 表示先頭 75 頁は既存の bar1_map を借用して読み書きし、窓の頁丸め extent がそこに重なる場合は GF2_EMAP。借用表示 map に窓の unmap は呼ばない。根拠: `hwdev/gf/production.h:55`、`drivers/video/geforce/geforce_platform.c:24`。
- 保持したままにする窓（F4 cursor 等）と T0 試験面の予約は off/bytes を 4KiB 整列とし、別予約の窓が同じ頁を共有しない。下位の頁単位契約は `hwdev/gf/instmem_map.c:12–15,24–25` に合わせる。
- F3 `gf3_test_ops.map/unmap` は上記関数へのポインタ型（既決の `__typeof__`）に固定。F4 cursor と F5 の未マップ領域の退避も同じ口を使う。T0 の表示面 readback は既存 bar1_map を借用する。
- 手本 N=`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau`、`N/nouveau_bo.c:669–679` の BO 固有 kmap、`:684–689` の kunmap に対応。OS32 は BO/TTM の代わりに呼出側窓と既存 map_ops を使う。int unmap と失敗時保持は `hwdev/gf/command_platform.c:32–42` の既存失敗契約に合わせる。TTM の reserve ロックは協調前景では追加しない。

### 2. 面予約台帳
```c
struct gf_vram_reservation {
    const void *owner;
    u32 off, bytes, reservation_id, generation;
};
struct gf_vram_reservation_node {
    struct gf_vram_reservation value;
    struct gf_vram_reservation_node *next;
};
struct gf_vram_registry {
    struct gf2_chan *ch;
    struct gf_vram_reservation_node *head;
    u32 next_id;
};
int gf_vram_registry_init(struct gf2_chan *ch);
int gf_vram_reserve(struct gf2_chan *ch, const void *owner,
                    u32 off, u32 bytes, struct gf_vram_reservation_node *node);
int gf_vram_release(struct gf2_chan *ch, const struct gf_vram_reservation *key);
int gf_vram_match(struct gf2_chan *ch, const void *owner,
                  u32 reservation_id, u32 generation, u32 off, u32 bytes,
                  struct gf_vram_reservation *out);
int gf_vram_next(struct gf2_chan *ch, u32 *cursor,
                 struct gf_vram_reservation *out);
```
- V0 は production の 1 channel 専用共通台帳を `static struct gf_vram_registry` 1 個として所有する（production の寿命内、host も 1 個）。全欄 0 が未初期化。init(ch) は初回に ch/head=NULL/next_id=1 を設定して 0、同じ ch への再呼出しは無変更で 0、異なる ch は GF3_ESTATE、NULL は GF3_EINVAL。C1 の gf3_init 結線で ch 確定後、最初の reserve と hook/test 公開より前に init(ch) を呼ぶ。V0 host 試験は 1 プロセス 1 ch。recover で台帳/ID 発行器を零化しない。
- node は予約主体が所有する静的記憶域または常駐 ctx の欄、初回は全欄 0。登録中は移動/書換え禁止。台帳は node を借用して連結し、画像や node の heap 確保を行わない。owner は同期間存続する所有者オブジェクトの非 NULL アドレス（同一性だけ比較、逆参照しない）。
- reserve は指定範囲を登録するだけで配置を探索しない。bytes>0、off<=ch->B かつ bytes<=ch->B-off、VRAM末尾 instmem より下、既存予約と非重複を要求する。全成功時だけ node を連結し value を確定して 0。push/instmem は台帳外で F2/F1 が所有する。
- reservation_id は登録ごとに next_id から新規発行し、解除後も再使用しない。U32_MAX 発行後は next_id=0（以後 ENOSPC）。generation は node の登録成功ごとに 1 増加、初回 1。上限到達 node の再登録も ENOSPC とし、古い組を再発行しない。これは既決の古い予約排除の識別契約であり、追加の HW 監視ではない。
- release は key の全 5 欄が生存項目と一致した時だけ連結を外し 0。node は next=NULL、value.owner=NULL、off/bytes=0 とし、ID/generation は履歴として保持。失敗時は保持。GPU 完了・表示による利用終了・窓解除は所有者が先に保証する。
- match は ID/generation と範囲包含を照合し、owner 非 NULL なら所有者も一致させ、成功時だけ *out に予約値をコピーして 0。owner=NULL は信頼済み内部利用者の所有者不問照合（ID/世代は必須）。off/bytes は部分面を許すが bytes>0。出力は借用 node ではなく値コピー。
- init 以外の通常成功は 0。NULL 必須引数/不正サイズは `GF3_EINVAL=-301`、配置可能範囲外は `GF3_ERANGE=-303`、重複/登録済み node は `GF2_EEXIST=-208`、ID/世代発行不能は `GF2_ENOSPC=-202`、未初期化/ch 不一致/予約不存在/古い ID・世代/所有者不一致は GF3_ESTATE。match の包含失敗は GF3_ERANGE。失敗時は台帳・node・出力を変更しない。
- next は *cursor=0 で開始し、ID 昇順で *cursor より大きい最初の項目を *out にコピー、*cursor=その ID として **1**。終端は **0**（両出力保持）、誤りは上記負 rc。列挙中は登録/解除しない。固定件数上限・全件用静的配列は設けない。
- 古い面排除に必要な最小補正として、§2 の `gf3_surface` を次の完全定義に置換する（他の描画 API 引数は不変、F2 fence epoch は変更しない）。
```c
struct gf3_surface {
    u32 off, bytes, pitch, width, height;
    enum gf3_fmt fmt;
    u32 reservation_id, generation;
};
```
- surface は全欄 0 では未予約。所有者が登録成功した value から ID/generation を設定する。B1 は match(ch,NULL,s->reservation_id,s->generation,s->off,s->bytes,&r) を先に行う。範囲だけから最新予約を推測して古い面を通すことは禁止。予約を破棄する recover/所有終了時は受付を閉じ、旧予約を解除して再登録し、surface/hook を作り直す。F5 の同一配置 save/restore は下記の保持契約を適用する。停止失敗時は予約を保持する。
- F3 単体でも C1 の module が静的 owner と表示用・試験用 node（各全欄 0）を持ち、gf3_init 結線で表示面、次に試験面を reserve し、成功後だけ hook/test を公開する。試験面は表示予約末尾の次の 4KiB 境界から確保し、必要 bytes も 4KiB に切り上げる（pitch は従来の 64B 整列）。終了時は利用停止・窓解除後に module が release する。
- §10:187 の scan 構築・bind は C1 の module 側へ移し、既存の幾何欄に `.reservation_id=scan_node.value.reservation_id,.generation=scan_node.value.generation` を追加する。C0a は未登録 scan を公開せず、表示確定情報を C1 の結線で渡す。登録失敗時は hook を無効のまま誤りを返す。
- `gf3_test_ops` に `const struct gf3_surface *test_surface;` を追加し、module 所有の常駐面（登録値から ID/世代を設定）を借用で渡す。T0 はこの予約内で部分面を作り、識別値を引き継ぐ。予約失効時は dispatch 公開を閉じる。reserve/release の 2 口を増やすより、BO 所有者が確保して利用者に渡す手本に近く、追加は面を渡す 1 欄で済む。
- F4 導入時は F3 の表示・試験利用を停止して両予約を解除した後に所有を引き継ぎ、重複登録しない。F4 は front/back/cursor 各予約の node を常駐 ctx に持ち、初期 scanout も試験公開前に登録する。mode 変更で旧面を解除するのは表示利用終了後。GfxScanout の ID は再使用されないため、adapter は所有 ctx に保持する同 ID の予約から generation を得て owner 付き match を行い surface に転記する。不一致を現在の範囲だけで補完しない。
- PM 決定 (ID 4): F5 は予約を解除せず、同一 VRAM 配置の save/restore をまたいで node・owner・ID/世代を生存させる。F5 は受付閉で next を全件走査し off/bytes と識別値を snapshot に値保存する。全画像記憶域確保→drain/blank→全件コピー（既存表示範囲は bar1_map、残りは窓。境界をまたぐ予約は分割）→全窓解除→既決停止。restore は同じ経路で同一 off/bytes に書き戻し、同じ識別値で全利用者を再 bind。push/PRAMIN 等は従来どおり再生成し、node/va を snapshot に保存しない。画像容量に固定上限を足さない。
- 手本は `N/nouveau_bo.c:594–599` の placement/validate/pin と `:618–629` の unpin。TTM の BO 所有・固定配置を OS32 の明示範囲/常駐 node 台帳に置換する（`ttm_bo_reserve` は排他であり範囲登録 API そのものではない）。ID/世代は既決 F4 の HAL 境界用、列挙は既決 F5 CPU 退避用。一般 allocator・evict・参照数は移植しない。
- V0 の CHECK の covers に `hwdev/gf/vram_view.*` を追加する（F3 §13:257 の既存 accel* だけでは対象外）。
- V0 に残すのは連結操作・照合 helper・map 実装と試験の細部。F3 A0 は上記宣言、A3/C 群は既決所有と起動/停止の結線を担当する。未決 0 件。

### 確定の補い (2026-11-04、claude-opus-5-5 の第 2 回、PM が反映)
- N1: F5 の save は、受付を閉じたあと、コピーの前に、開いたままの窓 (F4 の cursor) を持ち主の F4 に閉じさせる。コピーの後、F4 が開き直す。
- N2: 試験面の範囲は、表示の予約の末尾の次の 4KiB 境界から、B 以下の最後の 4KiB 境界まで。
- N3: F4 の段で、C1 の登録 (表示面・試験面) を F4 の ctx に移す (実行時の引き渡しはしない)。
- N4: C0a が backend の framebuffer の情報を C1 へ渡す口の名前と型は、A0 で accel.h に宣言する (A0 の実装者が決め、報告に書く)。

## 追記 (2026-11-04、第 1 波の欠け G1〜G3 — 試験の土台・面の形式の共有・M2MF の範囲)

> 出所: ~/os32-tmp/run/gf3-gaps1/ADDENDUM.r2.md (astra 起草 r1〜r2、Opus レビュー 2 回、確定の補いは PM)。

- P2-1: B0 担当による UNIT=0 移行・自作 stub 削除・observe への前提移植を明記。
- P2-2: 単独描画製品の packet 照合と stub の画像参照を区別し、ゲスト期待像を T0 担当の所有とした。
- P2-3: B8 の IFC 選択を ifc≠NONE と §7 の条件等で限定し、M2MF fallback 条件を表にも反映。
- P3-4: RESUME の失敗注入は last_rc のみ更新し、FAULT 注入と分離。
- P3-5: 失敗時出力を F1/F2 の実物に合わせ、DMA_NEW の inst.size 残存・handle=0 を明記。
- P3-6: host ref_scale を RGB point/同色に限定し、bilinear/YUV 理想像を T0 側へ移管。

## 追記 (2026-11-04、第 1 波の欠け)
本追記は §12・§13 の分担を補う。A0 が accel.h の追加宣言を凍結し、A1 が host 契約を実装してから B 群を再開する。既存 ABI は削除しない（A1 の仮 API だけ下記へ統一）。シングルタスク・協調前景、一般 heap・ロック・追加 HW 監視なし。製品の失敗値は既存 GF3_EINVAL=-301/EFORMAT=-302/ERANGE=-303/ESTATE=-304/EUNSUPPORTED=-305 と F1/F2 の原値、成功は 0。
参照略号は本文 §1 の X/N/V/E/A/F/M と同じ。行番号は保存済み手本を照合したもの。

### G2 — format・面・RAM の共有境界（宣言 A0、実装 B1）
`hwdev/gf/accel.h` に以下を追加する。表は B1 の static const 7 行のみ、呼出側への値コピーで所有を渡す。
```c
#define GF3_FMT_NONE 0U
struct gf3_format { u32 cpp, sf, sf_gdi, gdi, pat, ifc, sifm; };
enum gf3_surface_use { GF3_USE_BYTES, GF3_USE_2D, GF3_USE_GDI, GF3_USE_IFC, GF3_USE_SIFM_SRC };
struct gf3_span { u32 off, rowbytes, pitch, rows, bytes; };
int gf3_format_get(enum gf3_fmt fmt, struct gf3_format *out);
int gf3_span_check(u32 off, u32 capacity, u32 pitch, u32 rowbytes, u32 rows, struct gf3_span *out);
int gf3_surface_check(struct gf3_ctx *c, const struct gf3_surface *s, struct gf3_rect r, enum gf3_surface_use use, struct gf3_format *fmt, struct gf3_span *span);
int gf3_ram_check(const struct gf3_ram *ram, u32 rowbytes, u32 rows, struct gf3_span *out);
```
- format_get: NULL out は EINVAL、不明 fmt は EFORMAT。§9 の cpp/SF通常/GDI用SF/GDI/PAT/IFC/SIFM を返す。非対応欄は NONE（実 format 値 0 は使わない）。YUY2/UYVY は cpp=2、sifm=5/6、残り NONE。Y8 の ifc=NONE。表を B2〜B10 に複製しない。B2 も format_get の cpp/pat を使い（結合依存 B1 を追加）、Y8 は8bitとして扱う。mask/alu制約は B2 の責任。
- span_check: capacity は基底からの byte 数、off はその基底相対、bytes は最後の有効 byte までの包絡長 `(rows-1)*pitch+rowbytes`。pitch<rowbytes は ERANGE。off<=capacity、bytes<=capacity-off を減算/除算で検査し、溢れる積を先に作らない。rowbytes=0 または rows=0 は bytes=0（off<=capacity は要求）。NULL out は EINVAL、他の範囲不正は ERANGE、成功時だけ全出力を確定。
- surface_check: NULL c/ch/s/fmt/span、不明 use は EINVAL。最初に既決 match(ch,NULL,ID,世代,off,bytes,&key) を行い原 rc を返す。次に format、width/height>0、pitch>=width*cpp、面全体の最終 byte<=s->bytes、矩形包含を検査。span.off は VRAM 絶対、rowbytes=r.w*cpp、rows=r.h、pitch=s->pitch。各乗加算・絶対末尾を桁溢れなく検査し、失敗時は両出力保持。ready/epoch/RUN/fault は各描画入口の責任（B0 の初期化検査と混ぜない）。
- BYTES は全7形式可で 16bit field 制限なし。2D/GDI/IFC は該当 sf/sf_gdi/ifc が NONE なら EFORMAT、pitch と実際に pack する座標/寸法は <=65535。SIFM_SRC は sifm 必須、pitch/全面寸法の16bit制限、開始点の12.4制限、YUVの幅・r.x・r.w偶数を検査。一般の幅と pitch の整列制約は新設しない。IFC の高さ/行長/1792語制限は B6 が EUNSUPPORTED とする。
- ram_check: NULL ram/out、非空転送の NULL ptr は EINVAL。span_check(0,ram->bytes,ram->pitch,rowbytes,rows,out) と同じ範囲検査、RAM の ptr は借用し変更しない。mono の rowbytes は ceil(w/8)、その他は w*cpp を呼出側が溢れなく計算する。成功以外は out 保持。
- ゼロ矩形は各公開入口で必須ポインタ・状態を確認後に無投入 0、面や RAM に触れない。非空は全検査を最初の emit より前に済ませる。B1 surface_set は src/dst を全面検査し同 fmt を要求、gdi で sf_gdi/gdi を選ぶ。B3/B5 は GDI、B4 は 2D、B6 は IFC、B8 は BYTES、B9 は SIFM_SRC と 2D の組を使う。B8 は force_m2mf=false かつ format_get の ifc≠NONE かつ §7 の IFC 条件（RGB565/32bit、rowbytes>=4、h<=1024、align(rowbytes,8)/4<=1792）をすべて満たすときだけ IFC を選び、それ以外は M2MF を選ぶ。IFC の EUNSUPPORTED も M2MF へ fallback し、EFORMAT や他の失敗は伝播する（INDEX8/Y8 を IFC に渡さない）。B9 の出力 INDEX8 は明示 EFORMAT、clip/拡縮率/alias は各 B が既決条件で検査する。
- 対応: A:254–276 の format 取得、E:97–129 の GDI alpha 例外、:180–200 の二面設定、:302–319 の IFC、X/nv04_xv_blit.c:68–93,99–117 の SIFM。BO/pixmap に分散した幾何と値取得を OS32 の byte 範囲付き面に集約するだけで、検査・表の重複を製品に増やさない。

### G3 — raw M2MF は検査済み範囲を借りる内部口（B7/B8）
`accel.h` の既存宣言は次のまま（追加の handle/capability 型・台帳探索口は作らない）。
```c
int gf3_m2mf(struct gf3_ctx *c, u32 in_handle, u32 out_handle, u32 in_off, u32 out_off, u32 in_pitch, u32 out_pitch, u32 rowbytes, u32 rows, u32 dl);
```
- B7 は状態/epoch/fault、handle が VRAM または PCI であること、双方の pitch>=rowbytes、全 rows の最終 byte を検査。VRAM 容量は min(ch->B,VRAM末尾instmem開始)、PCI は生存 staging.size（4096以下）。未知 handle は EINVAL、PCI staging 不在は GF2_ENOSPC=-202、範囲不正は ERANGE。span_check を利用するため B7 の結合依存に B1 を追加。rows>2047 は拒否せず分割。VRAM 同士の重なりは EUNSUPPORTED（包絡が交差する場合を含め copy を使う）。ゼロは上の無投入契約。
- **予約の権限照合は呼出側**。B8 は surface_check(BYTES) と ram_check を全要求に先行させ、各片をその span 内から作る。B10 は B8 の upload(force=true) だけを呼ぶ。T0 は dispatch の upload/download/present を使い raw を公開しない。将来の内部 VRAM→VRAM 呼出側も両方の生存 surface を BYTES 検査してから raw を呼ぶ。範囲だけから予約を逆引きしてはならない。
- 検査から最終 reference 完了まで予約・staging を保持する責任は呼出側、待ちから再入/予約解除しない。B7 は各 raw 呼出しの全投入後に reference emit/wait して同期完了、B8 は成功帰還後だけ次片の staging を再利用/CPU読出しする（追加の二重 fence 不要）。B7 が予約不存在を検出するという試験は置かず、古い ID/世代・他予約侵入は B1/B8 と結合試験で検出する。
- 対応: E:394–410 の BO refn/validation を呼出側の予約確認に、:412–430 を B7 に分離。N/nouveau_bo.c:594–599,618–629 の固定配置寿命に相当。BO を受け取らない既存 raw ABI に推測認可を追加せず、協調前景の同一所有期間で保証する。手本の非同期 BO 待ちは §7 の同期 API に合わせる。

### G1 — A1 が所有する host API の全一覧
`tools/tests/gf_accel_host.h` は試験 TU だけに include。`GF3_HOST_UNIT` は B0〜B10 を 0〜10、C0 を 11、依存 stub 無しを 12 とする必須選択（範囲外/未定義は #error）。旧 GF3_HOST_COMMAND_STUBS と個別自作 stub は廃止。既存 B0 試験（tools/tests/gf_accel_b0_host.c:6）は A1 確定後に B0 担当が GF3_HOST_B0 を GF3_HOST_UNIT=0 に移行し、自作 stub 12 個を削除する。各呼出しの前提（pause 中・!ready・dl・align=16・zero=false）は該当 event の observe で照合する。製品 TU は1本、選択された製品の定義は絶対に stub しない。C1/C2/C4 の結合は 12 と F1/F2/各B実物を使う。
以下が新しい完全定義・宣言。helper はヘッダー内 static inline、依存 stub だけ外部定義。大きな記憶域は試験の static 領域、画像/記録バッファは呼出側借用、heap/VLA/libc は使わない。
```c
enum gf3_host_op { H_EMIT, H_FENCE, H_WAIT, H_FAULT, H_PAUSE, H_RESUME, H_STOP, H_ALLOC, H_FREE, H_DMA_NEW, H_GR_NEW, H_DEL, H_MATCH, H_FORMAT, H_SPAN, H_SURFACE_CHECK, H_RAM_CHECK, H_SURFACE_SET, H_ROP, H_IFC, H_M2MF, H_UPLOAD, H_FILL, H_COPY, H_READ, H_WRITE, H_CLOCK, H_SLEEP, H_OP_COUNT };
struct gf3_host_reg { u32 bar, off, value; };
struct gf3_host_packet { u32 first, deadline; u16 method, count; u8 subc; bool noninc; };
struct gf3_host_event { enum gf3_host_op op; u32 nth, a[12]; const void *p[4]; int rc; };
struct gf3_host_image { u8 *ptr; u32 bytes, pitch, width, height; enum gf3_fmt fmt; };
struct gf3_host {
    struct gf_foundation f; struct gf2_chan ch; struct gf3_ctx ctx;
    struct gf3_host_reg *regs, *writes; u32 cregs, cwrites, nregs, nwrites;
    struct gf3_host_packet *packets; u32 cpackets, npackets; u32 *words; u32 cwords, nwords;
    struct gf3_host_event *events; u32 cevents, nevents, calls[H_OP_COUNT], fail_at[H_OP_COUNT]; int fail_rc[H_OP_COUNT];
    u8 staging[GF3_STAGING_BYTES]; u8 *vram; u32 vram_bytes, now, complete_after, waits;
    struct gf_vram_reservation_node *reservations; bool paused; u32 live_objects, alloc_off;
    void (*observe)(struct gf3_host *, const struct gf3_host_event *);
};
static inline void gf3_host_init(struct gf3_host *h);
static inline void gf3_host_trace_clear(struct gf3_host *h);
static inline void gf3_host_fail_at(struct gf3_host *h, enum gf3_host_op op, u32 nth, int rc);
static inline void gf3_host_set(struct gf3_host *h, u32 bar, u32 off, u32 value);
static inline int gf3_host_read(void *owner, u32 bar, u32 off, u32 width, u32 *out);
static inline int gf3_host_write(void *owner, u32 bar, u32 off, u32 width, u32 value);
static inline int gf3_host_clock(void *owner, u32 *now);
static inline int gf3_host_wait(void *owner, u32 us);
static inline u8 gf3_host_rop_byte(u8 rop, u8 pattern, u8 src, u8 dst);
static inline void gf3_host_ref_fill(struct gf3_host_image *dst, struct gf3_rect r, u32 color, u32 mask, u8 rop);
static inline void gf3_host_ref_copy(const struct gf3_host_image *before, struct gf3_host_image *dst, struct gf3_rect sr, u32 dx, u32 dy, u32 mask, u8 rop);
static inline void gf3_host_ref_mono(struct gf3_host_image *dst, struct gf3_rect r, const struct gf3_ram *bits, u32 fg, u32 bg, struct gf3_rect clip);
static inline void gf3_host_ref_ifc(struct gf3_host_image *dst, struct gf3_rect r, const struct gf3_ram *ram);
static inline void gf3_host_ref_m2mf(const struct gf3_ram *src, u32 src_off, struct gf3_ram *dst, u32 dst_off, u32 rowbytes, u32 rows);
static inline void gf3_host_ref_scale(const struct gf3_host_image *src, struct gf3_host_image *dst, struct gf3_rect sr, struct gf3_rect dr, struct gf3_rect clip, bool bilinear);
```
- init は全欄零化後 f.io と timing を上記 callback/owner=h に接続、ch.f、RUN/epoch=1、staging_live/va/size、既定 DMA handles、ctx.ch/ready/epoch/obj handles を設定。B0 は ctx を零化してから init を試す。試験は init 後、バッファ pointer/capacity、VRAM 配置（ch.B/f.imem/bar1）、予約リストを明示設定。vram[0] は VRAM off=0、staging[0] は PCI off=0。fixture の容量は製品制限ではない。
- trace_clear は記録数/calls/waits だけを零化（状態/画像/失敗設定/時刻は保持）。fail_at は op の nth 回目だけ rc を返す設定、nth=0 は全回、rc=0 は解除。成功・失敗とも event を先に予約し、返却 rc と引数を記録、observe を帰還前に呼ぶ。配列満杯・不正な試験入力は常時有効 assert→gf2_host_fail→exit(1)、切捨てや製品 ENOSPC に偽装しない。
- event.a は関数宣言順の非pointer引数（enum/bool/u8/u16もu32、rect/fenceは宣言順の欄へ展開）、event.p は宣言順のpointer引数。余りは0。struct入力の内容比較は observe 内で行い、pointerを後で逆参照しない。emit成功時だけ packets と words に payload を深くコピー（first は words 添字）、失敗 emit は event のみ。全呼出しで dl を記録する。
- READ/WRITE は 32bit MMIO のみ、未設定 read=0、成功 write を writes と regs に保存。clock は now を返し、wait は成功時だけ now+=us/waits++。callback の注入 rc はそのまま返す。非空 stub の FENCE は epoch/++seq を発行、WAIT は fault→epoch→絶対期限→完了の順で検査し、complete_after 回の wait(1us) 後に成功（0=即時、U32_MAX=完了しない）。旧epochは GF2_EFAULT=-206、期限は GF2_ETIME=-205、clock/wait失敗は GF2_EIO=-204。GET==PUTを完了としない。
- F2 stub は command.h、F1 stub は foundation.h、F3 stub は accel.h、MATCH は vram_view.h の**既存の完全な型**で定義する（別の stub 用製品 ABI は作らない）。PAUSE/RESUME は paused を変更、RESUME 注入失敗は void のため ch.last_rc にだけ保存し、FAULT には遷移させない（F2 fifo.c:232–238）、STOP 成功は OFF と資源解放、失敗は資源保持。FAULT 注入は fault.latched=true/state=FAULT/last_rc=rc を設定する。ALLOC は alloc_off（試験が設定した instmem aperture 相対）から通知領域を確保、NEW/DEL は出力 object と live_objects を更新し pause 中を assert。FREE は void で失敗注入不可。成功・注入失敗とも出力/所有は各製品 F1/F2 の実物の契約に合わせる。DMA_NEW の descriptor 書込み失敗は確保を解放して inst.size を残し handle=0、rc=GF2_EIO とする（F2 dmaobj.c:42–46）。
- MATCH は h.reservations の生存値を ID/世代/owner/包含で照合（生産台帳実装には link しない）。FORMAT/SPAN/SURFACE_CHECK/RAM_CHECK は本追記の正常値と検査を独立に供給し、各 op に失敗注入可能。SURFACE_SET/ROP は引数を記録するだけで HW packet は合成しない。B1/B2 の packet 試験と、利用側の呼出し引数試験を混同しない。
- IFC/M2MF/UPLOAD/FILL/COPY は typed 引数の記録・注入に加え、成功時だけ借用画像を CPU 参照で更新する同期 stub。IFC/M2MF は FENCE/WAIT stub を経てから更新、失敗時は更新しない。M2MF は handles により vram/staging を選択、B8 の pack/unpack と再利用順を observe で検査。製品 B7 にはこの M2MF stub は存在せず、packet/実 WAIT 呼出しを照合する。host は全 GPU の emulator を作らない。

| 単独製品 | A1 が定義する依存 stub（H_接頭辞省略） | 使う参照/観測 |
|---|---|---|
| B0 objects | EMIT/FENCE/WAIT/FAULT/PAUSE/RESUME/STOP/ALLOC/FREE/DMA_NEW/GR_NEW/DEL | 全列・順序・所有・全段注入 |
| B1 surface | EMIT/FAULT/MATCH | 7形式・生存面・範囲・SF/GDI列 |
| B2 rop | EMIT/FAULT/FORMAT | rop_byte/ref_fill・全alu×mask |
| B3 fill | EMIT/FENCE/WAIT/FAULT/FORMAT/SPAN/SURFACE_CHECK/RAM_CHECK/SURFACE_SET/ROP | packet の payload を独立の期待値と照合 |
| B4 copy | 同 B3 | packet の payload を独立の期待値と照合・8方向 |
| B5 mono | 同 B3（ROPを除く） | packet の payload を独立の期待値と照合・128語境界・clip |
| B6 ifc | 同 B5 | packet の payload を独立の期待値と照合・padding・行再設定 |
| B7 m2mf | EMIT/FENCE/WAIT/FAULT/SPAN | packet の payload を独立の期待値と照合・2047分割・期限・失敗 |
| B8 transfer | FAULT/FORMAT/SPAN/SURFACE_CHECK/RAM_CHECK/IFC/M2MF/FENCE/WAIT | ref_ifc/ref_m2mf・staging・上記 force_m2mf/ifc≠NONE/§7 条件と EUNSUPPORTED の fallback |
| B9 scale | 同 B5 | packet の payload を独立の期待値と照合・format/比率/point/filter列 |
| B10 present | UPLOAD | 引数・force=true・失敗伝播 |
| C0 hook | FILL/COPY | clip後引数・HAL rc・非active |
- 対応表の EMIT=gf2_emit、FENCE/WAIT=gf2_fence_emit/wait、FAULT=gf2_fault_check、PAUSE/RESUME=gf2_fifo_pause/resume、STOP=gf2_stop、ALLOC/FREE=gf_inst_alloc/free、DMA_NEW/GR_NEW/DEL=gf2_dma_new/gr_object_new/object_del、MATCH=gf_vram_match、FORMAT=gf3_format_get、SPAN=gf3_span_check。残りの製品 stub は各 H_ 名を小文字にして gf3_ を付けた関数（SURFACE_CHECK 等も同じ）。MMIO/clock は全選択で利用可。未列挙の依存を各 B が勝手に増やさず A1 契約へ集約する。
- ref_* は成功した依存 stub が更新する像と B8/B10/C0 の期待像のために使う。B3〜B7/B9 の単独製品は packet を出すだけで比較対象の製品画像を生成しないため、ref_* との画像比較を合否にしない。CPU参照は妥当な入力だけを受け、失敗は assert、製品 rc の代行はしない。各参照は製品 format/ROP/helper を呼ばない。fill/copy は little-endian 各byteに `(P<<2)|(S<<1)|D` を評価、before は書込み先と別の呼出側保存像。rop は試験の独立 NVROP 表から渡すので clear/invert/set の既決 mask 非依存を維持。32bit は copyのみ、XRGB の X は比較から除外。
- mono は MSB-first/任意 RAM.pitch、clip∩r∩面、fg/bg のpacked色を展開。IFC は有効 w*cpp だけ各行へコピーし padding/矩形外を保存。M2MF は各行 rowbytes のみ、src/dst は非重複、off は各 RAM.ptr 相対。upload/download の期待像にも同じ byte参照を使い、分割算法は参照に再現しない。RAM末尾の読み越しは別の guard page/境界試験で検出する。
- host の ref_scale は RGB の point と同色入力だけを扱い、bilinear=true は同色入力のみ許す。範囲外の入力は assert。point は du=floor(sw*2^20/dw)、dv同様、sr開始+i*du/2^20 の floor を採り、clip外を保存する。製品 B9 の合否は独立 packet 値で判定する。
- T0 担当が §13 のゲスト実機試験用に CPU 期待像を自前で実装・所有する（host ヘッダーは呼ばない）。fill/copy/mono/IFC/M2MF は上記参照と同じ意味、bilinear/YUV の独立整数理想像は T0 側だけに置く: du=floor(sw*2^20/dw)、dv同様、位置は sr開始+i*du/2^20、point は floor、bilinear は周囲4点を20bit重みで補間し端を全面境界へclamp、clip外を保存。565 はbit反復で8bit展開、出力は上位bitへ量子化、Y8はR=G=B=Y、YUY2はY0/U/Y1/V、UYVYはU/Y0/V/Y1。YUV理想値はBT.601 limitedの整数式 C=Y-16,D=U-128,E=V-128、RGB=clamp((298C+409E+128)/256,(298C-100D-208E+128)/256,(298C+516D+128)/256)（負値はfloor）、alphaはARGB保持/他は255。
- T0 の scale 理想像は HW 丸めの仕様を新規断定しない。bilinear/YUV は既知単色・端・理想像との差を T0 が記録して §13 の実機受入で判定する。未測定の色変換誤差を任意の大きな許容値で成功にしない。製品に浮動小数点/色変換 fallback を追加しない。
- 手本対応: ROP は E:46–84 と X/nv_rop.h:303–318、copy は E:170–200,255–258、mono は F:105–124、IFC は E:312–374、M2MF は E:399–430 と X/nouveau_exa.c:256–281,341–365、SIFM は X/nv04_xv_blit.c:74–93,138–154。手本にはこの host API/CPU oracle はない。OS32 §13 の独立期待像・失敗試験のためだけに追加し、HWシミュレータや製品機能へ拡張しない。
- 共通土台は gf_command_host.h の「製品1 TU＋選択式依存」方式と gf_command_runtime_host.h の assert/memcpy/memset/exit。前者は GF2_HOST_B* 専用なので同時 include せず、後者を直接 include。各試験が自前 _start、-m32/-nostdlib、kstring が必要なら既存 freestanding 実装を補助 link、64bit除算は整数helperかtoolchainのfreestanding libgcc（libcなし）。A2 は stub選択・補助link・covers を一元登録する。
- C0a/C0b は既存 backend host fixture を維持、C1/C2 は生存予約→bind/publication、受付閉→窓解除→停止→release の結合、C4 は slot解除を実物で検証する。新たな製品 hook/dispatch/台帳APIは不要。B10/T0 は登録済み面を借りる。実装/試験コードを各所有段が完成させ、ビルド・変異実行は既定どおり PM 統合後。設計未決 0 件。

### 確定の補い (2026-11-04、claude-opus-5-5 の第 2 回、PM が反映)
- N1: B8 が IFC を選ぶ条件の並び (G2) に「かつ rowbytes×h < 16KiB (§7:145、xf86-video-nouveau の nouveau_exa.c:320 の `w*h*cpp < 16*1024`)」を足す。G1 の表の B8 の行も同じ条件にそろえる。

## 追記 (2026-11-04、B4 の欠け — 同じ VRAM を指す 2 面の copy)

> 出所: ~/os32-tmp/run/gf3-gaps2/ADDENDUM.md (astra 起草、Opus レビュー 1 回で確定、補いは PM)。§5 の「正規化する」はこの追記で置き換える。

## 追記 (2026-11-04、B4 の欠け — 同じ VRAM を指す 2 面の copy)

本追記は §5 の「異なる descriptor の alias を正規化」を以下の限定規則に置換する。G2・A0 の検査/予約契約と ABI は変更しない。未決 0 件。

### 手本と採用範囲
- 略号 X/N/V は §1 と同じ。`X/nv04_exa.c:170–200` は両 pixmap の BO を取得し、それぞれの pitch と BO 相対 offset=0 を SURFACE に設定する。同一 pixmap なら off/pitch は共通になる。同じ BO というだけで異なる pitch が安全とは読まない。
- `X/nv04_exa.c:255–258` は元の src/dst 座標と寸法をそのまま IMAGE_BLIT に渡す。PrepareCopy の dx/dy 方向引数は使わない。`:227–253` の別 pixmap 用 VBLANK 分割は alias 正規化の根拠ではなく、既決どおり採用しない。
- `X/nouveau_exa.c:88–98,113–143` は BO 先頭への map と面ごとの BO/pitch 確保。`V/nv04_fbcon.c:32–47` も同一表示面の座標を直接渡す。これらに任意の部分面 offset 差を商/余りで座標へ直す処理はない。
- `N/nouveau_bo.c:594–599,618–625` の固定配置の寿命に対応して、OS32 は生存予約を検査から reference 完了まで保持する。予約全体は幾何情報を持たないため、予約 off/bytes から親面を推測しない。

### 判定と投入
1. 必須ポインタ・ready/epoch/RUN/fault を既決どおり検査。w=0 または h=0 はその後、面を参照せず無投入成功。非空では以下を ROP を含む最初の emit より前に完了する。
2. 元の src/dst を各々 `gf3_surface_check(...,GF3_USE_2D,...)` で全面および要求矩形について検査する。dst 矩形は `{dx,dy,sr.w,sr.h}`。両 fmt の一致を要求する。全面検査は G2 の surface_set が後から検出する不正を先に排除するために必要。
3. 重なりとは「コピーする有効 byte」の共通部分があること。検査済み span ごとの行区間 `[span.off+i*pitch, span.off+i*pitch+rowbytes)` のいずれかが相手の行区間と交差する場合とする。descriptor.bytes や行間 padding の交差だけでは重なりとしない。
4. 行先頭は単調増加する。src/dst の先頭行から、交差すれば重複、交差しなければ先に終わる側の行を進め、片側が尽きれば非重複とする（O(src.rows+dst.rows)、追加配列なし）。末尾加算は検査済み範囲内、最終行後の先頭は計算しない。
5. 非重複なら、同一予約/異なる予約、同じ/異なる off/pitch を問わず、元の両 descriptor と座標のまま 1 矩形を投入する。
6. 重複なら、両 descriptor の `reservation_id,generation,off,pitch,fmt` が全て等しい場合だけ許可する。別の構造体でもこの条件なら既に共通座標系であり、「正規化できる」はこの恒等変換だけを意味する。width/height/bytes の一致は不要だが、各々が手順 2 を満たすこと。
7. 重複して手順 6 を満たさないものは `GF3_EUNSUPPORTED=-305`。off 差が 1 byte、1 行、複数行、cpp の倍数であるかを問わず off が異なれば拒否し、pitch 不一致も拒否する。差の除算・剰余、行折返し、行分割、一時面、CPU/M2MF への内部 fallback は追加しない。
8. 許可時は元の src/dst を `gf3_surface_set(c,src,dst,false,dl)` に渡す。bytes/width/height/予約 ID/世代を一切合成・拡張しない。重複時の共通 off/pitch は元の値、座標も元の値。B1 の全面検査を迂回しない（`gf3-b1/hwdev/gf/accel_surface.c:86–101`）。
9. §5 の ROP→SURFACE→BLIT:0300 の 3 語→reference emit/wait 1 回を維持する。上下左右/斜め/同位置も 1 矩形で、座標を右下へ移さず pitch を反転しない。dl は操作全体で不変、部分投入後の失敗は再送しない。
- NULL は EINVAL=-301、fmt 不一致/2D 非対応は EFORMAT=-302、元の面/矩形/packed field/予約包含の不正は ERANGE=-303。古い ID/世代等は match の ESTATE=-304 を含め原 rc を返す。元の検査失敗を EUNSUPPORTED に置換しない。全 rc は既決どおり last_rc に保存する。
- off=0 と off=1、pitch=16 の INDEX8 部分面が重なる例は EUNSUPPORTED。呼出側が共通の正規面 descriptor を既に所有する場合は、それを両引数に用い、部分位置を座標として渡す。B4 が予約から正規面を生成することはない。

### B4 host 試験（記述例、ビルド/実行は PM 統合後）
- 以下は生存予約 `[0,128)`、同一 ID/世代、INDEX8、bytes は全面の最後の有効 byte までを基本とする。拒否例は ROP/SURFACE_SET/EMIT/FENCE/WAIT が全て 0 回であることを照合する。
- 正規化可能：別構造体 A=`{off=0,bytes=64,pitch=16,width=16,height=4}`、B=`{0,48,16,16,3}`。sr=(0,0,8,2)、dst=(1,1) は成功。SURFACE_SET に元の全欄を渡し、BLIT payload は `[0,0x00010001,0x00020008]`。同一面の 8 方向・同位置も独立期待値で照合する。
- 正規化不能：A と B=`{1,63,16,15,4}`、sr=(0,0,8,2)、dst=(0,0) は EUNSUPPORTED。B=`{16,64,16,16,4}` と `{17,63,16,15,4}` でも同じ矩形で行をまたぐ重複を拒否する。B=`{0,56,20,16,3}` の異 pitch 重複も拒否する。
- 非重複：A と B=`{1,63,16,15,4}`、sr=(0,0,1,2)、dst=(0,0) は行区間が接するだけなので成功（包絡は交差）。異 pitch の B=`{64,56,20,16,3}`、sr=(0,0,8,2)、dst=(0,0) も成功し、元の off/pitch を保持する。別の非重複予約でも成功する。
- 境界：A の sr=(15,3,1,1)、dst=(14,3) は byte 63 までで成功、sr.x=16,w=1 は ERANGE。bytes=63 の A、pitch=65536、全面 height=65536 は小さな要求矩形でも ERANGE。古い世代は ESTATE、fmt 不一致は EFORMAT、ゼロ矩形は必須ポインタ/状態検査後に無投入成功。
- B4 単独 host は G1 の SURFACE_SET/ROP stub の引数を observe 内で照合し、実 B4 の BLIT packet と fence/deadline/失敗伝播を検証する。stub は SF packet を生成しない。B1 結合で SF の両 offset/pitch と全面検査を確認し、実機の重なり結果は T0 がコピー前保存像から作る期待像と照合する。

### 確定の補い (2026-11-04、claude-opus-5-5、PM が反映)
- R1: rc の順は src の検査 → dst の検査 → fmt の一致で、最初の rc を返す (B1 の gf3_surface_set、accel_surface.c:91-97 と同じ順)。
- R2: pitch=65536・height=65536 の例は span_check 経由の ERANGE。pitch の 16bit の上限は B1 の試験が持つ。
- R3: 非重複の例の前提に、[128,256) の予約 (ID・世代は別) を足す。
- R4: 同一面の 8 方向・同位置の例は、A で sr=(4,1,4,2)、dst を (3..5, 0..2) の 8 点と (4,1) とし、payload を列挙する。T0 には「重なる copy は同じ descriptor で」。

## 追記 (2026-11-04、結線の段の欠け C1/C2/C4)

> 出所: ~/os32-tmp/run/gf3-gaps3/ADDENDUM.md (astra 起草、Opus レビュー 1 回、確定の補い F1〜F7 は PM が反映 — 下の補いが本文より優先)。

## 追記 (2026-11-04、結線の段の欠け C1/C2/C4)
本追記は欠け 5 件を補う。シングルタスク・協調前景で直列実行し、待ちから描画・map・停止へ再入しない。heap、lock、refcount、追加 HW 監視は設けない。新しい整数戻り値は i32。凍結宣言は削除せず追加する。
参照 N=`/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau`（c03c1a1）、O=`/home/hight/os32-v3`。以下の行番号は保存済み実物。

### C1-2 / C2-1 — C1 の所有物と C2 の停止境界
新内部ヘッダー `hwdev/gf/accel_service.h` は `accel.h` を include、`struct gf_production;` を前方宣言し、以下を宣言する（宣言担当 A0、実装 C1/service.c）。
```c
struct gf3_service {
    struct gf_production *production;
    const struct gf3_test_ops **slot;
    struct gf_vram_reservation_node scan_node, test_node;
    struct gf3_surface scan, test;
    struct gf3_test_ops ops;
};
i32 gf3_service_bind(struct gf_production *p, const struct gf3_test_ops **slot);
struct gf3_service *gf3_service_get(struct gf2_chan *ch);
i32 gf3_service_start(struct gf3_service *s, u32 dl);
i32 gf3_service_close(struct gf3_service *s);
i32 gf3_service_fini(struct gf3_service *s, u32 dl);
void gf3_service_invalidate(struct gf3_service *s);
i32 gf3_service_release(struct gf3_service *s);
i32 gf3_service_stop(struct gf3_service *s);
i32 gf3_service_recover(struct gf3_service *s, u32 dl);
```
C1 は全欄零の static `struct gf3_service` を 1 個所有。production/slot は借用、node の owner は各 node 自身の番地。ctx は A3 の `production.accel` のみで複製しない。get は登録済み `&p->chan` と一致する時だけその service を返し、NULL/診断 ch/未 bind は NULL（誤りコードなし）。
追加結線段 C1b は `drivers/video/geforce/geforce_body.c:37–40` で `gf3_service_bind(&production,context->accel_out)` を呼ぶ。bind は必要ポインタ NULL で GF3_EINVAL=-301、別 p/slot への再 bind は GF3_ESTATE=-304、同じ組は無変更 0。初回だけ両借用を保存して slot を NULL にする。登録失敗は module 初期化失敗へ伝播。
start は未初期化 accel を対象とし、ready 済みは ESTATE。F2 RUN 後、台帳 init→既決 scan/test reserve→gf3_init→ctx/scan bind→slot=&s->ops。既存予約は失敗後の再試行用に保持し、同じ node を二重 reserve しない。surface は予約値から構築し、ops.ctx/test_surface は常駐所有物を指す。C0a の情報取得、試験面配置 N2、F4 の所有移動 N3 は既決どおり。
close は slot=NULL→gf_hook_scan_bind(false,NULL)→全窓 close（下記）。受付を閉じるために fini より先に accel.ready を潰さない。借用 dispatch/ctx は呼出し間で保持禁止。fini は ready かつ ch が RUN・fault 未検出・epoch 一致の時だけ gf3_fini、それ以外は 0 で F2 停止へ渡す。
invalidate は「旧 F2 の停止完了を確認済み」の内部口。旧 obj[12]/notify/notify_storage/notify_off/epoch を零化し ready=false、ch の借用と last_rc は保持。object_del/free を呼ばず、生存 inst node を零化しない。release は試験→表示の生存 node を gf_vram_release し各成功分の surface を零化、ID/世代の履歴は残す。未登録分は無操作。
bind/get/invalidate 以外の service 口は NULL s で EINVAL、開始に必要な RUN/epoch/所有状態不成立は ESTATE、成功 0、下位 F1/F2/F3 の負 rc は原値。下位失敗を accel.last_rc に保存。invalidate の NULL は無操作。release の失敗は未解放分を保持し、再試行時は生存分だけ扱う。
C2 は command_get 後 get(ch) し、NULL なら既存の停止列、非 NULL の時 close→fini→既存 gf2_stop→既存 foundation_stop/restore/quiesce 列を実行。close 失敗では F2/F1 停止へ進まず、予約・adapter・未解除窓を保持。fini 失敗でも F2 停止は試み、最初の rc を保存する。
F2 停止完了は rc==0 だけでなく ch->state==GF2_OFF で判定（command.c:60–72,142–156 は fault rc を残して OFF にできる）。OFF 確認後だけ invalidate。F2 未停止なら予約を保持し、既存 revoked 時の外側 route00/decode-safe 処理は妨げない。
表示の restore/quiesce まで成功して利用が終わった時だけ release。表示停止失敗では予約を保持。C2 の既存返値 0/-1 と最初の display.last_rc を維持し、engaged=false でも保持予約の後処理を飛ばさない。slot はすべての失敗で閉じたまま。start の途中失敗もこの停止列で巻き戻す。
C1-2 の対応は N/nvkm/engine/fifo/uchan.c:272–298 の block/remove/unbind→bind/allow/insert、C2-1 は同列と `N/nvkm/engine/fifo/nv04.c:41–99` の stop/start。OS32 は単一所有者を既存 command_get の ch で検索し、production キャストや新 display callback は不要。node の解放は N/nouveau_bo.c:618–629 の unpin 相当で、TTM 所有を既決常駐予約に置換する。

### C1-1 — production の停止・復旧入口
追加結線段 C1t は `hwdev/gf/command_test.c:173–195` を所有。production 分岐の GF2T_STOP は service_stop、GF2T_RECOVER は service_recover、GF2T_START は F2 start 成功後 service_start へ結線。production で service 未 bind は GF3_ESTATE、診断 ch は従来の gf2_* を直接呼ぶ。revoked 拒否は維持する。
service_stop は `gf_display_stop(&s->production->display)` を呼び、成功 0、失敗は保存した display.last_rc（無ければ GF2_ESTOP=-210）を返す。単なる F2 停止で表示予約を解放しない。通常 video_stop と診断 STOP は同じ C2 列へ入る。
recover は close→gf2_stop→OFF 確認→gf2_recover→旧 gf3 記述子 invalidate→旧予約 release/再 reserve→gf3_init→scan 再 bind→slot 再公開。事前 gf2_stop は停止完了と再起動失敗を分けるためで、gf2_recover 内の stop は OFF のため無操作。新しい F2 API は足さない。
事前 stop が OFF に達しなければ原 rc（無ければ ESTOP）で終了し全所有を保持。OFF なら過去の fault rc は復旧を妨げない。gf2_recover が失敗しても旧記述子は invalidate 可だが予約を保持し、再初期化・公開しない。新 F2 の残存資源は F2 が保持する。次の再試行も同列。
gf2_recover 成功後だけ旧予約を更新して古い surface の ID/世代を失効させる。gf3_init/予約更新の途中失敗は上記停止列へ送り、未解放分を保持、最初の rc を返す。復旧後 start はこの更新済み予約を使う。通常 start は同じ初期化・公開 helper を共用する。
service_start/recover の dl は操作全体の絶対期限。C1/C2 の通常呼出しは既存 clock_us の現在時刻+GF3_TIMEOUT_US、診断は q->deadline_us を渡し、更新しない。clock 失敗は GF2_EIO=-204、停止自体は F2 に渡せるが fini の新 fence は発行しない。
対応: N/nvkm/engine/fifo/uchan.c:272–298、同 runl.c:68–82,115–128,132–141 の受付停止→回復→再開。runlist の回復は後代 GPU の例で NV44 の reset 手順として移植しない。OS32 は既決の明示同期 gf2_recover（O/hwdev/gf/command.c:159–169）を使い、workqueue・多 channel・自動再送を足さない。事前 stop は同関数の再起動失敗が ESTOP も返す実契約を区別するため。

### C2-2 — 任意の呼出側窓を持ち主に閉じさせる
A0 は `hwdev/gf/vram_view.h` の既存 struct 末尾に下記 4 欄だけ追加する。完全形（既存 map/unmap の引数と test_ops の型は不変）:
```c
struct gf_vram_view {
    struct recover_bar_extent extent;
    volatile u8 *va, *map_base;
    u32 map_bytes, bytes;
    bool mapped;
    const struct gf_instmem_map_ops *map_ops;
    void *map_owner;
    struct gf2_chan *ch;
    struct gf_vram_view *next;
    void *close_owner;
    i32 (*close)(void *owner, struct gf_vram_view *view);
};
i32 gf_vram_views_close(struct gf2_chan *ch);
```
V0 は成功 map 済み窓の static head を 1 個持つ（既決の単一 production 台帳に対応）。呼出側は零初期化後、map 前に close_owner/close を設定する。close は必須、close_owner は NULL 可。map 成功時だけ ch/next を記入して連結、map 失敗は追加前の全欄と out を保持。close 不在は既存不正入力 GF2_EMAP=-203。
unmap 成功時だけリストから除去して従来どおり全欄零化、失敗は欄・連結を保持。再 map 前に callback を再設定する。view/callback/close_owner の記憶域は成功 unmap まで生存・移動不可。局所 view の unmap 失敗で帰還してはならず、T0 は再試行可能な static 窓を使う。
views_close は ch の各窓の close(close_owner,view) を呼ぶ。callback は CPU 使用と posted write 排出を終え、gf_vram_view_unmap の rc を返し、成功時には所有者の借用 VA も捨てる。自分の窓だけを解除し、別窓の操作・新 map・再入はしない。反復は解除前に next を保存し、最初の負 rc で中断する。
views_close は NULL ch に EINVAL、初期化済み台帳/ch 不一致に ESTATE、初期化前の空リストを含め空または全解除で 0、callback の負 rc をそのまま返す。既に閉じた窓は再試行時に呼ばない。失敗した窓・予約・adapter は保持。map は GPU 完了を待たない既存契約のまま、callback に GPU 待ちを追加しない。
T0 は内部に `struct gf3_test_view { struct gf_vram_view view; __typeof__(&gf_vram_view_unmap) unmap; };` を static 所有し、map 直前に取得した ops->unmap を保存する。`static i32 gf3_test_view_close(void *owner, struct gf_vram_view *view);` は owner の同レコードから unmap(view) を呼び、成功時に保存ポインタを捨てる。slot 閉後も解除できるよう、この関数ポインタだけは窓の借用寿命まで保持可（module は既存契約どおり pinned）。表/ctx/面の保持許可ではない。
V0 は map/unmap/列挙、T0 は試験窓 callback、F4 は cursor、F5 は一時窓 callback の唯一の所有者。F5 save の受付閉→持ち主による窓閉→コピー→再開も同口を使い、予約保持契約は変更しない。A0/V0/T0 と既存窓利用者の追随を C2 再開の前提にする。
対応: N/nouveau_bo.c:669–689 の BO 内 kmap/kunmap。OS32 は任意の呼出側 view を受けるので、BO が持つ窓所有を intrusive list と持ち主 callback で表す。既存 map_owner は下位 mapper の所有者であり流用しない。固定件数表、heap、強制解除、TTM reserve lock は加えない。

### C4-1 — kernel slot の取得
A0 が `drivers/video/geforce/geforce_module.h` の既存前方宣言の直後に `const struct gf3_test_ops *geforce_accel_ops(void);` を追加し、C4 が同 .c に `const struct gf3_test_ops *geforce_accel_ops(void) { return registered ? accel_slot : 0; }` を実装する。未登録/未公開/停止/失敗は NULL、別の誤り値はない。
slot は kernel の既存 static accel_slot、表は module/C1 が所有。T0 は各呼出し直前に取得し、NULL は利用不可、帰還後は表・ctx・面の借用を保持しない。C4 は load/start 失敗・abort 前に slot を NULL にし、detach 拒否でも再公開しない。既存 Context 欄と OPS_VERSION=4 は維持する。
PM 登録段が `kernel/module/kdebug_imports.def` に symbol=geforce_accel_ops/source=drivers/video/geforce/geforce_module.h を既存 geforce_foundation_ops と同じ方式で追加し、生成物・import 試験を追随する。geforce 側 import は不要、HWDEV に未使用 import は足さない。C5 の header 確認 0 行はこの宣言追加のみ A0 に移す。版 4→5 と feature5_out 等は F4 C3 のまま。
対応: N/nvkm/engine/fifo/uchan.c:276–297 の利用可否切替。nouveau に OS32 module slot ABI の対応物は無く、O/drivers/video/geforce/geforce_module.c:20–27 の foundation 取得口と同じ借用公開を用いる。
未決: 0 件。実装の追加所有段は A0（宣言）、C1b（body）、C1t（command_test）、V0/T0（窓追随）、PM（登録）。各段は直列統合し、検証は通常停止・fault/OFF・ESTOP 保持・復旧各段失敗・窓解除失敗/再試行・診断 ch 分離・slot 再取得を結合試験で照合する。

### 確定の補い (2026-11-04、claude-opus-5-5、PM が反映 — 本文と食い違えばこちら)
- F1: `service_stop` は close → fini → gf2_stop → OFF の確認 → 記述子の invalidate までにし、予約は保持する。表示の停止 (`gf_display_stop`) と予約の release は C2 (video_stop・gf_failure) だけに残す (診断の GF2T_STOP が表示全体を止めないように、層 2 の command_test.c:193 と同じ)。
- F2: recover の途中失敗 (gf3_init・予約の更新) は「slot を閉じたまま予約と ctx を保持して rc を返す」。片付けは次の C2 の停止が行う (fini は ready でないので飛ばし、gf2_stop が inst を全部解放する)。
- F3: recover では scan の幾何 (pitch・width・height・phys) を残し、予約の ID と世代だけを付け替える。bind の時に取った幾何は `gf3_service` の欄に写して保持する (`geforce_backend_framebuffer_info` は video_start の中でしか使えない、accel.h:200-206)。
- F4: recover の公開 (hook と slot) は display.engaged のときだけ。engaged でなければ F2 の復旧だけ (層 2 と同じ)。`!engaged` で入った停止の後処理は「ch が OFF のときだけ release」。
- F5: V0 の map の成功は、呼出側が先に入れた close と close_owner を保持し、ch と next だけを足す (今の vram_view.c:147 の全欄の上書きを直す — V0 の段で)。
- F6: 凍結の例外として、map の前提に close を足す (close が無ければ GF2_EMAP)。V0 の host 試験と既存の窓の利用者は追随する。
- F7: revoked のときは窓の close の失敗を記録して先へ進む (quiesce.c:18-20 の「cleanup の行は全部試す」に合わせる)。

## 追記 (2026-11-04、C1 の欠け — 試験面の幾何、PM 決定)
- `gf3_service.test` (T0 が借りる試験面) の幾何は表示面に合わせる: `fmt` = 表示面の fmt、`width` = 表示面の width、`pitch` = 表示面の pitch (64B の整列を満たさないときは 64B に切り上げ)、`height` = min(表示面の height, floor(試験面の予約の bytes / pitch))。予約の範囲は結線の追記の N2 (表示の予約の末尾の次の 4KiB 境界から B 以下の最後の 4KiB 境界まで)。height が 0 なら GF2_ENOSPC で試験面を公開しない (表示は続ける)。根拠: T0 の比べ方 (§13) は表示面と同じ形式・幅の面への描画と読み戻しで、別の幾何は要らない。

## 追記 (2026-11-04、T1 の欠け — T0 の入口と case、PM 決定)
- T0 の公開の入口は `int gf_accel_test(u32 case_id)` (T0 の実装、kdebug/gf_accel_test.c)。case_id は 0 init / 1 fill / 2 copy / 3 mono / 4 ifc / 5 upload / 6 download / 7 scale / 8 present-m2mf / 9 all。宣言は T0 と同じ置き場のヘッダー (kdebug/gf_accel_test.h、T1 が足す) に置く。
- 既存の GF の試験の入口 (kdebug/gf_command_test.c) に新しい case を 1 つ足す: `GF2_VBLANK` の次の番号を `GF3_ACCEL` とし、引数の 1 語を case_id として `gf_accel_test` へ dispatch する。追従する kdebug/gf_command_wire.h (case の番号)・kdebug/gfinit.c の上限の検査 (GF2_VBLANK まで → GF3_ACCEL まで)・shell の側の case の名前の表は、T1 の段が持つ (他の段は触らない)。
- build/hwdev.mk の HWDEV_GF への gf_accel_test の登録は PM が統合の段で足す。
