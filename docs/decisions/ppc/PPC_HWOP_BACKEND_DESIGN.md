> 出所: ~/os32-tmp/run/ppc-hwop/DESIGN.final.md（設計ラリー、2026-11-23）。
> 状態: **確定（2026-11-23）**。末尾の確定節を優先する。

# r2→r3 の変更

- P1-1　OF の既知 GEM 判定を gmac_ready より先に行い、橋を使えなければメモリ初期化前に停止する。
- P1-2　選択 store の arm 不成立を専用出口へ送り、解除も隔離も行わない。
- P1-3　USB の探索用関数と保存済み関数に同じ root 参照を持たせ、構成と BAR の双方で使う。
- P1-4　memory_boot_init 直後に master の共有 PT を予約し、leaf 交換・復元と予約解除の順序を定めた。
- P1-5　物理転送の命令表と正常・途中 DSI の完了障壁、戻り値を指定した。
- P2-1　PPC の HW コマンド入口を囲み、exec が受信先と実行期間を所有する。
- P2-2　UniNorth の比較マスクは未確認のため 0 とし、readback 自体の失敗と区別する。

完了の基準の案：801af974b を基点に、§5 の host 変異が各々 RED、正常系が GREEN、mac99 の常駐シェルで OHCI の実読・停止・再実行が通り、生成物の差が PPC に限られること。

# 1. 実物の確認

照合版はローカルの origin/main（801af974b）。fetch は読取り専用、リモート照会は名前解決不可のため、それ以降の更新は未確認。
以下の行番号はこの版に固定する。表の一行を食い違い一件と数える。接続済みの本線を再実装しない。

|件|確定設計を適用するときの差|実装への帰結|
|---|---|---|
|1|platform/powermac/platform_hwb.h:6 の POINT は常に 0、同 :11 の check と :19–28 の phys/block は NOSYS|印と不変表の slot につなぐ。I/O の NOSYS は仕様通り。|
|2|platform/powermac/platform_hwb_addr.h:4 は絶対番地のみ成功。kdebug/hwb_wire.h:23 に 8 はあるが 7 がない|PPC の 7 のみ追加。絶対番地の sentinel を維持。|
|3|build/targets/ppc-powermac.mk:347 のリンクは diag.o と kstring_c.o。module_diag.c:10 は LIST 入口、kdebug_imports.h:4 は一枠|PPC モジュールに共通検証器と型付き輸入を組む。|
|4|sdk/kapi_target_ppc.py:7 は kdebug_call の宣言。build/select.mk:166 は HWDEV の選択条件、:176–184 が選択先で、hwb_access のオブジェクト列ではない|配送は module_diag、リンクは PPC の所有者に置く。公開 SDK は既存のまま使う。|
|5|platform/powermac/pci_config.c:35,52 は target_bdf と dword に限定。usb_platform.c:125 の byte write は dword RMW、platform_hwb_pci.h:4–19 は NOSYS|橋を共有し、実幅を取引の引数にする。|
|6|platform/powermac/kboot_capture.c:157 の tree は選択した装置を保存|全 PCI 部分木を OF の利用中に捕捉する。|
|7|arch/ppc/kernel_trap.c:60,92 は一命令・一 DAR。連続 store の各要素に対応する枠がない|既存の単発枠を拡張し、範囲と方向を照合。|
|8|platform/powermac/pci_config.c:110,117 の停止処理は Command 読み・解除書込み失敗で 0。ppc_kernel.c:285 は負値で停止し :288 のメモリ初期化を避ける|提供側を修正し、既存の呼出しで memory_boot_init 到達回数を測る。|
|9|platform/powermac/input_decode.c:14 は端末の CANCEL と 03 を ESC に畳む|意味を失う前に、常駐 CUI 宛ての中止要求を取り出す。|

- GEM と列挙は同じ transaction を呼ぶ。lan_gem_io.c:45 の master と pci_config.c:81 の保存コピーは同じ橋を参照する。GEM の MAC レジスタ操作はドライバ所有。
- USB も同じ transaction を呼ぶ。usb_platform.c:111 以降の構成 read の既存の失敗値、Command の set/clear と明示的な確認は入口で保つ。
- BAR と raw phys は同じ資源表、単発と block は同じ一時窓を使う。常設の USB DMA・GEM マップには一時窓の所有権を渡さない。

# 2. 共通設計 §4 の回答

|経路|実装時の判断|
|---|---|
|端末入力|端末受信キューへ投入する際、CUI の常駐シェル宛ての受信バイト 03 を ring3_abort_request に一度配送。待ち中も協調 yield の受信サービスが進むことを試験する。|
|通常の編集|HW 実行中だけ停止文字を消費する場合は実行期間を明示して囲む。通常編集は従来の ESC も渡せる。入力全体を POINT で読み捨てない。|
|USB 入力|input/hal/usb_input.c:47 の停止通知と同じ exec 入口。押下を一回だけ数え、離鍵・リピートで次の台本へ持ち越さない。|
|印の所有|exec/exec_abort.inc:83 の CUI/shell 判定が印を立てる。同 :34 が IRQ 保存下で交換。ロード冒頭の POINT が旧印を捨てる。|
|観測|wait・poll・fill の既存の点で ABORT。中止が来た後の要素を発行しない。block は一要求として完結。|

待機中は wait/poll → sys_yield → idle_work_run → terminal_service_once → pm_terminal_poll → pm_lan_poll → pm_terminal_feed と進める。PPC の待機サービスは arch/ppc/exec_kapi.c:15。POINT はキューを読まない。
```c
/* feed の discard・容量検査後。HW 実行期間は全終了経路で閉じる。 */
if (c == 3 && resident_cui_hw_active()) { ring3_abort_request(); continue; }
enqueue(c);  /* 通常編集は input_decode の従来処理へ */
on_open_or_line_lost() { set_editor_cancel_pending(); } /* abort 通知なし */
on_usb_ctrl_pause_down_edge() { ring3_abort_request(); } /* 既存入力口 */
```

PPC の常駐 CUI に限り、shell の cmd_hw を薄い入口と従来本体に分ける。既存の shell 用 kdebug_call に PPC 私有の enter/leave op を追加し、カーネル入口で exec に配送する。
`cmd_hw(args) { entered = hw_enter(); rc = cmd_hw_body(args); if (entered) hw_leave(); return rc; }`
exec は enter 時に既存の CUI/shell 所有者を確認し active=1、leave 時に active=0。load 後のプロンプトや step 間の編集では inactive。

区間は BAR0 先頭からの半開区間とし、割当サイズとの共通部分だけを有効にする。

- mac99 の Apple OHCI は 106b:003f、class/prog-if 0c0310、LE、[0x00,0x04) の HcRevision。台本は自然整列した 32 bit read 一回。列挙で実際の BDF を選ぶ。[mac99 の列挙記録](https://boxes-of-tat.blogspot.com/2022/05/linux-for-powerpc-on-qemu-mac99-system.html)
- G4 は既存の UniNorth GMAC プロファイルと 106b:0021 の一致時、LE、[0x6030,0x6038) の TX_CONFIG/RX_CONFIG。根拠は gmac_tree.c:164–175 の ID、pci_config.c:113–114 の実読、lan_gem_io.c:11 の逆順ロード、drivers/net/gem/gem.h:64。実機装着の新規観測はない。
- いずれも資源登録は番地と順序の記述。両登録とも整列 32 bit 単発 read のみ許可し、write/block と他幅は WINDOW。GMAC 台本には OpenBSD sys/dev/ic/gemreg.h のレジスタ定義と lan_gem_io.c:11 の LE アクセスを記す。未知の ID に class のみで範囲を開かない。
- OF の判読は PCI Bus Binding to IEEE 1275 v2.1 の assigned-addresses、ranges に従う。PCI 空間と装置レジスタの順序は別に決める。mac99 の HcRevision の根拠は OpenHCI Specification for USB Release 1.0a §7.1.1。台本冒頭に仕様と参照実装を記す（docs/POLICY_PCIDEV.md:8）。
- 橋エラーの判別根拠は MPC7450 RISC Microprocessor Family User’s Manual Rev 3.1、例外一覧の machine check 0x200（TEA/MCP）と DSI 0x300。SRR0/DAR の単発 DSI 回復を 0x200 に転用しない。非同期 machine check は当該構成取引に帰属できないので通常の fatal 経路。全 1 応答、既に立った PCI Status の sticky bit から障害を推測しない。
- 初版 UniNorth では取引に帰属する独立のエラーラッチを資料から確認できていない。存在を仮定したレジスタ読出し・W1C 消去は行わない。選択・解除の readback は書込みの flush として必ず実行する。
- 窓と Type 0/1 の手本は NetBSD uninorth_conf_read/write、tag 配送は OpenBSD pci_machdep.c。NetBSD の差分にある badaddr は保護アクセスの例。指定版の行番号、UniNorth の無応答・転送異常の通知方式は未確認、QEMU と実機で観測。[NetBSD の変更記録](https://www.mail-archive.com/source-changes-full%40netbsd.org/msg47458.html)

# 3. 後端の各口

## 呼出しと所有者

|slot|カーネルが提供する型（引数は記載順）|成功時の出力|
|---|---|---|
|0 snapshot|void (struct probe_list *)|既存 LIST|
|1 abort_take|u32 (void)|消費した印 0/1|
|2 cfg_read|u32 (u32 bdf,u32 off,u32 width,u32 *out)|CPU 順、ゼロ拡張|
|3 cfg_write|u32 (u32 bdf,u32 off,u32 width,u32 value)|指定 lane に一度書く|
|4 bar|u32 (u32 bdf,u32 bar,u32 space,u32 base,u32 offset,u32 bytes,u32 *addr)|CPU 物理先頭|
|5 phys|u32 (struct hw_req *)|成功した read のみ value に反映|
|6 block|u32 (struct hw_block_req *)|count 個を指定幅で store|
|7 phys_check|u32 (u32 addr,u32 width,u32 bytes)|不変区間の判定のみ|

- モジュールは解決済みの slot 表を所有し、実装状態はカーネルが所有する。各フィールドの型と offsetof == index*4 を静的検査。
- 梱包時とロード時に 8 枠の順、名前 hash、kind、type_ver を一致させる。一枠でも必須解決に失敗したら dbg_call を公開しない。
- 32 byte の通常要求、40 byte の block、44 byte の範囲要求を混用しない。buf/size/version の失敗は呼出しエラー、内容エラーは call=0/status=ARG。
- HW_OP は通常 hwb_access へ渡す。PPC 私有 7 だけは次の分岐をモジュールで先に行う。PROBE_OP は既存の LIST 分岐。
- 7 の送信元は shell 側の platform_hwb_addr.h。絶対番地なら sentinel で offset を返す。BAR は呼出し可能な shell の kdebug_call に 44 byte を渡す。
- bar の口には要素幅がないため、7 はバイト単位の区間変換（wire.width=1）と定義。実幅の自然整列は he_address と特権側 phys/block が各々検査する。
- 外枠の size を確定する前に末尾を読まない。7 は bar<6、BDF<=65535、space が IO/PHYS、bytes>0、value/status=0 を検査。

```c
/* shell → module。rc==0 かつ status==HW_OK のときだけ value を使う。 */
struct hw_range_req q = {{HW_VERSION,7,space,1,bdf,base,0,0},bar,offset,bytes};
rc = kdebug_call(HW_OP,&q,sizeof q);
/* kernel: 型付き定数表 exports の sizeof == 32、各欄の位置を静的検査 */
request.imports = (const u32 *)&exports; /* kdebug/kdebug_module.c:57–62 */
/* module */
if (op == HW_OP && valid_prefix(buf,size) && req->action == 7) {
    if (size != sizeof(struct hw_range_req)) return OS32_ERR_INVAL;
    u32 pa = 0;
    req->status = req->width == 1 && valid_bar_fields(r) ? slots.bar(req->bdf,r->bar,req->space,
                         req->addr,r->offset,r->bytes,&pa) : HW_ARG;
    req->value = req->status == HW_OK ? pa : 0;
    return 0;
}
if (op == HW_OP) return hwb_access(buf,size);
```

- 8 は共通 hwb_access.c:22 のまま。未使用欄、幅、整列、正の幅倍数、末尾を検査。slot 7 は検索一回、窓とバスへのアクセスはゼロ。
- PCI は drivers/pci/pci_hwb.c:17 の共通検査を通す。PPC モジュールに同 TU をリンクし、platform_hwb_pci.h から slot 2/3 へ。
- 有効なポート操作は call=0/status=HW_NOSYS。READ 失敗の value=0、WRITE の value は保存する。部品無しの call=OS32_ERR_NOSYS と区別。

## 橋の登録と取引

```c
/* OF 利用中、親セル幅で reg を CPU PA に翻訳。失敗した host は登録不可。 */
for_each_host(node) if (pci_type(node) && compatible_token(node,"uni-north") &&
    valid_bus_range(node,&first,&last) && decode_cpu_reg(node,&pa,&len) &&
    fits_aperture_u32(pa,len,0x800000,8) && fits_aperture_u32(pa,len,0xc00000,8))
    capture_host(node,first,last,pa); /* OF 走査順に domain 番号を固定 */
for_each_captured_host(h) register_pair_if_both_mapped(h.id,h.pa+0x800000,h.pa+0xc00000);
default_host = first_captured_supported_host(); /* 列挙・HW_OP 共通。別 domain へ代替しない */
pci_config_probe() { return mapped_pair(default_host); } /* ID read なし */
/* 捕捉した root は起動後も不変。構成窓の VA は橋別に固定する。 */
root.pci = bind_registered(root.host_id); /* 失敗した USB root は探索しない */
first.root = hc->pci.root = &root; /* fn 0 の予備読取りより前。コピーにも参照を保持 */
usbp_cfg(f,...) = transaction(f->root->pci,bdf(f),...);
pa = translate(f->root->win,bar_base,size); /* VIA 重複・整列・サイズ検査を維持 */
map_key = (f->root,bdf(f),bar,pa); /* 全欄一致のみ常設 VA を再利用 */
handle = bind_registered(domain.host_id); /* 列挙/HW_OP。bus-range 外は選択不可 */
tag = (bus << 16) | (dev << 11) | (fn << 8);
reg = off & ~3u;
select = root_bus ? ((1u << dev) | (fn << 8) | reg) : (tag | reg | 1u);
lane = handle.data_va + (reg & 4u) + (off & 3u);
```

- 選択式の評価前に機構無しを NOSYS とする。隔離済みは IO_ERROR。根 dev<11 は read が幅分全 1、write は OK、選択も発行しない。
- IRQ を保存して EE=0、橋ロックを取得し隔離を確認。協調実行中はこの区間で yield しない。再入は待ち続けず失敗。
```c
/* IRQ 保存・橋取得後。初期状態 rc=HW_IO_ERROR、読取り値はローカル。 */
if (!arm_select_store(A)) goto done; /* 既存 guard はそのまま。ここまでバス操作 0 */
if (!run_armed_select(A,select) || !flush_address(A,select)) goto deselect;
rc = lane_access_once(lane,width,op,&tmp);
deselect:
if (!put_le32(A,0) || !flush_address(A,0)) { bridge->isolated=1; rc=HW_IO_ERROR; }
done: /* 解除を再試行しない。 */ unlock_restore_irq(); if (read) *out = rc == HW_OK ? tmp : 0;
```

|選択と解除の観測点|データ命令|解除試行／隔離|
|---|---|---|
|選択 arm 不成立|0|0／なし、IO_ERROR|
|選択命令 DSI・選択 flush 失敗|0|1／解除失敗時のみ|
|選択成功|1 回まで|1／解除失敗時のみ。解除失敗なら read 値 0|

`flush_address(A,v) = eieio(); guarded_read_le32(A,&rb) && (((rb^v)&verified_mask)==0)`。各段で前のアクセスを順序付ける。verified_mask=0。値の完全一致を要求する版・有効ビットは未確認。QEMU と実機で観測（選択値・解除値・各 readback 生値を記録）。
- 1 byte は lbz/stb、LE16 は lhbrx/sthbrx、LE32 は lwbrx/stwbrx。順序は eieio、完了が必要な箇所は sync とアドレス窓 readback。
- データ失敗でも解除成功なら次の取引は可能。解除失敗なら全入口で以後の address/data 回数がゼロ。別橋は継続。
- early quiesce は既知装置の ID 取引失敗、Command 読み、master 解除書込み、確認失敗で LAN_EIO。正常応答の ID 不一致は既存の対象外判定。既知 GEM の clock 準備失敗・保存 BDF 不一致は LAN_EIO。

```c
/* ppc_kernel.c、既知性は OF の node/profile/vendor_device から決める。 */
if (known_gem(info->gmac)) {
    if (!dev->gmac_ready || !(p = bind_registered(info->gmac.host_id)) ||
        pm_gmac_quiesce_setup_rh(&lan_boot,p,dev->clock_va) < 0 ||
        pm_gmac_early_quiesce() < 0) kboot_stop("gmac_quiesce");
    pm_pci_boot(p,lan_boot.bdf);
} /* 既知 GEM なし、または停止確認済みで memory_boot_init へ */
/* USB set/clear と GEM の停止。失敗値を合成する列挙用 read は通さない。 */
if (cfg_read(bdf,4,2,&cmd) != HW_OK) return LAN_EIO;
want = (cmd | set) & ~clear;
if (cfg_write(bdf,4,2,want) != HW_OK) return LAN_EIO;
if (verify && (cfg_read(bdf,4,2,&cmd) != HW_OK || ((cmd ^ want) & (set|clear)))) return LAN_EIO;
```
- pci_backend の既存 write16(Command) は幅2で配送。従来の pm_pci_write32 の Command 特例は value & 0xffff を維持し、汎用 HW_OP の実幅 write は独立させる。

## OF 資源と BAR

- pm_kboot_info が host/親 ID、bus-range、子/親セル幅、ranges の型・両基点・長さ、各割当の BDF/BAR・空間・基点・長さ・compatible・PCI ID を所有。
- child/peer で全橋を再帰走査。セル数の割り切れ、深さ、容量を記録。途中で欠けた枝は親に不完全を伝播し、重複し得る PA を raw phys からも除外する。親の範囲を絞れない場合は当該 domain 全体を不完全にする。起動後の OF ハンドル参照は禁止。diag 公開前に不変の割当表と PA 区間表を生成。

```c
/* BE セルを CPU 順へ。assigned-addresses 各項で構成領域を除外。 */
ss = (hi >> 24) & 3; bdf16 = (hi >> 8) & 0xffff; regno = hi & 255;
if (!ss || regno < 0x10 || regno > 0x24 || (regno & 3)) reject_bar();
barno = (regno-0x10)/4; pci_base = ((u64)mid << 32) | lo;
/* ss=1 IO、2 MEM32、3 MEM64。BAR の型と照合し、64 bit 対の上位欄は除外。 */
if (space == HW_IO) return HW_NOSYS;
if (!unique_assignment(bdf,bar,space,&a) || base != a.base) return HW_WINDOW;
if (!bytes || offset > a.len || bytes > a.len-offset) return HW_ARG;
if (!checked_add64(a.base,offset,&x)) return HW_ARG;
ss = a.ss;
for (p = a.parent; p; p = p->parent) {
    if (!unique_containing_range(p,ss,x,bytes,&r)) return HW_WINDOW;
    if (!checked_add64(r.parent_base,x-r.child_base,&x)) return HW_ARG;
    ss = r.parent_space; /* PCI 親は親 phys.hi の ss、CPU 親は物理空間 */
}
if (x > U32_MAX || bytes-1 > U32_MAX-x) return HW_ARG;
return resolve_unique_profile(x,bytes,addr);
```

- 多セル上位の切捨ては禁止。捕捉時に基点＋長さの溢れは枝不完全。全割当の全域を PA に翻訳して交差を記録し、部分的にしか訳せない割当は親の候補域を閉じる。PA 表は未知所有者の割当も競合検出に含める。実基点が変われば WINDOW。BAR サイズ測定書込みをしない。
- fill/block は count 確定後の総 bytes を使う。割当越境 ARG、確定プロファイルからの越境・重複は WINDOW。失敗時のストア数は 0。

## 一時窓と例外

```c
rc = lookup_order(addr,width,bytes,&order);
if (rc) return rc;
if (!window_begin(&w)) return HW_WINDOW;
rc = map_all(&w,addr,bytes,write);      /* 変更直前に旧 PTE を記録 */
if (rc == HW_OK) rc = guarded_transfer(op,w.va,width,order,value,count,increment,&tmp);
if (!restore_reverse_and_verify(&w)) { w.owner->isolated = 1; rc = HW_WINDOW; }
window_end(&w);                        /* ロック解放後、保存 IRQ を復元 */
if (op == READ) *out = rc == HW_OK ? tmp : 0;
return rc;
```

- カーネル専用の連続 2 VA ページを予約し、常設 alias と重ねない。begin は IRQ 保存、占有確認、隔離確認。失敗時も保存 IRQ を戻す。
- (addr のページ内位置 + bytes) から必要枚数を決める。I/G=1、supervisor、read は読取専用。第 2 ページ失敗時も第 1 ページを戻す。
```c
/* memory_boot_init 成功直後、shm_init と最初の子 PD 作成前。master が current。 */
if (paging_window_reserve(window_va,&pt) < 0) disable_window(); /* phys/block は WINDOW */
 /* 専用 PDE の prepare_tables(va>>12,2)。空 leaf の PT を子 PD に継承。 */
 /* 公開前の初期化撤回は paging_window_release(pt): PDE を外し同期後 pgalloc_free_pt。
    公開成功後は起動中保持。window_end は leaf だけを戻す。 */
pp = write ? 0 : 3; /* Ks=0/Kp=1: PP0 は supervisor RW、PP3 は両 key RO */
attr = I | G | pp; /* 専用 VA に限り pd_walk に適用。user の walk は常に拒否 */
```
- PP3 の HPT hit は user read も許す。窓の有効期間は単一 CPU、EE=0、yield・user 復帰禁止。公開前・復元後は HPT/TLB を全消去する。隔離時も旧状態を検証できなければ両 leaf を無効化して全消去し、遮断を検証できなければカーネルを停止する。
- map・逆順 restore・遮断とも EE=0 で raw leaf を設定 → sync → current root の HPT をクリア → sync → 全 tlbie → eieio → tlbsync → sync → isync。完了後にのみアクセスまたは IRQ 復元。旧 PTE の無効状態も記録し、逆順復元後に元の変換・属性を照合。
- 単発は実命令 PC、実 VA、専用復帰先を EE=0 で arm。枠使用中なら IO_ERROR で戻し他者の枠を保持。正常出口は自分の枠だけ clear。
- block は store PC と先頭・最終要素 VA、幅、store 条件をループ前に一度 arm。幅/順で外側を分岐し、要素間に呼出し・再マップ・読戻しを置かない。

```c
/* 単発は count=1, increment=0。ASM は op/width/order 別の実命令を選択。 */
if (!arm(insn_label,va,va+(count-1)*width,width,op!=READ,resume_label)) return HW_IO_ERROR;
/* insn_label は load/store 自身。load 完了時のみ *tmp を更新。resume_label は
   同じ ASM 関数の失敗エピローグ。既存 ISI/単発の照合を保持。手本 arch/ppc/kernel_probe.S:ppc_probe_copy8。 */
match = guard.active && supervisor && vector == DSI && known_data_reason && srr0 == guard.pc;
match &= dar >= guard.first && dar <= guard.last && (dar-guard.first)%guard.width == 0;
match &= (!!(dsisr & PPC_DSISR_STORE) == guard.store);
if (match) { guard.active = 0; frame->srr0 = guard.resume; return; }
```

|幅|BE load / store|LE load / store|
|---|---|---|
|1|lbz / stb|lbz / stb|
|2|lhz / sth|lhbrx / sthbrx|
|4|lwz / stw|lwbrx / stwbrx|

`armed: eieio → insn_label: load/store → eieio → 次要素または sync → clear → return HW_OK`。read は一要素をゼロ拡張し、正常出口で tmp へ渡す。
`resume_label: eieio → sync → return HW_IO_ERROR`。trap が自枠を消費済み。先行 store の完了を待ってから C が窓を戻す。DSI では tmp を公開しない。block の値は要素ごとに increment を足して幅内で巡回。途中 DSI は完了分を保持し、失敗要素を再実行せず cleanup。復元失敗の WINDOW を優先。

# 4. ファイル別の段と大きさ

各行は一ファイル・一担当の変更単位。P は platform/powermac/。

|ファイル|変更|先行条件|
|---|---|---|
|P/kdebug_imports.h|共有 slot 型・提供表・オフセット検査。モジュール側に提供者の実体参照を混入させない|型確定|
|P/kdebug_imports.json|8 枠の梱包契約|型確定|
|P/module_diag.c|slot 実体、7 の検証、18 の配送|型確定|
|P/platform_hwb.h|POINT/phys/block/check の typed call|型確定|
|P/platform_hwb_pci.h|検証済み PCI の typed call|型確定|
|P/platform_hwb_addr.h|shell の 7 送受信と sentinel|wire 確定|
|P/lan_boot.h|pm_pci に共有橋への参照、実幅 API|橋契約|
|P/pci_config.c|橋登録・取引・解除・early quiesce の失敗伝播|橋型、保護命令|
|P/pci_backend.c|既定 domain・列挙の入口を登録済み橋へ|橋型|
|P/usb_platform.c|byte RMW を実幅に置換、Command 確認|橋型|
|P/kernel_boot.c|host ごとの構成窓を早期配置、GEM の BAR 配置を独立|host 捕捉|
|P/lan_gem_io.c|master を host ID で bind|橋登録|
|P/gmac_tree.c|捕捉に host ID を伝播|橋型|
|P/usb_tree.c|各 USB root に OF host ID を保存|USB root 型|
|P/usb_boot.h|USB root に host ID 欄|捕捉契約|
|P/usb_boot.c|USB ハンドルを host ID で bind|橋登録|
|arch/ppc/pd_walk.c|窓専用 leaf の PP とアクセス判定|PT 予約|
|P/kboot_info.h|上限付きの親子・資源レコード|セル形式確定|
|P/kboot_capture.c|全 PCI 部分木をコピー|レコード型|
|P/hwb_resource.c（新）|不変表の構築、BAR と raw の検索|レコード型|
|P/ppc_kernel.c|橋登録・資源構築を早期停止・diag 公開に先行|資源と橋の API|
|arch/ppc/kernel_trap.c|block guard の arm と範囲照合|例外 ABI|
|arch/ppc/hwb_transfer.S（新）|命令ラベル、復帰口、幅別 block|例外 ABI|
|P/hwb_window.c（新）|窓予約・PTE/IRQ 所有、各出口の復元|例外 ABI、資源検索|
|userland/shell/cmd_hw.c|PPC 入口で enter。run/load/step/continue/abort/reset/直接命令、引数・確保・ログ失敗を含む本体の全 return 後に leave|実行期間|
|exec/exec_abort.inc|PPC resident_cui_hw_active の状態と enter/leave。常駐 shell の実行終了時にも破棄|実行期間|
|kdebug/kdebug_module.c|PPC のみ enter/leave を module 呼出し前に exec へ配送|exec|
|mm/paging_map.inc|PPC 限定 reserve/release と raw leaf 交換。専用 PDE の所有を記録|paging|
|include/usb_platform.h|PPC 限定 usb_pci_fn.root を探索と保存の両方に保持|捕捉契約|
|P/terminal_input.c|受信サービスで active 中の 03 を exec に通知|入力経路|
|build/targets/ppc-powermac.mk|PPC 専用の常駐対象、モジュール用 hwb_access.o と pci_hwb.o を選択|各提供者|

- 大きさは未ビルドの予算。kdebug.elf の alloc 節増分 2–5 KiB（検証器、配送、8×4 byte の slot）。ELF ファイル長は整列・再配置で別に増減する。常駐の text/data は 5–12 KiB、表は例として橋 8×64、ranges 64×40、割当 128×64、物理区間 128×24 byte で約 14 KiB。合計 14,336 B。捕捉コピーは別に host 8×64、ranges 64×40、割当 128×64、文字列・作業域 4 KiB の計 15 KiB を上限とし、超過は枝不完全。PT 1×4 KiB（物理）、窓 VA 2×4 KiB。常駐増分予算は 12+14+15+4=45 KiB、ロード済みモジュールの節・BSS・ページ丸めを別枠 8 KiB として計 53 KiB。梱包 kdebug.bin はヘッダ・再配置込み増分 8 KiB 枠。kdebug.elf、kdebug.bin、ロード後割当ページ、常駐像 text/data/BSS、配布像を個別に差分計測。計測は同じ FEATURE_KDEBUG 設定で size -A と link map の前後差、配布像の byte 差を併記。BSS、8 KiB の予約 VA、実ファイル長を混同しない。

# 5. 段別の受入・試験・変異

実装順は 3c の host 捕捉 → host 別マップ → 6a の単発保護命令 → 3b/6c の取引・起動停止を同時結合。資源変換・5c の PT 予約/復元・単発 load の結合と 3a を終えて段4へ進む。block の受入は単発の後。捕捉、マップ、PT、ASM は各々単独の host 試験を持つ。各行半日〜一日。host は実入口を呼び、失敗値と副作用の両方を計数。新規ゲストは全て s.wait でプロンプト・結果・次のプロンプトを待つ。

|段|受入 → 試験|一行変異 → RED の観測|
|---|---|---|
|3a 輸入|host で全 slot の型と梱包契約、mac99 で LIST と POINT、部品無しの呼出し失敗|slot 7 の hash を変更 → dbg_call 公開不可|
|3b 取引|host で GEM 無しの既定橋から列挙と HW_OP が同じ状態を選ぶ|target_bdf 制約を戻す → 別 BDF の実読不能|
|3c 捕捉|host の二段橋、異なるセル幅、後方の OHCI、容量超過を OF 終了後に照合|親 ID を根へ固定 → PA 不一致|
|4 OHCI|mac99 の KDEBUG 有効像で ID/class/BAR0 を pciread、CFG_OFFSET/CFG_VALUE を出し BAR0+0 を mmioread32。終了 0、独立計算 PA と既存初期化の revision 下位 8 bit が一致|LE32 変換を外す → revision 不一致|
|5a 停止|mac99 で wait と poll の各実行中に中止し ABORT、直後の再ロード成功。通常入力文字も保存。接続直後は印 0、明示停止直後の連続 POINT は 1 → 0|take の clear を外す → 次の POINT でも 1|
|5b port|幅 1/2/4 の read/write 各一台本で NOSYS・終了 3。host で call=0/status=2 と read=0|IO を OK にする → 終了値不一致|
|5c 窓|host で二枚目の map と逆順復元に個別失敗、初回 store 0、二枚目失敗後の旧 PTE・実効変換・IRQ と直後の再利用成功、復元不能後の窓取得拒否|逐次 map/store に変更 → 先行 store を検出|
|6a DSI|host の trap frame と PPC の命令ラベルで単発 load/store・途中 store の SRR0、DAR、前後の値、IRQ/PTE を照合。別 PC/DAR は通常経路。mac99 の試験専用 VA で二枚目を無効にし、実 store で同期 DSI → rfi → ASM 失敗出口 → cleanup を観測。完了済み値、後続 store 0、次の取得成功を確認|範囲の上限を一要素広げる → 対象外 DAR を消費|
|6b 橋|選択/flush/data/解除/解除 flush を各別に注入。解除不能後は HW_OP、列挙、GEM、saved、USB、新コピーの address/data 増分各 0、別橋成功。同 BDF の二橋を捕捉し後から先の USB controller で cfg/Command と BAR 再取得、先の橋と PA に一致|隔離をコピー内に置く → 次の入口で取引発生|
|6c 起動|既知 GEM の host 捕捉・窓 map・bind の各失敗（gmac_ready=0 を含む）で停止。Command 読み・解除書込みの各失敗で負値、memory_boot_init 到達 0。正常な解除後は到達 1|失敗を 0 に置換 → 起動が進む|
|7a 幅|Type 0/1、上下 dword、全 lane、W1C 隣接保持。選択不能・空 slot・機構無し・取引障害で各幅の値と試行数 0/1/0/1|16 bit を dword RMW に置換 → 隣接 status 消失|
|7b 区間|host の LE/BE 両窓で単発・block の生バイトと値を照合。末尾一致、1 要素超過、親越境、実基点変更、未知・競合、fill 最終越境で初回 store 0 を検査|bytes を width に置換 → 越境 fill が書く|
|7c 検査のみ|8 の短長・余分な末尾は INVAL、不正欄・桁あふれは ARG、いずれも slot 0 回。正常/未知は slot 1 回、窓・バス 0 回|検査を phys に置換 → バス回数増加|
|7d 結合|基点と同条件で x86 build_compare 差 0、PPC のみ生成物申告。既存 HW 台本とログの回帰、mac99 の再起動を確認|PPC の選択を x86 に混入 → 差分検査失敗|

# 6. 未決

ユーザー判断を要する未決は 0 件。受入は mac99 まで。G4 は再起動経路を確認した像でのみ将来の読取り試験を行い、HDD・NVRAM は対象外とする。

反論は 0 件。

## 確定 (2026-11-23、sol 第 3 回の条件付き承認、PM が反映)
本文と食い違う所はこの節を正とする。
- **R3-P1 GEM の既知の判定**: OF で GEM を識別した時点で `gmac_known` を起動の情報に保存し、橋の捕捉が失敗しても保持する (今の gmac_tree.c は捕捉の失敗で OF の識別も消すため、`known_gem(info->gmac)` が偽になって停止を飛ばし得る)。ppc_kernel.c はこの印で停止の確認に入り、捕捉・マップ・bind の失敗の時は `memory_boot_init` の前に停止する。
- **R3-P1 停止の入力の配送**: 実際の呼び出しの口は `kernel/module/kdebug_call.c` の `kdebug_call` (kdebug_module.c はロードと公開)。ここで PowerPC の私有の enter/leave を `ops.dbg_call` の呼び出しの前に exec へ配送する (共通のファイル — x86 の生成物は変えない、build_compare で示し、取り込みの前に本線に見せる)。`shell_cmd_hw` は enter の戻り値が 0 の時、本体の全部の終了の道で leave を呼ぶ。
- **R3-P2 受信の 03**: `pm_terminal_feed` は受信のバイトを取り出した直後、実行中の `03` を中止の印へ一度配送して消費する。残りのバイトには既存の捨て読み・容量の処理を当てる (捨て読み中・満杯の時に停止のバイトまで落とさない)。
- **R3-P3 (実装の段 6b へ送る)**: 既存の保護の枠を占有したまま構成の取引を呼び、IO_ERROR・アドレスとデータのアクセス各 0・隔離なし・既存の枠の保持を確かめる受入を足す。
