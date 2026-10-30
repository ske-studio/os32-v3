# TASK_SRC_LAYOUT — カーネルのソース配置を機能ごとに整理する (Linux を手本に)

> 状態: **実装中 (2026-10-30)** — §9 の手順で進行中。**L0 は main** (5e346e2d、sol、astra 2 往復で取り込み可: 成果物 28 本バイト一致・include の解決先 26,485 件一致・走査数一致、全体の検査 rc=0、NP21/W で起動・ver・mem 合格)。**L1 は main** (f25d0785・a4b679a1、terra の移動 + sol の試験の追従 (共通の -I)、成果物の差は検算値の定数だけ、取り込みの検査 rc=0)。手順の正典は [SRC_MOVE](../../SRC_MOVE.md)。L2 (terra) 実装中。それまで: 案 (下の §0〜§8、Opus のサブエージェントが読み取りだけで調べた、基点 b214e751) をユーザーに説明した段階。実装は未着手。
> 上位: [ROADMAP](../../ROADMAP.md)・ソースツリーの図の正典 [08_build](../../08_build.md) §8-3・ファイル地図 [DEVELOPMENT](../../DEVELOPMENT.md) §2。関連: [TASK_REDUNDANCY](TASK_REDUNDANCY.md) (進行中、同じファイル群を触る)・[TASK_DEP_DIR](TASK_DEP_DIR.md) (依存方向)。

## ユーザーの指示と決定 (2026-10-30)

- 発端 (原文): 「カーネルのソースの配置が気に食わないです。機能ごとに整理されていない印象です。linux のソースコードのディレクトリ管理ポリシーを教えてください。」→ PM が Linux の約束 (最上位はサブシステム、共通層と装置の分離、機種依存は arch の中、ディレクトリごとの Makefile、生成物はツリーの外) を説明 →「とりあえず、案だけ作成しておいて」。
- 案の §8「要判断」への答え (原文):「３と４．別で分離計画をたてるので、仮で良い。その際に状況をドキュメント化」
  - **要判断 3 (§3「要判断」表の 15 件の置き場)**: 表の**仮の置き場のまま**でよい。v86・paging・isr_handlers・idt.h などの本当の置き場は、ユーザーが**別に立てる分離計画**で決める。
  - **要判断 4 (-I を足すか、include を根からのパスに書き換えるか)**: **仮に本案 (-I を足す)** で進めてよい。書き換えは同じく別の分離計画で扱う。
- **まだ決まっていないこと**: 要判断 1 (`init/`・`mm/` を最上位に出すか、`drivers/char/` の名)、2 (部品の窓口・本体を装置の隣か `kernel/modules/` か)、5 (PC-98 固有ヘッダは v4、本案では動かさない — PM の推奨)、6 (改名をするか — PM の推奨は「しない、するなら別の段」)、7 (実施の時期 — PM の推奨は進行中の段 (GF_STAGES T4・TASK_REDUNDANCY の C 群) の取り込み後に L0 から)。PM の推奨は本案どおり。
- **実装者と完了条件 (ユーザー了承 2026-10-30「OK」)**: 実装者は **terra** (中身は書かず、対応表 JSON だけを手で書き、`git mv` と参照の置換は道具が行う — 「1,000 行超のファイルは terra に出さない」は編集の決まりで、置き場の移動には当たらない)。段の完了条件は `make all` + **バイナリのバイト比較の一致** (kernel・各 .mod・kdebug・hwdev・kernel-r1、ずれたら objdump で関数ごと) + `#include` の解決先の前後一致 + 走査型の検査の走査ファイル数の前後一致 + `check_select.py --lint` rc=0。**実装者は試験を回さない** (`MUTATE=0` も変異も)。試験・検査のパスの置換漏れは、取り込みの検査 (移動の量なので全体の検査になる) で 1 回だけ見て直す。L0 だけは kernel.map のパスが変わるのでゲストで 1 回起動する。
- 次の一手: ユーザーの分離計画を待つ。それまで実装に出さない。案の数字は b214e751 の時点で、着手の前に数え直す。

以下は案の本文 (基点 `main` b214e751 で数えた値。リポジトリは編集していない)。
対象: `kernel/` (.c 93・.h 70・.asm 7・.inc 2・.def 1)、`drivers/` (.c 38・.h 37・.asm 1)、`fs/` (45)、`gfx/` (15) の追跡ファイル 309 本。
`exec/` `lib/` `kapi/` `net/` `kdebug/` `hwdev/` `platform/` `boot/` は**動かさない** (理由は §2 末尾)。

## 0. 前提と方針

- 手本は Linux の約束 (最上位をサブシステムで分ける、共通層と装置の実装を分け依存は装置→共通、機種依存は arch/platform の中、
  生成物はソースツリーに置かない)。ただし OS32 の規模に合わせ、深さは最大 3 (`drivers/net/ne2000/`)、1 ディレクトリ 2〜22 本。
- **ファイル名 (basename) は変えない。** 動かすのは置き場だけ。改名は別の段 (関数名・記号・kernel.map の照合に響くため)。
- **KAPI・ABI・リンク順は変えない。** `build/kernel.mk` の `C_KERNEL` は 1 本の順序つき一覧のまま、パスだけ書き換える
  → `kernel.bin` は build_id を除いてバイト一致が期待できる (ソースに `__FILE__` は 0 件、確認済み)。
- v4 の計画との整合: 多機種・多アーキテクチャは v3 の範囲外 (`docs/decisions/V3_GOALS.md` §2)。v3 で守る v4 の原則は
  「PC-98 固有を portable 層へ流さない・HAL 境界・固定番地を契約にしない・platform-only は明示」(同 §2)。
  ROADMAP「他アーキテクチャへの移植に備えた調査」の残り「gdt/tss/cr3 と CPL=3 降下 asm の `arch/x86/` への移設」と同じ向き
  (本案の `arch/x86/kernel/`)。`platform/pc98/` の中身と `ARCH`/`PLATFORM` の `-I` の仕組み (arch/README.md) には触らない。
  PC-98 固有の**ヘッダ** (`include/pc98.h` ほか) の移動は userland も引く (下 §4-3) ので v4 の機種分割に回す。

## 1. 現状の分類 (中身から機能で)

先頭の説明と主な関数から分けた。行数の大きいもの: paging.c 1,696 / v86_bios.c 1,277 / ime.c 1,159 / pgalloc.c 1,098 / fdc.c 1,345 / loop_dev.c 1,177 / ne2000.c 1,057。

| 機能 | 現状のファイル (`kernel/` = k、`drivers/` = d、`gfx/` = g) | 中身 |
|---|---|---|
| CPU (x86) | k: gdt idt tss double_fault isr_handlers (.c/.h)、isr_stub kentry ring3_entry setjmp (.asm)、cpu_flags.inc entry_layout.inc | 記述子表・IDT・TSS・#DF タスク・例外 C 側・入口 asm・リング 3 降下 |
| 起動 | k: kernel.c boot_platform boot_display boot_splash bootinfo bootinfo_check bootlog bootlog_save sysconfig kaudit_boot boot_disk.h | `kernel_main`、起動情報域・起動ログ・起動設定・スプラッシュ |
| 機種境界の契約 | k: platform_{display,emergency,irq,memory,splash,sys,timer}.h | `platform/pc98/*.c` が実装する link-time の契約 (`include/io.h` と同じ性格) |
| 割り込み | k: irq irq_math tick | 動的 IRQ 登録・storm・IRQ0 コールバック |
| 時刻 | k: ktime time_math pit pit_math sysclk cpu_calibrate cpu_calibrate_math clock_service | µs 時計・PIT 分周・システムクロック判定・CPU 校正・時計 KAPI の転送 |
| 部品の読み込み | k: module module_image module_runtime module_schema.h hwdev_module kdebug_call kdebug_imports.def | KMOD 検証・load/start/abort・kdebug/hwdev の受け口 |
| メモリ | k: paging paging_app pgalloc physmem kmalloc dma_pool dma_pool_math shm shlib memory_boot | ページング・PFN 台帳・ヒープ・DMA 池・SHM・shlib・起動時のメモリ判定 |
| コンソール・文字 | k: console console_backend.h con_sink display_restore.h font_source font_guard unicode_file | TVRAM コンソール・GUI 中のシンク・フォント lease・Unicode 表 |
| FEP | k: ime ime_dict ime_romkana ime_render.h ime_render_tvram fep_module fep_body | 日本語入力 (fep.mod の本体と常駐の窓口) |
| V86 | k: v86* (22 本: .c/.h/.asm) | V86 モニタ・BIOS HLE・仮想 PIC/8251・ゲスト空間・v86.mod |
| 雑 (常駐サービス・診断) | k: sys kaudit kselftest.h kselftest_report kstack_hw serial_diag ver_collect ver_format ver_info.h gui input_service port_service kbd_inject sqlite_engine sqlite_outer r1_fixture font_guard_fixture | システム制御・診断・ver・GUI 背骨・KAPI 転送・SQLite の常駐側・試験専用 |
| 部品の窓口と本体 | k: {cd,cirrus,fep,fm,geforce,kcg,lan,pcm,v86}_module.c/.h と *_body.c、geforce_platform.c、snd_engine | `*_module.c` は常駐の窓口、`*_body.c` は .mod に入る本体 (build/*-module.mk)。装置ごとに対 |
| 装置の共通層 | d: dev | デバイス抽象化 (登録・検索) |
| ブロック | d: ide ide_addr atapi fdc fdc_decide fdc_track disk loop_dev pc98pt scsi.h | IDE/ATAPI・FDC・ループ・PC-98 区画表 |
| バス・DMA | d: pci pci_decode pci_bind pci_bind_match dma8237 dma8237_math | PCI 列挙・結線、8237 |
| 入力 | d: kbd kbd_dlog kbd_status mouse mouse_bus mouse_seamless | キーボード・マウス |
| 文字装置 | d: serial serial_plan rtc np2sysp | RS-232C・µPD4990A・NP21/W 通信 |
| ネット | d: ne2000 ne2000_ring ne2000_io.asm ne2000_regs.h lgy98 e82557 e82557_desc e82557_eeprom e82557_regs.h | NIC 2 系統 |
| 音 | d: fm pcm_cs4231 pcm_cs4231_math | OPN・CS4231 |
| 表示装置 | d: kcg wab_cirrus wab_glue.h wab_glue_xe10、g: backend_cirrus cirrus_identify backend_geforce gf_present backend_pc98 backend_pegc pc98_route | 漢字 ROM・Cirrus・GeForce・PC-98 内蔵 (GDC/PEGC) |
| 描画 HAL | g: gfx_core gfx_vram gfx_scroll palette gfx.h gfx_internal.h | 表示の共通層 |
| FS | fs: vfs vfs_fd fd_redirect pipe_buffer / ext2_* / fatfs/ fatfs_vfs / iso9660 / hostdrvfs hostdrv_* / serialfs sfs_* | VFS と各 FS |

## 2. 新しい配置の木

```
init/                  起動の順序だけ: kernel_main・起動情報・起動ログ・起動設定・スプラッシュ (15)
kernel/                常駐サービス・診断・ver・GUI 背骨・SQLite 常駐側・試験専用の器 (21)
  irq/                 動的 IRQ 登録・IRQ0 コールバック (6)
  time/                µs 時計・PIT 分周・システムクロック・CPU 校正 (14)
  module/              KMOD の検証と load/start/abort、kdebug/hwdev の受け口 (11)
  console/             TVRAM コンソール・シンク・フォント lease・Unicode 表 (10)
  ime/                 FEP (fep.mod の本体と窓口) (9)
  v86/                 V86 モニタと v86.mod (x86 かつ PC-98 専用 — §8 要判断) (22)
mm/                    ページング・PFN 台帳・ヒープ・DMA 池・SHM・shlib (19)
arch/x86/              (既存: arch_io.h / arch_cpu.h / x86_desc.h / x86_flags.h — 変えない)
  kernel/              GDT/IDT/TSS・#DF・例外・入口 asm・リング 3 降下 (15)
include/               (既存 59) + 機種境界の契約 platform_*.h 7 本
drivers/
  base/                装置の共通層 dev (2)
  ata/                 IDE・ATAPI と cd.mod の窓口・本体 (9)
  block/               FDC・FD 定数・ループ・PC-98 区画表・SCSI 定数 (13)
  dma/                 8237 (3)
  pci/                 PCI 列挙・復号・結線 (7)
  input/               キーボード・マウス (10)
  char/                シリアル・RTC・NP21/W 通信 (8)
  net/ne2000/          DP8390 系と LGY-98 (8)
  net/e82557/          Intel 8255x と lan.mod の窓口・本体 (10)
  sound/fm/            OPN と fm.mod (窓口・本体・サウンドエンジン) (7)
  sound/cs4231/        CS4231 と pcm.mod (6)
  video/kcg/           漢字 ROM と kcg.mod (5)
  video/cirrus/        Cirrus チップ・Xe10 グルー・HAL バックエンドと cirrus.mod (10)
  video/geforce/       GeForce の HAL バックエンドと geforce.mod (6)
  video/pc98/          PC-98 内蔵表示 (GDC planar・PEGC) のバックエンドと経路 (4)
gfx/                   描画 HAL の共通層だけ (7)
fs/                    VFS・fd・リダイレクト・パイプ (10)
  ext2/ (11)  fat/ (fatfs_vfs.c 1 + fatfs/ 7、vendor の中身は不変)  isofs/ (2)  hostdrv/ (5)  serialfs/ (9)
exec/ kapi/ lib/ net/ kdebug/ hwdev/ platform/ boot/ userland/ sdk/   (動かさない)
```

ディレクトリの約束:
- 部品 (.mod) の**窓口 `*_module.c` と本体 `*_body.c` は装置の隣**に置く (Linux の module_init が装置の .c にあるのと同じ)。
  常駐か .mod かはビルドの一覧 (build/*-module.mk) が決め、置き場では分けない。
- `include/` は今のまま「カーネル内の共通ヘッダ」。公開 ABI (Linux の include/uapi) は既に `sdk/include/` に分かれている。
- 動かさない理由: `exec/` (31) `kapi/` `lib/` `net/` は既に 1 機能 1 ディレクトリで目安の範囲。`kdebug/` `hwdev/` は直近の
  GF_STAGES の分割 (b214e751) で中が整ったばかり。`platform/pc98/` は v4 の機種分割の器なので v4 の票で扱う。
- Linux と違えた所: `init/` を分けた (kernel.c は 678 行の起動順序で、kernel/ の残りと性格が違う)。`mm/` を最上位にした
  (19 本、kernel/ に入れると 40 本になる)。`gfx/` は Linux の drivers/gpu/drm の共通部に当たるが、OS32 では独立の層なので最上位のまま。

## 3. 対応表 (今のパス → 新しいパス、全 309 本)

ファイル名は変えないので、新しいディレクトリごとに旧パスを並べる (旧パス → `新ディレクトリ/同じ名前`)。
新旧の名前の衝突は 0 (同じディレクトリに同名が来る組は無い)。

| 新しいディレクトリ | 本数 | 旧パス |
|---|---|---|
| `arch/x86/kernel/` | 15 | `arch/x86/kernel/cpu_flags.inc` `arch/x86/kernel/double_fault.c` `arch/x86/kernel/double_fault.h` `arch/x86/kernel/entry_layout.inc` `arch/x86/kernel/gdt.c` `arch/x86/kernel/gdt.h` `arch/x86/kernel/idt.c` `arch/x86/kernel/idt.h` `arch/x86/kernel/isr_handlers.c` `arch/x86/kernel/isr_stub.asm` `arch/x86/kernel/kentry.asm` `arch/x86/kernel/ring3_entry.asm` `arch/x86/kernel/setjmp.asm` `arch/x86/kernel/tss.c` `arch/x86/kernel/tss.h` |
| `drivers/ata/` | 9 | `drivers/atapi.c` `drivers/atapi.h` `drivers/ide.c` `drivers/ide.h` `drivers/ide_addr.c` `drivers/ide_addr.h` `kernel/cd_body.c` `kernel/cd_module.c` `kernel/cd_module.h` |
| `drivers/base/` | 2 | `drivers/dev.c` `drivers/dev.h` |
| `drivers/block/` | 13 | `drivers/disk.c` `drivers/disk.h` `drivers/fdc.c` `drivers/fdc.h` `drivers/fdc_decide.c` `drivers/fdc_decide.h` `drivers/fdc_track.c` `drivers/fdc_track.h` `drivers/loop_dev.c` `drivers/loop_dev.h` `drivers/pc98pt.c` `drivers/pc98pt.h` `drivers/scsi.h` |
| `drivers/char/` | 8 | `drivers/np2sysp.c` `drivers/np2sysp.h` `drivers/rtc.c` `drivers/rtc.h` `drivers/serial.c` `drivers/serial.h` `drivers/serial_plan.c` `drivers/serial_plan.h` |
| `drivers/dma/` | 3 | `drivers/dma8237.c` `drivers/dma8237.h` `drivers/dma8237_math.c` |
| `drivers/input/` | 10 | `drivers/kbd.c` `drivers/kbd.h` `drivers/kbd_dlog.c` `drivers/kbd_dlog.h` `drivers/kbd_status.c` `drivers/kbd_status.h` `drivers/mouse.c` `drivers/mouse.h` `drivers/mouse_bus.c` `drivers/mouse_seamless.c` |
| `drivers/net/e82557/` | 10 | `drivers/e82557.c` `drivers/e82557.h` `drivers/e82557_desc.c` `drivers/e82557_desc.h` `drivers/e82557_eeprom.c` `drivers/e82557_eeprom.h` `drivers/e82557_regs.h` `kernel/lan_body.c` `kernel/lan_module.c` `kernel/lan_module.h` |
| `drivers/net/ne2000/` | 8 | `drivers/lgy98.c` `drivers/lgy98.h` `drivers/ne2000.c` `drivers/ne2000.h` `drivers/ne2000_io.asm` `drivers/ne2000_regs.h` `drivers/ne2000_ring.c` `drivers/ne2000_ring.h` |
| `drivers/pci/` | 7 | `drivers/pci.c` `drivers/pci.h` `drivers/pci_bind.c` `drivers/pci_bind.h` `drivers/pci_bind_match.c` `drivers/pci_decode.c` `drivers/pci_decode.h` |
| `drivers/sound/cs4231/` | 6 | `drivers/pcm_cs4231.c` `drivers/pcm_cs4231.h` `drivers/pcm_cs4231_math.c` `kernel/pcm_body.c` `kernel/pcm_module.c` `kernel/pcm_module.h` |
| `drivers/sound/fm/` | 7 | `drivers/fm.c` `drivers/fm.h` `kernel/fm_body.c` `kernel/fm_module.c` `kernel/fm_module.h` `kernel/snd_engine.c` `kernel/snd_engine.h` |
| `drivers/video/cirrus/` | 10 | `drivers/wab_cirrus.c` `drivers/wab_cirrus.h` `drivers/wab_glue.h` `drivers/wab_glue_xe10.c` `drivers/wab_glue_xe10.h` `gfx/backend_cirrus.c` `gfx/cirrus_identify.c` `kernel/cirrus_body.c` `kernel/cirrus_module.c` `kernel/cirrus_module.h` |
| `drivers/video/geforce/` | 6 | `gfx/backend_geforce.c` `gfx/gf_present.c` `kernel/geforce_body.c` `kernel/geforce_module.c` `kernel/geforce_module.h` `kernel/geforce_platform.c` |
| `drivers/video/kcg/` | 5 | `drivers/kcg.c` `drivers/kcg.h` `kernel/kcg_body.c` `kernel/kcg_module.c` `kernel/kcg_module.h` |
| `drivers/video/pc98/` | 4 | `gfx/backend_pc98.c` `gfx/backend_pegc.c` `gfx/pc98_route.c` `gfx/pc98_route.h` |
| `fs/` | 10 | (据え置き 10 本: fd_redirect.c fd_redirect.h ide_limits.h pipe_buffer.c pipe_buffer.h vfs.c vfs.h vfs_device.h vfs_fd.c vfs_name_rules.inc) |
| `fs/ext2/` | 11 | `fs/ext2.h` `fs/ext2_ctx.h` `fs/ext2_dir.c` `fs/ext2_file.c` `fs/ext2_fmt.c` `fs/ext2_inode.c` `fs/ext2_layout.c` `fs/ext2_layout.h` `fs/ext2_priv.h` `fs/ext2_super.c` `fs/ext2_vfs.c` |
| `fs/fat/` | 1 | `fs/fatfs_vfs.c` |
| `fs/fat/fatfs/` | 7 | `fs/fatfs/diskio.c` `fs/fatfs/diskio.h` `fs/fatfs/diskio_os32.h` `fs/fatfs/ff.c` `fs/fatfs/ff.h` `fs/fatfs/ffconf.h` `fs/fatfs/string.h` |
| `fs/hostdrv/` | 5 | `fs/hostdrv_list_rules.inc` `fs/hostdrv_stat_rules.inc` `fs/hostdrvfs.c` `fs/hostdrvfs.h` `fs/hostdrvfs_proto.h` |
| `fs/isofs/` | 2 | `fs/iso9660.c` `fs/iso9660.h` |
| `fs/serialfs/` | 9 | `fs/serialfs.c` `fs/serialfs.h` `fs/serialfs_session.c` `fs/sfs_client.c` `fs/sfs_client.h` `fs/sfs_lan.c` `fs/sfs_lan.h` `fs/sfs_proto.c` `fs/sfs_proto.h` |
| `gfx/` | 7 | (据え置き 7 本: gfx.h gfx_core.c gfx_internal.h gfx_scroll.c gfx_vram.c palette.c palette.h) |
| `include/` | 7 | `include/platform_display.h` `include/platform_emergency.h` `include/platform_irq.h` `include/platform_memory.h` `include/platform_splash.h` `include/platform_sys.h` `include/platform_timer.h` |
| `init/` | 15 | `init/boot_disk.h` `init/boot_display.c` `init/boot_display.h` `init/boot_platform.c` `init/boot_platform.h` `init/boot_splash.c` `init/boot_splash.h` `init/bootinfo.c` `init/bootinfo_check.c` `init/bootlog.c` `init/bootlog_save.c` `init/kaudit_boot.c` `init/kernel.c` `init/sysconfig.c` `init/sysconfig.h` |
| `kernel/` | 21 | (据え置き 21 本: font_guard_fixture.c gui.c gui.h input_service.c kaudit.c kbd_inject.c kselftest.h kselftest_report.c kstack_hw.c kstack_hw.h port_service.c r1_fixture.c r1_fixture.h serial_diag.c sqlite_engine.c sqlite_engine.h sqlite_outer.c sys.c ver_collect.c ver_format.c ver_info.h) |
| `kernel/console/` | 10 | `kernel/console/con_sink.c` `kernel/console/console.c` `kernel/console/console_backend.h` `kernel/console/display_restore.h` `kernel/console/font_guard.c` `kernel/console/font_guard.h` `kernel/console/font_source.c` `kernel/console/font_source.h` `kernel/console/unicode_file.c` `kernel/console/unicode_file.h` |
| `kernel/ime/` | 9 | `kernel/ime/fep_body.c` `kernel/ime/fep_module.c` `kernel/ime/fep_module.h` `kernel/ime/ime.c` `kernel/ime/ime.h` `kernel/ime/ime_dict.c` `kernel/ime/ime_render.h` `kernel/ime/ime_render_tvram.c` `kernel/ime/ime_romkana.c` |
| `kernel/irq/` | 6 | `kernel/irq/irq.c` `kernel/irq/irq.h` `kernel/irq/irq_math.c` `kernel/irq/irq_math.h` `kernel/irq/tick.c` `kernel/irq/tick.h` |
| `kernel/module/` | 11 | `kernel/module/hwdev_module.c` `kernel/module/hwdev_module.h` `kernel/module/kdebug_call.c` `kernel/module/kdebug_imports.def` `kernel/module/module.c` `kernel/module/module.h` `kernel/module/module_image.c` `kernel/module/module_image.h` `kernel/module/module_runtime.c` `kernel/module/module_runtime.h` `kernel/module/module_schema.h` |
| `kernel/time/` | 14 | `kernel/time/clock_service.c` `kernel/time/cpu_calibrate.c` `kernel/time/cpu_calibrate.h` `kernel/time/cpu_calibrate_math.c` `kernel/time/cpu_calibrate_math.h` `kernel/time/ktime.c` `kernel/time/ktime.h` `kernel/time/pit.c` `kernel/time/pit_math.c` `kernel/time/pit_math.h` `kernel/time/sysclk.c` `kernel/time/sysclk.h` `kernel/time/time_math.c` `kernel/time/time_math.h` |
| `kernel/v86/` | 22 | `kernel/v86/v86.c` `kernel/v86/v86.h` `kernel/v86/v86_bios.c` `kernel/v86/v86_bios.h` `kernel/v86/v86_body.c` `kernel/v86/v86_entry.asm` `kernel/v86/v86_gcap.c` `kernel/v86/v86_gcap.h` `kernel/v86/v86_gcap_math.h` `kernel/v86/v86_io.c` `kernel/v86/v86_io.h` `kernel/v86/v86_kbd.c` `kernel/v86/v86_kbd.h` `kernel/v86/v86_mem.c` `kernel/v86/v86_mem.h` `kernel/v86/v86_module.c` `kernel/v86/v86_module.h` `kernel/v86/v86_pic.c` `kernel/v86/v86_pic.h` `kernel/v86/v86_snapshot.c` `kernel/v86/v86_test16.asm` `kernel/v86/v86_test16_disk.asm` |
| `mm/` | 19 | `mm/dma_pool.c` `mm/dma_pool.h` `mm/dma_pool_math.c` `mm/kmalloc.c` `mm/kmalloc.h` `mm/memory_boot.c` `mm/memory_boot.h` `mm/paging.c` `mm/paging.h` `mm/paging_app.c` `mm/paging_app.h` `mm/pgalloc.c` `mm/pgalloc.h` `mm/physmem.c` `mm/physmem.h` `mm/shlib.c` `mm/shlib.h` `mm/shm.c` `mm/shm.h` |

### 「要判断」(上の表では仮に置いた — 2026-10-30 ユーザー「仮で良い」、本当の置き場は別の分離計画で)

| ファイル | 仮の置き場 | 迷う理由 |
|---|---|---|
| kernel/v86* 22 本 | kernel/v86/ | x86 の仕組み (V86 モード・TSS) かつ PC-98 の BIOS・装置の模倣。arch/x86 にも platform/pc98 にも寄せ切れない。v4 で ARM に載らない部分なので「platform-only の明示」をどこでするか |
| mm/paging.c・paging.h | mm/ | 中身は x86 の 2 段ページング (CR3・PTE ビット) と AS 管理が混ざる。ROADMAP の「cr3 を arch/x86 へ」は関数単位の切り出しで、置き場の移動とは別の段 |
| arch/x86/kernel/isr_handlers.c | arch/x86/kernel/ | 例外の C 側だが sqlite_engine・font_guard・module_runtime・r1_fixture を引く (共通層への依存が多い)。分けるなら例外の受け (arch) と方針 (kernel) に割る |
| arch/x86/kernel/idt.h | arch/x86/kernel/ | IDT だけでなく PIC/PIT の定義と tick_count を持ち、共通層 12 か所が引く (§4-1)。割るまでは arch に置くと逆向きが 12 本立つ |
| kernel/cpu_calibrate* | kernel/time/ | 校正は PIT tick と CPU ループの組で、x86 の命令は使っていない (io.h だけ)。時刻の層に置いた |
| mm/memory_boot.c | mm/ | 起動の一部だが中身はメモリ判定と台帳への登録 |
| kernel/font_source・font_guard・unicode_file | kernel/console/ | 文字・フォントの層。GUI と共有するので `kernel/` 直下でもよい |
| kernel/snd_engine.c | drivers/sound/fm/ | fm.mod の中身で OPN 専用 (FM/SSG シーケンサ)。将来 PCM と共有するなら drivers/sound/ 直下 (core) |
| kernel/sqlite_engine・sqlite_outer | kernel/ | lib/sqlite3/ (os32_sqlite_* の glue) に寄せる案もある。常駐のスタック切替は kernel 側の責任と見た |
| kernel/geforce_platform.c | drivers/video/geforce/ | 起動時の BAR 予約で PC-98 の帯に依存。platform/pc98/ に置く案 |
| gfx/backend_pc98・backend_pegc・pc98_route | drivers/video/pc98/ | PC-98 内蔵表示。`platform/pc98/` 寄せの案もある (v4 で機種ごとに切る単位) |
| drivers/np2sysp | drivers/char/ | NP21/W 専用の通信。5 か所 (mouse・lgy98・backend_cirrus・backend_pegc・mouse_seamless) から引かれる「エミュレータ検出」なので drivers/platform/np21w/ の案 |
| drivers/disk.h・scsi.h | drivers/block/ | 定数だけのヘッダ |
| fs/ide_limits.h | fs/ | ide_contract.h の FS 側の写し。drivers/ata/ でもよい |
| kernel/kselftest.h・kselftest_report.c | kernel/ | 本体は kdebug/kselftest.c。kdebug/ に寄せる案 (kernel.map に固定記号が要るので常駐は残る) |

## 4. 依存の方向 (移した後に逆向きで残る include)

走査: 引数の無い `#include "x"` を「同じディレクトリ → INC_KERNEL 相当の -I 順」で今の木の実ファイルに解決し、新しい置き場で層を判定した。
層: arch = `arch/`、plat = `platform/` と PC-98 固有ヘッダ (`include/pc98.h` `tvram.h` `pegc.h` `wab_xe10.h` `lib/pc98_cell.h`)、
dev = `drivers/` `hwdev/`、fsimpl = `fs/<各FS>/`、それ以外を共通。

| 向き | 件数 | 主なもの (新しいパス:行) |
|---|---|---|
| 共通 → arch | 23 | `idt.h` 12 (init/kernel.c:11、init/bootlog_save.c:16、kernel/irq/irq.c:13、kernel/time/ktime.c:21・pit.c:1、kernel/ver_collect.c:4・ver_format.c:4、net/link.c:51、exec/ring3_ls.S:3、kernel/v86/v86.c:20・v86_pic.c:6、kernel/font_guard_fixture.c:11)、`tss.h` 5 (exec/exec.c:50、exec/v86_call.c:11、init/kernel.c:12、kernel/v86/v86.c:11・v86_io.c:8)、exec/exec.c:49 → gdt.h、init/kernel.c:66 → double_fault.h、kernel/v86/v86_entry.asm:17 → entry_layout.inc。include/io.h・cpu.h の 3 本は契約の固定名で正しい向き |
| 共通 → dev | 26 | init/kernel.c が窓口 8 本 (:22 kcg_module.h、:40 cd_module.h、:44 dev.h、:56 snd_engine.h、:57 fm_module.h、:59 pcm_module.h、:60 lan_module.h、:61 cirrus_module.h、:62 geforce_module.h)。**gfx/gfx_core.c:16・17・19** (kcg_module.h・geforce_module.h・pc98_route.h — HAL の共通層が装置を名指し)、kernel/console/console.c:20 → pc98_route.h、exec/exec.c:39 → snd_engine.h、kernel/ime/ime.c:18 → kbd.h、kernel/ime/ime_dict.c:31 → serial.h、fs/ext2/ext2_super.c:18 → pc98pt.h、fs/ext2/ext2_priv.h:6 → dev.h、kernel/v86/v86_bios.c:9 → loop_dev.h・v86_io.c:17 → snd_engine.h・v86_kbd.h:25 → kbd.h・v86_module.c:11 → kcg_module.h、net/link.c:22 → ne2000.h、kapi/kapi_generated.c:19・28 (生成物、KAPI 表は全部を引く) |
| 共通 → plat | 27 | `pc98.h` 11 (gfx/gfx.h:11・gfx_internal.h:5、kernel/irq/irq_math.c:9、kernel/time/pit_math.c:13・sysclk.c:10、init/kernel.c:46、include/crt_diag.h:5、v86 の 4 本)、`tvram.h` 7 (init/kernel.c:45、init/boot_splash.c:19、kernel/console/console.c:12、kernel/ime/ime.c:19・ime_render_tvram.c:6、kernel/v86/v86_mem.c:20、kapi/kapi_generated.c:14)、`pegc.h` 4 (gfx/gfx_core.c:12・gfx_vram.c:2・palette.h:21、kernel/v86/v86_io.c:10)、gfx/gfx_core.c:13 → wab_xe10.h、**init/kernel.c:78 → platform/pc98/boot.h**、**include/platform_memory.h:23 → platform/pc98/platform_memory_desc.h**、**net/link.c:2 → platform/pc98/tick_order.h** |
| 共通 → fsimpl | 2 | init/kernel.c:15 → fat/fatfs/diskio_os32.h、kapi/kapi_generated.c:42 → serialfs.h |
| 装置 → 別の装置 | 21 | `np2sysp.h` 5 (エミュレータ検出、§3 要判断)、drivers/base/dev.c:15-17 → disk.h・ide.h・fdc.h (**共通層が装置を名指し**)、fdc.c:13・pcm_cs4231.c:16 → dma8237.h、e82557 の 3 本 → pci、input/kbd.c:25 → serial.h、input/mouse.c:10 → kcg.h、video/geforce の 3 本 → hwdev/gf |
| 装置 → fsimpl | 2 | drivers/ata/cd_body.c:4 → iso9660.h (cd.mod が両方を持つ)、drivers/net/e82557/lan_module.h:6 → sfs_lan.h |

読み方:
1. **逆向きの大半は移動で生まれるのではなく、今の平らな配置で見えなかったもの**。移動の段では直さない (直すと機械語が変わる)。
2. 段の後に票を立てて直す候補 (効く順): ① `idt.h` を IDT (arch) と PIC/PIT・tick (kernel/irq, kernel/time) に割る — 12 本が消える。
   ② `gfx_core.c` の装置名指し 3 本と `console.c` → `pc98_route.h` をバックエンド表の登録に替える。③ `dev.c` → ide/fdc/disk を
   登録に替える。④ `init/kernel.c` の窓口 8 本は「起動の順序」の責任なので残してよい (Linux の initcall 化はしない、規模に合わない)。
3. PC-98 固有ヘッダを共通層が引く 27 本は v4 の機種分割の本題。v3 では一覧として票に残すだけ (V3_GOALS の原則 1「流さない」は新規分に適用)。
4. 新旧の解決先が変わる include は **0 件** (同じ走査で、移動後の -I 一覧で解決し直して比較)。ただし同名ヘッダが 2 組ある:
   `include/kbd.h` と `drivers/kbd.h`、`init/boot_disk.h` と `platform/pc98/boot_disk.h`。-I の順を並べ替えると黙って入れ替わるので、
   段の検査に「解決先の一致」を入れる (下 §7)。

## 5. 移動に伴って直す所の見積り (grep の数)

数え方: 動く 271 本の旧パス文字列 (`mm/paging.c` 等、`../` 付きも) の出現数 / ファイル数。`docs/archive/` と `docs/hw/` は除く。

| 場所 | 参照 / ファイル | 中身・直し方 |
|---|---|---|
| ソースの `#include` (パス付き) | **94 / 30** | `"../drivers/kbd.h"`・`"mm/pgalloc.h"` の形。動かさない kdebug/ (10 本)・hwdev/gf (4)・platform/pc98 (6)・lib/utf8.c にも及ぶ。うち生成物 33 (kdebug/generated/kdebug_imports.h 28、hwdev/generated 4、lib/sqlite3/generated 1) は**生成器の入力を直して再生成**: `kernel/module/kdebug_imports.def` (146 参照)、`hwdev/hwdev_imports.def` (7)、`tools/prototypes/*_imports.def` 9 本 (102) |
| ソースの註のパス | 約 320 / 約 140 | 機械語に響かない。段の中で一括置換してよい (しなくても壊れない) |
| -I の設定 | `build/config.mk` の INC_* 10 本 | 新ディレクトリ 30 を足す (`include/` と既存の `gfx/` `fs/` `kernel/` を除く)。案: `KERNEL_SRC_DIRS` 1 変数にまとめて INC_KERNEL/INC_DRIVERS/INC_FS/INC_GFX/INC_EXEC/INC_KAPI/INC_SQLITE から引く。fatfs の `-Ifs/fatfs` 先頭 (string.h の差し替え) は `-Ifs/fat/fatfs` に |
| ビルドの一覧・規則 | build/*.mk・Makefile・ld 239 / 16 (kernel.mk 141、programs.mk 26、lan-module.mk 14、kdebug.mk 8、他の *-module.mk 各 4〜7、boot.mk 5)、build/checks.d/*.mk 51 / 32 | `C_KERNEL`・`ASM_KERNEL` の順序は保つ。**パスで選ぶ規則** (`-Os` の LAYOUT_SMALL・R1_SMALL、`double_fault.o` の DF_GUARD_TEST、`lgy98.o: .FORCE`、`module*.o -Os`、fatfs の INC) を取りこぼすと機械語が変わる — 段の比較で必ず出る |
| ディレクトリ名の glob | build 16 / 6、check_map 14 / 5、tests 5 / 5 | `kernel/*.h` `drivers/*.h` (kdebug.mk:78 HWDEV_HEADERS、check-dep-dir-*.yaml) は `**` に。Makefile:95・146 の `find boot kernel drivers ...` に init mm を足す |
| 走査の根の一覧 (固定) | tools 4 本 | `check_p2v.py:11 ROOTS`、`check_arm_compile.py:73`、`check_arch_asm.py:9`、`check_kprintf_window.py`。**init/ mm/ を足し忘れると黙って走査が減る** — 段の前後で各検査の走査ファイル数を比べる |
| tools/tests | 2,363 / 337 | ホスト試験が直に開く・コンパイルするパス。`-I` 付きの組み立て 111 か所 (tests と checks.d)。機械置換で足りる形がほとんど |
| tools/check_map.d/*.yaml・check_map.yaml | 1,459 / 159 | 変更 → 検査の対応。機械置換。`check_select.py --lint` が通ること |
| tools/check_p2v_allow.txt | 34 / 1 | `ファイル:関数` の許可行。機械置換 |
| 変異の目印 | パスは 0 | 目印はソースの**文字列** (`MUTATIONS = [(名, 原文, 置換)]`、tools/tests 136 本) で、パスは試験側の開くパス (上の tests の数に含む)。移動だけなら目印は全部そのまま当たる |
| tools の他 | 151 / 34 | prototypes/*.def 102 (上記)、check_le_access.py 8、check_arm_compile.py 5、nhd_deploy.py・pc98pt.py・rshell_serial.py・np21w_mcp/gui.py 各 3 (註・表示) |
| docs | 798 / 76 | 正典の 2 か所: `docs/08_build.md` §8-3 (木の図、9 参照)、`docs/DEVELOPMENT.md` §2 (表はファイル名だけで書く — 節見出しの `kernel/` `drivers/` `fs/` を新しい区分に組み替え)。他は POLICY_DEBUG 30、05_drivers 8 など。`check_docs_links.py` が壊れたリンクを拾う |
| その他 | arch/README.md 2、THIRD_PARTY.md 1、sdk/include/os32/os32_kapi_shared.h 12 (註)、userland 81 / 40 (註、Rust の註を含む) | 註だけ |
| .github/workflows | **0** | パスを直に指していない (`make` 経由) |

## 6. 生成物 (.o/.d) をソースツリーから出す

- 今: `build/config.mk:10` の `KERNEL_OBJ_PREFIX = $(if $(filter-out build/out,$(BUILD_OUT)),$(BUILD_OUT)/obj/)` — 既定の
  `BUILD_OUT=build/out` のときだけ空でソースの隣に出る。**木の外に出す経路は既にある**: kernel-r1 (`$(R1_OUT)/obj`、毎回の `make all`) と
  別 BUILD_OUT (tools/tests/test_layout_defs.py・tools/clang_ast/build.py) で毎回通っている (kernel.mk:334-355 の `ifneq` 分岐)。
- 今ソースの隣にある .o: kernel 93・drivers 39・platform 22・lib 23・fs 21・exec 18・gfx 9・kapi 6・boot 6・sdk 6・net 1・userland 232。
- 範囲の案 (2 段):
  - **6a カーネル側** (kernel drivers fs gfx exec kapi lib net platform、約 230 本): 既定を `KERNEL_OBJ_PREFIX = $(BUILD_OUT)/obj/` に固定し、
    `ifneq` 分岐を常用にして、kernel.mk のソース隣の規則 (`kernel/%.o:` ほか 14 本の型規則と個別規則、66〜130 行) を消す。
    個別規則が持つフラグ (`-Os`・INC の違い・`.FORCE`) は分岐側の `LAYOUT_SMALL` 等に移っているか 1 本ずつ照合する — 今の分岐は
    `INC_KERNEL` 1 本で組むので、`INC_DRIVERS`/`INC_FS`/`INC_GFX` で組んでいた .o の解決先が変わらないことを §4-4 の走査で確かめる。
    *-module.mk の `$(KERNEL_OBJ_PREFIX)kernel/X_module.o` は既に対応済み。Makefile:95 の DEPFILES の find と clean-deps は build/out 以下に。
    ソースの .o を直に読む tools: 17 参照 / 8 本 (test_c_dialect.py 8、test_vmkernel_layout.py 2、test_ver_version.py 2 ほか、gen_kdebug_imports.py 1)。
    kernel.map の中のパスが `build/out/obj/...` に変わる — map を読む道具 (gen_memmap・check-memmap・kpeek の記号引き) は記号名で引くので
    影響しない見込み (未確認、§8)。
  - **6b ユーザー側** (userland 232・sdk・boot・lib/*_prog.o): programs.mk・libs.mk・sdk.mk・boot.mk の型規則 (programs 6・libs 10 ほか) と
    外部 SDK の約束 (sdk/README) に触るので別の票。急がない。
- **6a を移動より先にやる**: 移動の段で古い .o/.d がソースの隣に残って取り違える事故 (`-include $(DEPFILES)` が古い .d を読む) を消せる。

## 7. 実施の段

各段は「対応表 (JSON) → 道具で git mv → 参照の機械置換 → 生成物の再生成 → ビルド → 比較」。手で書くのは対応表だけにする
(`tools/split_c_functions.py` と同じ考え方: 道具 `tools/move_files.py` を足し、旧パスの完全一致・語境界つきで置換、`../` 付きは新しい相対に計算し直す)。

比較 (各段の受入):
- `kernel.bin`・`*.mod`・`kdebug.bin`・`hwdev.bin`・kernel-r1 をバイト比較 (build_id は固定値で組む — gen_build_id.py は git から取るので、
  比較用に同じ commit 文字列を与える口が要る、§8)。一致しなければ objdump で関数ごとの機械語を比べて原因の .o を出す (b214e751 と同じ手順)。
- `#include` の解決先の一致 (§4-4 の走査を道具にして前後で比べる)。
- 走査型の検査の走査ファイル数の一致 (check_p2v・check_arm_compile・check_arch_asm・check_c_dialect)。
- `make all` + `make check-fast` (全部の試験が新しいパスで通る) + `check_select.py --lint`。

| 段 | 中身 | 本数 | 依存 |
|---|---|---|---|
| L0 | §6a (カーネル側の .o を build/out/obj へ)、走査の根の一覧を 1 か所に (足し忘れの防止)、`move_files.py` と解決先の走査道具 | 動かすファイル 0 | 先頭。ここだけはバイト比較に加えゲストで起動 (kernel.map のパスが変わる) |
| L1 | kernel/ → init/ mm/ kernel/{irq,time,module,console,ime,v86}/ arch/x86/kernel/ include/ (窓口・本体は L2) | 128 | L0 の後 |
| L2 | drivers/ → drivers/<種類>/、kernel/ の窓口・本体 24 本と gfx/ のバックエンド 8 本を装置の隣へ | 108 | L1 の後 (kernel/ の同じ行・同じ yaml を触る) |
| L3 | fs/ → fs/<各FS>/ | 35 | L0 の後なら L1・L2 と並列にできる (触る行が別)。ただし kernel.mk の `C_KERNEL` と check_map.yaml は同じファイル — 取り込みは順に |
| L4 | 文書: 08_build §8-3 の木、DEVELOPMENT §2 の節の組み替え、POLICY_DEBUG ほかの参照 | docs のみ | L1〜L3 の後。文書だけの取り込み |
| L5〜 | 逆向きの依存を直す票 (§4 読み方 2 の ①〜③)、ディレクトリごとの源一覧 (`*/sources.mk`)、PC-98 ヘッダ・v86 の置き場 (v4) | — | 機械語が変わる。通常の設計・レビューの段 |

- 並列: 実装者は 1 段ずつ (L1 と L2 は kernel/ の同じファイル群を触るので並べない)。L3 は L1 と並べられるが、取り込みの検査が
  同時 2 本までの枠を食うので、PM の手間との兼ね合いで順にしてよい。
- **ディレクトリごとの Makefile (Linux の obj-y) は L1〜L3 ではしない**: `C_KERNEL` を割るとリンク順が変わり、番地が動いてバイト比較が
  使えなくなる。L5 で「各 .o の一致 + リンク順の変化だけ」として別に受け入れる (リンク順が変わると os32.ld の ASSERT・予算に触る可能性)。
- 段の大きさの目安: L1・L2 は道具がやる機械置換で差分は数千行になるが、人が読むのは対応表 JSON と build/*.mk・config.mk の差分だけ。

## 8. 未確認・要判断

未確認:
1. kernel.map のパスが `build/out/obj/...` に変わったとき、map を読む道具 (gen_memmap.py、check-memmap、tools/tests の map 読み、
   実機の peek) がオブジェクトのパスで引いている所が無いか (記号名で引いている見込みだが全数は見ていない)。
2. build_id を固定してバイト比較できる口が gen_build_id.py に無い (git の commit をそのまま読む)。比較用の上書き (環境変数) を L0 に足すか、
   build_id.o を除いて比べるか。
3. 今の `ifneq` 分岐 (別 BUILD_OUT) が `INC_KERNEL` 1 本で全 .o を組むことで、既定経路 (INC_DRIVERS/INC_FS/INC_GFX) と機械語が同じか。
   r1 が毎回この経路で組まれ起動試験も通っているので同じ見込みだが、.o 単位の比較はしていない。
4. tools/tests のパス置換が機械置換で足りない形 (`ROOT / 'kernel' / 'x.c'` のような分割したパス、6 か所見つけた) の全数。
5. Codex の sandbox・check_wt.sh の作業木で、移動後の初回ビルドに古い .d が残らないか (L0 を先にすれば消える見込み)。
6. 生成物 (kdebug/generated・hwdev/generated・lib/sqlite3/generated) を .def の変更から再生成したとき、パス以外の差が出ないか。

要判断 (ユーザーか PM):
1. **このディレクトリ名でよいか** — `init/` `mm/` を最上位に出すか (kernel/ の下に置く案)。`drivers/char/` の名 (Linux の旧名) か `drivers/tty/`+`drivers/rtc/`+`drivers/misc/` に割るか。
2. **窓口・本体を装置の隣に置くか** (本案)、`kernel/modules/` にまとめるか。前者は装置単位で読める、後者は「常駐の窓口の一覧」が 1 か所に見える。
3. §3「要判断」表の 15 件 (とくに v86・paging・isr_handlers・idt.h の置き場)。
4. -I を新ディレクトリの数だけ足す (本案、ソースの include はパス付き 94 本以外そのまま) か、include を `"drivers/input/kbd.h"` のような
   根からのパスに書き換えるか (Linux の `<linux/x.h>` に近い。差分は増えるが同名ヘッダの事故が消える)。本案は前者で入り、後者は L5 以降の候補。
5. PC-98 固有ヘッダ (`include/pc98.h` 57 本が引く、うち userland/sdk/boot/tools 14) を `platform/pc98/` に移すのは v4 の機種分割で、
   本案ではしない (外部プログラムの `PROGRAM_FLAGS` は `-Iplatform` を足さない約束、build/config.mk:192)。
6. 改名 (例: `kernel.c` → `init/main.c`、`backend_pc98.c` → `drivers/video/pc98/backend.c`) をするか。するなら移動と別の段。
7. 実施の時期: GF_STAGES など進行中の票と kdebug/ hwdev/ drivers/ の衝突。移動の段は他の段を止めて (取り込みの合間に) 入れるのがよい。

## 9. 実施の手順 (2026-10-30、ユーザー「手順をドキュメント化し終えたら実行してください」)

決まっていない要判断は PM の推奨で進める (ROLES §0「決裁」): 1 は `init/`・`mm/` を最上位・`drivers/char/` の名のまま、2 は窓口・本体を装置の隣、5 は PC-98 固有ヘッダを動かさない、6 は改名しない。3・4 は仮 (上の決定)。分離計画が後で置き場を変えるときは、同じ道具で移し直す (対応表を書き換えるだけ)。

### 9-1. 段と実装者

| 段 | 実装者 | 中身 | 基点 |
|---|---|---|---|
| L0 | **sol** (ビルドの仕組みを変える — 機械的でない) | (a) 比較の口: `tools/gen_build_id.py` に固定の commit 文字列を渡す環境変数、`kapi/kapi_sys.c:157` の `__DATE__ __TIME__` を固定できる口 (例: `SOURCE_DATE_EPOCH` 相当の `-D`)、(b) §6a (カーネル側の .o/.d を `build/out/obj/` へ、ソース隣の規則を消す)、(c) 走査の根の一覧を 1 か所に (check_p2v・check_arm_compile・check_arch_asm・check_kprintf_window が読む)、(d) 道具 `tools/move_files.py` (下 9-2) と `tools/include_resolve.py` (全 `#include "x"` の解決先を JSON に出す)、`tools/build_compare.sh` (固定の build_id で 2 つの木を組み、成果物をバイト比較・ずれたら objdump の関数ごとの差) | main |
| L1 | **terra** | `kernel/` → `init/` `mm/` `kernel/{irq,time,module,console,ime,v86}/` `arch/x86/kernel/` `include/` (§3 の表、窓口・本体を除く 128 本) | L0 の取り込み後 |
| L2 | **terra** | `drivers/` → `drivers/<種類>/`、窓口・本体 24 本と gfx のバックエンド 8 本を装置の隣へ (108 本) | L1 の取り込み後 |
| L3 | **terra** | `fs/` → `fs/<各FS>/` (35 本) | L2 の取り込み後 (C_KERNEL と check_map.yaml が同じファイルなので順に) |
| L4 | PM の Opus サブエージェント | 文書: 08_build §8-3 の木、DEVELOPMENT §2、ほかの参照 (check_docs_links が拾うもの) | L3 の後 |

L0 の後は、L1〜L3 のあいだ、`kernel/` `drivers/` `fs/` `gfx/` を触るほかの段 (TASK_REDUNDANCY の C 群など) を出さない。

### 9-2. 手順・完了条件・取り込み

正典は [SRC_MOVE.md](../../SRC_MOVE.md) (ソースの付け替え手順、ユーザー指示 2026-10-30「独立ドキュメント化して既存ドキュメントからは参照する形に」)。道具・1 段の手順・実装者の完了条件 (試験は回さない、バイト比較の例外)・PM の取り込み・段ごとに踏んだことはそこだけを更新する。
