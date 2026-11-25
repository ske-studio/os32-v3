# PPC_SEP3_DESIGN — 分離の第 3 段 (文脈・起動引数・syscall)

> 状態: **完了記録 (2026-11-08 確定、2026-11-17 に記録へ)** — 第 3 段は main に入った。分離の仕組みは 11-17 の作り直しで置き換わり、今の決まりは票 TASK_PPC_SEP の「現在地」(層の検査 `tools/check_layers.py`・所有者集合 `build/select.mk`・入口の骨格 `sys/kern/init/kernel_start.c`)。本文の selected の計測と known の表は今は無い。以下は確定した時の本文: 共通 C が x86 の文脈・フレーム・呼出規約を直接知っている所 (E1〜E7・A1・A2) を arch の口の後ろへ移す。x86 の KAPI・SDK・CRT の ABI は変えない。段の状態は票 TASK_PPC_SEP が持つ。
> 出所: ~/os32-tmp/run/ppc-sep3/DESIGN.final.md を 2026-11-08 に写した (gpt-6-astra 起草 r1〜r2、Opus 5.5 第 1・2 回、PM が N1〜N3 を反映)。
## r1→r2 の変更
- R1：legacy→caller→abort/pump→private LS→範囲→guard→legacy/print/strings→ptrmask→invoke→出口をコード化。
- R2：checks の covers と C/H/Python の参照を再検索し、同段担当へ割当。
- R3：D2 に legacy_guest_fake.h と全参照元を追加。
- R4：frame/jmp/invoke/resume/entry の host 追従を分担し、KSETJMP_BUF_LEN は撤去。
- R5：A に PPC ABI 定数 provider と selected の実 .d による storage 到達検査を追加。
- R6：新 x86 実装を arch/x86/kernel/ に統一。
- R7：生成器分割の Make 前提・covers、shim clean パスを追加。
- R8：layout マクロ・LS 公開ヘッダを維持し、runtime layout と静的保証を接続。
- R9：dispatcher/view・引数 source 完全型・layout 欄を確定。
- R10：固定引数を read_block で一括取得し、単語ごとのページ歩きを撤去。
- R11：E1 の通常/R1 リンク順を保持。
- R12：「syscall5」は予約 slot 範囲という PM 解釈を確定。
- 基点：`6ae60fc103503871dcc4076f0fb99410332abc5b`（HEAD 実確認）。読取元 `/home/hight/wt/os32-v3/ppc-sep3d`。本票だけが設計成果物。ビルド・試験・preflight は依頼に従い未実施、リポジトリ無変更。
- 目的：E1〜E7/A1/A2 の CPU ABI 解釈を arch へ移す。x86 の KAPI 番号・並び・cdecl・int80・CRT 4 引数入口・公開配置は不変。PPC 実装、MMU 表形式変更、apps/game 再開は含めない。案D、整数カーネル、単一タスク協調切替を維持。
- 正典：票 TASK_PPC_SEP の順 (3) と「ABI と完了条件」、PPC_SEP2_DESIGN (アーカイブ: `docs/archive/decisions/PPC_SEP2_DESIGN.md`) §5 の「第3段へ」、`arch/README.md`。以下のパスは特記以外リポジトリ相対。変更行数は移動の削除/追加を含む概算、実測でない。
## 1. 実物との対応と引渡し
|項目|現 HEAD の根拠（調査時の行番号は追わず実位置を使用）|実装対象|
|---|---|---|
|E1|`sys/kern/exec/exec_launch.inc` argc/argv/api/aux、16B整列、ダミーret、ID別saved_esp|入口の配置・遷移を分離。argv文字列の作成とAS所有は共通|
|E2|`sys/kern/exec/exec.c` gdt/tss、ESP0 snapshot、TSS引数付きresume|privileged snapshotと復帰、親文脈の寿命を分離|
|E3|`sys/kern/exec/appslot.h` 13語/添字、`sys/kern/exec/exec_syscall.inc`、`sys/kern/exec/exec_schedule.inc`|保存結果操作、再実行。追加漏れは `sys/kern/exec/exec_stdin.inc` の `exec_stdin_state()` のslot照会|
|E4|`sys/kern/exec/exec_tramp_init.inc` B8/LE32/CD80/C3、`sys/kern/exec/ring3_str.h` の `KAPI_STUB_SIZE` 8Bと写し場offset|容量・配置・emit・命令同期。予約関数にもstubを生成|
|E5|`sys/kern/exec/exec_syscall.inc` USER ESP+4とptrmask、`sys/arch/x86/kernel/ring3_entry.asm` memcpy/call|引数抽出とABI呼出。`sdk/gen_kapi.py` 固定引数数×4も分離|
|E6|`exec/ring3_ls.S:3,17–92` .code32、`sys/kern/exec/ring3_ls.c` の `ring3_ls_dispatch()` ESP+4|shimをarchへ。値コピー/FS処理は共通|
|E7|`sys/include/ksetjmp.h` 8語、`sys/arch/x86/kernel/setjmp.asm` 深さも保存|opaque型・archサイズ。`sys/kern/exec/exec_finish.inc` コピーも追従|
|A1|`sys/kapi/kapi_print.c` の `kapi_print_call()` fixed2語後のu32列|USER可変引数cursor。内部kvprintf用の検証済み列とは区別|
|A2|`sys/include/types.h`、`sdk/include/os32/os32_kapi_shared.h`|公開単独利用も可能な呼出属性の口|
- E1〜E7/A1/A2 の指摘内容は9件とも存続（内容の食い違い0）。共通CPU定数は現在 `sys/include/cpu.h`、VM判別は `sys/platform/pc98/legacy/guest.c` に移っている。
- `legacy_trap(native_frame)` は **arch syscall入口で最初に**呼ぶ。VMの拡張フレームを52BのUSER保存型にコピーしない。VM処理済みなら共通dispatcherを呼ばず戻る。
- 例外分類はx86に残す：`isr_stub.asm` のVM分岐、`isr_handlers.c` のfont/sqlite優先→USERまたはsyscall→kill、通常kernel例外→exec復帰/停止。例外フレームとsyscallフレームを同型にしない。
- 親snapshotはlaunch/resumeの生存スタック上でsetjmp前に一度保存、volatile/noinline維持。dispatchの短命スタックに退避しない。着地clear→pending cleanup→x87親復元→親snapshot復元→IF許可の既存順を維持（`exec_schedule.inc`）。
- `legacy_pinned` と `legacy_lowmap_busy` の入口拒否、closing中の解放禁止はそのまま。active/open/pin/busy/closingを統合しない。V86呼出中のpark拒否・end→cleanup・IF条件の試験を残す。
- 引渡しの「syscall5 予約 slot」は予約 slot 範囲の意と PM が解釈。`KAPI_FUNC_COUNT..KAPI_FUNC_CAPACITY-1` の予約stub→範囲拒否を受入対象にし、slot 5 を特別扱いしない。
- 食い違い②：`userland/diag/ring3_marker.h` はPF error_code非表示と記すが、`isr_handlers.c` は `err=` を出す。markerの値/kill差/実配置を正として受入し、コメント追従を最後の記録段へ。SURVEY指摘との食い違いに加えた現行コードと資料コメントの食い違いはこの1件。
- KAPI未変更の証拠は基点から採取：kapi.json、生成public宣言/slot/generations、KernelAPI sizeof/offset、SDK CRT入口/リンク成果物を比較保存。既存の「未変更」記述だけを証拠にしない。
## 2. 境界と所有（以下で境界型と順序を固定）
```c
/* sys/include/exec_arch.h: 容量/整列のみarch_exec_storage.hから選択 */
struct arch_syscall { void *native; }; /* archだけが解釈、入口の生存中だけ借用 */
void exec_syscall_dispatch(struct arch_syscall *);
/* 定義は sys/kern/exec/exec.c の static (今の sys/kern/exec/exec.c と同じ置き場)。ヘッダには置かない */
/* static struct arch_syscall *g_cur_frame;  共通execが親viewを保存・復元 */
struct arch_user_context;            /* 完全型はopaque storage。AppSlotが値を所有 */
struct arch_priv_context;            /* 親Ring3CallContextが所有 */
struct arch_entry_context;           /* 初回入口用、AppSlotが所有 */
struct exec_entry_args { u32 argc, argv, api, aux; };
int arch_entry_prepare(struct arch_entry_context *, u32 top, u32 entry,
                       int user, const struct exec_entry_args *,
                       int (*store)(void *, u32, const void *, u32), void *owner);
void arch_entry_enter(struct arch_entry_context *, u32 address_space);
void arch_priv_save(volatile struct arch_priv_context *);
void arch_priv_restore(const volatile struct arch_priv_context *); /* IRQ禁止中 */
void arch_user_save(struct arch_user_context *, const struct arch_syscall *);
u32 arch_syscall_slot(const struct arch_syscall *);
void arch_syscall_result(struct arch_syscall *, u32);
void arch_user_result(struct arch_user_context *, u32);
void arch_user_restart(struct arch_user_context *); /* 一回のresumeにつき一回 */
void arch_user_resume(const struct arch_user_context *, u32 address_space);
```
- 完全型は `sys/include/exec_arch.h` のbyte storage＋provider指定alignment。x86 user=52B/align4、priv=4B/align4で現配置を維持。commonは添字・SP・CS・VM・TSS・命令長を読まない。native viewの変換/borrowはarch入口のみ。
- x86置場：`sys/arch/x86/kernel/exec_entry.c`（配置とenter）、`exec_context.c`（snapshot/save/result/restart/resume）、`syscall_entry.c`（legacy先行・native view→共通dispatch）。`ring3_entry.asm` のint80/popad/iret/kapi_invokeはarch内で維持。
- `arch_entry_prepare` はchecked subtraction/alignとstore失敗を返し、commit前に完了。x86 USERは現20B、CPL0は16Bを書き、auxとapi選択は共通入力。CPL0の正常return/longjmp両方を保持。ESP退避はentry context内、ID別・ネスト別に保持。
- `arch_entry_enter` はUSERの場合noreturn、CPL0は戻り得る。arch内のCLI→ESP0→CR3→セグメント→iretを一続きにする。共通にTSS pointerを渡さない。既存のargv/store→CR3→heap→x87→commit→entry順は変更しない。
- parkはopaque contextを値コピー→x87/heap保存→caller clear→kernel AS→park commit→親復元→longjmp。g_cur_frameはborrow viewに置換し通常出口で親viewを戻す。保存済みAppSlotにはview pointerを残さない。
- resumeはKEEPなら結果変更なし、YIELD=0、POLL=文字/-1、KBD=take、stdin=peekのみ。stdinはslot番号と引数を保存したままx86 EIPを2戻す。commit→poison abort検査→exec復元→restart→atomic resume。PPCはSRR0を4戻せる口だが今回は未実装。
```c
/* sys/include/kapi_call.h: redir_access.hをincludeしcaller_access完全型を使用 */
struct kapi_arg_source { const struct arch_syscall *call;
    struct caller_access caller; unsigned next; int caller_valid; }; /* 完全型。borrowは呼出中のみ */
struct kapi_values { u32 value[KAPI_FIXED_MAX]; }; /* 検証済み論理値、wireでない */
void arch_kapi_source(struct kapi_arg_source *, const struct arch_syscall *); /* 呼出の間だけ有効 */
int arch_kapi_read_block(struct kapi_arg_source *, unsigned first,
                         unsigned count, struct kapi_values *out);
u32 arch_kapi_invoke(void *wrapper, const struct kapi_values *, unsigned count);
int arch_kapi_va_next(struct kapi_arg_source *, enum kapi_value_kind, u32 *value);
/* sys/include/syscall_code.h: codeとstring scratch/LS shimの区間を別々に返す */
struct syscall_layout { u32 stub_off, stub_stride, stub_count;
    u32 scratch_off, scratch_bytes, ls_off, ls_bytes, total_bytes; };
int arch_syscall_layout(unsigned capacity, struct syscall_layout *);
int arch_syscall_emit(void *dst, u32 user_base, u32 bytes, unsigned capacity);
void arch_syscall_publish(void *dst, u32 bytes); /* 全命令書込後、公開前 */
```
- source初期化はnext=0/caller_valid=0。初回readで `caller_access_get_user(&src->caller)` を呼び、失敗をread失敗として返す。read_blockは固定分を一度だけ `copy_caller_bytes` で取得し、count上限・加算/乗算overflowを先に拒否。x86はESP+4+first*4の連続範囲、PPC後段はr3〜r10＋stackを同じ論理valuesへ集める。read_block成功時だけnext=first+count、va_next成功時だけnext++。va_nextだけ要求語を逐次取得し未使用語を先読みしない。
- `KAPI_FIXED_MAX` はJSONの現在最大固定引数数から生成し、private LSの3語も収まることをassert。整数/ポインタの引数種別・数・ptrmaskは共通metadata、nativeの位置/slot幅はarch。未知のby-value aggregate/64bit/FPU型を生成器で拒否し、勝手に4Bと数えない。可変引数はkprintf専用処理へ。
- `sdk/gen_kapi.py` はJSON検証・slot/public wrapper生成を維持し、同ファイル内 のx86サイズ計算を新 `sdk/kapi_abi_x86.py` へ分離。初回は既存kapi_argsize出力をbyte一致で保つ。次に内部 `kapi_call_generated.h`（種別/数）を追加生成。target選択は既存profile経由、PPC選択は未対応として明示拒否。
- x86呼出実体は `sys/arch/x86/kernel/kapi_call.c` から既存 `kapi_invoke` ASMへ。以下は最終形（D1は旧処理をarch adapterで維持、D2/D3/E3で順次置換）。resultはローカルu32とsetterを使い、wrapperのout検証を維持する。
```c
/* sys/arch/x86/kernel/syscall_entry.c: ASMが呼ぶ既存cdecl symbol */
void __cdecl ring3_syscall_dispatch(u32 *native) {
    if (legacy_trap(native)) return; /* VM拡張frameはここで完結 */
    struct arch_syscall call = { native }; exec_syscall_dispatch(&call);
}
/* sys/kern/exec/exec_syscall.inc: 順序は現行56–174行に対応 */
void exec_syscall_dispatch(struct arch_syscall *call) {
    u32 slot = arch_syscall_slot(call), result, wrapptr;
    struct arch_syscall *prev_frame = g_cur_frame;
    int prev_in_syscall = ring3_in_syscall; CallerAccessFrame prev_caller;
    struct kapi_arg_source src; struct kapi_values values;
    if (!caller_access_enter(&prev_caller, CALLER_USER)) {
        ring3_caller_reject_count++; ring3_fault_kill(); return;
    }
    g_cur_frame = call; ring3_wm_depth = 0; ring3_wm_shell_gen = 0;
    if (g_cur_app) g_cur_app->last_kernel_tick = tick_count;
    exec_cui_display_abort(); ring3_abort_check(); ring3_gui_pump();
    /* source初期化は借用の設定だけ。引数メモリはまだ読まない */
    arch_kapi_source(&src, call);
    if (slot == RING3_LS_CALL) {
        ring3_in_syscall = 1;
        if (!arch_kapi_read_block(&src, 0, 3, &values)) ring3_fault_kill();
        result = (u32)ring3_ls_dispatch(&values); goto result_ready;
    }
    if (slot >= (u32)KAPI_FUNC_COUNT) ring3_fault_kill();
    wrapptr = ((const u32 *)KAPI_ADDR)[KAPI_FN_TABLE_FIRST + slot];
    unsigned count = kapi_fixed_count[slot]; ring3_in_syscall = 1;
    /* legacy/print/stringsは選択判定後にのみread_block、未選択なら読まない */
    int handled = legacy_call_dispatch(slot, (void *)wrapptr, &src, count, &result);
    if (handled < 0) ring3_fault_kill(); if (handled) goto result_ready;
    if (slot == KAPI_SLOT_KPRINTF) {
        if (!kapi_print_call(&src)) ring3_fault_kill(); result = 0; goto result_ready;
    }
    handled = kapi_strings_from_source(slot, (void *)wrapptr, &src, count, kapi_argptr[slot], &result);
    if (handled < 0) ring3_fault_kill(); if (handled) goto result_ready;
    if (!arch_kapi_read_block(&src, 0, count, &values)) ring3_fault_kill();
    for (unsigned k = 0; k < count && k < sizeof(u16) * BITS_PER_BYTE; k++)
        if ((kapi_argptr[slot] & (u16)(1u << k)) && !ring3_ptr_ok(values.value[k])) ring3_fault_kill();
    result = arch_kapi_invoke((void *)wrapptr, &values, count);
result_ready:
    arch_syscall_result(call, result); caller_access_leave(&prev_caller);
    ring3_in_syscall = prev_in_syscall; g_cur_frame = prev_frame;
    exec_cui_display_abort();
    if (geforce_fg_due(tick_count)) geforce_foreground_poll();
    ring3_abort_check(); exec_park_stop(call);
}
```
- D2のlegacy口は `int legacy_call_dispatch(u32 slot, void *fn, struct kapi_arg_source *src, unsigned count, u32 *result)` とする。providerは対象判定→既存busy/CUI/admit/mailbox/caller/pin→read_block→values処理→cleanup。先にvaluesを作るとbusy＋不正引数の結果が変わるため遅延取得し、旧raw/bytes口は撤去。strings adapterも対象判別後に一括取得、未対象0/失敗負/完了正を維持。
- 切替を小さくするため旧raw経路を一時的にx86 adapterへ閉じ込め、strings/legacy/printを順にvalues/cursorへ接続し、最後にraw口を撤去。移行中もcommonはnative引数番地を解釈しない。
- `sys/kapi/kapi_strings.c` はvaluesを受け、文字列の範囲検証・コピー・解放を維持しarch invokeへ。`sys/platform/pc98/legacy/v86_call.c` はpin→checked copy→呼出→cleanupの現契約を維持するadapter。`sys/include/legacy_guest.h` のcall口もsourceからgate後に論理valuesを取得する形に同期し、none providerを追従。
- `kapi_print` はfixed2引数（色/format）をread、formatをstageし、rendererが消費する型についてのみva_next。14引数上限・format1024・string4096・NULL%s・失敗時一文字も出さず全解放を維持。cursorはUSER境界、`kvprintf` のu32列は検証済み整数/ポインタ値の内部境界。builtin va_listをUSERメモリだとして直接使用しない。
- stub backendは `sys/arch/x86/kernel/syscall_code.c`。8B stub、予約stub、LS192B、現string scratch位置を同値で返す。全区間の重複/overflowと合計PAGE_SIZE以内を検査。x86 publishはno-op、PPC後段はD-cache writeback/I-cache invalidate/orderが必要な実装口。共通はemit成功→publish→USER table利用の順。
- LS：`exec/ring3_ls.S`→`sys/arch/x86/kernel/ring3_ls.S` に同命令列で移設。`sys/kern/exec/ring3_ls.c` をvalues(path,skip,out)で受ける。公開slot12のcallback拒否とprivate RING3_LS_CALLは維持。FSがunwindした後CPL3でcallback、nested listing/park/killでもkernel cursorを残さない。
```c
/* sys/include/ksetjmp.h: arch_jmp_storage.hは容量と整列だけを供給 */
typedef struct { unsigned char opaque[ARCH_JMP_BYTES]; }
    __attribute__((aligned(ARCH_JMP_ALIGN))) k_jmpbuf[1];
int exec_setjmp(k_jmpbuf);
void exec_longjmp(k_jmpbuf);          /* 常に1で着地。IFは復元しない */
void k_jmp_copy(k_jmpbuf dst, const k_jmpbuf src);
```
- `KSETJMP_BUF_LEN` はBで撤去し全利用者をk_jmpbufへ変更。x86は32B/align4、JB_*オフセットと深さ2語をarchでassert。既存ASMを変更不要とする。AppSlot・V86 session・g_exit_jmpbufを型置換し、終了前コピーもhelperへ。共通に余分なPPC最大配列を置かず、PPCの容量は後段でABIのcallee-save＋IRQ/EXC深さから決める。returns_twice追加は今回混ぜず、既存volatile規則を保持。
- A2：新 `sdk/include/os32/os32_callconv.h` を公開の唯一の属性定義口にし、i386は現cdecl、他targetは通常C ABIを表す（未対応targetのビルド拒否はprofile側）。CPU条件分岐はこの新しい口だけ。types.h/shared.hはこれをinclude、SDK単独GNU89でも使える。共通ABI_WORD_*は新 `sys/include/abi.h`→`arch_abi.h` 固定名に移し、x86=word bytes4/align4/stack align16（WORD_BITS=32）。Aで`sys/arch/ppc/arch_abi.h`も同定数（ABI_WORD_BYTES=4、ABI_WORD_BITS=32、ABI_WORD_ALIGN=4、ABI_STACK_ALIGN=16）を供給し「暫定・PPC実走禁止」と記す。ASM入口の定数も同値assert。
- shared.h自身は `gen_kapi.py` の書出先でない。shared.hのinclude編集と、生成宣言/metadata変更は区別し、生成物は必ず生成器から作る。PPC用のpublic宣言を手書き複製しない。
## 3. BSD の形との照合（ローカルのBSD実装だけを参照）
- N=`/home/hight/os32-tmp/ref/netbsd`、O=`/home/hight/os32-tmp/ref/openbsd`、F=`/home/hight/os32-tmp/ref/freebsd`。
- `N/sys/arch/powerpc/powerpc/syscall.c:52–54,96–98,125–149`：r3〜r10＋stack、結果r3/r4、ERESTARTはSRR0-=4。slot入力と結果/再開を別操作にした根拠。BSDのerrno/CR規約をOS32へコピーしない。
- `N/sys/arch/powerpc/powerpc/setjmp.S:8–31,35–67,73–104`：r1/r14〜31/LR/CR/CTR/XERの保存。x86の8語を汎用固定サイズにできない根拠。OS32の深さ保存を追加する必要がある。FPU保存やBSD signalsは導入しない。
- `O/sys/arch/powerpc/include/_types.h:125` はbuiltin_va_list、`F/sys/powerpc/include/stdarg.h:31–35` はsys/_stdarg.hへの委譲。固定USER u32列でva_listを代用しない根拠。保存域配置自体はこのヘッダで確定できないため、PPC実装時にcompiler/CRTのABIと合わせてcursor backendを決める（今回PPC実装済みとは扱わない）。
## 4. 実装段（各行1担当、半日〜1日、共有ファイルは直列）
- I=不変目標：build_id/日時固定の対応.oのtext/rodata/relocation、ELF load section、vmkernel.lz4、R1を基点と比較。単なるsymbol番地正規化で不一致を隠さない。不一致ならC扱いへ切替、NP21/W受入後に次へ。
- C=変化：公開ABIは不変でも機械語は変わる。PMがCI成果物をNP21/Wで下表N項目受入後に次段を積む。全段KAPI番号/世代/CRT入口不変、外部未ビルド（H-7）。SDKはAでヘッダのみ変化、Dで内部生成物追加、他段は無変更。
|段/依存|主ファイルと変更規模（新規は+、補助ファイルは§5）|区分/生成物|受入（§6）|
|---|---|---|---|
|A / 基点|types.h/shared.h各5、cpu.h8、abi/callconv/provider計+95（PPC含む）|I。SDK属性展開/機械語不変|H1、境界host、変異stdcall化、selected実.d確認|
|B / A|ksetjmp.h25、appslot.h4、exec_finish.inc10、v86_mem.h4、provider/helper+40|C（型変更で最適化影響を受入）、ASMは一致|H2、N1/N2/N8|
|C1 / A,B|exec.c40、exec_schedule.inc30、exec_syscall.inc25、exec_stdin.inc4、exec_context.c+110、契約+70|C。frame/snapshot/restartを一体切替|H3/H4、N1/N2/N4/N8|
|C2 / C1|exec_launch.inc90、exec_entry.c+110、契約追加20|C。初回引数/遷移|H5、N1/N2/N8|
|D0 / A|gen_kapi.py35、kapi_abi_x86.py+40、生成器試験40|I。既存生成物byte一致|H6、既存gen/layout/out試験|
|D1 / C1,D0|exec_syscall.inc70、syscall_entry.c+40、kapi_call.c+100、kapi_call.h+60、gen_kapi.py40|C。slot/result/read/invokeと旧経路adapter|H6/H7、N1/N3/N4/N8|
|D2 / D1|kapi_strings.c35、v86_call.c25、legacy口/provider各10、legacy_guest_fake.h10|C。strings/legacyをvalues化（legacyはgate後にsource読取り）|H7、N3/N5/N8、V86往復|
|D3 / D2|kapi_print.c35、kapi_print.h8、kapi_call.c40、旧adapter撤去20|C。va cursor接続|H8、N3/N4/N5/N8|
|E1 / A|ring3_ls.S移設±94、source選択/fixture計20|I。shim byte一致、R1_OBJECTS/ASM_EXTRA_OBJのリンク順は変えない|H9、header_boundary/kcallback|
|E2 / D3,E1|exec_tramp_init.inc45、ring3_str.h20、syscall_code.c+100、syscall_code.h+40|C。layout/emit/publish|H10、N1/N3/N6/N8|
|E3 / E2|ring3_ls.c20、exec_syscall.inc15、x86 private arg adapter20|C。callback引数抽出切替|H9、N3/N6/N8|
|F / 全段|正典・受入台本・known測定/fixture追従各10〜40|I（製品コードは触らない）|全N、H11、KAPI/SDK/CRT比較、容量記録|
- 各段の試験追従は同担当が同段で実施（追加50〜150行/段、既存追従は§7）。新試験にはCHECK inputs、移設製品行のmutationを `rg -n` で全検索して移し、一意anchor・runtime REDを要求する。
- 実装者は変更したkernel/R1と該当userlandをビルド、触った試験の--mutateとlint。PMのlandingはCI check/build、全mutationはfull-check。check-changedを回さない。この設計作業ではいずれも未実施。
## 5. ファイル別台帳と容量
- 新規：`include/{abi.h,exec_arch.h,kapi_call.h,syscall_code.h}`=15/100/60/40行、`sdk/include/os32/os32_callconv.h`=20、`arch/x86/{arch_abi.h,arch_exec_storage.h,arch_jmp_storage.h}`=15/25/10。
- 新規：`arch/x86/kernel/{exec_entry.c,exec_context.c,kapi_call.c,syscall_code.c}`=110/110/140/100、`sys/arch/x86/kernel/syscall_entry.c`=40、`sdk/kapi_abi_x86.py`=40。移設：ring3_ls.S=94。新規`sys/arch/ppc/arch_abi.h`=15行（A）。新内部生成 `sys/include/kapi_call_generated.h` は関数数比例（数百行）。
- 変更：`include/{cpu.h,types.h,ksetjmp.h,kapi_print.h,legacy_guest.h}`=8/5/25/8/10、`sdk/include/os32/os32_kapi_shared.h`=5、`sdk/gen_kapi.py`=75。
- 変更：`exec/{exec.c,appslot.h,exec_launch.inc,exec_syscall.inc,exec_schedule.inc,exec_stdin.inc,exec_finish.inc,exec_tramp_init.inc,ring3_str.h,ring3_ls.c}`=40/15/90/110/30/4/10/45/20/20。
- 変更：`kapi/{kapi_print.c,kapi_strings.c}`=35/35、`arch/x86/legacy/{v86_mem.h,v86_call.c,guest.c}`=4/25/10、`sys/platform/none/legacy_guest.c`=10。`arch/x86/kernel/{ring3_entry.asm,setjmp.asm}`は必要な型/定数コメント・入口symbol接続各0〜10、命令は極力維持。
- 変更：`build/kernel.mk`=5〜10（D0で生成規則の前提にsdk/kapi_abi_x86.py、D1で新生成物を列挙、E1でcleanのshim objectパスを移設先へ）。
- 変更：`build/arch/x86-sources.mk,build/sources/order.mk,build/kernel-objects.mk` 各5〜15（通常/R1 GASに同じinclude選択）、`tools/tests/fixtures/build_profile_x86.json`10、`arch/README.md`20、正典exec/KAPI文書各20。新host providerのヘッダ/関数を `tools/tests/host_arch/` に追加（約100）。
- `sys/kern/exec/ring3_ls.c` は現在 `build/arch/x86-sources.mk` のC_ARCHに入る。E3で共通所有へ移し、GASだけをx86に残す（共通ソース所有リストも約5行追従）。RING3_LS_CALLの値は変えない。新backendをsource listだけに置いて未リンクのまま閉じない。
- E2：`KAPI_STUB_SIZE/RING3_USTR_STUB_OFF/RING3_USTR_OFF/RING3_USTR_CAP/RING3_LS_SHIM_OFF`はコンパイル時マクロを維持。layoutは同マクロから構成し区間・定数をSTATIC_ASSERT、hostで返値との一致を検証。`sys/arch/x86/kdebug/selftest_exec.c`・kcallback/ring3_str hostのbyte観測を維持。LS宣言は`sys/kern/exec/ring3_ls.h`に残すので`kdebug_imports.def:2479–2490`のsource/slotは不変。
- A受入：selected 89 TUの実.dを採取し、特にsys/kern/exec/ring3_str.c・sys/kern/mm/paging.cからarch_abi.hへ到達すること、jmp/exec storageへ到達しないことを確認。到達すればAで最小PPC容量/整列providerを追加（実走禁止、実装を装うstubは禁止）。起草環境に既存.dはなく未検証。
- 新x86 C/GASは全てarch/x86/kernel/に置き、tools/source_roots.pyのp2v/arm_compile走査内。SCAN_ROOTS追加なし。
- 製品ソースは概ね1,300〜1,800変更行（元の600〜1,400見積りより周辺adapter分が増える）。試験追従/新試験は計700〜1,300行別枠、生成物行数も別枠。必要以上の全テスト書換えはしない。
- kernel常駐textは増分0〜4KiB、metadata/一時storageは0〜2KiBを予算とする（未計測）。opaque user/jmp/privはx86で現52/32/4Bを保持、追加entry/values stackは各128B以内を目安にframe sizeを測る。heap allocation/parkごとの割当は増やさない。
- 通常/R1ともlink mapのresident end・BSS・SHM基点、圧縮kernelとFD空き量を段ごと比較。R1のimageも変わり、起動FDの内容/圧縮サイズに影響するがloader/FD配置の変更は設計しない。stub pageは既存1ページ・LS192Bを厳守。溢れたら黙って容量/ABIを変えず段を止める。
- known21組：GUI→IME3、sys→geforce1、init/kernel→17。exec由来の組は現在ゼロ。sys/platform/pc98/kernel.cのidt（gdt間接）2組はinclude整理候補だが本段の契約切替には不要なので削減目標0、残21とする。double_fault/PC98boot/各module/IMEは後段。selected TUを除外して成功にしない。
- Fで `tools/ppc/check_compile.py --source-profile selected --accept --json` と実.dを記録し、実際に消えた組だけknownから減らす。新たにコンパイル可能になったexecの依存も記録。PPC provider未実装の失敗を隠す空実装は作らない。本体build/起動/ユーザ実行の到達点は別管理。
## 6. 受入 → host/NP21/W → 変異（各項一行）
|項目|観測/試験|代表変異（hostでruntime RED、guestは通常像で確認）|
|---|---|---|
|H1 属性/定数|新ABI host＋test_header_boundary/test_shell_service、GNU89 SDK単独、4/4/16・public offset比較|cdecl→stdcallで呼出前後SP不一致、整列16→4は非整列top入力でRED|
|H2 setjmp|test_ledger/test_stack_entry/test_exec_r1：深さ/非volatile reg/guard/IFを実ASMで確認|深さrestore一行削除・末尾越境|
|H3 保存/親復帰|test_exec_r1/test_kstop/test_x87_lifecycle：nested子→親のcaller/WM/priv値、STOP結果保持|priv restore省略、KEEPの結果上書き|
|H4 stdin再実行|test_exec_r1：非消費peek、引数/slot保持、再parkごと一回restart|EIP-=2削除、peek→take、slot結果上書き|
|H5 初回入口|test_exec_image/test_shell_service＋新entry host：argc0/複数、aux、api、CPL0/USER、store失敗|dummy ret省略、aux入替、underflow拒否削除|
|H6 生成/呼出|test_gen_kapi_cli/test_kapi_layout/test_kapi_out＋新kapi ABI host：0〜最大固定数/型/順/return|引数順交換、×4計算破壊、out検証省略|
|H7 保護/legacy|test_kapi_ranges/test_kapi_strings/test_v86_call/test_legacy_callers：未map末尾・overflow・pin/busy/closing|copy失敗無視、ptrmask除去、pin gate削除|
|H7 注記|汎用経路で USER の引数域が写っていないときは copy の失敗で FAULT kill (今は kapi_invoke 内の #PF で `[ring3] #PF ... err=` のログを出して FAULT kill。r2 以後は PF のログが出ない。kill 種別は同じ FAULT で、今との差として記録)|—|
|H8 可変引数|test_kapi_print/test_kprintf_window：%d/%x/%s/%%、NULL、14上限、末尾境界、無部分出力|先読み1語追加、cursor位置+4、失敗後emit|
|H9 callback|test_kcallback/test_kapi_strings：実shim実行・nested・private値3引数・FS後USER callback|private skip語交換、kernel callback呼出、公開slot guard削除|
|H10 stub|新code host＋test_exec_selftest：全capacityの8B比較・区間・publish順、reserved kill|CD80破壊、予約emit省略、publish呼出省略（mock記録）|
|H11 非依存|実.d/selected測定＋共通CのAPP_FRAME/EIP/ESP0/cdecl引数歩き残存検索|共通にx86専用includeを再導入して境界検査RED|
|N1 起動/終了|C各該当段：通常/R1起動、shell/USER/CPL0/nestedのargc/argv/api/aux、nop/hello DONE、終了後ver|H5のargc/aux破壊がREDであること|
|N2 park/resume|B/C1/C2：wait/kbd/poll/yield/STOP全起源、親WM復帰・入力継続・結果保持|H3の親priv/KEEP破壊|
|N3 syscall|D1以降：通常/0引数/最大引数、無効slot・予約slotで子だけkill、ver継続|H6/H10の順交換/予約stub欠落、private LSを範囲検査後へ移す変異、STOP保留＋無効slotのabort順破壊|
|N4 sys_read|C1/D1/D3：入力無しでpark→1byte注入→read一回で同byte、再park→再開、空ならAGAIN|H4のrewind/peek破壊|
|N5 表示|D2/D3：可変引数の符号・16進・文字列・%%、境界不正は部分出力無し、正常呼出継続|H8のcursorずれ/部分emit|
|N6 sys_ls|E2/E3：複数batch、nested callback、callback内park/kill、後続ls/FS正常|H9のkernel callback/private語交換|
|N8 kill/復帰|B以後全C段：fault/CTRL+STOP/parked kill、親とver生存、資源戻り。r3fault/guard/bbはARMD・SURV無し・kill+1、helloはDONE/+0|H2/H3/H7の深さ/復帰/gate削除|
- markerはその像のmapからSHMを計算しtag/target/ARMD/DONE/SURV/kill差を照合。Aのguard実配置、bb=0x6A000を確認。PFログのerrも記録する。R1のpoison abortはUSER PDロード前、V86往復はpin/open/closingと回収後IFを追加観測。物理実機は要求しない。
## 7. host試験の移設影響一覧（削除して通さない）
- 検索：`rg -n 'exec_launch\.inc|exec\.c|appslot\.h|exec_syscall\.inc|exec_schedule\.inc|exec_tramp_init\.inc|ring3_ls\.[Sch]|ksetjmp\.h|setjmp\.asm|ring3_entry\.asm|kapi_print\.c|types\.h|os32_kapi_shared\.h|gen_kapi\.py' tools/tests`。CHECKだけの広めの候補も除外せず下に全Pythonヒットを列挙、exec_source利用/明示mirrorも補完。行番号は基点、略名は `tools/tests/` 相対。
- 機構の要所：`exec_source.py` のinc合成、`test_exec_image.py` の実TUと独自gcc、`test_exec_selftest.py` の実TU/GAS、`test_exec_r1.py` の関数抽出＋本物ASM、`test_shell_service.py` の字面/入口/独自-I。
- 写しの木：`test_cd_read.py` TREE_FILES、`test_hdd_stage1.py` MIRRORとshared単独コピー、`test_hdd_stage2.py` MIRROR、`test_pci_decode.py` MIRROR、`test_e82557_drv.py` MIRROR、`test_gfx_attach.py` HEADERS、`test_dep_dir_d_{console,display,splash}.py:27` FILES、`test_access_walk.py` FILES/独自gcc。新abi/callconv/providerも同じtree構造でコピーする。
- `kernel_includes.py` は実buildの順序を使うが、独自-I/単独コピーを自動修復しない。host_archの選択、SDK単独copy、ASM用includeを別に追従。test_ledgerの8語字面はarch専用layout試験へ移し、共通はopaqueの前後canary/深さ復元を検証する。
- native layoutを模造する `test_exec_r1.py` と `test_kstop.py` は、新arch providerをリンクする。製品関数を古い複製に置換しない。`r1_recovery.py` の移設anchor、`test_kapi_{ranges,strings,print}` の切出開始/終端も移す。
- 下記一覧は影響点の母集団。呼出規約ヘッダのみの候補は依存コピー確認、抽出/字面/ASMの候補は同段で実行確認する。通常のroot include利用だけなら製品と同時追従で足りる。CHECK宣言だけの一致を「試験が実際に新backendを通った」証拠にしない。
- `exec_source.py` / `r1_recovery.py` / `test_allocator_bench.py` / `test_app_bb_overlap.py` / `test_appmem.py` / `test_appmem_map.py` / `test_b8_hostdrv.py` / `test_b8_open.py` / `test_boot_readers.py` / `test_bootinfo.py` / `test_c_dialect.py` / `test_caller_access.py` / `test_cd_module.py` / `test_cd_read.py` / `test_check_result_cache.py` / `test_ci_stab.py` / `test_cirrus_module.py` / `test_d2_services.py` / `test_db_caller.py` / `test_db_errstr.py` / `test_debug_probe.py` / `test_dep_dir_a_l01.py` / `test_dep_dir_a_l02.py` / `test_dep_dir_a_l04.py`
- `test_dep_dir_a_l05a.py` / `test_dep_dir_a_l05b.py` / `test_dep_dir_a_l06.py` / `test_dep_dir_a_l08.py` / `test_dep_dir_a_l09.py` / `test_dep_dir_a_l10.py` / `test_dep_dir_b_io.py` / `test_dep_dir_b_pic.py` / `test_dep_dir_b_timer.py` / `test_dep_dir_d3_boot.py` / `test_dep_dir_d3_disk.py` / `test_dep_dir_d_console.py` / `test_dep_dir_d_display.py` / `test_dep_dir_d_splash.py` / `test_disk_auth.py` / `test_display_cleanup.py` / `test_double_fault.py` の `check_asm_constants()` / `test_e10c_lifecycle.py` / `test_e82557_drv.py` / `test_e82557_eeprom.py` / `test_e9_observation.py` の `marker()` / `test_exec_budget.py` / `test_exec_fgpoll.py` / `test_exec_gui_run.py` の `run()`
- `test_exec_hwb_abort.py` / `test_exec_image.py` / `test_exec_r1.py` / `test_exec_selftest.py` / `test_exec_syscall_fgpoll.py` / `test_fep_core_boot.py` / `test_fep_meta.py` / `test_fep_store_info.py` / `test_fep_store_search.py` / `test_fm_module.py` / `test_font_guard.py` / `test_font_source.py` / `test_fstat_redir.py` の `check_single_source()` / `test_geforce.py` / `test_geforce_fgpoll.py` / `test_gen_kapi_cli.py` / `test_gf1_dispatch.py` / `test_gf1_engine_stop.py` / `test_gf1_foundation.py` / `test_gf1_irq.py` / `test_gf4_probes.py` / `test_gf5.py` / `test_gf5_boot_target_diag.py` / `test_gf5_critical_service.py`
- `test_gf5_display_lifetime.py` / `test_gf5_fancheck.py` / `test_gf5_gpio_diag.py` / `test_gf5_observe.py` / `test_gf5_rom_guard.py` / `test_gf5_service_live.py` / `test_gf5_therm_t1.py` / `test_gf_accel.py` / `test_gf_command.py` / `test_gf_cui.py` / `test_gf_display.py` / `test_gf_objwait.py` / `test_gf_runner.py` の `mutation()` / `test_gfinit_exec.py` の `main()` / `test_gfx_attach.py` / `test_guest_acceptance.py` の `setUp()` / `test_gui_button_dispatch.py` / `test_gui_desc.py` / `test_h3_park_resume.py` / `test_h5_shapes.py` / `test_hdd_stage1.py` の `build_target()` / `test_hdd_stage2.py` / `test_header_boundary.py` / `test_host_mailbox.py`
- `test_host_notice.py` / `test_hostsvc_block.py` / `test_hostsvc_block_contract.py` / `test_hostsvc_boot.py` / `test_hostsvc_contracts.py` / `test_hostsvc_e82557.py` / `test_hostsvc_image.py` / `test_hostsvc_init.py` / `test_hostsvc_lgy98.py` / `test_hostsvc_lgy98_attach.py` / `test_hostsvc_link_api.py` / `test_hostsvc_link_core.py` / `test_hostsvc_netcd.py` / `test_hostsvc_nic.py` / `test_hostsvc_nic_rx.py` / `test_hostsvc_open.py` / `test_hostsvc_persistent.py` / `test_hostsvc_sdk.py` / `test_hostsvc_serialfs.py` / `test_hostsvc_sfs_deadline.py` / `test_hostsvc_sfs_lan.py` / `test_hsync_fep_protect.py` / `test_hsync_h3.py` の `check_kapi_slot()` / `test_hwb_access.py`
- `test_hwb_exec.py` / `test_hwb_parse.py` / `test_hwb_scripts.py` / `test_hwb_shell.py` / `test_hwb_wire.py` / `test_hwdev_image.py` / `test_kapi_audit.py` / `test_kapi_layout.py` / `test_kapi_out.py` / `test_kapi_print.py` / `test_kapi_ranges.py` / `test_kapi_strings.py` / `test_kcallback.py` / `test_kcg_module.py` / `test_kdebug_imports.py` の `test_address_manifest_uses_only_linked_moved_sources()` / `test_kdebug_init.py` / `test_kdebug_probe.py` / `test_kernel_shell.py` / `test_kpeek.py` / `test_kpeek_cli.py` / `test_kprintf_attr.py` / `test_kprintf_va.py` / `test_kprintf_window.py` / `test_kstop.py`
- `test_kstring_c.py:54,110` / `test_lan_module.py:7` / `test_lanfs.py:37` / `test_ledger.py:44,45` / `test_legacy_callers.py:36` / `test_link_notify.py:19,22` / `test_lowmap_callers.py:3` / `test_mem_stat.py:8,11` / `test_module_image.py:5` / `test_module_runtime.py:5` / `test_mtrr_wc.py:5` / `test_multiapp_impl.py:18` / `test_nano_adapter.py:6,9` / `test_ne2000_dma.py:3` / `test_net_link.py:17,141` / `test_np21w_gui.py:461,462` / `test_np21w_symbols.py:6` / `test_osri_view.py:15` / `test_owner_reclaim.py:62` / `test_pci_bind.py:19` / `test_pci_decode.py:115` / `test_pci_hwb.py:6` / `test_pcm_module.py:5` / `test_pgalloc_model.py:32`
- `test_physmem.py` / `test_platform_memory.py` / `test_public_surface.py` / `test_r1_fixture.py` / `test_raw_wait.py` / `test_recover.py` / `test_recover_gf.py` / `test_ring3_guard.py` / `test_serial_vfast.py` / `test_serialfs.py` の `session_build()` / `test_shell_service.py` / `test_shm_user.py` / `test_splash_probe.py` / `test_splash_raster.py` / `test_splash_raster_diag.py` / `test_splash_v2.py` / `test_splash_wire.py` / `test_sqlite_engine.py` / `test_sqlite_module_proto.py` / `test_stack_entry.py` / `test_surface_bundle.py` / `test_surface_lease.py` / `test_tick.py` / `test_time_math.py`
- `test_tri_burst.py` / `test_tri_burst6.py` / `test_tri_w32.py` / `test_trident_contracts.py` / `test_trident_emergency.py` / `test_trident_lifecycle.py` / `test_trident_mode.py` / `test_trident_module.py` / `test_trident_pc98init.py` / `test_trident_present.py` / `test_trident_regs.py` / `test_trident_resources.py` / `test_trident_restore.py` / `test_trident_route.py` / `test_trident_save.py` / `test_trident_vram.py` / `test_trim_back_rs.py` / `test_trim_flow.py` / `test_trim_kernel.py` / `test_tvram_auth.py` / `test_unicode_lookup.py` / `test_unicode_surface.py` / `test_v86_call.py` / `test_v86_gcap.py`
- `test_v86_gcap_abort.py` / `test_v86_low.py` / `test_v86_module.py` / `test_ver_accept.py` / `test_ver_api.py` / `test_ver_collect.py` / `test_ver_layout.py` / `test_ver_shell.py` / `test_ver_version.py` / `test_vfs_excl.py` の `check_flag_constant()` / `test_x11_shared.py` / `test_x87_lifecycle.py` / `ver_test_utils.py`
### r2追加検索と担当（略名はtools/tests/、checksはtools/checks/）
- 検索はrg -n、tools/tests・tools/checksの*.c/*.h/*.pyと*.inc、host_arch全体。参照パスだけでなくg_cur_frame/APP_FRAME_/frame[7,11]/exec_setjmp/exec_longjmp/g_exit_jmpbuf/KSETJMP_BUF_LEN/kapi_invoke/ring3_resume/arch_enter_user/arch_call_on_stackを検索。以下は全ヒットのファイル列挙（行は先頭3件まで）。既存§7のPython母集団は維持し、types/shared/cpuコピーはA、jmpはB、frame/exec抽出はC1、entryはC2、generatorはD0、callはD1、legacy/stringsはD2、printはD3、LS GASはE1、layoutはE2、LS CはE3の担当が追従する。
- B担当の型・偽物・抽出・字面：`exec_image_host.c`、`font_guard_host.c` の `ring3_context_clear()`、`gf_wc_recovery_host.inc`、`legacy_callers_host.c` の `v86_call_dispatch()`、`r1_fixture_host.c`、`r1_recovery.py`、`r1_recovery_host.c`、`raw_wait_late_stop_host.c`、`shm_user_host.cほか`、`test_double_fault.py` の `check_asm_constants()`、`test_exec_r1.pyほか`、`test_h3_park_resume.py (Tests())ほか`、`test_kstop.pyほか`、`test_ledger.pyほか`、`test_stack_entry.py`、`v86_gcap_abort_host.c`、`x11_park_host.c`
- C1担当の型・偽物・抽出・字面：`appmem_map_host.c`、`caller_access_host.c (geforce_foreground_poll())ほか`、`db_caller_host.c`、`font_guard_host.c` の `geforce_foreground_poll()`、`gf5_foreground_host.cほか`、`kapi_print_host.c` の `dispatch_print()`、`kapi_strings_host.c` の `dispatch_strings()`、`multiapp_impl_host.c (ma_resume_kbd())ほか`、`r1_recovery_host.c`、`shell_service_host.c`、`test_double_fault.py` の `check_entry_layout()`、`test_exec_fgpoll.pyほか`、`test_exec_r1.pyほか`、`test_exec_syscall_fgpoll.pyほか`、`test_kapi_ranges.py` の `run()`、`test_kstop.pyほか`、`test_x11_shared.pyほか`、`x11_park_host.cほか`
- C1（resume）/D1（invoke）担当の型・偽物・抽出・字面：`appmem_map_host.c`、`caller_access_host.c`、`db_caller_host.c` の `kapi_invoke()`、`kapi_strings_host.c`、`r1_recovery_host.c`、`test_exec_r1.py`、`test_kapi_ranges.py` の `run()`、`test_kapi_strings.py`、`test_kstop.py`、`v86_call_host.c`、`x11_park_host.c`
- C2担当の型・偽物・抽出・字面：`host_arch/arch_cpu.h:66,72`、`test_exec_r1.py`、`x87_lifecycle_host.cほか`
- D2担当：`legacy_guest_fake.h`を上記source/count口へ同期し、gate前の読取りが無いことをfakeで観測。参照元全57ファイル（直接includeとPythonのコピー宣言を区別して確認）：`caller_access_host.c`、`con_sink_host.c`、`db_caller_host.c`、`dep_dir_d_console_grid_host.c`、`dma_boot_host.c`、`exec_image_host.c`、`font_guard_host.c`、`gf_wc_recovery_host.inc`、`gfinit_output_host.h`、`gfx_kernel_fb_host.c`、`kapi_audit_host.c`、`kbd_dlog_host.c`、`pegc_mode_host.c`、`r1_fixture_host.c`、`r1_recovery_host.c`、`shm_user_host.c`、`test_caller_access.py`、`test_ci_stab.py`、`test_cirrus_module.py`、`test_db_errstr.py`、`test_dep_dir_a_l01.py`、`test_dep_dir_a_l04.py`、`test_dep_dir_a_l05a.py`、`test_dep_dir_a_l06.py`、`test_dep_dir_a_l08.py`、`test_dep_dir_a_l09.py`、`test_dep_dir_a_l10.py`、`test_dep_dir_d_console.py`、`test_dma_pool.py`、`test_exec_r1.py`、`test_geforce.pyほか`、`test_gf_command.py`、`test_gf_display.py`、`test_ime_display.py`、`test_kbd_dlog.py`、`test_kcg_module.py`、`test_kstop.py`、`test_launch.py`、`test_legacy_callers.py`、`test_lowmap_callers.py`、`test_pegc_mode.py`、`test_raw_wait.py`、`test_trident_emergency.py`、`test_trim_flow.py`、`test_trim_kernel.py`、`test_unicode_surface.py`、`test_v86_call.py`、`test_v86_gcap_abort.py`、`test_v86_low.py`、`trim_flow_host.c`、`trim_kernel_host.c`、`trim_stop_host.c`、`v86_gcap_abort_host.c`、`v86_low_host.c`、`x11_park_host.c`、`x11_shared_host.c`、`x87_lifecycle_host.c`
- C/H/inc参照母集団（上記対応段が同段で担当）：`access_walk_host.c`、`allocator_bench_host.c`、`app_band_pde_host.c`、`app_bb_overlap_host.c`、`appmem_host.c`、`appmem_map_host.cほか`、`b8_hostdrv_host.c`、`b8_open_host.c`、`boot_splash_native_host.c` の `irq_save()`、`caller_access_host.c`、`cirrus_win_host.c`、`clock_caller_host.c`、`con_sink_host.c`、`db_caller_host.c`、`dep_dir_a_l08_host.c`、`dep_dir_a_l09_host.c`、`dep_dir_a_l10_host.c`、`dep_dir_b_timer_host.c`、`dep_dir_c_memory_host.c`、`dep_dir_d_console_grid_host.c`、`dep_dir_d_console_host.c`、`dep_dir_d_display_host.c`、`dep_dir_d_emergency_host.c`、`dep_dir_d_splash_host.c`、`device_reservation_stage_host.c`、`display_cleanup_host.c`、`e10c_gcap_host.c`、`exec_hwb_abort_host.c`、`exec_image_host.c`、`exec_selftest_host.c`、`ext2_read_bound_host.c`、`fd_redirect_d0a_host.c`、`fep_boundary_host.c`、`fep_standard_host.c`、`fm_playback_host.c`
- C/H/inc参照母集団（上記対応段が同段で担当）：`font_guard_host.c`、`font_source_host.cほか`、`fstat_redir_host.c` の `console_write()`、`gf1_dispatch_host.c`、`gf1_foundation_host.c`、`gf1_instmem_map_host.c`、`gf1_irq_host.c`、`gf4_display_fake.h`、`gf5_foreground_host.c`、`gf_cui_shell_host.c`、`gf_wc_recovery_host.inc`、`gfinit_exec_host.c`、`gfx_boot_host.c`、`gfx_kernel_fb_host.c`、`h4_backing_host.c`、`highram_stage_host.c`、`host_mailbox_host.c`、`host_notice_host.c`、`hostsvc_block_host.c`、`hostsvc_e82557_host.c`、`hostsvc_fd_host.c`、`hostsvc_lgy98_attach_host.c`、`hostsvc_lgy98_host.c`、`hostsvc_link_api_host.c`、`hostsvc_link_core_host.c`、`hostsvc_netcd_host.c`、`hostsvc_nic_host.c`、`hostsvc_nic_rx_host.c`、`hostsvc_open_host.c`、`hostsvc_persistent_host.c`、`hostsvc_serialfs_host.c`、`hostsvc_sfs_deadline_host.c`、`hostsvc_sfs_lan_host.c`、`hsync_fep_protect_host.c`、`hsync_sqlite_product_host.c`
- C/H/inc参照母集団（上記対応段が同段で担当）：`hwb_exec_host.c`、`hwb_fake.h`、`hwb_scripts_host.c`、`hwb_shell_host.c`、`hwb_wire_host.c`、`kapi_audit_host.c`、`kapi_bounds_host.c`、`kapi_db_v50_host.c`、`kbd_dlog_host.c`、`kbd_fgpoll_host.c`、`kbd_inject_host.c`、`kcallback_host.c`、`kcg_boot_host.c`、`kpeek_cli_host.c`、`kpeek_host.c`、`kstring_c_host.c`、`lanfs_host.c`、`launch_host.c`、`lease_host.c`、`legacy_callers_host.c`、`link_notify_host.c`、`lspci_cmd_host.c`、`mem_stat_host.c`、`memmap_boot_host.c`、`memory_boot_host.c`、`multiapp_impl_host.cほか`、`multiapp_model_host.c`、`ne2000_dma_host.c`、`net_link_host.cほか`、`os32x_layout_host.c`、`osri_view_host.c`、`owner_reclaim_host.c`、`paging_bounds_host.c`、`paging_device_map_host.c`、`pc98_route_host.c`
- C/H/inc参照母集団（上記対応段が同段で担当）：`pcm_cs4231_host.c:24`、`pegc_mode_host.c:28`、`pgalloc_range_host.c:2`、`public_surface_host.c:40`、`r1_fixture_host.c:3,6`、`r1_recovery_host.c:5,15,105ほか`、`raw_wait_late_stop_host.c:4,54`、`raw_wait_sql_host.c:9`、`recover_hostshim/io.h:3`、`resident_heap_host.c:4`、`ring3_guard_host.c:33,42,80ほか`、`ring3_str_host.c:25,40`、`serialfs_host.c:34,35`、`sh_status_host.c:668,761`、`shell_service_host.c:3,9,10ほか`、`shell_service_stub_host.h:2,5`、`shlib_high_host.c:10,57,62`、`shm_user_host.c:2,190`、`splash_obs_host.c:3`、`splash_probe_host.c:3`、`splash_raster_host.c:2`、`splash_v2_host.c:2`、`splash_wait_host.c:2`、`sqlite_engine_host.c:3`、`sqlite_memsys5_linux.c:7`、`stack_entry_host.c:8`、`stat_cmd_host.c:205`、`tick_host.c:3`、`tri_burst2_host.c:5`、`tri_burst3_host.c:5`、`tri_burst4_host.c:7`、`tri_burst5_host.c:7`、`tri_burst6_host.c:7`、`trident_emergency_host.c:4,5,9`、`trim_flow_host.c:6`
- C/H/inc参照母集団（上記対応段が同段で担当）：`trim_kernel_host.c`、`trim_stop_host.c`、`unicode_lookup_host.c`、`v86_call_host.c`、`v86_gcap_abort_host.c`、`v86_low_host.c`、`v86_module_host.c` の `_start()`、`vfs_fd_sqlite_host.c`、`x11_park_host.c`、`x11_shared_host.c`、`x87_lifecycle_host.c`
- A担当 covers全件：`d2-services-host.py:3`、`dep-dir-b-host.py:3`、`dep-dir-d3-host.py:3`、`double-fault-host.py:3`、`exec-image-host.py:3`、`fep-budget-host.py:3`、`gf-runner-host.py:3`、`gfinit-host.py:3`、`gshell-host.py:3`、`h2-fixtures-host.py:3`、`h3b-fixtures-host.py:3`、`kstring-c-host.py:3`、`mkmod-host.py:3`、`np21w-gui-host.py:3`、`privileged.py:3`、`recover-tables-host.py:3`、`trim-back-rs-host.py:3`、`ver-api-host.py:3`、`ver-format-host.py:3`、`ver-info-host.py:3`、`ver-services-host.py:3`、`ver-shell-host.py:3`、`ver-state-name-host.py:3`。
- B担当 covers全件：`double-fault-host.py:3`、`font-guard-host.py:3`、`guest-acceptance-host.py:3`、`kapi-audit-host.py:3`、`kapi-ranges-host.py:3`、`kbd-inject-host.py:3`、`mkmod-host.py:3`、`multiapp-model-host.py:3`、`np21w-gui-host.py:3`、`r1-fixture-host.py:3`、`trim-back-rs-host.py:3`、`vk32-crc-host.py:3`、`x11-shared-host.py:3`。
- D0担当 covers全件：`access-host.py`、`check-select-host.py`、`d2-services-host.py`、`deploy-set-host.py`、`docs-orphans.py`、`exec-image-host.py`、`font-guard-host.py`、`guest-acceptance-host.py`、`h2-fixtures-host.py`、`hdd-stage2-host.py`、`kapi-audit-host.py`、`kapi-layout-host.py`、`kapi-out.py`、`kapi-print-host.py`、`kapi-ranges-host.py`、`kapi-strings-host.py`、`kcallback-host.py`、`manifests.py`、`map.py`、`memmap-audit-host.py`、`memory-host.py`、`mkmod-host.py`、`multiapp-model-host.py`、`r1-fixture-host.py`、`ring3-guard-host.py`、`settings-protect-host.py`、`shm-user-host.py`、`sqlite-engine-host.py`、`tests-inventory.py`、`tools-host.py`、`trim-back-rs-host.py`、`tvram-auth-host.py`、`ver-api-host.py`、`ver-info-host.py`、`x11-shared-host.py`。
- D2担当 covers全件：`access-host.py`、`check-select-host.py`、`con-sink-host.py`、`exec-image-host.py`、`font-guard-host.py`、`gf-runner-host.py`、`gfinit-host.py`、`gfx-host.py`、`h3b-fixtures-host.py`、`kapi-audit-host.py`、`kapi-bounds-host.py`、`kbd-inject-host.py`、`memmap-audit-host.py`、`memory-host.py`、`r1-fixture-host.py`、`shm-user-host.py`、`trim-back-rs-host.py`、`vfs-mount-dev-host.py`、`x11-shared-host.py`、`x87-lifecycle-host.py`。
- D3担当 covers全件：`access-host.py`、`kapi-print-host.py`。
- E1担当 covers全件：`access-host.py`、`kcallback-host.py`、`mkmod-host.py`。
- E2担当 covers全件：`kapi-layout-host.py`、`kcallback-host.py`、`mkmod-host.py`。
- E1はkcallback-host.py/mkmod-host.pyの旧Sパスを移設先へ、ヘッダsourceは維持。D0は上記gen_kapi.pyのcovers全件へkapi_abi_x86.pyを追加し、tools/checks/{map,run,test_run}.pyの生成器依存・選択期待も確認。ディレクトリglobは既に新ファイルを含む場合に冗長列挙しない。A/B/C1/C2はhost_archに対応providerを用意し、x87_lifecycleのマクロ差替えを新entry backendの観測口へ移す。
- checks補完（製品C/ASM/inc名とヘッダ名の全grepヒット、上記段対応で担当。covers以外の選択fixtureも含む）：`access-host.py`、`check-select-host.py`、`d2-services-host.py`、`dep-dir-b-host.py`、`dep-dir-d3-host.py`、`deploy-set-host.py`、`docs-orphans.py`、`double-fault-host.py`、`exec-gui-run-host.py`、`exec-image-host.py`、`fep-budget-host.py`、`fep-worst-load-host.py`、`font-guard-host.py`、`gf-runner-host.py`、`gfinit-host.py`、`gshell-host.py`、`guest-acceptance-host.py`、`h2-fixtures-host.py`、`h3b-fixtures-host.py`、`hdd-stage2-host.py`、`kapi-audit-host.py`、`kapi-layout-host.py`、`kapi-out.py`、`kapi-print-host.py`、`kapi-ranges-host.py`、`kapi-strings-host.py`、`kbd-inject-host.py`、`kcallback-host.py`、`kstring-c-host.py`、`manifests.py`、`map.py`、`memmap-audit-host.py`、`memory-host.py`、`mkmod-host.py`、`multiapp-model-host.py`、`np21w-gui-host.py`、`osri-view-host.py`、`privileged.py`、`r1-fixture-host.py`、`recover-tables-host.py`、`ring3-guard-host.py`、`run.py`、`settings-protect-host.py`、`shm-user-host.py`、`sqlite-engine-host.py`、`test_run.py`、`tests-inventory.py`、`tools-host.py`、`trim-back-rs-host.py`、`tvram-auth-host.py`、`ver-api-host.py`、`ver-format-host.py`、`ver-info-host.py`、`ver-services-host.py`、`ver-shell-host.py`、`ver-state-name-host.py`、`vk32-crc-host.py`、`x11-shared-host.py`、`x87-lifecycle-host.py`。
## 8. 未決と完了条件
- ユーザー判断が必要な未決：0件。x86 ABIを変える必要は現時点で見つかっていない。技術確認（圧縮容量、PPC後段のva保存域）は実装/PMの確認事項で、ユーザー決裁にしない。
- x86公開ABI不変を満たせないと判明した場合だけ別提案へ：sdk/kapi.json末尾追加・版上げ・生成器再生成・clean→all・SDK/CRT同段変更を提示して判断を求める。現票の完了として扱わない。外部再ビルドはH-7再開後、それまでは外部未ビルド。
- 完了：各C段NP21/Wと各I段比較の証拠、触ったhost/変異/lint、CI check/build、実.d、通常/R1/FD容量、KAPI/SDK/CRT不変が揃う。独立OpusレビューはPMの次工程。本票は実装/試験済み報告ではない。
- 反論 R3（件数のみ）：指定範囲のlegacy_guest_fake.h参照は57ファイル、うち直接includeは26（.inc含む）で「58本がinclude」とは一致しない。影響追従の要求は採用。
- 反論 R4（分類のみ）：multiapp_impl_host.cはg_cur_frameの自前定義ではなくAppSlot.frameのAPP_FRAME_EAX操作（329等）。legacy_callers_host.cはKSETJMP_BUF_LENでなく5語配列。いずれもB/C1追従対象に残す。

## 確定 (2026-11-08、Opus 5.5 第 2 回の条件付き可を PM が反映)
- N1：g_cur_frame の定義はヘッダに置かず sys/kern/exec/exec.c の static とする (§2 の断片の注記を直した)。
- N2：引数域が写っていないときは copy の失敗で FAULT kill、PF のログは出ない。今との差として H7 の注記に記録。
- N3：host の偽物の ring3_fault_kill は戻ってくるので、LS と read_block が失敗した後の経路で `result = OS32_ERR_INVAL` を入れる (C host を追従する担当への注記、D1/D2)。
- 反論 R3 (件数)・R4 (分類) はレビュアーが妥当と認めた。追従の対象は変わらない。
- PM 決定 (REVIEW.r1 末尾)：R12 (syscall5 は予約 slot 範囲の意)、R5 (sys/arch/ppc/arch_abi.h を A で足す)、R6 (x86 実装は arch/x86/kernel/)、R8 (コンパイル時マクロを残す)。
