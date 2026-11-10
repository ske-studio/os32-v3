# PPC_LAN_SERVICE_DESIGN — PowerPC の LAN の service (段 P24) と起動への結線 (段 P25・P26) の境目

> 状態: **現行 (2026-11-11)** — 段 P24 (lan_service.c) とその前提・補助の段 (P24a〜P24d)、起動への結線 (P25・P26) の境目。全体の設計は PPC_LAN_DESIGN、LAN の印と受入の自動化は PPC_LAN_MARKS_DESIGN。段の状態は票 TASK_PPC_LAN が持つ。

> 出所: ~/os32-tmp/run/ppc-lan24d/DESIGN.final.md (gpt-6-astra 起草 r1→r2、Opus 5.5 第 1 回 D1〜D3 と第 2 回で条件付き可、PM が残り 1〜3 を反映)。中身は変えていない。

# P24/P25 境界設計 final
## r1→r2 の変更
- F1: clock alias未整備を認め、D1に従いP24/P25からclock呼出しを外す。
- F2: seq=0の初回BEACONは起動中1回だけとし、再接続で初回扱いに戻さない。
- F3: gem_rxは変更せず、serviceから1pollにつき1回だけ呼ぶ。
- F4: MIF走査のstep化を削除し、D2に従い250usの対象外で実測する。
- F5: PHY_DONEにBMSR単回読出しとlink喪失時のPHY_START/EIOを追加する。
- F6: 実行時の残時間検査を削除し、1ms間隔と件数予算を残す。
- F7: peek_okをRX区画を除く固定2区間にする。
- F8: lan_diag.cの関数境界を示し、ログ統計は既存4値をそのまま返す。
- F9: D3に従いP25/P26をLAN最後の同じ取り込みにする。
- F10: peer/link/response_fullを削除し、request_dropsは通常加算、DMA初期化口はPREPAREごとに呼ぶ。
- F11: serviceを7状態に整理し、MAC enable直前のg.step=0を維持する。
- F12: TB64をlan_gem_io.cに実装し、DMA同期宣言のheaderとmonitor戻り値の扱いを明示する。
- D1: 確定N2の実施時期を変更し、clockのslot・PA・先行呼出しは実機前の別段へ延期する。
- D2: MIF走査と初回dma_initは250us対象外とし、A11で各所要時間を記録する。
- D3: P25の結線とP26のbuild登録を同時に取り込む。
前提：シングルタスク・協調切替、NIC IRQなし。整数演算のみ。P22e優先。呼び手の保証を再検査しない。
## 1. 実物との境目
r1の確認を継承し、今回の再読は指摘関連箇所に限定。GPL・docs/archive/は未参照。
clock aliasは未整備。P24/P25はclock gateを要しないQEMUを対象とし、early_quiesce/startともclockを呼ばない。
gem_rxは1呼出し最大8descriptorを消費する既存仕様を使う。MIF走査も既存実装を維持する。
補完はgem_phy.cのDONE観測とlan_log.cの統計口の2件。service・PPC gem_io・診断組立て・P25結線は新規。
既存master_enable、boot snapshot/alias、codec、monitor、copy8を使用。P22e後のpeek_ig_ok/peek_denyは復活させない。
## 2. 境目のコード断片
以下は内部実装案。pm_lan_gem_io と pm_lan_diag_snapshot の宣言は platform/powermac/lan_boot.h に置く (pm_lan_copy8 と同じ置き場。宣言だけの変更なので使う側の段 lan_gem_io.c・lan_diag.c と一緒に入れてよい、ROLES の R3)。内部の定数は各 .c の中に置く。
```c
enum service_state { OFF, RESETTING, PREPARE, PUBLISH, PHY_WAIT, RUNNING, QUARANTINED };
static struct {
    enum service_state state;
    struct gem g;
    const struct pm_gmac_boot *boot; /* P25のstatic保存値 */
    u8 tx[14 + PLN1_HEADER_BYTES + PLN1_PAYLOAD_MAX], rx[1514];
    struct lan_message response;    /* type==0なら空 */
    u8 response_dst[6];
    u8 enabling;                    /* PHY_WAIT の中で mac_enable に入ったら 1 */
    u64 beacon_at, poll_at, phy_retry_at;
    u64 request_drops;
} s;
```
lan_gem_io.cの新規口は`pm_lan_gem_io(boot, &s.g.io)`。BAR/PCI aliasはppc_boot_devicesから取得する。
```c
/* ppc_dma_sync は lan_gem_io.c の中でプロトタイプを宣言する (gmac_dma.c:24-25 の ppc_dcbf_line と同じ前例)。dma_cache.h は作らず dma_cache.c も変えない。 */
int ppc_dma_sync(void *cpu, u32 bytes, enum dma_sync op);
/* lan_gem_io.c内。ppc_kernel.cのstatic tb_nowには依存しない。 */
static unsigned long long tb64(void *ctx); /* TBU/TBL/TBU一致読み */
static struct { struct pm_pci pci; u32 va; } hw;
static u32 rd(void *c, u32 off) {
    u32 v, a = hw.va + off; (void)c;
    __asm__ volatile("lwbrx %0,0,%1; eieio" : "=r"(v) : "r"(a) : "memory"); return v;
}
static void wr(void *c, u32 off, u32 v) {
    u32 a = hw.va + off; (void)c;
    __asm__ volatile("sync; stwbrx %0,0,%1; eieio" : : "r"(v), "r"(a) : "memory");
}
/* pm_lan_gem_io内：masterはcommand低16bitへMasterをOR、status上位は0、readback確認。 */
*out = (struct gem_io){ .ctx=&hw, .read32=rd, .write32=wr, .tb=tb64,
    .tb_hz=ppc_boot_info()->tb_hz, .sync=ppc_dma_sync, .master_enable=master };
/* lan_boot.hへ追加。lan_log.cで短いIRQ禁止下に既存4値をそのまま取得する。 */
void pm_lan_log_stats(u64 *next, u64 *records, u64 *bytes, u32 *flags);
/* gem_phy.cのgem_phy_step内：DONEだけ変更する。 */
case PHY_DONE:
    r = rd(g, g->phy_addr, BMSR, &v); if (r != GEM_OK) return r;
    if (!(v & BMSR_LINK)) { g->phy_step = PHY_START; return GEM_EIO; }
    return GEM_OK;
/* lan_diag.c：固定scratchを更新して返す。新しいcallback表は作らない。 */
const struct pm_lan_diag *pm_lan_diag_snapshot(const struct pm_gmac_boot *boot,
    const struct gem *g, u32 state, u64 request_drops);
/* service：照合済みREQかつ空枠のときだけ実行する。 */
d = pm_lan_diag_snapshot(s.boot, &s.g, s.state, s.request_drops);
if (pm_lan_monitor(&req, src, d, &rsp) == 1) {
    s.response = rsp; copy6(s.response_dst, src);
}
```
統計口はnext_seq/drop_records/drop_bytes/flagsを返し、端数加算・FIFO消費をしない。scratchは次回更新まで有効。
STATUSは17語：state、phy_id1、phy_id2、tx_packets・rx_packets・rx_drops・uncertain_tx・request_dropsの各hi/lo、error_status、tx_status、rx_status、tx_pending。linkはstate==RUNNINGから導く。
MEMORYは8語：0、admitted_end、image_first、image_end-image_first、hpt_pa、hpt_bytes、g.dma.pa、g.dma.bytes。
READ_DIAGは256B：0..67=STATUS、68..71=0、72..103=MEMORY、104..127=CLOCK、128..143=boot_id、144..149=自MAC、150..155=peer、156..255=0。整数はBE、MMIO窓にはしない。
BEACON payloadはSTATUSの68B。headerのseq/drop/flagsはログ統計（起動初回だけseq=0）、bootは保存値、nonce=0。
peek_okは`{0, dma.pa+GEM_POOL_RX_BUF}`と`{dma.pa+GEM_POOL_SIZE, PM_MEMORY_LEGACY_END-(dma.pa+GEM_POOL_SIZE)}`の2区間。RAM保証はreserveに任せる。
低い1MiBとガードはtranslateでRANGE、HPTは16MiBより上。descriptorは許可し、RX区画とそれを横断する要求は拒否する。
translateはr1どおりppc_pd_lookup_readにpm_p2v_ram経由のreaderを渡す。monitorがVA/PAを同じ許可区間で検査し、I/G専用検査は足さない。
CLOCKはIRQ保存→tick_count/tb_hz/TB64/platform_clock_read_usのlo/hi採取→IRQ復元。copy8はpm_lan_copy8。
Ethernetはserviceが所有し、dst/src/EtherTypeをbyteで組み、tx+14へencodeしてgem_txする。structを線上bytesへcastしない。
RX照合順は長さ→dst自MAC→src非zero/unicast→EtherType→decode→REQ型→boot一致→指定peer一致。broadcast peerは任意unicast srcを許す。
応答枠満杯ならrequest_dropsを通常加算しmonitorを呼ばない。RSPはAGAINで保持、OKでtype=0。reset中も未受理応答を保持する。
LOGはpeek→gem_tx→complete、OKだけ除去。初回BEACONは最初のRUNNINGでLOGより先に1回試行し、成功失敗によらず初回扱いを終える。
beacon_at=0を未試行印とし、初回試行後は次期限を保存する。resetで戻さず、以後1秒ごとに統計seqを使う。失敗分を再送しない。
```c
/* 各pollは現在の1段だけ。AGAINなら同段に留まり、後続状態へfall-throughしない。
 * OFF --start/reset開始--> RESETTING
 * RESETTING --gem_reset_step完了→gem_mif_init成功--> PREPARE
 * PREPARE --pm_gmac_dma_init→gem_ring_prepare完了→gem_mac_init--> PUBLISH
 * PUBLISH --gem_ring_publish完了--> PHY_WAIT
 * PHY_WAIT --gem_phy_step成功→g.step=0→gem_mac_enable_step完了--> RUNNING
 * RUNNING --link喪失/EIO/core RESETTING--> RESETTING
 * 未知PHY/無効条件→OFF、停止確認不能→QUARANTINED。
 */
```
RESETTING完了時にring_prepare_step/ring_cursor/ring_publish_step/rx_headを0にする。MIFは完了時1回、MAC_INITはprepare完了後1回。
PREPAREでは毎回dma_initを呼ぶが、2回目以降は既存結果を返すだけ。PHY_WAIT は phy_step が DONE になる前は gem_phy_step を呼び、OK を返した同じ poll で g.step=0 にして enable 側へ移る (enable に入ったかは service の構造体の `u8 enabling` で区別する。PHY_WAIT に入るときに 0、gem_phy_step が OK の poll で 1。gem_reset.c の非公開の RESET_DONE や gem_mac.c の step の値に頼らない)。以後は gem_mac_enable_step だけを呼び、gem_phy_step を再び呼ばない (RUNNING の link 観測は別)。
PHY_WAITの一時失敗は1秒後再試行。RUNNINGのDONE観測もphy_retry_atで1秒おき。異常時は送信を止め、必要時だけgem_enter_reset。uncertain_txはcoreだけが加算する。
1pollは最短1ms。TX回収はgem_poll(g,now,8)を1回、RXはgem_rxを1回（不正descriptorを含め最大8）、LOG試行4、RSP試行1、状態処理1。AGAINでその送信列を終える。
初回BEACONを除く順はRX→RSP→LOG→BEACON。実行時の時間予算検査はしない。250usはMIF走査・初回dma_initを除いて模型/QEMUで測る。
QUARANTINEDはpool保持。状態変化時だけ`LAN STATE state=<数値> link=<数値>`、無効化時は`LAN OFF code=<数値>`を1本出す。
## 3. P25の接続
ESCC成功後・最初のKBOOT前（132–137間）にlog_register。失敗はLAN無効だが、aliasが有効なら早期Master解除は行う。
memory_boot_init直前（145）にstatic bootへsnapshot保存。gmac_ready時だけaliasからPCIを構成しquiesce_setup→early_quiesce。負値は既存停止経路へ。
gmac_ready=0ではPCI/MMIOに触れずLAN OFFを1本。ID不一致等もLANだけ無効。quiesceの0を利用可能の保証とは解釈しない。
229の計測前にboot_id生成→start。startはPCI ID/Master=0を確認しreset開始、失敗はOFFを1本出して負値。P25は重ねて印字しない。
234は`do { now=tb_now(); pm_lan_poll(now); } while (...);`、240は`for (;;) pm_lan_poll(tb_now());`。OFF/QUARANTINEDは即return。EE=1と既存9印を維持。
alias有効・memory初期化済み・単回startをserviceで再検査しない。clock呼出しはD1の別段まで入れない。
## 4. ファイル別の変更と見積り
|段・ファイル|見積りと受持ち|
|---|---|
|前提1・gem_phy.c|約+5行。DONEのlink観測と対応試験・変異。|
|前提2・lan_log.c / lan_boot.h|約+15～25 / +1～3行。既存統計4値の取得口と試験。|
|P24補助・lan_gem_io.c|新規70～100行。PPC I/O 実体、ppc_dma_sync のプロトタイプは .c の中。lan_boot.h の pm_lan_gem_io の宣言を同じ段で。|
|P24補助・lan_diag.c|新規100～150行。固定scratch・2区間・診断組立て。lan_boot.h の pm_lan_diag_snapshot の宣言を同じ段で。|
|P24・lan_service.c|新規210～240行見込み。7状態・L2・キュー結線。補助と各別段。|
|P25＋P26・init/ppc_kernel.c / build/ppc-kboot.mk|+35～50 / +15～30行。LAN最後の同じ取り込みで結線・全必要TU/依存登録・link map確認。|
|実機前の別段・clock alias/PA/先行呼出し|D1によるN2延期。今回の前提2段には含めない。|
## 5. 受入→試験→変異
A8：実sinkのKBOOT全byte、PHY待ち、16KiB+256B、断線/再接続、AGAIN保持/OK除去、空FIFO統計をraw bytesと照合。再接続で初回seq=0を強制しない。変異はAGAIN除去・統計反映削除。
A10：製品service+monitorの5命令、偽dst/src/boot/peer、満杯・nonce切替、monitor非1時の空枠維持、RX前後/横断・descriptor・MMIO PA・DSI。変異はRX除外削除・満杯時monitor呼出し。
A11：badCRC混在で9descriptor目を消費しない、TX8/RX8/LOG4/RSP1/状態1、reset再準備・DMA実処理の再実行なし、TB停止を検査。通常pollの250usと対象外2処理の所要時間を別記する。
H24は製品TUを直接組みMMIO/時計/ASMだけ模型化。CHECK入力、一意anchor、runtime RED、到達印を要求。ring cursor初期化削除等を変異にする。gem_rx/P19のanchorは変更しない。
P25/P26統合後に7450/7455・動的UDP port・独立host capture・既存9印を照合。flood/切断でもtick/TB±5%、OF増分0。A12の像/DMA配置・禁止symbol・x86不変も確認する。
## 6. 未決・検証状況
未決0件。D1〜D3を採用済み。設計のみ・読み取り専用のため保存、preflight、ビルド、試験、変異、lint、QEMUは未実施。250us達成は未確認。


## 確定 (2026-11-11、Opus 5.5 の第 2 回の条件付き Approve を PM が反映)
- 残り 1 (§2 PHY_WAIT): enable への移行を service の構造体の `u8 enabling` で区別し (7 状態のまま)、gem_phy_step が OK を返した同じ poll で g.step=0、以後は gem_mac_enable_step だけ。
- 残り 2 (§2・§4): ppc_dma_sync は lan_gem_io.c の中でプロトタイプ宣言。dma_cache.h は作らず dma_cache.c も変えない。
- 残り 3 (§2・§4): pm_lan_gem_io と pm_lan_diag_snapshot の宣言は lan_boot.h、使う側の段と一緒に入れる。