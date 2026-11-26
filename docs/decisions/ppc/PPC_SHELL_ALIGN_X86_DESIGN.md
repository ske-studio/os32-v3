> 出所: 本線の機械の ~/os32-tmp/run/ppc-shell-align/DESIGN.final.md (設計ラリー 3 回、astra 起草・sol レビュー、2026-11-20 確定)。
> 状態: **確定 (2026-11-20)** — PowerPC の製品のシェルを x86 の常駐シェルに揃える (票 TASK_PPC_ALIGN_X86 の B03・B09・B15・要る分の B01)。末尾の「確定」の節は本文に優先する。

# PowerPC の製品シェルを x86 の常駐シェルに揃える（第 3 稿）

## r2→r3 の変更

- B の常駐入口と直接 return は入口アセンブリ、子の正常終了・fault は親の jump context で MSR[FP] を戻す。親の FP 演算を両出口で検収する。
- B の回復準備中は最終 trap 枠を保持し、親 stack へ移った非局所出口で busy を解放する。
- C の旧像名・旧旗を製品構成へ正規化し、実行系、root 依存、リンク入力を突き合わせる。
- F の生成規則に製品の切替条件を渡し、像別の absent 期待を更新する。

方針は POLICY_DEV §10（2026-11-20）。製品の仮シェルを x86 の既存経路に揃え、x86 の動作を保つ。

基準は origin/main 1951635dc。対象は B03、B09、B15 と、その起動に必要な B01。完了は、同じ登録表の CUI が両機で枠 1 を占め、最初の外部子が枠 2 を使って終了し、切替要求を一度だけ消費するところまで。
段 5d の公開は、この受入に加え、公開後の wrapper から共通の門を実際に通ることを条件とする。

## 1. x86 との比べ

|役目|x86 の実コード|PowerPC の採用形|
|---|---|---|
|初期化|sys/platform/pc98/kernel.c の platform_boot_machine で FD → pipe → exec、platform_boot_run でシェルへ|sys/platform/powermac/ppc_kernel.c の platform_boot_machine の machine の末尾に同順。ページ・共有 trampoline の準備との前後を維持|
|親の識別|sys/kern/exec/exec.c の exec_init・exec_nest_level の volatile int exec_nest_level は初回 0|sys/platform/powermac/exec_boot.c の pm_exec_boot の 1 代入を削除。初期化で作る枠と、実際にロードした像を区別|
|枠の選択|sys/kern/exec/exec_launch.inc の exec_launch。深さ 0 が枠 1、専用帯、AS なし|同じ判定を通す。子は深さ 1 から USER の空き枠を確保|
|実行の境目|sys/kern/exec/exec_launch.inc の exec_launch の argc・argv・実表・aux と親への戻り|PPC32 SysV の r3〜r6 に同じ意味を載せる。特権、実表、固定帯を一組で検証|
|サービス|sys/kern/exec/shell_service.c の shell_plain_context・shell_service_publish・shell_service_retire。文脈判定、登録世代、lease 回収|同じ表と wrapper。子の入口は trampoline と aux=0|
|端末|userland/shell/ui.c の shell_run と userland/shell/rshell.c の cmd_rshell_resident。profile の後に端末を開始|同じシェルから USB/FB と LAN を扱う。デバイスの初期化と入力の採取は機種側|
|登録|userland/shell/main.c の main・SH_REGISTER の base、file、dir、mnt、sys、pci、hdprep、rshell、env、script、filer|各登録を同順に通す。装置の不在はコマンド実行時の提供結果に反映|
|切替|sys/kern/gui.c の sys_switch_shell の i32 sys_switch_shell(const char *path)、sys/kern/gui.c の int gui_take_next_shell(char *out,int cap)|同じ保管場所、同じ owner 判定、同じ消費者。動く子の親を特権の根拠にしない|
|終了|sys/kern/exec/exec_finish.inc の exec_finish で深さ・owner を 0 に戻し、回収・retire|次のロードが同じ枠・帯を再利用。通常子の終了は exec_finish の子の復帰経路 以下で親に戻る|
|再起動|sys/platform/pc98/kernel.c の kernel_shell_loop の切替、失敗回数、待ちと消去|方針を共通に移し、PC-98 固有の描画・FDD と PowerMac の媒体・端末を機種に残す|

統合判断。枠・exec・シェル本体・次パスは既存。HAL に必要なのは CPU の入口と、起動時の表示・媒体の効果。B07 の診断像全体の統合を待たず、製品シェルの選択を一本にする。

## 2. 実物との食い違い（数え方は以下の 6 件）

1. 「nest=1 を外せば走る」には入口が足りない。sys/arch/ppc/exec_entry.c の arch_entry_prepare は user を捨て、sys/arch/ppc/exec_enter.S の arch_entry_enter は root を切り替え、LR を 0、PR を常に 1 にしている。
2. 「シェルの枠を新設」する必要はない。sys/kern/exec/appslot.c の appslot_init が初期枠に深さ・owner を設定し、sys/kern/exec/exec_launch.inc の exec_launch 以下に実像の commit と登録がある。仮の親は追加代入が作っている。
3. SHELL_AS_APP は x86 にも現役の利用者がある（build/programs.mk の sh）。廃止の単位は PPC の製品と専用の接着。x86 の GUI 内 sh の構成を壊さない。
4. M4 は機種別の門だけではない。origin/wip/kfill-5d-m4 の build/sources/common.mk の C_COMMON_EXEC に gui.c の EXEC への移動がある。この選択変更は取り直す。
5. 現行 x86 の切替の門は owner だけ（sys/kern/gui.c の sys_switch_shell）。登録の門 gui_register やサービスの文脈検査と混同しない。
6. PPC の USER sh の trap → 再起動という受入は、常駐シェルの生の CPU 例外と等価でない。x86 は USER kill（sys/arch/x86/kernel/isr_handlers.c の exception_handler）の後に別経路があり、exception_handler の特権例外処理 で exec 中の特権例外も復帰する。PPC の ppc_trap_dispatch の fatal に相当するだけでは不足（sys/arch/ppc/kernel_trap.c の ppc_trap_dispatch）。

## 3. 前提の段と引き渡す観測

|順|担当|次段に渡す物|
|---|---|---|
|A 配置|PPC|製品の ELF/map、OF の RAM/available、最終 root の歩査、固定帯の台帳|
|B 入口|PPC、A 後|小さい常駐試験像と USER 子で PR、root、r1、r3–r6、通常復帰を採取|
|C 製品シェル|PPC、A と並列にリンクまで可|静的 shell.bin、KAPI 刻印、完全な登録表、root 内の絶対パス|
|D 起動方針|本線、A–C と並列|PC-98 で HDD/FD 両方の同値を確認した共通ループ|
|E 接続|PPC、B/C/D 後|machine → services → run で同じ像の LAN と USB/FB が動く|
|F 公開|本線・PPC、E 後|提供対象だけ切替を有効にして実際の要求と消費を往復。子からの拒否も記録|

## 4. 固定帯と像の予算

|区間（終端を含まない）|用途と根拠|
|---|---|
|0x100000〜0x3e0000|カーネルと後続の heap/KAPI/guard/SHM。sys/include/memmap.h の MEM_GUARD_SIZE・KHEAP_SIZE・MEM_KAPI_SIZE・MEM_SHM_SIZE・MEM_KERNEL_BAND_END・MEM_KERNEL_IMAGE_MAX。sys/arch/ppc/kernel.ld の ASSERT が __kernel_resident_end と __kernel_chain_end を検査|
|0x3e1000〜0x3fb000|固定ページ表。前置 guard は 0x3e0000。sys/include/memmap.h の MEM_FIXED_PAGING_GUARD_BASE・MEM_FIXED_PAGING_BASE、sys/platform/powermac/kernel_boot.c の boot_walk_valid|
|0x3fb000〜0x3fc000／0x3fc000〜0x400000|カーネル stack 下端の非 present guard／16 KiB のカーネル stack。sys/include/memmap.h の MEM_STACK_GUARD・MEM_KSTACK_TOP・MEM_KSTACK_SIZE の unsigned long 定数。初期 SP 用の値は 0x3ffffc|
|0x400000〜0x4a5000|常駐 text＋data＋BSS 最大 660 KiB。その後 64 KiB 以上の libc 用余地。sys/include/memmap.h の MEM_SHELL_IMAGE_HEADROOM・MEM_SHELL_LOAD_ADDR・MEM_SHELL_HEAP_END、sys/kern/exec/exec.c の exec_shell_layout|
|0x4b5000〜0x4f5000|exec heap 256 KiB。CRT の break の上限は下端。sdk/link/app_sys.ld のヒープ境界|
|0x4f5000〜0x4f6000／0x4f6000〜0x500000|非 present 4 KiB／stack 40 KiB。sys/kern/mm/paging_boot.inc の boot_layout_valid・paging_init。物理予約は sys/kern/mm/memory_boot.c の memory_boot_regions|
|0x500000 以降|プールとの接点は sys/kern/mm/memory_boot.c の STATIC_ASSERT。PPC が上から取る動的ページも下限を使用（sys/platform/powermac/ppc_kernel.c の platform_boot_machine）|
|16〜32 MiB 内|128 KiB 整列の HPT を available から選択。sys/platform/powermac/kboot_reserve.c の pm_kboot_reserve。4〜5 MiB との交差なし|

- OF は PT_LOAD の BSS・ledger・stack までを所有済みとする（sys/arch/ppc/kernel.ld の image PT_LOAD）。二重 claim は行わない。
- sys/platform/powermac/kboot_reserve.c の pm_kboot_reserve は RAM と available を区間で検査し、像の直後の既存予約を最大 64 KiB 認めて 15 MiB までを証明する。
- シェルの配置は detach 後。帯内に OF の穴があれば起動時の窓検査で失敗とする。空き容量の合計から連続性を推定しない。
- BAT の重複は禁止（docs/ppc/cpu-mmu.md の BAT とページ変換）。製品の最終変換はソフトページ表と HPT。sys/platform/powermac/ppc_kernel.c の ppc_trap_fatal_sink・bat_valid の BAT 残存検査を通す。
- A は最終 root で 0x400000〜0x500000 をページごとに歩査する。既存の static int boot_walk_valid(u32 root)（sys/platform/powermac/kernel_boot.c の boot_walk_valid）は全帯を直接検査しない。B は実際の USER 子 root でも同帯を照合する。起動後に両 CPU で image・heap・stack の supervisor 同番地変換と guard の欠落を確認。子の root でも帯への USER アクセスを拒む。
- カーネル常駐上限の式は 0x2e0000 − 0x30000 − 0x1000 − 0x2000 − 0x38000 = 0x275000（2516 KiB）。root ISO を埋める分も含む。
- BSS だけでも userland/shell/rshell.c の HD_BUF_SIZE・hd_buf の転送領域が 256 KiB。raw が収まっても BSS を足せば超え得る。
- 手元の既存 x86 userland/shell.bin は 161088 B。PPC のリンク前見積もりは raw 160〜320 KiB（命令密度と静的ライブラリによる幅）。BSS は別計上。
- root の増分は新旧 bin の ISO 2048 B 丸め差にディレクトリ差を加える。build/ram-root.mk の RAM root 生成規則と tools/mkroot_image.py の create。kernel の純増を shell.bin 全量と取り違えない。
- 実装時は新旧の raw、BSS、root ISO、常駐終端、chain の余白を B 単位で記録。上限超過時は試験用子を製品 root から外し、再計測。なお超える場合は root 分離を別設計に戻す。

## 5. 常駐入口と終了

- 既存の署名は sys/arch/ppc/exec_entry.c の arch_entry_prepare。最上位からは sys/kern/exec/exec_launch.inc の exec_launch の第 2 引数が 0、USER では子の pd_phys。
- 準備時は従来の 24 B の pc、sp、4 語を用いる（sys/arch/ppc/exec_entry.c の STATIC_ASSERT、sys/arch/ppc/arch_exec_storage.h の ARCH_ENTRY_BYTES）。両方の linkage と引数の store 失敗は commit 前に返す。
- 実行時は第 2 引数が 0 の時点で常駐入口へ分ける。PR=0 のままカーネル root を保持。子側だけが従来の root 切替と PR=1 の rfi を行う。
- 常駐側は PPC32 SysV 呼出しとして親 stack に LR、非揮発 GPR、旧 r1 を保存。16 B 整列のシェル stack に替え、argc/argv/api/aux を r3/r4/r5/r6 に配置して間接呼出し。
- 戻り用の親フレームは非揮発レジスタで保持し、通常 return 時は親 r1、保存レジスタ、LR を戻して C へ。復帰先は sys/kern/exec/exec_launch.inc の exec_launch の正常終了。割込み許可を継承し、USER 例外用 SP を上書きしない。
- 通常の製品 CRT は sdk/crt/ppc/crt0.S の _start と sdk/crt/ppc/start.c の os32_start_c で libc 終了処理の後に sys_exit。こちらは既存の setjmp 復帰を使う。直接 return する小像でも別途試す。
- FP の値と実行許可には別の保存先がある。hard-float の常駐像（build/arch/ppc.mk の PPC_USER_ABI）へ入る前に sys/kern/exec/exec_launch.inc の exec_launch が親の全 FPR・FPSCR を深さ別に保存して初期化する。sys/arch/ppc/fp_context.S の arch_fp_init・arch_fp_restore の arch_fp_init はゼロ状態をロードして元の MSR に戻る。常駐用 sys/arch/ppc/exec_enter.S の入口は親フレームに旧 FP ビットを保存し、PR=0 のまま FP=1 を設定して isync 後に呼ぶ。直接 return は同じフレームから FP ビットだけを現 MSR に戻し isync、正常終了へ進む。
- 子の起動前に int exec_setjmp(k_jmpbuf buf) が親 MSR の FP ビットを PPC 専用の追加 1 語に保存する（sys/include/ksetjmp.h の exec_setjmp・exec_longjmp、sys/arch/ppc/setjmp.S の exec_setjmp、sys/arch/ppc/arch_jmp_storage.h の ARCH_JMP_BYTES・ARCH_JMP_ALIGN のサイズも拡張）。子の正常終了と fault はそれぞれ sys/kern/exec/exec_finish.inc の exec_exit・exec_pending_transfer から void exec_longjmp(k_jmpbuf buf) に合流する。着地直前に保存 FP ビットを現 MSR に合成し EE=0 で isync。常駐親には FP=1、最上位の終了では起動側の元の値が戻る。着地側は既存の arch_fp_restore で親 FPR・FPSCR を戻す（sys/kern/exec/exec_launch.inc の exec_launch、resume は sys/kern/exec/exec_schedule.inc の exec_resume）。直接 return も exec_exit を経て値の復元まで通る。
- コードのロード・cache は sys/kern/exec/exec_image.inc の exec_load_image と sys/arch/ppc/exec_cache.c の arch_exec_publish。常駐でも各ページを publish し、BSS は再ロードごとにゼロにする。
- 実表は sys/arch/ppc/exec_kapi.c の ppc_kapi_table の KernelAPI ppc_kapi_table。sys/kern/exec/exec_kapi_init.inc の console_printf の登録 の可変引数 console_printf まで直に呼ぶ。USER からの固定引数転送を流用しない。
- 4 語目は sys/kern/exec/shell_service.h の SHELL_SVC_REJ_CTX_GUARD・STATIC_ASSERT の ShellService *。32 B の PPC32 構造体サイズを検査対象に加え、戻り番地は PPC の呼出し元 LR から既存 wrapper で判定する。
- B の例外判定は void ppc_trap_dispatch(struct ppc_frame *f)（sys/arch/ppc/kernel_trap.c の ppc_trap_dispatch）で行う。まず DSI/ISI の HPT spill、次に一致する onfault probe の再開を優先する（sys/arch/ppc/kernel_vectors.S の ppc_kernel_frame_complete、sys/arch/ppc/kernel_trap.c の ppc_trap_dispatch）。外部割込み・DEC は同関数の登録処理又は未登録診断へ、USER は同関数の既存経路へ渡す。
- 保存 PR=0、常駐像の commit 後で深さ・owner とも1、IRQ 処理外、かつ未解決の同期 DSI/ISI・program・alignment のときだけ void exec_fault_recover(void)（sys/kern/exec/exec_finish.inc の exec_fault_recover）へ渡す。割込み内で生じた同期例外も除外する。exec 外は sys/arch/ppc/kernel_trap.c の ppc_trap_dispatch の診断・停止。
- busy の所有者は最終フレームの KFRAME_BUSY（sys/arch/ppc/kernel_vectors.S の KFRAME_BUSY のアセンブリ定数と格納）で特定する。spill 失敗で trap 枠に移した時点で旧 busy は解除済み（同ファイルの spill 失敗経路）。常駐 fault を選んだ時点で最終枠の busy 語のアドレスを PPC 専用の当該ジャンプ先に対応づける（一回限り、通常の setjmp では空）。exec_pending_transfer の終了準備と pending 設定（sys/kern/exec/exec_finish.inc の exec_pending_transfer）の間は busy=1 を保持する。exec_longjmp の末尾で EE を閉じ、必要な情報をレジスタに取り込み、対応づけを消費し、親 r1 に切り替えて旧枠へのアクセスを終えた直後に、控えたポインタが指す語だけを通常出口（同ファイルの特権復帰経路）と同様にクリアする。親の IRQ・例外深さは sys/arch/ppc/setjmp.S の exec_longjmp の既存復元に従う。
- B 完了には別々の常駐世代で実 trap を2回（未解決 DSI で枠移動を通し、次は program trap）起こし、各回の退役、再起動、次の DEC tick を記録する。別起動で exec 外に同じ同期例外を起こして停止を確認。登録 IRQ 内 fault と未登録外部割込みも常駐復帰に入らないことを分岐試験する。
- B の直接 return 試験では親 r1、LR、r2、r13–31、非揮発 CR2–4 に既知の状態を置き、16 B 整列の子側 linkage、実表の可変引数呼出し（文字列と整数の表示）、復帰先と親状態を照合する。親の保存先はシェル帯の外。保存状態の実例は sys/arch/ppc/setjmp.S の exec_setjmp。C でも実 CRT から同じ呼出しと終了後の親状態を確認する。
- B は常駐親で FP 命令を実行し、非揮発 FPR と FPSCR の丸めモードに既知値を置く。子は別値を書いて正常終了、次の子は別値を書いて実 fault。各復帰直後に親の FP 許可、保存値、同じ FP 命令の計算結果を照合する。直接 return は呼出し前後の FP ビットも照合する。
- commit → retire → publish → 深さ更新 → 入口の順を保存（sys/kern/exec/exec_launch.inc の exec_launch）。古い世代の lease が次の像から読めないことを確認。

## 6. 起動ループの境目

共通ソース（新設 sys/kern/init/shell_loop.c）は経路、所有する消費バッファ、連続回数を持つ。機種のヘッダを含めず、新設 sys/include/shell_loop.h に次の内部契約を置く。

~~~c
struct shell_loop_ops {                          /* 新設 */
    int (*gui_default)(void);
    int (*prepare)(int gui);                    /* 0=可、1=KCG欠、2=GUI依存欠 */
    void (*loading)(const char *path, int gui);
    void (*result)(const char *path, int rc, int gui, int streak);
    void (*failure)(int gui, int rc, int streak);
    void (*delay_clear)(void);
};
void shell_loop_run(const struct shell_loop_ops *ops); /* 新設 */
~~~

- パス初期値は sys/include/config.h の SYS_SHELL_BIN・SYS_GSHELL_BIN。既定を一度選び、各周の先頭で一件を消費して回数を 0 にする。consumed は次の周でも生存するバッファ。
- prepare の欠落理由 1 は CUI に --kcg-unavailable を付け、2 は通常 CUI。CUI 側の表示を通る。実コードの順序は sys/platform/pc98/kernel.c の kernel_shell_loop。
- GUI ロード失敗は failure の後で即 CUI。GUI fault は 1、2 回目を再試行し 3 回目だけ CUI へ。正常復帰で連続数を消す。
- GUI の通常復帰・fault と CUI の復帰は delay_clear を通す。PC-98 は同ファイルのシェル再起動待ち の tick 差、halt、画面消去を維持する。
- CUI の媒体失敗時、PC-98 は同ファイルのシェル読込失敗経路 の FDD 処理。PowerMac の root は読み取り専用。診断を端末に一度表示し USB・LAN の処理を維持、同じ壊れた像を空回りで読まない。
- 初回 USB キー／LAN OPEN 待ち（sys/platform/powermac/shell_boot.c の pm_shell_boot）を撤去。初回文字から入力として届ける。root 不成立は exec への到達以前の失敗とする。

## 7. 製品シェル、端末、切替

- build/programs.mk の sdk/crt/syscalls.o・userland/lib/rt/dbgserial.o・SHELL_SRC を構成の正とする。シェル全 .c、PCI デコーダ、partition 解読、filer draw、save、libc、libgcc をターゲット別に作る。
- sdk/link/app_sys.ld の常駐用の帯・世代・KAPI 刻印・余白 assert を PPC ELF に適用。小データ節の取りこぼしを map で調べ、PPC の ABI フラグは子と共通にする。
- PPC 常駐 CRT の syscalls と allocator は OS32_CRT_RESIDENT（build/programs.mk の sdk/crt/resident/nano_adapter.o・sdk/crt/resident/syscalls.o）。生成物を USER のオブジェクトと分ける。x86 の build/programs.mk の sdk/crt/resident/nano_adapter.o・sdk/crt/resident/syscalls.o と同じく常駐 syscalls.o と常駐 nano_adapter.o を直接入力とし、ターゲット libc、libgcc をリンクする。USER 専用 nano archive と USER syscalls.o（build/ppc-user.mk の PPC_USER_CRT）は子用。map で void *_sbrk(int incr)（sdk/crt/syscalls.c の _sbrk）が常駐 syscalls.o、void *os32_nano_crt_sbrk(struct _reent *r, ptrdiff_t incr)（sdk/allocator/nano_adapter.c の os32_nano_crt_sbrk）が常駐 adapter に一意に解決することを確認。C は整列済み _end から char __os32_shell_sbrk_limit[]（同ファイルのヒープの境界）まで伸ばし、上限を1 B 越える要求が失敗して break が動かないことを実像で確認する。
- sdk/mkos32x.py の main に ELF を渡し、静的、shlib_protocol=0、resident heap 指定は x86 同様 0。現行 build/ppc-user.mk の USER stack/heap 指定を引き継がない。
- root manifest で /sys/shell.bin に配置。build/targets/ppc-powermac.mk の ROOT_ISO・ROOT_DEPS の bin 用一覧と区別し、実ファイルも root の依存に加える。
- 製品像は LAN・USB・FB を併せ持つ現行 kboot-link-sh の機能集合を採用し、既定の選択を揃える。core・診断は B07 に残し、旧名を使う受入も同じ製品バイナリを使う。
- sys/conf/images/ppc-flags.mk の IMAGE で入力を解く。引数なしは製品、明示の kboot-link/core は診断。旧 kboot-sh、kboot-shell、kboot-link-sh と各名に対応した有効な旧旗組はすべて kboot-link-sh に写し、有効旗も LINK/SH/SHELL=1/1/0 に揃える。明示名と旗は双方を正規化して不一致を拒否し、禁止の旗組・NEGATIVE の検査を保つ。出力先・前処理・依存規則にも正規化後の旗を使う。C/D の受入では既定、各旧名、旧旗による起動ごとに KB_EXEC、ROOT_BINS/ROOT_DEPS、root manifest と /sys/shell.bin の内容、カーネルのリンク入力と map を正準製品と照合する（build/targets/ppc-powermac.mk の KB_EXEC・ROOT_DEPS・KB_INC_EXTRA・KB_INC）。診断の二像も明示選択で表の閉包を確認する。
- userland/ppc/sh/profile.h の SHELL_AS_APP・SH_DEFAULT_SHELL の制限、userland/ppc/sh/link_profile.h の SHELL_REMOTE_APP・SHELL_REMOTE_SFS の迂回と userland/ppc/sh/newlib_runtime.c の sh_runtime_init の表コピーを除去。x86 の app 用ブロックは build/programs.mk のアプリ用リンク規則の利用が残る。
- userland/shell/main.c の main・SH_REGISTER の通常経路を選び、標準出力の行バッファと環境初期化の順を保存。PATH は sys/include/config.h の SYS_DEFAULT_PATH、HOME と SHELL は cmd_env.c の通常既定。
- userland/shell/ui.c の shell_run は自動で rshell に入る。userland/shell/rshell.c の cmd_rshell_resident の UART 初期化必須条件を「UART が使用可、又は LAN の通知提供あり」に一般化する。提供の判定は実際の endpoint の能力で行う。
- 両方あれば従来どおり UART EOT と LAN を扱う。LAN のみなら UART 操作を飛ばし、ローカル入力も同じループで進める。両方なければローカルプロンプト。ホストが遅くても起動は進む。
- sys/platform/powermac/input_boot.c の lan_key・provider の物理入力と待機を提供側に残す。出力は端末の色・UTF-8・捕捉を既存のコンソール経路に流し、カーネル診断のミラーと分ける。
- LAN の一行は userland/shell/rshell_lan.c の rsh_lan_poll → userland/shell/rshell.c の rsh_lan_execute → rshell.c の既存 sfs_run_child に渡す。/host 不在、begin 失敗、子 fault、end の status と捕捉解除を試す。
- APP 専用 userland/shell/rshell_lan.c の rsh_lan_execute の重複処理を削除。GUI 引渡しは同ファイルの GUI 引渡しの ACCEPT/END 後の実行順を守り、CUI 最終出力を二重送信しない。
- M1〜M3 の SESSION_SHELL_CALLER()、P2 の consumer と parent 判定（origin/wip/ppc-gui-5d-p2:platform/powermac/session_shell.c のシェル資格判定）は採用しない。枠 2 の子が門を通る実装になる。
- M4 の gui.c 選択だけを現行 main に移植。提供対象の wrapper と exec の absent hook は別に選択。sdk/kapi_target_ppc.py の PROVIDER_GROUPS の一枠を GUI group 全体から独立に公開し、無関係の API を開かない。
- 公開前は既存 ABI slot の NOSYS、公開後は owner 1、空・過長パス、子拒否を同じ実体で判定。要求保存成功は切替完了を意味せず、終了して次の周で一回消費する。

### D の像別リンク受入（E の前提）

|像（現行の選択は build/images/ 各像の機能選択）|移行後の役目と consumer|GUI / IME の提供|F の切替枠|
|---|---|---|---|
|pc98|従来の HDD/FD、共通ループから gui.c を消費|両フック実体、既存 IME|既存|
|kboot-link-sh|既定製品、共通ループと gui.c|GUI 実体、IME absent|公開|
|kboot-sh、kboot-shell|旧製品受入名、同じ製品像へ解決|製品と同じ|公開|
|kboot-link|LAN 診断、ループ・gui.c ともに選択しない|両方 absent|非提供|
|kboot-core|核の診断、ループ・gui.c ともに選択しない|両方 absent|非提供|

製品の int gui_take_next_shell(char *out,int cap) は sys/kern/gui.c の gui_take_next_shell の実体一つ。GUI フックは sys/kern/exec/exec_gui.h の exec_gui_pump・exec_gui_owner_exit の実体経路を使う。D で PPC の IME absent 提供側に void ime_set_render(void *table) の空実装を追加する（宣言 sys/include/ime_control.h の ime_set_render、必要な呼出し sys/kern/gui.c の gui_owner_exit・gui_ime_set_render）。既存の absent は sys/kern/exec/absent/exec_ime.c の exec_ime_owner_exit の owner 回収だけである。x86 は従来の提供者を選ぶ。D の終了条件は全行の選択結果、実リンク、map の提供元、未定義・多重定義 0 の照合。D/E の PPC 公開表は NOSYS。F は build/targets/ppc-powermac.mk の KAPI 生成規則から正規化済み IMAGE が製品の時だけ新設 --feature-switch-shell=1 を渡す。sdk/gen_kapi.py の _write_text_if_changed・TARGET で既定 0 とし、1 の時だけ sdk/kapi_target_ppc.py の PROVIDER_GROUPS の absent 集合から sys_switch_shell を除いて sys/kern/gui.c の sys_switch_shell の共通実体へ結ぶ。既存 --feature-link は LAN 診断とも共有する。

### E の端末別受入

|利用可能な端末|初期化と入力|行末・終了|
|---|---|---|
|UART|従来の初期化、UART とローカルキー|従来の EOT 数と終了時 active 解除|
|LAN|UART 初期化・読出し 0、LAN 通知とローカルキー|LAN END、UART EOT 0、終了時 active 解除|
|UART と LAN|UART 初期化、両入力とローカルキー|各 transport の従来の応答と終了時 active 解除|
|両方なし|UART 操作 0、ローカルプロンプト|EOT 0、active は 0 のまま|

能力は接続済みホスト数と独立に判定する。userland/shell/ui.c の shell_run の案内前の初期化・active 設定、userland/shell/rshell.c の rsh_getch の static int rsh_getch(int *from_serial)、同ファイルの UART 処理 の行末・終了処理にも同じ判定を適用。併用時は既存 UART の終了行と行頭 ESC の EOT 数も比較する。

## 8. 失敗の表

|原因|後始末と次状態|
|---|---|
|OF 窓・HPT 不成立|sys/platform/powermac/kernel_boot.c の ppc_kernel_start の予約診断で停止。未所有メモリへの書込みなし|
|root mount 失敗|sys/platform/powermac/ppc_kernel.c の platform_boot_run の判定で起動を抑止。LAN 診断を維持|
|ヘッダ・世代・予算の不一致|commit 前に失敗。GUI は CUI に戻り、CUI は機種の媒体診断へ|
|短い読込／argv 配置失敗|sys/kern/exec/exec_image.inc の exec_load_image と sys/kern/exec/exec_launch.inc の exec_launch。FD を閉じ、abort で親へ。次回は全量ロード|
|子の AS・ページ不足|sys/kern/exec/exec_launch.inc の exec_launch の owner・AS 巻戻し。シェルの heap、root、FD を復元|
|常駐の正常終了・切替|sys/kern/exec/exec_finish.inc の exec_finish で owner・深さを落とし、所有資源と lease を回収。保留の要求を次周で消費|
|USER 子の fault|sys/kern/exec/exec_fault.inc の ring3_kill_kind・ring3_fault_kill の終了／移譲、親へ復帰。新規シェルを生成せず次のコマンドを受理|
|GUI 実行結果が fault|正常終了・新要求で回数をリセットし、三連続で CUI。実 trap でも §5 の復帰から同じ分岐へ|
|LAN 切断・満杯・重複通知|userland/shell/rshell_lan.c の rsh_lan_flush・rsh_lan_reset・rsh_lan_poll の flush/reset の契約を維持し、接続復帰後の新しい ID で一回だけ応答|
|switch の owner／パス不正|sys/kern/gui.c の sys_switch_shell。INVAL／NAMETOOLONG を返し、以前の有効な要求は残す|

## 9. ファイル別の段（同一行の作業だけを一人が所有）

|段・担当|対象ファイル|差分の目安と検収|
|---|---|---|
|D1 本線|sys/include/shell_loop.h（新設）|約 15 行、§6 の契約|
|D2 本線|sys/kern/init/shell_loop.c（新設）|約 80 行、消費と三連続の状態表|
|D3 本線|sys/platform/pc98/kernel.c|既存ループ約 120 行を機種効果に分解、FDD と診断の呼出順を比較|
|D4 本線|build/sources/common.mk|共通ループと GUI の選択、旧 CLI の選択を解く|
|B1 PPC|sys/arch/ppc/exec_enter.S|約 30 行、特権呼出し・親への帰還|
|B3 PPC|sys/arch/ppc/kernel_trap.c|同期例外の既存回復を優先し、常駐像の fault を exec へ渡す|
|B4 PPC|sys/arch/ppc/kernel_vectors.S|回復で飛び出すフレームの busy 解放と深さ復元|
|B5 PPC|sys/arch/ppc/setjmp.S、sys/arch/ppc/arch_jmp_storage.h|親 FP 許可の保存・復元と trap 枠放棄時の解放|
|B2 PPC|sys/kern/exec/shell_service.h|サイズ検査を PPC32 にも適用|
|C1 PPC|build/ppc-user.mk|約 60 行、常駐 CRT・リンクと USER の出力先を分ける|
|C2 PPC|build/ppc-ram-root.json|製品シェルの宛先を追加|
|C3 PPC|build/targets/ppc-powermac.mk|製品 root への依存、診断子の選択|
|C5 PPC|sys/conf/images/ppc-flags.mk|旧名・旧旗の正規化、明示診断の選択|
|C4 PPC|build/select.mk|既定製品を選択|
|E1 PPC|sys/platform/powermac/exec_boot.c|仮の深さ代入を除去|
|E2 PPC|sys/platform/powermac/ppc_kernel.c|machine の初期化、services の root と入力、run の起動を接続|
|E3 PPC|sys/platform/powermac/shell_loop_ops.c（新設）|約 50 行、表示・回復・待機|
|E4 PPC|build/platform/powermac.mk|機種側の選択を差替え|
|D6 PPC|sys/kern/exec/absent/exec_ime.c|IME 欠の像で render の宣言を満たす|
|E5a 本線|userland/shell/ui.c|起動案内前の UART と active の能力判定|
|E5 本線|userland/shell/rshell.c|端末の提供状態に従って LAN と UART を開始|
|E6 本線|userland/shell/rshell_lan.c|APP 専用の SFS 分岐を削除|
|F1 PPC|build/targets/ppc-powermac.mk、sdk/gen_kapi.py、sdk/kapi_target_ppc.py|切替 slot の提供条件|
|D5 本線|build/sources/hooks.mk|実体と absent の二重定義を防ぐ選択|

- 廃止は製品・診断の全呼出しを移した後。platform/powermac/{shell_boot,cli_boot}.{c,h}、kernel/cli/resident_cli.{c,h}、userland/ppc/sh/ 配下は各実ファイルを独立の削除段にする。
- 旧専用登録の利用者消滅後、main.c、cmd_sys.c、cmd_base.c、cmd_env.c 等の PPC 限定枝は各一ファイル一段。x86 の app 用との差分をプリプロセス後にも比較。
- 依存するファイル段を同じ統合枝で結合して受入する。中間の未接続コードを製品に出さない。

## 10. 受入 → 試験と変異

|観測するもの|試験／意図的に壊す箇所|
|---|---|
|初回枠 1、owner 1、深さ 1、AS なし、PR=0、実表／aux|QEMU 7450・7455 で入口の実レジスタと台帳を照合。root=0 分岐反転、aux の欠落を別々に RED|
|子枠 2、PR=1、aux=0、親への復帰|同じ常駐親から通常終了、fault、再実行。子の root 復元／戻り先を壊し、それぞれの後続コマンドで検出|
|guard と予算|ページ表歩査と境界 ELF。guard を present、BSS を上限＋1、root を大きくし各境界で失敗|
|完全なシェル|両ゲストで help、PATH、HOME、SHELL、CWD、引用引数、リダイレクト、パイプ、日本語、子の終了値を比較。登録 1 群除去と行バッファ解除を別試験|
|LAN とローカル端末|起動前から USB 一文字、後着ホスト、切断・再接続。最初のキーを捨てる／END の前に捕捉を解く変異|
|SFS 一行|ホストあり／なし、正常／fault。子終了値を固定又は end を省いて次のセッションで RED|
|GUI 選択・fallback|共通ループを偽の exec 結果で駆動。連続 fault 閾値変更、成功時リセット欠落、consume 忘れを個別に検出|
|F の成功|試験 root に製品 CUI と別の固定帯用常駐像を格納。枠1の試験呼出しから公開表の int (__cdecl *sys_switch_shell)(const char *path)（sdk/include/os32/os32_kapi_generated.h の OS32_KAPI_GENERATED_H）を通し、第2像の絶対パスを指定する。戻り成功と保存内容 → 旧像終了・旧世代 lease 退役 → 次周で一度だけそのパスを消費（再読は空）→ 第2像が枠1・PR=0 で固有文字列を出す順を記録する。消費後に旧像をロードする変異を最後の照合で検出。GUI 準備失敗は別試験で通常の os32gui 要求から CUI fallback を確認（userland/shell/cmd_sys.c の cmd_os32gui）|
|門の負例|子の切替、過長、空、同じパスの再要求。owner 条件反転・消去抜けで RED。未公開像は NOSYS のまま|
|x86 の回帰|NP21/W の HDD と FD で通常起動、GUI の戻り、ロード失敗、FDD 診断。別製品 sh.bin の GUI 内起動も照合|

## 11. 既存の固定と移行

|段|既存の固定（パス・行）|移す期待／保つ変異|
|---|---|---|
|A|tools/ppc/kboot_reserve.py の exercise、tools/ppc/kboot_root_image.py の verify、kboot_layout.py|小さい root の配置に加え、製品 PT_LOAD と固定帯、OF の穴を観測|
|B|tools/ppc/test_exec_entry.py の HARNESS・main|既存 USER の整列・store 失敗に常駐の通常 return、実表、親 SP を加える|
|B|tools/ppc/test_exec_publish.py、tools/ppc/test_kapi_binding.py の verify|子のコード可視化と out ポインタ検査を維持。常駐ロード後に異なる命令列を同番地から実行|
|B|tools/accept/arch_ctx.py の image と tools/accept/arch_ctx_probe.c の ctx_irq とシェル起動時の文脈検査|旧 shell_boot 置換と仮親直下の子起動を、実常駐親からの計測に移す。割込み・park/resume・終了・FP の文脈を比較|
|C|tools/ppc/test_sh_build.py の exercise・main|旧 USER VA 0x80100000 の固定を shell VA に変更。ELF entry、PPC 印、未解決 0、リンク入力の証跡を維持|
|C|tools/ppc/sc_run.py の outputs|環境既定を sys/include/config.h の SYS_DEFAULT_PATH・SYS_DEFAULT_SHELL に合わせる。SHELL の期待は /shell、実行ファイルの格納先は /sys/shell.bin|
|C|tools/accept/ppc_newlib.py の capacity・main|試験用 root への sh.bin 直入れを更新。子 fault 後の FD 空き回復・139 を維持|
|D|tools/ppc/shell_run.py の accept、tools/ppc/sb_run.py の main、tools/ppc/sc_run.py の LINK_NEST_EXPECTED・start|旧 CLI の os32>、exit=-2、exit=7 は製品の表示と status に移す。子の nest kind/code と parent=12345 はそのまま（親 ID の値ではない）|
|D|tools/accept/ppc_loop.py の observe・main|旧 resident_cli_ops の getkey 差替えと診断像の固定を製品の入力・子の結果へ|
|E|tools/ppc/link_sh_run.py の MUTATIONS・outputs・accept・app_started|g_cur_app 非ゼロ待ちを実枠の commit に変更。RSH 中の active、捕捉後の reader 解除、終了時の非 active を時点ごとに確認|
|E|同試験の trap 回復の観測|シェル内 trap は PR=0 の回復を通る。ログの旧 USER 文言を外し、同じ枠の世代更新、捕捉回収、次コマンドで判定|
|E|tools/ppc/usb_run.py の settings・product_settings・execute・usb_keys、tools/ppc/fb_run.py の MUTATIONS|初回キー消費の変異を「一文字目が入力へ渡る」に反転。USB 脱着、FB の表示、LAN RPC 後の読み手を再確認|
|E|tools/ppc/sd_run.py の start・keep_fs、tools/ppc/realhw_run.py の link_script・main|旧 USER 起動文言を常駐の実行観測に置換。二度目の起動と実機 nest/shell/exact の終了コードを保持|
|E|tools/accept/ppc_sfs.py の observe|一行の開始回数、終了 7、CWD 復元。未マウント時は共通常駐の拒否文言、begin 後の失敗とは別ケース|
|E|tools/accept/ppc_display_lifecycle.py の image・output_drawn、tools/accept/ppc_display.py の GATE、tools/accept/ppc_shm.py の試験設定、tools/accept/ppc_db.py の MUTATIONS|表示・SHM・DB の owner は実行中の子の ID と照合。子の終了で親所有の資源が残ることを維持|
|E|tools/accept/ppc_fp.py の verdict・observe、ppc_sqlite_stack.py、ppc_sqlite_static.py|USER 子の FP・stack・終了後の追加改行を、実常駐親の RSH で確認|
|E|tools/accept/ppc_{boot_info,console_cells,font,gfx2,kcg,mouse,path,pci,port,rtc,surface,sysinfo,ver}.py|共用の RSH セッション／製品像組立の呼出し側。旧 sh の到達待ちを共用側で更新して各コマンドの期待を照合|
|F|tools/ppc/test_kapi_binding.py の不在 slot の期待表|生成器の既定・LAN のみ・製品を比較。各構成の従来 absent 期待から製品だけ切替一枠を除く。製品 wrapper の実体への参照、他構成の同枠の NOSYS、全構成の gui_call/gui_register の NOSYS を確認|
|F|tools/tests/test_ring3_guard.py、test_shell_service.py、test_trim_kernel.py、test_trim_flow.py|本線の owner、戻り番地、世代、consume を維持。P2 の「子で通る」host 模型を統合しない|

### 台本で照合する時点

|停止点|実行枠 ID|PR|深さ|資源 owner|g_cur_app|
|---|---:|---:|---:|---:|---|
|初回親入口|1|0|1|1|NULL|
|最初の USER 子入口|2|1|2|2|枠2|
|正常終了又は fault 後の親コマンド|1|0|1|1|NULL|
|親 fault 後、回収してループへ|なし|0|0|0|NULL|
|次の親入口|1（新世代）|0|1|1|NULL|

根拠は static AppSlot *g_cur_app（sys/kern/exec/exec.c の g_cur_app）、commit と深さ更新（sys/kern/exec/exec_launch.inc の exec_launch）、親文脈復元・最上位終了（sys/kern/exec/exec_finish.inc の exec_restore_context・exec_finish）。入力台本は初回キーと再接続後の入力を個別に照合し、USB の再接続後は READY を待ってから要求する（tools/ppc/usb_run.py の launch）。root 台本は製品 root の実パスと試験 root の第2像をそれぞれ開いて検証する。

### 検査宣言と試験の土台

- tools/tests/ppc_sh_boot.py、ppc_sh_runtime.py、ppc_sh_launch.py、ppc_sh_input.py、ppc_sh_string.py、test_ppc_sh_stdio.py は、旧 profile と shim を製品の代わりに組む入力を外す。常駐 CRT の実リンク・出力・子起動へ移す。
- tools/tests/test_resident_cli.py の CLI 専用 parser・疑似 FS と tools/checks/ppc-cli-boot.py は、同じ役目の製品受入が緑になってから廃止する。
- tools/tests/test_ppc_link_sh*.py、test_ppc_sd_run.py、test_ppc_newlib.py、test_ppc_terminal_work.py の起動元と変異アンカーを同期。UTF-8 の RED は旧 runtime の差替えから実際の端末出力に移す。
- tools/checks/ppc-{sh-boot,sh-runtime,sh-string,sh-launch-host,sh-output,sc,sd,rh-fb,usb,terminal-input,exec-entry,fp-guest}.py の covers と inputs を各段で更新。
- tools/guest_acceptance.yaml と tools/checks/guest-acceptance-host.py は新製品の台本を参照。tools/tests/fixtures/build_profile_ppc.json は五つの構成の kboot-query 結果から更新する。
- tools/tests/test_build_selection.py は完全な製品と診断の閉包を比較。x86 fixture は x86 の実際の共通ファイル移動だけ反映。追加・削除した検査名は tools/checks/weights.json と突き合わせる。
- 既存の層検査は baseline 0。新設 init の依存を共通契約だけに収め、追加の例外は不要。共通ソースの PPC コンパイルも通す。
- 実装段の受入ログは両 CPU と NP21/W の像・版・入出力を区別する。実機確認は同じ製品像の既存 realhw_run 台本に引き渡す。本稿の数値は静的照合とサイズ見積もりで、ゲスト実測は実装後に記録する。

## 未決

ユーザー判断を要する項目は 0 件。

反論は 0 件。

## 確定 (2026-11-20 夜、sol 第 3 回「条件付きで可」、PM が反映)
以下は本文に優先する。
- **P2 (§5・§9) busy の語の型**: sys/arch/ppc/ppc_frame.h で、フレームの先頭から 164 B の busy の語を `u32 *busy` として取れる型付きの道を共有し (struct ppc_frame の 160 B の vector の後ろ)、アセンブリの定数 KFRAME_BUSY とのオフセットの一致を静的に検査する。PR=0 の回復の分岐だけで読み、その jump context に渡す。
- **P2 (§7・§9) 旧い旗の正規化の負例**: 製品の像へ正規化される入力は、旧名・旧旗を含めて NEGATIVE=0 の時だけに限る。旧旗の組 LINK/SH/SHELL=0/0/1 と NEGATIVE が非 0 の組み合わせを拒否する試験を足す (sys/conf/images/ppc-flags.mk の旗の検査の順に依らない)。
- **段の入れ替え (2026-11-20 夜、PowerPC の PM の決定を本線の PM が受けた)**: C2 (build/ppc-ram-root.json に /sys/shell.bin) を段 E に移し、起動を切り替える取り込みで旧い /bin/sh.bin・hello・fault を外すのと同時に入れる — 両方を置くと常駐の終端 0x396000・chain 0x401000 で上限を 135,168 B 超えるため (sol の C の P1)。段 C は「/sys/shell.bin を成果物として組める」まで (製品の像の中身は main と同じ)。段 E の受入に、新しい shell.bin (raw 218,544 B、旧 165,440 B より +53 KB) を入れた後の像の予算 (常駐の終端・chain・ISO の丸め) の照合を足す。
- **段の入れ替え (2026-11-20 夜、その 2)**: C5 (sys/conf/images/ppc-flags.mk の旧名・旧旗の正規化) と C4 (build/select.mk の既定の製品) も段 E に移す — 正規化は旧い名前の像 (kboot-shell の常駐 CLI の os32>・outprobe の自動実行ほか) を製品の像に変えるので、起動を切り替える前に入れると旧い受入 (tools/ppc/ppc_sh_output.py・check-ppc-sh-output ほか) が壊れる (sol の C の第 2 回の P1)。段 C は /sys/shell.bin を成果物として組める (make ARCH=ppc PLATFORM=powermac shell、sdk/link/ppc/app_sys.ld と世代の再生成) だけ。確定の「旧い旗の正規化の負例」の試験も段 E で。
- **E5a・E5 の LAN の提供の判定 (PM 決定 2026-11-20 夜、astra の止まりを受けて)**: 起動の案内の前に通知を消費せずに LAN の提供を判定する新しい口 (KAPI) は作らない — x86 の今の作り (rshell の中で `g_api->host_notice_read` があれば `rsh_lan_poll()` で通知を読む) に寄せる。入口の条件は「UART が使える (`serial_get_status()` で判定)、又は `host_notice_read` の slot がある」。実行中に `host_notice_read` が `NOSYS` を返したら「LAN の提供なし」として以後の LAN の poll をやめる (誤りとして扱わない、通知の消費の問題は起きない — 届いた通知は今と同じ処理)。
- **E5 の READY の `host_open` の NOSYS (PM 決定 2026-11-20 夜)**: READY の `host_open` が `NOSYS` の間は今の x86 と同じく再試行を続ける (共通の `link_host_open` はリンクの初期化の前にも NOSYS を返すので、そこで止めると LAN が遅れて上がる x86 の構成を壊す)。「LAN の提供なし」として poll をやめる条件は `host_notice_read` の NOSYS だけ。UART も無い時は active のまま rshell でローカルの入力を処理する (それでよい)。
- **E5・E6 の SFS の受入の台本 (PM 決定 2026-11-20 夜、sol の E5 のレビューを受けて)**: tools/accept/ppc_sfs.py の kboot-link-sh の台本を `/sys/shell.bin` の起動に切り替えるのは、`/sys/shell.bin` が像に入る PowerPC の段 E で (7450・7455 の両方で確かめる)。E6 で APP 専用の SFS の分岐を消したので、段 E と同じ統合の枝で入れるまで、この台本の正常系は単独では通らない。
