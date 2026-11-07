# PPC_MMU_DESIGN — OF からの切り離し、自前のベクタとハッシュ表

> 状態: **現行 (2026-11-07)** — OS32 が OF から切り離れ、DSI/ISI と decrementer を受けて復帰する所まで (mm はリンクしない案 C)。段の状態は票 TASK_PPC_MMU が持つ。
> 出所: ~/os32-tmp/run/ppc-mmu/DESIGN.final.md を 2026-11-07 に写した (astra 起草 r1〜r2、Opus 5.5 第 1・2 回)。

## 1. 実物の確認

- 読取元 `S=/home/hight/wt/os32-v3/ppc-env` は HEAD `229ed19f`。製品変更・起動は行っていない。例外として許可された D の単体コンパイルのみ実施（§2、成果は本ディレクトリの tmp/compile-d）。
- 手本の略記（以下すべて file:line）：`N=/home/hight/os32-tmp/ref/netbsd` =36eddebc、`O=…/openbsd` =b907757a、`F=…/freebsd` =6eae951c（実 HEAD の短縮表示も一致）。GPL ソースは参照しない。
- `NP=N/sys/arch/powerpc/oea/pmap.c`、`NM=N/sys/arch/powerpc/oea/oea_machdep.c`、`NC=N/sys/arch/powerpc/oea/cpu_subr.c`、`NO=N/sys/arch/powerpc/oea/ofwoea_machdep.c`。
- `NF=N/sys/arch/powerpc/powerpc/ofw_machdep.c`、`NT=N/sys/arch/powerpc/powerpc/trap_subr.S`、`FP=F/sys/powerpc/aim/mmu_oea.c`、`FT=F/sys/powerpc/aim/trap_subr32.S`、`OT=O/sys/arch/powerpc/powerpc/trap.c`。
- 指定された pmap、trap、locore、machdep、ofw_subr.S は存在。`N/sys/arch/powerpc/oea/ofw_subr.c` は無い（同名の C は `N/sys/dev/ofw/ofw_subr.c`）；メモリ取得実装は NF。`__syncicache` の本体は指定ファイル内に無いので、コピー同期には後述の ofwreal.S の実在する列を使う。

|食い違い（数える対象は D1～D7）|実物と設計上の帰結|
|---|---|
|D1 根は不透明、との契約|S/include/cpu.h:42–58 に対して下表の直接参照。命令wrapperだけでは不足。Dなら根をソフトPDとして扱い、miss walkerまで実装する。|
|D2 PPC の原始命令ヘッダは既存ではない|S/arch/ppc は entry.S 等だけ。`arch_io.h`・`arch_cpu.h`・`platform/powermac/platform_io.h` は無い。新規作成が必要。|
|D3 アーキ独立ヘッダの依存|S/include/cpu.h:30–34 は x86_flags.h の直接 include と cdecl の説明。この段では変更しない。定数includeはPPCの命令を生成しないが、cdeclをPPC ABIと同一視しない。|
|D4 OF から完全に切れた状態ではない|S/arch/ppc/entry.S:42–62 は元の MSR を戻す。S/platform/powermac/boot.c:49,51 は送信失敗時 OF stdout へ戻る。切離し後には使えない。|
|D5 BAT0 の 256MiB は物理 RAM 量ではない|S/platform/powermac/ppc_layout.h:44–45、entry.S:54–59。RAM の実在範囲は OF reg/available から別に得る。|
|D6 7450 と7455は同じ BAT 数ではない|NC:321–331 は7450を HIGHBAT から除外。既存 entry.S:46–53 は0～3だけ無効化。7455の4～7は切離し時に処理する。|
|D7 実機の OF 画面は切離し後の観測路にならない|S/docs/decisions/ppc/PPC_BOOT_DESIGN.md:183–191 は外部 ESCC 未確認、FB を次段に予定。今回 FB は範囲外なので実機での完了証明はまだできない（F17）。|

直接参照の全所在（`rg -n 'pd_phys|PDE|PTE|page_directory|page_tables|pt\[|pd\[' S/mm/paging.c S/mm/paging_*.inc` の一致を機能別に集約。コメントだけの重複は省略）：

|S 以下のファイル:行|呼び手が知る表の形・操作|
|---|---|
|mm/paging.c:52–63,66–199|キャッシュ属性、定数・配置の static assert、PD と PT ポインタ列。|
|mm/paging.c:233–383|PD 複製・USER 伝播・アプリ帯試験、PD 切替、PDE/PT の直接比較・復元。|
|mm/paging.c:451–601|PTE 権限分類、USER 書換、memmap/master/as audit が全 PDE/PT を走査。|
|mm/paging_boot.inc:5–49,82–147,188–190,228–250|固定 PD/PT の位置と内容検証、1024 要素の生成、PFN→PTE、root ロード、identity 検証。|
|mm/paging_map.inc:12–68,72–118|疎 PT 確保・ゼロ化、PDE 公開、PTE 書込、PDE に USER 伝播、flush。|
|mm/paging_map.inc:125–172,189–226|device map 前検査と書換、SHM/共有ページの PDE/PTE 権限、PCD/PWT。|
|mm/paging_map.inc:233–288|V86 の PD0/PT0 スナップショット・復元、current_root を P2V して PDE 書換。|
|mm/paging_map.inc:291–369|RO/not-present、present/flags/raw の取得と raw 復元。|
|mm/paging_addrspace.inc:15–58|P2V(pd_phys)→PDE→PTE の2段 walk、PRESENT/USER/RW の積、PS 拒否、root API 包装。|
|mm/paging_addrspace.inc:65–135,144–175|PD1ページの確保・全複写・帯の消去・所有検査・解放、私有 PT 確保と PTE/PDE 更新。|
|mm/paging_addrspace.inc:194–238,271–296|範囲 map の PDE ごとの準備と再ロード、帯だけの PTE walk・解放。|
|mm/paging_lease.inc:5–56,59–126,135–167|pd_phys の整列/所有/active、PDE/PT 一致、lease PT 確保・map/unmap、空 PT/PDE の回収。|
- 関連の別翻訳単位にも表形式が露出：S/mm/paging_app.c:12–33,69–119,152–208,215–226 は同じ2段表を直接操作。S/mm/paging.h:21–26 は1024×1024、VA>>22、(VA>>12)&1023、4KiB frame mask。`pd_phys` は単なる識別子ではなく pgalloc 所有の PD ページ。

- mm 外の直接参照（実物を rg で照合）：`S/exec/access_walk.c:14–31`（PD/PT所有・PS・権限）、`exec/lease.c:29–35,171–229`（raw walk/cache/A・D除外）、`exec/ring3_str.c:31–44`、`exec/exec_image_map.inc:14–19,45–57,101–114`、`exec/exec_tramp_init.inc:6–9`、`kernel/v86/v86_mem.c:44–50,77–95,146–154`（low PTE）、`mm/shlib.c:99–106,122–125`（USERと先頭PT）、`kernel/kaudit.c:59–62`。
- raw/cache の外部利用：`S/drivers/video/geforce/gf_wc.c:90–126`、`gf_accept.c:102–105`、`hwdev/gf/native.c:39–41`。kdebug の直接flags/raw操作7本は `kdebug_window.c:15–22,55,78–81`、`kpeek.c:94`、`selftest_exec.c:119–121`、`selftest_lease.c:23–38,55–115`、`kselftest/kcg_display_return.inc:13,34–38`、`gfx_ledger.inc:75–89`、`memmap_pool_user.inc:13,19`。表予算・定数利用は `caller_boot.inc:26`、`h4_backing.inc:212–213`、`ledger.inc:14` にもある。
- root読書きの58参照行（rgは60一致、うち宣言2行を除く。コメント・mm内部を含み、58個の独立した呼び手ではない）：`S/exec/{appmem_map.c:27,redir_access.c:45,87,158,exec_heap.c:161,exec_finish.inc:97–100,247,291,374,exec_launch.inc:49,280,394,exec_syscall.inc:35,lease.c:377–385,v86_call.c:68,104,140–143}`、`drivers/video/{geforce/gf_wc.c:105,trident/resources.c:28}`、`kdebug/{selftest_lease.c:57,100–104,kselftest/caller_boot.inc:8,39}`、mm内部は上の表と `pgalloc_surface.inc:60,98; shlib.c:97,119; paging_app.c:15–16,208; paging.h:240,243,274`。入口への根引渡しは `exec/exec_launch.inc:430,exec_schedule.inc:268,exec.c:518`。
- 操作別の詳細とBの移行範囲は `/home/hight/os32-tmp/run/ppc-mm-boundary/DESIGN.r1.md` §1・§5を参照。移設約2000行に加え実質変更1800～3000行、試験600～1000行であり、r1のmm中心800～1400行では足りない。

## 2. 案の比較と推奨

|案／表とハッシュを繋ぐ層|共有変更量・x86生成物|単一CPU・協調切替で省ける物／費用|
|---|---|---|
|A：rootロードでソフトPD/PT全走査→HPT再構築|mm 0、cpu.h 0も可能。PPC実装だけならx86不変|VSID allocator不要。疎な利用でも全走査し衝突/権限問題は残る。|
|B：mmと全呼び手を意味APIへ分離→PPC pmap|§1の移設2000＋変更1800～3000行、試験600～1000行。x86命令列は最初の同文移設後には変わる|SMP/IPI不要。方針と表形式を分離できるが今回の到達点を超える。|
|C：独立pmapでソフト写像→HPT、新context口|今段mm 0、cpu.h 0、README 0。x86実装0。`ppc_current_context/ppc_select_context` はarch/ppc/ppc_pmap.h内に閉じる|固定16写像・1context、逆写像/ページアウト/overflow/SMP/VSID allocator不要。mm接続は未達。|
|D：PD/PTをソフト表、DSI/ISIでHPTへ遅延挿入|今段mm 0、cpu.h 0、README 0。READMEの4種への訂正はPMの枝 `wt/ppc-readme` で実施済み（取り込み待ち）。ほかのREADME訂正はこの段では不要。PPCのみならx86不変|共通HPT1個とcur、切替ごとに全消去。逆写像/ページアウト/SMP/VSID allocator不要、spill・追出しは必要。|

|比較7点|C|D（実物と費用）|
|---|---|---|
|1 mm/のコンパイルとbit|独立mmuターゲットではmm非リンク、mm変更0。新pmapはPPC属性を内部で生成する。|`S/mm/paging.c:41–48,223–227,512`と4incを**未変更でコンパイル成功**。CR0は `paging_boot.inc:179` のコメントのみ。PRESENTはPDE/PTE両方、RW/USERは階層の積（`paging_addrspace.inc:15–27`）。PCD優先でPCD→I\|G、PWT→WはPCDが無いときだけ、通常RAM→M。W=I=1は使わない（PCD/PWTの両bitを立てるのはgf_wc.c:92だけ。根拠：N/sys/arch/powerpc/include/oea/pte.h:96–108）。PDEのPCD/PWTはx86では表walkのcacheで葉属性ではないため葉とORしない。PSは現コード同様拒否、葉bit7 PATはGF固有（gf_wc.c:11,92–121）で再解釈不可、PPC共通利用から外す。レビュアーは同じ試作で `mm/paging_app.c・pgalloc.c・physmem.c・shlib.c・shm.c・kmalloc.c・memory_boot.c・exec/access_walk.c・exec/ring3_str.c・kernel/kaudit.c` もコンパイルできた（exec/lease.cは未確認）。mm全体のリンク成功や配置互換を意味しない。|
|2 権限・BAT・guard|BAT0内は固定RWのみ。RO/guard試験はBAT外aliasに限定。Cも既存mmのguardを満たしたとは言わない。|256MiB BATのままでは `S/mm/paging_boot.inc:165–172,204` の固定guardも `kernel/sqlite_engine.c:128–131` の動的guardもfaultしない。Dをmmに接続する前にRAM BATを撤去し、コード/stack/表/HPTをソフトPDへ恒等登録する案が必要（縮小だけでは将来の動的guardを保証しない）。x86はCR0.WP=0でsupervisorがROにも書けるため、`!USER→PP0`、`USER+RW→PP2`、`USER+RO→PP1`（N/sys/arch/powerpc/include/oea/pte.h:105、U:RO/S:RW）でx86の意味と一致し、Ks=0/Kp=1の1組で足りる。|
|3 R/CとA/D読者|NP:788–825の無効化後R/C回収をmapping.rcへ保存。mmのA/Dとは結合しない。|A/Dの意味を使う読み手は無く、`S/mm/paging.c:579,589; paging_map.inc:146; paging_lease.inc:24,55; paging_app.c:23,167; exec/lease.c:197,229; kdebug/selftest_lease.c:26,34; kselftest/kcg_display_return.inc:35` は除外比較。`paging_pte_raw/flags`はrawを返すが現読者に参照頻度/dirty判定は無い。ソフトA/Dを更新せず0のままでも `d & ~(A\|D)` は成立。O/sys/arch/powerpc/powerpc/pmap.c:2533–2534の物理頁R/C保存はVM向け、OS32にその消費者は無い；A/D書戻しは不要、将来ページアウト導入時は再設計。|
|4 flush費用|個別変更はNP:788–825の1 VA tlbie、context selectだけNP:548–575の全列。HPT全走査不要。|`S/mm/paging.c:337,474; paging_map.inc:78,118,172,226,260,284,300,316,369` の11箇所で全消去。128MiBならFP:825–842によりHPT=1MiB（131072 entry）；この大きさでは各flushが1MiBゼロ書込＋64 tlbie＋後続の再miss。1MiBは式の値で、PTEGCOUNT（FP:825–826）により64KiB〜への縮小も可能。切替は `exec_launch.inc:280,394; exec_finish.inc:97–100; lease.c:378,385; paging_app.c:208` 等、時分割の毎tickではないがmap/leaseにも掛かる。時間は未測定。C比でspill/追出し/PD fixture・受入に製品約200～350行、試験約150～250行増の見積り。さらに通常trapのframe構築をDR=0で行う約30～50行を加え、C計1500～1800行に対しD計1730～2200行程度。|
|5 手本|FP:196–197,847–867,879–885のソフト写像/HPT、NP:837–868の挿入失敗を採る。有限wired写像なのでoverflowを呼ぶ上位層は不要。|`O/sys/arch/macppc/macppc/locore.S:853–947 → O/sys/arch/powerpc/powerpc/pmap.c:2236–2333`。pte_spill_rは低物理RAMを直接恒等化する分岐を持ち、それ以外はpte_spill_v→VP表walk（同:278–290）。**x86形PD/PTをソフト表として読む手本は無い**。理由はOS32既存表を保持するため。低RAMを無条件RW再生成する分岐はguardを破るのでDでは使えずPD/PT walkに替える必要がある。|
|6 実missの順・満杯|FT:93–158,346–378でreal保存→IR/DR/RI→C。stack/表はBAT内で再帰faultしない。両PTEG満杯はNP:849–868の失敗を返す。|O/sys/arch/macppc/macppc/locore.S:853–892,918–947：DSISR/SRR1のtable-miss判定→物理spill stack→volatile/XER/LR/CTR退避→realのpte_spill_r→成功ならrfi（同:910–918）、不成功は通常trap。`S/include/memmap.h:450–462`のP2Vは恒等で、初期BAT内にPD/PT/HPTを置けるが、**DR=0での表読みにBATは不要**。RAM BAT撤去後も再帰翻訳faultが無いのはspill入口だけ。通常trapはFT:93–158でIR/DRを立ててからstackへ書くため入れ子のDSIが起きうる。Dではframe構築をDR=0で行う変更（約30～50行）が必要。O/sys/arch/powerpc/powerpc/pmap.c:2451–2552はprimary8→secondary8→TB低bitでvictim→pte_del（:842–854、V消去→sync→tlbie→eieio→tlbsync→sync）→R/C保存→新hi無効/lo→eieio→V→sync。Dは満杯停止ではなくこの追出しが必要。Oの:2293–2322のDABR・無効写像・RO store・実行可否判定も対応づけ、失敗なら通常faultへ渡し、保護違反を再挿入だけで再試行し続けない。|
|7 旧契約・新宣言|根はPDではなくHPT/SRを持つcontext。旧rootは未提供、`ppc_current_context/ppc_select_context` を `arch/ppc/ppc_pmap.h` に閉じる（README:38–43、§4）。include/cpu.hに宣言を足さない。|`cur=r; HPT消去; 全TLB消去`、currentはcur、flushはcur不変、enableはIR/DRだけで旧4契約を表せる。SDR1はrootと別の固定ハード状態。新context宣言は不要。PPC内部補助はREADME:38–43に従いppc_名。HPT消去中はEE=0・IR/DR=0の恒等実行区間として再fillを止め、戻すMSRを保存する。|

- コンパイル証拠：`tmp/compile-d/{arch_cpu.h,arch_io.h,platform_io.h,paging.o,compile.log}`。cross-ppc GCC 13.2.0、`build/ppc.mk:12–17`相当のfreestanding/O2/7450/BE32/soft-float/no-altivec、試作includeを先頭にして `-c S/mm/paging.c`。ログ空、readelfはELF32 big-endian PowerPC。外部ppc_*へつなぐ宣言の検証であり、実HPT実装をリンクした結果ではない。nmはlibgcc未参照、既存memset等は未解決のまま。再現用コマンドは `tmp/compile-d/command.sh`。
- Dなら境界設計§2の意味API/transaction導入、§3のPD/PT raw隔離、§5の表形式を隠すためだけの移設・呼び手変更は不要になる。既存prepare/commit・owner/lease方針を保持できる。一方でplatformの地図、BAT/guard、V86・PAT/MTRR・特権入口の分離、PPC ABI修正は残る。B全体が不要と断言はしない。
- **今回はCに決定**。到達点はmm非リンクの例外・変換試験であり、Cは16個のwired写像で足りる。Dのコンパイル上の利点は実証できたが、この段ではその利用者をリンクせず、spill/追出し・RAM BAT撤去の費用が先行する。Dは整数C_KERNEL接続時の第一比較候補とし、Bへの着手を既定にしない。
- Cで書く物の再利用の概算（レビュアー）：そのまま約70%、手直し約20%（vectors.S・ppc_frame.h）、捨てる約10%（ppc_mapping[]・旧rootの仮置き・ppc_memmap.h）。`pmap.c` の `pte_insert / pte_unset` を `ppc_mapping[]` から分けて書く。
- Cの旧API対応：`current_root/load_root`は未定義リンク先、`flush_tlb`はNPの全列（書換はpmap経由）、`enable`はIR/DRのみ。`ppc_select_context` は同contextでもSR/SDR1を設定して全TLB消去、共通BAT不変、EE=0で呼ぶ。SDR1のbase/maskをpd_physに偽装しない。
- Cの単一context、固定16 `ppc_mapping{va,pa,attr,slot,rc}` はOS32試験容量（手本の値ではない）。primary/secondary満杯時は旧写像を保って-1、呼び手は診断停止。置換は先に枠を確保する。

## 3. 手順（手本との対応表）

|順|手本の手順・値|OS32 の対応／相違の理由|
|---|---|---|
|1 OF 利用中|NO:180–187 の TB 周波数・console probe、NF:215–269 の memory reg/available|既存entry.S:38–62でBAT再設定後にOFへ戻る順を維持（NM:703–721、N/sys/arch/macppc/macppc/machdep.c:181–204もBAT初期化→OF処理）。OF setup/ESCC prepare 後、TB 周波数・RAM・ESCC CSR の PA を `pm_boot_info` に保存（CPU core 周波数は使わない）。property 不在/長さ<=0 は手本どおり失敗。propertyエラーはOF stdoutへ出し停止。|
|2 メモリ記録|NF:223–253,277–310：cell 数で reg を解釈、0長を飛ばす、32bit超を飛ばす、MacRISC available は1+1cell|BE cell をホスト fake でも明示 decode。全 RAM ページ数は reg の合計。availableを保存し順2aで配置計算。以後OFの確保/mapは呼ばず、最後にstdoutへのTAKEOVERとdrainだけを行う。64bit値は hi/lo の比較・加算で扱い libgcc を不要にする。|
|2a HPT配置計算|FP:820–842,1995–2029|n=0x1000; while(n<物理ページ数)n<<=1; n>>=1; size=n*64。availableと低256MiBの積から低位予約・ELF/BSS/stackを除外、size整列で切出す。128MiBならn=16384、1MiB、mask=0x3fff。platform が `info.hpt_pa=切出し先; info.hpt_mask=n-1` を格納。不足はOF stdoutへ。TAKEOVERをstdoutへ出してdrain成功後に境界へ。drain失敗はOF stdoutへ出し、境界へ進まない。|
|3 不可逆の境界|N/sys/arch/powerpc/oea/ofw_subr.S:63–105 は復帰用状態保存、NO:191–196 は独自ベクタ後にも OF 利用|OS32 には復帰 trampoline を作らない。`ppc_takeover` に入った時点を境界とし、以後 OF stdout/exit/map を呼べない。境界からenableまではESCCを使わず停止のみ。enable後だけESCCで診断する。手本との相違は「完全切離し」が目的だから。|
|4 real mode|S/arch/ppc/entry.S:38–62、O/sys/arch/macppc/macppc/locore0.S:87|恒等窓内のコード/スタックで EE=0、sync、IR/DR=0、isync。IP=0、FP/VEC=0、PR=0。SRR に依存せず低位で継続。OF ベクタを書き換える間 ME/RI を立てない。|
|5 CPU/BAT|NC:309–331,357–374；NM:703–721|PVR>>16 の7450/7455分岐を維持。7450 は BAT0～3、7455 は HID0 HIGH_BAT_EN/XBSEN の手順後 BAT0～7を消去。lower→upper の NM:517–529 を使い、BAT0 RAM 0..256MiB を再設置。|
|6 HPT初期化|FP:840–842|`ppc_pmap_bootstrap(info)` が順2aの base/mask を所有ctxへコピーし、`(hpt_mask+1)*64` byteを全ゼロ。全mappingとソフトSRも初期化する。配置計算・失敗分岐をここで行わない。|
|7 ベクタ|NM:208–265,469、FT:346–378|MSR.IP=0、0x100単位の低位ベクタをコピー。0x300 DSI、0x400 ISI、0x900 DEC、他は診断停止。低位0..0x3000を予約、0x1000/1100/1200も用意、NM:263–264の0xf20も独立stubを置く（0xf00と重ならない短いbranch）。`mtsprg0 cpu_save_pa`→コピー→命令同期、その直後に `mtmsr (現在MSR \| ME \| RI); isync`。IR/DR/EEはまだ0。|
|8 HPT 挿入|NP:720–731,763–784,837–868|順6で構築したctx.sr（まだハードSRへ未ロード）の VSID、page index との XOR、primary→secondary、H bit 0/1。lo 書込→eieio→hi.V 公開→tlbsync→sync。valid再設定の検査も新ppc-mmuターゲットの `-DDIAGNOSTIC` で残す（現build/ppc.mkには無いので追加）。|
|9 SR/SDR1|FP:685–695|16 SR を mtsrin→sync、SDR1=HPT_PA\|(mask>>10)→isync→全 TLB 消去。VSID は単一 kernel context の16個（FP:883 の EMPTY_SEGMENT+i に対応）。BATは空間共通。|
|10 TLB|NP:552–574|sync；VA=0,0x1000,…,0x3f000 の64回 `tlbie; eieio; sync`；`tlbsync; sync`。同じcontextの再選択も全列。FP:461–463 の ptesync 列とは混ぜず32bit OEA の NP 列を採用。|
|11 変換開始|NM:474–477|順7でME/RI設定済み。enable は sync→mfmsr→IR/DRだけOR→mtmsr→isync。手本との相違は cpu.h:69 の「変換以外に触らない」を守る分離。EE はまだ0。ここから初めてESCCへOWN-VECTORS/OWN-MMUを出せる。|
|12 診断/割込み|NT:1201–1206、NO:335–350、N/sys/arch/powerpc/powerpc/clock.c:148–159|DSI/ISI試験後 `ppc_dec_init(info->tb_hz)` で周期=tb_hz/100 と初回DECを設定、EE=1。handler は未初期化検査、mfdec、負なら周期を加算して非負へ、mtdec、回数加算、最初のN回のみダンプ、以後は計数だけで復帰。周期補正のループも削らない。|

- OF quiesce の分岐も照合する：N/sys/arch/macppc/macppc/machdep.c:140–142 は PowerMac11,2 / 12,1 / 7,2 のみ、NO:186–187で実行。対象QuickSilver/mac99はこの条件外なので呼ばない。OF translations の取り込み（FP:896–937）も完全切離しのため採用しない。
- HPT の配置は保存した available を使い、低256MiBとの積集合から low vectors、ELFの全load/BSS/stackを除外して FP:1995–2029 の整列・区間分割・不足停止を適用する。予約の確定とbase/maskの記録は順2a、ゼロ化は境界後。available取得後は割当系OF呼出を止めるためclaim/再呼出は要らない。HPTをELF内の巨大BSSに置かない。RAM全量の常駐マップは今段不要。
- [C4] `platform/powermac/ppc_layout.h` に低位予約端、BAT窓、試験VA、周期、`PM_DEC_DUMP_N=2`、写像容量、stack/ELF予算を一元化。SPR/MSR/PTEのビットは `arch/ppc/ppc_oea.h`。既存0x6000 ELF予算/0x10000 image予算（S/platform/powermac/ppc_layout.h:38–39）は新独立ターゲット用に見積り直し、リンク ASSERT に反映。
- [C5] `platform/powermac/ppc_memmap.h` を新設：`pm_p2v_ram(pa)`/`pm_v2p_ram(p)` は低256MiBの予約済み RAM 内だけ恒等、`pm_p2v_io(pa)` は下記固定 ESCC alias。pmap lookupは固定mapping16個を検索しoffsetを加算する内部関数（BATなら恒等）、追加の共通APIにはしない。S/include/memmap.h:32–94,450–462 の x86 配置は取り込まない。任意 alias に V2P キャストは禁止；pmap lookup を用いる。
- I/O は DBAT にしない。ESCCの PA を4KiB単位で覆う PTE を `0xf0000000+ページ内offset` に設置（窓が跨げば2枚）、WIMG=I|G=0x28、PP=2。RAM alias は M=0x10、PP=2/3。根拠 NP と N/sys/arch/powerpc/include/oea/pte.h:77–108、FP:399–404。OF の I/O マップは捨てる。ESCC は byte load/store＋eieio（S/arch/ppc/entry.S:82–91）を維持。
- 固定試験 VA は0x40000000（BAT0外）、VAの同じページ内offsetを保つ。RAM PAは予約済み試験ページ。HPT hi=`V | (vsid<<7) | H | ((va>>22)&0x3f)`、hash=`(vsid ^ ((va>>12)&0xffff)) & mask`、secondary=`hash^mask`、lo=`pa | WIMG | PP`。SR は T=0、Ks=0、Kp=1、N=0、VSID=(1<<4)|segment（FP:157 の生成式；単一固定contextはOS32の選択）。
- PTE変更/削除は NP:788–825 をそのまま使う：有効性検査→shadow.V消去→sync→実PTE.V消去→sync→tlbie(VA)→sync→eieio→tlbsync→sync→R/C をshadowへ回収；再挿入は上表8。全消去だけで旧 HPT を残す unmap は禁止。
- 命令同期は N/sys/arch/powerpc/powerpc/ofwreal.S:215–245 の32byteコピー→各line dcbst/icbi→末尾line dcbst/icbi→sync/isync を採る。コピー元/先と長さを32byte整列し末尾lineも予約する。ベクタコピーと試験用実行ページに共用。一般サイズ用 `__syncicache` はこの sparse 指定範囲に本体が無いので流用済みとは書かない。
- HID0 は NC:681–696 の branch-cache列・7450 rev<=2.0 の BTIC解除を残す。NAP/SLEEPを利用する入口は作らない（同:691 QuickSilver hang）。HID1 の MP broadcast 設定、L2/L3再初期化は今段に入れない：単一CPUで、OFのcache構成を維持するため。MSR.POWは立てない。
- 0x1000/1100/1200は手本 NM:251–261/FT:388–590 では603/G2の software TLB miss。7450/7455はこの603用TGPR列を実行させず、共通 fatal stub を置く（CPU限定による相違）。ハッシュmissはDSI/ISIへ。7450を603と判定する分岐は作らない。
- ベクタ入口：FT:348–378 の SPRG1=旧r1、SPRG2=旧LR、SPRG3=vector、SPRG0=単一cpu_saveの物理番地。r28–31、CR、SRR0/1、DSIのDAR/DSISRを real mode で退避してから IR/DR/RI を立てる（FT:93–158）。全GPR0–31、LR/CR/CTR/XER、SRR0/1、DAR/DSISR、vector を32bitフレームへ；FP/VMXは保存しない。
- C handler は恒等 RAM の既存kernel stackに16byte整列frame＋ABI呼出域を積んで呼ぶ。EEを途中で許可しない。戻りは逆順、手本どおりIR/DR/ME/RIを落としてからCR・SRR0/1を戻し、r2/r3を復元してrfi（FT の FRAME_LEAVE:161–241を移植）。手本のユーザーstack切替・SMP CPU lookupは単一supervisorなので固定saveへ置換；未マップstackからの復旧は対象外。SPRG2/3は復帰末尾ではr2/r3の一時保存に再利用（FT:240–241）；入口時の用途を永続状態と誤認しない。
- DSIはDAR/DSISR、ISIはSRR0/1、DECは最初のN回に限りDEC/TB/回数と全frameをESCCへ16進出力。復帰試験はOT:279–295の「修復したfaultは再試行／onfaultでPC等を戻す」に対応：既知の試験VAだけをpmap登録して同じSRR0へrfi、保護違反は試験専用onfault recordへ復帰。無条件SRR0+=4は使わない。未知の例外はdump後停止。
- OF側ベクタ/stack/BATは復元しない。起動後の観測路は QEMU ESCC＋QMP。実機は切離し直前までOF画面、それ以降は実配線が確認されたESCCだけ；現状未確立なのでQEMU合格を実機合格と呼ばない。FBを本段へ黙って追加しない。

## 4. 部品の境目のコード断片

```c
/* arch/ppc/ppc_pmap.h 内だけに宣言する。include/cpu.h は変更しない。 */
struct ppc_mmu_context;  /* 定義は arch 固有。物理根という約束はしない。 */
struct ppc_mmu_context *ppc_current_context(void);
void ppc_select_context(struct ppc_mmu_context *ctx);
/* select: EE=0で呼ぶ。ctxは事前構築済み。旧TLBを全消去し以後ctxで変換。
 * 同じctxでも全消去。共通kernel BATは変えない。EEを変えない。 */
/* cpu.h:30–34 は変更しない。x86_flags.h と ABI_WORD_* を維持。
 * PPCのframeは16byte整列、r3-r10引数は専用入口で扱う。
 * cdecl切出しはユーザ/stack呼出を実装する段へ送る。 */
```
```c
/* 型の宣言は使用前。NF:197–199の32領域＋終端/分割余裕を踏襲。 */
struct pm_region { u32 start, size; };
struct pm_regions { u32 count; struct pm_region v[35]; }; /* reg<=32, avail<=32+分割2+終端 */
struct pm_boot_info {
    u32 tb_hz, escc_pa, hpt_pa, hpt_mask;
    const struct pm_regions *ram, *avail;
};
/* HPT-HANDOFF: platformが順2aでbase/maskを確定。bootstrapは受領・ゼロ化のみ。
 * size=(hpt_mask+1)*64、maskはPTEG数-1（SDR1の符号化済み値ではない）。
 * RESIDENT-HANDOFF: infoと区間配列はmmu_boot.cのstatic、恒等RAMのELF/BSS内。
 * 境界後も存続し読取専用。takeoverは入口のinfoポインタをC呼出を跨いで保存する。
 * mmu.ldのpm_image_start/endが全load/BSS/stackと下記save・試験頁を包含し、
 * platformは順2aの除外に同じ区間を使う。fakeは同名の区間fixtureを与える。 */
extern unsigned char pm_image_start[], pm_image_end[];
/* pmap.cは単一ctx/全写像、platformは区間とOF情報を所有。 */
struct ppc_mapping { u32 va, pa, attr, slot, rc; };
struct ppc_mmu_context { u32 hpt_pa, hpt_mask, sr[16]; };
/* 契約：BAT窓の中のVAを渡さない。呼び手は0x40000000・0xf0000000のBAT外だけを使う。 */
int ppc_pmap_enter(u32 va, u32 pa, u32 attr); /* EE=0, page aligned */
int ppc_pmap_remove(u32 va); /* 空=0、成功はR/C回収・HPT/TLB消去後 */
void ppc_pmap_bootstrap(const struct pm_boot_info *info);
/* PMAP-INIT: bootstrapで単一ctxをcurrentにし、mappingを空に、sr[i]を
 * §3のT/Ks/Kp/N/VSIDで初期化。enterはこのソフトSRを読む。順9のselectで
 * 初めてハードSR/SDR1へ渡す（FP:879–885のソフト初期化と:685–695のロード）。
 * attrはPTE下位語のWIMG|PPのみ、R/Cはpmap所有で初期値0。
 * ppc_oea.hにPPC_ATTR_RAM_RW=0x12、PPC_ATTR_RAM_RO=0x13、PPC_ATTR_IO_RW=0x2aを定義し、
 * caller/fixture共用。値は§3のM/I/G/PPそのもの、共通x86 flagsではない。 */
/* CPU側はOF handleを持たない。区間の複数予約による分割は走査で扱い、配列を増殖しない。 */
struct ppc_vector_image { const void *src; u32 size; }; /* 低位予約全体のimage、32byte整列 */
struct ppc_frame { u32 gpr[32], lr, cr, ctr, xer, srr0, srr1, dar, dsisr, vector; };
/* RESIDENT-HANDOFF: vectors.Sが単一saveとコピー元imageを所有する。
 * ppc_frame.hにこのsave/frameのC型とasm offsetを共置する（FT:93–158）。 */
struct ppc_cpu_save { u32 r28, r29, r30, r31, cr, srr0, srr1, dar, dsisr; };
extern struct ppc_cpu_save ppc_cpu_save; /* 16byte整列、ELF/BSS内 */
extern const struct ppc_vector_image ppc_vectors; /* image末尾line込みでELF内に予約 */
/* takeoverが&ppc_cpu_saveの恒等PAをSPRG0へ、&ppc_vectorsを_lidtへ渡す。 */
void ppc_exception(struct ppc_frame *frame); /* dump/修復/DEC/致命停止のdispatch */
/* ESCC-HANDOFF: escc_paはCSR、data PAは+PM_ESCC_STRIDE(0x10)、
 * 範囲はPM_ESCC_WINDOW(0x20)。既存escc_tree.c:54–89と同じ意味。
 * dump.cがOF handleを持たない送信状態を所有。順8でこの範囲のPTEを挿入し、
 * 順11後だけbindを呼ぶ。bindはaliasを記録するだけでMMIOに触らない。 */
void pm_mmu_console_bind(u32 escc_pa);
void pm_mmu_dump(const char *tag, const struct ppc_frame *frame);
/* dumpは既存escc.c:29–49の期限付きbyte I/Oをaliasで使い、失敗はstopのみ。
 * Aではframe=NULLでmarker、B/Cではframeを渡す。OFへのfallbackは無い。 */
/* DEC-HANDOFF: decrementer.cが周期・初期化状態・累積回数を所有する。
 * BSSの周期0を未初期化とし、AのEE試験は遠い正DECのみ書き、周期は0のまま。
 * initはEE=0で周期=tb_hz/100、回数0、初回mtdec。Cで一度だけ呼ぶ。
 * interruptはclock.c:148–159の未初期化検査・負DEC補正を保つ。 */
void ppc_dec_init(u32 tb_hz);
void ppc_dec_interrupt(struct ppc_frame *frame);
u32 ppc_dec_count(void); /* EE=0で試験側が読む。dump上限はPM_DEC_DUMP_N。 */
/* PROBE-HANDOFF: mmu_probe.Sが2枚の予約RAM頁と試験recordを所有する。
 * 頁は4KiB整列のELF/BSS、実行用コピー元と末尾lineもELF内。
 * recordはEE=0でprobeが設定/解除、exception.cが参照し成功時にactive=0。
 * DSI/ISI修復は指定va/pa/attrをenterし（ISIは§3のcache同期も済ませ）、
 * 同じSRR0へ。RO storeはonfaultへ。未知faultの扱いは変更しない。 */
extern unsigned char ppc_probe_pages[2][4096];
struct ppc_onfault { u32 pc, sp, cr, regs13_31[19]; };
struct ppc_probe_record {
    u32 active, vector, fault_pc, va, pa, attr, action;
    struct ppc_onfault onfault;
};
extern struct ppc_probe_record ppc_probe;
/* actionはppc_frame.hのPPC_PROBE_MAP=1 / PPC_PROBE_ONFAULT=2。
 * onfaultはOT:287–295どおりPC/SP/CR/r13–31を復元、r3=1。
 * 一致条件はvector・fault_pc・対象頁と試験種別（DSIのstore/protection、
 * ISIのmiss）；無関係なfaultを消費しない。recordの固定化はOS32試験専用。 */
void ppc_takeover(const struct pm_boot_info *info); /* 境界、戻らない */
```

|新規ファイル／全原始命令|実装・ABI・この段の状態|
|---|---|
|arch/ppc/arch_io.h：_enable/_disable|mfmsr→EEのみOR/AND→mtmsr→isync、外部asm呼出境界（LTO無し）。NP:516–528、S/arch/ppc/entry.S:96–104に対応。外部IRQは本段では有効化元を設定しない。|
|同：irq_save/irq_restore/_irq_enabled|保存値は不透明 MSR；save は旧MSR取得→EE消去、restoreは現在MSRのEEのみ保存値へ戻す、queryはmfmsrのみ。save中の割込みはmtmsr前なら全復帰後に継続するので禁止区間の始点はmtmsr。1つの外部asm関数内で完結しC操作の移動を防ぐ（LTO無し）。restoreのEE限定は旧値で変換状態を上書きしないOS32契約上の選択、手本NP:528は全MSR復元。|
|同：_lidt|シグネチャは `void _lidt(void *ptr)`、内部で `struct ppc_vector_image *` と解釈して低位へコピー・同期、IP=0選択。EE=0/IR=DR=0でtakeoverから1回のみ。x86の6byte IDTRは受けない（include/io.h:104–106は形をarchへ委任）。|
|同：_halt/_idle|今段は未実装、両者とも未定義の `ppc_wait_not_available()` を参照して呼べばリンク失敗。EEを立ててからPOWにする実装は不可分性を満たさない。NAP不採用根拠NC:691。DEC試験は通常のbusy loopで待つ。|
|同：_stop|EE消去→sync→自己分岐。S/arch/ppc/entry.S:105–111を利用。呼び手のfor(;;)契約を変更しない。|
|arch/ppc/arch_cpu.h：current_root/load_root|旧シグネチャのinline定義は置くが、本文は未定義 `ppc_legacy_root_not_available` へ。PPC呼び手はppc_pmap.hの `ppc_current_context/ppc_select_context` だけ使う。無言でSDR1をrootと返さない。|
|同：flush_tlb/enable|§3のNP全列／IR・DRのみ変更。外部asm呼出境界。context設定済みは呼び手の前提。|
|同：arch_enter_user/arch_call_on_stack|同引数マクロを定義し未定義関数参照で呼出時リンク失敗。後者の「全引数がstack上」はPPC ABIと不一致、将来専用ABI入口が要る。noreturn等を旧共通契約へ足さない。|
|platform/powermac/platform_io.h：inp/outp|引数はMMIO仮想番地、lbz/stb→eieio、結果はzero extend。PC98ポート番号は解釈しない。|
|同：inpw/outpw/inpd/outpd|今回は未定義リンク先へ。後段のPCI little-endian窓用 lhbrx/sthbrx/lwbrx/stwbrx＋eieioを別機能で実装する。ESCC以外のレジスタ幅/byte orderを推測して実装しない。|
|同：io_wait/io_wait_n|今回は未定義リンク先へ。eieio はバス順序であり include/io.h:62–69 の「短いウェイト」の代替にはしない。既存ESCCの期限付きpollを使用。|
- Cの原始操作は外部関数を呼び、host fakeが同名を提供する（手本S/arch/ppc/ppc_boot.h:10–16）：`ppc_load8/store8, ppc_sync, ppc_disable/enable/stop, ppc_irq_save/restore/enabled, ppc_vector_install, ppc_tlb_flush_all/one, ppc_sr_sdr1_load, ppc_enable_translation, ppc_cache_copy, ppc_read_pvr/hid0, ppc_write_hid0, ppc_read_dec/tb, ppc_write_dec, ppc_bat_reset/set, ppc_eieio/tlbsync/isync, ppc_hpt_ptr(pa)`。Cは `ppc_hpt_ptr(pa)` 経由でHPTへ触り、実機側はHPTの恒等アドレス、fake側は試験用領域を返す。build/ppc.mk:18の-I順では同名ヘッダを差し替えられないため、同名関数のリンク差し替えを用いる。CのCPU分岐は記録可能な外部関数経由、inline PPC asmを含むヘッダをfakeへ渡さない。takeover/frameの組立自体はfakeで代用せずobjdump＋QMPで検証。
- FAKE-ABI：上の原始操作は `ppc_oea.h` のC専用宣言（asmは定数部のみ）を製品/fakeで共有する。既存 `ppc_boot.h` の `unsigned int ppc_load8(cell)` / `void ppc_store8(cell,unsigned int)` / `ppc_stop(void)` のnoreturnを保持する。追加分の形は次のとおり。HPTは実機BE/fake nativeのu32語として扱い、fixtureも同じ語で比較する（OF propertyだけはBE byte decode）。TBは診断用下位32bitで、64bit戻り値ABI/libgccを導入しない。
```c
void ppc_enable(void);
u32 ppc_irq_save(void);
void ppc_irq_restore(u32 saved_msr);
int ppc_irq_enabled(void);
void ppc_vector_install(const struct ppc_vector_image *image);
void ppc_tlb_flush_all(void);
void ppc_tlb_flush_one(u32 va);
void ppc_sr_sdr1_load(const u32 sr[16], u32 sdr1); /* 符号化済みSDR1 */
void ppc_enable_translation(void);
void ppc_cache_copy(void *dst, const void *src, u32 size);
u32 ppc_read_pvr(void);
u32 ppc_read_hid0(void);
void ppc_write_hid0(u32 value);
int ppc_read_dec(void); /* signed 32bit */
u32 ppc_read_tb(void); /* TBL */
void ppc_write_dec(u32 value);
void ppc_bat_reset(u32 count); /* I/D両方、CPU分岐後の4または8 */
void ppc_bat_set(u32 index, u32 lower, u32 upper); /* I/D共通RAM、lower→upper */
void ppc_eieio(void);
void ppc_tlbsync(void);
void ppc_isync(void);
volatile u32 *ppc_hpt_ptr(u32 pa); /* 語順はentryごとhi,lo、native u32 */
```
- この段の未定義参照は「未実装を呼んだビルド」を拒否する境界で、実行時の新しい検査ではない。新規関数名はREADME:38–43に従いppc_/pm_。

## 5. 部品の大きさの見積もり

- 製品新規約1500～1800行、既存PPC変更約60～100行、試験約400～550行。BSD許諾文は別途各由来を保持（コードを抜くとき著作権・条項を省かない）。
- 内訳：OFメモリ/TB取得180、takeover/CPU設定120、pmap/HPT250、vector/frame250、C exception/DEC160、ヘッダ/MMIO/診断150。最初は最適化や全RAM PTE化をしない。
- 常駐データは既存8KiB stack、単一save/frameと16写像で1KiB未満、区間配列、試験用4KiB×2、HPT（128MiBなら1MiB）、低位予約12KiB。コード/BSS予算はリンクmapで実装時判定し、HPTはfile sizeへ算入しない。

## 6. ファイルごとの変更一覧

|PPC独立枝（S以下）|新規/変更・目安行数／1機能|
|---|---|
|platform/powermac/mmu_boot.c|新180：OF情報の取得とhandoff、既存boot.cの診断停止までの経路は旧ターゲットとして保持|
|platform/powermac/ppc_memmap.h / ppc_layout.h|新35／変更20：RAM/MMIO変換／番地定数|
|arch/ppc/takeover.S / cpu_setup.c|新80／新60：不可逆入口／PVRとHID設定|
|arch/ppc/pmap.c / ppc_pmap.h|新250／新35：写像とHPT／型宣言|
|arch/ppc/mmu.S / ppc_oea.h|新90／新70：SR/SDR1/TLB／OEA定数|
|arch/ppc/vectors.S / ppc_frame.h|新250／新35：保存復帰とcopy／frame offset共有|
|arch/ppc/exception.c / decrementer.c|新85／新55：fault診断／DEC再装填|
|arch/ppc/cache_copy.S|新45：手本の32byte copy/sync|
|arch/ppc/arch_io.h / arch_cpu.h|新65／新40：契約への薄い接続|
|platform/powermac/platform_io.h / dump.c|新45／新60：MMIO接続／整数16進dump|
|arch/ppc/entry.S / mmu.ld / build/ppc.mk|変更5／新45／変更40：新入口選択／配置／独立ppc-mmuターゲット（旧bootは維持）|
|tools/ppc/mmu_fake.c / mmu_fixture.h|新180／新80：libcなしfake／BSD順序の固定fixture|
|tools/ppc/mmu_run.py / arch/ppc/mmu_probe.S|新160／新90：QEMU/QMP受入／faultとレジスタcanary|

## 7. 受入・変異・段分け

- A=OF/切離し/vector＋順8〜11（ESCCのPTE1枚、SR/SDR1、TLB、変換開始）、B=満杯・追加写像・DSI/ISI、C=DEC/復帰の順。AのQEMU観測はESCCのPTE1枚を設置しenableまで通してから行う。旧ppc-bootは各段で維持、mac99 `-cpu 7450 -m 128M -smp 1`、ESCC両ログ/QMP、親期限30秒。起草時はDの単体compile以外未実行、レビュー掲載の実測はレビュアーの結果であり本書の実測ではない。
- host fakeは `S/build/ppc.mk:18–19` の32bit freestanding、自前_start/exit、qemu-i386、BE cell byte fixture。C製品を同名の原始操作fakeと `ppc_hpt_ptr(pa)` fakeへリンクし、CからHPTへのアクセスも後者へ通す。命令列はイベント＋objdump、翻訳・復帰はQMP/ESCCで見る。cacheはTCGだけで証明しない。

|段A（7項目）：観測判定|変異1箇所|
|---|---|
|切離し：正常時TAKEOVERが最後のOF出力、drain失敗時はOF stdoutへ出し境界へ進まない、境界後CI=0、境界後〜enable前のESCC=0、以後OWN-VECTORS/OWN-MMU|境界後failをOFへ戻す→fake順違反|
|reg/available：複数区間/0長/32bit超/MacRISC、整列分割/不足、HPT base/maskをdump/QMP照合。不足はTAKEOVER前OF失敗|nの最後の>>1を削る→128MiB時1MiB期待RED|
|7450/7455：PVR別SPR列、7450はBAT4以上無し、rev別HID0、MSR.IP=0/RI=1をQMPで確認|7450にもHIGHBAT設定→fake列不一致|
|vectors：0xf20を含む配置とSPRG0、コピー末尾dcbst/icbi/sync/isyncをobjdump/fake照合|末尾icbi削除→静的列RED|
|mmu_probeで_enable/_disable/irq_save/restore/_irq_enabledの各呼出後EE、_lidtのvector設置、inp/outpのESCC読書を照合。DEC設定前EE試験中はDECを遠い正値へ|irq_restoreを常時EE=1へ→保存EE=0復元ケースRED|
|順8〜11：ESCCのPTE1枚を挿入、SR/SDR1設定・変換開始後にESCC読書とOWN-VECTORS/OWN-MMUを確認|ESCC PTEのV公開を省略→fake列/ESCC出力RED|
|TLB：sync、64回tlbie/eieio/sync、tlbsync/sync、PTE無効化→R/C回収→再公開の列をfake/objdumpで照合|tlbsync削除→列RED|

|段B（4項目）：観測判定|変異1箇所|
|---|---|
|HPT：BAT外aliasの読書、primary8満杯でsecondary成功、両方満杯は-1かつ旧写像不変。fixtureがPTEG16枠を直接埋めるのでmapping16枠の制限に隠れない|secondaryのHを落とす→fixture/アクセスRED|
|DSI：DAR/DSISR、未写像alias追加で同じSRR0再試行、RO storeはonfault復帰|DARを別VAへ→期待診断/修復不一致|
|ISI：実行aliasでfault、写像追加・cache同期後に既知return値|復帰SRR0を次命令へ→戻り値/canary RED|
|unmap/remap/select：Aを温め→Bへ変更してB値、unmap後DSI、同context再選択後も正値|PTE.V消去省略→fakeと再アクセスRED|

|段C（5項目）：観測判定|変異1箇所|
|---|---|
|frame：DSI/ISI/DECのGPR全32/CR/LR/CTR/XER・SRR canaryとmain復帰marker|r31復元slot誤り→canary RED|
|DEC：N=2回dump、その後計数のみ、N+2回以上を確認、負DECへの周期加算・再装填、QMP EE/IR/DR/RI=1|mtdec削除→fake再装填列RED、回数と期限も確認|
|未提供API：_idle/_halt/旧root/user/stack/wordIO/wait呼出はリンク失敗、本体未定義symbol0|未定義呼出をno-opへ→誤リンク成功RED|
|停止：enable後未知例外はFATAL/frame、QMP PC=停止label・EE=0、cont/stop後も同じ。境界直後failは無出力停止|停止先変更→PC不一致|
|退行：旧ppc-boot、libgcc/newlib未参照|旧ppc-bootの既知出力markerを変更→起動ログ照合RED|

- ME設定はobjdumpで確認する。レビュアーのQEMU実測では `mtmsr 0x1032` 後のmfmsrが0x32となりMEが立たないため、QMPではME=1を受入条件にせずRIを確認する。
- 変異アンカーは一意、動作試験ではbuild失敗をREDと数えずGREEN→実行RED→復元GREEN。未提供APIのリンク拒否は専用の静的判定。変更テストの--mutateとlintを実装段で実行。新規防御を製品へ足さず、手本のproperty/範囲/PTE-valid/満杯/DEC未初期化分岐をfixtureに残す。

## 8. この先の見取り図

- 整数 C_KERNEL：libのendian/非整列/可変引数/longjmpを監査、C/Dを再比較して地図・guard・保護を確定してからリンク。Dなら既存PD/PTを保持する道があるためBを前提にしない。newlib/libgccは持ち込まない。
- KAPIは0xc00のsc入口、番号/引数/戻りのPPC ABIを定める。今回のfatal stubから置換。x86 intの番号やstackを流用しない。
- ユーザーモード：context切替、PR/EE同時復帰、kernel stack記録、ユーザーVA保護、FP/VMX所有と保存を別段で設計。単一アプリでもsupervisor BATとユーザーの分離は必要。
- OpenPIC：macioから発見、controller初期化・mask/EOI、0x500入口を接続。DECだけの現在の試験と混ぜない。
- FB直描き：OF終了前に framebuffer PA/stride/depthを保存し独自WIMG写像、描画後同期。実機でOF後の観測路を確立してF17を閉じる。
- `_idle` は実機で安全な待機方式が確定してから契約を満たす実装へ。未定義呼出を便宜的なenable/halt列で埋めない。

## 9. 未決

- ユーザー判断が要る未決は0件。今回はC案に決定、整数C_KERNEL接続時にDを再比較。実機観測路とcacheの実測は後段の作業であり、今回の範囲を広げる承認待ちにはしない。
- 順の変更：`S/docs/decisions/ppc/PPC_BOOT_DESIGN.md:183–191` のFB先行から、今回QEMU MMU先行へ変更する提案。理由は今回の範囲が変換/例外で実機ESCC未確認のため；TAKEOVER以後の実機の沈黙を成功としない。FBは本段に追加しない。
- `S/docs/tasks/DEFERRED_TESTS.md` への追記案（実装枝）：`PPC-MMU-RH | QuickSilver 7450/7455でTAKEOVER→独自変換・DSI/ISI・DEC・復帰、HID0/BAT・cache同期を確認 | OF後のESCC実配線/FB観測路未確立、実機未測定 | PPC-BOOT-RH完了かつESCC確認またはFB独立写像が使える時に実施`。この起草では同ファイルを編集しない。

## 確定 (2026-11-07、Opus 5.5 第 2 回の条件付き Approve を反映)

- PM：Cに決定、context関数をPPC内へ閉じ、共有枝と共有変更の工程・受入を削除。
- R1：ME設定はobjdumpで確認し、QMP判定からME=1を除外。
- R2：段Aに順8〜11とESCC PTE1枚を含め、段Bを満杯・追加写像・DSI/ISIへ限定。
- R3：ppc_hpt_ptr(pa)を同名fakeの差し替え対象へ追加し、CのHPTアクセスを集約。
- R4：BAT内RO/remove拒否の検査・受入を削除し、BAT内VAを渡さない契約を明記。
- R5：CR0.WP=0のx86権限に対応するPP0/PP2/PP1とKs=0/Kp=1を明記。
- R6：HPTの1MiBは式の値で、PTEGCOUNTによる64KiB〜への縮小が可能と併記。
- R7：再帰fault無しをspill入口に限定し、DのDR=0 frame構築約30〜50行を加算。
- R8：PCD優先、PWTはPCD無しの場合だけとし、W=I=1を使わない。
- R9：ESCC無出力区間を境界後〜enable前へ限定し、drain失敗はOF診断して境界へ進まない。
- IDなし（追加コンパイル）：レビュアーの追加10ファイルのコンパイル結果と未確認・非保証範囲を追記。
- IDなし（再利用概算）：C成果の70%/20%/10%の再利用概算とpte_insert/pte_unsetの分離を追記。

- 段 A の前の追記 HPT-HANDOFF：境界前にpm_boot_infoへHPT base/maskを格納し、境界後は受領・ゼロ化のみ。
- 段 A の前の追記 RESIDENT-HANDOFF：boot情報・予約区間・vector/save・試験頁の所有者、宣言と恒等RAM上の寿命を明記。
- 段 A の前の追記 PMAP-INIT：順8に先行するソフトSR/current初期化とattrの共有定数・意味を確定。
- 段 A の前の追記 ESCC-HANDOFF：CSR PAの意味、alias送信状態の所有者とenable後のbind/dump口を追加。
- 段 A の前の追記 DEC-HANDOFF：TB周波数からDEC周期へ渡すinit口、handler/計数口、A/Cの初期化状態を明記。
- 段 A の前の追記 PROBE-HANDOFF：B/Cのfault修復・onfaultの共有recordと所有者・復帰内容を宣言。
- 段 A の前の追記 FAKE-ABI：同名差し替えの型・引数・HPT語表現・TB幅を共有宣言として確定。
