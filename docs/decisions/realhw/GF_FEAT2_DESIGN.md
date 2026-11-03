# GeForce 6200 (NV44A) 層 2 (コマンドの投入) — 確定設計: PFIFO チャネル・プッシュバッファ・DMA object/RAMHT・PGRAPH・完了待ち

> 出所: ~/os32-tmp/run/gf-feat2/DESIGN.final.md (astra 起草 r1〜r2、Opus レビュー 2 回、条件は PM が反映)。
> 状態: **現行 (2026-11-02)** — 段の状態は票 TASK_GF_FEATURES が持つ。層 1 は GF_FEAT1_DESIGN。

# GeForce 6200 / NV44A 層2 詳細設計 r2
## r1→r2 の変更
- F1: S1を削除。層1を変えず、前景の件数差検出→採取→IRQ要求閉→gate閉へ変更。
- F2: A0に共有stateの全フィールドとRAMHT/platform/DMA opsの型付き境界を確定。
- F3: PCI dmaobj作成前にNV44 FB初期化の2 writeを追加。
- F4: host骨格をA、部品試験を各Bへ分割。CHECK・正常対照・実機台帳を追加。
- F5: NOP初期化直後のfree=max−curを明記。
- F6: reference method0050のsubchannel=0を明記。
- F7: BAR1部分extent・長さ検査・map状態と02_memoryのPT予算段を追加。
- F8: RAMHT更新時のpause/start手順を具体化。
- 作成: gpt-6-astra、2026-11-02。対象: Ra266 PCI 0:13.0、10de:0221、PCI 経由。GUI 接続なし。シングルタスク・協調切替、整数演算。
- O=`/home/hight/os32-v3`、R=`/home/hight/os32-tmp/refs/geforce6200`、N=`R/nouveau/drivers/gpu/drm/nouveau` (指定 c03c1a1)、X=`R/xf86-video-nouveau/src`、L=`R/xf86-video-nv/src`。以下の番地・値は16進、サイズの KiB は1024B。
- 表内の短縮 `fifo/`=`N/nvkm/engine/fifo/`、`gr/`=`N/nvkm/engine/gr/`、`fb/`=`N/nvkm/subdev/fb/`。§5の裸の `nv40.c` / `ctxnv40.c` / `ctxnv40.h` は同grディレクトリ。
- F1=`O/docs/decisions/realhw/GF_FEAT1_DESIGN.md`。確定済みの関数・配置・停止順を使用する。層1への追加契約はない。本書は実装設計であり、実機確認済みという主張ではない。

## 1. 実物の確認 (file:line)
| 確認 | 根拠と結論 |
|---|---|
| 機種分岐 | `N/nvkm/engine/device/base.c:677–698 nv4a_chipset`: imem=nv40、fifo=nv40、gr=nv44、dma/mmu=nv04。NV44 専用 GART を選ばない。 |
| 現状と範囲 | `O/docs/tasks/realhw/TASK_GF_FEATURES.md` §1–4、比較 `GF_FEATURE_COMPARE.md:32–39` の項目5・6。層1も現在は設計確定・実装待ち。 |
| OS32 の入口 | `O/hwdev/gf/internal.h:35–44 gf_io` の read/write/timing、`:195–205` owner、`production.h:31–46` が所有元。`O/drivers/video/geforce/gf_present.c:18–57` は CPU コピーと posted-write 排出。GPU 完了ではない。 |
| 固定配置 | F1 §4 (`:116–135`): RAMHT off=10000/8000B、RAMRO off=18000/実使用200B、RAMFC off=20000/実使用1000B。pad込み予約は各8000/8000/20000B。可変instは40000以降。 |
| DMA RAM | `O/mm/dma_pool.h:76–81,118–129`: dma_buf={pa,va,size}、dma_alloc/free/mark_leaked。`O/include/memmap.h:261–269`: WB共有64KiB、物理7f0000。pgallocの仮想連続をDMA連続と扱わない。 |
| 82557 の先例 | `O/drivers/net/e82557/e82557.c:691` 確保、`:229,569` 装置へpa、`:762,812` 解放。`lan_module.c:116–129` に常駐adapter。GeForceもVAを装置へ渡さない。 |
| メモリ定数 | `O/include/memmap.h:147–150` は640×480×8bit裏面、GeForce専用push定数は検索範囲に無い。307200B表示域をpushに流用しない。 |
| 単一チャネルの手本 | `X/nv_accel_common.c:624–656 NVAccelCommonInit`: 1 channel＋pushbuf群。object handleの0はHW chid指定ではない。`N/nvkm/engine/fifo/nv04.c:510–513` は最後のchidだけを無効値に予約。 |
| 旧nvとの区別 | `L/nv_hw.c:938 NVLoadStateExt`: `:983–1053` NV40用静的PRAMIN→`:1384–1419` PFIFO。配列添字はdword単位。RAMHT=03000100、DMA_INSTANCE=1213は旧配置専用でコピーしない。 |
- 実物との食い違いは **0件** (r1 の 1 件は S1 の口の不足で、F1 により S1 を削ったため解消)。
- [C1]〜[D4]/[HW3]準拠。内部GNU11、公開宣言が要ればC89、kstring、定数3層、PA/VA変換集約。MIT部分の著作権/許諾文を保持し、GPLのLinuxサービス本体を移植しない。

## 2. (a) PFIFO 初期化・チャネル
- **chid=0**、NV40_CHANNEL_DMA=406eを1本。31は無効マーカー、1は不要。根拠: `N/nvkm/engine/fifo/nv10.c:87–89`=32本、`nv04.c:510–513`。協調呼出しのみ、IRQ内の投入は禁止。
- 全体順: F1 start済み→FIFO/GR要求IRQを閉じる→FIFO停止ゲート→NV44 FB初期化→push/DMA/GR文脈を確保→GR初期化→RAMFC/RAMHT構築→PFIFO初期化→文脈bind→MODE bit0→IRQ開→初回reference→RUN。
- NV44 FB初期化: `N/nvkm/engine/device/base.c:682` のnv44_fb_new→`fb/nv44.c:48–53 nv44_fb_init`に従い100850=80000000→100800=1。PCI dmaobj生成より前、recoverでも実行。tileは§5のsnapshot転記を維持。
| 順・前提 | 手本の列 (N/以下) | OS32の列 / 誤りコード |
|---|---|---|
| 1 無投入でゲート閉 | `nvkm/engine/fifo/nv04.c:53–60 nv04_chan_stop` | 2500=0、3220 bit0=0、3200=0、3250 bit0=0。既存仕事がある場合は§6のdrainが先。EIO。 |
| 2 RAMFC | `fifo/nv40.c:39–58 nv40_chan_ramfc_write` | slot=ramfc.off+0×128、128Bをzero。+00/+04=push GPU base B、+0c=push_dma.off>>4、+18=30086078、+3c=0001ffff。残り0。ENOSPC/EIO。 |
| 3 固定表の番地 | `fifo/nv40.c:190–217 nv40_fifo_init` | 2040=ff→2044=2101ffff→2058=1→2210=03030100 (bits=12)→2218=180→2230=0→2220=((V−80000+ramfc.off)>>16)\|30000。VはF1のVRAM容量。 |
| 4 無効chidと開放 | 同`:221–228` | 3204=31→2100=ffffffff→2140=ffffffff→3200=1→3250=1→2500=1。ACK/enableはF1 IRQ所有者へ委譲、wanted maskも同期。 |
| 5 GR bind | `fifo/nv40.c:125–164 nv40_ectx_bind`、`fifo/chan.c:52–62 nvkm_chan_cctx_bind` | channel未開始で2500 bit0=0→active chid=(3204&31)が0なら32e0=ctx.off>>4→RAMFC+38に同値→2500 bit0=1。 |
| 6 開始 | `fifo/nv04.c:92–99 nv04_chan_start` | 2504 bit0=1。起動時に他chidを有効にしない。初回PUTでHWが0へ切替える。直接CACHE1へPUTを流し続ける方式にしない。 |
| 7 user aperture | `fifo/nv40.c:95–99 nv40_chan_userd`、`N/nouveau_chan.c:440–443` | BAR0+c00000+chid×1000、PUT=+40、GET=+44。BAR0の既存map範囲を検査し不足時はadapterで追加UC map。EMAP。 |
- 数値の定義元: `N/nvkm/engine/fifo/regsnv04.h:17–22,35–124`。+18のfetch値は同`:60,81,95` (128B trigger/128B fetch/8 requests)、little endian。
- 旧nv差分: `L/nv_hw.c:1389,1395,1400–1402` のchannel0直載せ/小表ではなくnouveauの128B RAMFCと32KiB RAMHTを採用。理由: F1配置と自動context切替を一致させる。
- 前提の差: IRQは初期化表の途中で実配送させず、最後に一括開放。理由: F1の共有IRQ/tickが半完成のchannelを観測しないようにする。

## 3. (b) DMA pushbuffer・メソッド境界
| 項目 | 手本の列 | OS32の列 / 誤りコード |
|---|---|---|
| 置き場 | `N/nouveau_chan.c:167–172,211–227 nouveau_channel_prep` はVRAM選択可 | **VRAM 64KiB、4KiB整列**。B=align_down(V−R−10000,1000)、[B,B+10000)を一般VRAMから予約、B>=4b000。B+10000<=BAR1.lengthをオーバーフローなしで照合。BAR1を複製したextentのbaseをbase+B、lengthを10000とし、既存map_ops.map(owner,&extent,10000,&va)でUC map。F1 inst allocatorには入れない。ENOSPC/EMAP。 |
| DMA範囲 | 同`:185–189,224–227` | push用FROM_MEMORY/RW/VRAM/start=0/limit=V−R−1。PUT/GETはBからのVRAM絶対byte offset。inst.off/PCI物理番地とは別。 |
| 初期値 | `N/nouveau_chan.c:439–478`、`nouveau_dma.h:45` | N=16384 dword、max=N−2、SKIPS=32。cur=put=0から先頭32語NOP(0)を積む→free=max−cur (`nouveau_chan.c:468`)。最初のfenceも積みkickしてGETをSKIPSより先へ進める。 |
| 空き | `N/nouveau_dma.c:41–66 READ_GET`, `:70–147 nouveau_dma_wait` | get=(GET−B)/4。GETが範囲外/非整列、またはget<32なら期限内再読。get<=curならfree=max−cur、get>curならfree=get−cur−1。free>=必要語数で確保。ETIME。 |
| wrap | 同`:110–143` | 末尾が足りない時curへB\|20000000の旧形式jumpを1語置く→get>32を待つ→WRITE_PUT(32)→cur=put=32→free=get−cur−1、外側ループへ。NOP領域を書換えない。 |
| kick | `N/nouveau_dma.h:82–96 WRITE_PUT/FIRE_RING` | compiler barrier＋CPU memory barrier→push[0]読戻し→user PUT=B+4×cur、put=cur。同値なら何もしない。UC読戻しを省かない。EIO。 |
- VRAM選択の理由: 64KiBのDMA poolをpushだけで使い切らずPCI転送へ残す。手本のGART既定値からの変更であり、PCI DMA機能の省略ではない (§4)。AGP→PCIブリッジをAGPホストのGARTと扱わない。
- wrap前に未kickの語がある場合は通常kickしてからGET待ちへ入る。予約はパケット全体を連続確保し、途中でwrapしない。free計算はdword、HW値はbyte。
- 空き待ちは§6の同一期限を渡す。待ちの途中にfaultが入れば中断。CPUはIRQを許可、長時間CLIはしない。一般の協調yield先から同一channelへ再入しない契約。
```c
/* command.h: すべてmodule内部ABI。構造体/実体はproductionが1個所有。 */
enum gf2_rc { GF2_OK=0, GF2_EINVAL=-201, GF2_ENOSPC=-202,
  GF2_EMAP=-203, GF2_EIO=-204, GF2_ETIME=-205, GF2_EFAULT=-206,
  GF2_ESTATE=-207, GF2_EEXIST=-208, GF2_ECLASS=-209, GF2_ESTOP=-210 };
enum gf2_state { GF2_OFF, GF2_INIT, GF2_RUN, GF2_DRAIN, GF2_FAULT };
struct gf2_fence { u32 epoch, seq; };
struct gf2_object { u32 handle, cookie; struct gf_inst inst; u16 class_id; };
struct gf2_dma_buf { u32 pa, size; void *va; }; /* 常駐dma_bufをadapterで変換 */
struct gf2_dma_ops {
  int (*alloc)(u32 bytes, u32 align, u32 mask, struct gf2_dma_buf *);
  int  (*free)(struct gf2_dma_buf *);   /* dma_free の rc を返す (dma_pool.h:118-129) */
  void (*leak)(struct gf2_dma_buf *);   /* free が失敗したら dma_mark_leaked */
};
const struct gf2_dma_ops *geforce_dma_ops(void); /* 単一import、常駐表 */
struct gf2_fault {
  bool latched; u32 epoch, chid, handle, put, get, reference;
  struct gf_irq_stats first, latest; /* first固定、latestは後続件数/OR mask */
  u32 caches, push1, cache_get, pull0, method, data, dma_state, dma_fetch;
  u32 dma_push, dma_dcount, gr_trap, gr_method, gr_data, gr_handle;
};
struct gf2_chan {
  struct gf_foundation *f; enum gf2_state state; unsigned heads;
  u32 B, cur, put, free, max, seq, epoch; volatile u32 *va;
  struct gf2_fault fault; struct gf_irq_stats seen; u8 bitmap[512];
  struct gf2_object push_dma, vram_dma, pci_dma; struct gf_inst grctx;
  const struct gf_instmem_map_ops *map_ops; void *map_owner;
  struct recover_bar_extent bar1, push_extent; u32 map_bytes;
  bool mapped, vram_reserved, staging_live; int dma_rc, last_rc;
  const struct gf2_dma_ops *dma_ops; struct gf2_dma_buf staging;
}; /* grctx scratchはB4の作業領域、object寿命は所有者固定。 */
int gf2_fault_check(struct gf2_chan *); /* 前景のみ、正常0/fault EFAULT */
int gf2_start(struct gf2_chan *, struct gf_foundation *);
int gf2_stop(struct gf2_chan *); /* drain -> channel stop -> F1 stop */
int gf2_recover(struct gf2_chan *); /* 明示再開、未完了仕事を再送しない */
int gf2_emit(struct gf2_chan *, u8 subc, u16 method,
             bool noninc, const u32 *data, u16 count, u32 deadline_us);
int gf2_kick(struct gf2_chan *);
int gf2_fence_emit(struct gf2_chan *, struct gf2_fence *out, u32 deadline_us);
int gf2_fence_wait(struct gf2_chan *, struct gf2_fence, u32 deadline_us);
/* emit: reserve(count+1)成功後だけheader/dataを書きcur/freeを更新。 */
/* header = (noninc ? 0x40000000U : 0) | count<<18 | subc<<13 | method; */
/* bindはemit(ch,subc,0,false,&object.handle,1,deadline)。 */
```
- encoding根拠: `N/include/nvif/push006c.h:50–65` / `nvhw/class/cl006c.h:33–38`。subc<=7、methodは4B整列かつ<=1ffc、1<=count<=2047、increment時末尾<=1ffc。違反EINVAL、非RUNはESTATE (内部drain fenceのみ例外)。
- 層3はobject生成→subchannel bind→各メソッド→fence発行→必要時wait。2DとM2MFで同じ口を使う。emit成功は投入準備、kick成功も完了ではない。故障時に既にkickしたパケットを巻戻さない。

## 4. (c) DMA object・RAMHT・PCI staging
- NV44 FB初期化: `N/nvkm/engine/device/base.c:682` のnv44_fb_new→`fb/nv44.c:48–53 nv44_fb_init`に従い100850=80000000→100800=1。PCI dmaobj生成より前、recoverでも実行。tileは§5のsnapshot転記を維持。
| 順・前提 | 手本の列 (N/以下) | OS32の列 / 誤りコード |
|---|---|---|
| 1 descriptor | `nvkm/engine/dma/usernv04.c:41–69 nv04_dmaobj_bind` | gf_inst_alloc(16,16,false)。start S、inclusive limit L。word0=flags0\|((S&fff)<<20)、word1=L−S、word2/3=(S&fffff000)\|flags2。write32順→F1 publish。 |
| 2 target/access | 同`:104–130 nv04_dmaobj_new` | class=2(FROM)/3(TO)/3d(IN) (`include/nvif/class.h:48–50`)。VRAM flags0\|=3000、PCI\|=23000。RO\|=4000/flags2=0、WO\|=8000/flags2=2、RWはflags2=2。32bit端点とS<=Lを検査、EINVAL。 |
| 3 handle登録 | `nvkm/engine/fifo/nv40.c:111–120 nv40_eobj_ramht_add`、`fifo/nv04.c:502–505` | context=(chid<<23)\|(engine<<20)\|(inst.off>>4)。DMA engine=0、GR=1。NV04流のbit31 validを足さない。 |
| 4 hash/probe | `nvkm/core/ramht.c:27–37,107–126` | 4096 entries、bits=12。handleを12bitずつXOR、さらにchid<<(12−4)。衝突は次entry、4096でwrap、満杯ENOSPC、同一handle EEXIST。CPU bitmap512Bで使用中を管理。 |
| 5 publish/remove | 同`:60–96,100–103` | table[slot*8]=handle→+4=context→F1 publish。cookie=slot+1。解除は完了後に両word=0、bitmap解除→gf_inst_free。 |
- 本層予約handle: 80000001=push DMA (RAMFC参照、RAMHT登録不要)、80000002=VRAM RW、80000003=PCI staging RW。層3 objectは80000100以降、handle=0は未bind用として使用しない。
- VRAM転送objectは[0,V−R−1]。描画/転送の領域境界検査は層3の役目で、push予約域を通常のsurface allocationから除外する。
- PCI objectは**物理連続staging単位**でS=buf.pa、L=pa+size−1。object相対offsetでアクセス。PCI_NOSNOOP/AGP/VM cloneはこの経路に不要。一般RAMはstagingへCPU copy、完了までpin保持。
- 常駐adapterはdma_alloc(4096,4096,ffffffff,&b)を1頁要求、元のrcを保存。取れなければENOSPCでPCI要求失敗を返す。VRAMチャネルは継続可。層3M2MFは4KiBずつ分割して同機能を提供する。
- 差分理由: nouveauのページ表/GEM汎用管理の代わりにOS32既存の連続DMA pool契約を使う。PCI到達性は実装後のRa266転送試験で判定し、失敗を機能削除の根拠にしない。
- 登録/解除はINIT時またはfenceで全仕事をdrainしてPFIFOをpauseした前景のみ。pause/startは`fifo/base.c:55–63`→`fifo/nv04.c:212–254`: 2500=0→3250 bit0=0→HASH_BUSY消滅待ち→HASH無効化→表書換え/publish→3250 bit0=1→2500=1。drain済みを前提としHASH_FAILEDは§6へ、期限切れは閉じたままETIME。公開中のRAMHTを書換えない。理由: 単一所有者なのでkernelのobject寿命/lock machineryを小さい契約に置換できる。
```c
int gf2_dma_new(struct gf2_chan *, u32 handle, u16 class_id,
               u8 target, u8 access, u32 start, u32 limit, struct gf2_object *);
int gf2_gr_object_new(struct gf2_chan *, u32 handle, u16 class_id, struct gf2_object *);
int gf2_object_del(struct gf2_chan *, struct gf2_object *); /* quiescent必須 */
int gf2_ramht_add(struct gf2_chan *, u32 handle, u8 engine, u32 inst_off, u32 *cookie);
int gf2_ramht_del(struct gf2_chan *, u32 cookie); /* 表/bitmapのみ、inst解放は呼出側 */
int gf2_map_vram(struct gf2_chan *, u32 off, u32 len, volatile u32 **va);
int gf2_unmap_vram(struct gf2_chan *); /* map失敗/解除失敗時は所有状態を保存 */
int gf2_dma_get(struct gf2_chan *); /* stagingへ1頁取得、raw rcはdma_rc */
void gf2_dma_put(struct gf2_chan *); /* quiescent後のみ、staging_liveを解除 */
```

## 5. (d) PGRAPH 初期化・文脈
- 下表は `N/nvkm/engine/gr/nv40.c:287 nv40_gr_init` の順。初期化中は無投入。BAR0相対。EIOは最初の失敗で停止し§6へ。IRQのACK/enableはF1経由。
| 順 | 手本の列 | OS32の列 / 誤りコード |
|---|---|---|
| 1 ctxprog | `ctxnv40.c:671–689 nv40_grctx_init`、`nv40.c:294–297` | §下記generatorのPROG pass、400324=0→400328へcode[0..n)。size=ctxvals_pos×4。256語超はEIO。 |
| 2 context無し/IRQ | `nv40.c:299–303` | 40032c=0→400100=ffffffff→40013c=ffffffff (配送は開始完了後)。 |
| 3 debug/control | 同`:305–313`、`gr/regs.h:5–9,47,131,169` | 400080=ffffffff→0、400084=401287c0、40008c=e0de8055、400090=8000、400610=00be3c5f、400144=10010100、400714=ffffffff。 |
| 4 unit | 同`:315–327` | j=1540&ff、非zero時400050ではなく**405000=ctz(j)**。400820=83280eff→400824=a0。 |
| 5 4a分岐 | 同`:346–365,367–373` | 400860=0→400864=0→400b38=2ffff800→400b3c=6000→400bc4=1003d888→400bbc=b7a7b500。 |
| 6 RAM config | 同`:388–423` | 4009f0=read100200→4009f4=read100204→4069f0=read100200→4069f4=read100204→400840=0→400844=0→4008a0/4008a4=BAR1 resource length−1。ここはV−1ではない。 |
| 7 tile整合 | `nv44.c:31–46 nv44_gr_tile`、`fb/nv44.c:40–45,60` | 無投入でPFBの12組100600+i*10のaddr/limit/pitchを採取。PGRAPH 400908+i*10=pitch→400904+i*10=limit→400900+i*10=addr。PFB設定は維持。 |
| 8 文脈実体 | `nv40.c:74–86 nv40_gr_chan_bind` | sizeをgf_inst_alloc(size,16,true)、VALS pass→ctx[0]=ctx.off>>4→publish→§2のRAMFC+38 bind。ENOSPC/EIO。 |
| 9 FIFO受信 | `L/nv_hw.c:1173 NVLoadStateExt`、`N/nvkm/engine/gr/nv40.c:268 nv40_gr_intr` | bind完了後400720=1。旧nvの初期化列を補助根拠とする。以後fault時の無条件再開には使わない。 |
- tile差分理由: nouveauのFB→GR tile通知をOS32の既存表示設定のsnapshot転記に置換。pushはlinear配置を前提とし、予約範囲が有効tileに重なる場合は起動ESTATE、層5のtile変更口に送る。
- **ctxnv40を2D用に削らない**。`ctxnv40.c:596–657 nv40_grctx_generate`: setup/load/save分岐→general (`:159`)→state3d (`:265`)→state3d_2 (`:370`)→state3d_3 (`:534`)→shader (`:549`)→exit。
- `ctxnv40.c:120–143` の4aはvs_nr=3。F1予約式のpopcount(1540)とは別の量。shaderは同`:569–591`: vs_len=4980/4、b0=1d40/4、b1=3f40/4、vs_nr_b0=363、b1=64。3f800000等はbit定数でCPU浮動小数演算なし。
- 実装はMITの4a分岐を保持したgenerator 1部品。`ctxnv40.h:6–22,25–129` のPROG/VALS、label解決、cp_ctx/cp_pos/gr_defのoffset計算を保つ。PROG用256語＋label32語のscratch、VALSはF1 write32で直接書く。
- sizeはgenerator結果を正とし、grctx実体のRAM配列をmodへ埋めない。PROG/VALSが同じ末尾offsetになることをhost試験で照合。値生成とctxprogを別々の手製表にしない。
- class一覧 (`N/nvkm/engine/gr/nv44.c:83–100`): 0012 beta1、0019 clip、0030 null、0039 M2MF、0043 ROP、0044 pattern、004a GDI、0062/3062 surface2d、0072 beta4、0089/3089 SIFM、008a IFC、009f imageblit、309e swzsurf、4497 Curie。
- `nv40.c:44–59 nv40_gr_object_bind`: 各GR objectは20B/16B整列、word0=class、残り4語=0→RAMHT engine1。リスト外ECLASS。4497/309eも生成口の表には保持、3Dメソッド実装は票の残タスク。
```c
/* grctx.cは生成だけ、gr.cがupload/確保/bindの順を所有。 */
int gf2_grctx_program(u32 code[256], u32 *words, u32 *bytes);
int gf2_grctx_fill(struct gf_foundation *, const struct gf_inst *ctx);
int gf2_gr_init(struct gf2_chan *); /* grctx allocationも所有、失敗時逆順free */
```

## 6. (e) 完了待ち・障害停止・復帰
| 項目 | 手本の列 | OS32の列 / 誤りコード |
|---|---|---|
| 完了番号 | `N/nv17_fence.c:90–93 nv17_fence_context_new`→`nv10_fence.c:33–55 emit/read`、`include/nvhw/class/cl006e.h:28–29` | `gf2_emit(ch,0,0x50,false,&seq,1,dl)`→kick (`include/nvif/push006c.h:9` SUBC_NV06E=0)。user+48 REFERENCEが到達したら完了。GET=PUTだけでは完了としない。 |
| 同期方式 | `nv17_fence.c:36–73 nv17_fence_sync` は2channel間semaphore | 単一channelなのでreferenceのみ。notifier領域/semaphore object不要。層3がnotifyを使う場合は別objectとして追加できる。 |
| 待ちと期限 | `N/nouveau_dma.c:47–59` はGET進行でtimeout更新、`gr/nv40.c:109–115` は2秒 | OS32は操作全体の絶対期限、既定2,000,000us。既存gf_io.timingのclock/waitで1〜50us刻み。理由: 協調系で無限に前景を占有しない。ETIME、時計失敗EIO。 |
| cache error | `fifo/nv04.c:303–350` はCACHES停止、method/data採取、GET+4、再開 | 前景でfirst-faultを採取しIRQ要求閉→gate閉。GETを進めて成功扱いしない。EFAULT。理由: OS32はsoftware-method emulationを発行せず、失敗仕事を捨てた結果を成功として返せない。 |
| DMA pusher | 同`:354–402` はGET/PUT/state採取、GET=PUT、state=0、再開 | 同じ採取対象、停止して前景resetへ。未完了fence全部EFAULT。自動再送なし。理由: 部分実行したM2MF等の再送を層2で判定できない。 |
| semaphore/未知FIFO | 同`:431–458` | semaphoreは使用しないので発生時faultとして採取、未知bitもmask。GET+4/再arm修復はしない。 |
| PGRAPH | `gr/nv40.c:238–268 nv40_gr_intr` | stat/NSOURCE/NSTATUSはF1 snapshotから、40032c/400704/400708/400160+subc*4は前景で採取。400720 bit0=0、fault化。DMA_VTX_PROTECTIONの402000処置とFIFO=1の再開は実行せずresetへ。理由: この層では3Dの継続処理を提供しない。 |
- seqはu32、0から開始し発行は++seq、判定は(s32)(observed−seq)>=0。同時未完了距離<2^31を保つ。epochは再構築ごとに更新し旧epochのfenceはEFAULT。fault確認をreference成功判定より先に行う。
- deadlineは開始clock+timeout、(s32)(now−deadline)>=0。timeout<2^31us、待ちごとに延長しない。ring内GETの移動とグラフィクス完了の停止を別に記録。
- fault採取: PFIFOは2500保存/0→3204、3270、3250、90000+((GET&7ff)/4)*8、隣のdata、3240/3244/3220/3228。根拠は上表cache-error/dma-pusherと`fifo/nv04.c:415–419`。採取後gf_irq_enable(f,false,false,ch->heads)→3220/3200/3250 bit0を閉じ、PGRAPH 400720 bit0も閉じる。ACKはF1のみ。
- **B7は前景だけ**。各API入口・待ちの各周回・kick直前にgf_irq_snapshot(f,&now)、now.fifo/graphとseenのunsigned差を確認。変化時は詳細採取→gf_irq_enable(f,false,false,heads)→gate閉→FAULT。IRQ/tickは既存F1の記録/ACKのみ。VBLANK要求は保持。
- IRQを開く直前にseenを保存。first-faultは最初の前景観測で固定し後続snapshotはlatestに件数/OR maskを集約。NSOURCE/NSTATUSはACK前にF1が保存済み。trap残留は実機で照合し、停止根拠は件数差とする。
- 手本の再開write (`fifo/nv04.c:345–349,397–398`、`gr/nv40.c:268`) を行わず採取する。ACKでtrapを失う/実行が続くとは断定しない。
- 前景service/各API入口はfaultを処理し、serialへepoch/chid/handle/PUT/GET/reference/stat/nsource/nstatus/method/dataを出す。正常stop: 新規受付を止めDRAIN→最終fence待ち→nv04_chan_stop相当→下記共通の停止・解放列。
- channel stopは`fifo/nv04.c:53–87`、RAMFC save/clear対象は`nv40.c:64–86`の全layout (2058の上位2bit、32e4/32e8/2088/3300/330c含む)。無効chid31へ替えMODE bit0を落とす。途中の復元開放列も手本順、直後F1停止へ。
- 共通停止 (timeout/faultも同じ): gate閉→IRQ要求閉→gf_engine_stop成功→可変instをRAMHTから外しgf_inst_free→F1 stop (再reset/IRQ detach/BM解除/固定inst・BAR2解放)→成功後だけPCI staging/VRAM mapを解放。最初のengine_stop失敗ではinstも保持、F1失敗では残資源を保持しESTOP、detach不可。二度のresetの理由: F1の固定領域解放前に層2instを返すため。resetをfence達成に数えない。
- recoverは明示呼出し: 失敗stopがあれば再度stop→F1 start→層2全再構築→epoch更新→初回reference試験→RUN。層3へ全handle再生成要求。旧データ/部分転送を正しいと扱わない。表示再設定が必要なら既存display開始口へ戻る。

## 7. 部品の大きさと上限
| 部品 | text/rodata見積KiB | RAM/VRAM |
|---|---:|---|
| fifo / push / dmaobj / ramht | 1.8 / 2.0 / 0.8 / 1.0 | state＋bitmap約1KiB、push VRAM64KiB、各DMA16B |
| gr / grctx | 2.0 / 7〜12 | ctxprog scratch約1.2KiB、grctxは生成size (予算80KiB、実値で確保)、GR object20B/個 |
| fence / fault / command / platform | 0.8 / 1.5 / 1.5 / 0.7 | fault記録約128B、PCI staging4KiB、追加map用PT |
| 合計 | 約20〜25 (層2予算28) | grctx/pushはimageに含めず、scratchは起動処理後再利用 |
- `geforce.mod image_max=128KiB` は維持。F1後の実像＋層2text/rodata/data/BSS/alignmentをlink mapで計測、収まるとは未断定。F1の増分予算8KiBに上表を足す。
- 超過時はgrctxを4a定数分岐で最適化、debug文字列を診断側へ、死コード除去。なお超過ならgrctx生成をbuild時生成の圧縮初期値/run列＋ctxprogへ変換して同値試験する。機能削除/上限拡張/本体のkernel複製では解決しない。
- importはF1後28〜30＋**1** (`geforce_dma_ops`等の単一typed accessor)で29〜31を目標。clock/wait/MMIO/mapは既存gf_io/production adapterを再用。dma_alloc/freeを別importにせず、常駐の型付きops表を取得する。汎用syscall dispatcherにはしない。
- 生成契約実数が32超なら既存個別資源importを同じ資源opsへ統合して32以下へ収める。32の上限は変更しない。常駐adapterの増分は0.5KiB予算、ctx本体を常駐させない。

## 8. ファイルごとの変更一覧 (O/相対、1行=1段=1実装者)
| 段/ファイル | 役目 / 依存 / 増分行目安 |
|---|---|
| A0 hwdev/gf/command.h 新 | 上記全stateと型付き境界を凍結、「確定」節の fifo/fb のプロトタイプを含む / 層 1 の A0 (foundation.h) の取り込み後 / 240 |
| A1 hwdev/gf/production.h 変 | gf2所有state / A0 / 8 |
| A2 tools/tests/gf_command_host.h 新 | 偽MMIO/PRAMIN/時計/IRQ骨格と部品stub、B群に依存しない / A0 / 140 |
| B0 hwdev/gf/fifo.c 新 | PFIFO/RAMFC開始停止 / A0,F1 / 200 |
| B1 hwdev/gf/push.c 新 | reserve/emit/wrap/kick / A0 / 180 |
| B2 hwdev/gf/dmaobj.c 新 | DMA descriptor生成 / A0,F1 / 85 |
| B3 hwdev/gf/ramht.c 新 | hash/add/del / A0,F1 / 100 |
| B4 hwdev/gf/grctx.c 新 | MIT grctx generator (補助関数も同ファイル) / A0 / 600〜750 |
| B5 hwdev/gf/gr.c 新 | PGRAPH init/context/class生成 / B4 / 210 |
| B6 hwdev/gf/fence.c 新 | reference/期限 / B1 / 90 |
| B7 hwdev/gf/fault.c 新 | 前景snapshot差分/詳細採取/要求閉/gate閉 / A0,F1 / 150 |
| B8 hwdev/gf/command_platform.c 新 | 部分extent/長さ照合/VRAM map予約とDMA ops接続 / A0 / 100 |
| B9 drivers/video/geforce/geforce_dma.c 新 | 常駐DMA ops実体 / A0 / 45 |
| C0 hwdev/gf/command.c 新 | start/stop/recoverの寿命管理 / B群 / 160 |
| C1 hwdev/gf/service.c 変 | F1開始後の層2開始 / C0 / 15 |
| C2 hwdev/gf/display/quiesce.c 変 | 層2stop→F1stop、重複stopは冪等 / C0 / 15 |
| C3 drivers/video/geforce/geforce_module.c 変 | detach時fault/所有資源残留を拒否 / C0 / 10 |
| C4 tools/prototypes/geforce_imports.def 変 | typed DMA ops取得を1 import / B9 / 15 |
| C5 build/geforce-module.mk 変 | 部品配置、サイズ検査 / B群,C4 / 20 |
| D0 docs/02_memory.md 変 | push用UC map 64KiB/16頁、仮想配置に応じ追加PT最大2頁を予算化し実測、予約/解除寿命 / B8 / 15 |
| A3 tools/tests/test_gf_command.py 新 (旧 T1) | CHECK登録、部品別host駆動、変異実行。A 段に置き、B 段の登録は PM が直列に取り込む (ROLES §3-3) / A2 / 100 |
| T2 kdebug/gf_command_test.c 新 | 非GUI serial受入入口 / C群 / 170 |
- 各B段はtools/tests/gf_command_b<N>_host.cを同じ担当の成果物として追加 (各約40行、合計約400行)。依存部品はA2 stubで置換、対象部品は実コードをlinkし、当該Bだけで正常/変異を実行可能。
- 定数は既存3層規則の正典→共通定義→部品aliasへ配置。新しい一般メモリ予約をmemmap.hに重複固定しない。Aで型を凍結、Bは部品別、Cは結合、Tで受入。各段は単独host検査可能にする。

## 9. 受入 → 試験・変異と段の分け方
| 段の出口/受入 | ホスト偽MMIO (各writeの順も検査) | Ra266シリアル / 変異 |
|---|---|---|
| A/B0/B2/B3 配置と登録 | V/Rを変え表非重複、RAMFC slot0、descriptor byte列、hash衝突/満杯/再利用 | V/R/B/off/2210/2220を出力 / R起点RAMFC式、bit31 valid追加、PCI VA混入を殺す |
| B4/B5 GR文脈 | 同commit MIT generatorを独立oracleとして4aのcode/size/全初期値一致、BAR1長≠Vのfixture、class全表 | ctxprog語数/size/ctx.offを記録、referenceと層3試験で実行確認 / vs=popcountへの誤置換、400140使用、ctx[0]省略を殺す |
| B1/B6 投入 | 初期GET=0でfree=max−curから初回fence投入、subc0/0050、先頭SKIPS、GET遅延、末尾jump、get=cur、get=32、u32時刻/seq wrap、GET追従だけでreference停止 | 10万referenceで複数回wrap、IRQ許可状態で完了 / 語/byte混同、−1省略、readback省略、GET=PUT完了判定を殺す |
| B7/C 障害寿命 | F1 ACK後の件数差→採取→要求閉→gate閉、first-fault固定、tick重複、timeout、F1停止失敗では解放なし、旧epoch拒否 | 不正handle/範囲外methodを診断専用入口で投入、停止ログ→明示recover→正常reference / 件数差見落とし、要求閉省略、fault後kick、reset後成功通知を殺す |
| PCI機能 | FB 100850→100800→PCI descriptor順、pa/va異なるfixture、4KiB端点、pool不足、完了前free禁止 | 層3の0039 M2MFと結合しRAM→VRAM→RAM、固定/歩行bit/CRC一致、82557併用 / PCI target誤値、端点+1、early-freeを殺す |
| B8 map | BAR1長不足/加算overflow/部分extentのbaseとlength/解除失敗時保持 | 64KiB mapのPT増分と予約解除を記録 / offset無視を殺す |
| 最終 | KDEBUG=0本番link、import<=32、image<=128KiB、全段rollback | 起動→投入→停止→再開、IRQ6共有/VBLANK増加、表示維持、0:13.0/0221一致 |
- host試験はレジスタ副作用のモデルでありDMA到達性の証明ではない。実機試験を実装前の着手条件にしない。PCI転送の結合試験だけ層3M2MFに依存し、referenceまでの層2受入を先行する。
- T1は`CHECK=dict(name='gf-command-host',tier=1,needs=[],budget=90.0,covers=['hwdev/gf/**','drivers/video/geforce/geforce_dma.c','tools/tests/gf_command*','tools/tests/test_gf_command.py'])`を宣言。未着手Bは登録せず、追加済みBの試験欠落は失敗。A時点で骨格の正常/変異を実行する。
- 各変異は無変更対照がGREEN→一意の目印で1箇所を変更→同じ対象試験が実行時RED→復元してGREENを要求。コンパイル失敗を検出成功に数えない。B担当が自身のfixture/変異をT1へ登録。
- D1段: `docs/tasks/DEFERRED_TESTS.md`へ `GF-L2-Ra266 | 未実施 | Ra266 0:13.0/0221 | 10万reference・wrap・fault→recover・VBLANK・PCI往復(層3結合) | 実施担当/日付/serialログを実施時記入` の行を追加。host合格で実機済みにしない。
- 各段の成果物: C1/GNU規則とMIT出所確認→局所host検査→前段と結合。実機配備/物理操作は本書の実行範囲外、D1/D2に従う。本書作成時はビルド/配備/リポジトリ編集を行っていない。

## 10. 未決 (ユーザー判断が必要なものだけ)
- **0件**。chid0、VRAM push、PCI staging、reference、明示recoverは上記推奨を設計値とする。実装後の値測定・PCI実機成否・像サイズ確定は技術検証事項で、ユーザー判断待ちにはしない。


反論: 0件。

## 確定 (2026-11-02、Opus 第 2 回の条件付き承認、PM が反映)
- F2 (条件): B0 fifo.c の公開関数と NV44 FB 初期化の持ち主を A0 の command.h で凍結する。どれも B0 (hwdev/gf/fifo.c) の持ち:
```c
int  gf2_fb_init(struct gf2_chan *);                  /* 100850=80000000 → 100800=1 (fb/nv44.c:48-53)。PCI dmaobj の前、FIFO 停止ゲートの後 */
int  gf2_fifo_init(struct gf2_chan *);                /* 3204=31→2100/2140→3200→3250→2500 (fifo/nv40.c:221-228)、RAMHT/RAMRO/RAMFC の 2210/2218/2220/2230 */
int  gf2_fifo_bind_gr(struct gf2_chan *, u32 ctx_off);/* B5 の gr.c が ctx.off を渡す。2500 bit0=0→32e0=ctx_off>>4→RAMFC+38→2500 bit0=1 (fifo/nv40.c:125-164) */
int  gf2_fifo_start(struct gf2_chan *);               /* RAMFC の書込みと MODE の bit、DMA_PUSH・PULL0 */
void gf2_fifo_stop(struct gf2_chan *);                /* 停止と RAMFC への退避 (fifo/nv40.c:39-86) */
int  gf2_fifo_pause(struct gf2_chan *);               /* 2500=0→PULL0 bit0=0→HASH_BUSY 待ち (nv04_fifo_pause、fifo/nv04.c:212-254) */
void gf2_fifo_resume(struct gf2_chan *);              /* PULL0=1→2500=1 */
```
- F2 (P3): dma_ops.free は int を返し、失敗時は leak (dma_mark_leaked) を呼ぶ (上の §3 の断片を直した)。
- F2 (P3): map_ops.map が返す `volatile u8 *` を gf2_map_vram で `volatile u32 *` に変える (B8 の 1 か所、off と len は 4 の倍数を前提)。
- F2 (前提): A0 (command.h) は層 1 の foundation.h (gf_inst・gf_irq_stats・gf_instmem_map_ops) を include する。層 1 の A0 を main に取り込んでから着手する (§8 の A0 の依存を直した)。
- F1 (P3): §1 の食い違いは 0 件に直した。
- F4 (P3): T1 を A3 として A 段に移した。B 段の試験の登録は PM が直列に取り込む。

## 追記: INIT のあいだの初回 reference (2026-11-03)

> 出所: ~/os32-tmp/run/gf2-a0/ADDENDUM.md (A0 の sol の設計の欠け → astra 起草、Opus レビュー Approve)。**小段 A0b** (hwdev/gf/command.h、terra): 下の宣言 1 行を足し、gf2_emit のコメントを「non-RUN ESTATE; INIT/DRAIN reference goes through gf2_push_reference」に替える (Opus P3-1)。B1・B6・C0 は A0b の上に書く。P3-2 (fence_emit が常に push_reference を呼ぶ形) は採らず、追記のとおり。

状態は既存の `GF2_INIT=1` を維持（OFF=0/RUN=2/DRAIN=3/FAULT=4、新値・フラグなし）。C0 の start/recover は §2 の IRQ 開まで済ませてから初回 reference を発行する。
B1 に固定 reference 専用の内部境界を置く（command.h の宣言追加1行、既存宣言変更0行）:
```c
int gf2_push_reference(struct gf2_chan *ch, u32 seq, u32 deadline_us);
```
この関数は `state==GF2_INIT || state==GF2_DRAIN || state==GF2_RUN` を許し、`subc=0, method=0x0050, noninc=false, count=1, data=&seq` だけを B1 の共通 reserve/write 処理へ渡す。`gf2_emit` の一般投入は RUN 限定を維持し、内部 drain もこの境界を使う。
B6 `gf2_fence_emit` は RUN で既定の `gf2_emit`、INIT/DRAIN で `gf2_push_reference` を呼び、既定の ++seq・epoch 記録・kick を行う。`gf2_kick` と `gf2_fence_wait` も INIT/RUN/DRAIN を許す。fault確認・期限・barrier/readback は既定どおり。
C0 の初回は `rc=gf2_fence_emit(ch,&f,dl); if (!rc) rc=gf2_fence_wait(ch,f,dl); if (!rc) ch->state=GF2_RUN;`（同一期限）。失敗時は RUN にせず §6 の共通停止へ。INIT 発行はこの初回試験、DRAIN 発行は最終 fence の呼出契約とし、追加の検査フラグは設けない。
手本（N=/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau）: `N/nouveau_chan.c:469–478,496–497` は ring/NOP と fence context の初期化、`N/nv17_fence.c:90–93` は NV10 emit/read の採用、`N/nv10_fence.c:33–41,53–55` は固定 reference の reserve/write/kick と読出し。INIT/RUN の区別と初回完了試験は OS32 の契約であり nouveau の状態値ではない。
確定本文を覆す必要なし：初回成功→RUN と一般 emit の RUN 制限を保ち、未定義だった内部初回 reference の境界を補う。リポジトリは未編集。

## 追記: B0 の fifo の 7 本の担当と C0 の呼ぶ順 (2026-11-03)

> 出所: ~/os32-tmp/run/gf2-b0/ADDENDUM.md (B0 の sol の設計の欠け → astra 起草 r1・r2、Opus レビュー r1 直しが要る → r2 写してよい)。**「確定」節の 7 本の注記はこの追記で置き換える** (宣言は変えない)。

確定節 :253–259 の注記を以下で置換する（宣言の変更0行、設計を覆す必要なし）。順序は push DMA 構築後に gf2_fifo_init → gf2_fifo_bind_gr → gf2_fifo_start。参照の fifo/・fb/ は設計本文の N/nvkm/engine/fifo/・N/nvkm/subdev/fb/。
int  gf2_fb_init(struct gf2_chan *); /* 停止ゲート後、PCI dmaobj 前に 100850=80000000→100800=1（fb/nv44.c:48–53）。 */
int  gf2_fifo_init(struct gf2_chan *); /* §2:43 の RAMFC 128B zero→+00/+04=B、+0c=push_dma.off>>4、+18=30086078、+3c=0001ffff（fifo/nv40.c:39–58）→§2:44–45 の表レジスタ・無効chid・開放（同:190–228）。PUSH0=1→PULL0=1→CACHES=1 は同:226–228、2100/2140 は書かない（F1 所有；手本の IRQ ACK/enable は fifo/nv40.c:223–224）。 */
int  gf2_fifo_bind_gr(struct gf2_chan *, u32 ctx_off); /* B5 が ctx.off を渡す。2500 bit0=0→active chid=0 の場合のみ32e0=ctx_off>>4→RAMFC+38に同値→2500 bit0=1（fifo/nv40.c:125–164）。 */
int  gf2_fifo_start(struct gf2_chan *); /* MODE(2504) bit0=1 のみ（fifo/nv04.c:92–99）。RAMFC は書かず、DMA_PUSH・PULL0 の書込みは不要。PULL0 は init の開放済み（fifo/nv40.c:226–228）；DMA_PUSH を追加で有効化しない。 */
void gf2_fifo_stop(struct gf2_chan *); /* 停止・active chid=0 の RAMFC 退避/レジスタクリア・無効chid選択・MODE解除（fifo/nv04.c:41–88、NV40 layout は fifo/nv40.c:64–87）。 */
int  gf2_fifo_pause(struct gf2_chan *); /* drain 後、2500=0→PULL0 bit0=0→HASH_BUSY消滅待ち→HASH無効化。HASH_FAILED/期限切れは§4・§6の既定処理（fifo/nv04.c:212–243）。 */
void gf2_fifo_resume(struct gf2_chan *); /* 表書換え/publish後、PULL0 bit0=1→2500=1（fifo/nv04.c:247–254）。 */
C0 の順は gf2_fifo_init → gf2_gr_init（bind_gr を含む）→ gf2_fifo_start。§2 の「GR初期化」は GR の文脈の確保と読む（RAMFC 初期値: fifo/nv40.c:39–58、GR bind: 同:125–164、開始: fifo/nv04.c:92–99）。+38 の退避・復元は足さない。
r1→r2 の変更: P2 の C0 呼出し順と §2 の解釈を明記し、P3 の 2100/2140 は B0 が書かず F1 所有と明記。

## 追記: 障害の出力から handle を外す (2026-11-03)

> 出所: ~/os32-tmp/run/gf2-b7/ADDENDUM.md (B7 の sol の設計の欠け → astra は確定本文 :183 を覆す必要ありとして停止)。**ユーザー決定 (2026-11-03)「handle を外す (推奨)」**: 障害の serial 出力は手本と同じく class・subc・method (既存の他の診断項目は維持)。subchannel→handle の追跡は足さない。**小段 A0c** (hwdev/gf/command.h、terra): 下の宣言の変更 2 行。B7 は A0c の上に書く。

確定本文 GF_FEAT2_DESIGN.md:183 の handle 出力要求を覆す必要があるため、次の最小修正判断を記して停止する：handle を出力から外し、PGRAPH は class/subc/method（既存の他の診断項目は維持）を出す；根拠は /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/nvkm/engine/gr/nv40.c:242–246,274–280（subc=(400704の値>>16)&7、method=同値&0x1ffc、class=read(0x400160+subc*4)&0xffff、object handle の出力なし）；subchannel→handle 追跡は追加しない；A0 command.h の宣言変更は2行（:122 の handle を削除、:127 の gr_handle を gr_class に改名）、B7 は上記採取値から subc/method を復号し、PFIFO に class/handle を推定補完しない；本文・宣言の実編集は未実施。

## 追記: C1・C2 から層 2 のチャネルに届く口 (2026-11-03)

> 出所: ~/os32-tmp/run/gf2-c12/ADDENDUM.md (C1・C2 の terra の設計の欠け → astra 起草 r1・r2、Opus レビュー r1 直しが要る → r2 写してよい)。新しい小段 **A0d** (hwdev/gf/internal.h) と **C1a** (hwdev/gf/devinit.c)。

確定本文を覆す必要なし。以下の相対パスは /home/hight/wt/os32-v3/gf2-l2 基準。F1取得口は無く、既存境界は hwdev/gf/internal.h:201–202 の owner hook → hwdev/gf/devinit.c:60–81 の production adapter（service.c:37、display/quiesce.c:21）。
小段 A0d（internal.h、C1/C2の前提）で宣言を3行追加、既存宣言変更0行：前方宣言2行と gf_display_owner 末尾の任意ops 1行。command.h・production.h の宣言変更は0行。
```c
struct gf2_chan;
struct gf_foundation;
struct gf2_chan *(*command_get)(struct gf_display_context *, struct gf_foundation **);
```
小段 C1a（hwdev/gf/devinit.c、A0d/A1/B9の後）で static adapter と owner の指定初期化を接続：`struct gf_production *p=c->io.opaque; *out=&p->foundation; return &p->chan;`。取得は副作用なし、start前・失敗後にも同じ実体を返す。汎用側で opaque を cast しない。
C1a は既存 foundation_start の資源照合成功後、F1 start 前に `p->chan.map_ops=p->map_ops; p->chan.map_owner=p->map_owner; p->chan.bar1=c->bars[GF_MODE_VRAM_BAR]; p->chan.dma_ops=geforce_dma_ops();` を設定（devinit.c:63–68、production.h:45–52）。chan全体のzero化・失敗資源の上書きはしない；C0は渡された f を保存し借用adapterを停止完了まで保持する。
C1/C2共通の取得は `struct gf_foundation *f=0; struct gf2_chan *ch=c->owner->command_get ? c->owner->command_get(c,&f) : 0;`。ch非NULLならfも有効という契約；層2を持たない診断owner・既存host fixtureはops未指定/NULLで層2を飛ばす。層2結合fixtureだけ自分のchan/fと偽adapterを返す。C1はF1開始成功後 `if (ch && (rc=gf2_start(ch,f))) { c->last_rc=rc; goto fail; }`。
層2開始前の失敗は既存 `gf_failure → gf_display_stop → owner->foundation_stop(c)` がF1を回収する（service.c:3–10、devinit.c:132でF1 start前にもBM取得）；F1 start内部rollback（foundation.c:114–117）済みでも同hookを呼ぶ。C0の未開始/停止完了GF2_OFFは `gf2_stop(ch)==0`、I/O・解放なしとし、新しい開始済みフラグは足さない。
C2は `rc=ch ? gf2_stop(ch) : 0; if (rc && !f->revoked) { c->last_rc=rc; c->reusable=false; c->stop_ok=false; return -1; } stopped=c->owner->foundation_stop(c);` の順で既存quiesceへ続く。revokedでない層2失敗ではengaged・資源を保持しhook/restoreへ進めない（§6の保持契約、C3 geforce_module.c:95–100）；成功後のhookは省略せずidentity照合・revoked/last_rc反映を保つ。F1重複stopは既存foundation.c:61–94の冪等停止に委ねる（無I/Oとはしない）；C0は失敗をOFFにせず再stop可能にし、表示停止完了後の重複呼出しは既存quiesce.c:15のengaged判定で0。
gf2_stop が失敗しても f->revoked なら foundation_stop と既存の quiesce へ進む（devinit.c:70–81、display/quiesce.c:16–39）；F1はtick解除とCPU unmapを試みinstを保持する（foundation.c:65–72）。chanはOFFにせず層2資源を保持し、C3はdetachを拒む。
r1→r2 の変更：P2のrevoked時も既存cleanupへ進む例外をC2のコード境界と説明に反映し、devinit.cのadapter終端とfoundation.cのstop終端の行番号を修正。

## 追記: T2 から層 2 を呼ぶ診断の境界 (2026-11-03)

> 出所: ~/os32-tmp/run/gf2-t2/ADDENDUM.md (T2 の sol の設計の欠け → astra 起草 r1・r2、Opus レビュー r1 直しが要る → r2 写してよい)。新しい小段 T2a0・T2a1・T2a2・T2b (T2c の cmd_gf2.c は T2b に含める、PM 決定)。

T2 補遺（確定本文の変更不要）：層1の op は増やさず、診断専用 gf2_test_ops を gf1_test_ops 末尾から渡す。層2本体の直接 link・汎用投入口・新しい防御機構は追加しない。
```c
enum gf2_test_op { GF2T_START, GF2T_REFERENCE, GF2T_SNAPSHOT, GF2T_STOP, GF2T_RECOVER, GF2T_BAD_HANDLE, GF2T_BAD_METHOD };
struct gf2_test_request { u32 size; enum gf2_test_op op; u32 deadline_us; };
struct gf2_test_reply { int rc, dma_rc; u32 state, epoch, seq, put, get, reference, B, ctx_off, ctx_bytes, ctx_words, fault_epoch, chid, fault_put, fault_get, fault_reference, caches, push1, cache_get, pull0, method, data, dma_state, dma_fetch, dma_push, dma_dcount, gr_trap, gr_method, gr_data, gr_class; bool latched; struct gf_irq_stats stats, first, latest; };
struct gf2_test_ops { int (*prepare)(struct gf_foundation *, const struct recover_bar_extent *, u32); int (*call)(struct gf_foundation *, const struct gf2_test_request *, struct gf2_test_reply *); };
const struct gf2_test_ops *command; /* gf1_test_ops 末尾へ追加（上の4宣言は hwdev/gf/gf2_test.h）。 */
```
所有：module 内 command_test.c に診断 chan を1個保持、prepare は C9 の準備済み foundation/lease を借り、f・BAR1・f の map_ops/map_owner・既存 geforce_dma_ops() を設定する。production は既存 production.chan を body から結ぶ。診断 f/bus/lease は層2 stop 成功・資源解放まで保持し、失敗時は再準備・再利用しない。前景のみ、C9 と同じ owner 選択で同時所有しない。
call は f で上記 chan を選択し、START/STOP/RECOVER は既存 gf2_*、REFERENCE は fence_emit→wait（同一期限）、SNAPSHOT は前景 fault_check と観測値のコピー、BAD_* は§9の固定不正handle/範囲外method投入だけ（RUN の診断 owner 限定、既存 reserve/kick/待ち・fault経路使用）。失効後は保存値だけ返す。診断 cleanup は層2 stop→既存層1停止・lease解放、停止失敗時は所有を保持する。
新小段 T2a0（宣言）：hwdev/gf/gf2_test.h の上記4宣言と gf1_test.h の末尾1行（A段と同じ扱い）。障害の handle は持たず gr_class を返し、subchannel→handle の追跡は足さない。
新小段 T2a1（本体）：hwdev/gf/command_test.c。T2a2（結線・契約）：hwdev/gf/foundation_test.c、drivers/video/geforce/geforce_body.c、build/geforce-module.mk（宣言依存）、drivers/video/geforce/geforce_module.h の `#define GEFORCE_MODULE_OPS_VERSION 4u`（3→4）と既存契約生成・対の更新（tools/gen_hwdev_contract.py の型収集、hwdev_contract.h の gf1_test_ops sizeof=12→16 の assert を含む）。foundation_out→既存 geforce_foundation_ops()->command で授受し geforce_module.c の新slot不要、GeForce import は32/32のまま・export追加0・HWDEV import追加0。
新小段 T2b（T2a0→T2a1→T2a2後、C9相当の結線）：userland/shell/cmd_gf2.c と既存shell登録/ビルド、kdebug/{gf_command_wire.h,gf_command_test.h,kdebug_main.c,gfinit.c}、build/hwdev.mk。GF_OP_COMMAND と専用要求型を追加し shell→kdebug_main→既存HWDEV gfinit振分け→T2へ接続、gfinitがowner準備/停止を担当。T2本体 kdebug/gf_command_test.c はT2a/b依存でserial受入を実装する。cmd_gf2.c を分ける場合だけ T2c とし、T2 は T2c にも依存。
r1→r2 の変更：P2 の reply.handle 削除・gr_handle→gr_class、P3 の ops版3→4と契約再生成を明記、T2aをT2a0/T2a1/T2a2に分割（宣言の変更は5行＋版定義1行＝6行）、確定本文を覆す必要なし。

