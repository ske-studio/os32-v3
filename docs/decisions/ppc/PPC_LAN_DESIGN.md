# PPC_LAN_DESIGN — PowerPC の LAN の最初の到達点 (GMAC で起動ログをホストへ、固定の診断コマンド)

> 状態: **現行 (2026-11-10)** — 内蔵 Ethernet (GMAC、QEMU の sungem と実機 QuickSilver で同じ core) を polling で動かし、到達点 ② の境界のまま起動ログを LAN でホストに保存し、固定の診断コマンド 5 個に答えるまでの層・境目・手順・段。段の状態は票 TASK_PPC_LAN が持つ。手本は BSD (ユーザー決定 2026-11-10)。メーカー資料 [M1]〜[M3] は未入手で、実機に載せる条件にはしない (末尾の「確定」)。

> 出所: ~/os32-tmp/run/ppc-lan/DESIGN.final.md (調査 gpt-6.1-sol、gpt-6-astra 起草 r1→r2、Opus 5.5 第 2 回で条件付き可、PM が N1〜N5 を反映)。QEMU の観測は末尾の「P0 の観測」。

## r1→r2 の変更
- F1: callback境界でslotを消費せず、末尾へbyte追記して送信時に閉じる。
- F2: ring prepare/publishとPHY/MAC enableを分離し、serviceの状態遷移を一本化。
- F3: 2MiB BARのうちGEM_MMIO_SPAN=64KiBだけをaliasへ写す。
- F4: ゲスト生成boot_idとbroadcast既定、bootargsは任意。
- F5: host nonceを要求に加え、host再起動で重複cacheを切り替える。
- F6: P0は採取とhost UDP経路まで、実frame往復は自前driver統合で受入。
- F7: --udpと--ifaceの二経路で共通codecを使用。
- F8: memory_boot_init後にDMA所有者がdescriptorをI/G化（範囲APIへ修正、末尾に反論）。
- F9: alias導入直後、allocator開始前にPCI Masterを解除・readback。
- F10: PCI machine-check復帰口と旧P6/P7を削除。
- F11: 製品段の直後へ個別host試験段を配置。
- F12: 型の所有header・wire status値・lan_messageを確定。
- F13: LAN無効印をLAN OFF code=<数値>に限定、既存禁止語を出さない。
- F14: 250us予算は同期kprintfを除外、印は状態遷移時だけ。
- F15: overflow後400msでFIFO進捗を検査し、停止時のみreset。
- F16: RANDOM_SEEDをBSDのbyte順の式へ修正。
- F17: RX補充の根拠をFreeBSDへ訂正、4枚ごとのKICK式を明記。
- F18: 正典PPC_KBOOT_DESIGNのalias表更新段を追加。
- F19: budget変異は1pollのRX消費が8枚超で検出。
- PM決定1: boot_idゲスト生成・peer broadcast既定を確定。
- PM決定2: managed RAM/像のみ、最大256Bのonfault付きPEEKを5番目の診断に追加。
- PM決定3: QEMU/試験はUDP、実機はAF_PACKET、codec共通を確定。
- P0観測: QEMU 8.2.2の既定sungem・106b:0021・2MiB BAR・UDP設定成功を採用。OF後tree/BAR・PHY・DMAは未確認のまま。
## 1. 実物の確認・適用範囲
- 基点 `b98087e8`（HEAD照合）。SURVEY.md を土台に、指定された製品・BSD の該当行を照合。r1の「食い違い0件」を撤回：callback粒度、RANDOM_SEED式、RX補充の引用元をF1/F16/F17で訂正。SURVEYの提案は既実装とは数えない。 `platform/powermac/log.c:15,23` は同期sink登録/直呼び、`init/ppc_kernel.c:132,137,234,240` は最初のKBOOT出力→2秒観測→idle。ここにenqueueとforeground pollを接続する。 `tools/lanfs_host.py:83` はLOGを破棄、`fs/serialfs/sfs_proto.h:16,167` は片道seq=0/10ms時計、`tools/ppc/kboot_run.py:38–46` はKBOOT行数を固定している。この3点を回避する接続を以下で確定する。 PPCKERN.ELFのみ。②の共通mm/MPIC/1kHzを維持、mount/module/exec/USERなし。GMAC coreはQEMU/QuickSilver共用、NIC IRQなし、再送なし。x86 LANの動作・公開ABIを変えない。 本作業は設計ファイル1本の作成だけ。preflight（修正を含む）・ビルド・試験・ゲスト起動は依頼に従い未実施。以下の受入は将来の実装条件。 引用略号：`O/`=/home/hight/os32-tmp/ref/openbsd/sys/dev/ic/、`OP/`=同openbsd/sys/dev/pci/、`N/`=同netbsd/sys/arch/macppc/pci/、`NG/`=同netbsd/sys/dev/ic/、`F/`=同freebsd/sys/dev/gem/。GPL・archiveは参照しない。 **[M1]** GEMレジスタ仕様、**[M2]** QuickSilver/UniNorthのclock・PCI/DMA仕様、**[M3]** 採取したPHYの仕様が未入手。印の付く値はBSDに沿う設計候補。**QEMUで確かめる段はBSDの値のまま実装して進めてよい**が、実機（QuickSilver）に載せる前に、印の付く書込みをメーカー資料と実機のtreeの採取で照合する（照合できない書込みは実機のprofileで無効のままにする。R1）。
## 2. 層・所有・像
|層|唯一の所有者/ファイル（新規名は本設計）|寿命・接続|
|---|---|---|
|OF採取/解釈|platform/powermac/gmac_tree.c、kboot_capture.c|値コピーのsnapshot。CI/OF pointerを保持しない|
|PCI設定|platform/powermac/pci_config.c|GMACに属するUniNorth root 1つ、指定BDFだけ。全PCI列挙なし|
|MMIO alias|arch/ppc/kernel_boot.c|初回rootの静的PTE、既存ESCC/MPICと同じ管理|
|DMA/cache|platform/powermac/gmac_dma.c / arch/ppc/dma_cache.c|静的pool、CPU/bus変換と同期を分離。停止不明なら永久隔離|
|NIC|drivers/net/gem/{gem_reset,gem_mac,gem_phy,gem_ring,gem_tx,gem_rx}.c|struct gem内の状態機械、ring indexと統計。OSのheap/IRQ/module依存なし|
|接続とpoll|platform/powermac/lan_service.c|struct gem 1個、唯一のRX drain、予算/時刻/状態遷移|
|ログqueue|platform/powermac/lan_log.c|登録済み静的sinkとFIFO。NICへ直送しない|
|線上codec/診断|platform/powermac/lan_wire.c / lan_monitor.c|wireはbyte codec、monitorは固定要求だけ。RXはserviceから借用|
|host|tools/ppc_lan_host.py|保存・表示・診断送受信を1プロセスで所有|
- 上記C/arch ASMはbuild/ppc-kboot.mkの明示TU一覧へ追加しPPCKERN.ELFに静的リンク。Pythonは像に入れない。net/nic.c、link、SFS transport、lan.modはリンクしない。 汎用GEM coreにはplatform依存を注入。将来NetNicOps（net/nic.h:11）の非待機口へadapter化できるが、この段では共通netの時計/ID/受信分配を変更しない。
## 3. 境目（名前・引数は実装契約）
```c
/* lan_boot.h所有: lan_rc/pm_* /lan_message/monitor status。gem.h所有: dma_sync/gem_dma/gem_io/struct gem。内部u8/u16/u32/u64。 */
struct gem; struct lan_message; struct ppc_boot_alias; enum lan_rc { LAN_OK=0, LAN_AGAIN=1, LAN_EINVAL=-1, LAN_ENODEV=-2,  LAN_ERANGE=-3, LAN_ETIME=-4, LAN_EIO=-5, LAN_EUNSUPPORTED=-6, LAN_EBUSY=-7 }; struct pm_pci_window { u32 ss, pci_base, cpu_base, bytes; };
struct pm_gmac_boot { u32 root_node, node, root_pa, bus_first, bus_last;  u32 bdf, bar_reg, bar_bus, bar_pa, bar_bytes, vendor_device;  u8 mac[6], peer_mac[6], boot_id[16]; u32 nwin; struct pm_pci_window win[8];
 u32 dma_cpu_base, dma_bus_base, dma_bytes, profile; }; int pm_gmac_capture(struct pm_gmac_boot *out); /* CI有効中だけ、採取もこの入口から */ struct pm_pci { u32 addr_va, data_va, bus_first, bus_last, target_bdf; };
int pm_pci_read32(struct pm_pci *, u32 bdf, u32 reg, u32 *value); int pm_pci_write32(struct pm_pci *, u32 bdf, u32 reg, u32 value); int pm_gmac_alias_plan(const struct pm_gmac_boot *, struct ppc_boot_alias out[4]);
struct gem_dma { void *cpu; u32 pa, bus, bytes; }; int pm_gmac_dma_init(const struct pm_gmac_boot *, struct gem_dma *out); int pm_dma_bus(u32 pa, u32 bytes, u32 *bus);
enum dma_sync { DMA_PREWRITE, DMA_PREREAD, DMA_POSTREAD, DMA_POSTWRITE }; int ppc_dma_sync(void *cpu, u32 bytes, enum dma_sync op); struct gem_io { void *ctx; u32 (*read32)(void *,u32); void (*write32)(void *,u32,u32);
 u64 (*tb)(void *); u32 tb_hz; int (*sync)(void *,u32,enum dma_sync); }; int gem_reset_step(struct gem *, u64 now); int gem_mac_init(struct gem *, const u8 mac[6]); int gem_mif_init(struct gem *); int gem_phy_step(struct gem *, u64 now);
int gem_ring_prepare(struct gem *, struct gem_dma *); int gem_ring_publish(struct gem *); int gem_mac_enable_step(struct gem *, u64 now); /* gem_mac.c、PHY成功後のみ */ int gem_tx(struct gem *, const void *frame, u32 bytes); /* 0=コピー受理、線上成功ではない */
int gem_rx(struct gem *, void *frame, u32 cap, u32 *bytes); /* 1=未到着 */ int gem_poll(struct gem *, u64 now, u32 tx_budget); /* 完了回収/状態遷移 */ int pm_lan_log_register(void); /* pm_log_add_sink(&static_sink)、開始前に1回 */
int pm_lan_start(const struct pm_gmac_boot *); void pm_lan_poll(u64 now); int pm_lan_encode(u8 *dst,u32 cap,const struct lan_message *m,u32 *used); int pm_lan_decode(const u8 *src,u32 bytes,struct lan_message *m);
struct lan_message { u8 boot_id[16], host_nonce[16]; u64 seq,drop_records,drop_bytes;  u32 request_id; u16 opcode,status,payload_bytes,flags; u8 type,payload[256]; }; int pm_lan_monitor(const struct lan_message *req,struct lan_message *rsp);
int ppc_probe_copy8(u32 va, u8 *out); /* kernel_probe.S: load成功0/onfault復帰1 */ int ppc_trap_onfault_data(u32 pc,u32 va,u32 resume); /* ppc_trap.h: 診断専用DSI mask、成功1/既arm時0 */
```
### (a) OF・takeover
- kboot_captureのtree walk（:153–192）の後、pm_gmac_captureでGMAC候補/祖先を追加採取・解釈する。raw propertyは既存512B/祖先8/探索1024の上限を継承。`reg / assigned-addresses / compatible / local-mac-address`とrootの`reg / bus-range / ranges / #*-cells`を値保存。候補0/複数はENODEV/EUNSUPPORTED。 OF-PCI §2.2.1.1/4.1.2/10/12（docs/ppc/open-firmware.md:81–100）通り3+2セルを検証、BAR0=reg 0x10で選択、ss=memory、各親ranges内に窓全体が収まることを確認。n=0だけを理由に拒否しない（qemu-mac99.md実測）。64bit上位非zero、桁あふれ、窓重複/8窓超過はERANGE。 UniNorth旧型profileだけ、U3/U4は非対応。MACは6B・非zero・unicast。IDはOF記録とPCI実読を照合。106b:0021は候補（F/if_gem_pci.c:80）、他IDはP0の証拠でprofileを追加するまで非対応。 boot_idはcapture時のTB64・MAC48・PVRの下位16bitをBE連結した16Bをゲストで生成（暗号的乱数/一意性保証ではない）。`os32-lan-boot=`32桁hexと`os32-lan-peer=`は任意上書き、peer既定broadcast。ホストはBEACON/seq巻戻りで保存世代を分け、同一IDの再起動疑いを表示する。初回BEACONはnext_seq=0、ログ送信前に1回試行、失敗でも待たない。再起動時に全初期frameを失った場合の識別不能は残る。 capture→既存reserve/最終available再取得→drain→ofw_detach（kernel_boot.c:163–167）→takeover。LAN追加領域をOF中に書かない。detach後は保存済み値だけ、OF呼出増分0を維持。
### (b) PCI設定口 [M2]
- N/uninorth.c:165–168,191–271を採用：root reg+0x800000のaddress、+0xc00000のdata（8B）。bus=bus_firstならdev>=11かつ`1U<<dev | fn<<8 | reg`、下位busなら`bus<<16|dev<<11|fn<<8|reg|1`。regは4整列/<256、data offsetはreg&4。 clockが必要なprofileではalias導入後にpm_gmac_clockを先行（②のallocator開始前）。IRQ保存/禁止→LE address書込み/readback→data読書き→address=0/readback→IRQ復元。root範囲/target_bdf以外はEINVAL。32bitでcommand/statusへ書く場合はstatus上位16bit=0（W1Cを再書込みしない）、command低位を操作しreadback照合。ppc_kernel_mainがmemory_boot_initの直前で`pm_gmac_early_quiesce()`（platform/powermac/pci_config.c、段P8の中、呼出しの追加はP25）を1回呼ぶ：OF指定BDFのIDを照合→Masterをclearしてreadback。**継続の条件は「Master=0をreadbackで確かめた」だけ**（N1）。GMACがtreeに無い・profileが分からない・IDが違う場合はPCIに触らず`LAN OFF code=<数値>`で続ける。止めるのは「Master解除を書いたのにreadbackが1」のときだけ（既存の停止経路）。clockが必要なprofileでは同じ関数の中でpm_gmac_clockを先に呼ぶ（P9のkernel_boot.cはclockを呼ばない、N2）。Master再設定はreset完了・DMA同期・ring base設定後、DMA enableの直前だけ。BAR全1書込み/再割当なし。
- OFが示す対象BDFだけを読む。全1/ID不一致はENODEV、走査・machine-check復帰は追加しない。②を続ける条件は上の1文のとおり（GMACが無い・分からない・IDが違うときはMasterに触らず続ける。Master解除を書いて読み戻しが0なら続ける。読み戻しが1のときだけ止める。R2）。
### (c) MMIO・alias [M1,M2]
|VA予約|長さ上限|対象・属性|
|---|---:|---|
|FE300000 / FE340000|各256KiB|既存ESCC / MPICを維持|
|FE380000|64KiB（予約slot 256KiB）|GMAC BAR0先頭GEM_MMIO_SPAN=0x10000だけ（bar_bytes>=span）。bank2=BAR+0x1000、RESET=bank2+0x10（OP/if_gem_pci.c:228–239、O/gemreg.h:56–65）|
|FE3C0000 / FE3C1000|各4KiB|PCI config address / data|
|FE3C2000|4KiB|UniNorth clock。物理ページ/bitは機種資料とtree一致時のみ|
- BAR全長2MiBは窓検証用に保存しaliasへ渡さない。O/gemreg.h:433–443の最大register 0x905c+4とbank2を64KiBが包含。
- すべてsupervisor RW、PCD→I/G。aperture先頭4MiBの既存PT内。offset保持・丸め後長さ・PA/VA加算overflow・ESCC/MPIC/gfx予約/相互非重複を検査。RAM/像/HPTとMMIO PAが重なる個体は拒否。恒等WB aliasは残さない。
- kernel_boot.c:32–42,56–60,85–112のplan/install/検証を表駆動に拡張、IR/DR開始前にwalk。IRQからGEMを触らず、fatal MMIOのDR=1規則は維持。kernel_hpt.c/ppc_pmap.hは変更不要。
### (d) DMA [M1,M2]
- `gmac_dma.c`に192KiB/64KiB整列の静的BSS pool。既存image_first..image_end予約に含め、image_endが既存固定領域/arenaに届かないことをlink mapとreserveで検査。収まらなければ配置再設計をPMへ返し、15–16MiBの除外域へ逃がさない。
- pool+0x00000：descriptor用64KiB、TX32×16Bを+0、RX32×16Bを+0x1000（両base 4KiB整列）。+0x10000：TX32×2048B、+0x20000：RX32×2048B。個々のbuffer/ringは64KiBをまたがない。RXの同時公開は28枚、残り4枚を常に空ける。
- CPU PAはP2V/V2P集約口で得る。bus=`dma_bus_base+(pa-dma_cpu_base)`、全長がprofileのDMA窓内/32bit内であることを検査。OF rangesはMMIO用でありDMAへ転用しない。PA=busはDMA資料で許可したprofileだけで暫定使用し、自前driverの往復で受入する（P0はDMA未確認）。
- descriptor区画64KiBはI/Gの恒等mapのみ。gmac_dma.cのpm_gmac_dma_initがmemory_boot_init後、Master=0/reset完了後、ring準備前に全dirty lineをdcbf→sync→`paging_map_range(base,base+0x10000,base,PAGE_RW|PTE_PCD)`→戻り値/全page walkを検査する。root導入時のimage属性検査（kernel_boot.c:87）には触れない。mm/paging.h:99–105の範囲APIでHPT/TLBを最後にflush、arch/ppc/pd_walk.c:48のPCD→I|Gを試験。buffer区画はM属性、2048B単位でcache lineを専有。
- 同期口：TX PREWRITE=各32B dcbst→sync、RX PREREAD=dcbf→sync、POSTREAD=dcbi→sync、POSTWRITE=sync。I/G descriptorはsyncだけ。CPUはNIC所有中にbufferへ触らず、invalidateで他のdirtyデータを失わない。32B幅/PVRはCPU資料で照合してprofile化。BSDのcoherent control/bus_dmamap_sync（O/gem.c:146–175,977–988,1082）を実現するarch providerであり、BSDにこのOS32固定pool配置は無い。
### (e,j) core・時計
- GEM registerはLE32 accessor（PPC lwbrx/stwbrx相当＋eieio、doorbell前sync）、descriptorはLE64をbyte/32bit2語で読み書き。64bit MMIOは禁止。状態所有者はservice、gemは手順内の進捗を持つ。gem_pollはgem_reset.cで有界な状態監視を統括、TX回収はgem_tx.c、RX watchdogはgem_rx.c。prepare/publishも内部cursorでstep化し、完了済みsubstepを再実行しない。異常はOFF/QUARANTINED（印は数値）。
- TBはTBU/TBL/TBU一致読みによるu64、tb_hzはOF取得値。期限は`ceil(us*tb_hz/1000000)`をu64で計算、now-start差で判定。tickは1kHzの観測のみ、SFS/linkの100Hz時計を渡さない。
- reset/PHY待ちはstep関数のLAN_AGAINで返す。各stepの内部ループは有限、TB期限に加えてpoll回数上限を置く。TB停止も無期限待ちにしない。tick IRQでNICへ入らない。
|service状態→呼ぶ順（成功で次へ、AGAINなら同状態、負値は停止/隔離）|次状態|
|---|---|
|pm_lan_start: 保存snapshot/早期Master解除済みとclockを検査、reset開始のみ|RESETTING|
|poll: gem_reset_step→gem_mif_init（完了時1回）|PREPARE|
|poll: （初回だけpm_gmac_dma_init、以後は呼ばない。N3）→gem_ring_prepare→gem_mac_init（filter含む）|PUBLISH|
|poll: gem_ring_publish（base→Master再設定→DMA enable、MACは停止）|PHY_WAIT|
|poll: gem_phy_step（PHY交渉だけ）→成功後gem_mac_enable_step（10ms待ちは次poll）|RUNNING|
|poll: gem_poll→RX→monitor→RSP→LOG→BEACON、link喪失/停止はreset列へ|RUNNING/RESETTING|
### (f) ログsink・poll
- pm_log_escc_init成功後、最初のkprintf（init/ppc_kernel.c:137）より前にpm_lan_log_register。log.cの既存pm_log_add_sinkを使い、log.c自体は変更不要。takeover前のOF直出力は対象外、最初のKBOOT OF-CAPTUREから保存対象。
- FIFOは64slot×256B。callbackのbyteを開いた末尾slotに追記し、満256Bまたは送信時に閉じてu64 seqを振る。空callbackはslotを使わない。満杯では新着だけ捨て、連続するdrop区間を256Bごとの仮想chunkとして閉じseq/drop_recordsを進める（回復時の端数も1件）。drop_bytesは実捨てbyte数、両者u64飽和＋flag。再入dropも同規則、1callbackのコピーは容量16KiB以内。READY前全KBOOTと通常PHY待ちの印が16KiB内で保存されるfixtureをA8で確認。
- sinkは短いIRQ禁止区間でFIFOを操作、再入guardが立っていれば全量drop。kprintf・MMIO・heap・待ち・pollを呼ばない。pollはslotを静的送信scratchへコピーしてIRQ復元後にgem_tx、AGAINならheadを維持。受理後はqueueから除き再送しない。
- READY直前にpm_lan_start、2秒ALIVE観測ループ内と最後の`for (;;) pm_lan_poll(tb_now())`で実行。TBで最短1ms間隔を目標に、1回TX完了8/RX8/LOG4/応答1/状態step1、TB使用上限250usはkprintfを除く処理の予算。状態遷移時だけ印をpollの処理後に出す（同期ESCCでmsを要し得るので厳密な1ms周期は保証しない）。単一MMIOのハード停止は別リスク。無期限halt/napなし、EEは通常1。
- link downは送信開始せずqueueを保留、満杯で新着drop。link再開で残ったqueueを送る。進行中TXが5秒停止ならresetへ、未完了ログをuncertain_txに計上して再送しない。停止確認不能ならpool隔離、LAN無効でもKBOOT/tick継続。
## 4. 線上形式・診断・host
- **新規最小形式PLN1、EtherType=0x88B6（実験用L2）を採用**。SFS HELLO/LOGは採らない：seq=0・session/CRC/10ms依存とhost LOG破棄を変更せず、boot/seq/dropを明示できる。IP/ARP/TCP/ファイル機能なし。既存0x88B5と分離する。
|offset（Ethernet payload起点）|field（数値はBE、packed struct cast禁止）|
|---|---|
|0..7|magic="PLN1"、version:u8=1、type:u8（1 BEACON/2 LOG/3 REQ/4 RSP）、header_bytes:u16=80|
|8..23|boot_id:16B（生成/衝突時の保存世代は§3(a)）|
|24..47|seq:u64、drop_records:u64、drop_bytes:u64|
|48..63|request_id:u32、opcode:u16、status:u16、payload_bytes:u16、flags:u16、reserved:u32=0|
|64..79|host_nonce:16B（REQ/RSPでhost session識別、BEACON/LOGは全0）|
- payload≤256B、EthernetはFCSなし94..350B（14+80+256）、余剰は最小frame paddingだけ許可。LOGはraw byte列、文字途中の分割を許す。seqはログchunkの番号（0開始）、BEACONはnext_seq、RSPはreqのseq/host_nonce/request_idを反射。BEACONは1秒ごとにboot/次seq/drop/uncertain_txとlink統計を通知（再送ではなく現在値）。
- LOG/BEACON宛先は既定broadcast（任意peer指定時は固定unicast）。REQは自MAC宛・unicast送信元・boot一致、peer指定時は送信元peer一致を要求。broadcast既定では単一hostが診断を使う前提、RSPは要求送信元へunicast。peerを学習してログ宛先を書き換えない。認証ではなく同一L2の診断用。
- hostはboot_id別にraw log＋JSONL metadataを保存し表示。最初のseq>0/seq間の穴/重複/順序逆転を表示し、ゲストdropと線上の穴を加算して二重計上しない。最後のframe以後の損失は次のBEACONまで不明と表示。終了時にも完全配送と称さない。
|診断opcode（5個）|要求payload→応答payload（statusの後の本体、全て≤256B）|
|---|---|
|1 STATUS|0B→state/link/PHY ID/TX-RX counters/uncertain_tx、固定u32配列|
|2 CLOCK|0B→tick:u32,tb_hz:u32,tb:u64,us:u64。短いIRQ禁止中のsnapshot|
|3 MEMORY|0B→admitted/image/HPT/DMA予約のbase,sizeを固定u32配列で返す（任意アドレスは受けない）|
|4 READ_DIAG|offset:u16,len:u16（1..128）→診断専用256B snapshot内のbytes。範囲は減算で検査、MMIO/stack/任意RAM参照なし|
|5 PEEK|va:u32,len:u16,reserved:u16=0（1..256）→読み出したbytes。範囲全体がmanaged RAM実区間またはimage予約内、overflow/穴/MMIOを拒否|
- PEEKはRAM/像allowlistを減算検査しPTEのPAも照合、I/G MMIOを拒否（既知descriptor RAMはRAMとして許可）。kernel_probe.Sへ値を返すbyte loadを追加し、既存onfaultと同じPC/VA限定・一回消費でDSIを拾う。kernel_trap.cの診断専用arm口はDSI MISS/PROTECT/IOだけのmaskを許し、既存exact reason口は維持、machine check/未知例外は対象外。1byteごと短くIRQ保存→arm→load→clear→復元、失敗はFAULT・payload0（部分値を返さない）。snapshot原子性は保証しない。
- wire status:u16はOK=0、UNSUPPORTED=1、BAD_LENGTH=2、RANGE=3、CONFLICT=4、STALE=5、BUSY=6、FAULT=7。Cの負lan_rcと混同しない。診断snapshotはserviceが固定項目から作る。REQ≤8B、opcodeごとの厳密長。未知opcodeはstatus=UNSUPPORTED、正しいheaderで長さ不正ならBAD_LENGTH、範囲不正ならRANGE。壊れたheader/別peer/別bootは無応答drop。
- host_nonceはhost起動時の乱数128bit。cache keyは送信元MAC+nonce+request_id、nonce変更時はcache/最後IDを捨てる（応答枠が空の時だけ切替）。古いnonceの遅延要求は新session扱いになり得るが読取りだけであり、hostは現nonceと一致するRSPだけ採用。request_idはhost session内のu32単調増加（周回前にhost sessionを終了）、最後の16件のrequest全体とresponseをcache。同一ID同一内容は同じ応答、同一ID異内容はCONFLICT、cacheより古いIDはSTALE。副作用なし。応答枠1個、満杯時は要求dropを計数、hostは500msでtimeout表示し自動再送しない。
- host新規`tools/ppc_lan_host.py (--udp 127.0.0.1:18026,127.0.0.1:18027 | --iface eth0) --peer <guest-mac> --out <dir>`。UDPはlocal bind/remote送信・送信元endpoint照合、1datagram=1Ethernet frame。AF_PACKETは0x88B6限定・PACKET_OUTGOING除外。共通codecで長さ/boot/nonce検査、BEACONからboot取得（--boot-idは任意制約）。stdinでstatus/clock/memory/read-diag/peek、selectで保存/要求を処理。
- lan_bridge.py/lanfs_host.pyは無変更・不使用。UDPとAF_PACKETのI/Oだけを分け、PLN1 codec/保存/診断を共有、FrameStream互換層は足さない。
## 5. GEMの書込み順序とendian（§3のservice順、PHY設定候補行は交渉の内訳）
|状態/OS32関数|BSDの関数・file:line / OS32の列|期限・失敗|
|---|---|---|
|clock / pm_gmac_clock|N/uninorth.c:135–142 uninorth_attachは子gmac確認後0xf8000020 bit1を設定。OS32は[M2]で機種/PA/bitを確定→alias越しRMW/readback→PCI読出し。未知機種で固定番地へ書かない|profile条件不足ならEUNSUPPORTED|
|停止 / gem_reset_step|O/gem.c:486–501 gem_reset→583–607 gem_reset_rx→647–659 gem_disable_rx。MAC_RX_CONFIG.ENABLEを落とし待つ→RX_CONFIG=0待つ→5ms→bank2 RESET.RX待つ|各bit待ち1秒（gem_bitwait :470–482、10000×100us）、失敗即隔離|
|TX停止/全reset / 同上|O/gem.c:615–640,666–678 gem_reset_tx/disable_tx。同じ列でMAC_TX_CONFIG→TX_CONFIG→5ms→RESET.TX、最後RESET.RX\|TX→self-clear確認。GEM_INTMASK=全1を先行/完了後再設定[M1]|各1秒、失敗後のDMA領域再利用なし|
|MIF / gem_mif_init|O/gem.c:1260–1268 gem_mifinitのBB_ENA解除→:249–293 gem_configのMDI1/PHY_SEL探索→Apple側MDI0候補。MIF_FRAME read/writeは:1286–1344、TA0完了判定|各transaction100us、32addr×2portを有限走査。0/ffff ID除外|
|host ring準備 / gem_ring_prepare|O/gem.c:685–709 gem_meminit、O/gemvar.h:260–271。TX zero/sync、RX buffer PREREAD→addr→capacity/OWN→descriptor sync。MAC設定より先に実施|32枚有限、公開前失敗はEIO|
|MAC基本 / gem_mac_init|O/gem.c:865–910 gem_init_regs：IPG0/1/2=0/8/4、MIN=64/MAX=1522\|(0x2000<<16)、PREAMBLE=7/JAM=4/ATTEMPT=16/CONTROL_TYPE=0x8088、RANDOM_SEED=((mac[5]<<8)|mac[4])&0x3ff、ADDR3..5=0、ADDR6..8=1/c200/180、FILTER0..2/MASK1_2/MASK0=0 [M1]|DMA/MAC停止中、有限書込み、失敗LAN無効|
|MAC残り / 同上|O/gem.c:913–952 gem_init_regs：NORM/FIRST/EXCESS/LATE_COLL、DEFER_TMR/PEAK_ATTEMPTS、RX_FRAME/LEN_ERR/ALIGN_ERR/CRC_ERR/CODE_VIOLを0→SEND_PAUSE_CMD=1bf0→CONFIG=TXDMA_LIMIT\|RXDMA_LIMIT\|PCI BURST_INF（AppleのみRONPAULBIT/BUG2FIX）→ADDR0=mac[4:5],1=[2:3],2=[0:1] [M1]|variant照合必須、未知なら書かない|
|filter / 同上|O/gem.c:1579–1639 gem_iffの空multicast分岐：RX_CONFIGのPROMISC/PROMISC_GRP/HASHをclear→HASH enable→HASH0..15=0→RX_CONFIG。自MAC+broadcastだけ|有限、softwareでもdstを検査|
|ring公開 / gem_ring_publish|O/gem.c:787–834 gem_init：TX/RX_RING_PTR_HI→LO（HI=0）→INTMASK→MAC_RX_MASK=DONE\|FRAME_CNT/TX_MASK=ffff/CONTROL_MASK=0→TX_CONFIG=size32\|((04ff<<10)&FIFO_TH)\|EN→TX_KICK=0→RX_CONFIG=size32\|THRSH1024\|FBOFF2\|EN、CXM_START0→PAUSE_THRESH=FIFO由来3/4,1/4→BLANKING=(6<<12)\|6 [M1]|base/sync成功→PCI Master再設定/readback後のみenable。INTMASKはBSDの選択解除と異なり全1、packet pollingのため|
|PHY交渉 / gem_phy_step|O/gem.c:837 gem_initはmii_mediachgへ委譲。**指定GEMファイルにはBMCR/ANAR/PHY固有resetの実体は無い**。P0で[M3]と対応BSD MII関数/行を追加採取することをこの行の実装ゲートにする|reset最大500ms、交渉5秒、その後DOWNで1秒間隔監視。未知PHYはEUNSUPPORTED|
|PHY設定候補 [M3]|profile確認後BMCR.RESET待ち→ANAR=selector+100TX fullのみ（pauseなし）→1000T control広告0→BMCR.AN_ENABLE\|AN_RESTART→BMSR2回読みによるlink/AN完了→ANLPARと一致確認。初版100Mbps fullで固定、別速度/halfはDOWN|gigabit/PHY固有page書込みは採らない。これらのPHYレジスタ列は上記資料が揃うまで確定しない|
|MAC有効 / gem_mac_enable_step|O/gem.c:1348–1404 gem_mii_statchg：MAC_TX_CONFIG=0→10ms→ENA_IPG0\|NGU\|NGU_LIMIT\|ENABLE\|IGN_CARRIER\|IGN_COLLIS→XIF_CONFIG=TX_MII_ENA\|LINK_LED\|FDPLX_LED（100MでGMII clear）→CONTROL_CONFIGのRX/TX_PAUSE clear。:839–851 gem_init：RX_CONFIGへMAC_RX_ENABLE\|STRIP_CRC→RX_KICK=28|10msはstepで待つ。link変化は送信停止→停止列→再準備|
|送信 / gem_tx|O/gem.c:1710–1802 gem_start：空き確認→buffer PREWRITE→addr/len\|SOP\|EOP→descriptor同期→TX_KICK=次index。1frame=1descriptor、31枚まで。NO_CRC/INTME/checksumなし、60Bへzero pad|AGAINは非待機。完了5秒停止でreset、線上成功を推測しない|
|完了 / gem_poll|O/gem.c:1646–1668 gem_tintはstatus>>19からconsumer更新/POSTWRITE。OS32はGEM_TX_COMPLETION（O/gemreg.h:149,174）を直接読み、未完了数を越える進行をEIO|IRQ status依存を外すための差。最大8枚回収|
|受信 / gem_rx|O/gem.c:959–1034 gem_rint：descriptor POSTREAD/WRITE→OWN=0確認→buffer POSTREAD→BAD_CRC/len検査→offset2からcopy→:1040–1087 fill/add_rxbufで再公開→RX_KICK。O/gemvar.h:260–271のOWN公開順を維持|14..1514Bのみ、badはdrop。再arm後rxptrを進め、rxptr%4==0のとき同期→RX_KICK=(rxptr+32-4)&31（F/if_gem.c:1581–1589）。28枚上限、最大8枚。OWN中はdata不読|
- BSDとの差：resetエラー時BSDはprintf後に進む箇所もあるがOS32は即隔離。固定pool/1descriptor/32枚/4枚補充/100M限定/状態機械は②の有界化による変更。[M1]でring最小/align/4枚fetch/offset/CRC長さを照合する。
- TX公開同期はbuffer PREWRITE→descriptor PREWRITE→sync→KICKとする。O/gem.c:1788–1790の末尾POSTWRITE表記をそのまま移さず、NG/gem.c:1589のPREREAD|PREWRITEと公開前可視化契約に沿う。
- gem_pollはSTATUS_ALIAS（O/gemreg.h:49）を読み、BERR/TAG_ERR/TX_MAC/RX_MACを検出したら対応status（O/gem.c:1151–1187）を採取。TX underrun/長過大は送信停止→reset。RX overflowはFIFO WR/RDを保存しTB400ms後にMAC_STATEを確認、overflow継続かつWR=RDまたは両pointer不変だけreset、進捗ありなら保存値更新→400ms再検査、overflow解消なら解除（O/gem.c:1172–1235 gem_eint/gem_rx_watchdog）。監視中の追加overflowで期限を先延ばししない。IRQ ackの代用に無目的なstatus readをしない。
- MIF_CONFIG.POLL_ENAもclearする（O/gemreg.h:411）。gem_mifinit自身はBB_ENAだけclearするため追加差分：FWが残したPHY自動監視を止め、frame transactionを専有する。根拠は同レジスタ定義、挙動は[M1]待ち。
|境界|byte順とaccess|
|---|---|
|OF cells / CPU private構造体|OF BE32 decode / PPC native BE、raw pointer借用なし|
|PCI config・GEM registers|LE32、clock controlだけ機種仕様[M2]の順序（Nのnative accessをPCI LEと混同しない）|
|DMA descriptor|LE64 flags+addr各8B、O/gemreg.h:530、O/gemvar.h:213–216、NG/gemvar.h:242–245、F/if_gemvar.h:225–226|
|Ethernet / PLN1|EtherType BE16、PLN1数値BE、MAC/raw logはbyte列|
## 6. サイズ・ファイル単位の段（各0.5〜1日、1ファイル=1段=1担当）
- 概算増分：text 16–28KiB、rodata/data 2–4KiB、BSS通常28–44KiB＋DMA192KiB＋整列padding最大64KiB。合計RAM増分222–304KiB程度。測定前の見積りでありlink mapを受入証拠にする。
- 16MiB managed枠内のimage予約にpool/queueを置き、allocatorの空きから二重払い出ししない。HPTは従来通り16–32MiB内/管理外、15–16MiB除外は維持。image境界の検証を緩めない。
- 全段I（x86生成物不変目標）。以下の表は変更台帳と段表を兼ねる。＋新規、Δ既存。定数/宣言も独立段、実装者が他の段のファイルを同時編集しない。依存のない行を並列配布できる。
- 各行のQ欄は追加meter `LAN <項目> ...`（KBOOTの固定9行へ混ぜない）。H欄は§7の試験ID、R欄は実機初確認。宣言/選択段の単独像には印を偽造せず、指定後続段の同じ観測を統合時に要求する。
|段|ファイル・行数目安|前提|Q観測 / H / R|
|---|---|---|---|
|P0|＋tools/ppc/lan_probe.py 100|なし|採取/UDP経路 / A0 / tree・資料|
|H0|＋tools/ppc/test_lan_p0.py 60–160|P0|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P1|＋platform/powermac/lan_boot.h 140|P0|P3 TREE / A1,A9,A10 / cells|
|H1|＋tools/ppc/test_lan_p1.py 60–160|P1|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P2|＋drivers/net/gem/gem.h 140|P0|P15 RING / A5 / descriptor|
|H2|＋tools/ppc/test_lan_p2.py 60–160|P2|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P3|＋platform/powermac/gmac_tree.c 250|P1|TREE bdf/bar/mac / A1 / root|
|H3|＋tools/ppc/test_lan_p3.py 60–160|P3|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P4|Δplatform/powermac/kboot_info.h 8|P1|P5 CAPTURE / A1 / snapshot|
|H4|＋tools/ppc/test_lan_p4.py 60–160|P4|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P5|Δplatform/powermac/kboot_capture.c 40|P3,P4|CAPTURE/OF増分0 / A1 / OF版|
|H5|＋tools/ppc/test_lan_p5.py 60–160|P5|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P8|＋platform/powermac/pci_config.c 150|P1|PCI id/master=0 / A2 / UniNorth停止|
|H8|＋tools/ppc/test_lan_p8.py 60–160|P8|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P10|Δarch/ppc/kernel_boot.h 25|P1|P9 ALIAS / A3 / 非重複|
|H10|＋tools/ppc/test_lan_p10.py 60–160|P10|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P9|Δarch/ppc/kernel_boot.c 110|P5,P8,P10|ALIAS span=65536 / A2,A3 / clock窓|
|H9|＋tools/ppc/test_lan_p9.py 60–160|P9|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P11|＋arch/ppc/dma_cache.c 100|P0|P12 DMA / A4 / cache命令|
|H11|＋tools/ppc/test_lan_p11.py 60–160|P11|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P12|＋platform/powermac/gmac_dma.c 160|P1,P11|DMA/I-G walk / A4 / bus到達性|
|H12|＋tools/ppc/test_lan_p12.py 60–160|P12|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P13|＋platform/powermac/gmac_clock.c 55|P0,P8|CLOCK readback / A2 / clock gate|
|H13|＋tools/ppc/test_lan_p13.py 60–160|P13|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P14|＋drivers/net/gem/gem_reset.c 150|P2,P13|RESET done / A5 / reset時間|
|H14|＋tools/ppc/test_lan_p14.py 60–160|P14|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P15|＋drivers/net/gem/gem_ring.c 180|P2,P12,P14|RING tx=32 rx=28 / A5 / alignment|
|H15|＋tools/ppc/test_lan_p15.py 60–160|P15|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P16|＋drivers/net/gem/gem_mac.c 170|P2,P14|MAC/filter/enable / A5 / revision|
|H16|＋tools/ppc/test_lan_p16.py 60–160|P16|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P17|＋drivers/net/gem/gem_phy.c 230|P0,P2,P14|PHY id/100/full / A6 / PHY固有処置|
|H17|＋tools/ppc/test_lan_p17.py 60–160|P17|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P18|＋drivers/net/gem/gem_tx.c 110|P15|TX complete / A7 / wire TX|
|H18|＋tools/ppc/test_lan_p18.py 60–160|P18|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P19|＋drivers/net/gem/gem_rx.c 170|P15|RX wrap/progress / A7 / RX/cache|
|H19|＋tools/ppc/test_lan_p19.py 60–160|P19|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P20|＋platform/powermac/lan_log.c 170|P1|P24 LOG / A8 / 初期量|
|H20|＋tools/ppc/test_lan_p20.py 60–160|P20|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P21|＋platform/powermac/lan_wire.c 150|P1|P24 WIRE / A9 / MTU|
|H21|＋tools/ppc/test_lan_p21.py 60–160|P21|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P22a|Δarch/ppc/kernel_probe.h 5|P1|P22 PEEK / A10 / load値|
|H22a|＋tools/ppc/test_lan_p22a.py 60–160|P22a|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P22b|Δarch/ppc/kernel_probe.S 20|P22a|P22 PEEK / A10 / byte load|
|H22b|＋tools/ppc/test_lan_p22b.py 60–160|P22b|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P22c|Δarch/ppc/ppc_trap.h 5|P22a|P22 PEEK / A10 / DSI mask|
|H22c|＋tools/ppc/test_lan_p22c.py 60–160|P22c|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P22d|Δarch/ppc/kernel_trap.c 20|P22c|P22 FAULT応答 / A10 / 同期DSI|
|H22d|＋tools/ppc/test_lan_p22d.py 60–160|P22d|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P22|＋platform/powermac/lan_monitor.c 210|P21,P22b,P22d|MONITOR 5 / A10 / RAM snapshot|
|H22|＋tools/ppc/test_lan_p22.py 60–160|P22|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P23|＋tools/ppc_lan_host.py 320|P21|HOST saved/gaps/reply / A9,A10 / host NIC|
|H23|＋tools/ppc/test_lan_p23.py 60–160|P23|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P24|＋platform/powermac/lan_service.c 240|P8,P12–P23|L2往復/LOG/LOSS / A8–A11 / 再接続|
|H24|＋tools/ppc/test_lan_p24.py 60–160|P24|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P25|Δinit/ppc_kernel.c 30|P24|ALIVE/LAN BUDGET / A11 / tick継続|
|H25|＋tools/ppc/test_lan_p25.py 60–160|P25|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P26|Δbuild/ppc-kboot.mk 30|P10,P25|IMAGE symbols/size / A12 / 配置|
|H26|＋tools/ppc/test_lan_p26.py 60–160|P26|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P29|Δtools/ppc/kboot_run.py 140|P23,P26|LAN印＋既存9印 / A11,A12 / ―|
|H29|＋tools/ppc/test_lan_p29.py 60–160|P29|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P30|Δtools/ppc/kboot_mutations.py 110|P29|期待診断/到達印 / A1–A12 / ―|
|H30|＋tools/ppc/test_lan_p30.py 60–160|P30|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P31|Δtools/ppc/kboot_accept.py 60|P30|7450/7455＋保存一致 / A12 / ―|
|H31|＋tools/ppc/test_lan_p31.py 60–160|P31|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P32|Δtools/ppc/kboot_all.py 12|P31|既存段とLAN統合 / A12 / ―|
|H32|＋tools/ppc/test_lan_p32.py 60–160|P32|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
|P33|Δdocs/decisions/ppc/PPC_KBOOT_DESIGN.md 15|P9,P12|alias表/早期Master解除/後段I-G / A3,A4 / ―|
|H33|＋tools/ppc/test_lan_p33.py 60–160|P33|直前段のH項目を製品本体で試験、CHECK/変異を同ファイルに所有|
- 表の前提P番号はその直後のH段完了までを含む。P6/P7（machine check）・P27/P28（一括host試験）は削除。H段はPython内fixtureから製品TUを直接組み、header/build/docsは利用側TU/製品設定との整合を検査する。Q印はP26以後の統合像で受入、単体で偽造しない。
- 並列組：P0後P1/P2/P11、P1後P3/P4/P8/P10/P20/P21、P14後P15/P16/P17、P15後P18/P19、P21後P23とPEEK系列。各組も表の追加前提と資料ゲートを満たすこと。1段1ファイル、同じファイルの並列編集なし。P24は番号範囲のうち削除段を除く全前提を待つ。
- X（x86生成物が変わる段）は今回0。将来C1=共通NIC/時計、C2=Host Services、E3後IRQ、③後shell/hsyncは別票。Iは最終統合前後のSHA付きbuild_compare一致で確定、不一致ならXへ分類して本線PMのNP21/W受入が必須。
## 7. 受入→試験→変異（実装後、製品本体を組む）
- C試験は製品.cそのものをリンク、差替えはMMIO/CI/cache ASM/時計の観測口だけ。host32はfreestanding static＋qemu runner。各anchorは製品中で一意、変異実行で表の`H:Ax`診断＋rc=1、到達印`reached=Ax`の両方を要求。別診断/timeout/compile失敗は試験失敗でREDにしない。
|ID/受入|試験（QEMUはLAN印とhostの独立観測を両方検証）|製品の変異→期待診断|
|---|---|---|
|A0 P0証拠の区別|tree/ID/BAR/PHYの採取結果と未取得理由、hostのUDP loop fixture。guest frame往復はA7|probeの未採取を採取済みにする変異→H:A0 evidence-missing|
|A1 snapshot/OF-OFF|cells長さ/overflow/2root/BAR順入替fixture、detach後call監視|BAR選択条件削除→H:A1 wrong-bar|
|A2 PCI/clock|config bus模型でonehot/type1/+4/LE/address clear/W1C、Master解除がallocatorより先、再設定はring公開時だけ|data+4削除→H:A2 config-lane|
|A3 alias|製品plan/installを実表でwalk、PA offset/境界/別alias/DR=1観測|衝突拒否削除→H:A3 alias-overlap|
|A4 DMA|PA→bus非identity fixture、alignment/64KiB/予約境界、memory_boot_init後のI/G属性変更と同期順を観測|PREWRITE sync除去→H:A4 dma-unpublished|
|A5 reset/MAC/ring|停止前resetを拒否する状態ful bus模型、5ms/1秒、bank2、全レジスタ書込み列/4枚余白を照合|RX停止待ち省略→H:A5 reset-active|
|A6 PHY/deadline|TA0無し/未知PHY/link down/100full交渉fixture、TB/回数期限でAGAIN→TIME|TA0条件反転→H:A6 phy-invalid|
|A7 TX/RX|LE golden bytes、33枚送信/受信、128枚wrap、badCRC/長さ/OWN、自前driverとhost UDPのnonce frame往復一致、overflowの400ms進捗/停止両fixture|RX OWN分岐反転→H:A7 rx-owned|
|A8 log/drop|READY前全KBOOT/PHY待ち印を実callback粒度で流し全byte保存、16KiB+256B/reentry/断線→64保持+1drop、seq/byte数照合|満杯時drop加算削除→H:A8 loss-count|
|A9 wire/host保存|Python製品moduleをimport、分割/短frame/不正version/boot/peer/duplicate、raw保存byte一致|hostのboot照合削除→H:A9 foreign-boot|
|A10 monitor|5コマンド・host再起動nonce/PEEK上端256B/overflow/MMIO/DSI回復/長さ/未知opcode/同ID異内容/17件超過/応答満杯|PEEK範囲拒否削除→H:A10 read-range（onfault PC不一致は既存fatalを別fixtureで検査）|
|A11 協調poll/tick|連続RX flood/PHY timeout/切断でもQMPのtick/TB±5%、poll budget、OF増分0。meterは最終印だけを信用しない|RX budget減算削除→H:A11 budget（1pollのRX消費が8枚を超えた瞬間に検出、模型は最大9枚で終了）|
|A12 像/回帰/meter|7450/7455、既存kboot全試験、LAN独立host capture、x86 build_compare、サイズ/閉包禁止symbol検査|meterのhost一致照合削除＋不一致capture→H:A12 false-accept|
- guest変異もP30で「直前LAN到達印＋期待のLAN ERROR code（例RESET_TIMEOUT）＋KBOOT ALIVE継続」を固定する。peekのPC/VA不一致など既存fatal対象は専用停止PCも照合。NIC失敗をOS停止の成功条件にしない。
- QEMU起動形：既存ISO/QMP/7450・7455/128M/-smp 1を保ち、`-nic none -device sungem,netdev=n0,mac=52:54:00:12:34:56 -netdev socket,id=n0,udp=127.0.0.1:18026,localaddr=127.0.0.1:18027`。hostは--udpで18026にbindし18027へ送る。user netは使わない。
- P0_QEMU.mdの確定事実は8.2.2・-S monitorのみ：既定sungem、0:15.0=106b:0021、BAR0 2MiB/未割当、bridge 106b:001f(dev11)/001e(dev14)/0022(dev12)、明示UDP設定受理。guestは未実行、OF後tree/BAR番地・PHY・DMAは未確認。P0の残りは既存OF採取経路と必要なMIF読出しで記録（不能項目は理由付き保留）、host socket経路確認と資料取得。OF NIC open/read/writeを合格条件にしない。実L2往復はP24/A7で証明する。
- LAN無効時は遷移につき`LAN OFF code=<数値>`1本だけ、通常LAN印にもFAIL/FATAL/UNSUPPORTEDを含めない。tools/ppc/kboot_run.py:40の既存禁止語/9印検査は維持。既定sungem付き②/無効LANでも合格する負経路をH29/A12で固定する。
- 実装者は該当像build＋対象試験--mutate＋lint/必要な選択検査。統合後はPPC_IMPL_RULESのkboot_all回帰。make check-changed/全体変異は回さず、landingはCI、full-checkはユーザー判断。今回これらは全て未実施。
## 8. 実機の危険・資料・未決
|順序|QuickSilverで初めて確かめること/観測|
|---|---|
|1 clock|個体model/PVR/OF tree/UniNorth revision採取→clock gate資料照合→readback→PCI ID。LAN以前の失敗はOF画面/既存表示とホストの無通信を区別して記録、ESCC外部配線を前提にしない|
|2 PHY|MDI/PHY address/ID/reset/交渉時間→100FD link LEDとhost link/capture。未知IDを汎用PHYとみなさない|
|3 DMA|許可RAM/bus変換→固定nonce1枚TX→RX1枚、guard/descriptor/completionとpcap照合。途中停止で領域を解放しない|
|4 cache|cache有効で複数wrap、異なるpayloadの連続往復、断線/再接続、queue overflow、診断応答とtick継続。QEMUのcoherentな成功で代用しない|
- **QEMU合格から実機のclock・PHY・DMA・cacheの安全性を外挿しない。** 実機MMIO bus hangはTB期限でも救えないため、資料と順序確認を先行する。
- PM取り寄せ一覧： [M1] Sun GEM/Apple GMAC当該revisionのregister/DMA descriptor仕様（bank、reset、mask、ring align、RX fetch/CRC/offset、Apple CONFIG bits）；[M2] QuickSilver Developer NoteとUniNorth当該revisionのclock gate/PCI config/host→PCI DMA窓・snoop/例外条件資料；[M3] 採取PHY IDのdatasheet/errata＋対応BSD MII実装の関数と行。MDDの資料で代用しない。
- **ユーザー判断が要る未決：0件**。メーカー資料が入手できない場合の扱いは 2026-11-10 にユーザーが決定：**「BSDを手本に」**（BSDの実装を根拠に実機へ載せてよい。メーカー資料は手に入れば照合に使うが条件にしない）。決定済みのGMAC先行/届く分だけ/固定診断/②維持は変更しない。
- **PM決定の候補**：資料入手とP0採取の担当、実機の--iface接続先と任意peer MAC、100FDの受入相手、poolが既存image枠へ収まらない場合の配置調整。ハード資料未解決は技術ゲートでありユーザーへの仕様再決裁ではない。
## 反論
- F8の実施時期/所有者/I-G化は採用するが、64KiBをpaging_set_pageループで変える案はmm/paging.h:99–105の必須規約に反するためpaging_map_rangeへ置換（反論1件）。

## 確定 (2026-11-10、Opus 5.5 の第 2 回の条件付き可を PM が反映)
- N1 (P1) §3(b): 起動を止める条件を「Master 解除を書いたのに readback が 1」だけにした。資料 [M2] が無いことを理由に allocator の前で止めない (② を後退させない、NIC の失敗で OS を止めない)。
- N2 (P2) §3(b)・§6: 早い段階の clock と Master 解除は `pm_gmac_early_quiesce()` (platform/powermac/pci_config.c) が持ち、ppc_kernel_main が memory_boot_init の直前で 1 回呼ぶ。P9 (arch/ppc/kernel_boot.c) は alias を張るだけで clock を呼ばない。P13 (gmac_clock.c) の前提は P8 (pci_config) で、P9 への依存は外す。
- N3 (P3) 状態表: pm_gmac_dma_init (dcbf と paging_map_range) は最初の 1 回だけ。link を失って RESETTING → PREPARE に戻ったときは ring_prepare と mac_init だけを繰り返す。
- N4 (P3) §4 PEEK: NIC が持っている RX buffer (pool の data 区画) は PEEK の許可範囲から外す (範囲が重なる要求は範囲外として拒否)。descriptor の RAM は読める。
- N5 (P3) §7: UDP の port は固定しない。kboot_run が run ごとに空いている port の組を割り当て、QEMU の netdev とホストの道具 (--udp) に同じ値を渡す (7450 と 7455 を並べて回しても衝突しない)。本文の 18026/18027 は例。
- 反論 (F8) は採用: descriptor の区画の I/G 化は paging_map_range (mm/paging.h:99-105 の規約)。
- 第 1 回の F1〜F19 と PM 決定の 3 点は r2 で直り、第 2 回で確認された。ユーザー判断の未決 0 件。資料 [M1]〜[M3] の扱いは下の「PM の通読」の R1 (ユーザー決定「BSD を手本に」)。
- PM の通読 (2026-11-10、ユーザーの指示で PM が確定版を通読し、手本の引用 4 点を抜き取りで照合):
  - R1 §1: 「メーカー資料と照合するまで該当ハード書込みを実装確定しない」は、[M1] の印がほぼ全部の GEM のレジスタに付くので、文字どおりだと QEMU の段も止まる。QEMU の段は BSD の値のまま進め、実機に載せる前に照合する、に直した。メーカー資料が手に入らない場合の扱いはユーザーに伺い、**「BSD を手本に」と決定 (2026-11-10)**: [M1]〜[M3] の印の付く書込みは BSD の実装を根拠に実機へ載せてよい。印は「BSD のどの行が根拠か」を示す目印として残し、実機で初めて確かめる物は §8 の順で観測する。
  - R2 §3(b): 「無効 NIC では Master 解除済みのときだけ ② 継続可」が N1 の直しと矛盾していたので、N1 の条件に揃えた。
  - R3 §6: 宣言・文書だけの段 (P4・P10・P22a・P22c・P33) の H 段は、それを使う段の H にまとめてよい (文書の変更に専用の試験ファイルを作らない)。
  - R4 §3(f): 最後の idle は `for (;;) pm_lan_poll(tb_now())` で CPU を止めずに回り続ける (QEMU の host の CPU を 1 つ使い切る、実機は発熱)。この段ではそのままにし、NIC の割り込み (段 E3 の後) を入れる段で halt に戻す。
  - R5 §4: PLN1 に認証は無い。既定では起動ログと BEACON を broadcast で流し、同じ L2 の誰でも PEEK でカーネルの RAM を 256B ずつ読める。開発用の LAN での診断という前提で、実機を共用の LAN につなぐときは peer を指定する。
  - 照合した引用 (合っていた): descriptor の形 (openbsd gemreg.h:529-532)、RX_KICK の 4 枚ごとの式 (freebsd if_gem.c:1581-1589)、UniNorth の GMAC の clock (netbsd uninorth.c:135-142)、RANDOM_SEED の式 (openbsd gem.c:888-889)。BAT は ② で全部無効 (PPC_KBOOT_DESIGN の ram_bat=0) なので、descriptor の区画の PTE の I/G は効く。EtherType 0x88B6 は既存の 0x88B5 (link・SFS) と重ならない。

## 実装で決めたこと (2026-11-11、段 P0〜P11 の取り込みで PM が決定)
- boot_id の生成は純粋な関数 `void pm_gmac_boot_id(struct pm_gmac_boot *b, u64 tb, u32 pvr)` に分けた (gmac_tree.c、host で試験するため PPC の命令を書かない)。TB と PVR を読んで呼ぶのは段 P5 (kboot_capture.c)。bootargs で上書きした印は struct pm_gmac_boot の `u32 override_flags` (bit0 = boot_id、bit1 = peer)。
- 早い段階の bus master の解除に記録と PCI の口を渡すため `void pm_gmac_quiesce_setup(const struct pm_gmac_boot *, const struct pm_pci *)` を足した (pci_config.c、呼ぶのは段 P25)。`int pm_gmac_early_quiesce(void)` は 0 = 続けてよい、負 = 解除を書いたのに読み戻しが 1 (確定の N1)。DMA の profile の暫定値は定数 `PM_GMAC_PROFILE_UNINORTH` (= 1)。
- UniNorth の親が `/` でない階層は、この段では祖先の ranges を順に変換せず LAN_EUNSUPPORTED で拒否する。
- 宣言の段の変異 (P1・P2) は、製品の `_Static_assert` で compile が落ちるものを RED と数える (compile の終了値・期待の assert の文字列・変異を当てた後の到達の印を照合)。加えて host の runtime の照合だけで落ちる変異を 1 本ずつ持つ。
- 実装者の sandbox は AF_INET の socket を作れない。UDP の netdev を付けた QEMU と UDP の loop の試験は PM が sandbox の外で回し、sandbox の中では rc=3 と別の行で知らせる形にした。
- tools/ppc/kboot_boot.py は x86 の build の生成物 build/out/memmap_layout.h を要する (新しい作業木では x86 の make を先に済ませる)。

## 実装で決めたこと (2026-11-11、段 P4・P5・P12・P20・P21 の取り込みで PM が決定)
- boot_id は capture (kboot_capture.c、host で試験する) では作らない。TB と PVR を読んで `pm_gmac_boot_id` を呼ぶのは LAN の service を始める直前の段 P25 (init/ppc_kernel.c)。
- struct pm_kboot_info に `struct pm_gmac_boot gmac` と `int gmac_rc` を足し、kboot_capture.c は tree walk の後に pm_gmac_capture を 1 回呼ぶ。LAN の失敗で起動を止めない。kboot_capture.c が gmac_tree.c を要するので、像の段 P26 の一部 (build/ppc-kboot.mk の明示の TU に gmac_tree.c) と、kboot_capture.c を組む既存の試験 4 本の TU の追従を前倒しした。
- ログの待ち行列の送信の側の口: `int pm_lan_log_peek(const u8 **data, u32 *len, u64 *seq, u64 *records, u64 *bytes, u32 *status)` (閉じた最古の slot を静的な scratch で見せる。status の bit0 = 飽和。開いた slot を閉じるのは、それが唯一の slot のときだけ) と `void pm_lan_log_complete(int result)` (受理で取り除き、AGAIN で残す)。宣言は lan_boot.h。
- DMA の pool は BSS (image の予約の中)。実際の像での収容の確認は段 P26 の link map。

## 実装で決めたこと (2026-11-11、段 P15〜P19 の取り込みで PM が決定)
- pool の配置の名前は gem.h の `GEM_POOL_TX_RING` 0・`GEM_POOL_RX_RING` 0x1000・`GEM_POOL_TX_BUF` 0x10000・`GEM_POOL_RX_BUF` 0x20000・`GEM_BUF_SIZE` 2048・`GEM_POOL_SIZE` 0x30000 (§3(d) の値)。gem_ring.c・gem_tx.c・gem_rx.c が共有する。
- TX の完了の回収は gem_tx.c の `int gem_tx_reclaim(struct gem *, u64 now, u32 budget)` で、gem_poll の末尾から呼ぶ。
- reset の入口は gem_reset.c の `void gem_enter_reset(struct gem *)` 1 つ (TX の誤り・RX の overflow の停止・TX の 5 秒の停止)。ここで `uncertain_tx += tx_pending` を 1 回だけ行い、tx_head・tx_tail・tx_pending を 0 に戻す (reset で NIC の TX の位置も 0 に戻り、ring を準備し直すため)。
- 段 P24 の service の契約: ring を準備し直すときは ring_prepare_step・ring_cursor・ring_publish_step と rx_head を 0 に戻してから gem_ring_prepare を呼ぶ。gem_rx・gem_tx_reclaim の GEM_EIO は gem_enter_reset で reset に回す (gem_rx は EIO の後に RX_KICK を出さない)。
- RX の準備し直しは OpenBSD gem_fill_rx_ring/gem_add_rxbuf の形: P15 は 0..27 の 28 枚だけ準備して RX_KICK=28 なので、使った枠 i ではなく枠 (i+28)%32 (生産側) をその枠の buffer で準備する。§5 の RX_KICK=(rxptr+32-4)&31 はこの形で OpenBSD の sc_rx_prod と同じ値になる (FreeBSD のように使った枠を戻すと、未準備の addr=0 の枠が NIC に渡る)。
- PHY: QEMU の sungem の PHY を PM が採取した (QEMU 8.2.2 mac99 -cpu 7450、OpenBIOS の prompt で Forth の l!/l@ を使い BAR0 0x82200000 の MIF_FRAME を叩く。l@ は BE なので byte を入れ替える)。MIF_CONFIG の LE 値は 0x100 (MDI0 だけ)、MDI0 の address 0 だけが応え PHYID1=0x0040・PHYID2=0x6210・BMSR=0x402c。openbsd miivar.h:236-238 で OUI 0x001018 (miidevs:46 BROADCOM)・model 0x21 (miidevs:178 BCM5201)・rev 0、bmtphy.c:67-68 が扱い BCM5221 以外は mii_phy_reset だけ。gem_phy.c の対応の PHY の表はこの 1 行で始め、表に無い ID は何も書かずに EUNSUPPORTED (§8「未知IDを汎用PHYとみなさない」)。QuickSilver の PHY は §8 の順 2 で採ってから足す。
- PHY の交渉の判定は BMSR の 2 回目 (今の状態、NetBSD ukphy_status) を使う。gem_phy_step が誤りを返すときは PHY_START に戻り、次の呼び出し (呼び手が 1 秒おき) で reset からやり直す。reset の待ちは NetBSD の 500ms (OpenBSD は 100ms)。

## 実装で決めたこと (2026-11-11、段 P22a〜P22d・P22・P23 の取り込みで PM が決定)
- §3 の `pm_lan_monitor(req, rsp)` では cache の鍵の送信元 MAC と診断の元が渡らないので、lan_boot.h の形を `int pm_lan_monitor(const struct lan_message *req, const u8 src_mac[6], const struct pm_lan_diag *d, struct lan_message *rsp)` (1 = 応答を作った、0 = 送らない) に置き換えた。`struct pm_lan_diag` は段 P24 の service が固定項目から作る: STATUS・MEMORY の u32 の並び (≤64 語)、READ_DIAG の 256B、PEEK の許可 `peek_ok` (≤8、managed RAM の実区間と像の予約)、I/G でも RAM と分かっている `peek_ig_ok` (≤2、DMA の descriptor の区画、§4「既知descriptor RAMはRAMとして許可」)、拒否 `peek_deny` (≤4、NIC が持つ RX buffer の区画、N4)、時計 `clock`・番地の変換 `translate`・1 byte の読み `copy8` の口。
- dst・src・boot・peer の照合、応答枠 1 個と満杯時の drop の計数は service (段 P24) が持つ。monitor は opcode・長さ・範囲・cache (送信元 MAC + nonce + request_id の 16 件) だけ。
- PEEK の 1 byte の読みの PPC の実体 `pm_lan_copy8` は lan_monitor.c に置く (IRQ 保存 → `ppc_trap_onfault_data` → `ppc_probe_copy8` → clear → 復元)。PPC の managed RAM は恒等写像なので、PA も同じ peek_ok の区間で照らす。
- `ppc_trap_onfault_data` は DSI の MISS・PROTECT・IO の集合だけを一回限り拾う。既存の exact reason の口 `ppc_trap_onfault` はそのまま (arm で data の印を落とす)。有効な PTE が MMIO の PA を指していても DSI_IO にはならないので、MMIO の拒否は monitor の I/G の検査と許可範囲で行う。
- 単純化 (段 P22e で行う、2 回の往復の後に PM が決定): `peek_ig_ok`・`peek_deny`・I/G の判定・d の NULL と件数の検査はシングルタスク前提では要らない。PEEK は VA と PA を RAM の許可表 1 種で照らすだけで I/G MMIO を拒否できる (MMIO の PA は RAM の表に無く、DMA の descriptor の区画は像の予約の中)。N4 は service が許可の区間を RX buffer の前後に割って渡す。d は呼び手が保証するので monitor は検査しない。
- ホストの道具 tools/ppc_lan_host.py: `--udp` では自分の MAC を `--mac` で与え (REQ の送信元と宛先の検査に使う)、LOG・BEACON は自分宛か broadcast、RSP は自分宛だけを採る。保存の世代は BEACON の next_seq と LOG の seq+1 の最大で追跡し、巻戻りで切り替える (§3(a))。lint が tools/ 直下の .py に受け持つ検査を求めるので CI の検査 tools/checks/ppc-lan-p23.py (tier 1) を登録した。

## P0 の観測 (PM、2026-11-10、QEMU 8.2.2 Debian 1:8.2.2+ds-0ubuntu1.18、ゲストは走らせていない: -S と monitor だけ)
- `qemu-system-ppc -M mac99 -S -monitor stdio` の `info network`: **既定の NIC は sungem** (`hub0port0: sungem.0 … model=sungem,macaddr=52:54:00:12:34:56`、相手は user (slirp) net 10.0.2.0)。
- `info pci`: Bus 0 device 15 function 0 = Ethernet controller **106b:0021**、IRQ pin A、BAR0 = 32 bit memory、大きさ 0x200000 (2MiB、firmware が走る前なので番地は未割当)。host bridge は 106b:001f (dev 11) と 106b:001e (dev 14)、106b:0022 (dev 12)。
- 明示の接続: `-nic none -device sungem,netdev=n0,mac=52:54:00:12:34:56 -netdev socket,id=n0,udp=127.0.0.1:18026,localaddr=127.0.0.1:18027` が通る (`info network`: `sungem.0 … \ n0: type=socket, udp=127.0.0.1:18026`)。**UDP の socket netdev は Ethernet の frame を 1 datagram に 1 枚で運ぶ**ので、ホストの道具は root も TAP も要らず、UDP の socket 1 本で raw の frame (EtherType 0x88B5 など) を送受信できる。既定の user net は IP しか通さないので使わない。
- 未確認: OF (OpenBIOS) が走った後の BAR0 の番地と device tree のノード名・property、sungem の PHY の模型、DMA の向き。ゲストを走らせる段 (P1) で採る。
