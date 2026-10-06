<!-- 生成物: tools/gen_tests_inventory.py が build/*.mk と試験スクリプトから書き出す。
     表を手で直さない。手書きは manual 区間 (下の HTML コメントの対) の中だけで、
     生成器はそこを読み戻して保つ。ずれは `make check-tests-inventory` が検出する。 -->

# 試験一覧

OS32 の自動試験の**正典**。日付を持たない (快照ではないので古くならない)。表は
[`tools/gen_tests_inventory.py`](../tools/gen_tests_inventory.py) が
[`build/checks.d/`](../build/checks.d/) / [`build/sdk.mk`](../build/sdk.mk) と各試験スクリプトから生成する。

```
python3 tools/gen_tests_inventory.py --write    # 表を更新する
make check-tests-inventory                      # 表が古くないか検査する (make check の中)
```

判断 (重複・弱さ・穴・改善案) は生成できないので `<!-- manual:… -->` の区間に手で書く。
2026-09-14 の快照 [`docs/archive/TEST_INVENTORY_2026-09-14.md`](archive/TEST_INVENTORY_2026-09-14.md)
の調査結果のうち、生成できない部分はこの文書の manual 区間へ移してある。

## 0. 読み方

<!-- manual:intro -->
### 表が言えること / 言えないこと

表は `build/*.mk` と試験スクリプトから**機械で読める事実だけ**を出す。次の 3 つは
生成できないので、この文書では列に無い。

- **件数** (表明の数)。数え方が種類ごとに違う — C は `check()` / `CHECK()` の呼び出し数、
  Rust は `#[test]` の数、Python は `def test*` または `CASES` の長さ。合算に意味が無い。
- **実行時間**。**1 本も計測していない**。ソースから読める重さの根拠だけは分かっていて、
  `check-cfg-host` / `check-db-v50-host` / `check-install-recover-host` /
  `check-sqlite-groups-host` の 4 本は SQLite の amalgamation
  (`lib/sqlite3/sqlite3.c`、**267,757 行**) を**それぞれ独立に**毎回コンパイルする。
- **試験の種類**。5 つある — 静的整合性 / ホスト模型 / 実物ソースを `#include` /
  ゲスト観測 / 起動時自己試験。「実物ソースを `#include`」は、出荷するソースをそのまま
  ホストで走らせる形 (模型ではない)。表の「対象ソース」列が埋まっているものはだいたいこれ。

### 表の読み落としやすいところ

- **`make check` は単独では走らない**。`check-manifests` は `make all` の成果物を見る、
  `check-privileged` は `userland/**/*.o` が無いと exit 2
  ([`tools/check_privileged.py`](../tools/check_privileged.py))。
- **対象ソースが空欄でも試験が無いわけではない**。`tools/check_*.py` 系はツリー全体を
  走査するので特定のソースを持たない。Rust の 4 本 (`check-term-model` /
  `check-term-render` / `check-t5a-host` / `check-gui-host`) と `check-gshell-host` は
  `#[path]` 取り込みや `integration.py` の実行時走査なので、静的には拾えない。
- **`check-privileged` は構造上落ちない**。findings があっても `--strict` を付けなければ
  `return 0`。表の ○ は「`check` の列にある」であって「門である」ではない。
- **`tools/tests/paging_rebuild_host.c` (5 モード) はどの自動ゲートからも到達しない**。
  [`test_paging_bounds.py`](../tools/tests/test_paging_bounds.py) は `--rebuild` を
  付けたときだけこのハーネスを選ぶが、`check-memory-host` は引数なしで呼ぶ。
- **`apps/` と `game/` はサブモジュール**で、切り出した worktree では空のことがある。
  それらの中のホスト試験はこの表に出ない (`make external` 側)。
<!-- /manual:intro -->

## 1. 名前の対応規則

表の「記録」列 (`tools/tests/*_tdd.md`) は、ターゲット名から機械的に決める。3 つの規則
すべてを当てて、当たったものを**全部**並べる。どれも当たらなければ空欄 —
**記録が無いことも事実**なので埋めない。

| 規則 | 当て方 |
|---|---|
| 1 | 実行スクリプト冒頭 40 行 (docstring / コメント) が名指しする `tools/tests/<x>_tdd.md` |
| 2 | `build/*.mk` のターゲット直前のコメント塊が名指しする `<x>_tdd.md` |
| 3 | 語幹の一致 — `test_<stem>.py` / `<stem>_host.c` / `check-<stem>` に対する `tools/tests/<stem>_tdd.md` |
| 4 | 「票」列は、当たった `*_tdd.md` の冒頭 16 行の「票」の行から `docs/**.md` のリンク > バッククォートの `.md` > 「票 B8」の記号、の順で取る |

1 つのターゲットが複数のスクリプトを回すとき (`check-memory-host` `check-tools-host`
`check-vfs-mount-dev-host` など) は、当たった記録を**全部**並べる。

「対象ソース」列は 2 通りで拾う。(a) ハーネス C が `#include "../../…"` している
**出荷するソース**、(b) 実行スクリプトの module 直下の代入 (`SRC` / `HOST_SRC` /
`TARGET_SRCS` など) にある `ROOT / "…"`。どちらにも現れないものは空欄になる
(Rust は `--manifest-path` を代わりに出す)。

## 2. `make check` の列 (138 ターゲット)

`build/checks.d/*.mk` の登録から集めた `CHECK_PAR_TARGETS` (`check-par` の依存) が
正典。この表はその列をそのまま展開したもの。コマンド列は `make check` (変異込み) の形。

| # | ターゲット | コマンド | 対象ソース | 記録 (RED→GREEN) | 票 | CI |
|---|---|---|---|---|---|---|
| 1 | `check-kapi-bounds-host` | `python3 -B tools/tests/test_kapi_bounds.py --mutate`<br>`python3 -B tools/tests/test_gfx_bounds.py --mutate` | — | [`tools/tests/kapi_bounds_tdd.md`](../tools/tests/kapi_bounds_tdd.md) | [`docs/tasks/v3/TASK_T2D_T2H.md`](tasks/v3/TASK_T2D_T2H.md) | × |
| 2 | `check-shlib-high-host` | `python3 -B tools/tests/test_shlib_high.py --mutate` | — | — | — | × |
| 3 | `check-bootinfo-host` | `python3 -B tools/tests/test_bootinfo.py --target --mutate` | `kernel/bootinfo_check.c`<br>`kernel/bootinfo.c`<br>`drivers/ide.c`<br>`boot/bootinfo.inc` | [`tools/tests/bootinfo_tdd.md`](../tools/tests/bootinfo_tdd.md) | [`docs/archive/realhw_v21/TASK_HDD_INSTALL.md`](archive/realhw_v21/TASK_HDD_INSTALL.md) | × |
| 4 | `check-hdd-stage1-host` | `python3 -B tools/tests/test_hdd_stage1.py --target --mutate` | `drivers/pc98pt.c`<br>`drivers/ide_addr.c`<br>`fs/ext2_layout.c`<br>`userland/shell/hdprep_plan.c`<br>`fs/ext2_super.c`<br>`fs/ext2_fmt.c`<br>`fs/ext2_inode.c`<br>`fs/ext2_dir.c`<br>`fs/ext2_file.c`<br>`tools/pc98pt.py`<br>`tools/nhd_deploy.py`<br>`boot/boot_main.c` | [`tools/tests/hdd_stage1_tdd.md`](../tools/tests/hdd_stage1_tdd.md) | [`docs/archive/realhw_v21/TASK_HDD_INSTALL.md`](archive/realhw_v21/TASK_HDD_INSTALL.md) | × |
| 5 | `check-hdd-stage2-host` | `python3 -B tools/tests/test_hdd_stage2.py --target --mutate` | `userland/system/cdinst.c`<br>`userland/system/inst_hdd.c`<br>`userland/system/inst_disk.c`<br>`userland/lib/rt/pkg.c`<br>`userland/shell/hdprep_plan.c`<br>`drivers/pc98pt.c`<br>`fs/ext2_layout.c`<br>`userland/system/install.c`<br>`tools/tests/test_hdd_stage1.py`<br>`tools/nhd_deploy.py`<br>`boot/boot_hdd.asm`<br>`kapi/kapi_generated.c`<br>`exec/exec.c` | [`tools/tests/hdd_stage2_tdd.md`](../tools/tests/hdd_stage2_tdd.md) | [`docs/archive/realhw_v21/TASK_HDD_INSTALL.md`](archive/realhw_v21/TASK_HDD_INSTALL.md) | × |
| 6 | `check-vmkernel-lz4-host` | `python3 -B tools/tests/test_vmkernel_lz4.py --real --target --mutate` | `tools/mkvmkernel.py`<br>`boot/lz4_mini.c`<br>`lib/lz4.c`<br>`boot/loader_fat_new.asm` | [`tools/tests/vmkernel_lz4_tdd.md`](../tools/tests/vmkernel_lz4_tdd.md) | [`docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md`](archive/realhw_v21/TASK_SERIAL_HOSTFS.md) | × |
| 7 | `check-vk32-crc-host` | `python3 -B tools/tests/test_vk32_crc.py --real --target --mutate` | `tools/mkvmkernel.py`<br>`boot/vk32_boot.c`<br>`boot/lz4_mini.c`<br>`boot/loader_fat_new.asm`<br>`tools/tests/vk32_old_lz4_3e22825.asm` | [`tools/tests/vk32_crc_tdd.md`](../tools/tests/vk32_crc_tdd.md) | [`docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md`](archive/realhw_v21/TASK_SERIAL_HOSTFS.md) | × |
| 8 | `check-build-id-host` | `python3 -B tools/tests/test_build_id.py --mutate` | `tools/gen_build_id.py` | [`tools/tests/vk32_crc_tdd.md`](../tools/tests/vk32_crc_tdd.md) | [`docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md`](archive/realhw_v21/TASK_SERIAL_HOSTFS.md) | ○ |
| 9 | `check-kbd-status-host` | `python3 -B tools/tests/test_kbd_status.py --target --mutate` | `drivers/kbd_status.c`<br>`drivers/kbd.c`<br>`kernel/v86_kbd.c` | [`tools/tests/kbd_status_tdd.md`](../tools/tests/kbd_status_tdd.md) | [`docs/POLICY_DEBUG.md`](POLICY_DEBUG.md) | × |
| 10 | `check-kbd-dlog-host` | `python3 -B tools/tests/test_kbd_dlog.py --target --mutate` | — | [`tools/tests/kbd_dlog_tdd.md`](../tools/tests/kbd_dlog_tdd.md) | [`docs/tasks/gui/TASK_KBD_NAV.md`](tasks/gui/TASK_KBD_NAV.md) | × |
| 11 | `check-pcm-cs4231-host` | `python3 -B tools/tests/test_pcm_cs4231.py --target --mutate` | `drivers/pcm_cs4231_math.c`<br>`drivers/pcm_cs4231.c` | [`tools/tests/pcm_cs4231_tdd.md`](../tools/tests/pcm_cs4231_tdd.md) | [`docs/tasks/v3/TASK_PCM_CS4231.md`](tasks/v3/TASK_PCM_CS4231.md) | × |
| 12 | `check-kprintf-attr-host` | `python3 -B tools/tests/test_kprintf_attr.py --target --mutate` | `lib/kprintf_attr.c`<br>`lib/kprintf.c` | [`tools/tests/kprintf_attr_tdd.md`](../tools/tests/kprintf_attr_tdd.md) | — | × |
| 13 | `check-key-inject-host` | `python3 -B tools/tests/test_key_inject.py` | — | — | — | × |
| 14 | `check-gui-gate-host` | `python3 -B tools/tests/test_gui_gate.py --mutate` | `tools/gui_gate.py` | [`tools/tests/gui_gate_tdd.md`](../tools/tests/gui_gate_tdd.md) | — | ○ |
| 15 | `check-kapi-version` | `python3 tools/check_kapi_version.py` | — | — | — | ○ |
| 16 | `check-kapi-out` | `python3 -B tools/tests/test_kapi_out.py`<br>`python3 -B tools/tests/test_gen_kapi_cli.py` | `sdk/gen_kapi.py` | [`tools/tests/kapi_out_tdd.md`](../tools/tests/kapi_out_tdd.md) | [`docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md`](archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md) | ○ |
| 17 | `check-docs-links` | `python3 tools/check_docs_links.py` | — | — | — | ○ |
| 18 | `check-docs-orphans` | `python3 tools/check_docs_orphans.py` | — | [`tools/tests/s0_tdd.md`](../tools/tests/s0_tdd.md) | [`docs/archive/settings/TASK_S0.md`](archive/settings/TASK_S0.md) | ○ |
| 19 | `check-docs-status` | `python3 tools/check_docs_status.py` | — | [`tools/tests/docs_status_tdd.md`](../tools/tests/docs_status_tdd.md) | — | ○ |
| 20 | `check-docs-status-host` | `python3 -B tools/tests/test_docs_status.py --mutate` | `tools/check_docs_status.py` | [`tools/tests/docs_status_tdd.md`](../tools/tests/docs_status_tdd.md) | — | ○ |
| 21 | `check-tests-inventory` | `python3 tools/gen_tests_inventory.py --check` | — | — | — | ○ |
| 22 | `check-manifests` | `python3 tools/check_manifests.py` | — | — | — | × |
| 23 | `check-packages-host` | `python3 -B tools/tests/test_packages.py` | — | — | — | × |
| 24 | `check-constraints` | `python3 tools/check_constraints.py` | — | — | — | ○ |
| 25 | `check-p2v` | `python3 tools/check_p2v.py`<br>`python3 tools/tests/test_p2v.py --mutate` | — | — | — | × |
| 26 | `check-c-dialect` | `python3 -B tools/check_c_dialect.py` | `lib/sqlite3/sqlite3.c`<br>`lib/sqlite3/os32_sqlite_vfs.c`<br>`lib/sqlite3/os32_sqlite_test.c` | [`tools/tests/c_dialect_tdd.md`](../tools/tests/c_dialect_tdd.md) | [`docs/archive/v3/TASK_C11_MIGRATION.md`](archive/v3/TASK_C11_MIGRATION.md) | × |
| 27 | `check-c-dialect-host` | `python3 -B tools/tests/test_c_dialect.py --mutate` | `tools/check_c_dialect.py` | [`tools/tests/c_dialect_tdd.md`](../tools/tests/c_dialect_tdd.md) | [`docs/archive/v3/TASK_C11_MIGRATION.md`](archive/v3/TASK_C11_MIGRATION.md) | × |
| 28 | `check-privileged` | `python3 tools/check_privileged.py` | — | — | — | × |
| 29 | `check-arch-asm` | `python3 tools/check_arch_asm.py`<br>`python3 tools/tests/test_clang_ast.py --mutate` | — | — | — | ○ |
| 30 | `check-le-access` | `python3 tools/check_le_access.py` | — | [`tools/tests/le_access_tdd.md`](../tools/tests/le_access_tdd.md) | [`docs/tasks/portability/ARM_GAUGE.md`](tasks/portability/ARM_GAUGE.md) | ○ |
| 31 | `check-ne2000-ring` | `mkdir -p $(BUILD_OUT)`<br>`gcc $(C_STD) -Wall -Wextra -DNE2K_HOST_TEST -Idrivers  -o $(BUILD_OUT)/ne2000_ring_test tools/tests/ne2000_ring_test.c drivers/ne2000_ring.c`<br>`$(BUILD_OUT)/ne2000_ring_test` | `drivers/ne2000_ring.c` | — | — | ○ |
| 32 | `check-shlib` | `python3 tools/mkshlib.py --check` | — | — | — | ○ |
| 33 | `check-gui-proto` | `python3 tools/check_gui_proto.py` | — | — | — | ○ |
| 34 | `check-term-model` | `cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline`<br>`cargo check --manifest-path userland/libos32term/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline` | `userland/libos32term/Cargo.toml` | — | — | × |
| 35 | `check-term-render` | `cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline`<br>`cargo check --manifest-path userland/libos32term_render/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline` | `userland/libos32term_render/Cargo.toml` | — | — | × |
| 36 | `check-t5a-host` | `cargo test --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml --target x86_64-unknown-linux-gnu --offline` | `userland/rust/t5a_display/host_tests/Cargo.toml` | — | — | × |
| 37 | `check-memory-host` | `python3 -B tools/tests/test_physmem.py`<br>`python3 -B tools/tests/test_paging_bounds.py --mutate`<br>`python3 -B tools/tests/test_paging_bounds.py --rebuild nonmaster`<br>`python3 -B tools/tests/test_paging_bounds.py --rebuild rollback`<br>`python3 -B tools/tests/test_app_band_pde.py --mutate`<br>`python3 -B tools/tests/test_pgalloc_model.py`<br>`python3 -B tools/tests/test_pgalloc_range.py`<br>`python3 -B tools/tests/test_highram_stage.py`<br>`python3 -B tools/tests/test_memory_boot.py --mutate`<br>`python3 -B tools/tests/test_ledger.py --mutate`<br>`python3 -B tools/tests/test_lease.py --mutate`<br>`python3 -B tools/tests/test_exec_r1.py --mutate`<br>`python3 -B tools/tests/test_kstop.py --mutate`<br>`python3 -B tools/tests/test_device_reservation.py --mutate`<br>`python3 -B tools/tests/test_sbrk_tier.py --mutate`<br>`python3 -B tools/tests/test_app_bb_overlap.py --mutate`<br>`python3 -B tools/tests/test_gfx_boot.py --mutate` | `kernel/pgalloc.c`<br>`kernel/physmem.c`<br>`exec/appslot.c` | [`tools/tests/k5b_kernel_tdd.md`](../tools/tests/k5b_kernel_tdd.md)<br>[`tools/tests/physmem_tdd.md`](../tools/tests/physmem_tdd.md)<br>[`tools/tests/paging_bounds_tdd.md`](../tools/tests/paging_bounds_tdd.md)<br>[`tools/tests/app_band_pde_tdd.md`](../tools/tests/app_band_pde_tdd.md)<br>[`tools/tests/pgalloc_model_tdd.md`](../tools/tests/pgalloc_model_tdd.md)<br>[`tools/tests/pgalloc_range_tdd.md`](../tools/tests/pgalloc_range_tdd.md)<br>[`tools/tests/highram_stage_tdd.md`](../tools/tests/highram_stage_tdd.md)<br>[`tools/tests/memory_boot_tdd.md`](../tools/tests/memory_boot_tdd.md)<br>[`tools/tests/device_reservation_tdd.md`](../tools/tests/device_reservation_tdd.md) | [`docs/archive/gui_v13/TASK_K5B_kernel.md`](archive/gui_v13/TASK_K5B_kernel.md)<br>[`docs/tasks/memory/APP_BAND_PDE.md`](tasks/memory/APP_BAND_PDE.md) | × |
| 38 | `check-memmap-host` | `python3 -B tools/tests/test_memmap_gen.py --mutate`<br>`python3 -B tools/tests/test_memmap_boot.py --mutate` | `tools/gen_memmap.py`<br>`kernel/paging.c`<br>`kernel/shm.c`<br>`kernel/pgalloc.c` | [`tools/tests/memmap_tdd.md`](../tools/tests/memmap_tdd.md) | [`docs/archive/kernel_v21/TASK_KSTACK_USER.md`](archive/kernel_v21/TASK_KSTACK_USER.md) | × |
| 39 | `check-memmap` | `python3 tools/gen_memmap.py --check` | — | [`tools/tests/memmap_tdd.md`](../tools/tests/memmap_tdd.md) | [`docs/archive/kernel_v21/TASK_KSTACK_USER.md`](archive/kernel_v21/TASK_KSTACK_USER.md) | × |
| 40 | `check-boot-splash-host` | `python3 -B tools/tests/test_boot_splash_native.py` | `gfx/gfx_core.c`<br>`gfx/backend_pc98.c`<br>`gfx/gfx_vram.c`<br>`kernel/boot_splash.c` | [`tools/tests/boot_splash_native_tdd.md`](../tools/tests/boot_splash_native_tdd.md) | — | ○ |
| 41 | `check-tools-host` | `python3 -B -m unittest discover -s tools/tests -p 'test_np21w_ini.py'`<br>`python3 -B -m unittest discover -s tools/tests -p 'test_np21w_transport.py'`<br>`python3 -B -m unittest discover -s tools/tests -p 'test_np21w_trial.py'`<br>`python3 -B tools/tests/test_nhd_deploy_failure.py`<br>`python3 -B tools/tests/test_filer_normalize.py`<br>`python3 -B tools/tests/test_filer_copy_abort.py`<br>`python3 -B tools/tests/test_about_info.py --mutate`<br>`python3 -B tools/tests/test_gui_button_dispatch.py`<br>`PYTHONPATH=. python3 -B tools/tests/test_emu_playbook.py`<br>`python3 -B tools/tests/test_mk_settings_db.py`<br>`python3 -B tools/tests/test_fetch_fonts.py`<br>`python3 -B tools/tests/test_mk_blank_nhd.py`<br>`python3 -B tools/tests/test_stat_cmd.py`<br>`python3 -B tools/tests/test_tar_cmd.py` | `userland/rust/filer/src/model.rs`<br>`userland/rust/filer/host/model_tests.rs`<br>`userland/rust/libos32gui/src/app.rs`<br>`tools/mk_settings_db.py`<br>`tools/fetch_fonts.py`<br>`userland/cmds/stat.c`<br>`userland/cmds/tar.c`<br>`lib/microtar/microtar.c` | [`tools/tests/about_info_tdd.md`](../tools/tests/about_info_tdd.md)<br>[`tools/tests/stat_cmd_tdd.md`](../tools/tests/stat_cmd_tdd.md)<br>[`tools/tests/s6_tdd.md`](../tools/tests/s6_tdd.md)<br>[`tools/tests/np21w_ini_tdd.md`](../tools/tests/np21w_ini_tdd.md)<br>[`tools/tests/np21w_transport_tdd.md`](../tools/tests/np21w_transport_tdd.md)<br>[`tools/tests/np21w_trial_tdd.md`](../tools/tests/np21w_trial_tdd.md)<br>[`tools/tests/emu_playbook_tdd.md`](../tools/tests/emu_playbook_tdd.md) | `S6` | × |
| 42 | `check-np21w-ctl-host` | `python3 -B tools/tests/test_np21w_ctl.py --mutate` | — | — | — | ○ |
| 43 | `check-np21w-ini-live-host` | `python3 -B tools/tests/test_np21w_ini_live.py --mutate` | — | [`tools/tests/np21w_ini_live_tdd.md`](../tools/tests/np21w_ini_live_tdd.md) | — | × |
| 44 | `check-gshell-host` | `python3 userland/gshell/host/integration.py --mutate` | — | — | — | × |
| 45 | `check-db-owned-host` | `MUTATE=$(MUTATE) python3 -B -m unittest discover -s tools/tests -p 'test_kapi_db_owned.py'` | `kapi/kapi_db.c` | [`tools/tests/kapi_db_owned_tdd.md`](../tools/tests/kapi_db_owned_tdd.md) | — | ○ |
| 46 | `check-vfs-fd-sqlite-host` | `python3 tools/tests/test_vfs_fd_sqlite.py` | `fs/vfs_fd.c` | [`tools/tests/vfs_fd_sqlite_tdd.md`](../tools/tests/vfs_fd_sqlite_tdd.md) | — | ○ |
| 47 | `check-fdc-seek-host` | `python3 -B tools/tests/test_fdc_seek.py --target --mutate` | `drivers/fdc_decide.c`<br>`drivers/fdc.c` | [`tools/tests/fdc_seek_tdd.md`](../tools/tests/fdc_seek_tdd.md) | — | × |
| 48 | `check-serial-vfast-host` | `python3 -B tools/tests/test_serial_vfast.py --target --mutate` | `drivers/serial_plan.c`<br>`userland/shell/serial_watchdog.c`<br>`drivers/serial.c` | [`tools/tests/serial_vfast_tdd.md`](../tools/tests/serial_vfast_tdd.md) | [`docs/archive/realhw_v21/TASK_SERIAL_VFAST.md`](archive/realhw_v21/TASK_SERIAL_VFAST.md) | × |
| 49 | `check-serial-portc-host` | `python3 -B tools/tests/test_serial_portc.py --mutate` | `drivers/serial_plan.c`<br>`drivers/serial.c`<br>`kernel/sys.c` | — | — | ○ |
| 50 | `check-cpu-calibrate-host` | `python3 -B tools/tests/test_cpu_calibrate.py --target --mutate` | `kernel/cpu_calibrate_math.c`<br>`kernel/cpu_calibrate.c` | [`tools/tests/cpu_calibrate_tdd.md`](../tools/tests/cpu_calibrate_tdd.md) | [`docs/archive/realhw_v21/TASK_SERIAL_VFAST.md`](archive/realhw_v21/TASK_SERIAL_VFAST.md) | × |
| 51 | `check-pit-clock-host` | `python3 -B tools/tests/test_pit_clock.py --target --mutate` | `kernel/pit_math.c`<br>`kernel/sysclk.c`<br>`kernel/idt.c` | [`tools/tests/pit_clock_tdd.md`](../tools/tests/pit_clock_tdd.md) | [`docs/tasks/v3/TASK_HAL_WIRING.md`](tasks/v3/TASK_HAL_WIRING.md) | × |
| 52 | `check-dma8237-host` | `python3 -B tools/tests/test_dma8237.py --target --mutate` | `drivers/dma8237_math.c`<br>`drivers/dma8237.c` | [`tools/tests/dma8237_tdd.md`](../tools/tests/dma8237_tdd.md) | [`docs/tasks/v3/TASK_HAL_WIRING.md`](tasks/v3/TASK_HAL_WIRING.md) | × |
| 53 | `check-dma-pool-host` | `python3 -B tools/tests/test_dma_pool.py --target --mutate` | `drivers/dma8237_math.c`<br>`kernel/dma_pool_math.c`<br>`kernel/dma_pool.c` | [`tools/tests/dma_pool_tdd.md`](../tools/tests/dma_pool_tdd.md) | [`docs/tasks/v3/TASK_HAL_WIRING.md`](tasks/v3/TASK_HAL_WIRING.md) | × |
| 54 | `check-pci-bind-host` | `python3 -B tools/tests/test_pci_bind.py --target --mutate` | `drivers/pci_bind_match.c`<br>`drivers/pci_bind.c` | [`tools/tests/pci_bind_tdd.md`](../tools/tests/pci_bind_tdd.md) | [`docs/tasks/v3/TASK_HAL_WIRING.md`](tasks/v3/TASK_HAL_WIRING.md) | × |
| 55 | `check-rshell-serial-host` | `python3 -B tools/tests/test_rshell_serial.py --mutate` | `tools/rshell_serial.py` | [`tools/tests/serial_vfast_tdd.md`](../tools/tests/serial_vfast_tdd.md) | [`docs/archive/realhw_v21/TASK_SERIAL_VFAST.md`](archive/realhw_v21/TASK_SERIAL_VFAST.md) | ○ |
| 56 | `check-serialfs-host` | `python3 -B tools/tests/test_serialfs.py --target --mutate` | `drivers/serial.c`<br>`fs/sfs_proto.c`<br>`fs/sfs_client.c`<br>`fs/serialfs_session.c`<br>`lib/crc32.c`<br>`drivers/serial_plan.c`<br>`userland/system/hsync_bootold.inc` | [`tools/tests/serialfs_tdd.md`](../tools/tests/serialfs_tdd.md) | [`docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md`](archive/realhw_v21/TASK_SERIAL_HOSTFS.md) | × |
| 57 | `check-vfs-mount-dev-host` | `python3 -B tools/tests/test_vfs_mount_dev.py`<br>`python3 -B tools/tests/test_ext2_read_bound.py`<br>`python3 -B tools/tests/test_ext2_write_io.py`<br>`python3 -B tools/tests/test_fatfs_stat.py` | `fs/vfs.c`<br>`fs/ext2_vfs.c`<br>`fs/ext2_file.c`<br>`fs/ext2_super.c`<br>`fs/ext2_inode.c`<br>`fs/ext2_dir.c`<br>`fs/ext2_fmt.c`<br>`lib/microtar/microtar.c`<br>`drivers/pc98pt.c`<br>`fs/fatfs_vfs.c`<br>`kernel/bootlog.c` | [`tools/tests/k5b_kernel_tdd.md`](../tools/tests/k5b_kernel_tdd.md)<br>[`tools/tests/s6p_tdd.md`](../tools/tests/s6p_tdd.md)<br>[`tools/tests/vfs_mount_dev_tdd.md`](../tools/tests/vfs_mount_dev_tdd.md) | [`docs/archive/gui_v13/TASK_K5B_kernel.md`](archive/gui_v13/TASK_K5B_kernel.md)<br>[`docs/archive/settings/TASK_S6.md`](archive/settings/TASK_S6.md) | × |
| 58 | `check-sqlite-groups-host` | `python3 tools/tests/test_sqlite_groups.py` | `lib/sqlite3/sqlite3.c`<br>`lib/sqlite3/os32_sqlite_vfs.c` | [`tools/tests/sqlite_groups_tdd.md`](../tools/tests/sqlite_groups_tdd.md) | — | ○ |
| 59 | `check-con-sink-host` | `python3 -B tools/tests/test_con_sink.py` | `kernel/con_sink.c`<br>`kernel/console.c` | [`tools/tests/con_sink_tdd.md`](../tools/tests/con_sink_tdd.md) | [`docs/archive/gui_v13/TASK_K6C_console.md`](archive/gui_v13/TASK_K6C_console.md) | × |
| 60 | `check-bootlog-host` | `python3 -B tools/tests/test_bootlog.py --target --mutate` | `kernel/bootlog.c`<br>`kernel/console.c`<br>`kernel/kernel.c`<br>`kernel/bootlog_save.c`<br>`fs/ext2_vfs.c`<br>`fs/fatfs_vfs.c`<br>`fs/ext2_dir.c` | [`tools/tests/bootlog_tdd.md`](../tools/tests/bootlog_tdd.md) | — | × |
| 61 | `check-kbd-inject-host` | `python3 -B tools/tests/test_kbd_inject.py` | `kernel/kbd_inject.c` | [`tools/tests/k7_tdd.md`](../tools/tests/k7_tdd.md) | [`docs/archive/gui_v13/TASK_K7_input.md`](archive/gui_v13/TASK_K7_input.md) | × |
| 62 | `check-launch-host` | `python3 -B tools/tests/test_launch.py` | `exec/launch.c` | [`tools/tests/t9_tdd.md`](../tools/tests/t9_tdd.md) | [`docs/archive/gui_v13/TASK_T9_sh.md`](archive/gui_v13/TASK_T9_sh.md) | × |
| 63 | `check-ring3-str-host` | `python3 -B tools/tests/test_ring3_str.py` | `exec/ring3_str.c` | [`tools/tests/t9_tdd.md`](../tools/tests/t9_tdd.md) | [`docs/archive/gui_v13/TASK_T9_sh.md`](archive/gui_v13/TASK_T9_sh.md) | × |
| 64 | `check-ring3-guard-host` | `python3 -B tools/tests/test_ring3_guard.py --target`<br>`python3 -B tools/tests/test_ring3_guard.py --mutate` | `exec/ring3_str.c`<br>`kernel/gui.c`<br>`fs/fd_redirect.c` | [`tools/tests/ring3_guard_tdd.md`](../tools/tests/ring3_guard_tdd.md) | [`docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md`](archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md) | × |
| 65 | `check-sh-launch-host` | `python3 -B tools/tests/test_sh_launch.py` | `userland/shell/sh_launch.inc` | [`tools/tests/t9_tdd.md`](../tools/tests/t9_tdd.md) | [`docs/archive/gui_v13/TASK_T9_sh.md`](archive/gui_v13/TASK_T9_sh.md) | × |
| 66 | `check-sh-shell-host` | `python3 -B tools/tests/test_sh_shell.py` | `userland/shell/sh_redraw.inc`<br>`userland/shell/sh_pipe.inc`<br>`userland/shell/sh_ls.inc`<br>`userland/shell/sh_launch.inc`<br>`userland/shell/sh_args.inc`<br>`userland/shell/cmd_script.c`<br>`userland/shell/cmd_fs_shared.c`<br>`userland/shell/cmd_file.c`<br>`userland/shell/cmd_mnt.c`<br>`userland/shell/cmd_env.c`<br>`userland/shell/cmd_sys.c`<br>`userland/shell/kbd_watch.c` | [`tools/tests/t9_tdd.md`](../tools/tests/t9_tdd.md) | [`docs/archive/gui_v13/TASK_T9_sh.md`](archive/gui_v13/TASK_T9_sh.md) | × |
| 67 | `check-sh-truncation-host` | `python3 -B tools/tests/test_sh_truncation.py` | `userland/shell/main.c`<br>`userland/shell/cmd_base.c`<br>`userland/shell/cmd_dir.c`<br>`userland/shell/cmd_env.c`<br>`userland/shell/cmd_fs_shared.c`<br>`userland/shell/cmd_file.c`<br>`userland/shell/cmd_mnt.c`<br>`userland/shell/cmd_script.c`<br>`userland/shell/cmd_sys.c`<br>`userland/shell/kbd_watch.c`<br>`userland/shell/cmd_pci.c`<br>`drivers/pci_decode.c`<br>`userland/shell/pci_verbose.c`<br>`userland/shell/cmd_hdprep.c`<br>`userland/shell/hdprep_plan.c`<br>`drivers/pc98pt.c`<br>`userland/shell/cmd_filer.c`<br>`userland/shell/rshell.c`<br>`userland/shell/serial_watchdog.c`<br>`userland/shell/ui.c`<br>`kernel/kernel.c` | [`tools/tests/sh_truncation_tdd.md`](../tools/tests/sh_truncation_tdd.md) | [`docs/archive/shell/TASK_SH_TRUNCATION.md`](archive/shell/TASK_SH_TRUNCATION.md) | × |
| 68 | `check-multiapp-model-host` | `python3 -B tools/tests/test_multiapp_model.py`<br>`python3 -B tools/tests/test_multiapp_impl.py`<br>`MUTATE=$(MUTATE) python3 -B tools/tests/test_owner_reclaim.py` | `exec/appslot.c`<br>`fs/fd_redirect.c`<br>`fs/pipe_buffer.c`<br>`kernel/shm.c` | [`tools/tests/multiapp_model_tdd.md`](../tools/tests/multiapp_model_tdd.md)<br>[`tools/tests/k5b_kernel_tdd.md`](../tools/tests/k5b_kernel_tdd.md) | [`docs/archive/gui_v13/TASK_K5_multiapp.md`](archive/gui_v13/TASK_K5_multiapp.md)<br>[`docs/archive/gui_v13/TASK_K5B_kernel.md`](archive/gui_v13/TASK_K5B_kernel.md) | × |
| 69 | `check-settings-protect-host` | `python3 -B tools/tests/test_deploy_protect.py`<br>`python3 -B tools/tests/test_hsync_protect.py` | `userland/system/hsync.c` | [`tools/tests/s0_tdd.md`](../tools/tests/s0_tdd.md) | [`docs/archive/settings/TASK_S0.md`](archive/settings/TASK_S0.md) | × |
| 70 | `check-hsync-h1-host` | `python3 -B tools/tests/test_hsync_h1.py --target` | `fs/hostdrv_stat_rules.inc`<br>`userland/system/hsync.c`<br>`lib/crc32.c` | [`tools/tests/h1_tdd.md`](../tools/tests/h1_tdd.md) | [`docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md`](tasks/shell/HSYNC_IMPROVEMENT_PLAN.md) | × |
| 71 | `check-hostdrv-list-host` | `python3 -B tools/tests/test_hostdrv_list.py --target` | `fs/hostdrvfs.c`<br>`fs/hostdrv_list_rules.inc` | [`tools/tests/h1_tdd.md`](../tools/tests/h1_tdd.md)<br>[`tools/tests/hostdrv_list_tdd.md`](../tools/tests/hostdrv_list_tdd.md) | [`docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md`](tasks/shell/HSYNC_IMPROVEMENT_PLAN.md) | × |
| 72 | `check-fs-kind-host` | `python3 -B tools/tests/test_fs_kind.py --target` | `userland/shell/cmd_fs_shared.c`<br>`userland/shell/cmd_file.c` | [`tools/tests/h1_tdd.md`](../tools/tests/h1_tdd.md)<br>[`tools/tests/fs_kind_tdd.md`](../tools/tests/fs_kind_tdd.md) | [`docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md`](tasks/shell/HSYNC_IMPROVEMENT_PLAN.md) | × |
| 73 | `check-vfs-kind-host` | `python3 -B tools/tests/test_vfs_kind.py --target` | `fs/vfs.c`<br>`fs/vfs_fd.c` | [`tools/tests/h1_tdd.md`](../tools/tests/h1_tdd.md)<br>[`tools/tests/vfs_kind_tdd.md`](../tools/tests/vfs_kind_tdd.md) | [`docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md`](tasks/shell/HSYNC_IMPROVEMENT_PLAN.md) | × |
| 74 | `check-b8-open-host` | `python3 -B tools/tests/test_b8_open.py --target`<br>`python3 -B tools/tests/test_b8_hostdrv.py --target` | `fs/hostdrv_stat_rules.inc`<br>`fs/ext2_super.c`<br>`fs/ext2_inode.c`<br>`fs/ext2_dir.c`<br>`fs/ext2_file.c`<br>`fs/ext2_fmt.c`<br>`fs/ext2_vfs.c`<br>`fs/vfs.c`<br>`fs/vfs_fd.c`<br>`lib/kutf16.c`<br>`fs/hostdrvfs.c` | [`tools/tests/b8_tdd.md`](../tools/tests/b8_tdd.md) | [`docs/archive/shell/TASK_FS_TYPE.md`](archive/shell/TASK_FS_TYPE.md) | × |
| 75 | `check-ext2-empty-name-host` | `python3 -B tools/tests/test_ext2_empty_name.py --target --mutants` | `fs/ext2_dir.c`<br>`fs/ext2_file.c`<br>`fs/ext2_vfs.c`<br>`fs/vfs.c`<br>`tools/mkpkg.py` | [`tools/tests/ext2_empty_name_tdd.md`](../tools/tests/ext2_empty_name_tdd.md) | [`docs/archive/kernel_v21/TASK_EXT2_EMPTY_NAME.md`](archive/kernel_v21/TASK_EXT2_EMPTY_NAME.md) | ○ |
| 76 | `check-vfs-fd-path-host` | `python3 -B tools/tests/test_vfs_fd_path.py --target --mutants` | `sdk/crt/syscalls.c`<br>`fs/vfs.c`<br>`fs/vfs_fd.c`<br>`fs/ext2_vfs.c`<br>`fs/ext2_file.c`<br>`fs/fatfs_vfs.c`<br>`fs/hostdrvfs.c`<br>`fs/iso9660.c`<br>`lib/kutf16.c`<br>`lib/sqlite3/os32_sqlite_vfs.c`<br>`kernel/ime_dict.c`<br>`lib/sqlite3/sqlite3.c`<br>`fs/fatfs/ff.c`<br>`tools/mkpkg.py`<br>`userland/system/cdinst.c` | [`tools/tests/vfs_fd_path_tdd.md`](../tools/tests/vfs_fd_path_tdd.md) | [`docs/archive/kernel_v21/TASK_VFS_FD_PATH.md`](archive/kernel_v21/TASK_VFS_FD_PATH.md) | ○ |
| 77 | `check-db-v50-host` | `python3 -B tools/tests/test_kapi_db_v50.py` | `exec/exec.c`<br>`lib/sqlite3/sqlite3.c`<br>`lib/sqlite3/os32_sqlite_vfs.c`<br>`kapi/kapi_db.c`<br>`kernel/paging.c`<br>`kernel/kselftest.c` | [`tools/tests/s0_tdd.md`](../tools/tests/s0_tdd.md) | [`docs/archive/settings/TASK_S0.md`](archive/settings/TASK_S0.md) | ○ |
| 78 | `check-db-errstr-host` | `python3 -B tools/tests/test_db_errstr.py --target --mutate` | `lib/sqlite3/sqlite3.c`<br>`kapi/kapi_db.c`<br>`lib/sqlite3/os32_sqlite_vfs.c` | [`tools/tests/db_errstr_tdd.md`](../tools/tests/db_errstr_tdd.md) | [`docs/archive/kernel_v21/TASK_DB_ERRSTR.md`](archive/kernel_v21/TASK_DB_ERRSTR.md) | × |
| 79 | `check-cfg-host` | `python3 -B tools/tests/test_cfg.py` | `tools/mk_settings_db.py`<br>`lib/sqlite3/sqlite3.c`<br>`lib/sqlite3/os32_sqlite_vfs.c`<br>`kapi/kapi_db.c`<br>`userland/lib/cfg/libos32cfg.c`<br>`userland/lib/cfg/cfg_enum.c`<br>`userland/lib/cfg/cfg_tsv.c`<br>`userland/lib/cfg/cfg_init.c`<br>`userland/lib/cfg/cfg_json.c`<br>`userland/lib/cfg/cfg_import.c`<br>`userland/cmds/cfg.c`<br>`userland/tests/cfg_bench.c` | [`tools/tests/s2_tdd.md`](../tools/tests/s2_tdd.md) | [`docs/archive/settings/TASK_S2.md`](archive/settings/TASK_S2.md) | ○ |
| 80 | `check-gui-host` | `python3 -B tools/tests/test_gui_reattach.py --mutate`<br>`cargo test --manifest-path userland/rust/libos32gui/host_tests/Cargo.toml --target x86_64-unknown-linux-gnu --offline` | `userland/rust/libos32gui/host_tests/Cargo.toml` | — | — | × |
| 81 | `check-install-recover-host` | `python3 -B tools/tests/test_install_recover.py` | `lib/sqlite3/sqlite3.c`<br>`lib/sqlite3/os32_sqlite_vfs.c`<br>`kapi/kapi_db.c`<br>`userland/system/install_recover.inc` | [`tools/tests/s3_tdd.md`](../tools/tests/s3_tdd.md) | [`docs/archive/settings/TASK_S3.md`](archive/settings/TASK_S3.md) | ○ |
| 82 | `check-install-fresh-host` | `python3 -B tools/tests/test_install_fresh.py --target` | `userland/system/install.c` | [`tools/tests/s3i2_tdd.md`](../tools/tests/s3i2_tdd.md) | [`docs/archive/settings/TASK_S3I2.md`](archive/settings/TASK_S3I2.md) | × |
| 83 | `check-host-agent` | `python3 -B tools/tests/test_host_agent.py` | — | [`tools/tests/n1_tdd.md`](../tools/tests/n1_tdd.md) | [`docs/archive/network/TASK_N1.md`](archive/network/TASK_N1.md) | ○ |
| 84 | `check-net-link-host` | `python3 -B tools/tests/test_net_link.py --target` | `net/link.c`<br>`kapi/kapi_host.c`<br>`kapi/kapi_generated.c`<br>`exec/exec.c`<br>`kernel/isr_handlers.c`<br>`drivers/lgy98.c` | [`tools/tests/n1_tdd.md`](../tools/tests/n1_tdd.md) | [`docs/archive/network/TASK_N1.md`](archive/network/TASK_N1.md) | × |
| 85 | `check-host-lib-host` | `python3 -B tools/tests/test_host_lib.py --target` | `userland/lib/host/libos32host.c`<br>`userland/cmds/wget.c`<br>`userland/cmds/lpr.c`<br>`userland/cmds/hclip.c`<br>`userland/cmds/hdate.c` | [`tools/tests/n3_tdd.md`](../tools/tests/n3_tdd.md) | [`docs/archive/network/TASK_N3.md`](archive/network/TASK_N3.md) | × |
| 86 | `check-lan-bridge-host` | `python3 -B tools/tests/test_lan_bridge.py --mutate` | `tools/lan_bridge.py`<br>`tools/host_agent.py` | [`tools/tests/lan_bridge_tdd.md`](../tools/tests/lan_bridge_tdd.md) | [`docs/tasks/realhw/TASK_LAN_82557.md`](tasks/realhw/TASK_LAN_82557.md) | ○ |
| 87 | `check-pci-decode-host` | `python3 -B tools/tests/test_pci_decode.py --target --mutate` | `drivers/pci_decode.c`<br>`userland/shell/pci_verbose.c`<br>`drivers/pci.c`<br>`userland/shell/cmd_pci.c` | [`tools/tests/pci_decode_tdd.md`](../tools/tests/pci_decode_tdd.md) | [`docs/tasks/realhw/TASK_LAN_82557.md`](tasks/realhw/TASK_LAN_82557.md) | × |
| 88 | `check-irq-math-host` | `python3 -B tools/tests/test_irq_math.py --target --mutate` | `kernel/irq_math.c`<br>`kernel/irq.c` | [`tools/tests/irq_math_tdd.md`](../tools/tests/irq_math_tdd.md) | [`docs/tasks/v3/TASK_HAL_WIRING.md`](tasks/v3/TASK_HAL_WIRING.md) | × |
| 89 | `check-time-math-host` | `python3 -B tools/tests/test_time_math.py --target --mutate` | `kernel/time_math.c`<br>`kernel/ktime.c` | [`tools/tests/time_math_tdd.md`](../tools/tests/time_math_tdd.md) | [`docs/tasks/v3/TASK_HAL_WIRING.md`](tasks/v3/TASK_HAL_WIRING.md) | × |
| 90 | `check-v86-gcap-host` | `python3 -B tools/tests/test_v86_gcap.py --target --mutate` | `kernel/v86_gcap_math.c`<br>`kernel/v86_gcap.c`<br>`kernel/v86_test16_gcap.asm` | [`tools/tests/v86_gcap_tdd.md`](../tools/tests/v86_gcap_tdd.md) | [`docs/tasks/realhw/TASK_PEGC480_REALHW.md`](tasks/realhw/TASK_PEGC480_REALHW.md) | × |
| 91 | `check-fdc-track-host` | `python3 -B tools/tests/test_fdc_track.py --target --mutate --require-image` | `drivers/fdc_decide.c`<br>`drivers/fdc_track.c`<br>`drivers/dma8237_math.c`<br>`drivers/fdc.c`<br>`fs/fatfs/diskio.c`<br>`fs/fatfs/ff.c`<br>`fs/fatfs_vfs.c` | [`tools/tests/fdc_track_tdd.md`](../tools/tests/fdc_track_tdd.md) | — | × |
| 92 | `check-cd-read-host` | `python3 -B tools/tests/test_cd_read.py --target --mutate` | `userland/lib/rt/pkg.c`<br>`drivers/atapi.c`<br>`fs/iso9660.c` | [`tools/tests/cd_read_tdd.md`](../tools/tests/cd_read_tdd.md) | — | × |
| 93 | `check-map` | `python3 tools/check_select.py --lint` | — | — | — | ○ |
| 94 | `check-check-select-host` | `python3 -B tools/tests/test_check_select.py --mutate`<br>`python3 -B tools/tests/test_checkinfra.py --mutate` | `tools/check_select.py` | — | — | ○ |
| 95 | `check-kapi-layout-host` | `python3 -B tools/tests/test_kapi_layout.py --mutate` | `exec/os32x_hdr.c`<br>`sdk/gen_kapi.py`<br>`exec/exec.c`<br>`sdk/crt/crt0_c.c` | — | — | × |
| 96 | `check-edit-doc-host` | `python3 -B tools/tests/test_edit_doc.py --mutate` | `userland/rust/libos32gui/src/textcore.rs`<br>`userland/rust/libos32gui/host/textcore_tests.rs`<br>`userland/rust/edit_gui/src/doc.rs`<br>`userland/rust/edit_gui/host/doc_tests.rs`<br>`userland/rust/libos32gui/src/widget.rs`<br>`userland/rust/libos32gui/src/app.rs`<br>`userland/rust/libos32gui/src/uistate.rs` | [`tools/tests/edit_gui_tdd.md`](../tools/tests/edit_gui_tdd.md) | [`docs/archive/gui_v14/TASK_EDIT_GUI.md`](archive/gui_v14/TASK_EDIT_GUI.md) | × |
| 97 | `check-fstat-redir-host` | `python3 -B tools/tests/test_fstat_redir.py --target --mutate` | `fs/vfs_fd.c`<br>`fs/fd_redirect.c`<br>`fs/vfs.c` | [`tools/tests/fstat_redir_tdd.md`](../tools/tests/fstat_redir_tdd.md) | [`docs/archive/test/TASK_FSTAT_REDIR.md`](archive/test/TASK_FSTAT_REDIR.md) | × |
| 98 | `check-kstring-c-host` | `python3 -B tools/tests/test_kstring_c.py --mutate` | `lib/kstring_asm.asm`<br>`lib/kstring_c.c` | [`tools/tests/kstring_c_tdd.md`](../tools/tests/kstring_c_tdd.md) | [`docs/tasks/portability/ARM_GAUGE.md`](tasks/portability/ARM_GAUGE.md) | × |
| 99 | `check-kstr-bench-host` | `python3 -B tools/tests/test_kstr_bench.py --target --mutate` | `userland/tests/kstr_bench.c`<br>`lib/kstring_asm.asm`<br>`lib/kstring_c.c`<br>`build/programs.mk` | [`tools/tests/kstr_bench_tdd.md`](../tools/tests/kstr_bench_tdd.md) | [`docs/archive/portability/TASK_KSTRING_BENCH.md`](archive/portability/TASK_KSTRING_BENCH.md) | × |
| 100 | `check-sh-status-host` | `python3 -B tools/tests/test_sh_status.py --mutate` | `userland/shell/main.c`<br>`userland/shell/cmd_base.c`<br>`userland/shell/cmd_dir.c`<br>`userland/shell/cmd_env.c`<br>`userland/shell/cmd_fs_shared.c`<br>`userland/shell/cmd_file.c`<br>`userland/shell/cmd_mnt.c`<br>`userland/shell/cmd_script.c`<br>`userland/shell/cmd_sys.c`<br>`userland/shell/kbd_watch.c`<br>`userland/shell/cmd_pci.c`<br>`drivers/pci_decode.c`<br>`userland/shell/pci_verbose.c`<br>`userland/shell/cmd_hdprep.c`<br>`userland/shell/hdprep_plan.c`<br>`drivers/pc98pt.c`<br>`userland/shell/cmd_filer.c`<br>`userland/shell/rshell.c`<br>`userland/shell/serial_watchdog.c`<br>`userland/shell/ui.c` | [`tools/tests/sh_status_tdd.md`](../tools/tests/sh_status_tdd.md) | [`docs/archive/shell/TASK_EXIT_STATUS.md`](archive/shell/TASK_EXIT_STATUS.md) | × |
| 101 | `check-hsync-h3-host` | `python3 -B tools/tests/test_hsync_h3.py --target --mutate` | `fs/vfs.c`<br>`fs/hostdrv_stat_rules.inc`<br>`userland/system/hsync.c` | [`tools/tests/h3_tdd.md`](../tools/tests/h3_tdd.md) | [`docs/archive/shell/TASK_H3.md`](archive/shell/TASK_H3.md) | × |
| 102 | `check-hsync-h2-host` | `python3 -B tools/tests/test_hsync_h2.py --target --mutate` | `userland/system/hsync.c` | [`tools/tests/hsync_h2_tdd.md`](../tools/tests/hsync_h2_tdd.md) | [`docs/archive/shell/TASK_H2.md`](archive/shell/TASK_H2.md) | × |
| 103 | `check-h4-manifest-host` | `python3 -B tools/tests/test_h4_manifest.py --target --mutate`<br>`python3 -B tools/tests/test_hostdrv_manifest.py --mutate` | `userland/system/hsync.c`<br>`tools/hostdrv_deploy.py` | [`tools/tests/h4_manifest_tdd.md`](../tools/tests/h4_manifest_tdd.md) | [`docs/archive/shell/TASK_H4.md`](archive/shell/TASK_H4.md) | × |
| 104 | `check-vfs-excl-host` | `python3 -B tools/tests/test_vfs_excl.py --target --mutate` | `fs/vfs.c`<br>`fs/vfs_fd.c` | [`tools/tests/vfs_excl_tdd.md`](../tools/tests/vfs_excl_tdd.md) | [`docs/archive/shell/TASK_H2.md`](archive/shell/TASK_H2.md) | × |
| 105 | `check-fs-kind-callers-host` | `python3 -B tools/tests/test_fs_kind_callers.py --target --mutate` | `userland/shell/cmd_fs_shared.c`<br>`userland/shell/cmd_file.c` | [`tools/tests/fs_kind_callers_tdd.md`](../tools/tests/fs_kind_callers_tdd.md) | [`docs/archive/shell/TASK_FS_TYPE.md`](archive/shell/TASK_FS_TYPE.md) | × |
| 106 | `check-cat-linenum-host` | `python3 -B tools/tests/test_cat_linenum.py --target --mutate` | `userland/shell/cmd_fs_shared.c`<br>`userland/shell/cmd_file.c` | [`tools/tests/cat_linenum_tdd.md`](../tools/tests/cat_linenum_tdd.md) | — | × |
| 107 | `check-result-conv-host` | `python3 -B tools/tests/test_result_conv.py --target --mutate` | `userland/tests/stat_t.c`<br>`userland/tests/restest.c`<br>`userland/tests/test2.c`<br>`userland/tests/klibc_test.c`<br>`userland/tests/font_load_test.c`<br>`userland/rust/alloc_demo/src/lib.rs` | [`tools/tests/result_conv_tdd.md`](../tools/tests/result_conv_tdd.md) | [`docs/archive/test/TASK_TEST_RESULT.md`](archive/test/TASK_TEST_RESULT.md) | × |
| 108 | `check-guest-host` | `python3 -B tools/tests/test_guest_tests.py --mutate`<br>`python3 -B tools/tests/test_e9_observation.py --mutate` | — | [`tools/tests/guest_tests_tdd.md`](../tools/tests/guest_tests_tdd.md) | [`docs/archive/test/TASK_TEST_RUNNER.md`](archive/test/TASK_TEST_RUNNER.md) | ○ |
| 109 | `check-fd-redirect-d0a-host` | `python3 -B tools/tests/test_fd_redirect_d0a.py --mutate` | `fs/fd_redirect.c`<br>`exec/redir_access.c` | — | — | × |
| 110 | `check-caller-access-host` | `python3 -B tools/tests/test_caller_access.py --mutate` | `exec/exec.c`<br>`exec/redir_access.c` | — | — | × |
| 111 | `check-access-walk-host` | `python3 -B tools/tests/test_host32.py`<br>`HOST32_RUNNERS ごと: python3 -B tools/tests/test_access_walk.py --runner <runner>; 先頭で --mutate` | `exec/access_walk.c`<br>`exec/redir_access.c`<br>`kernel/paging.c`<br>`kernel/pgalloc.c`<br>`kernel/shlib.c`<br>`kernel/kselftest.c` | — | — | × |
| 112 | `check-caller-copy-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_caller_copy.py --runner <runner>; 先頭で --mutate` | `exec/redir_access.c`<br>`exec/access_walk.c`<br>`kernel/kselftest.c` | — | — | × |
| 113 | `check-db-caller-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_db_caller.py --runner <runner>; 先頭で --mutate` | `kapi/kapi_db.c`<br>`exec/exec.c`<br>`exec/redir_access.c`<br>`exec/access_walk.c`<br>`fs/vfs.c`<br>`kapi/kapi_generated.c`<br>`kernel/kselftest.c` | — | — | × |
| 114 | `check-surface-query-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_surface_query.py --runner <runner>; 先頭で --mutate` | `exec/surface_query.c`<br>`exec/redir_access.c`<br>`exec/access_walk.c`<br>`kernel/paging.c`<br>`kernel/pgalloc.c` | — | — | × |
| 115 | `check-nano-inputs-host` | `python3 -B tools/tests/test_nano_inputs.py --mutate` | `tools/check_nano_inputs.py` | [`tools/tests/nano_inputs_tdd.md`](../tools/tests/nano_inputs_tdd.md) | [`docs/tasks/v3/TASK_T2D_T2H.md`](tasks/v3/TASK_T2D_T2H.md) | × |
| 116 | `check-nano-adapter-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_nano_adapter.py --runner <runner>; 先頭で --mutate` | — | [`tools/tests/nano_adapter_tdd.md`](../tools/tests/nano_adapter_tdd.md) | [`docs/tasks/v3/TASK_T2D_T2H.md`](tasks/v3/TASK_T2D_T2H.md) | × |
| 117 | `check-appmem-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_appmem.py --runner <runner>; 先頭で --mutate` | `exec/appmem.c`<br>`tools/tests/test_appmem.py`<br>`tools/tests/host32.py`<br>`tools/tests/mutpar.py` | [`tools/tests/appmem_tdd.md`](../tools/tests/appmem_tdd.md) | [`docs/tasks/v3/TASK_T2D_T2H.md`](tasks/v3/TASK_T2D_T2H.md) | × |
| 118 | `check-appmem-map-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_appmem_map.py --runner <runner>; 先頭で --mutate` | `exec/appmem.c`<br>`kernel/paging_app.c`<br>`exec/appmem_map.c`<br>`exec/appmem_unmap.c`<br>`kernel/paging.c`<br>`kernel/pgalloc.c`<br>`kernel/physmem.c`<br>`tools/tests/test_appmem_map.py`<br>`tools/tests/host32.py`<br>`tools/tests/mutpar.py` | [`tools/tests/appmem_map_tdd.md`](../tools/tests/appmem_map_tdd.md) | [`docs/tasks/v3/TASK_T2D_T2H.md`](tasks/v3/TASK_T2D_T2H.md) | × |
| 119 | `check-surface-lease-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_surface_lease.py --runner <runner>; 先頭で --mutate` | `exec/lease.c`<br>`exec/surface_query.c`<br>`exec/redir_access.c`<br>`exec/access_walk.c`<br>`kernel/paging.c`<br>`kernel/pgalloc.c` | — | — | × |
| 120 | `check-surface-bundle-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_surface_bundle.py --runner <runner>; 先頭で --mutate` | `exec/lease.c`<br>`exec/surface_query.c`<br>`exec/exec.c`<br>`kernel/v86_mem.c`<br>`kernel/paging.c`<br>`kernel/pgalloc.c` | [`tools/tests/surface_bundle_tdd.md`](../tools/tests/surface_bundle_tdd.md) | [`docs/tasks/v3/TASK_T2D_T2H.md`](tasks/v3/TASK_T2D_T2H.md) | × |
| 121 | `check-gfx-kernel-fb-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_gfx_kernel_fb.py --runner <runner>; 先頭で --mutate` | `gfx/gfx_core.c`<br>`gfx/backend_pc98.c`<br>`gfx/backend_pegc.c`<br>`gfx/backend_cirrus.c`<br>`gfx/gfx_vram.c`<br>`gfx/gfx_scroll.c`<br>`exec/surface_query.c`<br>`exec/lease.c`<br>`exec/access_walk.c`<br>`exec/redir_access.c`<br>`kernel/shlib.c`<br>`kernel/paging.c`<br>`kernel/pgalloc.c`<br>`kernel/sys.c`<br>`kernel/physmem.c` | [`tools/tests/gfx_kernel_fb_tdd.md`](../tools/tests/gfx_kernel_fb_tdd.md) | [`docs/tasks/v3/TASK_T2D_T2H.md`](tasks/v3/TASK_T2D_T2H.md) | × |
| 122 | `check-gfx-reinit-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_gfx_reinit.py --runner <runner>; 先頭で --mutate` | — | [`tools/tests/gfx_reinit_tdd.md`](../tools/tests/gfx_reinit_tdd.md) | — | × |
| 123 | `check-gfx-attach-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_gfx_attach.py --runner <runner>; 先頭で --mutate` | `gfx/gfx_core.c`<br>`gfx/backend_pc98.c`<br>`gfx/backend_pegc.c`<br>`gfx/backend_cirrus.c`<br>`gfx/gfx_vram.c`<br>`gfx/gfx_scroll.c`<br>`exec/surface_query.c`<br>`exec/lease.c`<br>`exec/access_walk.c`<br>`exec/redir_access.c`<br>`kernel/shlib.c`<br>`kernel/paging.c`<br>`kernel/pgalloc.c`<br>`kernel/sys.c`<br>`kernel/physmem.c`<br>`userland/lib/gfx/libos32gfx_core.c`<br>`userland/lib/gfx/draw/gfx_draw.c`<br>`userland/lib/gfx/draw/gfx_blt.c`<br>`userland/lib/gfx/draw/gfx_surface.c`<br>`userland/lib/gfx/draw/gfx_sprite.c`<br>`userland/lib/gfx/draw/gfx_rotate.c`<br>`userland/lib/gfx/draw/gfx_raster.c`<br>`userland/lib/gfx/draw/gfx_dump.c`<br>`userland/lib/gfx/geom/gfx_fill.c`<br>`userland/lib/gfx/geom/gfx_circle.c`<br>`userland/lib/gfx/geom/gfx_bezier.c`<br>`lib/utf8.c` | [`tools/tests/gfx_attach_tdd.md`](../tools/tests/gfx_attach_tdd.md) | [`docs/tasks/v3/TASK_T2D_T2H.md`](tasks/v3/TASK_T2D_T2H.md) | × |
| 124 | `check-gfx-reattach-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_gfx_reattach.py --runner <runner>; 先頭で --mutate` | — | [`tools/tests/gfx_reattach_tdd.md`](../tools/tests/gfx_reattach_tdd.md) | — | × |
| 125 | `check-unicode-surface-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_unicode_surface.py --runner <runner>; 先頭で --mutate` | `gfx/gfx_core.c`<br>`gfx/backend_pc98.c`<br>`gfx/backend_pegc.c`<br>`gfx/backend_cirrus.c`<br>`gfx/gfx_vram.c`<br>`gfx/gfx_scroll.c`<br>`exec/surface_query.c`<br>`exec/lease.c`<br>`exec/access_walk.c`<br>`exec/redir_access.c`<br>`kernel/shlib.c`<br>`kernel/paging.c`<br>`kernel/pgalloc.c`<br>`kernel/sys.c`<br>`kernel/physmem.c`<br>`userland/lib/gfx/libos32gfx_core.c`<br>`userland/lib/gfx/draw/gfx_draw.c`<br>`userland/lib/gfx/draw/gfx_blt.c`<br>`userland/lib/gfx/draw/gfx_surface.c`<br>`userland/lib/gfx/draw/gfx_sprite.c`<br>`userland/lib/gfx/draw/gfx_rotate.c`<br>`userland/lib/gfx/draw/gfx_raster.c`<br>`userland/lib/gfx/draw/gfx_dump.c`<br>`userland/lib/gfx/geom/gfx_fill.c`<br>`userland/lib/gfx/geom/gfx_circle.c`<br>`userland/lib/gfx/geom/gfx_bezier.c`<br>`lib/utf8.c`<br>`exec/system_surface.c` | — | — | × |
| 126 | `check-h3-park-resume-host` | `python3 -B tools/tests/test_h3_park_resume.py --mutate` | `tools/h3_park_resume.py`<br>`tools/h3_layout.c`<br>`userland/tests/h3/state.inc`<br>`userland/tests/h3/fixture.inc`<br>`userland/tests/h3a.c`<br>`userland/tests/h3b.c` | — | — | × |
| 127 | `check-cirrus-win-host` | `python3 -B tools/tests/test_cirrus_win.py --mutate` | `gfx/backend_cirrus.c`<br>`drivers/wab_glue_xe10.c` | [`tools/tests/cirrus_win_tdd.md`](../tools/tests/cirrus_win_tdd.md) | — | × |
| 128 | `check-pegc-mode-host` | `python3 -B tools/tests/test_pegc_mode.py --mutate` | `gfx/backend_pegc.c`<br>`kernel/console.c` | [`tools/tests/pegc_mode_tdd.md`](../tools/tests/pegc_mode_tdd.md) | [`docs/tasks/realhw/TASK_PEGC480_REALHW.md`](tasks/realhw/TASK_PEGC480_REALHW.md) | × |
| 129 | `check-display-cleanup-host` | `python3 -B tools/tests/test_display_cleanup.py --mutate` | `kernel/v86_io.c`<br>`kernel/v86_gcap.c`<br>`kernel/boot_splash.c`<br>`gfx/gfx_core.c` | [`tools/tests/display_cleanup_tdd.md`](../tools/tests/display_cleanup_tdd.md) | [`docs/tasks/v3/TASK_T2D_T2H.md`](tasks/v3/TASK_T2D_T2H.md) | × |
| 130 | `check-h2-fixtures-host` | `python3 -B tools/tests/test_h2_fixtures.py --mutate` | `userland/tests/h2_stack.c`<br>`userland/tests/h2_stack_probe.inc`<br>`tools/gen_h2_fixtures.py` | — | — | × |
| 131 | `check-kcg-boot-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_kcg_boot.py --runner <runner>; 先頭で --mutate` | — | [`tools/tests/kcg_boot_tdd.md`](../tools/tests/kcg_boot_tdd.md) | [`docs/tasks/v3/TASK_T2D_T2H.md`](tasks/v3/TASK_T2D_T2H.md) | × |
| 132 | `check-shm-user-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_shm_user.py --runner <runner>; 先頭で --mutate` | — | [`tools/tests/shm_user_tdd.md`](../tools/tests/shm_user_tdd.md) | — | × |
| 133 | `check-ci-stab-host` | `python3 -B tools/tests/test_ci_stab.py` | — | — | — | × |
| 134 | `check-kapi-ranges-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_kapi_ranges.py --runner <runner>; 先頭で --mutate` | `sdk/gen_kapi.py`<br>`exec/exec.c`<br>`exec/ring3_str.c`<br>`exec/access_walk.c`<br>`exec/redir_access.c` | — | — | × |
| 135 | `check-disk-auth-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_disk_auth.py --runner <runner>; 先頭で --mutate` | — | — | — | × |
| 136 | `check-guest-acceptance-host` | `python3 -B tools/gen_guest_acceptance.py --check`<br>`python3 -B tools/tests/test_guest_acceptance.py --mutate` | — | — | — | × |
| 137 | `check-kcallback-host` | `HOST32_RUNNERS ごと: python3 -B tools/tests/test_kcallback.py --runner <runner>; 先頭で --mutate` | — | — | — | × |
| 138 | `check-build-no-deploy` | `python3 -B tools/tests/test_build_no_deploy.py` | — | — | — | × |

## 3. `check` の列に**入っていない** `check-*` ターゲット (9)

門ではなく計測器・実機試験。`make check` からは呼ばれない。

| # | ターゲット | コマンド | 対象ソース | 記録 (RED→GREEN) | 票 | CI |
|---|---|---|---|---|---|---|
| 1 | `check-arm-compile` | `python3 tools/check_arm_compile.py` | — | — | — | × |
| 2 | `check-guest` | `python3 tools/guest_tests.py` | — | [`tools/tests/guest_tests_tdd.md`](../tools/tests/guest_tests_tdd.md) | [`docs/archive/test/TASK_TEST_RUNNER.md`](archive/test/TASK_TEST_RUNNER.md) | ○ |
| 3 | `check-net-l0` | `python3 tools/net_l0_test.py` | — | — | — | × |
| 4 | `check-net-l1` | `python3 tools/net_l1_test.py` | — | — | — | × |
| 5 | `check-net-l2` | `python3 tools/net_l2_test.py` | — | — | — | × |
| 6 | `check-net-l3` | `python3 tools/net_l3_test.py` | — | — | — | × |
| 7 | `check-net-m2` | `python3 tools/net_m2_test.py` | — | — | — | × |
| 8 | `check-net-m2-cpl3` | `python3 tools/net_m2_test.py --during-cmd "less /etc/profile" --wait-for "/etc/profile" --exit-key q --during-timeout 30` | — | — | — | × |
| 9 | `check-net-m4` | `python3 tools/net_m4_test.py` | — | — | — | × |

## 4. `make check` の外にある試験

<!-- manual:outside -->
`check-*` ターゲットを持たない試験。自動ゲートではないが、契約を守っているのはこちら
という場面がある。

| 名前 | 種類 | 何を守っているか | いつ走る |
|---|---|---|---|
| [`kernel/kselftest.c`](../kernel/kselftest.c) | 起動時自己試験 | カーネルが**実際に使う**プリミティブ (`kstring_asm.asm` / `kmalloc.c` / `kprintf`) の境界ケース。外部プログラムの `klibc_test` は newlib を測るので代替にならない。結果は `kselftest_pass` / `kselftest_fail` に残り `kernel.map` 経由で読む | 毎起動 (実機) |
| `link_selftest()` ([`drivers/lgy98.c`](../drivers/lgy98.c)) | 起動時自己試験 | LGY-98 リンク層の HELLO + PING/PONG。**`make kernel-lgy98-link` (LGY98_FLAG_LINKTEST) でビルドしたカーネルでのみ**走る。判定は `tools/net_l0_test.py` が `link_*` グローバルを `/api/mem` で読む | LINKTEST 版の毎起動 |
| [`tools/gui_gate.py`](../tools/gui_gate.py) | ゲスト観測 (判定は人) | GUI のゲート操作列 (`v11` / `v12g1` / `v12g4` / `shot` / `click`)。**自動照合は `wab_relay` / `scrn_ymax` / `fault_generation` の 3 数値だけ**、他は PM が目で見る | 手動 |
| `tools/emu_agent/tasks/regress.txt` | ゲスト観測 (ローカル AI) | 配備後の定型回帰 — kselftest カウンタ / `klibc_test` / `alloc_demo` / `ring3_fault` で落ちてもシェルが生きている / パイプライン 2 本 / screenshot。**モデルは観測を転記するだけで合否は人** | 手動 (スキル `os32-local-ai`) |
| `tools/emu_agent/tasks/v86_dos.txt` | ゲスト観測 (ローカル AI) | V86 の DOS 起動。`dos5hd.nhd` / master は 2026-09-10 に削除済みで、再作成は FORMAT から 30 分 | 手動 |

CI の静的ゲート (`.github/workflows/check.yml`、os32-v3 で 2026-09-30 に作り直し) が走らせるのは
上の表の「CI ○」の `make <target>` (yml の行から生成) と、CI 専用の「生成物が `sdk/kapi.json` と
一致するか」(`gen_kapi.py` / `kapi_rust_gen.py` を回して `git diff --exit-code` で 5 ファイル)。
クロスコンパイラと rustc が無い状態で rc=0 になる検査だけを載せているので、末尾で `i386-elf-gcc` を
使うホスト試験 (R6) と rustc が要るもの (R7) は本体ビルドの `build.yml` の `make check-fast` が回す
([08_build.md §8-6](08_build.md))。**`check.yml` の成功を GUI やカーネルの動作確認とは扱わない**。
<!-- /manual:outside -->

## 5. 分類 (重複 / 不要 / 弱い / 穴)

<!-- manual:findings -->
2026-09-14 の読み取り専用調査 (基点 `fac0d89`) の結論。**採否は未決**で、PM / ユーザーが
決める前提の材料。根拠の行番号はその時点のもの。

### (a) 重複 — 同じ契約を 2 か所以上で守っている

| a | 重複 | 根拠 | 残す案 |
|---|---|---|---|
| **a1** | `multiapp_model_host.c` と `multiapp_impl_host.c` | `test_multiapp_impl.py` の docstring:「K5a の `multiapp_model_host.c` は状態機械を**手で書いた**模型。こちらは**出荷するカーネルのソース**をコンパイルして**同じ番号の検査**に掛ける」。ケース関数名を突き合わせると model の 19 件中 **17 件が impl に同名で存在**。impl のみ 10 件、model のみ 2 件 (`case_launch_pending_parks` / `case_wait_key_joins_the_round`) | **impl を残す**。model 固有の 2 件を impl へ移してから model (1,438 行 / 62KB) を撤去する。設計文書としての価値は `multiapp_model_tdd.md` に残る |
| **a2** | `check-term-model` / `check-term-render` の `cargo test` と直後の `cargo check --lib` | `cargo test` が同じクレートを既にビルドする。差は `cfg(test)` の有無だけ | `cargo check --lib` を落とす。「test 無しでも lib が通る」を本当に見たいなら残す判断もある — **PM/ユーザー決裁** |
| **a3** | ページング系ハーネスの重複 (試験内容ではなく**仕掛け**) | `test_paging_bounds.py` / `test_app_band_pde.py` / `test_highram_stage.py` / `test_memory_boot.py` が `kernel/paging.c` の**同じ 5 行の特権 asm 置換**を各自コピーしている | 試験そのものは別契約なので統合しない。置換だけを 1 つのヘルパへ |
| **a4** | `kselftest.c` の `test_con_sink` / `test_kbd_inject` / `test_launch` / `test_tramp_user_str` / `test_db_v50` / `test_app_band_pde` と、対応するホスト試験 | グループ名とホスト試験の対象ファイルが一致 | **重複として扱わない**。ホスト試験は網羅、kselftest 側は**煙試験**で、実機で毎起動踏むという別の価値がある。ただし 1 表明のものは (c4) 参照 |

### (b) 不要 — 守っている契約がもう無い / 到達しない

| b | 対象 | 根拠 | 判断 |
|---|---|---|---|
| **b1** | `tools/tests/paging_rebuild_host.c` (39 `CHECK()`、5 モード) が `make check` から**到達しない** | `test_paging_bounds.py` は `--rebuild` を `choices=[nonmaster, rollback, sparse, attrs, final]` で受け、付いたときだけ `paging_rebuild_host.c` を選ぶ。`check-memory-host` は引数なしで呼ぶ | 「不要」ではなく**死んだ被覆**。5 モードを足すか、意図的に手動なら票に書く。**PM 決裁** |
| **b2** | `check-privileged` が構造上落ちない | findings があっても `--strict` でなければ `return 0`。既定の根拠文は「**現在 userland は CPL=0 で動作**。リング3 導入時に `--strict` でゲートする」。しかし `CLAUDE.md` は「External programs run at CPL=3 with their own page directory」と書いており、リング3 は既に入っている | 根拠文が現状と矛盾。`--strict` へ上げる / `make check` から外す / 現 offender を票に書いて例外表を持つ、のいずれか。**PM 決裁** |
| **b3** | 撤去済み機能の試験 — **見つからなかった** | T5b (常駐表示パネル) は `1c98613` で撤去済みだが `wm_tests.rs` に `panel` の語は 0 件。hermes の語は `tools/` `userland/` のコードに 0 件。`t5a_display` も生きた依存 | **撤去候補なし** |

### (c) 弱い — 構造ガードだけで振る舞いを見ていない

| c | 対象 | 何が弱いか | 振る舞いの試験にするには |
|---|---|---|---|
| **c1** | [`tools/tests/test_gui_button_dispatch.py`](../tools/tests/test_gui_button_dispatch.py) | docstring が自認:「**挙動試験ではない。**」中身は `arm.find("widget::on_button_down") > arm.find("MOUSE_BTN_LEFT")` という**文字位置の比較**。`if` を `match` に書き換える / ガードを別関数に括り出す / 定数名を変える、のどれでも黙って通る (または黙って落ちる) | `libos32gui` には既に `host_tests/` クレートがある。`app.rs` の `on_event` を `#[path]` で取り込み、`GuiEvent { kind: GUI_EV_BUTTON, button: MOUSE_BTN_RIGHT }` を 1 つ流して贋物で数える。2〜3 ケースで置き換えられる |
| **c2** | [`tools/tests/test_filer_normalize.py`](../tools/tests/test_filer_normalize.py) の 3 件目 | docstring は「両実装を**実ソースから抜き出して**ホストでビルドし突き合わせる」と書くが、**ビルドして走るのは Rust 側だけ**。C 側は `fs/vfs.c` の窓を切って 3 つの文字列を `assertIn` するだけで、空白 1 つ変えれば落ち、`.` の畳み方を変えても文字列が残れば通る | `vfs_resolve_path` は純関数。`test_vfs_mount_dev.py` と同じやり方で `fs/vfs.c` を `#include` し、Rust 側と**同じ 10 ケース**を C でも回す |
| **c3** | [`tools/tests/test_hsync_protect.py`](../tools/tests/test_hsync_protect.py) | docstring:「**実体規則 (`sys_stat` の inode 比較) は `hsync.c` 側にあり、ここではコンパイルが通ることだけを見る**」。72 表明はすべて字句正規化と名前規則に掛かっており、最後の砦 (inode 一致判定) は 1 表明も無い | inode 比較部分を `.inc` に切り出し、`sys_stat` の贋物 (同 inode / 別 inode / stat 失敗) を 3 通り差し替えて「truncate に進むか進まないか」を見る |
| **c4** | `kernel/kselftest.c` の 1 表明グループ 4 本 — `test_kprintf` / `test_ring3_pd` / `test_map_user_keep` / `test_app_band_pde` | それぞれ `check()` 1 回。実質「その経路が呼べて落ちなかった」の確認で、境界も反例も踏んでいない | `test_app_band_pde` はホスト側が 107 表明で覆っているので実機側は「境界の 1 点だけ」で妥当とも言える。**削除でも増強でもなく、意図の明文化**が要る |

### (d) 守るべきなのに試験が無い契約 (目についたもの。網羅ではない)

| d | 契約 | 現状 |
|---|---|---|
| **d1** | `hsync` が実体 (inode) で `settings.db` を守る — 契約 D0 の最後の砦 | ホスト試験なし (c3 と同じ穴) |
| **d2** | `st_dev` / `st_ino` をゲストで観測する | 観測手段が無い。`vfs_mount_dev_host.c` の 2 ケースがホストだけの根拠 |
| **d3** | G9 — 2 本以上のアプリを同時に ready にしたときの pick | **実機観測の手段が無く未実施**。ホスト模型を受入とした |
| **d4** | `libos32gui` のウィジェット描画と WM 配送 | ホスト試験は `cfgro` wrapper だけ。描画・ジャンプ表・WM は「動かさない」と `Cargo.toml` が明記。Button 配送は構造ガード 1 本 (c1) |
| **d5** | ネットワーク L0〜L3 / M2〜M4 の回帰 | `make check` にも CI にも無く、`host_agent.py` の起動 + LINKTEST カーネルの配備 + 実機が要る。合否は各スクリプトの `RESULT:` 行 |
| **d6** | T5b 撤去で消えた 4 本の書き直し | `wm_tests.rs` には既に 4 本が揃っている。**残件記述が古い可能性が高い** — PM が確認して閉じるのが先 |
<!-- /manual:findings -->

## 6. 改善提言

<!-- manual:recommend -->
2026-09-14 の調査が出した案。**採否は未決** (PM / ユーザー決裁)。

### 削除候補

| 案 | 対象 | 根拠 | 前提 |
|---|---|---|---|
| **R1** | `tools/tests/multiapp_model_host.c` (1,438 行 / 62KB) と `check-multiapp-model-host` からの `test_multiapp_model.py` | (a1)。19 ケース中 17 ケースが `multiapp_impl_host.c` に**同名で**存在し、impl は出荷する `exec/appslot.c` をそのまま走らせる | 先に model 固有の 2 件を impl へ移す。設計の記録は `multiapp_model_tdd.md` に残る |

**削除を勧めないもの**: T5b / hermes 由来の残骸は**見つからなかった** (b3)。
`t5a_display` も `libos32term` / `libos32term_render` も生きた依存。

### 統合候補

| 案 | 対象 | 根拠 |
|---|---|---|
| **R2** | `check-term-model` / `check-term-render` の `cargo check --lib` を落とす | (a2)。直前の `cargo test` が同じクレートをビルドする |
| **R3** | ページング系 4 本の特権 asm 置換を 1 つのヘルパへ | (a3)。**試験は統合しない** — 守る契約が別々 |

### `make check` から外して手動に落とす候補

| 案 | 対象 | 根拠 |
|---|---|---|
| **R4** | SQLite の amalgamation を毎回コンパイルする 4 本 (`check-cfg-host` / `check-db-v50-host` / `check-install-recover-host` / `check-sqlite-groups-host`) を `make check-slow` のような別ターゲットへ分ける | `lib/sqlite3/sqlite3.c` は **267,757 行**を 4 回。**実行時間は測っていない**ので、分割の是非は PM が 1 回計測してから決める |
| **R5** | NP21/W ホスト道具の 4 本 (`test_np21w_ini` / `_ini_live` / `_trial` / `_transport`) を `check-emu-tools` として分離 | `check-tools-host` は OS32 の契約と NP21/W の ini 道具 ([D2] の PM 専用道具) を同じターゲットに混ぜている。**分離の是非も計測次第** |

### CI に足すべきもの

| 案 | 対象 | 根拠 | 注記 |
|---|---|---|---|
| **R6** | rustc **不要**の C / Python ホスト試験を CI へ (`check-con-sink-host` `check-kbd-inject-host` `check-launch-host` `check-ring3-str-host` `check-sh-launch-host` `check-sh-shell-host` `check-vfs-fd-sqlite-host` `check-vfs-mount-dev-host` `check-db-owned-host` `check-settings-protect-host`) | カーネルの契約 (con_sink / kbd_inject / launch / ring3_str / FD リース / mount エンコード) は**どれも CI で走っていない**。これらは `gcc` と `python3` だけで動く | **ただし全部が末尾で `i386-elf-gcc` のクロスコンパイル確認をする**。CI にクロスコンパイラが無いので、**そのステップだけ切り離せる形にする改修が要る** |
| **R7** | `check-gshell-host` / `check-gui-host` / `check-t5a-host` / `check-term-*` を CI に足す | **rustc が要る**。「CI 成功を GUI の動作確認とは扱わない」 | `actions/setup-rust` 系を 1 ステップ足せば届く。`test_filer_normalize.py` は `rustup toolchain list` の**先頭**を使うので、CI では toolchain が 1 本であること |

### 被覆を足す候補

| 案 | 対象 | 根拠 |
|---|---|---|
| **R8** | `paging_rebuild_host.c` の 5 モードを `check-memory-host` に足す (または「手動専用」と票に書く) | (b1)。39 表明が現在どの自動ゲートからも到達しない |
| **R9** | `test_gui_button_dispatch.py` を `libos32gui/host_tests` の贋物試験へ移す | (c1)。足場は既に `host_tests/src/fake.rs` にある |
| **R10** | `hsync` の inode 比較を `.inc` に切り出してホスト試験へ | (c3) / (d1)。契約 D0 の最後の砦が現在ノーガード |

### 文書の整理

| 案 | 対象 | 根拠 | 状態 |
|---|---|---|---|
| **R11** | [`gui/v13/PLAN.md`](tasks/gui/v13/PLAN.md) の「残件: パネルに依存しない形で書き直す」を確認して閉じる | (d6)。`wm_tests.rs` に 4 本が既にある | 未決 |
| **R12** | 参照されていない `*_tdd.md` を対応する試験から 1 行リンクする | 削除ではない。証跡としては有効なのに、どこからも名前で引かれていなかった | **大部分は解消**。上の表の「記録」列が語幹の一致 (規則 3) で引くので、`app_band_pde` / `boot_splash_native` / `device_reservation` / `highram_stage` / `memory_boot` / `paging_bounds` / `pgalloc_range` / `pgalloc_model` がターゲットから辿れる。§ 追記も参照 |

**上の表に出てこない `*_tdd.md` (49 本中 10 本、2026-09-15 時点)**

| ファイル | 出てこない理由 |
|---|---|
| `gui_review_20260910_tdd.md` / `gui_review3_…` / `gui_review4_…` | 対応する `check-*` ターゲットが無い (レビュー往復の記録で、自動試験に紐づいていない) |
| ~~`physmem_synthetic_source_tdd.md`~~ | 2026-09-16 に `physmem_tdd.md` へ改名して解消 (語幹 `physmem` で `test_physmem.py` に当たる) |
| `gshell_buttonup_tdd.md` / `k5b_gshell_tdd.md` | `check-gshell-host` は `integration.py` を呼ぶだけで、語幹も `記録:` も持たない |
| `k6_ram_tdd.md` | K6-RAM の記録。`check-memory-host` の `test_highram_stage.py` が持つのは `highram_stage_tdd.md` のほう |
| `s4_tdd.md` | 票 S4-W は `check-gshell-host` (Rust) の側。上と同じ理由 |
| `s5_tdd.md` / `t8_tdd.md` | それぞれ `test_cfg.py` / gshell の中で触れられているが、冒頭 40 行の外なので規則 1 に掛からない |

**直し方は 2 つとも規則の側ではなく情報源の側**: 試験スクリプトの冒頭 40 行に
`記録: tools/tests/<x>_tdd.md` を 1 行足すか、`build/*.mk` のターゲット直前の
コメントに書く。どちらも生成器が拾う。試験本体には手を入れていないので、この
10 本は**このコーダーの範囲では未解消**として残す ([V4])。
<!-- /manual:recommend -->

## 7. 未読 / 調べていないこと

<!-- manual:unread -->
2026-09-14 の調査が**読んでいない**もの。表の空欄と混同しないこと。

- **`apps/` と `game/`** — サブモジュールで、調査した worktree では空だった。
  「`apps/` の filer のホスト試験」は読めていない。in-tree の
  `userland/rust/filer/host/model_tests.rs` (3 件) は読んだ。
- **実行時間** — 1 本も実行していないので全て未計測。R4 / R5 の判断には実測が要る。
- **`test_deploy_protect.py` の中身** — クラス名だけ読んだ。8 クラスに対して
  `ReviewB1`〜`ReviewB9` / `Review2`〜`Review5` と **6 回のレビュー往復ぶんの層**が
  重なっている (35 クラス)。同じ配備 4 系統を何度も踏んでいる可能性があるが、
  **本文を読んでいないので重複とは断定しない**。持ち主が読んで判断すべき最有力候補。
- **`cfg_host.c` (3,778 行 / 1,173 表明) の中身** — ケース名リストは読んだが本文は未読。
  この表で最大の試験なので、重複の有無は別途。
- **`docs/tasks/**/TASK_*.md` の受入条件の全件突き合わせ** — (d) は「目についたもの」だけで、
  票の受入条件を端から照合してはいない。
<!-- /manual:unread -->
