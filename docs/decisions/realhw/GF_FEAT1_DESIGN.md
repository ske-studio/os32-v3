# GeForce 6200 (NV44A) 層 1 (土台) — 確定設計: Bus Master・PRAMIN/instmem・割り込み

> 出所: ~/os32-tmp/run/gf-feat1/DESIGN.final.md (astra 起草 r1〜r2、Opus レビュー 2 回、条件は PM が反映)。
> 状態: **現行 (2026-11-02)** — 段の状態は票 TASK_GF_FEATURES が持つ。

## r1→r2 の変更
R1: 常駐tick枠・回収hook・PMC再開経路と停止時の参照解除を追加。
R2: 毎開始の先頭でBMを有効化し、再開始後0106hの試験を追加。
R3: engine停止をreset保持から手本のresetパルスへ変更。
R4: engine停止→IRQ登録解除→BM解除へ修正し、remove/suspendの根拠を追加。
R5: PVIDEOのACKと未使用PTIMER/PBUS/PMPEGのmask/ACKを追加。
R6: gf_stage_stateのCommand/PMCを所有stateの期待値で照合。
R7: instmemをUCのBAR2直接アクセス、偽MMIOをmap_ops差替えと確定。
R8: import現状20・見積28〜30・上限32と生成元の実装段を明記。
R9: latencyは0ChのWORD書込でcache lineを保持し、停止時復元を削除。
R10: PM判断に従い宣言だけの変更をA0に統合。

# GeForce 6200 / NV44A 層1 詳細設計 r2
2026-11-02。対象: Ra266、PCI 0:13.0、10de:0221、IRQ6、AGP→PCI ブリッジ。設計のみ。
`O/`=/home/hight/os32-v3/、`N/`=/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/。
手本は c03c1a14507dfaf8b856c2608f9ac216021fc222。以下の番地は特記なければ BAR0 相対、サイズは byte、区間は半開。
シングルタスク・協調切替、整数、GNU11、既存 kstring/P2V 系、定数3層管理。[C1]〜[D4]/[HW3] を維持。MIT 部の移植には著作権・許諾を残す。GPL は写さない。
層2〜5・GUI 接続は作らない。カーネル試験入口から全機能を呼べる。実機での確認は実装後でよい。

## 1. 実物の確認
票の「層1を作る」という前提は正しい。以下の4件は、票の変更範囲・比較の記述をそのまま実装指示にできない差分である。

| # | 票・比較側 | 現物と設計への反映 |
|---|---|---|
| 1 | 票§2は devinit・recover override・期待値の撤回を列挙 | `O/hwdev/gf/bus.c:29,109` も **毎回0102hとの完全一致**を要求。初期化だけ0106hにすると最初のMMIOで失敗。RUN/STOP別判定にする。 |
| 2 | overrideの置換だけで復帰も直るように読める | `O/kdebug/recover_profile.c:297` はBM=0を画面復帰条件にする。`:221–231` はQUIESCEとRESTOREが同値。RUN=0106h、S0=0102hを分離し、S0のBM=0条件は維持する。 |
| 3 | 比較:28,63のU1「MC/表示IRQ本体未取得」 | 手元には無いが同じ版の原本を取得確認。`N/nvkm/subdev/mc/nv44.c:43–46` はnv04のIRQ操作＋nv17の集約表、`N/nvkm/engine/disp/nv04.c:80` がPCRTC ACKを行う。U1のこの部分は解消。 |
| 4 | 票の修正対象はPMCの通常検証を列挙しない | `O/hwdev/gf/devinit.c:99–106 validate`はinitializedなら200hをbaseline_pmcと完全一致で比較する。層1のMC開始/engine停止に合わせたexpected_pmcが必要。保存した表示baseline自体は書き換えない。 |

追加の現状確認（食い違いには数えない）: `O/hwdev/gf/devinit.c:74–78` はWORDで0102hを書き読戻す。`internal.h:209` の常設mapはBAR0/1だけ。
`bus.c:26–27` のBAR1許可は307200Bまで。`O/docs/02_memory.md:152` のPT予算もBAR0 16MiB＋表示300KiBまでで、PRAMIN写像を含まない。
`service.c:25–29` はacquire→init→baseline→scratch解放。`internal.h:195–198` のownerに層1の生存期間hookはまだ無い。
`O/drivers/video/geforce/backend_geforce.c:99–102` の停止はvideo_stopへ到達。`geforce_module.c:98–99` はimport契約を持つ。新IRQ依存は契約にも追加が必要。
IRQ先例は `O/drivers/net/e82557/e82557.c:697–704`（共有登録→BM）、`:754–762`（DMA停止→登録解除→BM復元）。VBLANKとPIT待ちは別物（比較:29）。
原本補完: [MC nv44](https://github.com/torvalds/linux/blob/c03c1a14507dfaf8b856c2608f9ac216021fc222/drivers/gpu/drm/nouveau/nvkm/subdev/mc/nv44.c)、同ディレクトリのnv04.c/nv17.c/base.c、[表示IRQ](https://github.com/torvalds/linux/blob/c03c1a14507dfaf8b856c2608f9ac216021fc222/drivers/gpu/drm/nouveau/nvkm/engine/disp/nv04.c)、nvkm/core/intr.c。行番号は同コミットの原本。リポジトリには保存しない。

## 2. 境界と状態の所有者
`struct gf_production` が層1を1個所有し、display.owner経由で開始/停止。IRQのargも同じ層1。scratch・ROMと一緒に解放しない。
既存 `struct gf_io`（`O/hwdev/gf/internal.h:35–44`）を通常処理/偽MMIOで再利用。IRQはPCI再照会・window切替・待ちをしない常設MMIO adapterを使う。

```c
/* 新規 foundation.h。以下は内部ABI。既存公開 video_ops は変更しない。 */
enum gf1_rc { GF1_OK=0, GF1_EPCI=-101, GF1_EMAP=-102, GF1_ENOSPC=-103,
              GF1_EINVAL=-104, GF1_EIRQ=-105, GF1_ESTOP=-106, GF1_EIO=-107 };
struct gf_inst { u32 off, size; struct gf_inst *next; }; /* 呼出側所有、freeまで不動 */
struct gf_imem { u32 vram_bytes, reserved, aperture_bytes;
                 struct gf_inst vbios, ramht, ramro, ramfc, *used; };
struct gf_irq_stats { u32 entries, fifo, graph, vblank[2];
                      u32 pmc, pfifo, pgraph, nsource, nstatus, unknown; };
struct gf_foundation { struct gf_io io; struct gf_imem imem;
  struct gf_irq_stats stats; volatile u8 *bar0, *bar2;
  u32 wanted_fifo, wanted_graph, wanted_heads; /* IRQ要求mask */
  u32 expected_pmc; u16 expected_command; u8 irq_line;
  bool bm, mapped, registered, running; int last_rc; };
int gf_master_enable(struct gf_foundation *); /* cfg16/8、RUNへ */
int gf_master_disable(struct gf_foundation *); /* gf_engine_stop成功後のみ */
int gf_instmem_init(struct gf_foundation *);
int gf_inst_alloc(struct gf_foundation *, u32 bytes, u32 align, bool zero,
                  struct gf_inst *out);
void gf_inst_free(struct gf_foundation *, struct gf_inst *);
int gf_inst_read32(struct gf_foundation *, const struct gf_inst *, u32 off, u32 *);
int gf_inst_write32(struct gf_foundation *, const struct gf_inst *, u32 off, u32);
int gf_irq_attach(struct gf_foundation *); /* irq_register、sourceはまだ閉じる */
int gf_irq_enable(struct gf_foundation *, bool fifo, bool graph, unsigned heads);
int gf_irq_handler(unsigned irq, void *arg); /* irq_handler_fnそのもの */
void gf_irq_snapshot(struct gf_foundation *, struct gf_irq_stats *);
int gf_irq_detach(struct gf_foundation *);
void gf_irq_poll(struct gf_foundation *); /* IRQ0、running時に1巡 */
/* irq.cのvoid gf_tick_poll(void *arg)がgf_irq_poll(arg)へ型を接続。 */
int geforce_tick_bind(void (*poll)(void *), void *arg); /* 常駐adapter、NULLで解除 */
int gf_engine_stop(struct gf_foundation *); /* engine resetパルス、投入は再開しない */
int gf_foundation_start(struct gf_foundation *);
int gf_foundation_stop(struct gf_foundation *);
/* gf_display_ownerに追加、diagnostic ownerにもadapterを実装する */
int (*foundation_start)(struct gf_display_context *);
int (*foundation_stop)(struct gf_display_context *);
/* productionの資源管理adapter。fakeでは同契約を置換する。 */
struct gf_instmem_map_ops {
  int (*map)(void *owner, const struct recover_bar_extent *bar,
             u32 bytes, volatile u8 **out); /* 予約→UC map、失敗時内部巻戻し */
  int (*unmap)(void *owner, volatile u8 *ptr, u32 bytes); /* unmap→予約解除 */
};
```

- `gf1_rc`は層1内部の詳細。既存video層の0/-1契約にはadapterで変換し、`last_rc`とserialに詳細を残す。IRQ登録の元のIRQ_ERR_*も診断値に残す。
- PCI adapterは既存pci_cfg_*、map adapterは既存資源確認とpaging_map_phys/P2V_IOを接続。map_opsとownerはproductionが所有し層1へ貸す。追加mapの成功後だけ`mapped=true`。BAR再配置はしない。
- 起動: route00/資源確認→初回N1のBM enable→既存ROM/A2/main/baseline/modeset/CHECK→層1start→初回表示/route01。startの失敗出口はstopで巻き戻す。
- 層1start: 毎回gf_master_enable→nv44 MC初期化→instmem→tick bind→IRQ attach→短いirq_save/restore内でrunning=true・source ACK/enable・PMC enable。初回N1との重複は冪等。PFIFO/PGRAPHのチャネル初期化はしない。
- 終了: 新しい呼出しを止める→層1stop→既存局所復元/1850/1854/route00/98復帰。BM操作だけのために全体をCLIにしない。
- 再開時もservice.c:24–31のinitialized/validate経路から同じ層1startへ入る。停止済みの二重stopは成功。層2は停止前に投入停止/完了待ちを追加し、resetを完了通知に使わない。

## 3. (a) Bus Master と停止

| 順・前提 | 手本（関数・file:line） | OS32の操作/誤り |
|---|---|---|
| 1 資源の識別後、チャネル生成前 | `N/nouveau_drm.c:858 nouveau_drm_probe`、`:872`device作成→`:877`競合除去→`:881 pci_set_master`→`:892`enable_device→`:896`device_init。resumeも`:1074,1200`でset_master | N1をRUN=0106hへ変更し、層1startの先頭でも毎回gf_master_enable。Command(04h)を16bit書込/読戻し、不一致GF1_EPCI。再開始も0106h。 |
| 2 latency timerも扱う | 同`:881`がLinux PCIサービスに委譲。nouveau本体にはlatency補正関数/閾値なし | cfg8 latency(0Dh)が16未満なら64、以上なら保持。変更時は0Chをread16し下位cache lineを保存、上位を64にしてwrite16(0Ch)、read8(0Dh)で確認。不一致EPCI。0Eh/0Fhには書かない。 |
| 3 IRQを閉じる | `N/nvkm/subdev/mc/nv04.c:82 nv04_mc_intr_unarm`は140h=0→読戻し。表示は`N/dispnv04/disp.c:57 nv04_display_fini`の`:67–69` | 短いirq_save/restore内でrunning=false・tick参照解除→PMC閉→§5の各source mask/ACK。停止中は再armしない。 |
| 4 GPUのエンジンを停止 | `N/nvkm/core/subdev.c:54–85 nvkm_subdev_fini`はnvkm_mc_reset、`engine/device/base.c:2964–2975 nvkm_device_fini`はsubdev逆順。`mc/nv17.c:27–30`のGR=1000h/FIFO=100h/MPEG=2h | FIFO→GR→MPEGごとに200hの対象bitをclear→read→set→readするresetパルス（各MC disable/enableに対応）。読戻し不一致ESTOP、BM維持。無投入・source mask済みで実施、表示bit保持。 |
| 5 engine停止確認後 | `N/nouveau_drm.c:926–940 nouveau_drm_remove`:後始末→nvkm_device_del/free_irq→pci_disable_device、suspend`:1047–1052`も後始末→PCI停止。`O/drivers/net/e82557/e82557.c:758–759`はunregister→BM解除 | irq_unregister成功→Command=0102hをWORD書込/読戻し→STOP期待値→instmem/map解放。latencyは戻さない（pci_clear_masterと同じ）。EIRQならBM維持、EPCIならmap/所有状態を残す。 |

- 手本との差: RUN=0106h固定はOS32既存のdecode方針（I/O無効/MEM有効/SERR有効）を残してBMだけ撤回するため。Linux一般のCommand全bit方針の移植ではない。
- latencyの16/64は**nouveau内に無いOS32の明示方針**。Linuxの通常のPCI latency調整相当を狙うが、arch override/max_latency可変設定は持たない。GPL PCI実装は転記しない。
- engine停止のFIFO→GR→MPEGは**合成した順で、手本の順は未確認** (nvkm_device_fini の subdev の逆順を決める layout.h が refs に無い。MPEG→GR→FIFO の可能性がある。層1は投入済みの仕事が無いので順による害は無い見込み)。この合成列は手本に一体のS0列としては無い。層1は投入済み仕事が無いため各engine finiのresetパルスを合成し、層2は前段に固有の停止処理を追加する。
- 200h読戻しは `nv04_mc_device_enabled:49–51` と同じ状態確認。読戻し失敗時にBMを先に落とさないのは「GPU停止後にBM解除」という契約を守るため。
- `bus.c:29,109`は`expected_command`を照合（RUN/STOP両方でcleanup可能）。`:109`のdecode_safeはA2/通常validateからも呼ばれるためSTOP専用にしない。診断のgf_stage_state.c:63もexpected_commandを照合。
- `devinit.c:105`と`gf_stage_state.c:70`はexpected_pmcを照合。開始前baseline_pmc、MC初期化後ffffffffh、正常停止後もresetパルスの最終setでffffffffh。段の保存値との完全一致は使わず、既存表示baselineは保存のまま。
- `recover.h:160`の単一OVERRIDEをRUN/STOPの名前に分ける。`recover_profile.c:221–231`のS0/RESTOREにはSTOPを使い、その前に同じengine停止列を登録する。`:297`のBM=0確認は維持。
- restoreでBIOS由来0106hを自動復元しない。OS32は停止状態を返すので0102hを終値とする。既存RECログ・quiesce_count/予算式（同`:244–264`）も追加操作数で再計算する。

## 4. (b) VRAM / PRAMIN / instmem

| 順・前提 | 手本（関数・file:line） | OS32の配置・処理/誤り |
|---|---|---|
| 1 MC初期化 | `N/nvkm/subdev/mc/nv44.c:27 nv44_mc_init`、`:30–37`:10020ch読む→200h=ffffffffh→1700h=読値→1704h=0→1708h=0→170ch=読値 | BIOS/表示初期化後、IRQを閉じて同列。ここを飛ばしてinstmemの写像だけ作らない。既存の表示基準値採取との二重初期化は試験で照合。 |
| 2 容量と予約量 | `N/nvkm/subdev/fb/ramnv44.c:27 nv44_ram_new`は10020ch & ff000000h。`instmem/nv40.c:158 nv40_instmem_oneinit`の`:168–177` | V=上記容量。vs=popcount((1540h>>8)&ffh)、R=align4096((4980h*vs+4000h)*32+100000h)。`gr/nv40.h:39–44 nv44_gr_class`で4Ahは真。浮動小数なし。 |
| 3 VRAM末尾を確保 | 同oneinit`:164–179`の末尾予約＋heap初期化 | 表示[0,4b000h)、一般VRAM候補[4b000h,V-R)、instmem予約[V-R,V)。V>=R+307200を確認、失敗ENOSPC。BAR1の長さを物理VRAM容量として使わない。 |
| 4 PRAMIN aperture | `instmem/nv40.c:238 nv40_instmem_new`の`:249–253`はBAR2_INSTをmap。`:146,152`のrd32/wr32と`:48–58`のobject accessはaperture+instance offset。`engine/device/pci.c:1564–1577 nvkm_device_pci_resource_idx`は64bit BARを2slotとして数える | OS32の第三memory resourceを照合し、先頭R byteを常設UCでmap。PCI configのBAR slot番号と配列indexを混同しない。resource長>=R、map失敗EMAP。BAR0+700000hの旧nv04経路に置換しない。 |
| 5 固定領域を順に確保 | oneinit`:183–206`:VBIOS→RAMHT→RAMRO→RAMFC | instance offsetでVBIOS[0,10000h)、RAMHT[10000h,18000h)、RAMRO/pad[18000h,20000h)、RAMFC/pad[20000h,40000h)。VBIOSは保存、RAMHT/RAMFCはzero、RAMROはfalse。同じ順にallocatorで確保。 |
| 6 可変領域 | `nv40_instobj_new:120–138`のsize/alignment付きheap確保、`nv40_instobj_addr:87–89`はnode offset | [40000h,R)からfirst-fit。caller-owned gf_instをoffset順リストへ挿入、freeで除去。align=0は1、非2冪/size0/範囲外はEINVAL、空き不足ENOSPC。free後の穴も再利用。 |
| 7 CPU書込→利用側へ | `nv40_instobj_release:68–70`のwmb、rd32/wr32`:48–58` | instmemはf->bar2を直接UCで32bit読書きしgf_ioを通らない（bus.c:25–27の制限を回避）。偽MMIOはmap_ops差替え。整列確認、zero指定だけゼロ化、publish前compiler barrier＋aperture読戻し。MMIO失敗EIO。 |

- **予約区間とinstance addressは別**。`gf_inst.off`はPRAMIN aperture相対であり、`V-R+off`をGPUアドレスとして返してはいけない。
- 層2へはramht/ramro/ramfcのハンドルを貸す。RAMHTはoff=10000h,size=8000h、RAMRO実使用200h＋padding、RAMFC実使用1000h＋paddingを含め予約する。
- NV44AのRAMFCレジスタ設定は `N/nvkm/engine/fifo/nv40.c:181 nv40_fifo_init`のdefault`:214–217`、`((V-512KiB+ramfc.off)>>16)|00030000h`。**層1では書かず**、層2にV/offを渡す。予約下端を起点にしない証拠でもある。
- 手本との差: LinuxのWC mapではなく既存OS32 MMIOと同じUC。まず順序を固定でき、instmem書込量も小さいため。PRAMINをVRAMの新しい表示面として登録しない。
- allocatorはnouveauの汎用mmを移植せず、同じsize/alignment/free契約の小さいリストを実装する。単一装置・協調呼出しなのでmutexは不要。IRQからは呼ばない。
- RAMHTのzero根拠は `N/nvkm/core/ramht.c:141 nvkm_ramht_new`の`:157`（gpuobj_newのzero=true）。解放は層2の可変handle→固定4領域→mapの順、固定領域の個別freeは公開しない。
- BAR2 mapのPTはceil((BAR2先頭の4MiB内offset+R)/4MiB)頁を追加予算化。`docs/02_memory.md:152`の既存5頁に含めない。OS RAMをR byte確保するわけではない。

## 5. (c) PMC / PFIFO / PGRAPH / PCRTC IRQ

| 順・前提 | 手本（関数・file:line） | OS32操作/誤り |
|---|---|---|
| 1 登録前は全source閉 | `mc/nv44.c:43–44`→`mc/nv04.c:82 nv04_mc_intr_unarm`、`disp/nv04.c:29 nv04_head_vblank_put` | 140h=0→read、2140h=0、40013ch=0、600140h/602140h=0。8140h/9140h/1140h/B140h=0→8100h/9100h/1100h/B100hへffffffffhをW1C。tick bind成功後irq_register(6,gf_irq_handler,f,IRQ_F_SHARED)。失敗EIRQ。PCI Interrupt Line(3Ch)も読んでserialに記録、対象機の6と一致を受入条件とする。 |
| 2 sourceのpendingを消して開く | `fifo/nv40.c:223–224 nv40_fifo_init`:2100h=ffffffffh→2140h=ffffffffh。`gr/nv40.c:302–303 nv40_gr_init`:400100h=ffffffffh→40013ch=ffffffffh | 同ACK/enable列をIRQ部品に用意。通常の層1startではFIFO/GRも有効化可能（チャネル/engine起動操作はしない）。偽MMIOで全要因、実機では無投入状態の静穏を確認。 |
| 3 VBLANKと集約を開く | `disp/nv04.c:37 nv04_head_vblank_get`:600140h+head*2000h=1。`mc/nv04.c:72 nv04_mc_intr_rearm`:140h=1 | PCRTCのpending bit0をACK→head A enable=1、B=0→最後に140h=1。先行ACKはOS32差分:前回のpendingを新しい観測期間の1回に数えないため。 |
| 4 集約を確認 | `core/intr.c:164 nvkm_intr`は`:178`unarm→`:183`pending→`:194`dispatch→`:225`rearm。`mc/nv04.c:94`は100h読取。`mc/nv17.c:35–41`の表 | handlerは!runningなら再armせずIRQ_NONE。入口で140h=0/read→100h snapshot。FIFO=100h、GR=1000h、表示=03010000hでdispatch。PMC100h自体にはACKを書かない。0ならrearmしてIRQ_NONE。 |
| 5 PFIFO記録/ACK | `fifo/nv04.c:406 nv04_fifo_intr`は`:411–416`enable&status→CACHES保存/0、`:455–461`未処理mask/ACK→CACHES復元 | enable&2100hを記録、件数加算、同値を2100hへW1C。層1はcache-error/DMA-pusher修復やGET進行をしない。残留bitは2140hから落とし記録。 |
| 6 PGRAPH記録/ACK | `gr/nv40.c:232 nv40_gr_intr`は`:238–240`status/nsource/nstatus→`:267`W1C→`:268`FIFO再開 | 400100h/status、400108h/NSOURCE、400104h/NSTATUSを保存→400100hにstatusをW1C。400720h=1による再開はしない。残留bitは40013chから落とす。 |
| 7 VBLANK記録/ACK | `disp/nv04.c:80 nv04_disp_intr`は`:84–95`両head status→notify→各bit0 W1C。`dispnv04/crtc.c:1277`はその後のDRM通知 | 600100h/602100hを読み、enable中のbit0に対してvblank[head]++→当該statusへ1を書く。無効headのpendingもACKするが数えない。表示集約bit16(PVIDEO)では8100hを読み同値をW1C（8140hは閉のまま）。 |
| 8 解除 | `dispnv04/disp.c:67–69`が両PCRTCを閉じる。MC unarmは上記 | §3順にmask/ACK→engine reset→irq_unregister→BM解除→map解放。共通IRQ層がPIC/EOIを持つのでドライバからEOIしない。 |

- レジスタ定義出所: `N/nvkm/engine/fifo/regsnv04.h:8–9`、`gr/regs.h:10–11,41`（**NV40 enableは40013ch、400140hではない**）。NSOURCEは同ヘッダのNV03_PGRAPH_NSOURCE。
- ACKだけの層1handlerは手本の完全handlerと異なる。PFIFOのCACHES操作/修復、PGRAPHのFIFO再開を省く理由は、層1にchannel/object/再開契約が無いため。層2がその部分を追加する。
- ACK後の残留sourceをmaskし、未知PMCは記録してPMCも閉じIRQ_DEFERRED。手本`core/intr.c:210–217`は未処理IRQをblock。OS32ではtickが毎回gf_irq_pollを1巡呼び、PMC集約/現在のenableで省略せず各既知statusを要求maskで回収・再ACKし、未使用の8100h/9100h/1100h/B100hもmask/ACKし、残留が消えたsourceだけ要求enableへ復元、PMC残件なしなら140h=1へ戻す。未知が続く間もtickは既知のVBLANK等を回収する。
- 残件なしは最後に140h=1→IRQ_HANDLED。IRQ内は1巡、printf/alloc/待ち/復旧をしない。snapshotは短いirq_save/restoreでstructをコピーし、serial出力は呼出側。
- R5根拠: `N/nvkm/engine/mpeg/nv31.c:224,240–241 nv31_mpeg_init`はstatus ACK→enable。PMPEGを閉じたままにするのは層1で使わないため。PVIDEO/PTIMER/PBUSの番地・各initとの対応は`REVIEW.r1.md:R5`を根拠とする（指定commitのdisp/timer/busソースはローカル未取得、取得も失敗したため関数の行番号は未検証。実装時に照合）。
- tick契約は`O/kernel/irq/irq.h:14–19`。常駐geforce_tick.cがslot6へtick_register、`tick.h:5`を7、tick_order末尾にPC98_TICK_GFを追加。登録失敗EIRQ。解除APIが無いのでmodule関数をtick表へ直接残さず、常駐trampolineがcallback/argを所有する。
- bind(NULL,NULL)はirq_save/restore内で参照を消す。登録は前景で1回、同一登録は冪等。running=true時は毎tick回収（deferred限定にしない）。stop/失敗巻戻しはrunning=false→unbindをunregister/unmap/module detachより先に実施。gf_irq_pollは要求maskをstateに保持し、待ち・alloc・printfなし。真に未知の残留要因には推測でACKしない。
- 未知の要因でPMCを閉じているあいだはVBLANKをtickでしか拾わないので、tickの周期が垂直同期より遅いと少なく数える (受入の「A 増加」には影響しない)。
- counterはu32の自然wrap、差分はunsignedで計算。全経過フレーム数ではなく「IRQで観測したVBLANK数」。長いCLI中の取りこぼしを補間しない。

## 6. 像・部品の大きさ（実装前の予算、KiB）

| 入れ先 | 増分text/rodata | 常駐RAM / VRAM | 制限への影響 |
|---|---:|---|---|
| geforce.mod | master 0.6、instmem 1.8、IRQ 2.5、engine stop 0.3、統合/map 1.0、計約6.2（予算8） | 状態約0.3KiB＋callerの割当記述、追加PTは§4。VRAM Rは末尾を予約 | 大きい本体をkernel/起動FDへ複製しない。modをFDへ同梱する構成なら格納量+約8KiBを要確認。 |
| kernel | IRQ import/停止adapter・常駐tick 約0.5（予算0.8） | 追加PT以外の固定BSSは小ポインタ群のみ | `include/memmap.h:230`のMEM_KERNEL_IMAGE_MAXと既存link ASSERTで確認。 |
| kdebug.bin / 診断部品 | recover列・診断入口約0.8（予算1.5） | stats snapshot約0.05KiB、ログ予算増 | 層1本体を複製せずmodule側試験入口を使う。KDEBUG=0で本番がリンクすること。 |

許可された資料には現在のFD空き量/kdebug.bin上限の数値がないので「収まる」とは断定しない。実装段で既存の像サイズ検査と実ビルド前後差を必須にし、上限は緩めない。今回ビルド/配備はしない。

import現状は`build/out/geforce/geforce_imports.h:34`の20。irq_register/unregister・irq_save/restore・geforce_tick_bind・pci_cfg_read8・map系を加え28〜30を予算とし、`tools/module_schema.py:184–191`の上限32は維持。tick_registerは常駐adapterから直接呼ぶためmodule importには追加不要。生成した実数を受入で確定し、生成物は手編集しない。

## 7. ファイル別実装段（実装Cは1ファイル=1段=1実装者、宣言のみA0へ統合）
下表の行ごとに担当を渡せる。同一帯は契約凍結後並列可。各段は半日、複雑な新規Cは1日。行数は増分概算。PM判断により宣言だけの変更はA0にまとめる。

| 段 | ファイル（O/相対、新/変） | 単一の役目 | 行数 / 依存 |
|---|---|---|---|
| A0 | hwdev/gf/foundation.h 新、hwdev/gf/production.h・hwdev/gf/internal.h・kdebug/recover.h・kernel/irq/tick.h・platform/pc98/tick_order.h 変、drivers/video/geforce/geforce_tick.h 新、tools/tests/tick_host.c・tools/tests/test_tick.py 変 | 内部ABI・所有state・hook・RUN/STOP・tick枠6→7の宣言。tick の試験も 7 枠に (tick_host.c:34 の callbacks[] を 7 個に、test_tick.py:12-21 の OWNERS と :3 の covers に geforce_tick.c) | 計170 / 最初 |
| B0 | hwdev/gf/master.c 新 | PCI BM/latency | 80 / A0 |
| B1 | hwdev/gf/instmem.c 新 | instance確保/free/access | 180 / A0 |
| B2 | hwdev/gf/irq.c 新 | 登録/dispatch/ACK/counter | 240 / A0 |
| B3 | hwdev/gf/engine_stop.c 新 | engine停止 | 40 / A0 |
| B4 | hwdev/gf/mc.c 新 | nv44 MC初期化 | 35 / A0 |
| B5 | hwdev/gf/instmem_map.c 新 | BAR2資源/mapの生存期間 | 80 / A0 |
| B6 | drivers/video/geforce/geforce_tick.c 新 | 常駐tick登録・callback参照管理 | 55 / A0（kernelへ配置） |
| C0 | hwdev/gf/foundation.c 新 | 開始/終了の順序。Command の書き込みと expected_command の更新のあいだに MMIO を挟まない | 100 / B群 |
| C1 | hwdev/gf/devinit.c 変 | N1/owner adapter | 35 / B0,C0 |
| C2 | hwdev/gf/bus.c 変 | 状態別Command照合 | 12 / B0 |
| C3 | hwdev/gf/service.c 変 | 初回/initialized再開始の両経路からCHECK後start（先頭BM enable）へ接続 | 15 / C0 |
| C4 | hwdev/gf/display/quiesce.c 変 | 全停止入口の共通stop接続 | 12 / C0 |
| C5 | drivers/video/geforce/geforce_module.c 変 | tick unbind/IRQ登録解除済みをdetach条件にする | 20 / B2,B6 |
| C7 | kdebug/recover_profile.c 変 | engine停止列とS0/REC予算 | 55 / B3,A0 |
| C8 | kdebug/gf_stage_state.c 変 | :63 Command・:70 PMCをexpected_command/expected_pmcと照合 | 10 / A0,C0 |
| C9 | kdebug/gf_foundation_test.c 新 | serial試験adapter | 100 / C0 |
| C10 | tools/prototypes/geforce_imports.def 変 | import契約追加（kexports生成元） | 50〜70 / A0,B5,B6 |
| C11 | build/geforce-module.mk 変 | 新部品/常駐tick配置とimport生成依存 | 20 / B群,C10 |
| D0 | docs/design/hwdev/gf/devinit.md 変 | N1/試験値の訂正 | 15 / C群 |
| D1 | docs/design/hwdev/gf/README.md 変 | 新部品索引/開始終了列 | 15 / C群 |
| D2 | docs/02_memory.md 変 | BAR2 PT予算の追記 | 8 / B5 |
| T0 | tests/gf_foundation_test.c 新（既存host配置規則に合わせる） | 偽PCI/MMIO/IRQの順序検査 | 240 / B〜C群 |

`build/geforce-module.mk:3–6,23–33`が`tools/gen_sqlite_imports.py:162–171`からimportsとgeforce_kexportsを生成する。`tools/module_schema.py:189`の32上限を検査し変更しない。back-endの描画関数表とGUIは変更不要。資源予約定数の追加が必要なら実定義元を1段追加。
診断ownerも新hookを満たすよう、その定義元を同様に1段追加。型だけ変えてNULL呼出しを残さない。1機能を複数の新規実装Cへ分散しない。

## 8. 受入→試験・変異、段の検査

| 受入 | ホスト偽MMIO/実機Ra266 serial | 検出すべき変異（各1件） |
|---|---|---|
| BM/latency | host:0106/0102両初期値→RUN、latency=0/15/16/64/255、読戻し失敗。実機:起動0106h→停止0102h→再開始0106hとlatency記録（cache line/BIST不変、停止でlatencyを戻さない） | CommandをDWORD書込にする→Statusへの書込で失敗 |
| 停止順 | host:tick unbind/mask→engine resetパルス/read→unregister→BM clear→unmap、各段失敗時の所有状態。実機:繰返し開始/終了と98復帰 | BM clearをengine停止より前へ→順序assert失敗 |
| BM再開始 | host:initialized=trueの停止→再開始でもBM enableが先頭。実機:再開始後Command=0106h | startのBM enableを削除→再開始時BM=0を検出 |
| tick回収 | host:PTIMER/PVIDEO残留・DEFERRED→tick→PMC再開、未知継続中のVBLANK回収、停止/unmap/detach後callbackなし。実機:再開始後もA増加 | tick回収末尾の140h=1を削除→再arm検査失敗 |
| PMC検証/import | host:開始/停止後も段の検査が成功。生成import数<=32、常駐tickの配置/参照解除を確認 | :70を保存PMCとの比較へ戻す→停止後検証失敗 |
| instmem | host:vs=0..8、V不足、BAR2不足、整列/穴再利用/終端、固定4領域。実機:可変領域1頁へpattern/readback、表示CRC前後一致 | offにV-Rを足す→apertureアクセス範囲/固定配置試験失敗 |
| IRQ/共有 | host:PMC0でNONE、FIFO/GR同時、W1C、未知/残留、他デバイスとの共有登録。実機:IRQ6登録成功と静穏 | PGRAPH enableを400140hへ→期待レジスタ列失敗 |
| VBLANK | host:両head bit/無効head/wrap、snapshot。実機:表示中の固定観測時間でA増加、B不変、mask中停止、再enableで増加 | PCRTC ACKを省く→偽level IRQの残留検出 |
| 全終了入口/像 | host:初期化途中失敗、video_stop、backend停止、detach、recover S0。KDEBUG=0/1で既存check/サイズ検査。実機:終了後IRQ増加なし | mapをunregister前に解放→handlerが無効mapへ触る検査失敗 |

- A群: 宣言/既存型接続のコンパイルを凍結。B群は並列、T0のfake骨格も並行。C群はB完成後に並列、最後にC0担当が通し列を検査。
- D群とserial入口は並行。統合半日〜1日で既存make check・KDEBUG両構成・module import生成/像サイズ検査、次の半日でRa266シリアル受入。
- 実機は既存手順で操作者が実施。層1でDMA転送成功を主張しない（層2の試験）。IRQエラーの実機注入に未知レジスタを書かず、分岐はhost、自然VBLANK/開始終了を実機で検証。

## 9. 未決（ユーザー判断が必要なもののみ）
**0件。** 上記を推奨案として実装可能。残るBAR2実測/像の空き/受入値は実装時の検査項目であり、範囲決定をユーザーへ差し戻す事項ではない。

反論: 0件。

## 確定 (2026-11-02、Opus 第 2 回の条件付き承認、PM が反映)
- N1: A0 の段に tick_host.c・test_tick.py の 7 枠への更新を足した。
- R2 の条件: C0 に「Command の書き込みと expected_command の更新のあいだに MMIO を挟まない」。
- R3: engine 停止の順は合成で、手本の順は未確認と書き直した。
- P3: PMC を閉じているあいだの VBLANK の数の意味を §5 に 1 行。

## B 段の出し方で PM が決めたこと (2026-11-02)
- 層 2 の Opus レビュー F4 と同じ理由で、T0 (1 ファイルの host 試験) を B 段ごとに分ける: 各 B 段は自分の部品の host 試験 `tools/tests/gf1_<部品>_host.c` と `tools/tests/test_gf1_<部品>.py` (CHECK 宣言・変異) を成果物に含める。T0 は C 段の後の結合の試験 (開始・停止の順) に絞る。
- B 段の .c はモジュールへの結線 (C11 の build/geforce-module.mk・C10 の import) の前なので、B 段ではホストの試験で組んで確かめる。像への組み込みは C10・C11。
- 実装者: B2 (irq.c、判定の論理) は sol、ほかの B 段は terra。

## B 段の実装で分かった欠けの追記 (2026-11-02、astra 起草、Opus 第 3 回、PM 反映)


> 確定本文 §2 の「map 時に予約、unmap 後に予約解除」は PM 決定で覆す (BAR の予約は起動時の永久予約のまま、層 1 は map/unmap だけ)。
O/ は /home/hight/os32-v3/、N/ は /home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/。
BAR の予約は起動時の永久予約（O/drivers/video/geforce/geforce_platform.c:14–42）のまま、層1は map/unmap だけを所有する。
手本: N/nvkm/subdev/instmem/nv40.c:249–253 は BAR を ioremap_wc するだけ、:222–223 は iounmap；:164–179 の予約は VRAM 末尾の instmem 用であり BAR decode の再予約ではない。

### 欠け1 — build 対象を決める
B の前に build/geforce-module.mk の追補を出し、GEFORCE_SOURCES の wildcard 結果から次のファイルを filter-out する。
```make
GEFORCE_LAYER1_PENDING := hwdev/gf/master.c hwdev/gf/instmem.c hwdev/gf/irq.c \
 hwdev/gf/instmem_map.c hwdev/gf/foundation.c
GEFORCE_SOURCES := $(filter-out $(GEFORCE_LAYER1_PENDING),$(GEFORCE_SOURCES))
```
配置は既存 GEFORCE_SOURCES 定義の直後、GEFORCE_SERVICE_OBJS 算出より前。B6 の常駐 tick も C11 まで kernel に追加しない。
理由: 未完成の import/provider に依存せず、末尾の「B は host 単独検証、像への組込みは C10/C11」を維持できる。
各 B の host 試験は当該 .c を直接組む。tools/checks/map.py の needs=['make:all'] は残し、check-changed の準備も通す。
C10 の import 契約と B6 provider を揃え、C11 で除外を撤去し常駐 tick を配置する；層1を参照する C の接続変更はこの結線と一緒に検査する。
C10 は下記 production map adapter の paging_map_phys・paging_set_not_present も import に含め、既存 paging_pte_raw を再利用し、32 枠上限を検査する。

### 欠け2 — resource・V・R と呼出し順
resource の入口は既存 gf_production_identify → boot_resources → gf_production_prepare(boot) → p->display.bars（O/hwdev/gf/devinit.c:30–36）。
C1 の owner start adapter は既存の資源再照合成功後、第三 memory resource の p->display.bars[2] を f->instmem_bar に値コピーしてから C0 を呼ぶ。
これは PCI BAR slot を再解釈する経路ではない。diagnostic/fake owner も照合済みの同じ resource を設定する。
C0 はその snapshot を使い、map_owner を具体型へ cast しない。snapshot は scratch/ROM と独立し、失敗した stop の後も保持する。
C1 は B5 提供の gf_production_instmem_map_ops と production owner を貸す；fake は同契約の独自 ops/owner を貸す。
B1 の gf_instmem_init(f) が f->io で 10020ch と 1540h を読み、V=(10020ch の値 & ff000000h)、vs=popcount((1540h の値>>8)&ffh)、
R=align4096((4980h*vs+4000h)*32+100000h) を算出する。読取失敗は GF1_EIO、V<R+307200 は GF1_ENOSPC。
V/R を f->imem.vram_bytes/reserved に保存し、gf_instmem_map(f, &f->instmem_bar, R) に resource と R を渡す。
B5 は valid・ページ整列・範囲内・resource.length>=R を確認する。不成立/map 失敗は GF1_EMAP。
C0 の順は master_enable → mc_init（IRQ 閉）→ instmem_init〔V/R 算出 → B5 map → 固定4領域確保〕→ tick bind → IRQ attach → enable。
固定領域は VBIOS → RAMHT → RAMRO/pad → RAMFC/pad、map 成功前には確保も zero もしない。成功した map の aperture_bytes は R。
途中失敗は C0 の foundation_stop へ戻し、その順 (r2 §3・foundation.h:84-85) に従う (ここで列を書き直さない)。停止失敗時は必要な所有状態を保持する。

### A0 追補 — PM が出す変更行（リポジトリはこの依頼では変更しない）
foundation.h の struct gf_foundation に次の1行を追加する。
```c
    struct recover_bar_extent instmem_bar; /* Checked third memory resource, copied before start. */
```
foundation.h の struct gf_instmem_map_ops 定義の後に次の1行を追加する（定義と production callbacks は B5 の instmem_map.c）。
```c
extern const struct gf_instmem_map_ops gf_production_instmem_map_ops;
```
map_ops 直前の契約コメント3行を次の3行で置換する。
```c
/* Production owns adapter/owner; fake MMIO supplies the same contract.
 * BAR reservation is permanent from boot. map only maps UC; failure leaves no new map.
 * mapped is set only on success. unmap removes access, never the BAR reservation. */
```
B5 map 直前コメントの末尾2行を次の2行で置換する（bar/bytes の説明2行は維持）。
```c
 * GF1_OK publishes bar2/aperture_bytes and mapped=true; GF1_EMAP leaves no new map.
 * Foundation owns the successful map; the permanent boot reservation is unchanged. */
```
gf_instmem_init(f) と gf_instmem_map(f,bar,bytes) の関数宣言、production.h・internal.h は変更不要。
宣言の変更は追加2行（既存宣言の置換0行）、別に契約コメント5行を置換する。

### 欠け3 — map/unmap API と失敗
O/ で rg -n 'paging_map_phys|paging_unmap' mm include を実行し、paging_unmap は存在しないと確認した。
map は paging_map_phys(base, base, R/PAGE_SIZE, PAGE_RW | PTE_PCD)（mm/paging.h:124、mm/paging_map.inc:107）。PTE_USER は付けない。
B5 production adapter は geforce_range_reserved(base,R) の成功と、対象全頁の paging_pte_raw の Present=0 を先に確認し、既存の他者の map は上書きしない。
成功後だけ *out=P2V_IO(base) を公開する。引数/予約/PTE 検査の失敗は無変更、paging_map_phys の失敗も全範囲無変更・追加 PT 全解放（同 .inc:14–48,107–119）。
成功後に失敗し得る処理を置かず、map 失敗時の個別ページ巻戻しや ledger_reserve_set は呼ばない。B5 は失敗を GF1_EMAP に変換する。
unmap は paging_set_not_present(base, base+R-1)（mm/paging.h:143、mm/paging_map.inc:241）。end は inclusive；全対象の P を落とし TLB を flush する。
これはアクセス解除であり PTE の frame/属性や PT 自体は残る。PT は master の寿命まで保持し、再開始で再利用する。永久 BAR 予約も残す。
unmap 成功時だけ mapped=false・bar2=NULL・aperture_bytes=0。失敗は GF1_EMAP とし、それらと ops/owner を保持して stop 再試行へ渡す。
新 PT は live AS≠0 では作れない (mm/paging.h:114–116、paging_map.inc:22-25) ので、**BAR2 の PT は起動時に作る** (下の「確定」の B7)。B5 の map は既存の PT に P を立てるだけになる。
BAR2 の追加 PT 予算は §4 の ceil((base の4MiB内offset+R)/4MiB) を D2 に明記する。unmap による PT の回収を予算に織り込まない。
B1/B5 host 試験は V/R と map-before-zero、map拒否時の無変更、末尾頁だけまでの unmap、unmap失敗時の所有保持、再開始を検査する。

### 確定 (2026-11-02、Opus 第 3 回、PM が反映)
- P1 (欠け 3): GeForce の開始は GUI を起こしたとき (live AS≥1) なので、初回 map で新 PT を作る形では毎回 GF1_EMAP になる。BAR2 (0x25000000、PDE 0x94) の PT は今 geforce_platform.c:38-41 で作られていない (BAR0 の 0x90〜0x93 と表示の窓 0xC0 だけ、:7-9 の STATIC_ASSERT で 5 頁)。**段 B7 (drivers/video/geforce/geforce_platform.c) を足す**: reserve() で BAR2 の先頭 R_max を起動時に paging_map_phys(PAGE_RW|PTE_PCD) で張り、すぐ paging_set_not_present で閉じる。R_max = vs=8 の R = align4096((4980h*8+4000h)*32+100000h) = 618000h (PT 2 頁)、BAR2 の長さで切る。STATIC_ASSERT・GF_PRODUCTION_PT_PAGES・docs/02_memory.md:152 の予算を +2。AS は PDE ごと写すので、どの AS からも同じ PT が見え、B5 の「全頁 Present=0 を確かめて map → set_not_present で unmap」はそのまま成り立つ。手本 (ioremap だけ) との差は「OS32 では新しい PT を後から作れないため」。
- P3 (欠け 1): filter-out は未解決の import を持つものだけ (master・instmem・irq・instmem_map・foundation)。mc.c・engine_stop.c (B4・B3、main) は今も geforce.mod に組まれリンクできているので外さない。
- P3 (欠け 2): 途中失敗の後始末は foundation_stop の順に従う (列を書き直さない)。
- 段の順: **A0 追補** (foundation.h の追加 2 行・コメント 5 行、build/geforce-module.mk の filter-out) と **B7** (geforce_platform.c の BAR2 の PT) を先に出し、取り込んでから B0・B1・B2・B5 を続ける。

### B1 の再開で PM が決めたこと (2026-11-02)
- 4 の倍数でない大きさの確保: 大きさを 4 バイトに切り上げて確保し、zero もその大きさで 32bit 単位に書く (instmem へのアクセスは手本の nvkm_wo32/ro32 と同じく 32bit だけ。範囲の外には書かない)。size 0 は従来どおり拒否。
- 直接 MMIO の失敗: bar2 への volatile の読み書きには失敗の戻り値が無いので、読み書きの関数は失敗を返さない (EIO は map の失敗 = B5 の GF1_EMAP から来るものだけ)。§4 の「MMIO 失敗を EIO」は map されていない (mapped=false) ときの呼び出しを指すと読む — そのときは GF1_EMAP を返す。

### C0 で PM が決めたこと (2026-11-02)
- 停止の後の expected_pmc: foundation_stop は engine 停止 (B3 の reset パルス) が成功したら、PMC (200h) を gf_io で 1 回読み、その値を expected_pmc にする。MC の PMC の書き込みより前に失敗した場合も同じ (結果は「停止前の PMC | 0x1102」になり得る。§3:112 の「正常停止後 ffffffff」は MC が成功した場合の値としてそのまま成り立つ)。読み取りが失敗したら expected_pmc は変えず、停止の失敗として返す。手本は PMC の期待値を持たないので、OS32 の段の検査 (gf_stage_state.c:70) のための差分。B3・B4 は変えない。

## C 段の実装で分かった欠けの追記 (2026-11-02、astra 起草、Opus 第 4 回の条件付き承認、PM 反映)

### C 段の実装で分かった欠けの追記 (2026-11-02、astra 起草)
確定本文・PM 決定の撤回なし。O/、N/ と手本の版は本文と同じ。以下は内部 ABI の補完。

### 1. identity/BAR 失効は「停止未確認」のまま所有を残す
手本 N/nvkm/subdev/mc/nv04.c:82–106 は常設 MMIO で unarm/pending、PCI 再照会をしない。
N/nouveau_drm.c:881、:933–941 は master 有効化と device 後始末→PCI disable の順。
BAR が別物になった OS32 の旧 map でこの列を実行してよい根拠にはならない。本文の停止失敗時所有保持を適用する。
```c
/* foundation.h: struct gf_foundation の末尾へ追加（宣言1行） */
    bool revoked; /* sticky until reboot; not a successful hardware stop */
/* foundation.h: 関数宣言追加（1行）。定義は foundation.c */
void gf_foundation_revoke(struct gf_foundation *);
```
revoke は短い irq_save/restore 内で revoked=true、running=false にするだけ。PCI/MMIO/待ち/ログなし。
C2 は bus.c の失効確定箇所で p->revoked と上記を同じ CLI 区間で公開する。診断 bus も ops の REVOKE 経由で同期接続。
C1 の常設 read/write は f->revoked ならアクセスせず失敗。IRQ/tick は running=false により MMIO 前に退出。
C1 の stop adapter は前景で identity/BAR を照合し、失敗なら revoke 後、必ず通常の gf_foundation_stop へ入る。
```c
/* C0: stop の先頭。正常時は以後の既定 mask/reset/detach/BM/unmap 列へ */
f->running = false;                 /* 既存の短い CLI 区間内 */
rc = geforce_tick_bind(0, 0);
if (rc || f->revoked) { /* CLI を戻してから return */
    return f->last_rc = rc ? GF1_EIRQ : GF1_EPCI;
}
```
失効時は mask/ACK/reset/PMC 読み/PCI Command 書き込みを一切しない。expected_* と bm は最終確認値のまま。
registered、instmem の handle、map、map_ops/owner、常設 BAR0/1、永久予約を保持。IRQ unregister も行わない。
未停止の共有 IRQ source を閉じたとは扱わず、handler は IRQ_NONE。共有 IRQ6 全体を無効化しない。
tick unbind 失敗でも running=false を維持し、stop 再試行は参照解除だけ可能。失効を解除して再開始しない。
C4 は active=false、reusable=false、engaged と失敗を保持し、旧表示の局所復元へ進まない。
C5 は登録/tick 参照又は未解放所有がある限り detach を拒否し owner/module を保持（失効時は再起動まで）。
start と試験の可変操作（STOP/REVOKE を除く）も revoked を先に拒否。検出前の物理的抜去まで保証する新 hotplug 機構は作らない。

### 2. 診断 owner と stage の接続
```c
/* gfinit.c の static control 内（scratch/ROM/work には置かない） */
struct gf_foundation foundation; /* struct gfinit_owner に追加 */
/* gf_stage_state.h: 前方宣言と struct gf_stage_state の借用欄 */
struct gf_foundation;
const struct gf_foundation *foundation;
/* gfinit.c: baseline を構築し終え、capture/entry に渡す前 */
stage.foundation = &g->foundation;
```
production は従来どおり p->foundation、診断は control.foundation が唯一の所有者。両者は既存 device lease で排他。
診断は資源照合後、下記 ops.prepare で常設 io・BAR0・instmem_bar・map_ops/owner を接続してから N1 を行う。
初期 expected_command は STOP、初期 expected_pmc は main 後の baseline 採取時に設定。以後は master/MC/stop が更新。
診断 N1/start/stop hook は下記 call 経由。CHECK 後 start、全 cleanup/S0 前 stop、失敗なら lease/owner を解放しない。
C7 の正常な S0 列は維持し、失効時は同じ bus guard で拒否。正常 stop の PMC 更新は末尾 C0 決定に従う。
C8 は参照なし/失効なら失敗、Command/PMC は参照先 expected_* と比較し、保存 baseline は変更しない。
work 解放だけでは foundation をゼロ化しない。正常 stop と参照解除後に stage.foundation を外し、次 session を準備する。
停止失敗時は control・bus adapter・lease・module の寿命を保持し、ROM 等の作業領域とは切り離す。

### 3. serial/module 試験入口
Context 出力 slot に統一する理由: 層3と同じ module 内 dispatch を借用でき、geforce.mod の import を増やさない。
```c
/* geforce_module.h: 層1で VERSION を 2→3。Context 末尾（この順） */
const struct gf3_test_ops **accel_out; /* 層3の確定欄を先に確保、層1では NULL */
const struct gf1_test_ops **foundation_out;
/* 新 gf1_test.h（内部型。foundation.h の宣言変更には含めない） */
enum gf1_test_op { GF1T_MASTER, GF1T_START, GF1T_STOP, GF1T_SNAPSHOT,
                  GF1T_HEADS, GF1T_INST_PAGE, GF1T_REVOKE };
struct gf1_test_request { u32 size, op, heads; };
struct gf1_test_reply { int rc; u32 command, pmc, flags, bytes, bad_off;
                       struct gf_irq_stats stats; };
struct gf1_test_ops { struct gf_foundation *production;
  int (*prepare)(struct gf_foundation *, const struct recover_bar_extent *, u32);
  int (*call)(struct gf_foundation *, const struct gf1_test_request *, struct gf1_test_reply *);
};
const struct gf1_test_ops *geforce_foundation_ops(void); /* kernel の slot getter */
```
prepare の最後2引数は照合済み bars 配列と要素数。常設 adapter を貸し、未所有の診断 f だけを準備する（HW 書込なし）。
call は同期・前景専用、size/op/heads を検査し gf1_rc を戻して reply.rc にも記録。要求ポインタは内部だけで使用。
MASTER/START/STOP/REVOKE は既存関数へ、SNAPSHOT はソフト状態と stats を採取（失効後も可）。
HEADS は running 時だけ既存 FIFO/GR 要求を保ち gf_irq_enable。INST_PAGE は1頁を確保し32bit pattern/readback/free。
flags は bit0=bm、1=mapped、2=registered、3=running、4=revoked。command/pmc は期待値、実測と区別して出力。
INST_PAGE は bytes=比較済みbyte数、bad_off=先頭不一致（なしは ~0U）。他操作は bytes=0、bad_off=~0U。
module の新 hwdev/gf/foundation_test.c が表と dispatch/prepare を所有し、C11 は geforce.mod だけに組む。
C5 は kernel static slot の番地を Context へ渡す。module 初期化成功後に公開、abort/detach 前に NULL、旧版は拒否。
loader/manifest/host 期待値を3へ揃える。層3は確保済み accel_out を使い、同じ版上げを重複実施しない。
getter を kernel export 生成元へ追加し kdebug/hwdev の import に接続。geforce.mod は既存31本のまま、生成物は手編集しない。
gfinit_wire と hwdev_ops callbacks は変更しない。診断 gfinit.c と C9 は getter から毎回借用し、表を長期保存しない。
C9 は serial `gf1 status|instmem|vblank|cycle` を既存コマンド dispatch へ結線。表示中は production、診断中は lease owner の f を使用。
vblank は snapshot→1000ms待ち→snapshot を head A有効/両head無効/A再有効で各1回。待ちは C9 の既存時計、IRQ 有効のまま。
差分は u32 wrap、A増加/B不変→両方不変→A増加を検査し元の heads を復元。tick 回収中も無効headを数えないことを検査。
cycle は所有者の表示 stop/start 経由で STOP/RUN 実測 Command を採取、instmem は表示 CRC 前後一致も C9 で検査。
serial は case/owner/観測ms/rc/期待値/実測値/flags/stats差分/比較bytes/不一致offset を完了時出力。未公開は unavailable、合格にしない。
PM は宣言追補→C0/C1/C2/C4/C5→診断 owner 接続・C8→module dispatch/loader・export生成元・C9/serial結線を各Cファイル1段で出す。
受入に失効直後IRQ割込み、stop再試行、診断work解放、slot解除、3→旧版拒否、import<=32を追加。正常停止の既存試験も維持する。

### 確定 (2026-11-02、Opus 第 4 回、PM が反映)
- P2-a: 失効 (revoked) を見たら、foundation の停止は B5 の unmap (paging_set_not_present、CPU の PTE を落とすだけで BAR には書かない) を呼ぶ。旧 BAR2 の物理番地への PTE を Present のまま残さない。B1 の gf_inst_read32/write32 の API は変えない (unmap の後は呼ばれない — running=false の後なので)。
- P2-b: 失効でも 98 の表示への切り替え (backend_geforce.c:101 の pc98_route_to_98、GeForce の MMIO を使わない) は従来どおり行う。display/quiesce.c の gf_display_stop で変えるのは「失効のときは reusable=false を保持する」ことだけ (書き込みの拒否を全行試し、最後に engaged=false にする今の動きは変えない)。
- P2-c: 層 3 の確定設計の末尾にも同じ日付で 1 行足した (accel_out の欄と OPS_VERSION 3 はこの追記の §3 で確保済み、層 3 の C5 は 0 行)。
- P3: identity が一致して BAR だけが変わった場合に config 経由で Command=0102h を書くか (手本 nouveau_drm.c:940 の pci_disable_device 相当) は、DMA を出す層 2 で決める。層 1 は書かない。
- P3: drivers/video/geforce/geforce_module.h に `struct gf3_test_ops;` の前方宣言を足す。getter は kdebug の import にだけ足す (C9 と gfinit.c はどちらも kdebug)。prepare(f, bars, n) で貸す常設 adapter の opaque は診断の lease の bus (診断の f の revoked はその bus の REVOKE が立てる)。
- P3 の訂正 (2026-11-02、PM、CK の sol の指摘): 「getter は kdebug の import にだけ」は誤り。kdebug/gfinit.c と gf_stage_state.c は build/hwdev.mk:5-12 で -DHWDEV_MODULE として hwdev.mod に組まれる (kdebug.mk の KDEBUG_OBJS には無い)。getter は**それを使うファイルが入る像の import に足す** — gfinit.c・gf_stage_state.c は hwdev (import 37/128)、C9 (gf_foundation_test.c) は置く像に合わせる。追記の当初の記述 (:402、kdebug と hwdev の両方) に戻す。
- 段の順 (PM): gf_foundation_revoke (hwdev/gf/foundation.c) は CK が書く。CK を先に取り込み、CW をその上に進めて仕上げる。
- 段の分け直し (2026-11-02、PM): CK の 3 回目の停止 (C9 の cycle で診断表示の stop/start をどの入口・状態遷移で行うかが未規定、kdebug/gfinit.c:46-77・348-461、include/hwdev_private.h:15-22) を受け、CK を **C7・C8・gf_foundation_revoke・宣言の追補** に絞る。**C9 (実機のシリアルの試験の入口) は CW の取り込みの後**に、設計者が入口の契約を追記してから出す。

## CK の実装で設計者が決めたこと
- C7 の固定表は変動する PMC 値を表せないため、Command の QUIESCE rule に REC_GF_ENGINE_STOP を登録し write_verify から固定停止関数を呼ぶ；汎用 plan/式の実行契約は変更せず、FIFO→GR→MPEG の現値 clear/read/set/read を既存の guarded bus・REC・safe point で実施し、どの失敗も後続 Command/保存復元を止める。
- C7 の PMC は read-safe target として追加し、各 engine ごとの先頭 read を含む 15 アクセスと最大 30 map/unmap の REC pair を静的・実効 cleanup 予算へ加算する；独立画面 S0 の 4096 byte/10秒予約は維持し、RUN/SIZE_BARS の既定枠を各32768 byte・60秒・131072 poll とする（9600bps、送信余裕・実行時間を含む）。
- C8 は stage に const foundation 借用欄を追加して参照なし/失効を拒否する；所有者の設定と寿命の結線は CW、CK は共有レイアウトの入力台帳も追従させ生成器で契約 CRC を更新する（公開 KAPI 変更なし）。
