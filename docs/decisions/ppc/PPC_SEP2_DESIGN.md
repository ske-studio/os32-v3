# PPC_SEP2_DESIGN — 分離の第 2 段 (起動・例外・IRQ・時計・V86)

> 状態: **現行 (2026-11-07)** — x86 の振る舞いを変えずに、起動の段・IRQ controller・時間源・V86 の optional な口を契約にする。段の状態は票 TASK_PPC_SEP が持つ。
> 出所: ~/os32-tmp/run/ppc-sep2/DESIGN.final.md を 2026-11-07 に写した (astra 起草 r1〜r2、Opus 5.5 第 1・2 回)。

- 2026-11-07。読取元 S=`/home/hight/wt/os32-v3/ppc-sep1`（第1段 `wt/ppc-sep1`）。第2段は第1段の上に積み、実装・比較の基点は第1段が main に入った SHA とする。以下の相対 file:line は S の実物確認に基づく。
- 根拠：`/home/hight/os32-tmp/run/ppc-arch-survey/SURVEY.md` I1/I2/I3/I4/I5/A4/A6/V1/V5、`docs/tasks/ppc/TASK_PPC_SEP.md`、`docs/decisions/ppc/PPC_MMU_DESIGN.md`。本書は案D接続前の共有変更。
- x86 の機能・KAPI/SDK・PD/PT形式を維持。単一CPU・協調切替、整数カーネル。既存本文への PowerPC ifdef 追加なし。契約は include、CPU実装は arch、機種実装は platform。
- ビルド・試験・実走は今回行わない（読取りと検索のみ）。以下の受入は実装者と os32-v3 PM の作業。x86 は NP21/W のみ、PPC は後段のエミュレータ。実機の回を作らない。

## 1. 実物と境界
|ID|確認した実物|設計上守ること|
|---|---|---|
|I2|`init/kernel.c:122` stack診断→pc98 bind、`:130` bootinfo capture→`:131` BIOS原本保存→`:138` RAM検出→`:141` CPU段→`:143`入力|低位再利用前の2つの保存は別物。V86無効でもPC98のRAM/clock/displayは原本を必要とする|
|I2|`init/kernel_boot_cpu.inc:29` GDT→`:35` TSS→`:36` #DF→`:40` IDT→`:46` PIC→`:52` sysclk→`:54` DMA→`:57` PIT→`:65` IRQ0→`:66` cascade→`:70` STI→`:71` calibrate|表示の位置も維持。cascadeはPC98の7、ATの2にしない|
|A4|`init/kernel_boot_memory.inc:84` paging→`:88` conventional reclaim→`:90` font→`:97` CR0読取→EM/TS消去→`:100` x87_probe→`:104` memory_boot_init|整数化を理由に既存x87初期化を削除しない。ここはx86互換初期化として残す|
|I1|`kernel/irq/irq_math.h:31` 16線、`irq_math.c:18` 動的3/5/6/8/9/10/11/14/15、`:103` 2巡共有dispatch、`:132` storm、`:151` EOI政策|16線と予約集合だけを機種へ。共有・隔離・登録エラー優先順位は共通に残す|
|I1|`kernel/irq/irq.c:107` IRQ15 ISR採取→`:112` EOI選択→`:119` unexpected報告。`include/platform_irq.h:5` はslave ISR/master EOI/timer IRRの口にすぎない|IRQ15 spuriousでも旧dispatchと統計を保ち、slave EOI無し/masterのみ。handledとEOIを結び付けない|
|I1|`arch/x86/kernel/isr_stub.asm:557` IRQ7専用入口、`:565` ISR、`:571` IRRへ復帰、`:576` spuriousならEOIなし|IRQ7を汎用dispatchへ統合して意味を変えない。固定スタブのEOIと新口の二重完了を禁止|
|I3|`kernel/time/pit.c:26` fallback込み設定、`pit_math.c:49` 100Hzのみ、`:55` 2入力クロックのみ、`:71` 整数分周。`ktime.c:66` 未準備NOSYS、`:70` IF保存、`:76` 採取、`:85` 再試行、`:94` clamp、`:102` IF復元、`:107` 出力|3回失敗AGAIN、失敗時出力不変、IF復元後にuserへ書く。32bit tick wrapを含め現仕様を変えない|
|I4/I5|`include/cpu.h:30` x86_flags直include、`:32` ABI_WORD_BYTES=4、`:34` ABI_STACK_ALIGN=16。`drivers/net/ne2000/ne2000.c:149` eflags_ifにpushfl/popl|I4はflagsだけ切る。ABI定数とcdeclは第3段へ。I5は既存 `_irq_enabled()` に置換|
|A6|`kernel/r1_fixture.c:37,44` ud2、`kernel/font_guard_fixture.c:23` DS破壊、`:24` page0アクセス|fault注入だけarch化。font faultは停止が正解であり、例外復帰試験とは区別|
|V5|`include/bootinfo.h:6,38,73,95` INT1Bh/LE/DA80・81/struct wire。`init/bootinfo.c:60` capture、`:74` magic消去|wireをPPCに課さない。PC98の64B wire・検査順・ログ・KAPIレイアウトは維持|
|例外|`arch/x86/kernel/isr_handlers.c:290,297,316` font→SQLite→user/syscall分類、`isr_stub.asm:242,333` #GP/#PFとV86経路|x86 frame復号はarchに保持。停止・復帰の優先順と既存exec fault出口を変えない|

### V86呼び手の全範囲（製品C/h/incとx86 ASMを検索、コメント・生成物は別記）
|呼び手・読み手|置換/保持と試験|
|---|---|
|`exec/exec_syscall.inc:60,120` VM検出/int80、KAPI dispatch|VM判定はx86専用入口へ。slot dispatchはlegacy口。test_exec_r1/test_v86_call、ring3 hello/fault|
|`exec/exec_schedule.inc:17,187,188`、`exec/exec_launch.inc:15,16`、`exec/exec.c:463`、`exec/lease.c:369`|pinとlow-map占有を混同せず別照会。busy/invalidの返値は各呼び手のまま。test_exec_r1/test_shell_service/test_v86_call|
|`exec/exec_finish.inc:332–377` closing/aborting/end/cleanup|再入停止→abort設定→end→cleanupの順を保存。通常endも別口。test_exec_r1/test_r1_fixture/test_v86_gcap_abort|
|`kernel/console/console.c:55,67,402,420` 描画/カーソル|legacy_activeへ。sink出力はそのまま。新caller試験でactive=0/1とsink=0/1を組み合わせる|
|`init/kernel.c:131`、`init/kernel_boot_memory.inc:123`|BIOS原本はplatform capture、module loadはlegacy_init。原本をfeature無効で消さない|
|`kernel/kaudit.c:20,54`、`mm/paging.c:484,520`、`mm/paging_addrspace.inc:67`、`mm/paging_map.inc:230–284`|低位mapping占有の照会と取引へ。test_v86_lowと新paging caller試験でskip/拒否/復元を検証|
|`exec/v86_call.c:34–157`、`kernel/r1_fixture.c:34,40`|前者はx86 legacy adapterへ移設、後者は状態照会＋arch fault口。admit/pin/copy失敗/解放をhostで維持|
|`platform/pc98/{memory.c:48,clock.c:9,display.c:50,splash_probe.c:411,1497}`、`gfx/gfx_core_surface.inc:140`|BIOS原本getterの互換名を残して所有権だけplatformへ。gfx本線は触らない。新capture読者試験|
|`arch/x86/kernel/isr_stub.asm:100,152,187,263,350,468,511`、`tss.c:15`|反射・timeout・exit・TSSはx86側に保持。非搭載リンク用入口はframeを読まず非活動を返す。x86 NP21往復|
|`exec/exec_kapi_init.inc:431–434,506`、`kapi/kapi_generated.c:1765–1786,2321`|5スロット/引数/ラッパー不変。既存名facadeを残す。非搭載5口のNOSYSと出力不変を試験|
|`kdebug/kselftest/memmap.inc:17`、`kcg_display_return.inc:26`、`kdebug_main.c:205–215`、`kdebug/v86_gcap.c:36–43`、generated V86 imports/exports|x86 feature所有。互換ヘッダ/シンボルで本線変更を避ける。test_v86_gcap/test_v86_module。PPCでは選択しない|
|`drivers/input/kbd.c:37–39,332,388–390` / `:270`、`drivers/video/pc98/backend_pegc.c:157`、`kdebug/crt_diag.c:18`|Eでkbdのactive/request_exit/pushをlegacy口へ、BIOS読者はGのgetterへ（PEGCは互換getter）。kbd_status/kbd_dlog/kbd_fgpoll/pegc_mode/crt_diagで非活動・入力・BIOS値を検証|
|`kernel/v86/*`内部、`gfx/gfx_core_surface.inc:379` gfx_v86_return、kapi_sys.c監査カウンタ|前者はbackend内部、後二者は復帰先/統計でV86 frame不要。名称のみの一括改名はしない|

## 2. 契約とx86の置き場
### 起動・例外（B段）
```c
/* include/boot_cpu.h: Cのみ。すべて割込み状態を勝手に変更しない */
void arch_boot_segments(void);     /* x86 GDT、現行表示の間で呼ぶ */
void arch_boot_exception_stack(void); /* TSS(MEM_KSTACK_TOP), #DF */
void arch_boot_vectors(void);      /* IDT、既存の例外/IRQ/syscall gates */
void arch_boot_after_paging(void); /* CR0.EM/TSとx87_probe、現位置 */
/* include/platform_boot.h */
void platform_boot_capture(void);  /* PC98: bootinfo→BIOS原本、元の位置 */
void platform_boot_irq_controller(void);
void platform_boot_clock_detect(void);
void platform_boot_irq_start(void); /* PC98 IRQ0→IRQ7、IFは変更しない */
```
- `kernel_boot_cpu` が上の順を明示して呼ぶ。clock_detect→既存BOOT_DMA→timer_init→irq_start→既存_enable→cpu_calibrateを保持。巨大なarch_init一発にしない。
- 段間の結線：Bではtimer_initは既存pit_initのまま、Dでplatform_clock_initへ変更。Bではkernel_mainのcapture2呼出も維持し、Gでplatform_boot_captureへ置換する。後段未実装のシンボルを先に呼ばない。
- 実装は `arch/x86/kernel/boot_cpu.c`、`arch/x86/kernel/boot_after_paging.c`、`platform/pc98/boot_irq.c`、`platform/pc98/boot_capture.c`。sysclk検出はPC98、速度校正は当面x86所有。
- 例外入口/frame/CR2/VM/#DFは既存archファイル。今回共通へ汎用trap frameを新設しない。legacy trapの結線はEで行う。
- `include/fault_fixture.h`: `void arch_fixture_ud(void); void arch_fixture_gp(void);`。実装 `arch/x86/kernel/fault_fixture.c` はud2/DS注入のみ。page0のNP試験とarmのone-shot消費はcallerに残す。未対応archはfixtureを選択しない（成功no-opにしない）。

### IRQ（C段）
```c
/* include/platform_irq.h: topology/reserved照会はIF不問、操作はIF=0。boot後固定 */
struct irq_topology { unsigned lines, dynamic_count; const unsigned *dynamic_lines; };
const struct irq_topology *platform_irq_topology(void);
int platform_irq_reserved(unsigned line); /* 範囲内固定線/予約線。範囲外は先に拒否 */
void platform_irq_mask(unsigned line); void platform_irq_unmask(unsigned line);
/* native_tokenはcontroller固有。spurious分類と完了の責任はbackend。 */
struct irq_claim { unsigned line, native_token; int deliver, needs_finish; };
int platform_irq_claim(unsigned native_token, struct irq_claim *out);
void platform_irq_complete(const struct irq_claim *claim);
```
- x86 stubは線番号tokenを渡す。動的IRQ15は旧どおりdispatch後completeでISRを読む（先にspuriousとして捨てない）。IRQ7専用スタブは従来の判定・EOIを保持し、completeを追加しない。固定0/1/2/4/12/13も従来の所有者だけが完了。
- claimは成功1（deliver=0も成功）、未知token/不正出力は負値でout不変。成功時deliver=1だけdispatch、needs_finish=1だけcomplete。既存 `irq_finish(irq, handled_any)` は互換口として残しx86 claim→complete→unexpected報告を行う；新経路とは排他的に呼びEOIは一度。`dep_dir_b_pic_host.c:66`, `irq_math_host.c:336,408` を更新。
- OpenPIC向けにはIACKで得たvector/tokenを渡し、255はdeliver=0/needs_finish=0、正常線はfinish一度。共通はPIC/ISR/255を知らない。後段の入口はclaim失敗時にdispatch/finishしない。
- `platform/pc98/irq_policy.c` に16線/動的9線/予約表/EOI計算を移す。`platform/pc98/irq_complete.c` がISR→IRR復元とslave→master完了を所有。既存 `pic.c` はI/O実装として維持。
- `irq_dyn_index_in(topology, irq)` を新しい純関数とし、公開 `irq_dyn_index(irq)` は選択topologyへ転送してkdebug import (`kdebug_kexports.c:124`, `kselftest/irq_dynamic.inc:21`) を維持。`irq_math` はtopologyを引数に受ける純粋政策、2巡/最大handler数/storm閾値は維持。配列容量は `PLATFORM_IRQ_LINES` / `PLATFORM_IRQ_DYNAMIC_COUNT` で静的確保し、CでBのcontroller初期化直後・unmask前へ挿入する `irq_topology_check()` で一致確認。失敗時はkprintfで診断し `for(;;) _stop()` で停止する。固定名 `platform_irq_limits.h` を `platform/pc98/`（16線/動的9線）と `platform/powermac/`（最小値、暫定providerの実走禁止）の両方へCで置き、`include/platform_irq.h` から選択先をincludeする。`tools/ppc/selected_success.json:47–48` のirq.c/irq_math.cにもこの選択を適用する。
- `irq_storm_masked/irq_line_quarantined` のu32は既存診断の下位32線互換投影。全線の正はirq_line内の状態、32以上ではshiftしない。64線mockで検証し、OpenPICに16/32線制限を持ち越さない。

### 時計（D段）
```c
/* include/clock_source.h: 単位を分け、浮動小数点を要求しない */
struct clock_info { u32 tick_hz, counter_hz; int ready; };
int platform_clock_init(u32 tick_hz); /* 本段のOS tickは100Hz固定 */
void platform_clock_info(struct clock_info *out);
int platform_clock_read_us(u32 *lo, u32 *hi); /* IF=0、kernel局所出力のみ */
/* include/kernel_tick.h: PIT/IDTをincludeしない */
extern volatile u32 tick_count;
```
- PC98実装 `platform/pc98/clock_source.c` は現在のpit_get_setup検証、3回採取/time_decide/time_us_fromをそのまま所有しNOSYS/AGAINを返す。pit/pit_mathをplatformへ、time_decide/time_us_fromとtime_branch_reset/time_branch_hitsを `platform/pc98/pit_time.c/.h` へ移す。`kernel/time/time_math.c/.h` にはtime_clampを残し、ktime.c:25とkdebug importの共通依存を維持。
- 共通 `kernel/time/ktime.c` はirq_save→read_us→成功時clamp→irq_restore→成功時だけuser出力。情報照会の周波数はtick_hz=100と入力counter_hzを混同しない。クランプ統計とtime_test_feedの試験入口を保持。
- tick_count定義はまず `idt.c:19` のまま宣言だけ分離（配置不変）。D段で `kernel/time/tick.c` に定義を移す。x86 ASMのincと呼出順は維持。将来PPC DECから同じtickを更新する。
- `include/boot_diag.h` にtimer種類/入力周波数/IRQ vector表示値のsnapshot口を設ける。PC98値を供給し、verの既存表示文言・並びは不変。`bootlog_save` はkernel_tickだけ読む。PIT/IDTのヘッダを診断の契約にしない。

### V86 optional serviceと低位mapping（E/F段）
```c
/* include/legacy_guest.h、link時にx86かnoneを1つ選ぶ */
int legacy_init(void); int legacy_active(void); int legacy_pinned(void);
int legacy_session_open(void);
int legacy_closing(void); int legacy_lowmap_busy(void);
void legacy_abort_begin(void); void legacy_end(void); void legacy_cleanup(void);
int legacy_trap(void *native_frame); /* x86だけframeのVMを読む。消費1、通常0 */
void legacy_request_exit(void); int legacy_kbd_push(u8 scancode);
int legacy_call_dispatch(u32 slot, void *fn, const void *args, u32 bytes, u32 *result);
```
- dispatchの返値は非対象0、処理済1、-1 = 呼び手が ring3_fault_kill する。`exec/v86_call.c:119,129,154,157–158` はcleanup後に-1を返すだけで、killは `exec/exec_syscall.inc:120–121` が行う。-1ではcallerは通常のresult書戻しをしない（既存exec fault出口へ）。legacy_trapはEでexecのVM分岐の同位置へ結線。
- `exec/v86_call.c` の移設先は `arch/x86/legacy/v86_call.c`。x86 adapterは現行v86_callとsession各fieldへ転送。終了再入時のexec_stop_mark/_stopはcallerで維持。abort_beginはaborting=1だけ、endとcleanupをまとめない。session.open、closing、pin、mapping占有を独立に扱う。
- `legacy_session_open()` はsessionがopenなら1、そうでなければ0を返す独立照会。`arch/x86/legacy/guest.c` は `arch/x86/legacy/v86_mem.c` が所有する `v86_session.open` をそのまま返す（beginのsnapshot成功後に1、endの復元・I/O policy reset・detach後に0）。active/pinned/closing/lowmap_busyから導出せず、closing=1でもopen=1の期間を保持する。通常のカーネル文脈・IRQ/例外文脈で呼出可、IFの事前条件なし。状態の読取だけで、割当・待機・module呼出・IF変更・状態更新を行わず、複数照会間の一貫したsnapshotは保証しない。`kernel/r1_fixture.c` のready/endは既存のopen読取だけをこの口へ置換し、closing・ring3_in_syscall・one-shotの判定順とx86の挙動を変えない。
- `platform/none/legacy_guest.c`（feature offで選ぶ実装）はactive/pinned/session_open/closing/lowmap_busy/trap/kbd_push=0、end/cleanup/abort_begin/request_exitは無操作、init=NOSYS。dispatchは対象5slotならresult=NOSYS・消費1、その他は0；args/frameを読まない。
- KAPIの既存名5facadeもnone実装を用意しNOSYS、出力バッファ不変。常駐x86 facadeのmodule未ロードと、非x86の非搭載を区別。sdk/kapi.json・スロット順・生成ラッパーは変更しない。
- `include/legacy_lowmap.h` の唯一の型は `struct legacy_lowmap_snapshot { u32 root, master_pde, active_pde; u32 *saved_pte; u32 page_count, limit, restore_pte_bad, restore_pde_bad; };`。session内へ埋め、既存配列をsaved_pteへ渡す。open時にpage_count/limitを固定してPT0/容量内を検査、復元時にcaller値を再採用しない。割当を増やさない。
- `mm/paging_lowmap.inc` にsnapshot/restore/mapの実装を切り出し、V86定数はcallerのページ数/上限として渡す。単一取引tokenで占有を照会。全PTE→2つのPDE→flush一度→照合→占有解除、復元中無割当を保持。
- v86_mem側adapterが従来名 `paging_v86_*` を提供し新取引へ転送。共通paging/lease/kauditは汎用busy口だけ読む。noneは取引を開始しない。案DのPD/PT形式・USER伝播・拒否条件は変更しない。
- BIOS原本 `kernel/v86/v86_snapshot.c:10,24` は `platform/pc98/bios_snapshot.c` へ。`real_lowmem[PAGE_SIZE]` と `real_saved` をbios_snapshot.cの静的域へ移しsessionから削除。platformのready/getter/save/restore口へ集約、v86_mem.c:64もready口へ。旧getter/save名は互換入口として保持。`kdebug/crt_diag.c:18` はgetter importへ変更（kdebug本文変更あり）、import定義・生成物も正規生成で追従。実保存をboot_captureで一回だけ行い、feature offでもPC98読者を満たす。
- V86本体は第1段のfeature選択に加えx86所有を明記し、CPU部を `arch/x86/legacy/`、BIOS/PIC/KBD/I/O emulationを `platform/pc98/legacy/` へ機械的移設する。旧ヘッダはx86専用互換転送として残し、共通からは引かない。

### I4/I5・bootinfo（A/G段）
- `cpu.h` からx86_flags includeのみ削除。Aで `arch/x86/arch_cpu.h` 自身が `x86_flags.h` をincludeし:98の依存を解消（exec.cへの追加は不要）。同じAで `tools/tests/host_arch/arch_cpu.h` に `x86_flags.h` includeを1行追加する（現物:20はmemmap.hのみ）。`tools/tests/shm_user_host.c:210` と `exec/exec_syscall.inc:60` のEFLAGS利用を維持し、host_archとexecを引くpy 22本のCHECK inputs/include探索先/コピーを実装者が確認する。EFLAGSを直接使う残りのx86 TUは明示include、共通IF照会は `_irq_enabled()`。paging/ring3_strのPTEビットは `paging.h` のまま。EFLAGSの値を共通へコピーしない。
- `exec/ring3_ls.S:3,9` はidt/ring3_ls.h→cpu.hを読む。`__ASSEMBLER__` 外のABI定数を維持しC宣言を流さない。通常/R1両規則 `build/kernel-objects.mk:55,93` の `-m32 -Isdk/include/os32` を確認し、明示flagsを必要とする場合は相対pathか両規則への `-Iarch/x86` を揃える。
- `test_shell_service.py:49,184,195` は既にkernel-objects.mkとcpu.hのABI_STACK_ALIGNの字面を検査する。ABI定数移設は本段でしない。関数抽出hostのコピー先に新ヘッダを追加、検査を消して通さない。
```c
/* include/bootinfo.h: native情報。旧boot_image_info等の関数ABIは維持 */
struct boot_common { int image_valid; u32 image_crc, image_size; };
const struct boot_common *boot_common_get(void);
/* platform/pc98/bootinfo_wire.h: 旧wire定義・offset・LE parser専用 */
/* platform/pc98/bootinfo_native.h: BIOS詳細/幾何、PC98診断専用 */
```
- `init/bootinfo_check.c` はPC98 parserへ移しbyte読取りを維持。`init/bootinfo.c` はimage/buildの共通公開口とPC98 capture/geom/reportへ分割。旧bootinfo_get互換はmodule pair検査に必要なimage欄を維持し、BIOS詳細読者だけplatform口へ移す。
- kernel/module、各*.mod loader、ver、bootlog、init/boot_disk.hの読者を試験。drivers/video・hwdevは互換口を通し、kdebug/crt_diag本文だけ上記getterへ変更する。PPCはOF引数の検証済み値からnative情報を供給し、BIOS幾何は「無し」。wire構造体のcast禁止。

## 3. 独立受入の段とファイル別規模
- 数字は実装差分の概算行数（追加/変更、移設は別記）、測定値ではない。各セルの列挙は各ファイルへの割当。1ファイル1機能を守り、大きな万能backendを作らない。A→B→C→D→E→F→F2→G→H1→H2、各段をPMが受入後に次を取り込む。
|段・生成物|ファイルごとの変更量|host試験（既存名はtools/tests/test_*.py、新規は＋）。右欄の受入は各段6項以内|
|---|---|---|
|A 不変目標：宣言/純移設|include/cpu.h −1、arch/x86/arch_cpu.h +1、tools/tests/host_arch/arch_cpu.h +1（x86_flags.h include）、arch/x86のflags利用TU各+1、include/kernel_tick.h 新15、init/bootlog_save.c 2、build/kernel-objects.mk 8、test_shell_service.py 8。ne2000はBへ|shell_service/ring3_str/bootlog/shm_user/ne2000_dma（ne2000_dma_host.cのEFLAGS依存を明示）、＋header_boundary（ASM/依存検査、変異＝flags再流入）。①host ②変異 ③通常/R1 ASM ④機械語比較 ⑤known差分|
|B 変化：起動/fixture/I5|include/boot_cpu.h 新25、include/platform_boot.h 新20、include/fault_fixture.h 新12、arch/x86/kernel/boot_cpu.c 新35、boot_after_paging.c 新20、fault_fixture.c 新12、platform/pc98/boot_irq.c 新25、init/kernel_boot_cpu.inc 35、kernel_boot_memory.inc 15、init/kernel.c 8、r1_fixture.c 8、font_guard_fixture.c 5、ne2000.c 7、ne2000_dma_host.c 8（_irq_enabled fake）、tools/tests/fixtures/build_profile_x86.json 各5–20|ne2000_dma/r1_fixture/font_guard/exec_r1/dep_dir_d3_boot/stack_entry、＋boot_order（実関数をstub結合し呼出列・IF・CR0 maskを検証）、変異＝TSS/IDT逆転・EM/TS残存・IF反転。①host ②変異 ③通常/R1生成 ④NP項1–3,7 ⑤known|
|C 変化：IRQ|include/platform_irq.h 45、platform/pc98/platform_irq_limits.h 新10（16/9）・platform/powermac/platform_irq_limits.h 新10（最小値、実走禁止）、include/boot_diag.h 新30、platform/pc98/irq_policy.c 新60、irq_complete.c 新35、boot_diag.c 新35、kernel/irq/irq_math.c 45・同.h 25、irq.c 65・irq.h 15、kernel/ver_collect.c 20・ver_format.c 20・ver_info.h 15、tools/tests/fixtures/build_profile_x86.json 各5–20|irq_math/dep_dir_b_pic/ver_collect/ver_format/ver_layout、＋irq_controller（実irq.c＋mock、16/64線）。変異＝EOI二重/逆順、IRQ15 slave誤EOI、IRQ7誤EOI、31/32 shift、予約許可。①host ②変異 ③生成 ④NP項1,4,5 ⑤known|
|D 変化：時計|include/clock_source.h 新25、platform/pc98/clock_source.c 新65、kernel/time/ktime.c 55、tick.c 新8、arch/x86/kernel/idt.c 2、idt.h 20、kernel/ver_collect.c 10。pit.c(36)/pit_math.c(79)/pit_math.hをplatformへ全量移設、time_math.c/.hはclampを残してdecide/us_fromとtime_branch_reset/time_branch_hits約60/50行をplatform/pc98/pit_time.c/.hへ。kdebug/kselftest.c:57のincludeを確認しclamp共通とPIT側宣言を分ける。tools/ppc/selected_success.json 5、kernel/module/kdebug_imports.def 5（clamp共通、pit_get_setup :902–903のsourceを移設後のPIT宣言先へ、time_branch_reset :1030–1031・time_branch_hits :1472–1473のsourceをplatform/pc98/pit_time.hへ更新）、tools/tests/fixtures/build_profile_x86.json 各5–20|pit_clock/time_math/dep_dir_b_timer/ver_collect/dep_dir_d3_boot/stack_entry/mem_stat/p2v、＋clock_caller（sys_time_now実関数、失敗時未書込/IF復元/周波数）。変異＝retry削除/clamp迂回/復元前user書込。①host ②変異 ③生成 ④NP項1,4 ⑤known|
|E 変化：legacy口|include/legacy_guest.h 新35、arch/x86/legacy/guest.c 新70、platform/none/legacy_guest.c 新65、exec/v86_call.c→arch/x86/legacy/v86_call.c移設(全量)+10、exec/exec.c 8、exec_syscall.inc 10、exec_schedule.inc 8、exec_launch.inc 4、exec_finish.inc 20、exec/lease.c 3、console.c 12、kaudit.c 5、r1_fixture.c 5、init/kernel_boot_memory.inc 3、drivers/input/kbd.c 12、tools/tests/legacy_guest_fake.h 新60、tools/tests/fixtures/build_profile_x86.json 各5–20|v86_call/v86_module/exec_r1/shell_service/v86_gcap_abort、＋legacy_callers（抽出した起動/終了/console/lease/監査、none全slot）。変異＝pin無視/closing見落し/cleanup順逆転/none引数読取。①host ②変異 ③onとoff（none＋抽出した呼び手＋mock）のリンクfixture ④NP項2,5,6,7 ⑤known|
|F 変化：低位取引|include/legacy_lowmap.h 新45、mm/paging_lowmap.inc 新80、paging_map.inc −57、paging.c 10、paging_addrspace.inc 2、arch/x86/legacy/v86_mem.h 20・v86_mem.c 35、arch/x86/legacy/lowmap.c 新35、exec/lease.c 2、kernel/kaudit.c 2、tools/tests/fixtures/build_profile_x86.json 各5–20|v86_low/ring3_guard/ring3_str/exec_r1、＋lowmap_callers（実paging片＋所有token）。変異＝PDE片側未復元/flush前解除/残存alias/別token受理。①host ②変異 ③生成 ④NP項2,6,7 ⑤known|
|G 変化：bootinfo/native|include/bootinfo.h 180、platform/pc98/bootinfo_wire.h 新125・bootinfo_native.h 新55・bootinfo_capture.c 新80・bootinfo_geom.c 新160・bootinfo_report.c 新100、init/bootinfo.c 250、init/boot_disk.h 3、init/bootlog_save.c 10、kernel/ver_collect.c 10、ver_format.c 10、platform/pc98/boot_capture.c 新15。bootinfo_check.c全量移設、v86_snapshot.c全量をbios_snapshot.cへ＋静的保存域/ready口15、v86_mem.h −2・v86_mem.c 2、kdebug/crt_diag.c 2・kernel/module/kdebug_imports.def 4、surface_bundle_host.c:169・v86_gcap_abort_host.c:210 各10、tools/ppc/selected_success.json 5、tools/tests/fixtures/build_profile_x86.json 各5–20|bootinfo/bootlog/ver_collect/ver_format/v86_module/dep_dir_d3_boot/stack_entry/mem_stat/p2v/crt_diag/surface_bundle/v86_gcap_abort/shm_user/r1_recovery、＋boot_readers（module pair/BIOS保存/非PC98供給）。変異＝BE読取/封印無視/保存順逆転/feature offで原本欠落。①host ②変異 ③生成 ④NP項1,6 ⑤known|
|H1 不変目標：機種V86移設|platform/pc98/legacyへkbd/io/pic/biosと各h/incを全量、各include 1–5行。下記検査/build/docs一覧のH1対象各1–10行、tools/tests/fixtures/build_profile_x86.json 各5–20|kbd_status/kbd_dlog/kbd_fgpoll/dep_dir_d_display/v86_gcap/v86_gcap_abort。①正常host ②対応変異 ③通常/R1/module比較 ④known ⑤差があれば不受入|
|H2 不変目標：残る本体|arch/x86/legacyへ下記残部全量、各include 1–5行。互換ヘッダ各5行、下記H2対象各1–10行、tools/tests/fixtures/build_profile_x86.json 各5–20|v86_low/v86_call/v86_module/r1_fixtureと下記全検査。①正常host ②対応変異 ③通常/R1/module比較 ④known ⑤差があれば不受入|
- 全段の結線変更：`build/arch/x86-sources.mk`、`build/platform/pc98.mk`、`build/features/v86.mk`、`build/sources/order.mk`、`build/select.mk` 各段各5–25行（必要な所有者だけ）。試験のCHECK inputs/fixture copiesと変異anchorは各既存試験5–30行、新規試験は各120–250行。生成物を手修正しない。
- H1/H2の各移設先：arch/x86/legacyへ `v86.c,v86.h,v86_entry.asm,v86_mem.c,v86_mem.h,v86_gp_emulate.inc,v86_test16.asm,v86_test16_disk.asm`。platform/pc98/legacyへ `v86_bios.c,v86_bios.h,v86_bios_fdd_read.inc,v86_bios_fdd_command.inc,v86_bios_sasi.inc,v86_pic.c,v86_pic.h,v86_kbd.c,v86_kbd.h,v86_io.c,v86_io.h`。
- H2のmodule結線 `v86_body.c,v86_module.c,v86_module.h,v86_gcap.c,v86_gcap.h,v86_gcap_math.h` はarch/x86/legacyへ全量（PC98 bodyは機種選択）。`v86_snapshot.c` はGで移設済み。kernel/v86の旧ヘッダは転送のみ。SDK/本線のパスを壊さない。
- 移設元の実測行数（rgによる空行込み）：v86.c 881、v86.h 259、entry.asm 75、mem.c 205、mem.h 131、gp_emulate.inc 87、test16.asm 86、test16_disk.asm 45；bios.c 491、bios.h 165、fdd_read.inc 251、fdd_command.inc 309、sasi.inc 226、pic.c 236、pic.h 74、kbd.c 145、kbd.h 54、io.c 428、io.h 48（各名のv86_接頭辞を省略）。
- 同：body.c 31、module.c 201、module.h 39、gcap.c 10、gcap.h 54、gcap_math.h 105、snapshot.c 27；exec/v86_call.c 159、init/bootinfo_check.c 277、pit_math.h 68、time_math.c 76、time_math.h 75。大きな移設量を新規ロジック行数と合算しない。
- A/H1/H2の比較は基点/変更後でSOURCE_DATE_EPOCH・build_id・feature・リンク順を固定、通常/R1/moduleの対応.oの実行節/再配置と最終ELF load bytes・vmkernel.lz4・module payloadを照合。パス由来の差も黙って除外しない。不一致は移設の修正か独立した「変化」段へ戻す。
- 比較道具は `tools/build_compare.py --all`（:93–112でbuild/out再帰の.o/.elf/.mod等とR1必須像、:232でall/kernel-r1）。moduleも比較対象だが両側で無いmoduleは検出しないのでpaired module生成一覧も照合。移設.oの旧新path対応は比較器へ明示登録し実行節/再配置を比較、最終像は既存厳密比較を維持する（比較器変更約20行）。
- host実行形は `python3 -B tools/tests/test_<表の名前>.py`、`--mutate` は対応試験だけ（irq_math/pit_clock/time_math/ne2000_dma等）。ring3_str/ver_formatは通常実行のみ。各段の②は同一入力の未変異GREENを正常対照に、新規表記の変異がREDであることを確認する。新規には両入口とCHECK inputsを設ける。各実関数抽出fixtureは新ヘッダ/移設先を読むよう更新し、呼び手をmockそのものへ置換して検証を失わない。

- host台帳：`rg -l v86 tools/tests/*_host.c` の全44本（以下は `_host.c` を省略）、各5–25行＋親testのCHECK/コピー更新各5–20行。Eの `legacy_guest_fake.h` は状態/返値/呼出履歴を共通化、各fixtureの関数重複定義を除去する。割当は初回修正段、後続移設時も再検証。
- host B（4本）：dma_boot, font_guard, r1_fixture, x87_lifecycle。dma_bootとx87_lifecycleはEでも再修正（dma_boot_host.c:71–79のmodule load fake、x87_lifecycle_host.c:45–46,102–103,183のsession/cleanup fake）。
- host D（1本）：dep_dir_b_timer。
- host E（26本）：caller_access, con_sink, db_caller, dep_dir_d_console_grid, dep_dir_d_emergency, display_cleanup, e10c_gcap, exec_image, kapi_audit, kapi_ranges, kbd_dlog, kcg_body, kcg_display, kdebug_init, mem_stat, multiapp_impl, r1_recovery, shm_user, trim_flow, trim_kernel, trim_stop, v86_call, v86_gcap, v86_module, x11_park, x11_shared。trim_stop:29/caller_access:60/db_caller:18,86/exec_image:145の直fakeを共通化。
- host F（4本）：dep_dir_c_memory, memmap_audit, memory_boot, v86_low。
- host G（8本）：boot_splash_native, crt_diag, dep_dir_d_display, gfx_kernel_fb, pegc_mode, splash_probe, surface_bundle, v86_gcap_abort。surface_bundle:210のservice fakeもEで先行し、Gでは:169の原本fieldをgetterへ。
- host Gの再修正：shm_user_host.c:146,415,432とr1_recovery_host.c:294のreal_saved読者をplatform ready口へ変更（初回Eの割当は維持）。
- host Eの追加台帳（44本の*_host.cとは別）：tools/tests/gf_wc_recovery_host.inc:90–95、gfinit_output_host.h:17、v86_gp_host.incのfakeと抽出実体、test_kstop.py:66–67、test_exec_r1.py:75–77,488の埋め込みfake。legacy_guest_fake.hとの結線、親CHECK/コピーを更新し、実GP decoderの検証を維持する。
- host H1（1本）：kbd_status。
- Hの検査台帳は `kernel/v86/` を含むtestsのpy/c 35本＋checks 10本＝45本。H1対象でも本体参照が残ればH2で再更新。kbd_status_host.c:22の.c include、test_dep_dir_d_displayのFILESも新実体へ向ける。
- H1検査（19本、各1–10行）：checks/dep-dir-b-host.py, checks/docs-links.py, checks/memmap-audit-host.py, checks/mkmod-host.py, checks/shm-user-host.py, tests/kbd_status_host.c, tests/test_dep_dir_a_l04.py, tests/test_dep_dir_b_timer.py, tests/test_dep_dir_d_display.py, tests/test_display_cleanup.py, tests/test_kbd_dlog.py, tests/test_kbd_fgpoll.py, tests/test_kbd_status.py, tests/test_kdebug_imports.py, tests/test_splash_probe.py, tests/test_splash_raster_diag.py, tests/test_v86_gcap_abort.py, tests/test_v86_low.py, tests/test_v86_module.py。
- H2検査（26本、各1–10行）：checks/access-host.py, checks/double-fault-host.py, checks/h3b-fixtures-host.py, checks/memory-host.py, checks/r1-fixture-host.py, tests/kdebug_init_host.c, tests/r1_recovery.py, tests/test_build_selection.py, tests/test_ci_stab.py, tests/test_crt_diag.py, tests/test_dep_dir_a_l05a.py, tests/test_dep_dir_a_l06.py, tests/test_dep_dir_a_l08.py, tests/test_dep_dir_a_l09.py, tests/test_dep_dir_a_l10.py, tests/test_dep_dir_d_emergency.py, tests/test_exec_hwb_abort.py, tests/test_h3b_fixtures.py, tests/test_kdebug_ctx.py, tests/test_ledger.py, tests/test_module_policy.py, tests/test_r1_fixture.py, tests/test_shm_user.py, tests/test_surface_bundle.py, tests/test_v86_call.py, tests/test_v86_gcap.py。
- H2で `tools/tests/test_build_selection.py:32` の `not any('kernel/v86/' in name ...)` を `arch/x86/legacy/` と `platform/pc98/legacy/` も含めた非搭載検査へ更新する。
- H補助入力（6本、各1–10行）：fixtures/build_profile_x86.json, fixtures/dep_dir_c/manifest.json, fixtures/dep_dir_d/emergency.json, k5b_kernel_tdd.md, kbd_status_tdd.md, t8_tdd.md。H1で機種部、H2で残部。buildはfeatures/v86.mk・sources/order.mk・v86-module.mk・kdebug.mkの4本（各5–25行）、module規則/ASM include/生成器入力を更新。tools/prototypes/v86_imports.def・check_p2v_allow.txt・check_docs_links.py・ppc/check_accept.pyも同順、known一覧は実.d確認後に更新。
- docs更新先20本（各1–10行、H1で機種部・H2で残部の参照パスのみ更新）：05_drivers.md, 09_exec.md, KAPI_SPEC.md, POLICY_DEBUG.md, WORK_GUIDE.md, decisions/ppc/PPC_MMU_DESIGN.md, decisions/ppc/PPC_SEP1_DESIGN.md, decisions/sound/SYNTH_OPNA_DESIGN.md, design/README.md, design/kdebug/recover_profile.md, design/kernel/README.md, design/kernel/console/console.md, design/kernel/v86/README.md, design/kernel/v86/v86.md, tasks/portability/ARM_GAUGE.md, tasks/v3/RUST_VS_C11.md, tasks/v3/TASK_REDUNDANCY.md, tasks/v3/TASK_SRC_LAYOUT.md, tasks/v3/TASK_SYNTH_OPNA.md, tasks/v3/TASK_T7_AND_FOLLOWUPS.md。

### D/E/G の移設検査
- D：`tools/tests/test_recover.py:6`、`test_dep_dir_b_pic.py:23`、`tools/checks/{dep-dir-b,mkmod,double-fault,ver-info}-host.py:3` のCHECK inputs/コピー/旧PIT参照を移設先へ更新。`tools/tests/test_kdebug_imports.py:104` のtime_math.o/time_decide/time_branch_hits検査もpit_time.oへ追従し、共通clampの検査を保つ。
- E：`tools/tests/test_v86_low.py` の実exec/入力呼び手抽出とlegacy fake、新ヘッダのCHECK/コピーを更新する。
- G：`tools/tests/test_shm_user.py:148` のsnapshot関数抽出と静的保存域をbios_snapshot.cへ、`test_v86_low.py:120` の読取元も更新。`test_dep_dir_d3_disk.py:10,41–42` と `tools/checks/dep-dir-d3-host.py:3` のbootinfo分割後のCHECK/コピー/抽出を追従させる。各検査はHまで先送りしない。

## 4. PMのNP21/W受入手順（8項、B–Gは表の該当項を毎段実施）
- 起動順の字面検査 `test_dep_dir_d3_boot.py:50–66` / `kernel_boot_source.py`、`test_stack_entry.py:53`、`test_mem_stat.py:165`、`test_p2v.py:240` はB/D/Gで抽出範囲・新口へ更新（各5–20行）。順序の意味保証は新boot_orderへ移し、元の保証を削除しない。
- 準備：PMの受入用checkoutで `make kernel programs`（fixture時 `make kernel-r1`）、隔離媒体へ既存deploy経路を使う。NP21/Wを停止して `make deploy-kernel`、新kernelとpaired modules/userlandの同一世代を配備して再起動。R1は必ずt2h専用：stop→umount→pull --profile t2h→`make deploy-kernel-r1 PROFILE=t2h`（copy）→deploy --profile t2h→start (`build/deploy.mk:108–112`)。通常用deploy-kernelをR1に使わない。これらは本設計作業では実行しない。
- ring3の長名は受入用媒体に8.3別名 `r3hello.bin/r3fault.bin/r3guard.bin` で配置する（既定配置名と仮定しない）。time_testは `timetest.bin`。PMが媒体内容と実行CRCを記録する。
|#|打つコマンド/操作|期待と証拠|
|---|---|---|
|1 起動|再起動→`ver`→`mem`|GDT/IDT/PIC/PIT/CPUの表示順、PIT 1.9968M、自己試験fail=0、shell応答。verのPIC/PIT表示とboot image CRC、module pair、build IDを基点と照合|
|2 例外復帰|`r3hello`→`r3fault`→`ver`→`r3guard`→`ver`→`r3guard bb`→`ver`|hello DONE/kill+0、faultはPF addr=KERNEL_LOAD_ADDR/kill+1、guardは対象guard/kill+1、bbは0x6A000/kill+1。markerのARMDとSURV無しを確認（userland/tests/ring3_marker.h:7–45）。guard実配置も確認|
|3 #GP/#UDと停止|R1像でfixture要求を設定後、GUIアプリを動かしGUIポンプのsyscallへ入る（exec/exec_abort.inc:175）：`emu_write_mem addr=font_guard_fixture_request data=0d000000`。別起動でR1 V86 fault/endの既存arm手順を実施|font #GPは診断停止・自動reset無し、復帰と判定しない。V86開始前#UDは通常回収、closing中再入は停止。停止例ごとに再起動。hostの注入だけでguest合格としない|
|4 時計|`timetest`→`ver`、ホスト時計で120秒後 `ver`、`timetest ro`|PASS/fails=0、uptime差約120秒（同一emu速度・停止時間除外）、巻戻り無し、roの不正出力は既存保護処理でshellへ戻る。clamp回数の増分とtimer自己試験を記録|
|5 入力/IRQ|`ver`の入力を連続反復、backspace、CTRL+STOP、GUI起動→マウス移動→終了→`ver`|キー欠落/固着無し、STOP後に入力可能、マウス/時計継続。人工spurious/共有2巡/stormはNP21で再現したと称さずCのhostログを添付|
|6 V86往復|CUIで `v86 -t` を2回→`v86 -g -t`→`ver`→`mem`|selftest成功、ゲストIRQ/IO trapを記録、CUI/カーソル/キー/時計復帰、pin/session解除、low PTE/PDEと監査失敗増分0、所有頁残留0。必要なmodule有りの像で実施|
|7 異常復帰|R1のV86開始前fault/終了再入、font request=14を別起動で実施、通常像へ戻し `v86 -t`→`r3hello`→`ver`|r1_fixtureの既存条件に沿いone-shot消費、割込み深さ/IF/所有rootと回収状態を確認。font PF/closing再入は停止、それ以外は所定のshell復帰|
|8 終了|通常像で `ver`→`mem`、serialログ/marker/カウンタ/CRCと配備条件を保存|基点との機能差無し。各段のhost・変異結果を添付。feature offは既存compile-onlyなので新hostリンクfixtureでNOSYSを受入し、未対応製品の起動成功とは書かない|
- emu操作の引数はPMの接続ツールに合わせる。上のdataはLE32値13の例。R1のarm値/slot/シンボルはその像のkernel.mapとr1_fixture.hから取り、通常像へアドレスを流用しない。
- 現行R1の具体値（kernel/r1_fixture.h:7–11）：開始前faultは `emu_write_mem addr=r1_fixture_arm+12 data=58463152` → `v86 -t`、終了再入は別起動で `emu_write_mem addr=r1_fixture_arm+16 data=58463152` → `v86 -t`。各々armが0へ消費されたことを読む。font PFは `emu_write_mem addr=font_guard_fixture_request data=0e000000` 後にGUIアプリを動かしてGUIポンプを通す。
- 項6の採取：`mm/paging.c:198–199` の `page_directory` / `page_tables` はstaticでkernel.mapに無いため、同じ像の `nm build/out/kernel.elf` から各シンボルのアドレスを取得し、ポインタ値を `emu_read_mem` で読み、開始直前と復帰後のPDE[0]/PT0全対象PTEを保存比較。active rootはsession snapshotのroot、復元検査は `v86_restore_mismatch` とsession内lowmap.restore_pte_bad/restore_pde_badを各u32で読む（F前は直下field）。`mem` の `leftover` / `audit_fail` 行 (`userland/shell/cmd_sys.c:71–75`) を前後比較、`ledger_owners` の該当owner.pages (`mm/pgalloc.h:161,198`) とowner-exit serialで回収を確認。常駐module頁は基点として残し、guest所有頁だけ残留0を要求する。
- `timetest ro` は明示検査によるkillが正解（userland/tests/time_test.c:115–124）。`6 **NOT KILLED**` が出たら失敗。#PF発生そのものを要求しない（CR0.WP=0）。

## 5. known依存・第3段/PPCへの引渡し
|20組中の解消対象（source→header、間接includeも含む）|段と確認|
|---|---|
|mm/memory_boot.c→platform_memory_desc、mm/paging.c→platform_memory_desc（2）|Fに併設する独立F2：include/platform_memory.h:23を固定名includeへ（1行）、platform/pc98は同値、platform/powermac/platform_memory_desc.hをF2で最小実装（新25行、同じgeometry/attributes定義、UC空区間、PC98 include無し、暫定値の実走禁止）。tools/tests/fixtures/build_profile_x86.jsonもF2のTU追加に追従。host欄：test_memory_boot.py:103–105、test_mem_stat.py:173–176の相対include用コピーを固定名include/選択providerへ変更し、実装時に確認。mock providerでPC98ヘッダ非依存を試験。PC98 .o/ELF機械語不変比較を受入条件とする|
|init/bootlog_save.c、kernel/ver_collect.c、kernel/ver_format.c→各gdt.h/idt.h（6）|A/C/Dでtick/diag宣言へ。ver_formatのPIC offset直参照もsnapshotへ。単なるinclude削除では不可|
|kernel/irq/irq.c、kernel/time/ktime.c→各gdt.h/idt.h（4）|C/D。idt.hがgdt.hを引く間接2組も消える|
|exec/ring3_str.c、mm/paging.c→x86_flags.h（2）|A。PTE定数の移設不要、cpu.h由来の依存を切る|
|kernel/kaudit.c、mm/paging.c→v86_mem.h（2）|E/F。lowmap型を共通化し、feature onでもmmがV86 sessionを直接読まない|
|残る4組|kernel/gui.c→IME3ヘッダ、kernel/sys.c→geforce_module.hは第5段。今回除外理由はV3/A5、既知一覧から消して隠さない|
- 合計16組を解消目標、残4組。`known_header_dependencies.json` は段ごとに必ず更新し、実.dで消失を確認した組だけ減らす（消失0組の段も測定結果を記録）。Aはbootlog_save→gdt/idtの2組とx86_flagsの2組の計4組、F2はplatform_memory_descの2組を減らす。Cのver_collect→gdt/idtは実.dに従い、Dまで残る依存をCで先に削除しない。選択からTUを落として解消扱いにしない。実装時 `tools/ppc/check_compile.py --source-profile selected --accept --json` の測定と新mock host依存試験を併記、PPC全リンクは本段の条件でない。
- F2は受入①provider host ②誤include変異 ③x86機械語比較 ④known2組確認の4項。mm本体接続の前提はF/F2/A完了、F2の暫定PPC providerを実RAM/機器範囲で確定、RAM BAT撤去と案DのHPT/flush/guard受入（MMU設計参照）。表形式の変更を混ぜない。
- B実測（2026-11-07）：CR0操作の分離で `init/kernel.c` がselected compile成功へ移った。既存known 16組は消失0、同TUの既存ヘッダ依存25組が初めて実.dで観測可能となり、knownは41組。選択TUを除外せずselected_successへ追加し、G以降の分離で解消を追う（コンパイル成功はPPC起動可能を意味しない）。
- D実測（2026-11-07）：selected compile の実 .d で `kernel/time/ktime.c` → gdt.h/idt.h の2組消失を確認。known は35→33組、selected成功は88 TU（PC98 pit_math を機種所有へ移し、共通 tick.c を追加）。PPC全リンク・起動は未確認。
- E実測（2026-11-07）：legacy口への置換でkaudit→v86_memの1組、init/kernel.cのmodule宣言をBIOS原本宣言へ絞ったことで7組（kbd/kbd_status、v86/v86_io/v86_kbd/v86_module/v86_pic）が実.dから消失。knownは33→25組、selected成功はnone providerを含む89 TU。R1のsession.open照会は契約に口が無いため既存読取を保持（active/pin/lowmapとの同一視はしない）。PPC全リンク・起動は未確認。
- 第3段へ：legacy_trapのnative frame境界、例外分類とexec復帰の順序、pin/closing/busy契約、ABI_WORD_*とABI_STACK_ALIGNの暫定配置、syscall5予約slot、KAPI未変更の証拠、ring3 markerの基点を渡す。汎用frame/context/ユーザ遷移ABIはここで先取りしない。
- PPCへ：boot各段、kernel_tick、clock_source、IRQ topology/claim/complete、native boot情報、none legacyを渡す。DECはCPU例外、OpenPICは機種controllerであり同一IRQ線に押し込まない。DEC再arm後のtick更新/共通handler順は後段で定義する。
- BSD根拠（ローカル実物、GPL未参照）：N=`/home/hight/os32-tmp/ref/netbsd`、O=`/home/hight/os32-tmp/ref/openbsd`。`N/sys/arch/powerpc/powerpc/clock.c:103–116` timebase周波数/hzからDEC設定、`:139–159` 遅延分を考慮した再arm、`:300` counter周波数。OS tickとcounterを分ける手本。
- `O/sys/arch/macppc/dev/openpic.c:235` 実線数、`:299` spurious=255、`:619–622` IACK/255終了、`:566–568` EOI、`:660–666` このBSDではhandler前ack。OS32で一律「全controllerがhandler後EOI」と一般化せず、backendがack時期を所有しcompleteは必要な後処理一度だけ。この段ではOpenPICを実装しない。

## 6. 未決と反論
- ユーザー判断が必要な未決：0件。案D、今着手、エミュレータ受入をそのまま採用。実装レビューで決められるファイル細分/内部名/試験mockは承認待ちにしない。
- 反論 R8：読取元で `rg -l v86 tools/tests/*_host.c` は39本でなく44本（コメント含む、全件を上記台帳へ割当）；レビューの39本を特定する検索条件は示されていないため、広い集合で漏れを防ぐ。

## 確定 (2026-11-07、Opus 5.5 第 2 回の条件付き可を反映)
- R8：*_host.c全44本の台帳と反論を採用。
- S1：Aのhost_arch flags include、shm_user試験と関連py 22本の実装時確認を追加。
- S2：CでPC98/Powermac両方の固定名IRQ容量ヘッダと選択includeを供給。
- S3：Eのinc/h/埋め込みfake台帳とdma_boot/x87_lifecycleの再修正を追加。
- S4：Gにshm_user/r1_recoveryのreal_saved読者を追加。
- S5：§3にD/E/Gの移設検査を追加。
- S6：dispatchの-1は呼び手がring3_fault_killする契約へ修正。
- S7：staticなpage_directory/page_tablesの取得元をnm build/out/kernel.elfへ修正。
- S8：Gのselected_success、B〜H2/F2のbuild_profile、各段のknown削減確認を追加。
- S9：pit_time.c/.hとv86_call.cの移設先、import source、IRQ検証失敗時停止を確定。
- S10：F2にmemory_boot/mem_statの相対include用コピー修正を追加。
- S11：H2のfeature off検査にarch/x86/legacyとplatform/pc98/legacyを追加。
- 段 E の実装で埋めた欠け：R1の独立open照会 `int legacy_session_open(void)` を§2へ追加（x86はsession.openの読取、noneは0）。
- 前提：第1段wt/ppc-sep1の上に積み、mainへ入ったSHAを基点とする。
- 段 G の実装で埋めた欠け：native image 情報の供給口 `void boot_common_set(const struct boot_common *info, u8 source)` を `include/bootinfo.h` に追加（PC98 parser が capture 時に一度供給、PPC/none はゼロ情報と source=0）。`[boot] src=` と BootImageInfo.source の供給元を保つため。wire/BIOS 情報は共通ヘッダに入れず、BootImageInfo・KAPI・SDK の ABI は不変（PM 判断 2026-11-08）。
