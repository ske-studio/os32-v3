# OS32 開発案内 — ファイル地図

> 役割 (2026-09-05 に再定義): **「何をしたいか」から「どこを読み、どこを触り、どう検証するか」へ
> 案内する**文書。仕様・規則・番地・手順の本文はここに置かない (正典は
> [INDEX.md「情報単位ごとの正典」](INDEX.md) の表)。ここに書くのは参照先と、
> ファイルと役割の対応だけ。

---

## 1. 作業別の参照先

[WORK_GUIDE.md](WORK_GUIDE.md) (索引の文書、2026-10-23 にここから切り出した)。

## 2. ファイル地図 (どのファイルが何で、仕様はどこか)

### カーネル `kernel/`

| ファイル | 役割 | 仕様 |
|---|---|---|
| `kernel.c` `kentry.asm` | 起動、シェル起動ループ (CUI `/sys/shell.bin` ⇄ GUI `/bin/gshell.bin` を `gui_take_next_shell()` で往復)、`/etc/system.cfg` の反映 | [01 §1-2](01_system.md) |
| `sysconfig.c` | `/etc/system.cfg` の解析 (`GUI=0/1`、`GFX=pc98\|pegc\|cirrus\|auto`) | [01 §1-2](01_system.md)、archive/gui_v11/TASK_K4 (アーカイブ: `docs/archive/gui_v11/TASK_K4_gui_boot.md`) |
| `gui.c` | GUI の背骨: `gui_call` (アプリ → WM の唯一の入口)、`gui_register`、所有者回収 `gui_owner_exit`、次シェル要求 | 票 `tasks/gui/API_CONTRACTS.md` T1〜T9 |
| `shlib.c` | 共有ライブラリ帯 0x80000000〜 (高位 VA、AS ごと) のロード (`OS32ShlibHeader`) と、アプリ PD ごとの .data/.bss 複製 (`shlib_addrspace_attach`) | [02 §2-1](02_memory.md)、[09](09_exec.md)、archive/gui_v11/TASK_K3 (アーカイブ: `docs/archive/gui_v11/TASK_K3_shared_lib_band.md`) |
| `paging.c` `pgalloc.c` | ページテーブル (現行実装: 静的な恒等写像 32MB、それより上の RAM は検出量だけ動的に写像、RAM の登録上端 2GB。設計上限ではない — 02_memory.md §2-1)、ガードページ、物理ページ確保。PD はプログラムごと (v2 M1)。デバイス窓は `paging_map_phys` (supervisor+PCD)、CLIENT は lease の私有 VA、共有 PT の通常 map/unmap は拒否 | [02](02_memory.md)、`docs/archive/kernel_v2/M1_RING3.md` |
| `kmalloc.c` | カーネルヒープ (320KB) | [02 §2-1](02_memory.md) |
| `shm.c` | 共有メモリの割当と DB / GUI の予約。帯とブロック数は `include/memmap.h`、配置の説明は [02 §2-1](02_memory.md) | [02](02_memory.md)、票 `tasks/gui/API_CONTRACTS.md` T2 |
| `idt.c` `isr_*.c` | 割り込み、`int 0x80` KAPI トランポリン着地点 | [04](04_interrupts.md)、archive/kernel_v2/M2 (アーカイブ: `docs/archive/kernel_v2/M2_KAPI_TRAMPOLINE.md`) |
| `ime.c` `ime_romkana.c` `ime_dict.c` `ime_render*.c` | FEP。描画は関数表 (`ime_render.h`) 越し。GUI は `ime_feed_key` / `ime_set_render` (KAPI v42) で WM が FEP を持つ | 票 `tasks/fep/00_INDEX.md`、archive/gui_v11/TASK_W2 (アーカイブ: `docs/archive/gui_v11/TASK_W2_fep_lease_modal.md`) |
| `ring3_entry.asm` | `int 0x80` の入口 (`int80_stub`: セグメント復元後 `sti`、出口 `cli`)、`kapi_invoke` | [09](09_exec.md)、[POLICY_DEBUG §4-19](POLICY_DEBUG.md) |
| `console.c` | TVRAM 出力、スクロール予約 (`tvram_set_scroll_reserve`)、GDC カーソル | [05 §5-9](05_drivers.md) 周辺、[POLICY_DEBUG §4-18](POLICY_DEBUG.md) |
| `con_sink.c` | console シンク — GUI モード中のカーネル出力を 8KB のリングに溜め、端末アプリが `con_sink_read` (KAPI v46) で吸う | [KAPI_SPEC v46 節](KAPI_SPEC.md)、`docs/archive/gui_v13/TASK_K6C_console.md` |
| `kbd_inject.c` | 打鍵の注入リング (256B) — GUI モード中、端末アプリが `kbd_inject` (KAPI v47) で注ぎ、`kbd_getchar` は第 2 の park 点になる。注げるのは con_sink の読み手だけ | [KAPI_SPEC v47 節](KAPI_SPEC.md)、`docs/archive/gui_v13/TASK_K7_input.md` |
| `snd_engine.c` | FM/SSG シーケンサ (タイマ IRQ 駆動) | [05 §5-3](05_drivers.md) |
| `v86*.c` | V86 モニタ、仮想 PIC、キー所有権、脱出キー | archive/v21/v86v2/ (アーカイブ: `docs/archive/v21/v86v2/README.md`) |
| `kselftest.c` | ブート時セルフテスト (kstring / kmalloc / kprintf)。プリミティブを触ったら項目を足す | [POLICY_DEBUG §2](POLICY_DEBUG.md) |
| `module.c` / `module_image.c` / `module_runtime.c` | 部品の検証・固定 record・load/start/abort と隔離の核、および `/boot/kdebug.bin` の接続 (KDEBUG=1 のときだけ、root マウント後)。部品の中身は `kdebug/` | 票 TASK_T4_T6_MODULES §2-2、票 TASK_DBGREF |
| `irq.c` `irq_math.c` | 割り込みの動的登録 (`irq_register`、共有 IRQ、storm の検出) — v2.1 | 票 TASK_HAL_WIRING §1-1 |
| `dma_pool.c` `dma_pool_math.c` | DMA プール (0x7F0000 の 64KB 整列の固定区間。64KB 境界をまたがない配り方、[HW2]) — v2.1 | 票 TASK_HAL_WIRING §1-3、[02 §2-1](02_memory.md) |
| `ktime.c` `time_math.c` | µs 時計 (`ktime_*`、2.4576MHz 系のクランプ) — v2.1 | 票 TASK_HAL_WIRING §1-5 |
| `sysclk.c` `pit_math.c` `cpu_calibrate.c` `cpu_calibrate_math.c` | システムクロックの判定 (1.9968 / 2.4576MHz、`0000:0501h` bit7) → PIT の分周、CPU の校正 — v2.1 | [POLICY_DEBUG §4-54](POLICY_DEBUG.md) |
| `bootinfo.c` `bootinfo_check.c` | ローダから受け取る起動情報 (起動元、イメージの CRC `VK32` → `ver` の `Image CRC`) — v2.1 | archive/realhw_v21/TASK_HDD_INSTALL (アーカイブ: `docs/archive/realhw_v21/TASK_HDD_INSTALL.md`) 段 0 |
| `bootlog.c` `bootlog_save.c` | 起動ログ `/var/log/boot.log` (前回分は `.1`) — v2.1 | [RELEASE_v2.1 §1](RELEASE_v2.1.md) |
| `v86_gcap.c` `v86_gcap_math.c` `v86_test16_gcap.asm` | `v86 -g`: 実機の ROM が INT 18h AH=30h/31h で流す I/O (OUT 列) を記録 — v2.1 | 票 TASK_PEGC480_REALHW §3 段 1、[POLICY_DEBUG §4-62](POLICY_DEBUG.md) |

### 実行と KernelAPI `exec/` `kapi/` `sdk/`

| ファイル | 役割 | 仕様 |
|---|---|---|
| `exec/exec.c` `exec_heap.c` | OS32X ローダ (ヘッダ v4・60B)、子プロセス帯の動的レイアウト、Ring3 AS の USER 写像 (プログラム帯 / スタック / SHM / shlib / lease 窓。低位 VRAM・フォントは supervisor、KAPI / lease 経由)、`exec_launch_abort` (起動失敗の巻き戻し)、所有者別の資源回収、CTRL+STOP (`ring3_abort_*`)、syscall 境界ポンプ (`ring3_gui_pump`)、`exec_run` の setjmp 分割壁 | [09](09_exec.md) |
| `exec/exec_kapi_init.inc` | KAPI 表の初期化 (生成物) | [KAPI_SPEC §3-1](KAPI_SPEC.md) |
| `kapi/kapi_generated.c` `kapi_sys.c` `kapi_db.c` | `__cdecl` ラッパー (生成 + 手書き) | 同上 |
| `sdk/kapi.json` | **KAPI の正典** | 同上 |
| `sdk/include/os32/os32_kapi_shared.h` | 構造体・`KAPI_VERSION`・`OS32_ERR_*` (エラーコードの正典)・OS32X ヘッダ v4・60B・`OS32ShlibHeader` | 同上 |
| `sdk/include/os32/os32_kapi_slots.h` | `KAPI_SLOT_*` = `int 0x80` のスロット番号 (生成物、型非依存) | [KAPI_SPEC §3-1](KAPI_SPEC.md) |
| `sdk/include/os32/os32_gui_shared.h` `sdk/rust/os32api/src/gui/` | GUI の op / イベント / SHM レイアウト (C と Rust に同じ値) | 票 `tasks/gui/API_CONTRACTS.md`、票 `tasks/gui/PROTO_LAYOUT.md` |
| `sdk/link/app.ld` `app_sys.ld` `shlib.ld` `sdk/crt/crt0.asm` | アプリ (私有 VA 0x80100000) / シェル帯 (0x400000、gshell) / 共有ライブラリ (私有 VA 0x80000000) のリンク | [09](09_exec.md)、[02 §2-1](02_memory.md) |
| `sdk/gen_kapi.py` `kapi_rust_gen.py` `mkos32x.py` `tools/mkshlib.py` | KAPI 生成、OS32X ヘッダ付与、`.shlib` 生成と番号表照合 (`make check-shlib`) | [08 §8-4](08_build.md) |

### ドライバ `drivers/`

| ファイル | 役割 | 仕様 |
|---|---|---|
| `fdc.c` `disk.c` `dev.c` `loop_dev.c` | FDC、論理ディスク、デバイス登録簿、ループバック | [05 §5-2, §5-11](05_drivers.md) |
| `ide.c` `atapi.c` | IDE HDD、ATAPI CD-ROM | [06 §6-3](06_filesystem.md)、[05 §5-6](05_drivers.md) |
| `kbd.c` | PC-98 キーボード、修飾キー、V86 への注入。GUI 中は cooked に積まず raw リング (`kbd_trygetrawkey`、`keycode\|down<<8\|mods<<9`)、CTRL+STOP → `ring3_abort_request` | [05 §5-1](05_drivers.md) |
| `serial.c` | RS-232C (rshell / ai-debug の経路) | [05 §5-4](05_drivers.md) |
| `mouse*.c` | バスマウス + NP21/W シームレスマウス (座標は移動範囲へ比例配分、480 ライン可) | [05 §5-7](05_drivers.md) |
| `wab_glue.h` `wab_glue_xe10.c` `wab_cirrus.c` | ウィンドウアクセラレータ: ボードグルー契約 / Xe10 内蔵 (ID 5Bh、0FAAh/0FABh) / CL-GD5430 チップ (BLT は I/O 経由、8bpp、DAC)。定数は `include/wab_xe10.h` | 票 `tasks/gui/DESIGN.md` §6〜§8、archive/gui_v11/TASK_H3 (アーカイブ: `docs/archive/gui_v11/TASK_H3_cirrus.md`) |
| `fm.c` | OPN/OPM | [05 §5-3](05_drivers.md) |
| `kcg.c` | 旧 `.kcgfont` の読込 / KCG facadeへの橋 | [05 §5-9](05_drivers.md) |
| `kernel/kcg_body.c` `kernel/kcg_module.c` | KCG ROM/cache部品 / typed facade | [05 §5-9](05_drivers.md) |
| `np2sysp.c` | NP21/W ハイパーコール | [05 §5-10](05_drivers.md) |
| `lgy98.c` `ne2000.c` `ne2000_ring.c` `ne2000_io.asm` `ne2000_regs.h` | LGY-98 (NE2000 互換 C バス LAN)。カード固有 / 8390 本体 / リング計算 (ホスト試験可) / 16bit PIO / レジスタ定数の正典。既定は有効 (カード未装着時は起動継続)。無効化は `make kernel-nolgy98` (`build/config.mk`) | 票 `tasks/network/PLAN.md` |
| `pci.c` `pci_decode.c` `pci_bind.c` `pci_bind_match.c` | PCI の列挙 (メカニズム #1)・構成空間のデコード・結線表 (ドライバとの結び付け)、`lspci -v` — v2.1 | 票 TASK_LAN_82557 (L-A)、票 TASK_HAL_WIRING §1-4 |
| `dma8237.c` `dma8237_math.c` | 8237 DMA の共通部 (`dma_chan_setup`、auto-init) — v2.1 | 票 TASK_HAL_WIRING §1-2 |
| `pcm_cs4231.c` `pcm_cs4231_math.c` | CS4231 (MATE-X PCM) の PCM 再生 (リングは DMA プール、IRQ は動的登録) — v2.1 | 票 TASK_PCM_CS4231 |
| `pc98pt.c` `ide_addr.c` | PC-98 の区画表の読み書き (HDD インストール) / ATA のセクタ指定の選択と範囲検査 — v2.1 | archive/realhw_v21/TASK_HDD_INSTALL (アーカイブ: `docs/archive/realhw_v21/TASK_HDD_INSTALL.md`) |
| `fdc_track.c` `fdc_decide.c` | FD のトラック読みと 8 セクタの LRU / 時間上限の判定 (機構の最悪値から) — v2.1 | archive/realhw_v21/TASK_FDC_REALHW (アーカイブ: `docs/archive/realhw_v21/TASK_FDC_REALHW.md`)、[POLICY_DEBUG §4-51](POLICY_DEBUG.md) |
| `kbd_status.c` `kbd_dlog.c` | キーボード 8251 の状態 (`kbdstat`) と make/break の記録 (`kbdstat -w`) — v2.1 | [POLICY_DEBUG §4-57](POLICY_DEBUG.md) |
| `serial_plan.c` | シリアルの整数分周の計画 (115200 まで、クロック判定つき) — v2.1 | archive/realhw_v21/TASK_SERIAL_VFAST (アーカイブ: `docs/archive/realhw_v21/TASK_SERIAL_VFAST.md`)、[POLICY_DEBUG §4-49](POLICY_DEBUG.md) |

### ファイルシステム `fs/`

| ファイル | 役割 | 仕様 |
|---|---|---|
| `vfs.c` `vfs_fd.c` | VFS、FD 表、所有者タグ・保護 FD | [06 §6-1](06_filesystem.md) |
| `ext2_*.c` | ext2 読み書き (NHD の主 FS) | [06 §6-2](06_filesystem.md) |
| `fatfs/` `fatfs_vfs.c` | FatFs (唯一の FAT 実装) | [06 §6-8](06_filesystem.md) |
| `hostdrvfs.c` | HostDrv (`/host`) | [06 §6-7](06_filesystem.md) |
| `pipe_buffer.c` `fd_redirect.c` | パイプ・リダイレクト | [06 §6-4, §6-5](06_filesystem.md) |
| `iso9660.c` | CD-ROM | [06 §6-6](06_filesystem.md) |
| `serialfs.c` `serialfs_session.c` `sfs_client.c` `sfs_proto.c` | SerialFS — シリアル越しの `/host` (ホスト側は `tools/serialfs_host.py`)。`hsync --root` と組んで実機の HDD を更新する — v2.1 | archive/realhw_v21/TASK_SERIAL_HOSTFS (アーカイブ: `docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md`) |

### グラフィックス `gfx/`、SQLite `lib/sqlite3/`、ブート `boot/`

| ファイル | 役割 | 仕様 |
|---|---|---|
| `gfx/gfx_core.c` `gfx_vram.c` `palette.c` | HAL の入口: バックエンド選択 (probe 順 Cirrus → PEGC → 9801 (auto で Cirrus を試すのは NP21/W の上だけ、Cirrus のリニア窓はデバイス窓の帯 0xFE000000〜 — v2.1)、`GFX=` の強制)、present (ページフリップ)、パレット、カウンタ (`gfx_stats`)、`gfx_bb_phys_range` | [05 §5-5](05_drivers.md) |
| `gfx/backend_pc98.c` `backend_pegc.c` `backend_cirrus.c` `include/gfx_hal.h` | バックエンド表 `GfxBackend` (probe / init / query / present_rect / fill / blit / enter / leave / shutdown) と 3 実装: 9801 4 プレーン (BB は pool から確保した 32 頁 (SURFACE))、PEGC 640×480×256 (F00000h/FFF00000h 窓 (043Bh bit2=1 は後者)、BB は arena 上端の 300KB (gfx_boot_reserve))、Cirrus GD5430 (0xFE000000 リニア窓、表示面 + クライアント面、エンジン BLT) | [05 §5-5](05_drivers.md)、[02 §2-1](02_memory.md)、票 `tasks/gui/DESIGN.md` |
| `lib/sqlite3/os32_sqlite_vfs.c` `os32_sqlite_config.h` `sqlite_stack.asm` | カーネル内 SQLite (core BSS の直後、MEMSYS5 384KB、代替スタック) | archive/v21/sqlite/ (アーカイブ: `docs/archive/v21/sqlite/00_INDEX.md`) |
| `boot/boot_*.asm` `loader_*.asm` `boot_main.c` `ext2_mini.c` `lz4_mini.c` | IPL、第 2 段ローダ (PM 遷移を含む、分割禁止)、LZ4 展開 | [01 §1-2](01_system.md)、[10 §10-2, §10-3](10_notes.md)、archive/v21/boot_reform/ (アーカイブ: `docs/archive/v21/boot_reform/00_OVERVIEW.md`) |

### ユーザ空間 `userland/`

| 場所 | 役割 | 仕様 |
|---|---|---|
| `shell/main.c` `ui.c` `cmd_*.c` `cmd_script.c` `rshell.c` | 常駐 CUI シェル (0x400000, CPL=0)。コマンド登録 (`ShellCmd`, 最大 128)、行編集・補完・履歴、スクリプト、シリアル rshell、`os32gui` / `gfxmode` | [07](07_shell.md) |
| `gshell/` (Rust) | GUI シェル = WM (シェル帯 0x400000 に CUI と入れ替わりで常駐、`/bin/gshell.bin`)。`wm.rs` 窓 / Z 順 / 所有者、`handler.rs` op 表、`input.rs` X3/X4 の入力取り込み (raw キー・FEP 退避・ボタンエッジの領分)、`visible.rs` 可視領域、`fep.rs` (カーネル FEP を `ime_feed_key` で駆動)、`lease.rs` `modal.rs` `timer.rs` `chrome.rs` `cursor.rs` | archive/gui_v11/TASK_W1 (アーカイブ: `docs/archive/gui_v11/TASK_W1_wm_core.md`)、TASK_W2 (アーカイブ: `docs/archive/gui_v11/TASK_W2_fep_lease_modal.md`)、票 `tasks/gui/API_CONTRACTS.md` |
| `rust/libos32gui/` `libos32gui_stub/` | GUI クライアント (G 描画 / `gui_call` / U3 ループ / ウィジェット木 / 箱レイアウト)。`.shlib` として私有 VA 0x80000000 に写像、アプリは stub (ジャンプ表) をリンク | archive/gui_v11/TASK_C1〜C3 (アーカイブ: `docs/archive/gui_v11/TASK_C3_shared_lib.md`) |
| `cmds/` `system/` `tests/` `rust/` | コマンド、システムユーティリティ、テスト (`hal_test` `gdi_test` `ring3_guard` `gui_busy` `lease_test` `gui_bench` …)、Rust (Cargo ワークスペース、`os32api` クレート) | 一覧は各 `deploy.yaml` と [07 §7-1](07_shell.md) |
| `system/cdinst.c` `install.c` `inst_hdd.c` `inst_disk.c` `shell/hdprep_plan.c` | CD からの HDD インストール (段 1・2、他 OS の区画を消す ERASE) — v2.1 | archive/realhw_v21/TASK_HDD_INSTALL (アーカイブ: `docs/archive/realhw_v21/TASK_HDD_INSTALL.md`) |
| `tests/pegcchk.c` `pcm_test.c` `time_test.c` | 実機の確認用: PEGC 640x480 の入り・戻りの状態 (`pegcchk`、受け入れ (C)) / PCM の再生 / µs 時計 (`timetest`) — v2.1 | 票 TASK_PEGC480_REALHW §5、票 TASK_HAL_WIRING |
| `lib/os32` `math` `input` `gfx` `snd` `db` | 基盤ライブラリ (デバッグ出力 / 整数数学 / 入力抽象 / 描画 (4 プレーンと PACKED8 の両経路、`libos32gfx_attach`) / FM・SSG / SQLite ラッパ) | `tasks/lib*/`、[05 §5-5](05_drivers.md) |
| `lib/tilemap` `ui` `filer` `md` `asset` `ecs` `save` | 描画・UI 系 (タイルマップ / microUI (テスト導入) / ファイラ / Markdown / アセット / ECS / セーブ) | 同上 |
| `apps/` `game/` (submodule) | 標準アプリ、対戦スゴロク RPG。ゲームエンジン 11 ライブラリは os32-game が自前ビルド | 各リポジトリの `docs/` |

## 3. 参照

- Claude Code / Codex 共用スキル: [os32-emu-debug](../.claude/skills/os32-emu-debug/SKILL.md) (NP21/W 調査)、[os32-build-verify](../.claude/skills/os32-build-verify/SKILL.md) (変更別ビルド・反映確認)。本文は `.claude/skills/`、Codex の `.agents/skills/` から同じフォルダを参照する。`.agents/` は現行の Git 除外対象なので共有リンクは環境ごとに設置する (例: `.agents/skills/os32-emu-debug` → `../../.claude/skills/os32-emu-debug`)。
- 規則の正典: [CONSTRAINTS.md](CONSTRAINTS.md) (CLAUDE.md は ID で参照。Hermes 用だった SOUL.md は `SOUL.md` (非公開・リポジトリ外、`.gitignore`)、効力なし)
- 成果の履歴: [CHANGELOG.md](../CHANGELOG.md)、`docs/archive/ROADMAP_v1.0.md`。計画: `docs/ROADMAP.md`
- 落とし穴の経緯: [POLICY_DEBUG.md §4](POLICY_DEBUG.md)。短い注意は同じ §4 の冒頭「領域別の早見」
