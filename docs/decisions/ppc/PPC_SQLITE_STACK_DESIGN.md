# PowerPC SQLite 別スタック S の実装追補 (確定)

> 出所: ~/os32-tmp/run/ppc-sqlite-stack/DESIGN.final.md (設計の往復の確定版)。
> 状態: **現行 (2026-11-18)** — 確定。 sol 起草 r1・r2、Opus 5.5 の独立レビュー 2 回、PM が第 2 回の N1 を反映。往復の版は ~/os32-tmp/run/ppc-sqlite-stack/。

## r1→r2 の変更

- P2-1 — PPC の包みを `set_msr` より前に置き、F3 のソース加工と FP 検査の covers を追従する。
- P2-2 — PM 決定どおり、固定名ヘッダの PPC extern と x86 static inline を使い、共通 engine の呼出しを統一する。
- P2-3 — 専用 ELF の object・旗・配置・開始 MSR・観測・終了値・変異コンパイルを定義する。
- P3-1 — guard の寸法マクロと重複した寸法検査を削除する。
- P3-2 — PPC compile-only のソース集合を common.mk に置き、選択文書にも追記する。
- P3-3 — 確定設計の置き換え範囲と枝名を明記し、製品比較の判定を x86 と PPC で定義する。

確定設計 `docs/decisions/ppc/PPC_FP_NEWLIB_DESIGN.md` の §5・§7 の S 3 行・§8 S1 を本追補で置き換える（確定設計への反映は PM）。
基点は main `756b5b175`。枝は `wt/ppc-sqlite-stack`。FP 保護の順序は確定設計 §2 に従う。
対象は置き場、共通口、コンパイルと試験への接続。単一 CPU・協調切替と、PowerPC のハードウェア FP を前提とする。

## 1. 実物の確認

| # | 確定設計との差 | main の根拠 |
|---|---|---|
| 1 | S guard 結線で共通が PPC 専用ヘッダを直に読む形は新しい R1 に当たる。既存の guard 処理を共通契約から呼べる形にする。 | `kernel/sqlite_engine.c`、`arch/ppc/kernel_fp.h`、`arch/ppc/kernel_fp.c`、`docs/design/tools/check_layers.md` |
| 2 | §5 の `lib/sqlite3/` は共通所有。PPC 命令の trampoline は CPU 所有とする。 | `docs/decisions/ppc/PPC_FP_NEWLIB_DESIGN.md`、`docs/design/tools/check_layers.md` |
| 3 | feature 内の NASM/GAS 排他選択では現行の PPC 試験に届かない。SQLite feature は PowerMac で既定 0、1 の明示指定も拒否される。 | `build/features/sqlite.mk`、`build/platform/powermac.mk`、`build/select.mk` |
| 4 | 共通の engine は現在 `C_FEATURES` 所属で、PPC の未リンク共通 TU の検査対象から漏れている。単体コンパイルには SQLite ヘッダの探索パスも要る。 | `build/features/sqlite.mk`、`build/sources/common.mk`、`build/targets/ppc-powermac.mk`、`kernel/sqlite_engine.c`、`lib/sqlite3/generated/sqlite_facade.h` |

- 現行 TU を link-sh の make が出す PPC コンパイル行で確認。既定の探索パスでは `os32_sqlite_config.h` が見つからず、対象 TU に `-Ilib/sqlite3` を足すと `-Werror` 付きでコンパイル成功。出力先は `/dev/null`。新しい口を含む版の確認は §6。
- `build/images/ppc-flags.mk` は kboot 像と旗の対応を担う。SQLite の有効化は上記 `build/select.mk` の判定に従う。
- 統合判断 1。同じ役目は x86 の `lib/sqlite3/sqlite_stack.asm` にある。どちらも旧 SP を保持し、整列した別スタックで callback を呼んで戻す。
- 統合判断 2。整数 ABI の `sqlite_call_on_alt_stack` と共通の `sqlite_engine_run` を両 arch で共有する。SP/LR と非揮発レジスタの保存だけ CPU ごとの assembly が担う。PPC ではさらに共通契約越しに FP 保護を行う。
- 統合判断 3。確定設計の §2・§5 の順序と後段の製品有効化に沿う。AC-BUILD-1 の provider/closure は両像で検査する。AC-LIBC-1 は確定設計全体の USER 側の契約で、この段の callback 境界は整数だけ。両 arch で callback への引数、SP 整列と復元、戻り先、非揮発状態、canary を観測する。FP の契約 ID は確定判断どおり追加不要。
- 参照する手本は確定設計 §5 の N `.../setjmp.S`、F `lib/libc/powerpc/gen/_setjmp.S`、OS32 `sqlite_stack.asm` と `docs/ppc/abi.md`。

## 2. 口の形

`include/exec_arch.h` には次の前方宣言と説明のコメントだけを置く。原型は arch 選択後の固定名ヘッダが提供する。

```c
/* Begin returns 0 on success. End pairs with a successful begin.
 * Keep the guard on the caller's stack through the alternate-stack call. */
struct arch_kernel_fp_guard;
```

`arch/ppc/arch_kernel_fp.h` は既存の PPC 型を実メンバーとして保持する。両方の固定名ヘッダに include guard を付ける。

```c
#include "kernel_fp.h"

struct arch_kernel_fp_guard {
    struct ppc_kernel_fp_guard ppc;
};

extern int arch_kernel_fp_begin(struct arch_kernel_fp_guard *g);
extern void arch_kernel_fp_end(const struct arch_kernel_fp_guard *g);
```

`arch/ppc/kernel_fp.c` の include を固定名に替え、次の包みを `static void set_msr(` より前に定義する。既存の PPC 入口・型・状態操作を使う。

```c
int arch_kernel_fp_begin(struct arch_kernel_fp_guard *g)
{
    return ppc_kernel_fp_begin(g ? &g->ppc : 0);
}

void arch_kernel_fp_end(const struct arch_kernel_fp_guard *g)
{
    ppc_kernel_fp_end(&g->ppc);
}
```

`arch/x86/arch_kernel_fp.h` は次の型とインライン関数を提供する。製品ビルドの最適化でダミー領域と呼出しが消えることは §6 の比較で確認する。

```c
struct arch_kernel_fp_guard { char unused; };

static inline int arch_kernel_fp_begin(struct arch_kernel_fp_guard *g)
{
    (void)g;
    return 0;
}

static inline void arch_kernel_fp_end(const struct arch_kernel_fp_guard *g)
{
    (void)g;
}
```

`kernel/sqlite_engine.c` は `exec_arch.h` と固定名 `arch_kernel_fp.h` を include し、次の位置に挿入する。

```c
    /* 既存の IRQ 保存と未準備・再入・IRQ/例外・I/O 拒否を通過 */
    sqlite_engine_active = 1;
    irq_restore(flags);
    struct arch_kernel_fp_guard guard;  /* 生き残る旧 kernel stack */
    if (arch_kernel_fp_begin(&guard)) {
        (void)irq_save();
        sqlite_engine_active = 0;
        irq_restore(flags);
        return 0;
    }
    sqlite_call_on_alt_stack(call, sqlite_engine_stack_top, frame);
    arch_kernel_fp_end(&guard);
    (void)irq_save();
    sqlite_engine_active = 0;
    irq_restore(flags);
    /* STOP belongs to the caller's safe point, after output and teardown. */
    return 1;
```

- begin 成功時の順は IRQ 停止 → 旧 MSR と全 FP 保存 → FP=1/FE=0 → init → 元の EE を復元。end は IRQ 停止 → 全 FP 復元 → 旧 MSR/isync。拒否で callback は走らず、元の IRQ 状態に復帰する。
- callback 内のエラーは通常 return で end を通す。区間中の park/exec/longjmp/USER callback は禁止。IRQ は整数のみ。STOP は呼出し側の安全点。

## 3. 置き場と選択

- `arch/ppc/sqlite_stack_ppc.S` に既存と同じ公開シンボルを置く。r3=call、r4=top、r5=frame。旧 16B 整列フレームに LR と使用する非揮発 GPR (r30/r31) を保存し、r2/r13 と非揮発 CR を保持する。
- 旧 r1 と callback を非揮発 GPR に保持 → 新 r1=`align_down(top,16)-16` → backchain に旧 r1、LR 用 linkage を予約 → r3=frame、mtctr/bctrl → 旧 r1/LR/GPR の復元、blr。予約域/guard を越えず、trampoline で FP 命令を使わない。hard-float callback は ABI に従い f14〜31 を保持する。
- 選択の境界は `ifeq ($(FEATURE_SQLITE),1)` 配下の x86-pc98 と、専用 PPC 試験の明示ソース一覧。前者は既存の `ASM_FEATURES += lib/sqlite3/sqlite_stack.asm`、後者は GCC で .S を組む。`FEATURE_SQLITE=1` の PPC 拒否を維持し、像の `GAS_ARCH_*` や `ppc-flags.mk` への追加は不要。
- `build/sources/common.mk` に `C_COMMON_COMPILE_ONLY_PPC := kernel/sqlite_engine.c` を置く。`build/targets/ppc-powermac.mk` の `KB_PENDING_OBJ` を `$(C_COMMON_PC98_PENDING) $(C_COMMON_COMPILE_ONLY_PPC)` から既存の変換で導き、`$(KB_OUT)/kernel/sqlite_engine.o: KB_TU_FLAGS += -Ilib/sqlite3` を加える。生成済み契約ヘッダは管理対象のファイルを使い、`-MMD -MP` の依存を生かす。
- `C_COMMON_COMPILE_ONLY_PPC` は `common-compile` に適用する。`KB_OBJ` は既存の製品集合から求める。x86 の engine 選択は `C_FEATURES` が担う。`docs/design/build/select.md` の `common-compile` の段落にこの集合の用途を 1 行追記する。
- 層検査は両像の違反 0 を維持する。PPC 製品外の engine/試験 TU についても専用試験で PPC の `-I` と依存を確認する。共通 → 固定名 → PPC 専用ヘッダの最後の辺は PPC 内。
- `build-selection-host` で x86 の engine/NASM が各 1 本、全 kboot 形の engine と .S が各 0 本、PPC の `FEATURE_SQLITE=1` が拒否されることを確認する。common-compile の確認はリンク一覧と独立に行う。

## 4. 部品の大きさ

| 部品 | 追加の目安 | 製品像への反映 |
|---|---|---|
| PPC trampoline | 約 65 行 | 専用試験 ELF のみ |
| PPC guard 接着 | 約 25 行と旧スタック上 272 B/整列 8 | 包みの定義は既存 PPC guard TU に同居。現行像からの参照はなく GC 対象。保存域は試験の呼出し時のみ |
| 共通呼出しと選択 | 約 22 行 | engine の PPC object は compile-only、x86 は既存機械語 |
| 試験台本・fixture・登録 | 約 250〜340 行 | 専用試験のみ |

## 5. ファイルごとの変更一覧

| ファイル | 差分目安 | 単独の担当範囲 |
|---|---:|---|
| `include/exec_arch.h` | +3 | 前方宣言と利用契約 |
| `arch/ppc/arch_kernel_fp.h` | 新 12 | PPC 型と extern 原型 |
| `arch/x86/arch_kernel_fp.h` | 新 18 | ダミー型と static inline |
| `arch/ppc/kernel_fp.c` | +10 | B の既存実装を包む |
| `kernel/sqlite_engine.c` | +15 | active と guard の接続 |
| `arch/ppc/sqlite_stack_ppc.S` | 新 65 | S の SysV trampoline |
| `build/sources/common.mk` | +1 | PPC compile-only ソース集合 |
| `build/targets/ppc-powermac.mk` | 2 行 | 集合からの object 選択と TU の include |
| `docs/design/build/select.md` | +1 | compile-only 集合の用途 |
| `tools/tests/test_ppc_fp.py` | 1〜数行 | F3 の加工境界と固定名ヘッダの covers |
| `tools/checks/ppc-fp-guest.py` | +1 | PPC 固定名ヘッダの covers |
| `tools/tests/test_build_selection.py` | +10 | 各像の負の選択と明示 feature 拒否 |
| `tools/checks/ppc-sqlite-stack-guest.py` | 新 25 | CHECK・環境・変異引数 |
| `tools/accept/ppc_sqlite_stack.py` | 新 100〜140 | 専用 ELF の構築、起動、判定 |
| `tools/tests/ppc_sqlite_stack_guest.c` | 新 70〜95 | 合成 callback と guard/active の観測 |
| `tools/tests/ppc_sqlite_stack_regs.S` | 新 55〜80 | ABI sentinel と帰還の観測 |

- ROLES §4-3 のソース一覧・リンク順と像の固定を点検。`build/sources/order.mk`、製品側の arch 一覧、`tools/tests/fixtures/build_profile_x86.json` の `C_KERNEL`/ASM および `build_profile_ppc.json` の `KB_C`/`KB_ASM` は現値一致が期待値。profile を実際に照合し、生成値を手で補正しない。KAPI 変更なし。
- 呼出し・stub の棚卸しは `test_sqlite_engine.py`、`fep_host_build.py`、`test_fep_core_boot.py`、`test_sqlite_module_proto.py` のソースコピー/切出しまで行う。x86 fixture は既存の x86 include 経路を確認し、独自の `host_arch` だけで引く fixture には同名のダミー型と static inline を用意する。PPC 受入は本物の guard を使用する。
- `test_ppc_fp.py` の F3 は `set_msr` の定義だけを模擬版に置換し、現在はその前の include と宣言も `guard.inc` に残す。包みは加工境界の前に置き、`ppc_fp_guard_host.c` から組んだ GREEN=0／no-restore RED=3／再 GREEN=0 を確認する。`test_ppc_fp.py` と `ppc-fp-guest` の covers に `arch/ppc/arch_kernel_fp.h` を加える。
- 変更された既存 CHECK の covers/dep-dir と変異用コピーを追従する。新規 CHECK に covers、basis/kind、依存および該当する env_limit/pins を宣言する。
- `tools/tests/test_sqlite_engine.py` の trampoline 呼出しアンカーは維持。reentry/STOP と他の兄弟試験の目印も grep で確認し、置換が必要なら一意な新位置で実行 RED を確認する。PM は `stale_anchor.py 756b5b17` を回す。

## 6. 受入 → 試験と変異

`tools/ppc/kboot_vectors.py` の単独 ELF、HFS ISO、serial と QMP を使う。既存の `tools/accept/ppc_fp.py` に合わせ、7450 と 7455、正常／変異ごとの出力分離、期限つきの起動と判定を用意する。

| 項目 | 専用 ELF の作り方 |
|---|---|
| entry・I/O の object | `arch/ppc/{entry,ofw_note}.o`、`platform/powermac/{ofw,escc_tree,escc}.o`、`lib/kstring_c.o`。各 object は同名の製品ソースから組む。 `entry.S` の `_start` が旧 r1 と BSS を初期化し、`PPC_MMU` 無指定で fixture の `powermac_boot(ppc_ci)` を呼ぶ。fixture は既存の vectors fixture と同じ `ofw_init → ofw_setup → pm_escc_prepare` を実施する。 |
| 検査対象の object | `kernel/sqlite_engine.o`、`arch/ppc/{sqlite_stack_ppc,kernel_fp,fp_context,mmu}.o`。IRQ の save/restore/enabled は `mmu.S` の実装を使う。 |
| fixture の object | `tools/tests/{ppc_sqlite_stack_guest,ppc_sqlite_stack_regs}.o`。C 側が `volatile u32 kctx_irq_depth, kctx_exc_depth;` と終了値の格納先 `volatile u32 sqlite_s1_status;` を定義する。FP 命令を使う fixture（callback を含む）は `.S` に置き、製品側の FP 命令は `fp_context.S` にある。 |
| 旗 | `CC=$PPC_CROSS_DIR/bin/powerpc-elf-gcc`（既定 `/home/hight/opt/cross-ppc`）。共通旗 `FLAGS` は `kboot_vectors.py` の `FLAGS` と同じ（`-mcpu=7450 -m32 -mbig-endian -msoft-float -mno-altivec -msdata=none -ffreestanding -fno-pie -nostdlib -O2` 等）に `-ffunction-sections -fdata-sections` を追加。engine 用 `ENGINE_INC` は `-I$ROOT/kernel -I$ROOT/init -I$ROOT/kernel/console -I$ROOT/kernel/irq -I$ROOT/kernel/time -I$ROOT/mm -I$ROOT/sdk/include -I$ROOT/sdk/include/os32 -I$ROOT/lib/sqlite3 -I$ROOT/fs -D__KERNEL_BUILD__`。全 C TU は soft-float。 |
| リンカ | `$CC $FLAGS -no-pie -Wl,-N,--gc-sections,--no-undefined -Wl,-T,fixture.ld -Wl,-Map,fixture.map <上の objects> -o SSTACK.ELF`。vectors と同じ `elf32-powerpc` / `ENTRY(_start)` / `0x20000` / PT_LOAD と PT_NOTE。`.note.ofw` は `KEEP(*(.note.ofw))`。text/rodata/data/sdata、4B 整列の `_edata`、NOLOAD の bss/sbss/COMMON、16B 整列の旧スタック 16 KiB と `__stack_top`、`_end < 0x400000` の順。専用 NOLOAD 予約域は旧スタックの直前に置き、`_edata.._end` のゼロ初期化範囲に含める。 |
| 別スタック | 専用予約域を 16B 整列し、下 canary 16B → 使用域 32 KiB（`SQLITE_ENGINE_STACK_SIZE`）→ 上 canary 16B の順にシンボルを置く。fixture が両 canary を既知値で埋め、使用域の末端（上 canary の先頭）を製品 TU の `sqlite_engine_stack_top` に代入する。map と `nm` で実 engine の入口、予約域境界、旧スタックとの非重複、SQLite モジュール・辞書・IME の provider の不在を確認する。 |
| 開始 MSR | OF 呼出し中は `_start` が復元した firmware の状態を使う。ESCC 準備後、検査開始前に `sync; mtmsr; isync` で FP=0・IR=0・DR=0・EE=0・FE=0 に正規化し、readback を確認する。FPR/FPSCR sentinel の設定・採取は assembly が必要時だけ FP を有効にし、元の MSR を復元する。engine 帰還時は FP を一時有効化する前に MSR を採取する。 |
| 起動・終了 | vectors と同じ `genisoimage -quiet -hfs -r -part -no-desktop`、mac99 / TCG / 同じ ROM、OF の `boot-device=cd:,\SSTACK.ELF`、serial A/B を個別ファイルに保存する。QMP で 30 秒以内に同じ終端 PC を連続 2 回観測し、`xp /1wx` で `sqlite_s1_status` を読む。ゲストは status を格納し、serial を drain して終端ループへ入る。QMP quit 後の QEMU rc は全受理ケースで 0。ホストは期待した対照と変異がすべて一致した場合に 0 を返す。 |
| 正常と変異の compile 行 | 正常 engine は `$CC $FLAGS $ENGINE_INC -c "$ROOT/kernel/sqlite_engine.c" -o "$out/sqlite_engine.o"`。end 変異は一意な `    arch_kernel_fp_end(&guard);` を除いたコピーを別の出力先に書き、`$CC $FLAGS $ENGINE_INC -c "$out/sqlite_engine.mut.c" -o "$out/sqlite_engine.o"` で組む。`-I$ROOT/kernel` が元の相対 include の探索起点にもなる。旧 r1 変異は `$CC $FLAGS -c "$out/sqlite_stack_ppc.mut.S" -o "$out/sqlite_stack_ppc.o"`。各ケースで該当 object 一つだけを差し替えて同じ専用 ELF をリンクする。`KB_MUT_SRC` は製品の `KB_OBJ` 用。 |

| ケース | serial A の到達印 | 診断行 | QMP 終端 PC / ゲスト終了値 |
|---|---|---|---|
| 正常・復元後 | `S1-BEGIN` → `S1-ABI-CALLBACK` → `S1-ENGINE-CALLBACK` | `S1-PASS` | `sqlite_s1_done` / 0 |
| 旧 r1 復元破壊 | `S1-BEGIN` → `S1-ABI-CALLBACK` | `S1-FAIL-SP` | `sqlite_s1_bad_sp` / 1 |
| guard end 省略 | `S1-BEGIN` → `S1-ABI-CALLBACK` → `S1-ENGINE-CALLBACK` | `S1-FAIL-FP` | `sqlite_s1_bad_fp` / 2 |

各目印は独立した行に 1 回だけ出す。ゲスト終了値の初期値は `0xffffffff`、その他の検査失敗は専用診断付きで `sqlite_s1_fail` / 3。正常対照と変異は両 CPU で実行する。

- S1 正常。callback 冒頭の r1 が予約した別スタック内で 16B 整列し、backchain と frame が一致することを記録。帰還後の旧 SP、LR、非揮発 GPR/CR と f14〜31、上下の canary を比較する。ABI の観測は、guard を開いてから assembly の検査関数が trampoline を直接呼び、帰還直後に f14〜31 を採取する。別ケースで実 engine を通し、end 後の全 FPR/FPSCR と MSR/EE を比べる。
- S1 拒否と後始末。既存の未準備・再入・IRQ/例外・IRQ 禁止 I/O に加え、別 guard 保持中の begin 拒否を起こす。戻り値 0、callback 未実行、active 解除、FP/IRQ 状態維持と次の正常呼出しを観測する。正常 callback は active が立つことを確かめ、通常帰還後に解除を観測する。
- 最初に直接 ABI ケースを実行する。旧 r1 変異では LR と GPR の復元後、末尾の `blr` の直前に一意なアンカーで `addi 1,1,16` を挿入する。assembly の呼出し側が事前に保存した期待 r1 と直後の r1 をメモリアクセス前に比較し、差があれば正しい r1 を戻して `S1-FAIL-SP` を報告する。end 変異では実 engine の最初の正常呼出しから戻った直後の FP/MSR 比較で `S1-FAIL-FP` に進む。正常 GREEN → 各変異の実行 RED → 正常 GREEN の順に、表の到達印・診断・停止 PC・ゲスト終了値・QEMU rc を照合する。ビルド失敗、別の停止点、timeout は試験失敗とする。
- 既存 x86 の `make check-sqlite-engine-host`を使い、同じ callback 境界を NASM 側でも確認する。影響する既存検査と新規試験を変異付きで回し、`python3 -B tools/checks/run.py --lint` を実行する。
- `PPC_CROSS_DIR=/nonexistent make check-ppc-sqlite-stack-guest` は理由つき SKIP/rc=0。同条件と `OS32_REQUIRE_PPC=1` の併用は非 0。PM が 7450/7455 の証跡を集約する。
- 最終ソースで `make ppc-ci-image` の `common-compile`、PowerMac の link-sh 製品リンク、両像の layers、`check-build-selection-host`、x86 `make all` を確認する。比較は PM が `tools/build_compare.sh 756b5b17 HEAD --all --output <比較先>` で基点と実装作業木を同条件で組み、x86 の成果物一致と `sqlite_engine_run` を含む関数差 0 を確認する。PPC は包みの追加による `kernel_fp.o` の差を許容し、map とシンボルで製品像に残る差を確認する。現行像で包みは GC 対象であり、製品像に差が残れば PM が理由を確認する。枝は `wt/ppc-sqlite-stack` で、x86 の像変更に対する `Sep-Changes:` は不要。
- 起草時は基点とフォントおよび現行 PPC TU の単体コンパイルを確認した。preflight の x86 成果物 2 件は未生成で、実装後のビルド、QEMU、変異、像比較は未実施。

## 7. 未決

- 0 件。

反論 0 件。

## 確定 (2026-11-18、Opus 第 2 回、PM が反映)
- N1 (P2): §6 の `ENGINE_INC` に `-I$ROOT/fs` を足した (kernel/ime/ime.h と lib/sqlite3/os32_sqlite_vfs.h が `vfs.h` = fs/vfs.h を読む)。手で写した一覧が common-compile の make の行とずれたら、実装者は make の行に合わせて報告に書く。
- 第 1 回 P2-1 の反例はレビュアーが撤回 (test_ppc_fp.py は set_msr の定義だけを差し替える)。r2 の置き場で F3 は組める。
- 確定設計 PPC_FP_NEWLIB_DESIGN の §5・§7 の S 3 行・§8 S1 は本追補で置き換える (PM がリポジトリの写しに反映)。

## S-a・S-b の取り込みで後の段へ送る項目 (2026-11-18、Opus レビュー、どれも P3)
- tools/accept/ppc_sqlite_stack.py の observe() は tools/ppc/kboot_vectors.py の QEMU の引数と QMP の stop→NIP→cont の繰り返しの写し。tools/ppc/run.py か kboot_vectors に「ELF・CPU・終端の記号の集合 → (PC, 終了値, serial)」の関数を 1 つ置いて両方から呼ぶ (QMP の競合の直し — sd_run の口の予約・build/out/ppc の固定の名前 — と同じ段でよい)。
- ppc-sqlite-stack-guest の covers は kboot_vectors.py の import が辿る補助入力 (kboot_vectors_fixture・boot.c など) を含む。入力の監査がその module 全体を辿る作りのため。監査を関数単位にしたら外す。
- S-a の実装は ppc-powermac.mk の engine の TU 旗に -Ifs も足した (確定 N1 の「make の行に合わせる」、59 行目の -Ifs は KB_EXEC が空でない時だけ入るため)。

## 確定後の追記 (2026-11-18、astra の設計レビュー rev4 への対応、astra 起草 r1・r2、Opus 2 回)

astra (gpt-6.2-astra) が確定後の本文をレビューして P2 4 件を出した (起草が ROLES §0 と違い sol だったため、ユーザーの指示で読み直した)。以下の A-1〜A-4 は本文の該当する文を置き換える (各 A-n の冒頭に置き換える本文の文を書いた)。

### r1→r2 の変更

- P2-1　A-4 の比較基点を着手直前とし、関数差と x86 オブジェクトの逆アセンブル差を受入にした。
- P2-2　MPIC 設定を削り、0x500 への配送を S1-FAIL-EXT とした。
- P2-3　各追記の冒頭に本文の置換箇所を示した。
- P3-1　契約型と静的照合を型変異より先に置いた。
- P3-2　公開関数、SOURCES、CHECK の basis・covers と変異の目印の確認を明記した。
- P3-3　型変異の実行条件と静的照合の前例を記した。
- P3-4　追加ケースの位置、呼出し回数と DEC stub の退避先を指定した。
- 参考 A-1　同期例外の監査起点を明記した。

### 追記 A-1　同期例外と製品有効化（§2・§6）

本文 §6 の「S1 拒否と後始末」の IRQ/例外の受入範囲を次で置き換える。

S1 が証明するのは engine が受け取った深さに対する拒否である。外部 IRQ と DEC は arch/ppc/kernel_trap.c で kctx_irq_depth を増減する。通常 syscall 以外の同期例外については、SQLite 本体を PowerPC 製品像にリンクする段の開始条件とする。現段の製品像では本体が無効で、有効化時の実際の呼出し先で到達性を判定するためである。

監査は arch/ppc/kernel_trap.c の ring3_fault_kill と ppc_trap_fatal_sink から続く経路を含む。有効化の受入では各同期例外の入口から直接・間接の呼出し先を列挙し、engine に到達しない根拠を残す。到達し得る経路は次の形で囲み、復帰・早期帰還・入れ子を含めて深さを元に戻す。通常 syscall は囲まない。

~~~c
++kctx_exc_depth;
handle_sync_exception(frame);  /* 対象ハンドラの全処理を含む */
--kctx_exc_depth;
~~~

深さを採用した経路では実際の例外から engine を呼ぶ受入で callback 0 回、復帰後に通常呼出し成功を確認する。入口の加算を除く変異は実行 RED。到達不能を採用した経路は製品リンクと呼出し先の監査を受入とする。現段は追記のみ。後段の深さ管理の候補ファイルは arch/ppc/kernel_trap.c。

### 追記 A-2　EE=1 の実 engine（§6）

本文 §6 の「S1 正常」「S1 拒否と後始末」の実 engine ケースと guest.c の CALL-COUNT == 2 を次で置き換える。

既存 EE=0 ケースを維持し、guest.c の最後の engine_ok(start_msr) と CALL-COUNT の間に次の一回を追加し、calls == 3 を検査する。MPIC は操作しない。EE=0 で DEC に正の最大値を再装填し、選択中の例外接頭域の 0x900 と 0x500 に stub を設置する。DEC stub は作業 GPR を SPRG0（SPR 272、fixture 専用）に退避し、DEC に正の最大値を書き、GPR を復元して rfi する。外部割込み stub は S1-FAIL-EXT を報告し、sqlite_s1_status に 3 を書いて sqlite_s1_fail で停止する。この停止は全ケースで RED とする。両 stub のコピー元と長さをキャッシュライン境界に揃え、ppc_cache_copy で公開してから EE を開く。OF へ戻らず、EE 区間中に firmware や外部装置の初期化を呼ばない。

~~~c
expected_ee = PPC_MSR_EE;
set_msr(start_msr | expected_ee);   /* sync / mtmsr / isync */
CHECK(msr() == (start_msr | expected_ee), "EE-START");
engine_ok(start_msr | expected_ee);
CHECK(arch_kernel_fp_begin(&guard) == 0, "EE-HELD");
CHECK(msr() & PPC_MSR_EE, "EE-BEFORE-REJECT");
rejected(0, 0);                     /* guard 保持で begin 拒否 */
CHECK(msr() & PPC_MSR_EE, "EE-REJECT");
arch_kernel_fp_end(&guard);
set_msr(start_msr);
~~~

s1_engine_body は再入試行より前に次を実行する。expected_ee の初期値は 0。engine_ok は帰還直後の全 MSR、rejected は callback 不発・active 解除・MSR 不変を既存の方法で照合する。正常呼出しは合計 3 回。

~~~c
CHECK((msr() & PPC_MSR_EE) == expected_ee, "CALLBACK-EE");
~~~

kernel/sqlite_engine.c の「sqlite_engine_active = 1; に続く irq_restore(flags);」だけを一意に選び、復元行を除くコピーをリンクする。7450/7455 の両方で正常 GREEN → 変異実行 RED → 正常 GREEN。変異は既存の BEGIN、ABI-CALLBACK、ENGINE-CALLBACK の各印の後、S1-FAIL-CALLBACK-EE が一度、sqlite_s1_fail、status 3、QEMU rc 0。既存の旧 r1・end 変異の判定を保つ。変更先は下表の段 2・3・5・6。

### 追記 A-3　スタック観測の範囲（§1・§6）

本文 §1 の「両 arch で callback への引数、SP 整列と復元、戻り先、非揮発状態、canary を観測する」のスタック観測範囲を次で置き換える。

入口 SP の整列、backchain、旧 SP の復元、上下 canary の受入は PowerPC S1 に限定する。x86 の sqlite_engine_host.c は callback の局所変数が別スタック範囲内にあることを調べる。観測の対応を擬似式で示す。

~~~c
/* PPC */ aligned16(entry_r1) && old_r1 == restored_r1 && canaries_intact()
/* x86 */ stack_low <= local_address && local_address < stack_top
~~~

PPC の旧 r1 変異は S1-FAIL-SP、x86 は既存の check-sqlite-engine-host とその変異を維持する。今回触るのは設計追記だけである。

### 追記 A-4　両 provider と共通契約の型照合（§2・§6）

本文 §2 の「前方宣言と説明のコメントだけを置く」と §5 の表の「include/exec_arch.h | +3 | 前方宣言と利用契約」を次で置き換える（同表は +5、前方宣言・利用契約・関数型）。

include/exec_arch.h の guard 前方宣言の直後に関数型を置く。

~~~c
typedef int arch_kernel_fp_begin_fn(struct arch_kernel_fp_guard *);
typedef void arch_kernel_fp_end_fn(const struct arch_kernel_fp_guard *);
~~~

kernel/sqlite_engine.c の exec_arch.h、arch_kernel_fp.h の include 後に次を置く。型だけを参照し、格納域も呼出しも生成しない。

~~~c
_Static_assert(__builtin_types_compatible_p(__typeof__(&arch_kernel_fp_begin),
                                            arch_kernel_fp_begin_fn *),
               "arch_kernel_fp_begin signature");
_Static_assert(__builtin_types_compatible_p(__typeof__(&arch_kernel_fp_end),
                                            arch_kernel_fp_end_fn *),
               "arch_kernel_fp_end signature");
~~~

arch_exec_publish は固定名ヘッダのマクロも許し、arch_fp_present は cpu.h に外部関数原型を持つが、この対は x86 の空 static inline と PPC の外部関数を同じ関数型で検査する。

kernel/sqlite_outer.c の _Static_assert、__builtin_types_compatible_p、__typeof__ の組合せを前例とする。他の変異と同じく --mutate 時だけ、型の変異では PPC 固定名ヘッダのコピーだけで begin の戻り型を int から long に、別ケースでは end の引数から const を除く。正常 engine TU をコンパイルし、各変異でそのヘッダを先に探索して同じ TU をコンパイルする。対応する assertion 診断と非 0 を照合し、正常ヘッダで再 GREEN。コンパイル失敗を RED に数えるのはこの型照合試験だけである。実行変異のビルド失敗は受理しない。PM は最終版を同条件で tools/build_compare.sh <A-4 着手直前の枝先（現在 6ee9a56f6）> HEAD --all --output <比較先> に通し、関数の差 0 に加え、両ビルドの x86 sqlite_engine.o に同じ objdump -d を適用して入力パス見出しを除く全逆アセンブル差 0 を受け入れる。

#### 現枝に対する実装修正の段（1 ファイルにつき 1 段）

行数は追加・変更行の見積り。A-1 の製品有効化と PM の成果物比較はこの段数に含めない。

| 段 | ファイル | 行数 | 内容 |
|---|---|---:|---|
| 1 | include/exec_arch.h | 2 | begin/end の契約型 |
| 2 | tools/tests/ppc_sqlite_stack_regs.S | 15〜30 | 再配置可能な DEC・外部割込み vector 本体と境界ラベル |
| 3 | tools/tests/ppc_sqlite_stack_guest.c | 60〜100 | ppc_cache_copy による両 vector の設置・公開、DEC、EE 三地点の観測 |
| 4 | kernel/sqlite_engine.c | 6 | 段 1 の関数型を使い、固定名ヘッダ直後で静的照合 |
| 5 | tools/accept/ppc_sqlite_stack.py | 30〜50 | SOURCES に arch/ppc/cache_copy.S を追加。EE 実行変異と診断、独立した型変異のコンパイル判定 |
| 6 | tools/checks/ppc-sqlite-stack-guest.py | 3〜6 | CHECK の basis に A-2・A-4、covers に arch/ppc/cache_copy.S と型照合・変異の入力を反映 |

段 5 の前に tools/tests と tools/accept の既存変異を検索し、active 代入と irq_restore の連続 2 行、begin と end の型変異の置換元が各対象で一意に一致することを確認する。変更で古くなった目印を移し、各変異で所定の RED を確認する。

#### 追記の確定 (2026-11-18、Opus 第 2 回、PM が反映)
- P3-a (A-2 の 0x500 の stub): 写した先で動く本体なので PC 相対の b/bl は使わない。C の停止関数へは絶対番地 (lis/ori → mtctr → bctrl、または ba) で飛ぶ。例外の中では MSR の FP が落ちているので、その先は整数だけの関数 (say・finish) を呼ぶ。
- P3-b (A-2): vector を書く前に `CHECK(!(msr() & PPC_MSR_IP), "VECTOR-PREFIX")` を置き、「選択中の例外接頭域」を「0 番地 (IP=0 を検査)」に改める。
