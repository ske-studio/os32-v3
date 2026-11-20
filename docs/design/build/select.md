# build/select.mk — カーネルのビルドを 1 系統にし、像の構成の軸を作る (BUILD_UNIFY_DESIGN)

> 状態: **現行 (2026-11-17)** — 出所: 機種依存の分離の作り直しの 2 段目 (ユーザー決定 2026-11-17「共通ソースが機種ヘッダを読む数 0・ビルド 1 系統・入口の骨格 1 本」)。Opus 5.5 のレビュー 2 回で確定。段 1a・1b・2 は実装済み、段 3 以降は未着手。§1 の実物は段 1a の前 (origin/main `75c7c6f69`) の姿。
ソース: `build/select.mk`、`build/images/`、`build/sources/`、`build/arch/`、`build/platform/`、`build/features/`、`build/targets/ppc-powermac.mk`

## 1. 実物 (origin/main `75c7c6f69`)

- x86-pc98: `C_SELECTED = C_COMMON C_ARCH C_PLATFORM C_FEATURES`、`C_KERNEL = filter(C_SELECTED, C_ORDER_LEGACY C_ARCH_NEW C_PLATFORM_NEW) C_PLATFORM_NEW C_DIAGNOSTICS` (`build/select.mk`)。そのあと `build/kernel-objects.mk` が `platform/pc98/tsc_math.c`・`tsc_clock.c`・`net/rx_work.c` を後ろに足す。`build_profile.py` の `C_KERNEL` は 376 本で、`tools/tests/fixtures/build_profile_x86.json` が並びを固定している。共通の場所の物は 164 本。
- PowerPC の製品の像は、この時点では独立の Makefile の手書きの一覧 (段 5 で `build/targets/ppc-powermac.mk` に畳んだ)。`KBOOT_LINK ?= 1` なので旗が無い時の像は link。

  | 像 | 旗 | 出力 (`build/out/` の下) | .S | .c |
  |---|---|---|---:|---:|
  | core | `KBOOT_LINK=0` | `ppc-kboot` | 9 | 63 |
  | neg1 | `KBOOT_LINK=0 NEGATIVE=1` | `ppc-kboot/neg1` | 9 | 63 (core と同じソース) |
  | link (既定) | — | `ppc-kboot/link` | 9 | 69 |
  | shell | `KBOOT_SHELL=1 KBOOT_LINK=0` | `ppc-kboot/shell` | 12 | 203 |
  | sh | `KBOOT_SH=1 KBOOT_LINK=0` | `ppc-kboot/sh` | 12 | 238 |
  | link-sh | `KBOOT_SH=1` | `ppc-kboot/link-sh` | 12 | 244 |

- 6 像のソース (.S 12 本・.c 254 本) は、条件 always 66・LINK 10・NOLINK 4・EXEC 143・NOEXEC 2・USERSHELL 35・CLI 2・USERSHELL かつ LINK 2・USERSHELL かつ LINK 無し 2 で過不足なく割り付けられる。5 種の並びを全部再現する 1 本の全順序が作れる (実測)。
- x86 の共通 164 本のうち PowerPC の像に入っているのは 115 本、入っていないのは 49 本 (ext2 7・fat 3・hostdrv 1・`serialfs_session.c`・gfx 8・init 5・kapi 4 (`kapi_generated.c`・`kapi_db`・`kapi_sys`・`kapi_osri`)・`gui.c`・`irq.c`・`irq_math.c`・`kaudit.c`・`kselftest_report.c`・module 4・`ktime.c`・`time_math.c`・`ver_collect.c`・`ver_format.c`・lib 4・mm 3)。
- 独立の Makefile を直に呼ぶ道具は 42 本、`KB_MUT_*` を使う物は 13、`KBOOT_*` を渡す物は 22。
- `ARCH=ppc PLATFORM=powermac` の select 側は compile だけの計測で、製品と別の集合を選ぶ。`FEATURE_*` 15 個は全部 x86-pc98 に固定 (`build/select.mk`)。`build/targets/x86-pc98.mk` は機能に 0 があると `kernel-compile` 以外を止める。

## 2. 到達点

1. ソースの持ち主の一覧は 1 か所。x86 も PowerPC もそこから選ぶ。
2. 像の構成は `IMAGE` の値 1 つで決まる。arch・platform と独立の軸。
3. `make ARCH=ppc PLATFORM=powermac IMAGE=<名>` が PowerPC の製品の像を組む。手書きの一覧と compile だけの計測は無い。

## 3. 像の構成の軸

### 3-1. 像と機能

`build/images/<名>.mk` が像の機能の値と順の式を決める。`build/select.mk` が最初に `include build/images/$(IMAGE).mk` する。

| `IMAGE` | 使う所 | `LINK` | `EXEC` | `USERSHELL` | `USB` | `CLI` | `PC98_PENDING` | 順の式 |
|---|---|---|---|---|---|---|---|---|
| `pc98` | x86-pc98 の既定 | 1 | 1 | 1 | 0 | 0 | 1 | legacy |
| `kboot-core` | `KBOOT_LINK=0` | 0 | 0 | 0 | 0 | 0 | 0 | kboot |
| `kboot-link` | 旗なし (最上位の `make ARCH=ppc PLATFORM=powermac` の既定) | 1 | 0 | 0 | 0 | 0 | 0 | kboot |
| `kboot-shell` | `KBOOT_SHELL=1 KBOOT_LINK=0` | 0 | 1 | 0 | 0 | 1 | 0 | kboot |
| `kboot-sh` | `KBOOT_SH=1 KBOOT_LINK=0` | 0 | 1 | 1 | 1 | 0 | 0 | kboot |
| `kboot-link-sh` | `KBOOT_SH=1` | 1 | 1 | 1 | 1 | 0 | 0 | kboot |

```make
# build/images/kboot-link-sh.mk
IMAGE_ORDER = kboot
FEATURE_LINK = 1
FEATURE_EXEC = 1
FEATURE_USERSHELL = 1
FEATURE_USB = 1
FEATURE_CLI = 0
FEATURE_PC98_PENDING = 0
```

像の機能の値は像のファイルだけが決める。コマンドラインで `FEATURE_LINK=0` のように上書きすると `select.mk` が止める (組み方を変える時は `IMAGE` を選ぶ)。PowerPC の像は名前 (`IMAGE=kboot-*`) でも旗 (`KBOOT_LINK`・`KBOOT_SH`・`KBOOT_SHELL`) でも選べる。対応は `build/images/ppc-flags.mk` が持ち、`select.mk` が ARCH=ppc の時に像のファイルより先に読む: 名前だけなら旗を名前から決め、旗だけなら像を旗から決め、両方をコマンドラインで与えて食い違えば止める (出力の置き場と中身が食い違わないように)。

- 像の機能は `IMAGE_FEATURES = LINK EXEC USERSHELL USB CLI LOOP PC98_PENDING` として、今の 15 個 (`FEATURES`) と別の一覧に持つ。値の検査 (0 か 1)・未知の `FEATURE_*` の検査は両方を見る。`build/targets/x86-pc98.mk` の「0 があれば止める」は今の 15 個だけを見る (下の `KNOWN_FEATURE_VARIABLES` の項)。
- 依存: `USERSHELL`・`CLI` は `EXEC` が要る。`USERSHELL` と `CLI` は同時に 1 にできない。`select.mk` の今の依存の検査と同じ書き方で足す。
- `IMAGE` の既定は `select.mk` の冒頭で決める: `IMAGE ?= $(if $(filter powermac,$(PLATFORM)),kboot-link,pc98)`。`build/images/$(IMAGE).mk` が無ければ `$(error unknown IMAGE ...)`。
- `KNOWN_FEATURE_VARIABLES` は今の 15 個のまま変えない (`build/targets/x86-pc98.mk` の「0 があれば止める」が読む。このファイルは `tools/tests/test_sep5_boot.py` が基点 `3ebbd208` とバイト単位で固定しているので触らない)。像の機能は `IMAGE_FEATURE_VARIABLES` に持ち、未知の `FEATURE_*` の検査は両方を見る。`kernel-objects.mk` の SELECTION_STAMP には `IMAGE=<名>` を足す。

### 3-2. 持ち主

共通の場所の物は `build/sources/common.mk` に機能ごとの組で宣言し、`C_COMMON` は選ばれた組の和にする (`C_SELECTED` の式は変えない)。

```make
# build/sources/common.mk
C_COMMON_CORE = <両 arch の全部の像に入る 18 本> lib/kstring_c.c
C_COMMON_LINK = kernel/dev/device_host.c net/link.c net/link_notify.c net/host_mailbox.c net/nic_rx.c net/rx_work.c
C_COMMON_EXEC = kernel/dev/device_fs.c <今 C_COMMON にあり PowerPC の exec の像に入る 93 本> drivers/base/dev.c input/hal/legacy_ring.c input/hal/legacy_mode.c kernel/port_service.c kernel/serial_diag.c
C_COMMON_USERSHELL = fs/serialfs/serialfs.c fs/serialfs/serialfs_persistent.c fs/serialfs/sfs_client.c fs/serialfs/sfs_proto.c lib/crc32.c
C_COMMON_USB = $(wildcard drivers/usb/usb_*.c drivers/usb/ohci_*.c drivers/usb/hid_*.c) drivers/usb/ehci_quiesce.c
C_COMMON_CLI = kernel/cli/resident_cli.c
C_COMMON_PC98_PENDING = <49 本>
C_COMMON = $(C_COMMON_CORE) $(foreach f,$(IMAGE_FEATURES),$(if $(filter 1,$(FEATURE_$(f))),$(C_COMMON_$(f))))
```

`C_COMMON_USB` の wildcard は今の `build/targets/ppc-powermac.mk` と同じ式で、`usb_module.c`・`usb_resident.c`・`usb_early_quiesce.c` を含むかは実装で今の一覧と突き合わせる (含むなら今と同じに)。

arch・platform の物は、その持ち主のファイルが機能ごとの組を宣言する。組み合わせの条件 (USERSHELL かつ LINK) は持ち主のファイルが `$(if ...)` で自分で書く。

```make
# build/platform/powermac.mk (kboot の像の分)
C_PLATFORM_CORE = platform/powermac/rh_observe.c ... drivers/net/gem/gem_reset.c ... lib/lz4.c platform/powermac/ppc_kernel.c
C_PLATFORM_LINK = platform/powermac/link_platform.c platform/powermac/nic_gem.c platform/powermac/link_log.c platform/powermac/link_diag.c
C_PLATFORM_NOLINK = platform/powermac/lan_wire.c platform/powermac/lan_terminal.c platform/powermac/terminal_input.c platform/powermac/host_unavailable.c
C_PLATFORM_EXEC = platform/powermac/root_boot.c ... kernel/console/fbcon.c gfx/gfx_console.c kernel/console/font_source_kcg.c drivers/video/of_fb/backend_of_fb.c drivers/video/of_fb/of_fb_v2.c kernel/dev/ram_ro.c fs/ramfs/ramfs.c fs/isofs/iso9660.c fs/isofs/iso9660_media.c init/boot_root.c
C_PLATFORM_NOEXEC = platform/powermac/kboot_console.c
C_PLATFORM_USERSHELL = platform/powermac/shell_boot.c platform/powermac/input_boot.c platform/powermac/input_decode.c platform/powermac/hostfs_boot.c input/keymaps/hid_jp.c $(if $(filter 1,$(FEATURE_LINK)),platform/powermac/hostfs_link_io.c platform/powermac/hostfs_link_peer.c,platform/powermac/hostfs_io.c platform/powermac/hostfs_peer.c)
C_PLATFORM_USB = platform/powermac/input_usb.c platform/powermac/input_keymap.c platform/powermac/usb_tree.c platform/powermac/usb_platform.c platform/powermac/usb_boot.c
C_PLATFORM_CLI = platform/powermac/cli_boot.c
# build/select.mk — 像の順の式が kboot の時だけ足す
C_PLATFORM += $(C_PLATFORM_CORE) $(foreach f,$(IMAGE_FEATURES),$(if $(filter 1,$(FEATURE_$(f))),$(C_PLATFORM_$(f)),$(C_PLATFORM_NO$(f))))
C_ARCH     += $(C_ARCH_CORE)     $(foreach f,$(IMAGE_FEATURES),$(if $(filter 1,$(FEATURE_$(f))),$(C_ARCH_$(f)),$(C_ARCH_NO$(f))))
GAS_SELECTED = $(GAS_ARCH_CORE)  $(foreach f,$(IMAGE_FEATURES),$(if $(filter 1,$(FEATURE_$(f))),$(GAS_ARCH_$(f))))
```

持ち主を決めた物:

| ソース | x86 | PowerPC | 理由 |
|---|---|---|---|
| `input/keymaps/hid_jp.c` | `C_PLATFORM` (pc98.mk、今のまま) | `C_PLATFORM_USERSHELL` (powermac.mk) | 今の x86 の持ち主が platform。置き場と持ち主の食い違いは置き場の段で直す |
| `kernel/time/cpu_calibrate.c` | `C_ARCH` (x86-sources.mk、今のまま) | `C_ARCH_EXEC` (ppc-sources.mk) | 同上 (今の持ち主が arch) |
| `net/nic.c` | `C_COMMON_CORE` (pc98-devices.mk から外す) | 同じ | 両 arch の全部の像に入る |
| `net/link.c`・`link_notify.c`・`host_mailbox.c`・`nic_rx.c`、`drivers/base/dev.c`、`input/hal/legacy_*`、`kernel/port_service.c`・`serial_diag.c` | `C_COMMON_LINK`・`C_COMMON_EXEC` (pc98-devices.mk から外す) | 同じ | PowerPC の像も使う共通の物 |
| `net/rx_work.c` | `C_COMMON_LINK`。`kernel-objects.mk` の足しは消し、並びは §4 の末尾の一覧で保つ | 同じ | |
| `platform/pc98/tsc_math.c`・`tsc_clock.c` | `C_PLATFORM` (pc98.mk)。`kernel-objects.mk` の足しは消す | — | rx_work と同じ末尾の一覧で並びを保つ |
| `lib/kstring_c.c` | `C_COMMON_CORE` に在り、x86 は順の一覧に無いので今までどおり落ちる (`KSTRING_C_SRC` の仕組みは変えない) | kboot の順の一覧に在る | 今の振る舞いを保つ |
| `kapi/kapi_generated.c` | `C_COMMON_PC98_PENDING` | 入らない (生成した物を `KB_EXTRA` で組む、今のまま) | |
| `platform/none/legacy_guest.c` | `FEATURE_V86=0` かつ `FEATURE_EXEC=1` の時 (`select.mk` に EXEC の条件を足す) | 同じ式で shell・sh・link-sh にだけ入る | core・link には今も入っていない |
| `kernel/cli/resident_cli.c`、USB 21 本 | `CLI=0`・`USB=0` なので選ばれない | `C_COMMON_CLI`・`C_COMMON_USB` | x86 は USB を部品として組む |

x86-pc98 の既定の `C_KERNEL` 376 本は並びごと変えない (`build_profile_x86.json` を 1 字も変えない)。`FEATURE_PC98_DEVICES=0` などを付けた問い合わせでは、移した 10 本が残る (機種の装置ではないので、それが正しい)。

## 4. 順の式

```make
# build/sources/order.mk に足す
C_ORDER_TAIL = platform/pc98/tsc_math.c platform/pc98/tsc_clock.c net/rx_work.c
# build/select.mk
ifeq ($(IMAGE_ORDER),kboot)
# PowerPC の像: 並びは所有者集合の順。arch/ppc/kernel.ld が入口を .text.entry で先頭に置くので、
# 並びに意味は無い。順の一覧は持たない。
C_KERNEL = $(C_SELECTED)
GAS_KERNEL = $(GAS_SELECTED)
else
C_ORDER = $(C_ORDER_LEGACY) $(C_ARCH_NEW) $(C_PLATFORM_NEW)
C_KERNEL = $(filter $(C_SELECTED),$(C_ORDER)) $(C_PLATFORM_NEW) $(C_DIAGNOSTICS) $(filter $(C_SELECTED),$(C_ORDER_TAIL))
endif
```

legacy の式は今の `select.mk` に末尾の一覧を足しただけ (x86 の並びは `… C_PLATFORM_NEW C_DIAGNOSTICS tsc_math tsc_clock rx_work` で今と同じ)。「順に無いソースで止める」検査は kboot の像だけに入れる (x86 は `lib/kstring_c.c` が今も黙って落ちる)。

## 5. 段

### 段 1a — PowerPC の一覧を所有者集合から引く

- PowerPC の像の規則 (`build/targets/ppc-powermac.mk`) は一覧 (`KB_ASM`・`KB_ARCH`・`KB_PLATFORM`・`KB_COMMON`・`KB_GEM`・`KB_C` の足し引き) を持たず、`select.mk` の `GAS_KERNEL`・`C_KERNEL` を引く。旗から `IMAGE` を決めるのは `build/images/ppc-flags.mk`。**変数・既定 (`KBOOT_LINK ?= 1`)・出力の置き場は変えない**。

  ```make
  # build/images/ppc-flags.mk
  KB_IMAGE := $(if $(filter 1,$(KBOOT_SH)),$(if $(filter 1,$(KBOOT_LINK)),kboot-link-sh,kboot-sh),$(if $(filter 1,$(KBOOT_SHELL)),kboot-shell,$(if $(filter 1,$(KBOOT_LINK)),kboot-link,kboot-core)))
  override IMAGE := $(KB_IMAGE)
  # build/targets/ppc-powermac.mk
  KB_ASM := $(GAS_KERNEL)
  KB_C := $(C_KERNEL)
  ```

  `KB_INC_EXTRA` の条件 (`-Inet` は LINK の時、exec の `-I` の群は EXEC の時)・`KB_EXTRA`・TU ごとの旗・KAPI の生成・`memmap_layout.h`・RAM root (`RAM_ROOT_C` は `build/ram-root.mk` が持つので、その分は `C_PLATFORM_EXEC` に `$(RAM_ROOT_C)` として載せる)・`kboot_audit`・`KB_MUT_*`・`kb-user` は今の規則のまま残す (条件は `KB_EXEC`・`KBOOT_*` のままでよい)。
- 確かめ:
  - x86: `build_profile_x86.json` が不変 (`build-selection-host`)。手元で `tools/build_compare.py` で kernel.bin と部品が一致。
  - PowerPC: 6 像の `KB_ASM`・`KB_C` の並びを `tools/tests/fixtures/build_profile_ppc.json` に固定し、`test_build_selection.py` が `make ARCH=ppc PLATFORM=powermac` から引いて比べる (段の前の木で取った値を fixture にする)。手元で 6 像の `PPCKERN.ELF` の sha256 が前後で同じ。今の `test_build_selection.py` がそのまま通る)。
  - `tools/tests/test_remote_x9.py` (`build/targets/ppc-powermac.mk` の本文に `kernel/console/con_sink.c` があること) は、`select.mk` から引いた `kboot-link-sh` の `C_KERNEL` に在ることに書き換える。
  - CI: check (ppc-compile・ppc-qemu)・build。
- 触るファイル: `build/images/*.mk` (7)、`build/sources/common.mk`・`order.mk`、`build/select.mk`、`build/features/pc98-devices.mk`、`build/platform/pc98.mk`・`powermac.mk`、`build/arch/ppc-sources.mk`、`build/kernel-objects.mk`、`build/targets/ppc-powermac.mk`、`tools/tests/test_build_selection.py`・`test_remote_x9.py`・`fixtures/build_profile_ppc.json`、`tools/check_arm_compile.py` は触らない (`KSTRING_C_SRC` の前提は同じ。`config.mk` だけの問い合わせの対象集合には、`kernel-objects.mk` が後から足していた `tsc_math.c`・`tsc_clock.c`・`rx_work.c` の 3 本が増え、実際の `C_KERNEL` と同じになる)。`build/targets/ppc-powermac.mk` の .o と `PPCKERN.ELF` は、一覧と順を持つ .mk (`KB_SELECT_MK`) を前提に持つ (一覧を変えたら組み直される)。

### 段 1b — compile だけの計測を消し、`make ARCH=ppc` を製品の入口にする

- `C_COMMON_COMPILE_ONLY_PPC` は PPC 製品外の共通ソースを `common-compile` で検査する。
- compile だけの計測は持たない。共通のソースが機種のヘッダを読まないことは層の検査 `tools/check_layers.py` が、PowerPC でも組めることは `make ppc-ci-image` の `common-compile` が見る。
- 最上位の `make ARCH=ppc PLATFORM=powermac [IMAGE=kboot-*] [旗] [kernel]` が製品の入口。最上位の `Makefile` が `build/config.mk` (→ `select.mk`) と `build/targets/ppc-powermac.mk` を 1 回ずつ読む (別の Makefile への sub-make は挟まない)。
- root の像に入るユーザーのバイナリ (`kb-user`) は、この make の `build/ppc-user.mk` の置き場 `PPC_USER_OUT` が像の置き場 `KB_SH_OUT` と同じならそのまま組む。違う時 (link の像は置き場と profile が別) は、置き場と profile を替えて同じ `Makefile` を呼ぶ。
- 最上位の ARCH=ppc の `kernel-compile` は拒否する (像を組むのは `kernel` の旗と出力だけ。`build/targets/ppc-powermac.mk` は `build/kernel-objects.mk` を読まない)。x86 の式から、定義する所が無くなった `C_PLATFORM_NEW` を外した。
- 確かめ: `test_build_selection.py` (最上位の ARCH=ppc の `C_KERNEL`・`GAS_KERNEL` が `kboot-link` の固定した並びと同じ、ARCH=ppc に `IMAGE=pc98` は拒否)、PowerPC の 6 像の `.o` が前と同じ、CI の ppc-compile・ppc-qemu。

### 段 2 以降

| 段 | 中身 | x86 の確かめ | PowerPC の確かめ |
|---|---|---|---|
| 2 (済) | `exec_*.h` 16 組を arch の軸から外す。フックの 14 組は `exec/exec_<名>.h` と `exec/absent/exec_<名>.c`、CPU・配置の 2 組は契約の固定名 `arch_exec_cache.h`・`arch_exec_kapi.h`。設計は [exec/exec_hooks.md](../exec/exec_hooks.md) | 機械語は変わらない → `tools/build_compare.py` で全部一致 | `.o` が置き場の名前を除いて前と一致、ppc-qemu |
| 3 (済) | 順の一覧を 1 本に: PowerPC の像は順の一覧を持たず、所有者集合の順で並べる (`C_ORDER_KBOOT`・`GAS_ORDER_KBOOT` を削除)。順の一覧は x86 の `C_ORDER_LEGACY` だけ | 不変 | 配置が変わる → PowerPC の検査 (QEMU を含む) の全部 |
| 4 (済) | `-D` の名前の差し替え 11 件を口へ: `kprintf` はカーネルの診断の出力で行き先は機種が決め、端末に必ず出す物は `console_printf` (PowerMac の exec を持つ像は `platform/powermac/log_printf.c` が `kprintf` を作る)。コンソールの鏡・UTF-8 の道・fd 1 の出力先は機種ごとの固定名 `platform_console.h`。設計は [lib/kprintf.md](../lib/kprintf.md) | 機械語は名前と契約の指紋だけが変わる (`tools/build_compare.py`) | 配置が変わる → PowerPC の検査 (QEMU を含む) |
| 5 (済) | 像の規則を `build/targets/ppc-powermac.mk` に畳み、独立の Makefile を削除。道具は `make ARCH=ppc PLATFORM=powermac` に旗か `IMAGE=<名>` を付けて呼ぶ。試験が規則・旗の対応の写しを差し替える口は `TARGET_MK`・`PPC_FLAGS_MK`。出力の置き場 `build/out/ppc-kboot/<像>/` は変えない | 不変 | PowerPC の `.o` が前と同じ (workflow `sep`)、ppc-compile・ppc-qemu |
| 後 | `PC98_PENDING` の 49 本を include を直した順に本来の機能へ。入口の統合で `#if KBOOT_*` を機能の値に | | |

## 6. 未決

ユーザー判断が要る物は無い。特務 PM が決めた物: 像の名前、USB を別の機能にすること、未移行の 49 本を 1 つの組で持つこと、段 1a で入口・出力・最上位の ARCH=ppc の振る舞いを保つこと。
