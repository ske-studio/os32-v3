# PowerPC の KAPI: db の組 17 本の公開 (計画の段 7b) — 詳細設計 r3

> 基点: main 80df94029 (読んだ木は wt/ppc-int-1119 = 112bf4cdc)。r1・r2・r3 とも 2026-11-19。r3 は REVIEW.r2 (sol) と 7b-選択 (wt/ppc-sqlite-select b52fa778e、Opus Approve) の反映で、往復の最後。
> 前提 (議論しない): シングルタスク・協調切替。PowerPC はハードウェアの FPU を判定せずに使う。カーネルに libgcc を入れない。SQL の PPC 再実装はしない。module は静的 link (TASK_PPC_SHELL.md:14)。
> ビルドしていない。大きさは見積もりで、関門は P0 (部品) と P2 (3 像の最終の map)。

## r2→r3 の変更

- S1 (選択の実物) — b52fa778e が EXEC の前提と PowerPC の GAS の登録 (`sqlite.mk` の `GAS_ARCH_EXEC +=`) を済ませている。r2 の ppc-sources.mk の行は消す (二重登録)。smoke の TU (`os32_sqlite_test.c`) と module の登録は P2 で x86 の枝へ (§2-2・§2-3)。
- S2 (pool) — 元の VFS は 512 KiB の静的配列で `sqlite_module_pool` を定義しない。既存の生成器に静的 link 用の出力を足し、PowerPC も owner の pool を受ける。P0 でシンボルと BSS を確かめる (§2-3)。
- S3 (生成規則・softfloat の変数) — `SQLITE_GENERATED` の規則を `build/sqlite-gen.mk` に出して両 CPU から読む。softfloat の集合は `build/softfloat.mk` (config.mk:67 で両 CPU が読む) に足す (§2-3)。
- S4 (旗) — SQLite の本体の専用の旗に `-std=gnu89 -DNDEBUG -D__KERNEL_BUILD__` (config.mk:192-193 と同じ) を明記 (§2-1・§2-3)。
- S5 (gc の判定) — 除去は最終の ELF の nm と map の「採用された節」で判定 (map の破棄節の一覧に名前が出るのは正常)。Opus の P3 (module_runtime・irq・ime_boot_bytes が無くリンクで止まる見込み) を P2 の最初の確認と完了の基準に入れ、止まった時の戻り先を決めた (§2-2・§6)。
- S6 (quiesce の再マップ失敗) — `body_stop` の戻りと残る状態の表。x86 の STACK の fault の位置を明記 (§2-2)。
- S7 (名前の境界) — 反論 1 は解消 (判定は足さないで合意)。DELETE の journal を試験で固定し、24 byte は open 成功・CREATE 失敗・close 後の本体 0 byte・別 DB の行を確かめる (§2-4・§5 #7)。
- S8 (変異の局所化) — #3 は見張り値を置いた対象の step の復元だけを欠落 (2 ファイルの変異)、#4 は `db_open_existing` の主 DB の大きさの判定に局所化。どちらも直前の GREEN の行を置く (§5)。
- S9 (P2 の実動作) — 起動ログの rc=0 だけでなく、未公開のまま内部の経路で `/tmp` の create・commit・再 open を確かめる受入を P2 に足す (§5)。
- r1→r2 の変更 (R1〜R20、PM 決定 U1・U2) は DESIGN.r2.md の冒頭のまま。

## 1. 実物の確認
### 1-1. 計画の前提と実コードの食い違い (8 件。r1 から #4 の言い方だけ直した)

#1〜#3・#5〜#8 は r1 §1-1 の表のまま (変更なし)。直した行:

| # | 実物 | 根拠 |
|---|---|---|
| 4 | SQLite の VFS は**主 DB に ext2 を要求**する。PowerPC の mount は iso9660 (読み取り専用)・ramfs・serialfs。ramfs は 16 個 × 1024 B、inode・unlink・sync の口なし | `os32_sqlite_vfs.c:524-540`、`ext2_vfs.c:483`、`boot_root.c:13,28`、`ramfs.h:6-7`、`ramfs.c:157-160` |
| 3 (補足) | x86 の本体は kmath も同梱 | `sqlite-proto.mk:8,29-31` |

### 1-2. 統合の判断 3 点
1. **既存・計画**: SQL・KAPI・VFS の層・facade は x86 の物をそのまま使う。**新規の差分**は (a) 静的な本体の起動 30 行、(b) ramfs の SQLite の能力、(c) PowerPC の製品の build (旗・GAS の登録) と監査の FP 許可。
2. **1 つの実装か**: はい。CPU の差は既存の trampoline と `arch_kernel_fp_begin/end` だけ。pool・stack の準備と後始末は共通の 2 関数 (§2-2) を x86 の module と PowerPC の静的起動が呼ぶ。ramfs は共通の fs。
3. **計画の向き**: 合う。facade の固定長の口は整数・ポインタ・u64 戻り・out ポインタだけで、集成体の値返しは無い。

## 2. 構造
### 2-1. 像と TU

入れる像は EXEC の 3 像 (sh・link-sh・shell)。link には入れない。

| TU | 旗 |
|---|---|
| `kapi/kapi_db.c`、`kernel/sqlite_engine.c`、`kernel/sqlite_outer.c`、`kernel/sqlite_static.c` (新)、`lib/softfloat_bits.c` と `SOFTFLOAT_RESIDENT_SOURCES` (softfloat.mk:44) | 既定 (soft-float) |
| `arch/ppc/sqlite_stack_ppc.S` | 既定 |
| 本体: `lib/sqlite3/sqlite3.c`・`os32_sqlite_vfs.c`・`os32_sqlite_module.c`・`lib/kmath.c` | `-mhard-float -Os`。`sqlite3.c`・VFS は加えて `-std=gnu89 -DNDEBUG -D__KERNEL_BUILD__ -Wno-long-long -w -include lib/sqlite3/os32_sqlite_config.h` (x86 の CFLAGS_SQLITE と同じ)。VFS は生成器の出力 (§2-3) |
| 変換: `lib/softfloat_abi.c` と P0 の表で要ると出た SoftFloat | `-mhard-float -Os` |

変換の provider の表 (r2 の見込み。**P0 が実測で埋めて確定**。呼出しが実在しない行は足さない):

| 名前 | 呼出し側 | 定義 | 閉包 | specialize・状態 |
|---|---|---|---|---|
| `__floatdidf` | sqlite3.o | `softfloat_abi.c:47` | `i64_to_f64.c`、s_normRoundPackToF64・s_roundPackToF64 | 丸めは `softfloat_roundingMode` (既定の最近接偶数)、`softfloat_state.c` |
| `__fixdfdi` | sqlite3.o | `softfloat_abi.c:43` | `f64_to_i64.c`、s_roundToI64・shift の s_* | 範囲外・NaN は 8086 specialize の値 (x86 の OS32 と同じ結果)、`softfloat_raiseFlags.c` (旗は誰も読まない) |
| `__fixunsdfdi`・`__floatundidf` | P0 で有無 | `softfloat_abi.c:45,49` | `f64_to_ui64.c`・`ui64_to_f64.c` | 同上 |
| `__divdi3` 系・`kmem*`・`kstr*` | 本体 | 像に既にある `lib/divdi3.c`・`lib/kstring_c.c` | — | 整数だけ。カーネルの物を共用 |
| `fabs` ほか通常名 | sqlite3.o | `lib/kmath.c` | — | hard-float で組む |

- 状態はグローバル 2 つ (丸め・例外の旗)。シングルタスクで書き手は本体だけ、既定のまま使う。
- 境目: 固定長の口は整数とポインタだけ。**va_list は 7b の像では渡らない** — `os32_sqlite_snprintf` (`sqlite_outer.c:10`) の呼び手は `kernel/ime/` だけで PowerPC は未選択。P2 が map で `os32_sqlite_snprintf` が像に無いことを確かめる。7a (IME) のために P0 で両フラグの va_list の表現 (`sizeof`・GPR の保存域の並び・`va_arg(int/u32/char*)` の逆アセンブル) を照合して表に残す。境目を渡る書式は `%u %d %s` に固定 (今の呼び手 6 か所はその範囲)。本体からの `kprintf` は整数の可変長だけ (同じ照合に含める)。

### 2-2. 本線の前提の段
- **7b-選択** は済 (wt/ppc-sqlite-select b52fa778e、Opus Approve): `FEATURE_SQLITE` の前提は `FEATURE_EXEC`、`sqlite.mk` は PowerPC で `GAS_ARCH_EXEC += arch/ppc/sqlite_stack_ppc.S`・x86 で NASM。smoke の TU と module の登録は共通のまま残っている (P2 が x86 の枝へ移す)。PowerMac の既定は 0 のまま (P2 でリンクが通る版と同じコミットで `?= $(FEATURE_EXEC)` にする)。
- **7b-分割** (`kernel/sqlite_engine.c`・`.h` のその場の切り出しだけ。移動なし): `sqlite_start` (:137-160) と `sqlite_quiesce` (:167-184) から次の 2 関数を切り出し、x86 はそれを呼ぶ。fault 注入の 2 行 (`SERVICES_FAULT_POOL`・`_STACK`) は start の中にそのまま残す (PowerPC では常に 0)。`candidate_valid`・`valid_api`・`cleanup_blocked`・`SERVICES_FAULT_SQLITE_INIT`・`_SHUTDOWN` と module の隔離は x86 の呼び手の側に残す。
```c
/* kernel/sqlite_engine.h */
typedef int sqlite_body_init_fn(void *pool, SqliteModuleApi *out);
enum { SQLITE_BODY_OK, SQLITE_BODY_NO_POOL, SQLITE_BODY_NO_STACK, SQLITE_BODY_NO_GUARD, SQLITE_BODY_INIT };
/* pool → stack → guard page → 初回の塗り → 別スタックで init (init_called = 1)。 */
int sqlite_engine_body_start(u32 owner, sqlite_body_init_fn *init, SqliteModuleApi *out, int *init_rc);
enum { SQLITE_STOP_DONE, SQLITE_STOP_BUSY, SQLITE_STOP_SHUTDOWN, SQLITE_STOP_REMAP };   /* 下の表 */
int sqlite_engine_body_stop(int (*shutdown)(void));
```

| `body_stop` の戻り | 起きたこと | 残る状態 | 再び呼んだ時 |
|---|---|---|---|
| DONE | shutdown 成功 (init_called の時)、guard 再マップ | `stack_top`・`pool_phys`・`paging_sqlite_stack_guard`・`init_called` = 0 | 何もせず DONE |
| BUSY | `sqlite_engine_active` か別スタックに入れない | 何も変えない | 同じ判定から |
| SHUTDOWN | shutdown が失敗 | 何も変えない (`init_called` = 1、guard は NP) | shutdown からやり直す |
| REMAP | shutdown は成功、guard の再マップが失敗 | `init_called` = 0、`stack_top` = 0 (engine は常に拒否)。`paging_sqlite_stack_guard` (NP のまま) と `pool_phys` は残す | shutdown を飛ばし再マップだけやり直す |

  x86 の quiesce は DONE だけを 1、ほかを 0 (module は隔離、頁は回収しない — 今と同じ) にする。`body_start` の中の順は今の :140-145 の字のまま: POOL の fault → pool の確保 → **STACK の fault (pool の確保の直後)** → stack の確保。本線の受入: x86 の fault 注入 (POOL・STACK・SQLITE_INIT・SHUTDOWN)・quiesce・FEP の rollback・legacy order の既存の試験が同じ結果。
- PowerPC は `sqlite_engine.c` を丸ごと選ぶ。module の接着・services_boot・services_report は呼び手が無く (`ver_collect.c` は PENDING)、`--gc-sections` (ppc-powermac.mk:154) で落ちる見込み。Opus の P3 のとおり、落ちなければ `module_load`・`module_start`・`fep_module_*`・`ime_boot_bytes` ほかが未解決でリンクが止まる。**P2 の最初の確認** (`FEATURE_SQLITE=1` を手で与えた 1 像のリンク): 通ること、最終の ELF の `nm` に `services_boot`・`os32_sqlite_start`・`module_load`・`fep_*`・`ime_boot_bytes` が無いこと、map の採用された節に `sqlite_engine.o` のそれらの節が無いこと。止まったら P2 を止めて PM へ — 戻り先は本線が `sqlite_engine.c:96-222,225-595` を x86 だけの TU へ移す段 (r1 §2-2 の形)。PowerPC の側に stub は足さない。
### 2-3. 静的な本体の起動と build (P2)
```c
/* kernel/sqlite_static.c */
extern int sqlite_module_init(void *pool, SqliteModuleApi *out);   /* os32_sqlite_module.c:6 */
static SqliteModuleApi static_api;
int sqlite_static_boot(void)
{
    int init_rc = SQLITE_MISUSE;
    int rc = sqlite_engine_body_start(LEDGER_OWNER_KERNEL, sqlite_module_init, &static_api, &init_rc);
    if (rc == SQLITE_BODY_OK && init_rc == SQLITE_OK) {
        sqlite_module_api = &static_api;
        if (!os32_sqlite_outer_boot_check(0, 0)) {
            services_stage = SERVICES_SQLITE_ONLY; services_public = 1; return 0;
        }
        sqlite_module_api = &sqlite_stub_api; rc = SQLITE_BODY_INIT;
    }
    if (rc == SQLITE_BODY_OK || rc == SQLITE_BODY_INIT) (void)sqlite_engine_body_stop(static_api.shutdown);
    return rc ? rc : SQLITE_BODY_INIT;
}
```
| 失敗点 | 残る状態 | 後始末 |
|---|---|---|
| pool が取れない | 何も無い | 無し |
| stack が取れない / guard を外せない | pool (と stack) の頁、`stack_top` = 0 のまま | 頁は `LEDGER_OWNER_KERNEL` のまま保持 (解放しない。起動時 1 回だけ・最大 548 KB)。engine は `stack_top` = 0 で常に拒否 |
| init が失敗 / boot_check が失敗 | init_called = 1、guard は NP | `body_stop`。DONE でも頁は保持。SHUTDOWN・REMAP は §2-2 の表の状態のまま保持し、起動ログに戻りを出す (再試行しない) |

どの場合も `services_public` = 0・`sqlite_module_api` = stub で、db の 17 本は NOSYS、起動は続く。
```c
/* platform/powermac/ppc_kernel.c — pm_exec_boot (:401) の直前。固定の並び hostfs→usb→work→input の外 */
#if OS32_HOOK_KAPI_DB
    kprintf(KLOG_ATTR, "[sqlite] static rc=%d\n", sqlite_static_boot());
#endif
```
```make
# build/features/sqlite.mk (P2 が b52fa778e の形から直す。GAS の行は触らない)
C_FEATURES += kernel/sqlite_engine.c kernel/sqlite_outer.c
ifeq ($(ARCH),ppc)
GAS_ARCH_EXEC += arch/ppc/sqlite_stack_ppc.S
include build/features/sqlite-static.mk
else
C_FEATURES += lib/sqlite3/os32_sqlite_test.c
ASM_FEATURES += lib/sqlite3/sqlite_stack.asm
MODULE_FEATURES += sqlite
MODULE_MK_sqlite = build/sqlite-proto.mk
endif
# build/features/sqlite-static.mk (新)
C_FEATURES += kapi/kapi_db.c kernel/sqlite_static.c lib/softfloat_bits.c $(SOFTFLOAT_RESIDENT_SOURCES) \
  lib/sqlite3/sqlite3.c lib/sqlite3/os32_sqlite_module.c lib/kmath.c lib/softfloat_abi.c $(SOFTFLOAT_CONV_SOURCES)
# build/softfloat.mk (config.mk:67 で両 CPU が読む。C_SELECTED の展開より前): SOFTFLOAT_CONV_SOURCES := P0 の表の物
# build/sqlite-gen.mk (新。sqlite-proto.mk:3 の SQLITE_PROTO_DEF_SOURCES と :47-52 の SQLITE_GEN・SQLITE_GENERATED・その規則を純移動)
#   sqlite-proto.mk と ppc-powermac.mk の両方が include する
# build/platform/powermac.mk
FEATURE_SQLITE ?= $(FEATURE_EXEC)
# build/sources/hooks.mk
HOOK_kapi_db = $(if $(filter 1,$(FEATURE_PC98_PENDING) $(FEATURE_SQLITE)),1,0)
# build/sources/common.mk: COMPILE_ONLY_PPC から sqlite_engine.c、PENDING から kapi_db.c・kmath.c を外す
# build/targets/ppc-powermac.mk
KB_SQLITE_VFS := $(KB_OUT)/gen/sqlite/os32_sqlite_vfs
$(KB_SQLITE_VFS).c: tools/gen_sqlite_imports.py lib/sqlite3/os32_sqlite_vfs.c
	python3 tools/gen_sqlite_imports.py --static-vfs $@
KB_EXTRA += $(KB_SQLITE_VFS).o                    # 規則は既存の %.o の形に -I$(dir lib/sqlite3/) を足して 1 本
KB_SQLITE_STD := $(KB_OUT)/lib/sqlite3/sqlite3.o $(KB_SQLITE_VFS).o
KB_SQLITE_FP := $(KB_SQLITE_STD) $(addprefix $(KB_OUT)/lib/,sqlite3/os32_sqlite_module.o kmath.o softfloat_abi.o \
  $(SOFTFLOAT_CONV_SOURCES:lib/%.c=%.o))
$(KB_SQLITE_FP): KB_TU_FLAGS += -mhard-float -Os -Ilib/sqlite3 -Ilib/softfloat -Ifs
$(KB_SQLITE_STD): KB_TU_FLAGS += -std=gnu89 -DNDEBUG -Wno-long-long -w -Wno-error -include lib/sqlite3/os32_sqlite_config.h
$(KB_SQLITE_FP) $(addprefix $(KB_OUT)/,kernel/sqlite_engine.o kernel/sqlite_outer.o kernel/sqlite_static.o kapi/kapi_db.o): | $(SQLITE_GENERATED)
```
- pool: `gen_sqlite_imports.py` の今の置換 (:85-91、`static char sqlite_mem_pool[SQLITE_MEMSYS5_SIZE];` → `char *sqlite_module_pool;` と `tick_count` → `ktime_ticks()`) を関数に出し、`--static-vfs <出力>` はその結果だけを書く (module 用と違い先頭の `#include "sqlite_imports.h"` を付けない = slot を通さず直に呼ぶ)。x86 の出力は 1 byte も変わらないこと (生成器の既存の試験)。P0 の確認: VFS の object に `sqlite_module_pool` (4 byte の BSS) が在り `sqlite_mem_pool` が無い、本体の BSS の和に 512 KiB が入っていない。
- `-D__KERNEL_BUILD__` は `KB_FLAGS` に既にある。`-std=gnu89` は後ろの指定が勝つ。`os32_sqlite_module.c`・`kmath.c` は x86 と同じく GNU11 のまま。
- 最後の行が生成ヘッダ (`lib/sqlite3/generated/`、`gen_sqlite_imports.py --contracts-only`、sqlite-proto.mk:47-52) の明示の前提。clean の並列ビルド (`-j`) で 1 回通すのが P2 の検査。import の slot・stub は使わない (本体はカーネルの関数を直に呼ぶ)。
- 登録の確認: b52fa778e の `test_build_selection.py` の場合に乗る (`sqlite_stack_ppc.S` が EXEC の 3 像に 1 本ずつ・link に 0 本、`os32_sqlite_test.c` と module の登録が PowerPC の選択に無い)。clean の `-j` を P0・P2・`common-compile` の 3 か所で通す。
- SoftFloat の endian: `lib/softfloat/platform.h:42` を `__BYTE_ORDER__` で分ける。
- guard page: `paging_set_not_present` が PowerPC の頁表で効くかは P2 の最初の確認 (効かなければ `SQLITE_BODY_NO_GUARD` で止まるので起動ログで分かる。PM へ)。

### 2-4. VFS — `/tmp` (ramfs) に SQLite の能力 (P1)
PM 決定 U1: 揮発の `/tmp` で 7b を閉じる。永続の媒体は別の計画。
```c
/* fs/vfs.h */
#define VFS_SQLITE_RAMFS 2
/* lib/sqlite3/os32_sqlite_vfs.c:526 */
        if (backend <= VFS_SQLITE_UNSUPPORTED)      /* 旧: backend != VFS_SQLITE_EXT2 */
/* fs/ramfs/ramfs.h */
enum { RAMFS_FILES = 16, RAMFS_FILE_MAX = 32 * 1024, RAMFS_DATA_MAX = 64 * 1024, RAMFS_GROW = 1024 };
/* fs/ramfs/ramfs.c */
typedef struct { char name[32]; u8 *data; u32 size, cap, ino; } RamFile;      /* ino は作成の通し番号、使い回さない */
typedef struct { RamFile files[RAMFS_FILES]; u32 count, cap_total, next_ino; } RamFs;
_Static_assert(RAMFS_FILE_MAX <= RAMFS_DATA_MAX, "ramfs data budget");       /* 旧の積の assert (:17) を置き換え */
static int ramfs_reserve(RamFs *fs, RamFile *f, u32 need);   /* 下の契約 */
static int ramfs_sync(void *ctx) { (void)ctx; return VFS_OK; }
static const VfsInoOps ramfs_ino_ops = { ramfs_ino_lookup, ramfs_ino_read, ramfs_ino_write, ramfs_ino_stat, ramfs_ino_truncate };
static int ramfs_sqlite_resize(void *ctx, u32 ino, u32 size);  /* ext2 と同じ: 同じ大きさ = OK、0 = 切り詰め、他 = NOSYS */
static const VfsSqliteOps ramfs_sqlite_ops = { VFS_SQLITE_RAMFS, ramfs_sqlite_resize, ramfs_sqlite_writable };
/* ramfs_ops に足す: .ino .sqlite .unlink .create_excl .sync */
```
容量の契約:

| 操作 | 決まり |
|---|---|
| 数え方 | `cap_total` = 全ファイルの `cap` (確保した量) の和。上限 `RAMFS_DATA_MAX`。作成時の `cap` は `RAMFS_GROW` |
| 伸ばす (`ramfs_reserve`) | `need` を 1 KB に切り上げ。`need > RAMFS_FILE_MAX` か `cap_total - cap + need > RAMFS_DATA_MAX` なら **確保の前に** `VFS_ERR_NOSPC`。新しい領域を取れてから写して旧を放す (取れなければ旧データ・`size`・`cap` は不変で NOSPC) |
| 穴 | `offset > size` の書込みは間を 0 で埋める。伸ばした領域も 0 |
| truncate (0) | `size` = 0、`cap` を `RAMFS_GROW` に戻し差を `cap_total` から引く |
| unlink | 全部放し `cap_total` から引く。枠は詰める。開いている FD は VFS が失効にする (vfs.h:68) |
| 満杯 | 書込みが NOSPC → SQLite の commit が失敗コードで返る。既存の commit 済みの内容は読める |

- 同じ実体: パスの口 (`ramfs_read`・`ramfs_write_stream` ほか) と inode の口は同じ `RamFile` を読む・書く。パスの口の中身を inode の口の呼出しに畳む。`/tmp` の今の振る舞い (`echo > /tmp/f`・cat) は変えない。
- 二重 open: 同じ DB の 2 つ目の open を拒むのは主 DB の inode の claim (`vfs_fd.c:871-886`)。ramfs は `ino` と mount の世代を返すだけ。
- 名前: ramfs の名前は 31 byte まで (`ramfs.c:23`)。VFS の `journal_name_fits` は VFS_MAX_PATH だけを見るので、24〜31 byte の DB は open が成功し、既定の DELETE の journal を作る最初の書込みだけが失敗コードで返る。判定は足さず、この振る舞いを試験で固定する (§5 #7)。
- P1 の最初の確認: `vfs_open_internal` (`vfs_fd.c:176-306`) を読み、inode を持つ FS に要る口が上の 5 つで足りるか。

### 2-5. 別スタックと FP の guard・所有者の回収・SHM の結果 (足すコードは無い)
- 呼出し: USER → `kapi_db_*` → `os32_sqlite_outer_*` → `sqlite_engine_run` → `arch_kernel_fp_begin` → `sqlite_call_on_alt_stack` (r1・LR・GPR を戻す、`sqlite_stack_ppc.S:7-24`) → 本体 → 通常の return → `arch_kernel_fp_end` (旧スタックの guard から FPR・FPSCR・MSR を戻す、`kernel_fp.c:23-46`)。
- guard の中の決まり: IRQ は元の EE のまま動き、IRQ の中は整数だけ。callback の中から park・exec・USER の呼出しをしない。P2 が到達する経路を読んで確かめる 2 点: xDelete の `vfs_sync()` は全 mount の sync を回る (`os32_sqlite_vfs.c:585`、`fs/vfs.c:802-811`。今の serialfs の sync は無動作、ramfs は成功を返すだけ)、xSleep (`os32_sqlite_vfs.c:639`) は tick を見て回るだけで park しない。どちらかが park に届くなら PM へ。
- 所有者: `db_slots[].owner = res_owner_get()` (`kapi_db.c:151`)、`slot_get` が別の所有者を拒む (`kapi_db_helpers.inc:60`)。回収は `exec_reclaim_resources` が FD の回収より先に `exec_db_cleanup_owned(id)` を呼ぶ (`exec_finish.inc:128-149`)。確かめ方はゲスト (§5 #6)。
- SHM の結果・bind の寿命は共通のまま (`kapi_db.c:87`、`kapi_db_v50.inc:367-390`)。

### 2-6. 製品の監査 (P2)
今は FP の命令を `fp_context.o` の 2 つの葉だけに許す (`kboot_audit.py:55-76,101-105`、ppc-powermac.mk:163-165)。
```python
parser.add_argument('--fp-body-object', action='append', default=[])   # KB_SQLITE_FP を渡す
# object ごとの検査: 引数に在る object は FLOAT を許す (ALTIVEC は不可)。ほかの object は今のまま。
# 像の検査: map の入力節 (.text*) の出所が --fp-body-object の object である番地の範囲を集め、
#           FLOAT はその範囲か既存の allowed の時だけ可。decoded() が番地も返すよう 1 欄足す。
```
- mk: `audit:` の行に `$(addprefix --fp-body-object ,$(KB_SQLITE_FP))` (FEATURE_SQLITE=1 の時だけ)。
- host の試験 (既存の監査の試験に 2 場合): 許可の無い object に FP 命令 → 問題として出る / 許可した object の節の番地の FP は通り、同じ像のほかの番地の FP は出る。

## 3. 部品の大きさと関門
見積もり (予備の値): sqlite3.o は 420〜480 KB (x86 の実測 367 KB × 1.15〜1.30)、ほかの TU は 30〜40 KB (BSS 約 15 KB)。pool 512 KB + stack 32 KB + guard 4 KB は像の外 (pgalloc)。

今の余り (`__kernel_chain_end` → `PM_KERNEL_CHAIN_LIMIT` 0x3E0000): link-sh 580 KB、sh 644 KB、shell 未計測。`PM_KERNEL_IMAGE_MAX` (0x275000) の余りも同じ値 (`kernel.ld:55-60`)。

**関門 (PM 決定 U2: 今の上限の内で測ってから PM が判定)**:

1. P0: 全 TU の text・rodata・data・BSS の和を出す (480 KB は sqlite3.o の text の目安で、合否ではない)。
2. P2: sh・link-sh・shell の最終の map で、text・rodata・data・BSS と整列込みの `__kernel_resident_end − __kernel_image_start ≤ PM_KERNEL_IMAGE_MAX` と `__kernel_chain_end ≤ PM_KERNEL_CHAIN_LIMIT` (リンクの ASSERT)、3 像の余りの値、QEMU の起動で pool・stack が取れた (`[sqlite] static rc=0`) ことを PM に出す。上限を変えるかは測った後の別の判断。

## 4. ファイルごとの変更一覧と分担
| 段 | 担当 | ファイル (新 = 新規) | 行数 |
|---|---|---|---|
| 7b-選択 | 本線 (済) | b52fa778e: `build/select.mk` / `build/features/sqlite.mk` / `test_build_selection.py` | — |
| 7b-分割 | 本線 | `kernel/sqlite_engine.c` / `kernel/sqlite_engine.h` | ±60 / +9 |
| P0 | PPC の PM | 無し (計測。成果物は表 3 枚を票へ) | — |
| P1 | 実装者 A | `fs/ramfs/ramfs.c` / `ramfs.h` / `fs/vfs.h` / `lib/sqlite3/os32_sqlite_vfs.c` | +150 / 2 / 1 / 1 |
| | | `tools/tests/ramfs_sqlite_host.c` (新) / `test_ramfs_sqlite.py` (新) / `tools/checks/ramfs-sqlite-host.py` (新) | 160 / 50 / 15 |
| P2 | 実装者 B | `kernel/sqlite_static.c` (新) / `platform/powermac/ppc_kernel.c` / `lib/softfloat/platform.h` | 30 / +4 / +3 |
| | | `build/features/sqlite.mk` / `sqlite-static.mk` (新) / `build/sqlite-gen.mk` (新・純移動) / `build/sqlite-proto.mk` / `build/platform/powermac.mk` / `build/sources/hooks.mk` / `build/sources/common.mk` / `build/softfloat.mk` / `build/targets/ppc-powermac.mk` | ±6 / 3 / 8 / −7 / +1 / +1 / ±3 / +6 / +14 |
| | | `tools/gen_sqlite_imports.py` (`--static-vfs`) と生成器の試験 / `tools/accept/ppc_sqlite_static.py` (新) / `tools/tests/ppc_sqlite_static_probe.inc` (新) / `tools/checks/ppc-sqlite-static-guest.py` (新) / `test_build_selection.py` の追従 | +15 / +10 / 80 / 50 / 24 / 数行 |
| | | `tools/ppc/kboot_audit.py` と その host の試験 / `build_profile_ppc.json` ほか一覧の試験の追従 | +30 / +40 / 数行 |
| P3 | 実装者 C | `userland/ppc/dbprob.c` (新) / `tools/accept/ppc_db.py` (新) / `tools/checks/ppc-db-guest.py` (新) | 170 / 150 / 24 |
| | | `sdk/kapi_target_ppc.py` (db の組 :92-110 と ZERO_SLOTS の :170) / `tools/ppc/test_kapi_binding.py` / `build/ppc-user.mk` / `tools/checks/weights.json` | −20 / −4 / +2 / +2 |

- 共有の登録ファイル (select.mk・common.mk・hooks.mk) は PARALLEL_BRANCHES §2 の持ち主から受け取る。
- P2 は本線の持ち物 (`sqlite.mk`・`sqlite-proto.mk`・`gen_sqlite_imports.py`) を触る。出す前に本線の PM に知らせ、x86 の製品比較が同一であることを完了条件にする。
- **P1 は x86 の製品が変わる** (`os32_sqlite_vfs.c:526` の 1 比較で sqlite.mod が変わる)。出す前に本線の PM に知らせる。P2 は x86 の製品比較が同一。

## 5. 受入 (QEMU 7450・7455、像は link-sh、`/tmp/T.DB`)
期待の出所は PPC_KAPI_FILL_DESIGN §5 の db の行。`DBPROB.BIN` を引数 1・2 で 2 回走らせ、`tools/accept/ppc_db.py` が RSH の出力を照らす。待つのはコマンドの完了 (プロンプトの戻り) と固有の行で、時刻では待たない。撮影はしない。

| # | 項目 | ゲストの行 (正像は全部 OK) | host |
|---|---|---|---|
| 1 | create / insert | `DB-OPEN`・`DB-CREATE`・`DB-INSERT` | ramfs-sqlite-host |
| 2 | bind 4 種 | `DB-BIND-RC` (bind 5 回と step の戻り)、`DB-BIND-VAL` (`SELECT i,a,b,hex(x),typeof(n)` が 7・alpha・beta・hex・null) | kapi_db_v50_host (既存) |
| 3 | step / column と FP | `DB-STEP` (`column_int`・`column_text`)、`DB-CONV` (`3000000000*1.0` = "3000000000.0"、`-3000000000*1.0` = "-3000000000.0"、`3000000000.5 % 7` = 4、`-3000000000.5 % 7` = −4、`0.1+0.2` = "0.3")、`DB-FP-PRE` (ここまでの REAL の step が成功。直後に目印の `db_error_code(-2)` を呼ぶ — 正像では MISUSE が返るだけ)、対象の step: 直前に FPSCR の RN をゼロ方向・f31 に見張り値 → REAL の step → `DB-FPSCR` (RN と f31 が同じ)、`DB-NEXT` (その次の db 呼出しが成功) | sqlite-engine (既存)、S1 |
| 4 | finalize / close / 再 open | 1 回目 `DB-CLOSE`。2 回目 `DB-STAT` (`/tmp/T.DB` を KAPI の stat で読み大きさ > 0)、`DB-REOPEN` (`db_open_existing` と `count(*)` = commit 済みの行数) | — |
| 5 | 失敗コード | `DB-ERR-RC` (`db_open("/X.DB")` = −1、壊れた SQL = −1)、`DB-ERR-CODE` (`db_error_code(-1)` = 14、`db_error_code(h)` = 1、`db_last_error` が空でない) | kapi_db_v50_host |
| 6 | 使用量・終了回収 | 各回の最初に `DB-BASE n` (open 前の `db_mem_used`)、`DB-USED` (open 中 > 基準、close 後 = 基準)。1 回目は最後に BEGIN → INSERT と step 途中の stmt を残して exit。2 回目 `DB-RECLAIM` (基準が 1 回目と同じ・再 open 成功・未 commit の行が無い) | kapi_db_owned_host (既存) |
| 7 | 名前の境界 | `DB-NAME` (`PRAGMA journal_mode` が delete。23 byte の名前は CREATE まで通る。24 byte は open 成功・CREATE が −1・close 後に本体が 0 byte。その後も `/tmp/T.DB` の既存の行が読める) | ramfs-sqlite-host |

`DB-CONV` の式が P0 の表の helper を実際に通ることは、P0 の逆アセンブルの呼出し箇所 (sqlite3.o の中の関数名) で対応を付ける。

変異 (各 1 行。**前提 GREEN** = その変異でも出なければならない行、**RED** = 欠ける行):

| # | ファイルと一意な目印 | 置き換え | 前提 GREEN | RED |
|---|---|---|---|---|
| 1 | `fs/ramfs/ramfs.c` の `.sqlite = &ramfs_sqlite_ops` | 消す | `[sqlite] static rc=0` | `DB-OPEN` |
| 2 | `kapi/kapi_db_v50.inc:375-376` の 2 行 `text_copy_buf, length,` + 改行 + `SQLITE_TRANSIENT));` | `SQLITE_STATIC` (複製の欠落) | `DB-OPEN`・`DB-BIND-RC` | `DB-BIND-VAL` (a も beta) |
| 3 | 2 ファイル (`KB_MUT_SRC` と `KB_MUT_EXTRA_SRC`)。(a) `kapi/kapi_db_v50.inc` の `if (handle == -1) {` の前に `{ extern volatile u32 services_boot_fault; if (handle == -2) services_boot_fault = 99; }` を足す。(b) `kernel/sqlite_engine.c:68` の `arch_kernel_fp_end(&guard);` | (b) を `if (services_boot_fault != 99) arch_kernel_fp_end(&guard);` に (目印の後の engine の呼出し = 対象の step だけ所有者の復元を欠落。`services_boot_fault` は PowerPC で起動の後は読まれない) | `[sqlite] static rc=0`、`DB-CONV`、`DB-FP-PRE` | `DB-FPSCR` と `DB-NEXT` |
| 4 | `kapi/kapi_db_v50.inc:199` の `if (st.st_size == 0) {` (ファイルに 1 か所。`db_open_existing` の主 DB の大きさの判定) | `if (st.st_size != 0) {` | 1 回目の全行、2 回目の `DB-BASE`・`DB-STAT` | `DB-REOPEN` |
| 5 | `kapi/kapi_db_helpers.inc:111` の `db_open_fail[owner] = code;` | 消す | `DB-ERR-RC` | `DB-ERR-CODE` |
| 6 | `exec/exec_finish.inc` の `exec_db_cleanup_owned(id);` | `;` (回収の呼出しだけ無動作) | `[sqlite] static rc=0`、1 回目の全行 | `DB-RECLAIM` |

- 台本の型は `tools/accept/ppc_path.py`。目印は台本が始めに一意かを確かめる。P3 は残る absent の見本 (`snd_bgm_is_playing`) と、ほかの受入の目印がこの段で消えないかを rg で横断して確かめる (ppc_qemu.md §3)。
- 登録: `tools/checks/ppc-db-guest.py` (tier 2・full_only・env は ppc-cross・qemu-ppc・socket、covers は ppc-path-guest と同じ)、`tools/checks/ramfs-sqlite-host.py` (tier 1)。QEMU の受入を回すのは PM (CI)。実装者は host の試験と自分の段のビルドだけ。

ramfs-sqlite-host (P1。実物の `fs/vfs_fd.c`・`fs/ramfs/ramfs.c` と host の SQLite を組む) の場合: commit・rollback・close → 再 open / 満杯 (journal 込みで 64 KB を超える INSERT が失敗コード、その前の commit 済みの行は読める、`cap_total` が戻る) / 伸長の確保失敗で旧データ不変 / 穴の 0 / truncate・unlink の返却 / パスの口で書いた物が inode の口で同じ内容・大きさ / 別 FD の読み書き / 同じ DB の二重 open が拒否 / unlink → 同名の再作成で古い FD が新しい実体を指さない / 名前 23・24 byte。

### 段の分け方と回帰の範囲
| 段 | 大きさ | 前の段 | 並列 | この段で閉じる検査 |
|---|---|---|---|---|
| P0 | 半日 | 無し | 今すぐ | 製品と同じ `KB_CC`・`KB_FLAGS` + §2-1 の旗で**全 TU** (本体 4・変換・kapi_db・engine・outer) を一時の置き場に組む。成果物: (a) `nm -u` の未解決名の一覧と provider の対応 (通常名も)、(b) 変換の呼出し側 (sqlite3.o) と定義側 (softfloat_abi.o) の逆アセンブルで double が f1 で渡ること、(c) §2-1 の表の確定、(d) va_list の表、(e) 大きさの和、(f) 試しの部分リンクの map に `libgcc` の archive が出ない |
| P1 | 1 日 | 無し | 7b-選択・7b-分割・P0 と並列 | ramfs-sqlite-host、**x86 の ext2 の回帰** (sqlite の VFS・identity・product の既存の host 試験一式)、x86 の製品比較 (1 比較と ramfs だけ)、PM が ppc-path-guest (`/tmp` の既存の振る舞い) |
| P2 | 1 日半 | 7b-分割・P0・P1 (7b-選択は済) | P3 の dbprob・台本の下書き | 最初に §2-2 のリンクの確認。clean の `-j` で 3 像のリンク、監査 (FP の許可・libgcc 無し)、nm・map の確認 (§2-2・§2-1・§3)、選択の試験。PM が QEMU で**未公開のまま** 3 像の起動と `[sqlite] static rc=0`、**ppc-sqlite-static-guest** (下)、ppc-fp-guest・ppc-path-guest・S1 の退行、x86 の製品比較が同一 |
| P3 | 1 日 | P2 | — | 公開表・test_kapi_binding・AC-KAPI-1 とゲスト (正像 + 変異 6) を**同じ版**で。ppc-db-guest |

- **ppc-sqlite-static-guest** (P2、S9): 製品に試験のコードを入れない。`tools/accept/ppc_sqlite_static.py` が `kernel/sqlite_static.c` の写しを作り、`services_public = 1;` の直後に probe の呼出しを 1 行、末尾に `tools/tests/ppc_sqlite_static_probe.inc` を足して `KB_MUT_SRC` で組む (公開表は db が absent のまま)。probe は内部の口 (`os32_sqlite_group_acquire(OS32_SQLITE_RESIDENT, 0, …)` → `os32_sqlite_group_open("/tmp/P.DB", READWRITE|CREATE)`、`os32_sqlite_test.c:17-19` と同じ形) で CREATE・INSERT (commit、DELETE の journal の作成と削除)・close・再 open・`count(*)` を回し、`[sqlite] probe create=0 commit=0 reopen=0 rows=1 journal=0` を 1 行出す。7450・7455 でこの行を照らす。変異 1 本: `ramfs_sync` の `return VFS_OK;` を `return VFS_ERR_NOSYS;` → `commit` が 0 でない (前提 GREEN は `[sqlite] static rc=0` と `create=0`)。
- `weights.json`: `ppc-sqlite-static-guest` 60。 `ppc-db-guest` 220 (ビルド 7 回 × 2 CPU。ppc-path-guest は 3 回で 89)、`ramfs-sqlite-host` 2。実測の後に os32-ci-inventory で直す。

## 6. 完了の基準の案
- `ABSENT_PROVIDER_SLOTS` が 110 → **93** (組 16 → 15)。`test_kapi_binding.py` GREEN。
- `ppc-db-guest` が 7450・7455 で正像 GREEN、変異 6 本が前提 GREEN の上で表の行だけ RED。`ramfs-sqlite-host` GREEN。
- 3 像がリンクでき、両上限の内。余りの値と pool・stack の確保を票に記録し PM が判定済み。
- `FEATURE_SQLITE=1` の 3 像がリンクで止まらない (Opus の P3)。最終の ELF の nm に `services_boot`・`os32_sqlite_start`・`module_load`・`fep_*`・`ime_boot_bytes`・`os32_sqlite_snprintf`・`sqlite_mem_pool` が無く、map に libgcc の archive が無い。監査が 3 像で GREEN (FP は許可した object の番地だけ、未解決なし)。
- `ppc-sqlite-static-guest` が 7450・7455 で GREEN、変異 1 本が RED (P2 の時点、未公開のまま)。
- 退行なし: ppc-fp-guest・ppc-path-guest・S1、x86 の sqlite の host 試験一式と fault 注入。x86 の製品は P1 の 1 比較以外が同一。

## 7. 未決
無し (U1・U2 は PM 決定で閉じた)。永続の媒体を公開の条件に足す・配置の上限を変える・SQLite の機能を変える、のどれかが要る時だけユーザーへ。PM 間の調整: 7b-分割 (その場の切り出し) を本線の前提の段に足す依頼、P1 が x86 の製品を変える事前の知らせ。

## 反論

無し (r2 の反論 1 は REVIEW.r2 で「判定の省略は可」となり解消。Opus の P3 は反論せず、P2 の最初の確認・完了の基準・戻り先として入れた)。

## 確定 (2026-11-19、sol の第 3 回の条件付きの承認、往復 3 回、PM が反映)

第 3 回 (REVIEW.r3.md) の残り 5 件を、実装の完了条件として次のとおり決める。上の本文と食い違う所は、この節が優先する。

| ID | 直し (実装者はこの形で書く) |
|---|---|
| F1 (P1・§2-3 S2) | 静的の VFS は、module 用の生成器と **pool の置換だけ** を共有する。tick の置換は共有しない — 外した `sqlite_imports.h` の `ktime_ticks` の宣言と、PowerPC で選ばれない `sqlite_kexports.c` の定義に頼らず、静的の版は既存の `tick_count` を使う |
| F2 (P1・§2-3／§4) | PENDING から `kapi_db.c`・`kmath.c` を外す時は、PC-98 の側で両 TU の今の選択の条件と順 (legacy の順) をそのまま保つ。x86 の build_compare で kernel.bin・vmkernel.lz4・.mod が基点と同じ、common-compile の対象からも落ちないことを確かめる |
| F3 (P1・§2-3) | `KB_EXTRA` への静的の VFS の追加、生成した VFS・追加のオブジェクト・SQLite の専用の旗・監査の引数は、**FEATURE_SQLITE=1 の時だけ**。link の像と、EXEC の像で FEATURE_SQLITE=0 を明示した時に、VFS も include も組まれないことを選び方の試験で照合する |
| F4 (P1・§5 P2 S9) | ramfs_sync を失敗させる変異は、CREATE の成功を出力した**後**の INSERT の確定の、最初の xSync だけを失敗させる (常に失敗させると CREATE の自動コミットで落ち、create=0 の観測行が出ない) |
| F5 (P2・§5 #3 S8) | FP の変異は、マーカーを消費する **1 回きり** の形にする (99 のまま残さない)。REAL の行を返す db_step は shm_write_row で SQLite に再入するので、KAPI の中の再入も含めて、最初に欠ける観測行を受入の期待に決めてから変異の RED を判定する |

- 往復の記録: r1 (Fable) → REVIEW.r1 (sol、P1 8 件) → r2 → REVIEW.r2 (P1 5 件、条件付き) → r3 → REVIEW.r3 (条件付きの確定、残り 5 件を PM が反映)。
- PM 決定: U1 = 揮発の /tmp (ramfs) で 7b を閉じる (永続の媒体は別の計画)。U2 = 今の上限 (PM_KERNEL_IMAGE_MAX・PM_KERNEL_CHAIN_LIMIT) の内で、3 像の最終の map で測ってから PM が判定。
- 前提の段: 7b-選択 (PowerPC、wt/ppc-sqlite-select b52fa778e、Opus Approve)。7b-分割 (本線、sqlite_engine.c からその場で 2 関数を切り出す最小差分) は本線 PM に形を送る。P2 で本線の持ち物 (sqlite.mk・sqlite-proto.mk・gen_sqlite_imports.py) を触るので、出す前に本線 PM に知らせる。

## P0 の計測と FP の ABI の調べで決めたこと (2026-11-19、PM 決定)

出所: P0 の計測 (~/os32-tmp/run/ppc-db-p0/P0_RESULT.md、astra) と、sol の調べ (~/os32-tmp/run/ppc-db-fpabi/REPORT.md、試しのリンクと QEMU の独立の ELF で確認)。§2-1・§2-6 の本文より、この節が優先する。

| ID | 決定 |
|---|---|
| A1 | 大きさの関門 (§3、U2): SQLite の全 TU で約 496 KB、link-sh の余り約 98 KB で、今の上限の内に収まる — **通す**。最終は P2 の 3 像の map で再判定 |
| A2 | hard-float と soft-float の .o の混在 (Tag_GNU_Power_ABI_FP の不一致でリンクが止まる): SQLite の hard の TU 群と変換を **部分リンク** (`ld -r -d --fatal-warnings --gc-sections -u sqlite_module_init`) で 1 つの .o にまとめ、`objcopy --keep-global-symbol=sqlite_module_init` で入口を 1 つに絞り、`.gnu.attributes` の tag 4 の hard/soft の欄だけを中立 (0) にする (64-bit long double・struct-return の欄は保つ)。属性は解釈して期待の値と形式を確かめてから書き換え、固定のバイトの位置への無検証の書き込みはしない。hard の raw の .o は最終のリンクの入力から外す。`-nostdlib --no-undefined` は保つ |
| A3 | `--no-warn-mismatch`・TU ごとの `-mno-gnu-attribute`・SQLite 側を soft と名乗らせる、は採らない (誤った soft→hard の double の呼び出しを通すか、境目の型を強制できない — sol の実測) |
| A4 | 最適化は SQLite と変換の TU を `-mhard-float -Os` (-O2 は像に収まらない)。`-Os` が出す GPR の復元 18 名 (`_restgpr_{14..31}_x`) と FPR の復元 2 名 (`_restfpr_{30,31}_x`) は `arch/ppc/` の自前の .S (PowerPC の SysV ABI 1995 §3 の図 3-32/33、r11 基準、lmw/lfd、r11+4 の LR を戻し r1←r11) に置き、**部分リンクの中に閉じる** (FP の復元も FPU の guard の中) |
| A5 | 64 ビットのシフト `__ashldi3`・`__lshrdi3` は `lib/shift_di.c` (新、x86 の `lib/divdi3.c` に倣う、PowerPC の選択で既定の soft・-O2、64 ビットを u32 の上下の語に分け 0・1〜31・32〜63 を扱う)。手本は LLVM compiler-rt 7.0.1 の `lib/builtins/{ashldi3,lshrdi3}.c` (MIT/UIUC の時代の版、名前と役目だけ) と ABI の仕様。GPL の実装は使わない。**カーネルに libgcc は入れない** (ユーザー決定) |
| A6 | 境目: `kprintf` は整数とポインタの書式に限る。`va_list` は越境させず、最終の map で `os32_sqlite_snprintf` が像に無いことを確かめる |
| A7 | 製品の監査 (`tools/ppc/kboot_audit.py`): raw の .o と最終の ELF を照合し、既存の FP context の葉と明示した SQLite の入力の実行の範囲だけ FP を許す (最終の像の許可は map の入力の節の範囲まで絞る)。AltiVec の禁止は保つ |
| A8 | P2 の検査に足す: readelf で書き換えの前後の属性、nm と reloc で export と生きた import の許可表、map で 3 像の大きさ・provider の出所・libgcc と libc の不在。外部の double の呼び出しと許可の外の FP の負例。QEMU で SQL の REAL・変換と FP の guard の回帰 |
- P2 は本線の持ち物 (build/features/sqlite.mk・sqlite-proto.mk・gen_sqlite_imports.py) に加えて、`tools/ppc/kboot_audit.py` と `arch/ppc/` の .S・`lib/shift_di.c` を足す。出す前に本線 PM に知らせる。
