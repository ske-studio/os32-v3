# exec/exec_hooks.h — exec のフック 16 組を arch の軸から外す (EXEC_HOOKS_DESIGN)

> 状態: **現行 (2026-11-17)** — 出所: 機種依存の分離の作り直し、ビルドの一本化 (docs/design/build/select.md) の段 2。Opus 5.5 のレビュー 2 回で確定、実装済み。§1 の実物は移す前 (`arch/x86/exec_*.h`・`arch/ppc/exec_*.[ch]`) の姿。
ソース: `exec/exec_hooks.h`、`exec/exec_<名>.h` 14 本、`exec/absent/exec_<名>.c` 14 本、`build/sources/hooks.mk`、`arch/<arch>/arch_exec_cache.h`・`arch_exec_kapi.h`

## 1. 実物

`exec/` の共通のソースが `#include "exec_<名>.h"` で読むヘッダが、`arch/x86/` と `arch/ppc/` に同じ名前で 16 組ある (`-Iarch/$(ARCH)` で引く)。x86 のヘッダは実体を inline で呼び、PowerPC のヘッダは宣言だけで、`arch/ppc/exec_<名>.c` が空の実体を返す。層の検査では両方の像で R1 (共通 → arch の専用ヘッダ) に数えられている (各 20 件。`exec_r1.h` は `#ifdef OS32_R1_FIXTURE` の中だけで読まれ、R1 の像は層の検査の根に入っていないので数えられていない)。

| 組 | 読む側 | x86 (inline) が呼ぶ物 | PowerPC の実体 | 分類 |
|---|---|---|---|---|
| `exec_cache` | `exec/exec.c`、`arch/ppc/syscall_code.c` | `#define arch_exec_publish(dst, bytes) ((void)0)` | 命令キャッシュの flush (`arch/ppc/exec_cache.c`) | **CPU** |
| `exec_kapi` | `exec/exec.c` | `exec_kapi_table()` = `KAPI_ADDR` | 静的な表 `ppc_kapi_table`、`ppc_kapi_wait_once` | **CPU・配置** |
| `exec_gfx` | `exec/exec.c` | `gfx2_owner_release`・`gfx_return_emergency` | 0 を返す | 機能 |
| `exec_gfx_poll` | `exec/exec_schedule.inc`・`exec_syscall.inc` | `geforce_fg_due`・`geforce_foreground_poll` | false・何もしない | 機能 |
| `exec_surface` | `exec/appslot.c`・`exec/lease.c` | `ledger_surfaces`・`gfx_sf_backend`・`v1_bridge_*`・`gfx2_map_audit`・`gfx_policy_reeval`・`gfx2_head_restore_v1` | 空 | 機能 |
| `exec_gui` | `exec/exec.c` | `gui_get_pump`・`gui_owner_exit` | 0・何もしない | 機能 |
| `exec_font_guard` | `exec/exec.c`・`exec/shell_service.c` | `font_guard_active`・`font_guard_fault`・`font_guard_run` (R1 の像では `font_guard_fixture`) | 0・`call(ctx)` を直に呼ぶ | 機能 |
| `exec_font_lease` | `exec/exec.c`・`exec/shell_service.c` | `font_lease_shell_*`・`font_lease_close_*` | `OS32_ERR_NOTFOUND` | 機能 |
| `exec_ime` | `exec/exec.c` | `ime_owner_exit` | 何もしない | 機能 |
| `exec_snd` | `exec/exec.c` | `snd_owner_exit` | 何もしない | 機能 |
| `exec_shlib` | `exec/exec.c`・`exec/access_walk.c` | `shlib_addrspace_attach` ほか 4 本 | 0 | 機能 |
| `exec_legacy` | `exec/exec.c` | `legacy_pinned` ほか 6 本 | 0 (`platform/none/legacy_guest.c` と役が重なる) | 機能 |
| `exec_r1` | `exec/exec.c`・`exec/lease.c` | R1 の像 (`OS32_R1_FIXTURE`) の時だけ `r1_fixture_*` | 0 | 機能 |
| `exec_reclaim` | `exec/exec.c` | `pcm_reclaim`・`host_owner_exit` | 何もしない・`platform_host_owner_exit` | 機能 (2 本で事情が違う) |
| `exec_kapi_db` | `exec/exec.c` | `db_cleanup_owned` | 何もしない | 機能 |
| `exec_serial_diag` | `exec/exec.c` | `serial_puts_polled` | 何もしない | 機能 |

PowerPC の空の実体のうち、提供者 (`kernel/serial_diag.c`・`kernel/console/font_source.c`・`font_guard.c`・`gfx/v2/*`・`gfx/gfx_policy.c`) が PowerPC の exec の像に入っている物がある。今は「入っているが、フックは空」。

## 2. 到達点と、この段でしないこと

- フックのヘッダと空の実体は共通の場所 (`exec/`) に置き、どちらの実体を使うかは像の構成が決める。`arch/<a>/` に残るのは CPU・配置に依る 2 組だけで、契約の実装の固定名にする。
- **この段は移すだけで、どの像の振る舞いも変えない**。x86 は生成物が不変 (`tools/build_compare.py`)、PowerPC は `.o` の中身が不変 (置き場だけ変わる)。
- この段でしないこと (次の段): PowerPC で提供者が像に在るフックを本物に切り替える、`exec_legacy` を `platform/none` の提供者に一本化する、`exec_reclaim` の host の名前の違いを揃える。どれも PowerPC の振る舞いが変わるので、ppc-qemu の受入と一緒に 1 つずつ行う。

## 3. 形

### 3-1. フックのヘッダ (14 組)

`exec/exec_<名>.h` に 1 本。読む側は全部 `exec/` の中なので、`#include "exec_<名>.h"` の行は変えずに `exec/` の物が先に見つかる。

```c
/* exec/exec_gui.h */
#ifndef OS32_EXEC_GUI_H
#define OS32_EXEC_GUI_H
#include "exec_hooks.h"
#if OS32_HOOK_GUI
#include "gui.h"
typedef void (*ExecGuiPump)(void);
static inline ExecGuiPump exec_gui_pump(void) { return (ExecGuiPump)gui_get_pump(); }
static inline void exec_gui_owner_exit(int owner, int kind) { gui_owner_exit(owner, kind); }
#else
typedef void (*ExecGuiPump)(void);
ExecGuiPump exec_gui_pump(void);
void exec_gui_owner_exit(int owner, int kind);
#endif
#endif
```

`#if` の側は移す前の x86 のヘッダの中身、`#else` の側は移す前の PowerPC のヘッダの中身を、字句を変えずにそのまま置く (型の宣言が両方に要る時も、外に出さず両方の側に 1 回ずつ置く — 前後の字句の一致で確かめられるように)。`arch/README.md` が禁じているのは CPU 名での分岐で、これは像の構成の値での分岐。x86 のヘッダが 2 段上を指して読んでいる 3 本 (`gfx/v2/gfx2_exec.h`・`gfx/v2/gfx2_int.h`・`drivers/video/geforce/geforce_module.h`) は、置き場が 1 段浅くなるので 1 段上を指す形に直す (`__FILE__` は使っていないので機械語は変わらない)。

`exec/exec_hooks.h` は、マクロを渡さない組み方の既定を 1 つの値で持つ:

```c
/* 製品の像は -DOS32_HOOK_<名>=0/1 を 14 個とも渡す (build/sources/hooks.mk)。
 * 渡さない組み方 (host の試験など) は OS32_HOOK_DEFAULT (既定 1 = 全部のフックが在る、今の x86 と同じ)。
 * PowerPC 側のソースを単体で組む試験は -DOS32_HOOK_DEFAULT=0 を 1 つ渡す。 */
#ifndef OS32_HOOK_DEFAULT
#define OS32_HOOK_DEFAULT 1
#endif
#ifndef OS32_HOOK_GUI
#define OS32_HOOK_GUI OS32_HOOK_DEFAULT
#endif
... (14 個)
```

### 3-2. 無い時の実体

`arch/ppc/exec_<名>.c` を `exec/absent/exec_<名>.c` へ `git mv` し、先頭に `#define OS32_HOOK_<名> 0` を 1 行足す (無い時の実体は、どう組まれても自分のヘッダの宣言の側を見る — 単体で組む試験が `-D` を忘れても inline の定義とぶつからない)。ほかの中身は変えない。`exec_reclaim` は `exec_pcm_reclaim` (空) と `exec_host_owner_exit` (`platform_host_owner_exit` を呼ぶ) を今のまま 1 本に置く。

### 3-3. 像が選ぶ

```make
# build/sources/hooks.mk — フックの名前と、在るかどうかを決める値。select.mk が読む。
# 今は全部「未移行の物が入っている像か」に連動する (x86-pc98 は 1、kboot の像は 0 = 今の振る舞い)。
# フックの提供者を PowerPC の像に入れた段で、その行だけ本来の機能の値に替える。
EXEC_HOOKS = gfx gfx_poll surface gui font_guard font_lease ime snd shlib legacy r1 reclaim kapi_db serial_diag
$(foreach h,$(EXEC_HOOKS),$(eval HOOK_$(h) = $$(FEATURE_PC98_PENDING)))
hook_macro = OS32_HOOK_$(shell echo $(1) | tr 'a-z' 'A-Z')
HOOK_DEFS := $(foreach h,$(EXEC_HOOKS),-D$(call hook_macro,$(h))=$(HOOK_$(h)))
C_COMMON_HOOKS_ABSENT = $(foreach h,$(EXEC_HOOKS),$(if $(filter 0,$(HOOK_$(h))),exec/absent/exec_$(h).c))
```

- `select.mk` が像のファイルの後で `include build/sources/hooks.mk` する (`HOOK_DEFS` は `:=` なので、機能の値が決まった後に読む)。`build/targets/ppc-powermac.mk` の `KB_FLAGS :=` は `select.mk` の include より後にあるので `$(HOOK_DEFS)` を足せる。`KB_SELECT_MK` に `build/sources/hooks.mk` を足す (値を替えたら組み直される)。
- `select.mk` は `C_COMMON_EXEC_HOOKS = $(if $(filter 1,$(FEATURE_EXEC)),$(C_COMMON_HOOKS_ABSENT))` を作って `C_SELECTED` に足す。x86-pc98 は全部 1 なので空、kboot の core・link は exec を持たないので空。
- `select.mk` で `KERNEL_CFLAGS_EXTRA += $(HOOK_DEFS)` (TICK1K と同じ足し方で、arch を問わない。`build/targets/x86-pc98.mk` は基点と固定されているので触らない)。R1 の像と kdebug の部品も `KERNEL_CFLAGS_EXTRA` を含む旗で組むので受け取る。PowerPC の像は `build/targets/ppc-powermac.mk` の `KB_FLAGS` に `$(HOOK_DEFS)`。
- 順: PowerPC の像は順の一覧を持たない (所有者集合の順で並ぶ。設計は `docs/design/build/select.md` §4)。

### 3-4. CPU・配置の 2 組

- `arch/x86/exec_cache.h`・`arch/ppc/exec_cache.h` → `arch_exec_cache.h` (契約の固定名)。読む側 (`exec/exec.c`、`arch/ppc/syscall_code.c`・`exec_cache.c`) の include を替える。x86 は `arch_exec_publish` をマクロで与えるので、`include/exec_arch.h` には**宣言を足さず**、註だけ足す (「`arch_exec_cache.h` が `arch_exec_publish(dst, bytes)` を関数かマクロで与える: 書いた命令を実行に公開する」)。`exec/exec.c` の include は今の位置 (`exec_arch.h` の後) のまま。
- `arch/x86/exec_kapi.h`・`arch/ppc/exec_kapi.h` → `arch_exec_kapi.h`。`exec_kapi_table()` の名前は変えない (読む側の変更を include の 1 行に留める)。契約の註は `arch_exec_kapi.h` 自身に書く (x86 が `static inline` なので `include/` に extern の宣言は置けない)。`ppc_kapi_wait_once` は契約ではないので、宣言を `arch/ppc/exec_kapi.c` の定義の直前へ移す (読むのは生成された KAPI の wrapper の `extern` だけ)。`arch/README.md` の契約の表に 2 行足す。
- `arch/ppc/exec_selftest.c`・`exec_context.c`・`exec_entry.c` と `arch_exec_*.h` は既に契約の実装なので触らない。

## 4. 確かめ

| 対象 | 方法 |
|---|---|
| x86 の生成物 | `tools/build_compare.py <基点> HEAD` で全部一致 (inline の中身と展開先が同じ。`-D` が増えるだけ) |
| PowerPC の 6 像 | 移した 14 本と残り全部の `.o` の sha256 が、置き場の名前を読み替えて前と一致。核・link・neg1 は `PPCKERN.ELF` も一致 (exec の像は RAM root の ISO が作るたびに変わるので `.o` で見る) |
| 層の検査 | 両方の像で R1 が 20 件ずつ減る見込み (`exec/` から arch へ向く辺は 0 になる)。x86 では `exec/exec_gfx_poll.h → drivers/video/geforce/geforce_module.h`・`exec/exec_snd.h → drivers/sound/fm/snd_engine.h` が R3 に現れる (今は arch のヘッダの陰で数えていなかった辺)。`--write-baseline` で基準を下げ、差分を報告に書く |
| 選択の試験 | `test_build_selection.py` の fixture (x86 は不変、PowerPC は 14 本の置き場の名前だけ) |
| 追従 (covers) | `arch/x86/exec_*.h`・`arch/ppc/exec_*.[ch]` を `covers` に持つ検査 (`tools/checks/exec-image-host.py`・`ppc-exec-*.py`・`ppc-kapi-table.py`・`ppc-syscall-code.py` など)。`run.py --lint` の missing input で洗う |
| 追従 (PowerPC 側を単体で組む試験) | `tools/ppc/test_kapi_table.py` (`exec.c` の写しを cross で組む) に `-DOS32_HOOK_DEFAULT=0` を渡す。`tools/tests/test_ppc_rshell_link.py` (移した `exec_reclaim.c` を組む) は .c 自身が 0 を定義するのでパスの追従だけ。`tools/tests/test_ppc_terminal_work.py` はフックのヘッダを読まないので、include の名前 (`arch_exec_kapi.h`) とパスの追従だけ |
| 追従 (arch のヘッダ・.c をパスで読む試験) | `tools/tests/test_geforce.py`・`test_gfx2_i3.py`・`test_gfx_policy_triggers.py`・`test_gf_cui.py`・`test_build_selection.py`・`r1_fixture_host.c`・`font_guard_host.c`・`exec_source.py`。本文から切り出す箇所と変異の目印は lint では捕まらないので、フックのヘッダの名前で `git grep` して洗い、`--mutate` で回す |

## 5. ファイル

| 変更 | ファイル |
|---|---|
| 新規 | `exec/exec_hooks.h`、`exec/exec_<名>.h` 14 本 (x86 と PowerPC のヘッダを 1 本に)、`build/sources/hooks.mk` |
| 移動 | `arch/ppc/exec_<名>.c` 14 本 → `exec/absent/exec_<名>.c` |
| 改名 | `arch/{x86,ppc}/exec_cache.h` → `arch_exec_cache.h`、`arch/{x86,ppc}/exec_kapi.h` → `arch_exec_kapi.h` |
| 削除 | `arch/x86/exec_<名>.h` 14 本、`arch/ppc/exec_<名>.h` 14 本 |
| 変更 | `build/select.mk`・`build/sources/order.mk`・`build/arch/ppc-sources.mk`・`build/targets/ppc-powermac.mk`、`exec/exec.c` (include 2 行)、`arch/ppc/syscall_code.c`・`exec_cache.c`・`exec_kapi.c` (include)、`include/exec_arch.h`、`tools/layers_baseline.json`、`tools/tests/fixtures/build_profile_ppc.json`、検査の `covers`、`arch/README.md`、`docs/design/build/select.md` (段 2 の行) |

## 6. 未決

ユーザー判断が要る物は無い。特務 PM が決めた物: `#if OS32_HOOK_*` で 1 本のヘッダに並べる形 (x86 の機械語を変えないため)、14 個のフックを今は 1 つの値 (`FEATURE_PC98_PENDING`) に連動させること、マクロを渡さない組み方の既定を `OS32_HOOK_DEFAULT` (既定 1) で持つこと、段 1b の後に行うこと。

## 7. 実装の時に確かめたこと

- x86: `tools/build_compare.py` で 1227 個の生成物が全部一致。選択の fixture で変わったのは `CFLAGS_BASE` の欄だけ (`-DOS32_HOOK_<名>=1` が 14 個増えた)。
- PowerPC: 6 像の 952 本の `.o` が、移した 14 本の置き場の名前を読み替えて前と一致 (RAM root の ISO の 3 本を除く)。
- 層の検査: 共通 → arch の専用ヘッダ (R1) は両方の像で 25 件から 5 件に減った。x86 では `exec/exec_gfx_poll.h → drivers/video/geforce/geforce_module.h`・`exec/exec_snd.h → drivers/sound/fm/snd_engine.h` の 2 件が R3 に現れた (arch のヘッダの陰で数えていなかった辺)。辺の合計は x86-pc98 が 118 → 100、ppc-powermac が 74 → 54。
- `KERNEL_CFLAGS_EXTRA` をコマンドラインで与える組み方 (`tools/tests/test_nofpu.py`) では `HOOK_DEFS` が落ち、ヘッダの既定 (在る) になる。x86 のフックを 0 にする段が来たら `HOOK_DEFS` は `KERNEL_CFLAGS` に直に足す。
