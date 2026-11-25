# PPC_FP_NEWLIB_DESIGN — PowerPC: ハードウェアの FPU とユーザーランドの newlib

> 状態: **現行 (2026-11-16)** — 確定設計の写し。段の状態は票 TASK_PPC_FP_NEWLIB が持つ。
> 出所: ~/os32-tmp/run/ppc-fp/DESIGN.final.md (gpt-6.1-sol 起草 r1・r2、Opus 第 1・2 回レビュー、PM が反映)。
> ユーザー決定 (2026-11-16): 他アーキのユーザーランドも newlib (AC-LIBC-1)。カーネルに libgcc を入れない。PPC は G3 以上が対象で FPU があるので、判定せずハードウェアの FPU を使う (x86 は本線が「判定してから使う」に改修)。

## r1→r2 の変更
- R1: x86 の固定3ファイルは変更せず、新規 PPC 道具で import・PPC 帳簿だけ追加し x86 bytes を比較。
- R2: QEMU 受入を 7450・7455 に限定し、750 は USER/SQLite TU の最終 ELF 静的監査のみ。
- R3: kernel の -mcpu=7450 を維持し、750 互換の範囲を USER/SQLite TU に統一。
- R4: begin は int を返し、非0なら sqlite_engine_run は0で失敗し fatal にしない。
- R5: SQLite TU に -msdata=none/-mno-altivec を維持し、許容する ABI 警告を限定。
- R6: §7 と完了条件に ROLES §4-3 の SKIP/必須失敗契約と CHECK 登録を追加。
- R7: f0 非破壊保存の根拠を O に倣う念のための措置へ修正。
- R8: reclaim/再使用での消去を不要とし、launch の init を使用。
- R9: 0x275000 を常駐像サイズ上限とし、root_image_blob.o の常時算入を明記。
- R10: ISA 監査を最終 ELF に限定し、SDA の項を確認済みに修正。

# PowerPC hardware FP と newlib nano 詳細設計 r2
基点: `30791b4c`、2026-11-16。設計のみ。起動受入は 7450・7455、750 互換は USER/SQLite TU の最終 ELF 静的監査のみ。単一 CPU・協調切替、FPU 必須。以下の断片は境界の仕様であり実装ではない。

## 1. 実物の確認
共通化の判断: newlib/adapter/exec の口は x86 と共用し、機械命令・保存域・旗・リンクだけ arch 別にする。
AC-LIBC-1 (`docs/decisions/ARCH_COMPAT_CONTRACT.md`) と AC-BUILD-1 (:130) に沿う。FP 専用の契約 ID は足さない (確定の S1)。FP は既存の exec_arch.h の不透明な口 (arch_fp_save・restore・init) の中で閉じる。互換の約束に FP の項目が要るかは、本線の FPU の設計と合わせて本線が決める。
整数優先 (`docs/POLICY_DEV.md` §2) の例外は今回承認された SQLite と newlib に限定。カーネルには newlib/libgcc を入れない。
「食い違い」は承認された到達点に対する不足を次の **10 件**と数える。前提の取り消しを意味しない。

|ID|実物 (file:line)|到達点との差・処置|
|---|---|---|
|D1|`sys/arch/ppc/exec_context.c`|FP 三関数が空。専用 .S へ移す|
|D2|`sys/arch/ppc/arch_exec_storage.h` の `ARCH_FP_BYTES`|保存域 1B/align1。264B/align8 にする|
|D3|`sys/arch/ppc/ppc_oea.h` の `PPC_MSR_POW`、`kernel_vectors.S`|FP/FE の定義と例外内 FP 有効化が無い。明示使用区間を設ける|
|D4|`sys/arch/ppc/kernel_trap.c` の `ppc_trap_dispatch()`|USER の 0x800 は汎用 kill、特権側は unknown。専用理由を付ける|
|D5|`build/arch/ppc.mk`、`build/targets/ppc-powermac.mk`、`build/ppc.mk`|全体 soft-float、CPU は 7450。kernel は -mcpu=7450 維持。USER/SQLite TU を hard-float・750 互換にする|
|D6|`build/ppc-user.mk`|libc/libgcc/adapter/syscalls がリンクされない|
|D7|`sdk/crt/ppc/start.c` の `os32_start_c()` 対 `sdk/crt/crt0_c.c`|_init と help の共通動作が無い|
|D8|`sdk/link/ppc/app.ld`|small-data/GOT/fixup の明示配置が無い|
|D9|`sdk/allocator/build_nano.py`、`tools/check_nano_inputs.py`、`sdk/allocator/check_link.py`|i386 固定の3ファイルは変更せず、新規 PPC 道具から import する|
|D10|`sys/conf/features/sqlite.mk`、`lib/sqlite3/sqlite_stack.asm`|別スタックは x86 のみ。PPC の口を先行して作る|

追加の結線上の注意: `build/ppc-user.mk` の shell 専用 format/stdio/string/runtime は newlib と定義が重複しうる。N3 の provider 点検対象に含める。
現行 `sys/include/exec_arch.h` の「kernel integer-only」は FP 区間以外と読み替え、仕様コメントも直す。x86 の保存形式 108B は維持 (`sys/arch/x86/kernel/x87.c`)。

## 2. FP の扱い (カーネル)
### 方針と手本の順序
参照の略号: N=`/home/hight/os32-tmp/ref/netbsd`、O=`.../openbsd`、F=`.../freebsd`。
M=`.../vendor/text/2004_MPC7450UM_MPC7450_RISC_Microprocessor_Family_Users_Manual_Rev_3.1.txt`。GPL/QEMU ソース・archive は使用しない。

|手順|BSD が行う順・前提 (file:line)|OS32 の対応・相違の理由|
|---|---|---|
|取得|N `sys/arch/powerpc/powerpc/trap.c:342-345` の USER EXC_FPU → fpu_load、`fpu.c:72-94` の fpu_state_load|遅延取得は採らず、USER の初回 entry/resume 前に状態を用意し SRR1[FP]=1。協調切替では所有者探索を増やす利点が小さい|
|初期化|N `fpu.c:76-78` は未使用 PCB をゼロ化してから load|新規 USER の全 FPR と FPSCR をゼロ化。前 owner の値を残さない|
|MSR|N `fpu.c:82-92` は旧 MSR を保存 → EE=0/FP=1 → isync → load → sync → 旧 MSR/isync|save/restore/init と kernel FP 区間で同じ順。一般の例外 C/IRQ では FP=0 を維持|
|保存|N `fpu.c:102-115` → `fpu_subr.S:78-117` は全 FPR → mffs f0 → FPSCR → sync|同順。ただし save は最後に f0 を再ロード。O に倣い、念のため非破壊で保存する。O `fpu.c:157-158` の save_fpu も f0 を戻す|
|復元|N `fpu_subr.S:34-73` は FPSCR を f0 経由で mtfsf、次に f0〜31|同順。f0 の最終値を FPSCR の一時値にしない|
|所有権解放|N `fpu.c:120-123` は保存 frame の FP を落とす。O `fpu.c:160-164` は frame/CPU/PCB の owner を解除|遅延所有者は持たず AppSlot と親保存域で完結。切替で必ず保存/復元するため owner CPU は不要|
|カーネル利用|上記 N/O の load/save には SQLite 用区間は **無い**。N `fpu.c:74,105` は PCB、O `fpu.c:108-124` は ci_fpuproc を対象とする|OS32 独自 guard。USER と SQLite の状態を分離する必要がある|
|別スタック/newlib|上記 fpu/trap の関数には **無い**。扱う対象は PCB と例外 frame|OS32 の既存 sqlite_call_on_alt_stack/CRT 契約に従う。BSD の FP 所有処理から類推しない|

- M §4.5 表4-6 (:17894 以降) は sc/外部/DEC を含む例外で FP/FE0/FE1 を 0 にする。起動時だけ FP を立てても syscall 内では使えない。
- M §4.6.8 (:18447) は FP=0 の FP load/store/move も 0x800 にする。`docs/ppc/cpu-state.md` と一致。
- FE0=FE1=0 (mask `0x800|0x100`)、FPSCR 初期値=0: RN=nearest、NI=0、例外 enable/status=0。M 表2-4 (:6470-6485)。NaN/Inf/丸め状態は trap を起こさず FPSCR に残す。
- USER の FPSCR は丸ごと保存する。MSR の FE は常に 0 に正規化し、浮動小数点シグナル/fenv の trap モードは今回の公開契約にしない。
- FP は USER と明示 kernel FP 区間で有効。IRQ/DEC、fault 診断、一般カーネル C で FP を使わない。割込み入口で落ちた FP は SRR1 により元の区間へ復帰する。
- 0x800 を自動再試行しない。USER は専用診断後 `ring3_fault_kill()`、kernel は専用 fatal (IRQ からでも同じ)。初回 USER で発生したら実装不良として受入 RED。

### 保存域と境界
```c
/* arch 内部の layout。実数演算を要求しない uint64 の bit image */
struct ppc_fp_image { u64 fpr[32]; u64 fpscr_image; }; /* 264B, align8 */
/* arch_fp_context.opaque: [0,256)=FPR、[256,264)=mffs の像 */
void arch_fp_save(struct arch_fp_context *out); /* hardware は不変 */
void arch_fp_restore(const struct arch_fp_context *in);
void arch_fp_init(void); /* zero image を復元 */
struct ppc_kernel_fp_guard {
    struct arch_fp_context previous;
    u32 previous_msr;
};
int ppc_kernel_fp_begin(struct ppc_kernel_fp_guard *g);
void ppc_kernel_fp_end(const struct ppc_kernel_fp_guard *g);
```
- asm は FP=1/EE=0 で操作し sync 後に旧 MSR/isync。GPR/CR/LR の SysV 非揮発規則も守る。restore/init は FPR を置換する特殊口なので呼出し元を soft-float に限定する。
- `AppSlot.fp_state` (`sys/kern/exec/appslot.h`) が休止 USER を所有。`fp_parent[exec_nest_level]` が親を所有し、像の書換えで共有一時バッファを使わない。
- launch: `exec_launch.inc` 親 save → init → entry。park: `exec_syscall.inc` USER frame → FP 保存。resume: `exec_schedule.inc` 親 save → 子 restore。
- 終了/fault/kill/park の longjmp 後は `exec_launch.inc` / `exec_schedule.inc` で親 restore。FP 保存域は slot と同寿命、reclaim/再使用での消去は不要。launch は init を使い、動的確保しない。
- 通常 syscall では USER の FPR がハードウェアに残る。一般 kernel は soft-float のため触らず、SQLite だけ guard が退避する。非揮発 FPR と FPSCR が通常の syscall 往復でも保たれる。
- begin: 成功は0、IRQ/例外深度・再入の拒否は非0。拒否時は状態を変えず sqlite_engine_run が0 (失敗) を返し、fatal にしない。成功時は IRQ 停止 → 旧 MSR/全 FP 保存 → FP=1、FE=0 → init → 元の EE を戻す。
- end: IRQ 停止 → 全 FP 復元 → 旧 MSR/isync。begin と end の間に park/exec/longjmp/ユーザー callback は許可しない。SQLite のエラーも必ず通常 return して end を通る。
- guard は生き残る kernel stack 上。sqlite_engine_active の既存再入防止と併用。IRQ は FP を触らないため長い SQLite 実行中も EE を許可できる。
- USER 初回 `exec_enter.S` と `exec_resume.S` の SRR1 に FP=1/FE=0 を反映し、通常 USER rfi の値も検査する (`kernel_vectors.S`)。early boot は FP 不要。
- x86 は今回 guard を呼ばず従来の x87/exec の道を保つ。x86 の判定後利用への改修は別本線。共有コメント更新で x86 の機械語は変えない。

## 3. 組み立ての旗
|像/TU|旗の設計|監査|
|---|---|---|
|boot `build/ppc.mk`|soft-float 維持、CPU は 7450 維持、Altivec 無効|FP 命令ゼロ、libc/libgcc ゼロ|
|kernel 共通、trap/IRQ、guard 呼出し側|`-mcpu=7450 -msoft-float -mno-altivec -msdata=none` 維持|復号した命令を全 object で照合|
|FP の専用 .S|命令を明示、G3 の FPR/FPSCR 命令だけ|許可はファイル/関数範囲を限定|
|SQLite の実数演算 TU (IME 段)|soft-float を除き `-mhard-float -mcpu=750 -mcall-sysv -mno-eabi -msdata=none -mno-altivec`|kernel 全体の旗は外さない。float 引数/戻り値を soft 側へ渡さない|
|USER/CRT/adapter `build/ppc-user.mk`|soft-float を除き hard-float、750/SysV/no-eabi/no-altivec/msdata=none|libc/libgcc も ELF32 BE・選択 multilib を一致|

- 共通と kboot の双方で TU ごとの旗を一意に指定。後置で偶然上書きする形を避け、soft/hard の二重指定を検査する。
- hard-float TU は整数だけの関数にも FP load/store を使いうる (コピー最適化等)。`-fno-builtin` だけに頼らず一般 kernel は soft-float のまま、最終逆アセンブルで拒否する。
- FP 許可 TU の整数公開口だけを `sqlite_engine_run` 経由で呼ぶ。将来の sqlite_entry/object 全体を allowlist にし、任意の kernel object を許可しない。
- `build/targets/ppc-powermac.mk` は GCC driver による no-undefined と整数命令監査がある。FP 三関数と stack/guard の関数範囲だけ監査例外を追加し、残りの禁止を維持。
- 各 ELF は `powerpc-elf-nm -u` が空、map に libgcc/libc が無いことを確認。hard-float でも i64 変換・long double・整数除算の helper は残りうる。
- IME の SQLite object には `nm -u`、最終 map/`objdump -dr` で `__*df*`/`__*sf*`/`__*di*` 等を列挙。通常 float/double は FPU 命令で満たし、不足 helper は必要な bit/整数算法だけ OS32 `lib/` に BSD/MIT 根拠で実装する別段へ。libgcc 追加や成功 stub で塞がない。
- kernel/boot の -mcpu=7450 は維持する。750 互換は USER と SQLite TU の命令だけに求め、最終 ELF の該当範囲を逆アセンブルして監査する。archive 自体は ISA 判定対象にしない (_restvr_* の未抽出 member による誤検出を防ぐ)。不適合なら同版・同 configure・無改変で750向けに再構築する。x86 は再構築しない。
- 許容する ld 警告は hard-float SQLite TU と soft-float kernel の組合せによる Tag_GNU_Power_ABI_FP の不一致だけとし、その他の警告は失敗にする。

## 4. newlib の結線 (N1〜N3)
### N1: target と入力の固定
- `nano_inputs.json` の既存 schema=2、toolchain/sdk_build の内容・hash は一切動かさず、追加 top-level `targets.powerpc-elf` に対応する同形の帳簿を置く。
- 新規 PPC 道具だけで target を選択し、既存 x86 道具の引数と省略時動作は変えない。ledger 選択→prefix/bin の target 接頭辞→gcc の `-print-file-name=libc.a`/`-print-libgcc-file-name`/`-print-multi-directory` で実在ファイルを解決する。
- x86 では従来の実行引数・archive member 順・adapter flags・receipt 形式をそのまま使う。PPC だけ elf32ppc emulation と ABI 旗を選ぶ。
- PPC 帳簿には newlib 4.4.0.20231231、GCC13.2.0、同 upstream SHA、configure、CFLAGS、patches=[]、libc/libg/member hash、symbol→member、adapter 入力、multilib/BE/hard-float を記録する。
- configure は `tools/ci/build_cross_ppc.sh` の disable-multilib/supplied-syscalls、enable-nano-malloc/formatted-io。target/prefix のアーキ差以外は x86 と同じにする。
- build_nano.py・check_link.py・tools/check_nano_inputs.py は変更しない。新規 sdk/allocator/build_nano_target.py・check_link_target.py と tools/check_nano_inputs_target.py から既存道具を import し、PPC 選択/receipt/builder を担う。import してよいのは target に依らない関数 (require・digest・compare・sdk_inputs など) だけ。command・inventory・build_inputs・check・cache_key・build_nano.build のように i386 の接頭辞や x86 の BUILDER に結び付く物は、PPC の側に書く。既存のモジュールの大域変数は差し替えない (確定の S2)。x86 の ledger 節は1文字も変えず、cache_key に PPC 節を混ぜない。
- PPC builder には構築 receipt を記録/照合する呼出しを追加。既設バイナリの hash だけで「同じ configure」を証明したことにしない。x86 の --record-built に当たる「今ある導入物 (~/opt/cross-ppc) を、build_cross_ppc.sh の --dry-run の configure と照らして記録する」経路を PPC の道具に用意する。再構築は、記録と合わないときだけにする (確定の S3、ユーザー決定「今の hard-float の cross-ppc を使う」)。
- build_nano は libc を改変せず同じ members を抽出・os32_private_* に改名、共通 nano_adapter.c を PPC 旗で組む。BE の構造体配置・整数幅を host32/QEMU で確認する。
- 出力は `build/out/ppc-user/nano/`、CRT receipt も PPC syscalls.o のもの。reuse は同 target/ABI/receipt を必須とし、x86 archive の流用を拒否する。
- 改修前後を同じ絶対出力パス・同じ prefix/ソース/時刻条件で構築し、x86 libos32nano.a を `cmp`/SHA256 で比較。x86 の節 (toolchain・sdk_build) の JSON を正規化した値と、`--cache-key` の出力が前後で一致することを比較する (確定の S4)。DWARF パス差を差分理由として見逃さない。

### N2: CRT・リンカ・syscalls
- `sdk/crt/ppc/start.c` に x86 と同じ `_init()`→help 処理→main→sys_exit を入れる。kapi/aux は先に設定。help 未発見なら main を呼び、見つかれば exit=0。
- 今回は x86 crt0_c.c を共通関数へ移動しない。x86 の生成 bytes を固定するため PPC の C 入口だけ揃え、共通挙動の fixture で双方を検査する。
- `crt0.S` の16B call frame/loader BSS 初期化を維持。FP 初期化は CRT でなく exec が担い、起動直後の hard-float 可変長関数にも間に合わせる。
- `app.ld`: RO の `.sdata2*` を text 側、RW の `.sdata* .got* .got2 .fixup` を data 側、`.sbss* .sbss2*` を bss 側へ明示収容。境界を 8/16B 整列し、全 RW 初期値を code_size の後、全 NOLOAD を bss_size 内に置く。
- `.init/.fini` と init/fini-array は保持/配置を明示するが、今回は x86 と同じ `_init` 契約。孤児 alloc section を map/readelf で拒否し、ctors 実行を暗黙に約束しない。
- SDA は確認済み。-msdata=none の利用者に r2/r13 相対参照を要求する newlib member はない。最終リンクの reloc 検査でこの条件の退行を検出する。
- 純 C の `sdk/crt/syscalls.c` を PPC 用別 object として組み、生成 KAPI header/unit stamp・newlib headers を使用。既存 `_sbrk`→adapter→KAPI の鎖を保つ 。x86 の object/rule を変えない。
- syscalls.c が取り込む ls.c 等の依存も PPC で closure を検査し、BSS に environ/errno/reent が収まることを確認する。

### N3: 全 USER の結線と受入
- hello/fault/nest/outprobe だけでなく sh も切替える。shell 固有の KAPI glue は残し、libc と重複する format/stdio/string の提供を build 選択から除く。旧 include stub が newlib headers を隠さないよう include 順も直す。
- 同じ名前の `kapi` や sys_exit glue の二重提供は nm/provider 監査で拒否。必要な shell 固有 runtime 部分の抽出は独立ファイル段にし、newlib 代用品を増やさない。
- リンク順: `crt0.o start.o syscalls.o help.o app.o [必要な OS32 USER libs] libos32nano.a -lc -lgcc`。相互依存が出る場合だけ末尾3 archive を group 化。`-u _printf_float` で nano の float formatter を明示抽出する。
- link_guard による世代検査を維持し、check_link は target 別 receipt/map を確認。malloc 系の public 定義は共通 adapter のみ、private nano/sbrkr/CRT は信頼した member/hash に一致させる。
- PPC テストアプリは `printf("%.2f",1.25)`→`1.25`、可変長 double を複数渡す関数、malloc/calloc/realloc/free の内容・失敗・64KiB 境界、fopen→write→close→read/EOF/errno を検査。
- QEMU の 7450 と 7455 の起動で同じアプリを実行。newlib が cr1 の varargs 条件で stfd を通るケースを必ず含める。shell 起動/外部 exec/--help/終了値も旧挙動と照合する。
- fopen 試験ファイルは専用の一時 FS に作り、終了/fault/kill 後の FD/heap owner 回収と同名再実行を確認。x86 は既存 newlib/allocator/CRT 試験と同一条件の生成物比較を回す。

## 5. カーネル SQLite の別スタック (今回は .S と口だけ)
> 2026-11-18: 本節・§7 の S の 3 行・§8 の S1 は [PPC_SQLITE_STACK_DESIGN](PPC_SQLITE_STACK_DESIGN.md) (11-17 の層の規則に合わせた置き場と口の形) で置き換えた。ABI・保存するレジスタ・guard の順は本節のまま。

```c
/* 既存口と同一。float の引数/戻り値は境界を越えない */
void sqlite_call_on_alt_stack(void (*call)(void *), u32 top, void *frame);
```
- `lib/sqlite3/sqlite_stack_ppc.S`: r3=call/r4=top/r5=frame。元 stack 上に16B整列フレームを確保し LR と使用する非揮発 GPR (例 r30/r31) を保存。未使用の r2/r13/CR 非揮発部を壊さない。
- 旧 r1 と callback を非揮発 GPR に保持→新 r1=align_down(top,16)−16→backchain に旧 r1→LR 保存用 linkage を確保→r3=frame、mtctr/bctrl。
- callback は hard-float SysV の非揮発 f14〜31 を保存する。trampoline 自身は FP を触らず、戻ったら旧 r1/LR/GPR を復元して blr。top の予約域/guard を越えない。
- 呼出し前後の全 FP 保護は §2 の guard。`sqlite_engine_run` (`sys/kern/sqlite_engine.c`) の active/reentry/IRQ/exception 拒否を通過した後 begin。非0なら active を解除し0を返す。成功時だけ alt stack、end、active 解除の順。
- BSD の根拠: N `.../setjmp.S:36-66,76-104` は r1/非揮発 GPR/CR/LR を保持するが FP はコメントのみ。F `lib/libc/powerpc/gen/_setjmp.S:59,86` は f14〜31 を実保存する。
- SQLite の stack swap 自体の手本はこれらには **無い**。OS32 x86 `sqlite_stack.asm:14-26` の「旧 stack 保持→整列した別 stack→復元」と `docs/ppc/abi.md` の根拠を使う。
- sys/conf/features/sqlite.mk は ARCH により asm/.S を排他選択。今回は合成 callback だけをリンクする専用 test。SQLite 本体、辞書、IME provider の有効化は後段で、未提供を stub 成功にしない。

## 6. 部品の大きさの見積もり
数値は **KiB の予算**で実測ではない。現作業木に build/out が無く、基点 ELF の余裕は未測定。archive の disk 長 (libc 約4621KiB/libgcc 約741KiB) は像の増分ではない。

|部品|入る像|予算増分|kboot の上限への影響|
|---|---|---|---|
|FP .S/guard/診断|PPC kernel/kboot|text/rodata 2〜5KiB|常駐像に加算|
|264B 保存域|AppSlot/親 FP 域、guard は stack|旧1Bから各263B + align padding、概算2〜4KiB|BSS 増。slot/親の実配列数から厳密計上|
|stack trampoline|専用試験、後段 SQLite kernel|0.2〜0.5KiB|実体を使う像だけ|
|newlib/adapter/CRT|各 USER bin、root image 経由で kboot にも|小 app 20〜60KiB、%f 使用 app 40〜100KiB、shell 30〜100KiB (旧部品の減少を差引)|同梱 bin 数 × 増分が支配的|
|SQLite 本体/別 stack|IME 段の kernel/module|今回0、本体は次段で実測|今回の余裕を本体込みと誤認しない|

- `sys/arch/ppc/kernel.ld` の file/resident/chain end を個別に記録し、常駐像サイズ (__kernel_resident_end - __kernel_image_start) の上限 `0x275000` と照合する。root_image_blob.o は、KB_EXEC が有効な像 (KBOOT_SH=1 か KBOOT_SHELL=1、build/targets/ppc-powermac.mk) では build/targets/ppc-powermac.mk の KB_EXTRA に常に入り、その増分も算入する。KBOOT_SH=0 の診断像には入らない。別に heap 等を含む最終 chain を PM_KERNEL_CHAIN_LIMIT と照合する (同じ上限と取り違えない)。
- 上限は正典の layout 定数を使い、設計の値を build に複写しない。上限直前の予算だけで合格にせず linker ASSERT と iso/ELF の実サイズを通す。
- N3 の全 USER を詰めた像で再測定し、超過なら test apps を外部試験媒体に置く。製品 shell の機能を削ったり窓を拡大して通さない。

## 7. ファイルごとの変更一覧
各行=1ファイル=1段=1実装者。宣言/結線を先に確定し、同波の別行だけ並列。既存の登録ファイルには最小の登録だけを置く。行数は追加/変更のおよそ。

|波・段|ファイル|新/変・行数|機能/依存|
|---|---|---|---|
|A宣言|sys/include/exec_arch.h|変8|FP 利用コメント、公開口は維持|
|A保存域|sys/arch/ppc/arch_exec_storage.h|変8|264B/align8|
|Aビット|sys/arch/ppc/ppc_oea.h|変6|FP/FE 定数|
|A guard口|sys/arch/ppc/kernel_fp.h|新35|guard layout/API|
|B FP実体|sys/arch/ppc/fp_context.S|新180|save/restore/init、A後|
|B空実体除去|sys/arch/ppc/exec_context.c|変3|旧三関数を除去、B統合時|
|B guard|sys/arch/ppc/kernel_fp.c|新85|明示区間、B FP後|
|B初回entry|sys/arch/ppc/exec_enter.S|変5|USER SRR1 合成に FP|
|B resume|sys/arch/ppc/exec_resume.S|変8|USER SRR1 正規化|
|B通常復帰|sys/arch/ppc/kernel_vectors.S|変8|USER SRR1 の FE をマスク|
|B理由定義|sys/arch/ppc/ppc_trap.h|変3|0x800 専用理由の enum 追加|
|B fault|sys/arch/ppc/kernel_trap.c|変12|0x800 専用理由|
|B登録|build/targets/ppc-powermac.mk|変20|FP objects/audit登録、CPU 7450 維持|
|B旗|build/arch/ppc.mk|変10|kernel 7450 維持、USER hard-float 分離|
|B boot|build/ppc.mk|変更なし|7450、soft 維持|
|B監査|tools/ppc/kboot_audit.py|変60|限定 FP 許可、無関係命令は禁止|
|N1在庫|sdk/allocator/nano_inputs.json|変150|PPC 節だけ追加|
|N1照合|tools/check_nano_inputs_target.py|新100|既存を import、PPC receipt/builder/cache|
|N1構築|sdk/allocator/build_nano_target.py|新55|既存を import、PPC 抽出/ABI旗|
|N1リンク検査|sdk/allocator/check_link_target.py|新35|既存を import、PPC provider/receipt|
|N1 CI|tools/ci/build_cross_ppc.sh|変20|構築 receipt、x86 script 不変|
|N2 CRT|sdk/crt/ppc/start.c|変35|_init/help、N1後|
|N2配置|sdk/link/ppc/app.ld|変20|明示 section 収容|
|N3 shell glue|userland/ppc/sh/newlib_runtime.c|新80以内|既存 runtime の固有役割だけ、定義棚卸し後|
|N3結線|build/ppc-user.mk|変85|全USER/新glue/nano/syscalls/help、N1/N2後|
|S stack|lib/sqlite3/sqlite_stack_ppc.S|新65|別 stack ABI、A後|
|S guard結線|sys/kern/sqlite_engine.c|変12|PPC 条件の begin/end、B後|
|S選択|sys/conf/features/sqlite.mk|変8|arch 排他選択|
|T FP host|tools/tests/test_ppc_fp.py|新180|保存layout/命令/旗/禁止範囲|
|T nano host|tools/tests/test_nano_targets.py|新190|x86 bytes/cache と PPC provider|
|T FP guest|userland/ppc/fp_probe.S|新180|全FPR/FPSCR/可変長 ABI helper|
|T libc guest|userland/ppc/newlib_probe.c|新160|%f/heap/file/exit|
|T stack guest|tools/ppc/sqlite_stack_probe.S|新120|合成callback/非揮発/stack整列|
|T受入|tools/accept/ppc_fp.py|新200|実ゲスト観測/変異/対照、全波後|
|T登録|tools/checks/*.py|追加|新試験の CHECK 登録と入力/根拠/dep-dir 宣言|

ROLES §4-3: toolchain/QEMU を使う各新試験は PPC_CROSS_DIR=/nonexistent で SKIP・rc=0、OS32_REQUIRE_PPC=1 併用で失敗する契約を持つ。

`build/programs.mk`、共通 `nano_adapter.c/.h`、`sdk/crt/syscalls.c`、x86 CRT/旗/CI builder は変更不要。PPC 用に再コンパイルする。共有 sdk_build 入力 hash も不変。
S guard結線は IME まで製品リンクしないが専用試験で組む。既存巨大ファイルへの新機能本体は追加せず、登録と構造体の変更だけを例外とする。

## 8. 受入 → 試験・変異・段の区切り
各変異は一意 anchor、元版 GREEN→変異版の **実行結果 RED**→元版 GREEN を記録。静的 grep の失敗だけを変異合格にしない。

|受入ID|host / QEMU 観測|変異 (実行時の RED)|
|---|---|---|
|F1 初回FP|host MSR旗、7450/7455 USER の最初の fadd が成功|entry の FP 設定を外す→0x800/faultでRED|
|F2 全状態隔離|asm A/B が全32FPR/異なるRN・statusを設定、park/resume/入れ子/再起動後に bit 比較|特定 stfd を外す→別アプリの値/旧値でRED。FPSCR保存除去も別変異|
|F3 kernel隔離|合成 kernel FP callback で全FPR/FPSCRを壊し、USER syscall 前後と IRQ 中断後を比較|guard restore を外す→USER値不一致でRED|
|F4 故障扱い|USERでFPを意図的に落とすfixture→専用fault、IRQ FP誤用→kernel fatal|0x800を無条件再試行→timeoutでRED|
|F5 回収|fault/kill/通常exit後にslot再使用、FPRゼロ/FD/heapの回収を確認|initのFPRゼロ化を外す→前owner値でRED|
|N1 在庫/退行|host target違い/receipt違い拒否、x86 archive/cache-key/生成物をbyte比較|PPC receiptをi386に差替え→構築fixtureの実行が失敗してRED|
|N2 CRT/配置|host section/reloc/ABI、QEMU _init副作用・help成功/不在→main・bss/sdata sentinel|_init呼出し除去またはsbss収容除去→sentinel不一致でRED|
|N3 libc|QEMU %f/複数double varargs/malloc境界/再確保/fopen/errno/終了値、shell基本操作|_printf_float抽出除去→出力不一致、adapter経路破壊→heap検査RED|
|S1 別stack|QEMU callback内r1/整列/引数、帰還後LR/GPR/非揮発FPR/canary比較|r1復元元を壊す→制御帰還失敗/canary不一致でRED|
|B1 像/closure|host nm/map/命令監査/size ASSERT、両archの対象ビルド|一般kernel TUにFP命令fixture追加→監査実行が拒否してRED|

- A は宣言だけ半日、別ファイル並列可。B は各行半日〜1日、FP実体→guard→結線の依存以外は並列可。
- N1 の4道具は引数契約確定後に並列、帳簿/CI receipt 統合を先に検査。N2/S は別ファイルで並列、N3 は N1/N2/B 完了後。T の観測契約は A と同時に確定する。
- 手本のある宣言/移植中はビルドせず、各波の実装が揃った時点で触った PPC kernel/kboot/全USER/stack試験を組む。共有道具を変えた波は x86 programs と byte 比較も必須。
- 完了条件: 各新試験の CHECK を tools/checks/*.py に登録し、ROLES §4-3 の PPC_CROSS_DIR=/nonexistent で SKIP・rc=0、OS32_REQUIRE_PPC=1 併用で失敗を確認する。750 は USER/SQLite TU の最終 ELF 静的 ISA 監査だけとし、G3 起動対応は別段として後で扱う。
- 最終ソース変更後に当該 host tests の `--mutate` と `python3 -B tools/checks/run.py --lint`。各新 test の CHECK に入力/根拠/dep-dir を列挙し、既存変異 anchor の移動漏れを tools/tests の検索で洗う。
- 取り込みは CI の check/build/check-fast。check-changed/ローカル全変異は実行しない。QEMU 受入は PM が 7450/7455 各1回に統合してログ/終了値/変異対照を残す。x86 回帰は本線の既存 runner で行う。
- この起草ではソース・成果物を変更していないためビルド/実行試験/独立レビューは未実施。読み取り専用指定を優先し、字体をコピーする preflight --fix-fonts も未実施。実装開始時は base確認/preflight を必須とする。

## 9. 未決 (ユーザー判断だけ)
**0 件。** 明示 FP 区間、FE=00、264B 保存、newlib の既存 adapter 方式を推奨案として具体化した。ISA/receipt/窓余裕の確認は実装の検証事項であり、ユーザーへの追加決裁にはしない。

## 確定 (2026-11-16、Opus 第 2 回、PM が反映)
- S1 (P2): AC-FP-1 を ARCH_COMPAT_CONTRACT に足す案は外した。FP は exec_arch.h の口の中で閉じる。互換の約束の FP の項目は、本線の FPU の設計と合わせて本線が決める。
- S2 (P2): import してよい関数を target に依らない物に限り、モジュールの大域変数を差し替えない、と §4 N1 に書いた。
- S3 (P2): 今ある cross-ppc を記録する経路を置き、再構築は記録と合わないときだけ、と §4 N1 に書いた。build_cross_ppc.sh に足すのは記録の出力だけで、configure は変えない。
- S4 (P3): x86 の bytes の確かめを「x86 の節の JSON の正規化した値と cache_key の一致」に直した。
- (実装の段 ppc-fp-b の停止を受けて PM が直した) §6 の root_image_blob.o は、KB_EXEC が有効な像に限ると書き直した。
