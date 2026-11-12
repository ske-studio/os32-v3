# PPC_SHELL_EXEC_DESIGN — PowerPC の最終の到達点 S-B (外部の USER アプリを起動し終了して呼び手へ戻る) の詳細設計

> 状態: **確定 (2026-11-11)** — USER の ABI (入口・sc の syscall・rfi と例外・子だけの終了)、OS32X の LE の wire を BE の CPU で読む直し、命令の公開の cache 同期、KAPI の 8B の stub、PPC の SDK (crt0・link・libgcc を link しない)、既存の exec の経路を PPC でも通すための facade の段 (F01〜F20)。全体の計画と中間 S-A は PPC_SHELL_DESIGN。段の状態は票 TASK_PPC_SHELL が持つ。

> 出所: ~/os32-tmp/run/ppc-shellb/DESIGN.final.md (gpt-6-astra 起草 r1→r2、Opus 5.5 の第 1 回 Request changes・第 2 回 条件付き Approve、PM が N1〜N4 を反映。R4 は PM 決定 (a) = 既存の exec の閉包を facade で切る、本線 PM も賛成)。中身は変えていない。


書いた：S-B 詳細設計案（読み取りのみ）
ユーザー判断が必要な未決：0件。公開 flag の ABI 版扱いは PM 判断。
ファイル変更・build・試験は未実施。閉包リンク成功の報告ではない。

**1. 範囲と実物の確認**

- 到達点は「外部 USER アプリを起動し、終了して呼び手へ戻る」。
- シングルタスク・協調切替を維持し、プリエンプティブなスケジューラは加えない。
- 整数・soft-float・AltiVec なし、module 静的 link は確定条件。
- S-A の LAN 端末・rd0 の ISO9660 root・rd1 の ramfs を利用する。
- S-A 中間部品は `/home/hight/wt/os32-v3/ppc-shw3` にあり、未取り込み。
- S-B の呼び手は開発用 CLI。W7 の製品起動は kernel → USER `sh.bin`。
- 新しい名前・定数・ファイル名は実装案。既存 API と区別する。

|#|実物との食い違い／不足|根拠と処置|
|---|---|---|
|1|OS32X wire は LE、kernel は native 構造体を参照|`exec/os32x_hdr.c:9`。読込み時に decode|
|2|ELF と配置刻印の読取りも LE 固定|`sdk/os32x_hdr.py:80–89,193`。EI_DATA に従う|
|3|PPC profile の SDK 能力は無効|`sdk/target_profile.py:18–20`。USER C 能力だけ開く|
|4|CRT は x86 stack 引数専用|`sdk/crt/crt0.asm:33–37`。PPC `_start` を別実装|
|5|USER 例外・SC は停止経路|`arch/ppc/kernel_trap.c:90–125`。既存 exec dispatch に接続|
|6|保存域・FP 域が暫定|`arch/ppc/arch_exec_storage.h`。整数 ABI に確定|
|7|KAPI 引数サイズ生成は i386 cdecl 前提|`sdk/gen_kapi.py:436–439`。target 別 adapter|
|8|文書の戻り値は status 表、非 GUI 実装は終了値|`exec/exec_launch.inc:342`。受入では7を返す|
|9|exec の主要2 TU は selected 成功一覧にない|`exec/exec.c`、`kapi/kapi_generated.c`。§9の facade 段で接続|

- `exec/`・`kapi/` の直下 `.c` と `tools/ppc/selected_success.json` の差は上記2 TU。
- `exec_syscall.inc` は独立 TU ではなく `exec.c` の閉包。成功一覧への不在は今回の再測定結果ではない。
- 停止依存の根拠は `exec.c:1–42`、`exec_syscall.inc:84–170` と関係する呼出しの限定 grep。
- USER 属性は既存：Ks=0/Kp=1、USER RO=PP1、USER RW=PP2（`pd_walk.c:10,44–46`）。
- root 切替・HPT/TLB 消去・BAT 撤去も既存。旧 B15・B16 の再実装は不要。
- libc 等の存在と ABI 適合は別。hello は newlib・libm・libnosys・libgcc をリンクしない。
- `SHELL_AS_APP` は既存。W7 の shell 全機能を今回の受入へ加えない。
- GPL・QEMU ソース・`docs/archive/` は未参照。r1 の無関係な根拠は再調査していない。

**2. S-B の受入の形**

|操作|観測する結果|
|---|---|
|LAN 端末から `/bin/hello.bin ok`|root 内の外部ファイルを読み、PR=1 で実行|
|hello が argc/argv を使用|`hello ok` と改行を出力|
|`main` が7を返す|CRT → `sys_exit(7)` → 呼び手の `exec_run` が7|
|CLI が結果を表示|`exit=7` の後、次のプロンプト|
|続けて同じコマンド|再度成功し、前回の子の資源を引き継がない|

```c
/* resident_cli_ops への追加。既存コマンド以外の行を渡す。 */
int (*run)(const char *line);
/* platform/powermac/cli_boot.c */
.run = exec_run; /* int exec_run(const char *cmdline); */
```

- CLI は引数分割で元の行を壊す前に外部起動へ渡す。戻り値を表示して入力ループへ戻る。
- `kernel/cli` は共通箇所として本線へ先行通知。x86 の build 対象でなければ★ではない。
- hello の組込み関数呼出しや PR=0 実行で代替しない。
- CLI は自分の stack・AS・端末状態へ戻る。root を再 mount しない。
- 終了値7と既存の負の exec エラーを使い分け、新しい結果構造体は作らない。

**3. USER ABI：入口と C 呼出し**

|項目|S-B 契約|
|---|---|
|データモデル|ELF32、BE、32bit pointer/int/long|
|C ABI|PPC32 SysV、`-mcall-sysv -mno-eabi -msdata=none`|
|入口|r3=argc、r4=argv、r5=api、r6=aux|
|aux|USER は0。kernel 内部サービス表を渡さない|
|stack|r1 は16B整列、下向き、red zone は使わない|
|argv|USER stack 上の BE pointer 配列、末尾 NULL、文字列は byte 列|
|予約レジスタ|r2/r13 に SDA・TLS・kernel pointer を載せない|
|非揮発レジスタ|通常 C ABI の r14–r31・CR 非揮発部分等を遵守|
|復帰|`_start` に C の呼び手はない。必ず syscall で終了|

- 通常 C の整数引数は r3–r10。EABI の SDA 初期化や8B stack alignment を混ぜない。
- 独自契約は入口4引数と syscall。Unix の初期 stack は模倣しない。
- 通常 C 内の u64・構造体・可変引数はコンパイラ ABI、syscall の対応型は§4に限定。

```c
/* arch/ppc/exec_entry.c の private 型。全欄 u32。 */
struct ppc_entry {
    u32 pc, sp;
    struct exec_entry_args args;
}; /* 24B */
sp = align_down(top, 16) - 16; /* top は argv 配置後 */
store(owner, sp, zero_linkage, 16);
entry = (struct ppc_entry){pc, sp, *args};
```

- `arch_entry_prepare` は commit 前に store を完了し、既存の減算・store エラー契約を維持。
- 呼び手が確定した argc は再検査しない。初期 LR=0、back chain=0。
- CRT は main を呼ぶための通常の linkage frame を作る。

**4. syscall と KAPI の境界**

|項目|契約|
|---|---|
|入口|KernelAPI 関数表 → USER trampoline → `sc` → 既存 exec syscall 経路|
|番号|r0。既存 KAPI slot 番号|
|引数|r3–r10、最大8個の32bit整数／pointer|
|戻り値|r3 の32bit値。負値も同じ bit pattern|
|破壊|r0・r3。他の USER 文脈は frame から復元|
|エラー|既存の負値。CR0.SO による errno 規約は採用しない|
|未対応|既知 slot は `OS32_ERR_NOSYS`、範囲外は既存の子終了規約|
|必須|`shell_putchar` と `sys_exit`。引数取得 syscall は不要|

```asm
# 1 slot = 8B。b は LR を変更しない。
li r0, SLOT
b  .Lsc
# 同じページ内に1組だけ置く
.Lsc:
sc
blr
```

- `KAPI_STUB_SIZE=8u`、関数表・scratch・shim の配置、1ページ制約は変えない。
- PPC の `ls_off` には `li r3,OS32_ERR_NOSYS; blr` を emit。S-B の SYS_LS は未対応。
- 共通 `.Lsc` は既存 shim 予約域の `ls_off+8` に置く。scratch を命令で上書きしない。
- `SYS_LS` の表要素は既存初期化どおり `ls_off`。USER callback を kernel で呼ばない。
- shim＋共通入口は16Bで既存192B枠内。通常 stub は共通入口への相対分岐を生成する。
- api の数値・関数 pointer・命令語は BE。OS32X wire の LE と混ぜない。
- USER 公開ページは RO。予約欄・slot 順・data offset は既存 KAPI のまま。
- `arch_kapi_source/read_block/invoke/result` を実装し、共通 dispatch の検証・回収を使う。
- adapter は型付き。異種関数を一律 cast して呼ばない。
- u64 直接引数／戻り値、構造体値渡し／値返し、可変引数 syscall は未対応。
- 未対応型を word 数だけで r3–r10 に詰めない。未使用の将来配置表も作らない。
- `shell_putchar(char, attr)` は PowerMac provider → S-A stream。queue と LAN service を進める。
- `sys_exit` は既存の終了値保存・子終了・親復帰へ接続。別の終了機構を作らない。
- generator は共通の番号・型・配置を使い、target 別 adapter・provider binding を出力する。
- `--target ppc-powermac --out-dir ...` を追加し、x86 の生成先を上書きしない。
- `__cdecl` も target 生成口で扱い、共有ソースに PPC 条件分岐を散らさない。

**5. PR=1 遷移・例外・親への復帰**

```c
/* arch_entry_enter の意味 */
irq_disable();
priv->kernel_sp = current_r1; /* 生存する kernel stack、恒等写像内 */
arch_mmu_load_root(child_root);
/* asm: 必要な値と全 GPR を先に読み終える */
SRR0 = entry_pc;
SRR1 = PPC_MSR_EE | PPC_MSR_PR | PPC_MSR_ME |
       PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_RI;
rfi(); /* mtsrr0/mtsrr1 と rfi の間に load を置かない */
```

- `ppc_oea.h` の名前を使い、不足する定義を追加。LE=SE=BE=FP=IP=0、VEC=0。
- BE bit は Branch trace。big-endian の指定ではない。USER の MSR 値は採用しない。
- PR=1 からの例外は、控えた kernel r1 の下に176B frame を作る。
- 入口では必要な最小退避後に保存 SRR1.PR で分岐し、USER r1 を kernel stack に使わない。
- USER 起源経路は大域 trap stack の busy 語を取得・解放しない。
- kernel 起源例外は既存の trap stack／busy 規約を維持し、通常の vector return で解除。
- syscall 中の kernel 再例外は後者へ入る。USER frame は生存する kernel stack に残る。
- `sys_exit`・USER fault の longjmp 後に busy=1 を残さない。子専用 kernel stack は確保しない。
- 通常 SC は保存 SRR0（sc の次の命令）へ復帰。復帰時も全 load を SRR 設定前に済ませる。
- IRQ・DEC は既存 handler 後、同じ USER 文脈へ戻す。HPT miss は既存 refill を先行する。

```c
if (is_irq_or_dec(f)) handle_and_return(f);
else if (from_user(f) && is_sc(f)) dispatch_user_sc(f);
else if (from_user(f)) exec_child_fault(f);
else if (user_copy_fault(f)) fail_user_copy(f);
else existing_kernel_fault(f);
```

- USER の未解決 DSI/ISI・不正命令・特権命令・alignment・FP unavailable は子だけ終了。
- kernel 一般 fault を「子がいる」だけで子終了へ変換しない。
- copy の限定復帰は既存 `ppc_trap_onfault`／`ppc_trap_onfault_data` を流用し、対象 copy 中だけ設定。
- `arch_kapi_read_block` は対応するレジスタだけ読み、USER stack を探らない。
- fault は `EXEC_ERR_FAULT` を保存し、正常終了と同じ共通回収経路へ合流する。
- 共有 kernel mapping 上で親 root に戻してから子 AS を解放する。
- FD・heap・ページ等を既存順で回収し、親 owner・heap・例外入口用 SP を復元。
- `exec_launch.inc:320–342` の生存 stack と setjmp 着地点、既存 pending-finish 規約を維持。
- PPC setjmp/longjmp は r1/r2/r13–r31・LR・CR に加え、IRQ・例外の深さ2語を保存・復元。
- この集合は25語＝100B。暫定 `ARCH_JMP_BYTES=96` を合わせ、保存 offset と整合させる。
- `ARCH_USER_BYTES=176`、entry=24B、priv は root と入口 kernel-SP の8Bを基本とする。
- FP 域は1B opaque placeholder、save/restore/init は no-op。FPR/FPSCR/vector 保存は作らない。

**6. 実行形式と命令公開**

- OS32X v5 の64Bヘッダ＋flat image。実行時 ELF loader は追加しない。
- PPC32 BE・整数 ABI v1 の識別に flag `0x0040`、ABI 名 `ppc32-sysv-soft-v1` を提案。
- bit がない既存 image は x86。PPC は bit を必須とし、x86 は拒否する。
- ELF の CLASS=1、DATA=2、machine=20 は包装時に照合。runtime は wire flag で識別。
- 公開定義は `sdk/include/os32/os32_kapi_shared.h`。共通箇所として本線へ先行通知。
- ABI 判断材料：公開 flag の追加だが KAPI slot・型・構造体配置は不変。版上げ要否は PM が決める。
- 将来の別 PPC ABI 用の空欄は増やさない。

```c
int os32x_decode(OS32Header *h, const u8 *wire, u32 len);
static u32 get_le32(const u8 *p)
{
    return (u32)p[0] | (u32)p[1]<<8 | (u32)p[2]<<16 | (u32)p[3]<<24;
}
/* 各 field を offset 指定で native 値へ変換する。 */
```

- byte buffer → decode → layout/target 検査 → AS 確保。magic の既存 wire bytes は変えない。
- `arch_exec_image_accept(flags)` で target 判定。x86 は既存検査を残す static inline、追加の口は空にする。
- PPC bit 拒否は共通 header 検査の★段で扱い、x86 の空 hook で検査を失わない。
- SDK の ELF header・section・symbol・刻印は EI_DATA の endian。raw payload は変換しない。
- RO prefix・BSS・entry 範囲の既存照合を再利用する。
- `exec_load_image` 後、kernel root のまま対象 PA の P2V 恒等 alias で命令公開する。
- 最終書込み後に `dcbst`（各 line）→ `sync` → `icbi`（各 line）→ `sync` → `isync`。
- stub も同じ方式で公開。子の実行 VA を使う別経路は作らない。
- x86 の publish hook は空の static inline。共通呼出し追加で x86 命令列を変えない。
- r1 の参照資料を継承：[MPC7450UM](https://www.nxp.com/docs/en/reference-manual/MPC7450UM.pdf)、[NetBSD の frame 実装](https://raw.githubusercontent.com/NetBSD/src/trunk/sys/arch/powerpc/powerpc/sig_machdep.c)。コードは独立に書く。

**7. address space と所有**

|範囲|属性・所有|
|---|---|
|既存 kernel・root・MMIO・例外 stack|各 AS で共有、supervisor のみ|
|`0x80100000` から code_size|子の物理ページ、RO+USER|
|続く data/BSS|子の物理ページ、RW+USER|
|`0x88000000` の exec heap|既存契約の子所有 RW+USER|
|`0x90000000` を上端とする stack|hello は16KiB、RW+USER、下に非 present guard|
|KAPI 表・stub・scratch|既存の1ページ配置、USER RO、kernel 書込み可|
|argv・文字列|子 stack 内。親 pointer を残さない|

- 高位 VA と物理 RAM を同一視しない。16MiB 管理内のページを割り当てる。
- `paging_addrspace_map_user_range_phys` 等と既存 arch current/load-root を利用。
- software の PTE_USER/PTE_RW を既存 refill が Ks=0/Kp=1、PP=1/2 に変換する。
- USER RO の PP=1 は supervisor 書込みを許し、既存 tramp 更新とも整合する。
- supervisor 専用 PP=0、既存 BAT 撤去を維持。USER の部分的 NX は仮定しない。
- root 更新は IRQ 禁止中、既存 HPT/TLB 無効化契約を使う。再実行で古い変換の残留を観測する。

**8. SDK と hello の生成**

```asm
# sdk/crt/ppc/crt0.S。BSS は loader がゼロ化済み。
_start:
    stwu r1,-16(r1)
    bl   os32_start_c
```

```c
void os32_start_c(int argc, char **argv, KernelAPI *api, void *aux)
{
    int rc;
    kapi = api;
    rc = main(argc, argv, api);
    api->sys_exit(rc);
    for (;;) {}
}
```

- link script は `elf32-powerpc`、`ENTRY(_start)`、base=`0x80100000`。
- `.text.startup/.text/.rodata` → 4KiB整列 → `.data` → `.bss(NOLOAD)`。
- `__code_end`・BSS 境界を定義。配置刻印は `(INFO)`＋KEEP、非ロード。
- USER C 能力だけ開く。resident・shlib・Rust・module codec は未対応。
- `sdk_values` の全項目必須を機能単位に分け、未対応項目をダミー値で埋めない。
- hello は allocator・printf 不要。必要な memory 関数だけ freestanding で用意。
- multilib は `.` のみ、既存 libgcc は hard-float。USER もリンクせず、未定義 helper は build 失敗。
- hello では u64 除算を使わない。専用出力 directory を使い、i386 nano を流用しない。

```sh
make ARCH=ppc PLATFORM=powermac sdk-user
make ARCH=ppc PLATFORM=powermac hello
# -m32 -mbig-endian -mcall-sysv -mno-eabi -msdata=none
# -msoft-float -mno-altivec -ffreestanding -fno-pie -nostdlib
# guarded ELF link → objcopy raw → mkos32x --target ppc-powermac
```

**9. 実装段：各段1実装者、主機能1つ、400行以下・1〜3ファイル**

- `共`＝共通箇所、本線に先に知らせる。`不変`＝x86 生成物不変。`★`＝x86 が変わる。
- facade の不変は x86 の static inline で既存呼出しへ展開し、既存機能・順序を維持する設計条件。
- 段内の header・試験・生成差分も上限に含める。超える場合は同じ依存を接続箇所別に分割。
- 表の `口` は arch/platform の選択 header、`PPC実体` は依存ごとの専用 provider を表す。
- generated ファイルは手編集せず generator から出力。x86 出力に差が出れば不変扱いで着地させない。
- R4 は既存 `exec_run`→load→entry→syscall→finish を使う。PPC 専用 launcher は作らない。

|段|主ファイル／機能|依存・順序|印|
|---|---|---|---|
|B01|`exec/os32x_hdr.c`＋宣言：LE decode|先行|共★|
|B02|既存 header 読込元＋公開 SDK header：decode・PPC flag 拒否|B01|共★|
|B03|`exec_arch.h`＋x86/PPC 口：target 識別|B02|共・不変|
|B04|`sdk/os32x_hdr.py`：ELF endian・PPC flag|B02|共・不変|
|B05|`sdk/target_profile.py`：USER 能力分離|独立|共・不変|
|B06|`build/sdk-target.mk`：能力単位の選択|B05|共・不変|
|B07|`sdk/gen_kapi.py`：target・出力先|B05|共・不変|
|B08|PPC adapter 生成 helper：型付き整数 adapter|B07|不変|
|B09|PPC exec/jmp storage header：保存域確定|先行|不変|
|B10|PPC setjmp assembly：C 文脈と深さ2語|B09|不変|
|B11|PPC exec context：root・入口 kernel-SP|B09|不変|
|B12|PPC exec entry：argv 後の入口準備|B09|不変|
|B13|PPC entry assembly＋MSR 定義：PR 遷移|B11・B12|不変|
|B14|`kernel_vectors.S`：PR 別 frame と復帰|B13、busy 非使用|不変|
|B17|PPC cache backend：恒等 alias の命令公開|独立|不変|
|B18|PPC syscall code：8B stub・ls_off・共通 SC|B08・B17|不変|
|B19|PPC KAPI call：logical-values 接続|B08|不変|
|B20|`kernel_trap.c`：SC・USER fault 分岐|B14・B19|不変|
|B21|PPC copy backend：既存 onfault 接続|B20|不変|
|B22|PowerMac 出力 provider：S-A stream|S-A・B19|不変|
|B23|PPC exec provider：既存終了・親復帰へ接続|B10・B11・B20|不変|
|B24|`exec_launch.inc`＋arch 口：publish 接続|B17・B23|共・不変|
|F01|`exec.c`＋legacy 口＋PPC実体：legacy_guest 分離|閉包の先頭|共・不変|
|F02|`exec.c`＋gfx 口＋PPC実体：gfx/gfx_hal include と参照|F01|共・不変|
|F03|gfx 口＋`exec_finish.inc`＋PPC実体：emergency・owner 回収|F02、**HAL v2 の I1 が main に入った後の main を基点**|共・不変|
|F04|`exec/lease.c`＋surface 口＋PPC実体：表示 lease 依存|F02、**HAL v2 の I1 が main に入った後の main を基点**、I2 (C14) と時期を合わせる|共・不変|
|F05|`exec.c`＋gui 口＋PPC実体：gui 登録・owner 通知|独立|共・不変|
|F06|`exec_abort.inc`＋pump 口＋PPC実体：ring3_gui_pump|F05|共・不変|
|F07|`exec.c`＋snd 口＋PPC実体：snd_engine・終了通知|独立|共・不変|
|F08|`exec.c`＋shlib 口＋PPC実体：loaded/pages/attach/detach|独立|共・不変|
|F09|`exec.c`＋IME 口＋PPC実体：IME include・owner 回収|③統合前|共・不変|
|F10|生成器＋IME 生成 helper：KAPI の IME binding|F09、③統合前|共・不変|
|F11|`exec_syscall.inc`＋GeForce 口＋PPC実体：due/poll|独立|共・不変|
|F12|`exec_schedule.inc`：同じ GeForce 口へ接続|F11|共・不変|
|F13|`exec_syscall.inc`＋KAPI 表の口＋PPC実体：KAPI_ADDR 参照。PPC の KAPI_ADDR は platform/powermac の配置の header に置き、include/memmap.h には触らない (HAL v2 との約束)|B08、x86 は元の表参照へ inline 展開|共・不変|
|F14|生成器＋PPC binding helper：不要 wrapper の実依存を除去|F02・F05・F07・F10、型付き未対応応答|共・不変|
|F15|KAPI 初期化生成口＋PPC 表実体：全 slot と metadata の配置|F13・F14、既存初期化経路へ接続|共・不変|
|F16|font guard の口＋PPC 実体 (fault は既存の停止)：`font_guard_fault` (定義は x86 の isr_handlers.c だけ、呼ぶ所は exec_finish.inc:329,345・exec_fault.inc:15)|**HAL v2 の I1 の後の main を基点** (exec_finish.inc)|共・不変|
|F17|kselftest の口＋PPC 実体 (no-op)：`kselftest_run_audit` (exec_launch.inc:379)、`kselftest_probe.h` の include (exec.c:43)|独立|共・不変|
|F18|r1 fixture の口＋PPC 実体 (no-op)：`r1_fixture_resume`・`r1_fixture_parent` (exec_schedule.inc:254・exec_finish.inc:312、kernel/r1_fixture.c は legacy_guest.h を include)|**HAL v2 の I1 の後の main を基点** (exec_finish.inc)|共・不変|
|F19|pcm の回収の口＋PPC 実体 (no-op)：`pcm_reclaim` (exec_finish.inc:157)、host の回収 (exec_finish.inc:207)|**HAL v2 の I1 の後の main を基点** (exec_finish.inc)|共・不変|
|F20|exec.c の残りの include の口：`serial_diag.h`・`kapi_db.h` (exec.c:6,11)|F01|共・不変|
|B25|PPC `crt0.S`＋`start.c`：CRT|B07、arch と並列|不変|
|B26|PPC `app.ld`：USER 配置|B04・B25|不変|
|B27|`sdk/link_guard.py`：PPC USER 検証接続|B05・B26|共・不変|
|B28|PPC target build：USER 入口・libgcc 非リンク|B06・B27|不変|
|B29|PPC clock provider：共通 tick 単位|統合前必須|不変|
|B30|PPC exec 初期化 TU：既存サービス初期化の接続|B18・B22・B24・F15|不変|
|B31|`build/ppc-kboot.mk`：既存 exec 閉包 link|F01–F20・B28–B30|不変|
|B32|resident CLI の `.h/.c`＋`cli_boot.c`：run callback|S-A・B31|共・不変|
|B33|`userland/ppc/hello.c`：受入アプリ|B28|不変|
|B34|PPC root image 入力：hello 同梱|B33|不変|
|B35|PPC S-B runner：最小通し試験|B32・B34|不変|
|B36|`docs/09_exec.md`：ABI・終了値|統合後|共・不変|

- F01–F12 の PPC は不在照会なら false/0、取得なら NULL、操作なら既存の未対応値、回収なら no-op。
- gfx・gui・snd・ime・shlib・GeForce の実装を引き込まず、不在機能用の状態管理・再試行・過剰防御は加えない。
- F14 は `kapi_generated.c` の周辺機器等への不要な参照も target binding で除去する。全機能の偽実装は作らない。
- 共通の caller 検証・owner・FD・heap・終了処理は残す。F15 は番号・予約欄・metadata を省略しない。
- selected 成功済み TU もリンク成功を意味しない。B31 の未解決参照を消すまで閉包完了とはしない。

- 生成器 (sdk/gen_kapi.py と生成の口) に触る段 B07・B08・F10・F14・F15 は**直列** (同じファイルを同時に直さない)。kapi_generated.c は HAL v2 の A0 (gfx2_* 29 本、main 済み) の後を基点にする。
- exec/exec_finish.inc と exec/lease.c に触る段 (F03・F04・F16・F18・F19) はすべて HAL v2 の I1 が main に入った後の main を基点にする。段の一覧は出す前に本線の PM に送る。

**10. 受入・最小試験・容量・判断事項**

|試験|確認するもの／最小の変異|
|---|---|
|Host H1|LE header、BE ELF と刻印、target 識別。endian 選択反転1個|
|Host H2|8B stub・共通 SC・ls_off、入口配置、cache 命令列。分岐先を壊す変異1個|
|QEMU Q1|hello → exit=7 → prompt → 再実行。終了値伝達を壊す変異1個|
|QEMU Q2|USER text store 1回 → FAULT → prompt → hello。子終了分岐の変異1個|

- PP 変換の再試験は追加しない。Q1/Q2 の復帰と再実行で busy 残留も観測する。
- 不正命令・stack fault・copy fault の別 QEMU 像を重ねない。copy は既存 helper 試験を必要箇所だけ更新。
- cache は命令列照合まで。実効性は QuickSilver の後続受入へ継承する。
- facade・生成物は build と既存照合で確認し、同じ条件を下位の新試験で重ねない。
- tick は導入閉包の利用者を共通時間単位へ接続。PIT_HZ の一括変更はしない。
- 実装時は指定 PPC image／USER build、触った試験の変異、lint。各段の上限に試験変更も含める。
- ★段は必要な x86 build。不変段は生成差分・対象 object の確認で不変条件を確かめる。
- ABI 変更扱いなら clean → all。全変異・`make check-changed` はローカルで回さない。

|対象|増分予算・実測方法|
|---|---|
|kernel 像|暫定 +64〜192KiB。既存 exec・生成表・回収閉包を含む計画値。B31 の map で確定|
|hello.bin|64Bヘッダ＋RO prefix 4KiB＋小 data、概ね4〜12KiB|
|RAM root 像|ISO block 丸め込み概ね +6〜16KiB。生成像の差分|
|実行中 RAM|USER stack16KiB＋heap既定64KiB＋image＋PT＋context、概ね90〜144KiB/子|
|kernel stack|子専用の増分0。既存 stack に USER trap frame と syscall 呼出し分が必要|
|KAPI 常駐 RAM|USER trampoline は既存1ページ。kernel の本物の表・metadata は kernel 像側に計上|

- 旧 +32〜96KiB は閉包未分解の値として撤回。新予算も上限保証ではなく、初回 link で更新する。
- PM 候補は flag `0x0040`、ABI 名、hello stack16KiB、専用 build 名・出力先。子 kernel stack16KiB 案は削除。
- W6 の FD 容量・W7 の shell 全機能は別段。実装開始には base SHA と S-A 取り込み位置が必要。
- 今回は読み取り設計のため preflight・build・試験を省略した。

## 確定 (2026-11-11、Opus 5.5 の第 2 回の条件付き Approve を PM が反映)
- N1: exec の閉包の漏れ 5 系統を F16〜F20 として足した (font_guard_fault、kselftest、r1 fixture、pcm の回収と host の回収、serial_diag.h・kapi_db.h の include)。B31 の前提を F01–F20 に。
- N2: exec_finish.inc と lease.c に触る段すべてに「HAL v2 の I1 の後の main を基点」。
- N3: 生成器に触る段は直列、kapi_generated.c は A0 の後を基点。
- N4: PPC の KAPI_ADDR は platform/powermac の配置の header、memmap.h には触らない。
