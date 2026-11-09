# PPC_KBOOT_DESIGN — PowerPC で OS32 本体を QEMU mac99 の到達点 ② まで通す

> 状態: **現行 (2026-11-10)** — OF から入り、OF 切り離し・案 D の HPT・例外・MPIC・1kHz の decrementer が動き、カーネルの初期化を core-ready 境界まで通して起動ログを出すまでの段と境目。段の状態は票 TASK_PPC_KBOOT が持つ。

> 出所: ~/os32-tmp/run/ppc-kboot/DESIGN.final.md (gpt-6-astra 起草 r1→r2、Opus 5.5 第 2 回で承認、PM が N1・N2 を反映)。


## r1→r2 の変更
- R1: geometryを15/15/16/16/16MiBに変更し、system除外・恒等写像・ledger一致をB3受入に追加。
- R2: ESCC/MPIC aliasを静的aperture PT内の固定offsetに置き、ledger非経由とgfx衝突回避を規定。
- R3: HPT恒等PTEを廃止し、既存恒等写像からも除外。
- R4: 0x500 dispatch/fatalのMMIOをframe完成後DR=1のI/G alias経由に限定。
- R5: kboot profileをinit起点のsymbol閉包＋明示追加TUと定義し、H1の分類対象を限定。
- R6: G2直接console providerとH1専用providerで未着分離段への待ちを除去。
- R7: H3のPIT_HZ利用者不在監査と③前のtick単位負債を追加。
- R8: MPIC GCR/IPI送信offset・IPI priority/unmask手順を追加。
- R9: OF自己申告＋host変異、BAT静的SPR列＋guard負像へ証拠の種類を明記。
- R10: detect→paging→after_paging→memory_boot_init→shm→kmallocへ修正。
- R11: 依存を守る並列の波を明記。
- R12: HPTを128KiB整列/容量、SDR1 HTABMASK=1へ縮小。

- 基点 `cfb9cec3`、PACK の決定を固定。最短経路は「整数カーネル＋共通 mm の実体＋ESCC＋HPT＋例外＋MPIC＋1kHz DEC」。②受入で x86 本線実装を再開する。
- 本票は設計のみ。リポジトリ編集・preflight（--fix-fonts を含む）・ビルド・試験は PACK の禁止により実施していない。数値は見積もり、実走結果ではない。
- 以下 S=このリポジトリ、O/N/F=`/home/hight/os32-tmp/ref/{openbsd,netbsd,freebsd}`。BSD の版は `docs/ppc/README.md:32` の固定版。メーカー本文は同 `ref/vendor/text/`、索引は `vendor/SOURCES.md`。GPL ソース未参照。

## 1. 最初の確認：実物・前提・ずれ
- 指定の `git log --oneline -60 -- arch/ppc platform/powermac build/ppc.mk build/targets build/arch build/platform include/platform_*.h init tools/ppc` と HEAD を確認した。
- 入った段：boot `91977906`、MMU `93574b04/2d4b7b28`、分離1 `03469ffd`、分離2 B/C/D `3c177bbe/21b7c595/0f9e7727`、F2/G `0c324c99/48630a6f`、分離3 C1/C2/D1 `308a64d6/4a41ccee/325b9473`、分離4 A `865acf2d`、分離5 A/E/F `28b80560/079f2101`。
|照合対象|実物 file:line|判定|
|---|---|---|
|単体像・OF切離し|S/build/ppc.mk:51、arch/ppc/takeover.S:6、platform/powermac/mmu_boot.c:233|PACK と一致。既存成果の受入を再実施した意味ではない|
|案C・RAM BAT|S/platform/powermac/ppc_layout.h:71、arch/ppc/cpu_setup.c:47、pmap.c:132|16写像＋256MiB RAM BAT。D接続済みではない、前提と一致|
|旧計画の起動直接依存|S/docs/tasks/ppc/TASK_PPC_SEP.md:22 対 S/init/kernel_boot_cpu.inc:28,34,38,44,50,55,63|**食い違い1**：GDT/TSS/IDT/PIC/PITの直呼びは既に arch/platform 口の後ろ。順(2)の分離を再設計しない|
|旧計画のIRQ16線前提|同 TASK:22 対 S/include/platform_irq.h:5–16、kernel/irq/irq.c:37|**食い違い2**：線数・claim/completeは既に機種契約。PPC limits:4 は暫定1線、PCI bitmap拡張は別途本線待ち|
|時計の口|S/include/clock_source.h:4–8、kernel/time/ktime.c:38、tick.c:3|共通clampとtick_countはある。PPC DEC:35は100Hzの単体計数で本体tickではない|
|bootinfo|S/include/bootinfo.h:8–11、platform/powermac/boot_capture.c:4|native image口は既存、RAM表は無い。PPC暫定ゼロ情報を正式captureへ替える|
|メモリprovider|S/platform/powermac/platform_memory_desc.h:1–14、mm/memory_boot.c:245|暫定値実走禁止は前提と一致。OFサイズをmem_kbへ丸投げするだけでは不足|
|trap/frame/exec|S/arch/ppc/ppc_frame.h:3–19、vectors.S:309–315、include/exec_arch.h:6–37|176B native frame とopaque exec口はある。frame構築前DR=1、PPC exec provider未提供。第2段は共通fault handlerまでは作っていない|
|全リンク・console|S/build/targets/ppc-powermac.mk:4–9、arch/ppc/arch_cpu.h:4–12、arch_io.h:10–12|compile-only、root/stack/idle未実装。第5段display/console provider着後もESCC製品実装は別途必要|
- 食い違いは古い TASK の代表記述との2件。PACK の既存成果・未着段の前提との矛盾は0件。未実装の存在を「食い違い」に重複計上しない。

## 2. ②の製品範囲と合否
- kboot profile は `init/ppc_kernel.c` から到達するsymbolの閉包＋明示した追加TU（takeover・vector・spill・CPU初期化のASM/C入口）と定義し、H3のsource manifest/link mapで列挙、未解決symbol無しでリンクする。単体 `PPCMMU.ELF` は残す。通常のselected全TU計測集合を減らして成功数を上げない。
- kboot の init は新 `init/ppc_kernel.c` をsource選択する。実体は共通 paging/physmem/pgalloc/ledger/kmalloc/shm、kstring/kprintf、時刻、IRQ。VFS/root mount/exec_init/module読込みはcore-readyの外。閉包内の未対応依存はH1のkboot専用providerで閉じる。
- PC98 の `init/kernel.c:123–286` を丸ごと呼ばない。盤面・入力・PCI探索・root mount・GUI/IME・module/shlib読込み・`exec_init()`のUSER trampoline生成は今回の起動経路に含めない。全リンクとはOS32全部のfeatureを有効にする意味ではない。
- 「ユーザーランドの手前」は、実ページ管理がonline、heap/shared領域が初期化済み、時刻と割込みが継続し、起動ログが流れ、次がユーザー実行準備となる **PPC core-ready 境界** と定義する。未提供サービスを成功に偽装した `services=READY` は出さない。
- PM推奨：OF FBは②の条件にせず後段。終了はEE=1のbusy idle（POW/nap無し）を推奨。即停止より継続tickと外部割込み復帰を観測でき、G4省電力の設計を増やさない。
```sh
qemu-system-ppc -M mac99 -cpu 7450 -m 128M -smp 1 -accel tcg \
  -bios /usr/share/qemu/openbios-ppc -cdrom kboot.iso -boot d \
  -prom-env 'boot-device=cd:,\PPCKERN.ELF' -prom-env 'auto-boot?=true' \
  -nographic -serial file:a.log -serial file:b.log -monitor none -qmp stdio
```
- 新 `tools/ppc/kboot_run.py`。run.py:175/mmu_run.py:32のISO・QMP stdio方式を再利用。ROMは実行用バイナリのみ。toolchain/QEMU版、ROM/ELF hash、link map、コマンド、raw A/Bログを保存する。
- 起動から60秒で次の順序を要求（可変値をparse、各節目1回、FAIL/FATAL/UNSUPPORTED無し）。最終行を固定印字するだけでは不合格。
```text
KBOOT OF-CAPTURE ram=<bytes> admitted=<bytes> tb_hz=<hz>
KBOOT OF-OFF calls=<n>
KBOOT MMU-D root=<pa> hpt=<pa> bytes=<n> ram_bat=0
KBOOT MEMORY online=1 heap=1 shm=1
KBOOT TRAP dsi=pass isi=pass guard=pass flush=pass
KBOOT MPIC sources=<n> priority=0 cpu=0 delivery=pass
KBOOT CLOCK hz=1000 tick=<n> us=<n>
KBOOT READY stage=pre-user
KBOOT ALIVE tick_delta=<n> tb_us=<n> of_calls_delta=0
```
- READY後ゲストTBで2秒以上、tick増分とTB差を±5%で照合、QMPでも実行継続を確認して台本がquit。STOP命令地点到達だけでは合格にしない。7455でも同じ判定（別起動、各60秒）。壁時計TCG速度を1kHz精度の基準にしない。
- fault/未対応口の負試験は別像・各30秒、所定のFATALと停止PCを要求。compile失敗・QEMU起動不能・timeoutだけを変異REDに数えない。

## 3. 段の一覧＝ファイル別変更台帳
- 各行0.5〜1人日、主ファイル1本を基本とする。＋新規、Δ変更行概算（試験は末尾に分離）。P=PPCだけ、I=x86不変目標。共有変更はbuild_compare、差が出ればPMのNP21/W受入が終わるまで閉じない。
- 依存の無い枝は並列可能、同じファイルの変更は直列。分離3 E2/E3/F、4 D/E/F、5 B1〜B6/C1〜C3/D1/D2/S1/S2/Zの既設設計は取り直さず、未着の段はkboot専用providerで迂回する。
|段|依存・成果|主ファイルと付随差分（行）|区分|
|---|---|---|---|
|A1 link配置|独立、kernelの固定配置とsymbol契約|arch/ppc/kernel.ld ＋100、platform/powermac/kboot_layout.h ＋45|P|
|A2 entry|A1、OF入口・32KiB stack・BSS|arch/ppc/kernel_entry.S ＋100、kernel_note.S ＋35|P|
|B1 OF snapshot|独立、memory/available/clock/ESCC/MPIC記録|platform/powermac/kboot_capture.c ＋180、kboot_info.h ＋65|P|
|B2 OF配置|A1/B1、固定領域検証とHPT切出し|platform/powermac/kboot_reserve.c ＋160|P|
|B3 mm geometry|B2、15–16MiB除外・ledger/恒等写像一致の受入|platform/powermac/platform_memory_desc.h Δ25、memory.c ＋65、boot_capture.c Δ15|P|
|C1 D walk|独立、ソフトPD/PT権限/属性の変換|arch/ppc/pd_walk.c ＋140、ppc_pd.h ＋45|P|
|C2 HPT|C1、一次/二次/victimと全消去|arch/ppc/kernel_hpt.c ＋200|P|
|C3 root/flush|C2、real区間・現在root・4契約|arch/ppc/kernel_mmu.S ＋130、arch_cpu.h Δ20|P|
|C4 CPU|A2、PVR/HID0/SR/SDR1/BAT引渡し|arch/ppc/kernel_cpu.c ＋90、ppc_oea.h Δ12|P|
|D1 frame|C1、DR=0保存＋spill入口|arch/ppc/kernel_vectors.S ＋約430（既存vectorsの大半再利用）、ppc_frame.h Δ20|P|
|D2 trap|D1/C2、fault分類とfatal|arch/ppc/kernel_trap.c ＋120、ppc_trap.h ＋30|P|
|D3 boot口|C3/C4/D1/B3、takeover→paging|arch/ppc/kernel_boot.c ＋130、kernel_takeover.S ＋45|P|
|E1 MPIC発見|B1、OFバス窓を解決|platform/powermac/mpic_tree.c ＋120|P|
|E2 MPIC駆動|E1/D2、mask/priority/IACK/EOI|platform/powermac/mpic.c ＋190、mpic.h ＋55|P|
|E3 IRQ adapter|E2、本線bitmap着後なら接続|platform/powermac/irq.c ＋95、platform_irq_limits.h Δ8|P|
|F1 時計provider|B1、TB読み・周波数・us|platform/powermac/clock.c ＋110、kboot_info.h Δ5|P|
|F2 DEC|F1/D2、1000Hz再装填とtick|arch/ppc/kernel_dec.c ＋85、ppc_trap.h Δ8|P|
|G1 log|独立、bounded ESCC＋後続sink|platform/powermac/log.c ＋110、kboot_log.h ＋25|P|
|G2 console provider|G1、console TUを除外し直接互換口を提供|platform/powermac/kboot_console.c ＋120|P|
|H1 非対応provider|D3/E3/G2、閉包の未解決のみ分類・専用providerで充足|arch/ppc/kboot_unavailable.c ＋120、arch_exec_storage.h ＋25、arch_io.h Δ8|P|
|H2 init|B3/D3/E3/F2/G2/H1、実mm初期化|init/ppc_kernel.c ＋150|P|
|H3 本体graph|H2、段階link・未解決/PIT_HZ利用者監査|build/targets/ppc-powermac.mk Δ40、build/arch/ppc-sources.mk Δ20、build/platform/powermac.mk Δ20、build/ppc-kboot.mk ＋100|I|
|T1 host|各部品着後、入力/更新列/順序|tools/ppc/kboot_host.c ＋280、kboot_model_host.c ＋180、kboot_host.py ＋100|I|
|T2 guest|C〜H後、実fault・root・IRQ・tick|arch/ppc/kboot_probe.S ＋150、platform/powermac/kboot_probe.c ＋160|P|
|T3 acceptance|T1/T2/H3、二CPU＋負試験|tools/ppc/kboot_run.py ＋240、kboot_mutations.py ＋100|I|
|T4 回帰・記録|全段後|tools/checks/ppc-kboot-host.py ＋25（CHECK inputs明記）、tools/ppc/check_compile.py Δ15、selected_success.json/known_header_dependencies.json 各実測差分、arch/README.md Δ20、docs/tasks/ppc/TASK_PPC_SEP.md Δ20|I|
- 並列の波：{A1,B1,C1,G1}→{A2,B2,C2,D1,E1,F1}→{B3,C3,C4,D2,E2,F2,G2}→{D3,E3,H1}→H2→H3→T（各波内も表の依存順を守る。H1の最終分類はD3/E3後）。分離3 F・4 F・5 C1〜C3/Zの着地を待たない。E3は②専用外部入口にE2を接続できる（§6）。段Pは②直後GMACの前提で、②の待ち条件ではない。
- 見積もり計：製品約3,000〜3,700行（ベクタ再利用約350行込み）、試験約1,200行、文書/選択約200行。D単独の旧1,730〜2,200行には本体link/OF予約/console/MPICは含まれない。1人直列で約15〜24人日。未着分離段の待ち時間は②のcritical pathに含めない。

## 4. OF → init とメモリの境界
```c
/* platform/powermac/kboot_info.h、native private、PC98 wireとは共有しない */
struct pm_kboot_info {
    struct pm_regions ram, available; /* BEセルをdecode済み、静的所有 */
    u32 admitted_end, image_first, image_end, hpt_pa, hpt_bytes;
    u32 tb_hz, escc_pa, mpic_pa, mpic_bytes;
    u32 of_calls_at_detach;
};
const struct pm_kboot_info *pm_kboot_capture(ppc_ci ci);
void pm_kernel_takeover(const struct pm_kboot_info *) __attribute__((noreturn));
void ppc_kernel_main(void) __attribute__((noreturn));
```
- 所有：snapshot/予約表はplatformのBSSにコピー、OFのproperty領域への借用pointerを残さない。`boot_common_set` は従来のimage情報だけ、source=NONE、CRCを実際に検証していなければimage_valid=0。x86のwire/SDK ABIを拡張しない。
- 順：OF入口r5保存→専用32KiB stack/BSS→OF setup/ESCC→RAM reg/available＋CPU TB＋MPIC tree→固定配置検証→必要な予約→最終available再取得→TAKEOVER出力/drain→OF呼出禁止フラグ→real takeover。
- regは総RAM、availableは利用可能RAM。セル数・MacRISCのavailable特例は `mmu_boot.c:158,245–249` を再利用、長さ不整合/overflow/32bit外を黙って利用しない。複数区間は正規化、上限超過は診断停止。
- **最短の②は管理RAMを16MiBまでに制限**（QEMU搭載128MiBは維持）。S/mm/physmem.c:90–115 の既存legacy modelを安全に使うため、image所有領域を除く `[1MiB,15MiB)` (legacy_end=15MiB、arena と末尾 metadata もこの中) がregかつ最終availableで覆われることを必須にする。穴があれば停止し、総容量を連続RAMとみなさない。将来の全RAM利用は別段。
- kernel/heap/KAPI/SHM/固定PD/PT/stack/5MiB以上のarena/末尾metadataをこの検証窓へ収める。低位0..1MiBはallocatorへ渡さず、`paging_reclaim_conventional()`を呼ばない。vector専用0..0x3000はarchitected使用で別予約。
- PPC geometryは `{15MiB,15MiB,16MiB,16MiB,16MiB}`、high probe=0。native_ucは空、system=[15MiB,16MiB)の1MiBを捨てる。bios_rom/low_deviceは互換固定予約 `[0xA0000,1MiB)`（実ROM/機器と報告しない）。B3受入はpaging_initの恒等写像からsystemが除外され、ledgerのBACKGROUND予約とphysmem除外が同区間で一致し、memory_boot_initが成功すること。image/arena/metadata等をsystemへ置かない。
- HPTはavailable∩`[16MiB,32MiB)`から128KiB整列の128KiB、allocator外に置く。OF切離し後に初めて触るなら既存C同様snapshotからの予約でよい。切離し前に書く追加領域はOF claim/mapの成功・返却番地を検査、image PT_LOADの二重claimは禁止。
- IEEE1275本体未入手のため新しい汎用claim規約を発明しない。最短経路は追加領域をOF中に触らず、最終snapshot後はdrain以外の確保/mapをしない方式。OF-PPC本文:338はvector領域をclaimしないこと、:357はOF管理中の一般確保にはclaimが必要と定める。
- real takeover→全BAT無効/ベクタ導入→segments(no-op)/例外stack/ME・RI→`memory_boot_detect(16384)`→`paging_init(16384)`→`arch_boot_after_paging()`→ESCC sink→`memory_boot_init(16384)`→`shm_init()`→`kmalloc_init()`→MPIC→clock→EE→probe→READY→idle（kmalloc は mm/shm から呼ばれないので kmalloc_init の位置は任意。x86 は kmalloc_init を paging より前に呼ぶ: init/kernel.c:170・196）。
- `paging_init`内部 `arch_mmu_load_root` の初回のみ、ESCC/MPIC用I/G aliasを `MEM_DEVICE_APERTURE_BASE` の静的PTへ追加する。HPTの恒等PTEは作らず、その物理範囲の既存恒等PTEもNPにする（spill/flushはrealアクセス）。0..1MiBの不要mappingもNP、guard/system除外を維持。image/stack/PD/PTとMMIO aliasをwalkで検証してからIR/DRを上げる。S/mm/paging_boot.inc:103,189を使い、共通mmの表形式・free/owner規則を変えない。
- alias固定割当：aperture先頭4MiB内のoffset `[0x300000,0x340000)` をESCC、`[0x340000,0x380000)` をMPICへ予約（VA 0xFE300000/0xFE340000）。PAのpage内offsetを保持し、丸めた窓長が各256KiB以内か検査、超過は停止。arch/platform所有の静的予約でledgerのdevice記録は経ない。後のgfxのpaging_map_physはこの予約帯を避け、platform側で重複を拒否してから呼ぶ契約とし、gfx接続の受入に衝突負試験を置く。PM_IO_ALIAS=0xF0000000はkbootでは使わない。
- ABIは全kernel TUにbuild/arch/ppc.mk:8の同一flags（-m32 -mbig-endian -msoft-float -mno-altivec -msdata=none）を適用。入口r2/r13を使用しない生成コードを確認し、C/ASM境界のstack16B整列・linkage領域とnative variadic出力をguest canaryで検証する。
- 初期pagingはIR/DR=0で作るのでflushは準備済み空HPTを扱う。`pg_enabled`になる前のroot未設定を通常root切替と混同しない。未設定時に不用意なtranslated実行へ戻さない。

## 5. D・frame・fault・exec の境界
```c
/* PPC private：根はソフトPDの物理番地、SDR1/HPTは別の固定状態 */
struct ppc_translation { u32 pa, attr; };
int ppc_pd_lookup(u32 root, u32 va, int store, int user,
                  struct ppc_translation *out); /* DR=0、allocation無し */
int ppc_hpt_spill(struct ppc_frame *f); /* 1=再試行可、0=通常fault */
u32 ppc_root_current(void);
void ppc_root_load(u32 root); /* 保存MSR→real→current更新→HPT/TLB全消去→復元 */
void ppc_root_flush(void);    /* current不変、同じ消去列 */
void ppc_trap_dispatch(struct ppc_frame *f);
```
- `arch_mmu_current_root/load_root/flush_tlb/enable`を上記へ結線。currentはarch所有、PD/PTの寿命・所有者は共通mm。HPTはキャッシュであり正典はPD/PT。flushはV消去だけでなく全HPTゼロ＋全TLB消去、root同値でも実行する。
- walkはPDE/PTE present必須、user/rwは両段のAND、PS拒否、leaf PAT拒否。BE CPUのnative u32を読む（x86形はbit配置でありLE wireではない）。Ks=0/Kp=1、supervisor→PP0、USER+RW→PP2、USER+RO→PP1で現x86 CR0.WP=0に合わせる。supervisor RO store拒否を誤って合格条件にしない。
- leaf PCD優先→I/G、PCD無しPWT→W、通常RAM→M。PDEのPCD/PWTを葉にORしない。A/Dの消費者は現時点で無く書戻し無し（MMU_DESIGN:60）。ページ単位NXを捏造しない。ISIも有効PTEなら補充、実行禁止はSR単位しかない。
- spillはDSISR/SRR1のtranslation missだけ。primary8→secondary8→victim（同一VSID/VAの重複を除く）。victimのV落とし→sync→旧VA tlbie→eieio/tlbsync/sync→lo→eieio→hi.V→sync。guard/権限違反で再挿入を続けない。
- victimの旧VAはO/pmap.c:2523–2531と同様にslot index/H/API/VSIDから復元する（単一root・16個の固定VSIDなのでsegmentを一意に得る）。復元をhostで全segment/一次二次について照合し、現在のfault VAを誤ってtlbieしない。
- flush中はEE/IR/DR=0、コード/stack/表へ物理恒等でアクセス、fill禁止。全64 tlbie列後、保存MSRへ復帰。一般のclearループをtranslated Cで走らせると自身のmissで消去中に再挿入するので不可。
- D1は既存176B frame（GPR32/LR/CR/CTR/XER/SRR0/1/DAR/DSISR/vector）を維持。SPRG一時保存→予約したphysical exception/spill stack上で**全frameをDR=0で完成**→spillをrealのまま実行→成功rfi。失敗はframeの値を使う通常dispatchへ。
- 0x500の通常dispatchとfatal診断のMMIOは、全frameをDR=0で完成させた後、DR=1へ切替えてapertureのI/G PTE経由でのみ触る。realの物理MMIOは禁止。壊れた元SPを使わず、EEは復帰まで0。保存域はspill用と通常trap用を分け、回復不能な再入は専用fatal stackで停止。alias/root未準備時のfatalはMMIOに触れず停止。guard stackを誤って物理書込みで突き抜けない。
- 通常trapで共通mm操作が必要なprobeはframe完成後にIR/DRを復元して呼ぶ。MMIO dispatchはDR=1・EE=0を明示し、ここで起きるHPT missは別spill stackで処理できる。未知のDSI/ISIはPPC fatalへ。S/arch/x86/kernel/isr_handlers.c:400のx86 frame handlerを流用しない。
- **共通mmに汎用fault入口は現存しない**。本段の「DSI/ISI→mm」は実PD/PTをwalkし、未存在/保護なら診断停止する接続。demand pagingを追加しない。負試験のみPC/VA/種別が一致したone-shot onfaultで戻る。
```c
/* execとの将来対応（②では呼出し禁止、分離3の型を変えない） */
struct arch_syscall view = { .native = frame }; /* nativeはPPC入口だけが解釈 */
/* arch_user_contextへGPR/SRR0/SRR1等を値保存、view pointerを保存しない。 */
/* arch_priv_context = 現在root等、arch_entry_context = 入り口/stack準備の所有域。 */
```
- slot/引数レジスタの割当・USER遷移・SRR0-=4での再実行は③のABI設計。②で暫定4/4/16をUSER ABIの承認にしない。`arch_entry_prepare`は明示エラー、enter/resume/syscall/kapi invoke/setjmp未実装経路はUNSUPPORTED印字停止。
- 必須実体はroot/flush、irq save/restore、boot、clock、console、stack切替が実起動で使われる場合のstack helper。任意のdisplay/IME/moduleは能力無しを返すkboot専用provider、リンク用の戻り0で「初期化成功」にしない。H1で閉包のundefined symbolだけをこの分類へ割当て、H3で閉包＋明示追加TUの未解決0を確認、weak万能stubは禁止。

## 6. MPIC・tick・log の境界
```c
/* platform所有：OF snapshotの物理窓、CPU0だけ。native_tokenはIACK済vector */
void platform_boot_irq_controller(void);
int platform_irq_claim(unsigned native_token, struct irq_claim *out);
void platform_irq_complete(const struct irq_claim *claim);
u32 pm_tick_hz(void); /* 1000。共通initにPIT_HZを書かない */
int platform_clock_init(u32 tick_hz);
void ppc_decrementer_interrupt(struct ppc_frame *f);
/* console/log：同期sink。LANは後段でenqueue-only sinkとして追加する */
struct pm_log_sink { int (*write)(void *, const char *, u32); void *ctx; };
int pm_log_add_sink(const struct pm_log_sink *sink);
void pm_log_write(const char *bytes, u32 n);
```
- E1はmacio assigned-addresses＋子reg＋親rangesをdecode、MPIC窓をCPU物理へ解決。CPU BEとMMIO順序を分離、PPC accessorでLE/BEを機種compatibleに従い選ぶ。UniNorth config=LEは後段Pでも固定。固定QEMU番地を正常経路に埋めない。
- E2はFEATUREでsource数確認（上限超は停止）、TPR=15→全source mask・宛先CPU0・priority1/vector→8259 passthrough無効→spurious255→pendingを上限つきでdrain→IPI0を予約vector・priority≥1で設定しunmask→TPR=0。reset bitへの無期限待ちを導入しない。
- MPIC offsetはO/sys/arch/macppc/dev/openpicreg.h:35,38–40,49–90に従う：FEATURE=0x1000、GCR(CONFIG)=0x1020（8259_PASSTHRU_DISABLE=0x20000000）、IPI_VECTOR(i)=0x10A0+16i、SPURIOUS=0x10E0、SRC_VECTOR(s)=0x10000+32s、IDEST(s)=0x10010+32s、IPI(cpu,i)=0x20040+0x1000*cpu+16i、CPU0 TPR/IACK/EOI=0x20080/0x200A0/0x200B0。窓長を検査してからアクセスする。
- 外部0x500→IACKを一度読む→spuriousならEOI無し→claim→handler→completeでEOI一回。DECはMPICの線に登録しない。PCI装置は全maskのまま。
- 配送試験はMPIC IPI0をIPI(0,0)へ宛先CPU0のbit 0を書き、CPU0自身へ2回送る専用probe（SMPを実装する意味ではない）。異なるvectorを予約し、0x500/frame/EOI/再配送を実測。IPI経路はE2内、普通のsource bitmapへ偽の線番号を足さない。QEMUでIPI未対応ならMPIC内蔵timerによる配送試験へ変更し、readbackのみで合格に格下げしない（PM）。
- 本線bitmap未着時はE2の0x500専用dispatchで②を閉じる。共通irq.cを改造して並行実装しない。本線着後E3はIACK済tokenを既存claim形へ渡し、line=sourceで登録。32以上のsourceは本線bitmap受入が前提、段Pの装置IRQは②後。
- F1はCPU timebase-frequency検証、TB上/下/上retry、起点差からu64 us変換。必要な整数除算helperはPPC libgccの必要symbolだけlink監査（FP命令無し）。ready前readは失敗して出力未変更。`kernel/time/ktime.c`のclampを維持。
- F2はperiod=TB_HZ/1000、余りはfraction accumulatorで配る。負DECへ次periodを加え正へ戻して再装填、経過period数をtick_countへ加算。②には待機サービスが無いが後段でもtick単位を1msに固定。EE前にready/初回DECを設定、IRQ内で通常ログを出さない。
- busy idleはEE=1のbranch loop。`_idle`はenableしてbusy loopへ入るのでwake eventを消費するsleep窓無し、`_halt`は割込み状態を保つ軽い待ち、`_stop`はEE=0で停止。省電力は後段。
- G2はkboot profileから既存consoleのTUを選ばず、console_write/shell_printを直接提供する暫定stream provider。既存 `lib/kprintf.c:283–306` のnative va_list経路→互換口→pm_log_write→ESCCを接続する。画面座標APIはcapability無し、UTF-8はbyte列、属性は無視。分離5 C1〜C3/Zの着地後に正式providerへ差し替え、両providerを同時選択しない。第5段のcon_backend/gate分離を重ねて作らない。
- sinkはstatic所有・初期化時登録、最大2、早期ESCC＋後段LAN queue。bounded poll、失敗/drop数を保持、LANの接続待ちをkprintf/IRQで行わない。fatal時はregistry/heapを使わずESCC PAへ最小hex dump。既存GUI `con_sink` は別の画面record APIなので転用しない。

## 7. レジスタ・手順の手本との対応（読むべき列を固定）
|項目|BSD file:line／メーカー節|OS32列・差の理由|
|---|---|---|
|入口/MSR|O/sys/arch/macppc/macppc/locore0.S:87、machdep.c:138,168；OF-PPC 8.2.1–8.3|r5/専用stack、capture後EE=0/IR=DR=0、IP/FP/VEC/PR=0。BSDのOF再呼出しは採らず完全切離し|
|memory|O/sys/arch/macppc/macppc/ofw_machdep.c:136–153；N/sys/arch/powerpc/powerpc/ofw_machdep.c:201,264|reg/availableを別parse。OS32は16MiB連続窓を検証して限定管理、未検証の穴を補わない|
|HID0/BAT|N/sys/arch/powerpc/oea/cpu_subr.c:309–331,357–374；N/sys/arch/macppc/macppc/machdep.c:703–721；MPC7450UM 表2-6/2-33・5.3|7450は4組、7455はHIGH_BAT_EN/XBSEN条件を維持し8組無効化。STEN=0を確認/設定、TBEN条件確認。既存cpu_setup.c:47のRAM BAT再設置はkernel版では行わない|
|SR/SDR1|F/sys/powerpc/aim/mmu_oea.c:685–695；N/sys/arch/powerpc/oea/pmap.c:3570；PEM 7.7.1|16SR、Ks0/Kp1/固定VSID、128KiB整列/容量HPT・HTABMASK=1、SDR1=pa\|1。sync→SR/SDR1→sync/isync→TLB、STEN=0のhardware search|
|spill/権限|O/sys/arch/macppc/macppc/locore.S:853–949、O/sys/arch/powerpc/powerpc/pmap.c:2236,2287–2333|real spill→成功rfi、保護は通常trap。BSDの低RAM無条件RW分岐を採らず、全RAMをPD/PTで判定（DにBSDの同形表の手本無し）|
|HPT更新/追出し|同 O/pmap.c:842–854,2451–2552|一次/二次→victim→無効化同期→新lo→新hi.V。OS32はA/D未利用、全flushを採る分だけ性能不利|
|frame/rfi|F/sys/powerpc/aim/trap_subr32.S:93–158,231,346–378；MPC7450UM 4.3/4.6|BSDのDRを先に上げる順はBAT無しでは再帰faultするため変更。全frame real構築、SRR0を勝手に+4しない、rfiで復帰|
|cache/TLB|F/sys/powerpc/powerpc/syncicache.c:48；N/sys/arch/powerpc/oea/pmap.c:548–575；MPC7450UM 2.3.4.3.1/5.4.4.2.3|dcbst→sync→icbi→sync→isync、TLB64列→tlbsync→sync。非AltiVecなので表2-33のdssall省略条件を満たす|
|MPIC設定|O/sys/arch/macppc/dev/openpic.c:212–299,600；N/sys/arch/powerpc/pic/pic_openpic.c:63–68,95–118|FEATURE/source、mask/CPU0、spurious/TPR。Nの一括EOIをそのまま写さずIACK結果で完了する。Oのreset待ちはbounded化|
|MPIC配送|F/sys/powerpc/powerpc/openpic.c:290–308,342–351；O/openpic.c:175–181,748|IACK→dispatch→EOI。自己IPIはprobeだけ。PCI_IRQ_ROUTE_DESIGN:284–287に従いTPR0/CPU0/bitmapを保持|
|DEC/TB|O/sys/arch/macppc/macppc/clock.c:119–129,251–261；N/sys/arch/powerpc/powerpc/clock.c:108–116,139–159；MPC7450UM 4.6.9、PEM 6.4.9|Oは再arm型、OS32はNの周期加算型を採る。100Hzからplatform指定1000Hzへ。TB周波数をCPU core clockと同一視しない|
- メーカー本文の照合箇所：UM検索本文:4472–4474の同期注記、OF-PPC本文:224の入口r5節、:338/:357の予約・claim。引用は規約の根拠でありBSDコードの構造をコピーする指示ではない。

## 8. 大きさ・load/claimの見積もり
- PPC kernelは今回未リンク：text+rodata+data **0.5〜1.5MiB**、BSS/常駐表 **0.2〜0.8MiB**を初期予算とする（根拠弱、H3のsize/mapで置換）。HPTは別128KiB、入口stack32KiB・trap/spill各16KiB、固定PD/PTは既存26page枠内。
- ELFは1MiB load、resident末尾から既存KHEAP/KAPI/SHMが続き、`MEM_KERNEL_IMAGE_MAX` と0x3E0000 guard境界をlinker ASSERTで検証する。範囲超過をOF NOTEのmax-sizeだけ広げて逃がさない。
- 単体像のPM_LINK_BASE=0x20000/PM_MMU_IMAGE_LIMIT=0x20000/PM_STACK_SIZE=8192（S/ppc_layout.h:37,40,65）は本体に流用しない。OF NOTE、PT_LOAD p_filesz/p_memsz、entry、BSS、resident/ledger symbolsを同じ新配置から生成する。
- OF loaderがimageのmemszを確保していることを最終availableとPT_LOADで照合。既存像がロード可能でも1MiB以上のkernelロード保証ではない。H3/T3でOFからの実loadを条件にする。
- 固定16MiB窓を確保できないQEMU構成ならB2は止まる。穴を支える汎用RAM adapterを先に拡大せず、OF占有とELF配置を記録してPMが配置変更を判断する。実機用の全RAM利用と回収は②後。

## 9. 受入項目 → 試験 → 変異（各一行）
|項目|証拠・試験|単一変異とRED条件|
|---|---|---|
|全リンク/配置|selected全TU計測＋ELF32/BE/EM_PPC、undefined=0、link map、OF実load|PT_LOAD memszを縮める→配置照合不一致（runtime変異とは別静的検査）|
|OF表/予約|host BE/複数区間/穴/overflow、QEMU snapshotとallocator範囲照合|available穴を無視→穴fixtureで起動が誤続行しhost RED|
|OF切離し|ラッパーの自己申告counterをREADY/ALIVEログで比較（QEMU外部観測ではない）＋host禁止呼出変異|禁止flag検査を落とす→hostの切離し後呼出でRED|
|実mm/allocator|QEMU allocate/write/free、owner/ledger/guard、予約HPT/systemを割当てない、system恒等PTEのNPとledger予約一致（B3）|HPTをadmitted RAMへ混入→予約監査RED|
|PD/PT/権限|host present/USER/RW/PCD/PWT/PAT、QEMU実DSI/ISIとNP guard|PTE present判定反転→有効頁の読書/guardでRED|
|HPT満杯|host同hash17頁、QEMUprimary/secondary/victim後の再読|secondary H bit削除→既知値不一致RED|
|flush/root|QEMU root A/Bの同VA別PA、unmap/remap、動的guard|全HPT clear省略→旧値又はguard非faultでRED|
|BAT撤去|objdumpでPVR分岐を含むBAT無効化SPR列を静的検査＋QEMU固定/動的guard負像（QMPでvalid値を読めるとはしない）|RAM BATを再設置→guard到達後SURVでRED|
|frame/再入|QEMU GPR/CR/LR/CTR/XER canary、元stack guard、DSI中spill|r31復元offsetを隣へ→canary RED|
|fatal/未対応|QEMU未知fault/USER入口で正しいdump＋停止PC|unsupportedをreturnへ→SURVでRED|
|MPIC|host MMIO列/endianness/spurious、QEMU自己IPI二配送|TPRを15のまま→配送期限内のprobe不成立RED|
|EOI|host claim完了一回、QEMU二配送＋後続DEC|EOI書込み省略→二配送不成立またはhost列RED|
|tick/time|host TB wrap/余り/負DEC/出力未変更、QEMU2秒TB対tick|DEC再装填削除→tick増分不足RED|
|console/LAN口|host二sink順/失敗drop/va_args、QEMU Aのみ節目出力|ESCC data store省略→節目欠落RED|
|x86不変|build_compare（通常/R1/配布対象）、変更部host、差ならNP21/W|source選択にPPC provider混入→選択fixture RED、生成物比較も失敗|
- 実装者は各段の要求build、触った試験の`--mutate`、`python3 -B tools/checks/run.py --lint`。新変異はunique anchor、既存mmu_run.py:214以降のanchorを移す場合は旧単体像の試験も維持する。
- selected/knownは実.dによる更新、bootstrap7本など既存必須集合を落とさない。全リンクprofileの成功と通常selected失敗一覧は別に報告。CI landing/full-checkの役割はROLES §0に従い、make check-changed/全変異をローカルで回さない。

## 10. 危険・未確認・PM推奨
|区分|残る危険と扱い|
|---|---|
|QEMUで判定する|OF大きいELF load、固定窓available、IPI実装、HPT/flush再miss、stack/frame、1000Hz継続、selected未解決一覧。初回H3のmapとT3ログが出るまで成功を断言しない|
|QEMUでも保証しない|cache/WIMG/orderingの実機時間、HPT更新競合・745x errata、実clock精度。host列＋メーカー条件を併用し、QEMU成功から安全性を外挿しない| realデータアクセスのWIMG=0011はcache有効（PEM本文12910–12914、7450UM本文13357）。G4のESCC・MPIC IACK/EOIはframe完成後DR=1・aperture I/G必須で、QEMU通過だけではこの条件を保証しない。
|G4実機で後追い|QuickSilverのPVR/OF tree、RAM穴、ESCC配線/電源、MPIC窓とsense、OF virtual address、DMA/cache。G4の確認を②の条件に追加しない|
|資料不足|SysV/EABI全仕様、IEEE1275本体、QuickSilver Developer Note、UM Rev5/errataは未入手。整数kernelのコンパイラABIを一致させ、USER/module ABIは③へ残す|
|最短経路の制限|16MiB管理・画面無し・root無し・USER未準備をログとprofile名に明示。既存PC98の全services-readyと同一だと報告しない|
|③前の負債：tick単位|共通PIT_HZ=100（memmap.h:437、bootlog.h:72、kbd.c/iso9660.h/fdc.h）と1kHz DECの整合は③前に解消。H3で閉包内にPIT_HZ利用者が無いことを推移的header依存・生成物も含め監査し、あれば②は未受入とする|
- ユーザー判断が必要な未決：**0件**。②定義、案D、整数のみ、LAN優先は既決。新たな承認質問を増やさない。
- PM推奨：core-ready境界／16MiB限定／busy idle／OF FB後段／新受入台本を採用。QEMU IPI不可時の内蔵timer probe、容量超過時の配置修正は観測後PMが処理する。
- ②合格直後はGMAC→Host Services LAN log/遠隔操作、必要なPCI段Pをその前提に置く。今回のsinkはenqueueを追加できる契約に留め、画面や他装置を先行させない。

## 確定 (2026-11-10、Opus 5.5 第 2 回の承認を PM が反映)
- N1: §4 の起動順の注記「x86と同順」を事実に合わせて差し替え (x86 は kmalloc_init が paging より前、mm/shm は kmalloc を呼ばないので順は任意)。
- N2: 検証窓を `[1MiB,15MiB)` に揃えた (geometry {15M,15M,16M,16M,16M} に合わせる)。
- 未決: 「ユーザーランドの手前」= core-ready 境界 (VFS・root mount・exec_init・module 読込みを除く) の解釈はユーザーへ報告して確認中、推奨どおり進める。

## 実装中の PM 決定 (2026-11-10)
- **B3 の区間の一致** (astra 第 1 回 P2): high probe=0 では共通 mm の physmem は [15MiB,4GiB) が一続きの UNKNOWN (非 RAM)、ledger は [15,16)MiB と [16MiB,4GiB) の BACKGROUND になり、§4 の「ledger の BACKGROUND と physmem の除外が同じ区間で一致」は字面どおりには成り立たない。② では allocator が system も HPT も 16MiB 以上も渡さないことが成り立つので、**② の受入は「全ページの非 RAM の一致 + allocator の除外」で足りる**とする。共通 mm に high 登録と独立した除外の経路を足すと x86 の生成物が変わり NP21/W が要るので、16MiB を超える RAM を使う段 (② の後) へ送る。
- **B3 で見つかった既存の x86 の試験の判定** (tools/tests/test_boot_readers.py:169、全 REQUIRE の失敗を exit=1 に畳む): B3 の範囲外。直すなら x86 本線の試験の整備として別に出す。
- **E3 (IRQ の adapter) は ② の後へ**: 本線の PCI IRQ の共通部 (bitmap) はまだ main に無く、§6 の l.176 のとおり ② は E2 の 0x500 専用の dispatch で閉じる。E3 は本線の bitmap が入った後に出す。これで第 4 波は D3 と H1 になり、H1 は D3 の後に出せる。
- **E1 の mac99 の互換**: QEMU mac99 の OF は macio の assigned-addresses と ranges の親で phys.hi の n を 0 のまま割当済みの番地を入れる。E1 は曖昧でない場合だけ受け入れ、BAR の候補が曖昧なら拒否する。G4 の OF が同じ形かは実機で確かめる。
- **G2 の能力無しの値**: 画面の座標は -1、サイズは 0×0。戻り値の無い cursor/GDC の制御は何もしない。LF は変換しない (x86 の serial の出力の慣習に合わせる)。lib/kprintf_attr.c は kboot の profile で選ばず、属性の変換の口は G2 が出す。
- **T3 の受入の台本を H3 より先に作った** (仮の像で自己試験)。本体の負像の FATAL と停止の symbol は H3 の後に表へ足す。
- **試験の変異の判定**: 変異ごとに期待の停止 PC/終了コード/診断の行と到達の印を表に持ち、両方が一致したときだけ RED (決まりの全文は PPC_IMPL_RULES §3)。

## 後続の題目
- **PowerPC のブートスプラッシュ** (2026-11-10、os32-v3 の PM 経由のユーザー決定): 票 TASK_PPC_SPLASH。② の段の並びは変えない (② は画面を条件にしない)。実装は OF の FB を描く段の後。
