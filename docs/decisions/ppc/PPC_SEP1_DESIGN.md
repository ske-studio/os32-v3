# PPC_SEP1_DESIGN — 分離の第 1 段 (ツールチェーン・ソース選択・生成物形式)

> 状態: **現行 (2026-11-07)** — x86 の生成物を変えずに、make ARCH=ppc PLATFORM=powermac が PowerPC のソース一覧でコンパイルまで進む形にする。段の状態は票 TASK_PPC_SEP が持つ。
> 出所: ~/os32-tmp/run/ppc-sep1/DESIGN.final.md を 2026-11-07 に写した (astra 起草 r1〜r2、Opus 5.5 第 1・2 回)。

- 読取元 `/home/hight/wt/os32-v3/ppc-env`、実装基点=`89a81bc33454035dacadb4281fed91f2a7e64c80`（origin/main）、2026-11-07。読取木HEADは229ed19だが、基点とのMakefile/build差分ゼロをgit diffで確認。行番号はこの共通版。PPC 計測器だけ `ppc-gauge` の枝を読んだ。
- 対象は SURVEY の B1/B2/T1。33件全部の実装ではない。mm は分割せず、案D（OS32の2段表をソフト表として読む→ハッシュ表へ遅延投入）に接続できる入口を作る。KAPI/SDK ABI・既存 C 本体・配置は変えない。
- 本書は実装設計。元リポジトリは変更せず、89a81bc3の隔離写し `r2-probe/` で起草用実験のみ実施。7本のコンパイルは `r2-probe/seven.log`、提案旗の再試行は `r2-proposed-seven.json`、順序/include/selector実験は `r2-evidence.txt`。実装後の全受入は未実施。新設名は提案。

## 1. 実物と変更境界
|対象|確認した固定箇所（file:line）|第1段の処置|
|---|---|---|
|ツール/旗|`build/config.mk:41,42,43,44,45` CC/AR/NASM/guard付きLD/objcopy、`:134,135` i386機械旗、`:185,186` ELF emulation/libgcc|arch profileへ。同じ展開順を保つ|
|include/ユーザ|`build/config.mk:59` x86・V86・装置の列、`:154,170` LGY98、`:201,202,203` i386 sysroot/ld/GCC版、`:212,213` CRT列|include列を選択化。SDK・userland規則はx86側だけ読み込む|
|ASM|`build/kernel.mk:6,7,17,18,21,25` kstring以外x86固定、`:228` R1にもshim、`:235,238,240,242,308,310,319,322` NASM世代include/elf32/shimの-m32|ASMとGAS .Sを別変数にし、shimの「ソース→例外名.o」対応をx86側へ|
|C一覧|`build/kernel.mk:28,29,30,31,33,34,35,36,37,38,39,40,42,46,48,50,51,52,53` GeForce/x87/x86/V86/PC98と汎用の混在、`:59,63,68` 診断追記|既存列の順序表＋所有者別の選択集合。名前だけでCPU汎用と判断しない|
|リンク/個別旗|`build/kernel.mk:87,91,92,129,153,154,229,233,260,278,281,284,287,291,293,300,301,306`|x86固有規則を隔離、同じパスを維持。R1_SMALL/個別include/-Osを温存。追加mkも再構築依存へ|
|boot/asmライブラリ|`build/boot.mk:17,24,38,63,110`、`build/libs.mk:121,187`、`build/programs.mk:27,40,108,179,180,307,324,339,413,475,684,685,686`|x86/pc98製品graphに閉じ込める。汎用化済みとは称さない|
|部品の生ld|`build/{cd-module.mk:23,kcg-module.mk:17,fm-module.mk:21,pcm-module.mk:21,v86-module.mk:17,lan-module.mk:31,cirrus-module.mk:21,fep-module.mk:17,trident-module.mk:18,sqlite-proto.mk:26,kdebug.mk:48,hwdev.mk:30,33,65,83,geforce-module.mk:31,100}`|x86値が同じMODULE_LD/MODULE_LDFLAGSに置換。機能offでは各mk自体をincludeしない|
|生成依存|`build/kdebug.mk:79,82` x86ヘッダglob/固定CC、`build/geforce-module.mk:4` x86/pc98ヘッダ、`build/trident-module.mk:8` pc98_init|x86側に残す。PPCでDWARF配置生成を誤実行しない|
|T1 ELF/再配置|`tools/mkmod.py:56,58,59,67,89,153,155,158,163,175,180` LE/EM_386/REL/R_386_32・PC32、`tools/gen_memmap.py:799,805,807,808,810` LE/EM_386|x86専用処理をprofileの能力として明示。machine値だけ変えてPPC対応としない|
|T1 配置/guard|`tools/gen_memmap.py:50,54,63,113,156,232,436` PC98/x86配置と監査、`sdk/link_guard.py:87,103,104,106,107` arとElf32、`sdk/os32x_hdr.py:66,76,78,93,101,121` LE reader|ELFと配置は別profile。ar選択だけではPPC guardは動かないため能力確認を先行|
|入口|`Makefile:44,45,46`以降が全mk無条件include、`:116` allがboot/module/SDKへ、`build/config.mk:11,17` parse時mkdir|最上位で製品graphを分ける。問い合わせもBUILD_OUTを隔離し元の木を汚さない|
- 全所在の意味はB1/B2/T1のbuild・生成器・その利用者。SDK/CRT/Rustの移植（SURVEY K1/K2）、共通C内の残存asmは後段。単なる `-m32` 全件をx86依存と数えない（PPCも32bit）。
- 実物との食い違いは2件：①指定の `parse_make_vars` は存在せず、現行は `make_config_vars:213` / `parse_kernel_vars:229` / `parse_c_kernel:252`。② `build_compare.py:54,66` の合否はbin/mod/VK32であり.o/ELFは含まない（関数差は失敗時のみ`:103,148`）。
- 用語の確認：`ppc-boot` は既存の独立bootstrap段を指す。基点の入口は `build/ppc.mk:25,27` の `make -f build/ppc.mk all`。この経路を残し、後述の利便aliasを足す。

## 2. 選択モデルとmakeの契約
|ファイル（新設）|唯一の責務|
|---|---|
|`build/arch/x86.mk` / `ppc.mk`|ツール・機械旗・ASM方言・emulation。includeパスや装置政策は持たない|
|`build/arch/x86-sources.mk` / `ppc-sources.mk`|CPU専用C/ASM/GAS/shimとincludeディレクトリ。x87は独立featureへ|
|`build/platform/pc98.mk` / `powermac.mk`|対応ARCH、機種専用C/bootとinclude列、既定feature。pc98= x86、powermac= ppcのみを当段で許可|
|`build/features/<機能>.mk`|早期にはソース/includeと遅延適用用のmoduleパス・診断旗の値だけ宣言（規則include/旗適用は禁止）。v86/x87/pc98-devices/geforce/trident/cirrus/pcm/fm/lan/kcg/cd/fep/sqlite/hwdev/kdebugを各1ファイル|
|`build/sources/order.mk` / `common.mk`|前者は旧C/ASM/include/module順序の正典、後者は共通候補の明示一覧。共通候補はPPCで通るという宣言ではない|
|`build/select.mk`|profileとfeatureをincludeして選択、重複/未知値/非対応組合せを検査。ホスト道具にも同じ展開を提供|
|`build/kernel-objects.mk` / `build/kernel.mk`|前者はC/ASM/GAS導出・個別include/-Os/尾呼出旗・.o規則を共有。後者はx86リンク/VK32/paired/R1製品規則のみ。PPCは後者を読まない|
|`build/targets/x86-pc98.mk` / `ppc-powermac.mk`|製品到達点。x86は従来graph、PPCはコンパイルgraph。config単独問い合わせからは製品規則を読まない|
- 読込順はconfig（ARCH/PLATFORM→arch→select早期→共通旗）→共有kernel-objects.mk→x86のみkernel.mk→遅期適用。遅期moduleは旧Makefile:46–58順（kdebug/hwdev/sqlite/fep/cd/kcg/fm/pcm/v86/lan/cirrus/geforce/trident）、診断旗はconfig確定後に適用。C_DIAGNOSTICSは早期に宣言しC_KERNEL導出に含める。`ARCH=x86 PLATFORM=pc98`、全feature既定onの値は現状維持。PPCは非対応feature既定off。
- x86 CC/AR/AS/LD/OBJCOPY の代入方式も `=` を維持（make組込みCCに `?=` で負けない）。コマンドライン上書きは維持。PPCは `PPC_CROSS_DIR ?= /home/hight/opt/cross-ppc` から絶対パスを選ぶ。CROSS_DIRの既存意味を変えない。
- PPC機械旗は既存bootstrapに合わせ `-mcpu=7450 -m32 -mbig-endian -msoft-float -mno-altivec -msdata=none`＋共通freestanding/非PIE/stack protector無効/nostdlib/fcommon/signed-char/enum幅/DEPFLAGS。x86固有のmarch/red-zoneは混ぜない。
- x86の `CFLAGS_MACHINE` は分割片を元の位置で連結する。CFLAGS_BASE、CFLAGS_SQLITE、各INC_*、LIBDIR、SDK公開形、生成刻印は変えない。
- feature offは当段では**ソース・モジュールを選択しないコンパイル構成**。V86/x87/PC98/GeForceを外した完全kernelのリンク・起動は未達。残る呼び手の非活動/NOSYS実装はV1/I2/V2/A4/A5段で行い、成功する空stubで隠さない。
- `FEATURE_V86=1 ARCH=ppc` 等は選択時エラー。x86のfeature offではcompile-onlyを明示指定できるが製品all/kernelは「非搭載サービス未実装」と止める。LGY98既存stampの意味とfeatureによる完全除外は別で、既定x86では従来どおり。
```make
# order.mk: 旧 C_KERNEL を一語ずつ同順に移す（sort/wildcard禁止）
C_ORDER_LEGACY = kdebug/gf5_test.c kdebug/gf_display_test.c ... lib/divdi3.c $(KSTRING_C_SRC) ...
# select.mk: 各集合は重複なし・全旧ソースの所有者は一つ
C_SELECTED = $(C_COMMON) $(C_ARCH) $(C_PLATFORM) $(C_FEATURES)
C_ORDER = $(C_ORDER_LEGACY) $(C_ARCH_NEW) $(C_PLATFORM_NEW)
C_KERNEL = $(filter $(C_SELECTED),$(C_ORDER)) $(C_DIAGNOSTICS)
# kernel-objects.mk: 両graph共有。遅期のC_KERNEL_OBJ +=はこの導出の後
C_KERNEL_OBJ = $(addprefix $(KERNEL_OBJ_PREFIX),$(C_KERNEL:.c=.o))
ASM_KERNEL_OBJ = $(addprefix $(KERNEL_OBJ_PREFIX),$(ASM_KERNEL:.asm=.o) $(ASM_EXTRA_OBJ))
GAS_KERNEL_OBJ = $(addprefix $(KERNEL_OBJ_PREFIX),$(GAS_KERNEL:.S=.o))
```
- `...` は設計上の省略。実装では全旧語を列挙し、選択集合との差分ゼロ・重複ゼロを試験。ただしKSTRING語は別扱いとし、通常x86では `$(KSTRING_C_SRC)` が空なのでorder側に `lib/kstring_c.c` が現れないことを検証する。空側はC_COMMON候補から同語を除いた有効集合と比較し、PPCおよび明示 `KSTRING_C_SRC=lib/kstring_c.c` のqueryでは同語が旧位置に一度だけ現れることを検証する。filterは第2引数の順を保存。ASM_PRE/kstring/POST/shimも同様の順序表でx86の25行目と同値にする。ARM/legacy-x86計測のqueryだけ `KSTRING_C_SRC=lib/kstring_c.c` を明示し旧非空側の測定範囲を維持、通常x86は空。
- `C_COMMON` から明示的に外す対象はarch/x86全体、kernel/v86、exec/v86_call・ring3_ls（C/.S）、CPU専用時計calibrate/sysclk/PIT、PC98専用boot/display/装置、GF専用試験とWC。pc98専用サービスはplatformまたはfeatureの所有。mm/paging等、init/kernel・exec/execなど未分離の**共通候補は残し**計測で障害として出す。
- C/ASMファイル移動なし。kernel-objects.mkに旧kernel.mk:72,87–111,127（.FORCE）,224–226,229,231–254,291–322を責務別に移す。R1の.o規則も同ファイルのx86節に置き、R1のリンクはkernel.mk。x86専用個別規則は選択集合で対象を絞る。`R1_SMALL` 5本（`kernel.mk:229`）、`CORE_KERNEL_INCLUDES`、drivers/gfx/fs/fatfs/exec/kapi/libのパス依存旗を保持。feature診断の追記順はsplash-diag→splash-obs→pcm-e4、相互排他も保持。
- `KERNEL_SRC_DIRS` も旧順序表を所有者集合でfilterし、PPC固有dirを末尾追加。PPCではx86/pc98/V86/非搭載装置への `-I` を入れず、同名ヘッダの誤解決を防ぐ。ただし-I.で届くため、合否は.dに記録された実依存パスを正規化してarch/x86・platform/pc98・非選択所有者ヘッダを検出する。失敗TUは.d未生成を成功扱いせず、必要なら同一argvの依存生成とエラーを記録し漏れを別集計。
### KERNEL_SRC_DIRS の dir→include所有者
`build/config.mk:59` の全31ディレクトリを以下で覆い、列の順序は旧順序表のfilterで保存する。include所有者はソース所有者とは別契約であり、共有ヘッダのディレクトリを装置Cの所有者だけで除外しない。
|include所有者|ディレクトリ（各行内は旧順）|
|---|---|
|common|`kernel include init kernel/console kernel/irq kernel/module kernel/time mm drivers/base fs/ext2 fs/fat fs/hostdrv fs/isofs fs/serialfs`|
|arch/x86|`arch/x86/kernel`|
|feature/fep|`kernel/ime`|
|feature/v86|`kernel/v86`|
|feature/pc98-devices|`drivers/ata drivers/block drivers/char drivers/dma drivers/input drivers/net/ne2000 drivers/pci drivers/video/pc98`|
|feature/lan|`drivers/net/e82557`|
|feature/pcm|`drivers/sound/cs4231`|
|feature/fm|`drivers/sound/fm`|
|feature/cirrus|`drivers/video/cirrus`|
|feature/geforce|`drivers/video/geforce`|
|feature/kcg|`drivers/video/kcg`|
- `drivers/base/dev.h` は `fs/ext2/ext2_priv.h:6`、`fs/fat/fatfs_vfs.c:18`、`fs/fat/fatfs/diskio.c:20` が参照するため、`-Idrivers/base` はPPC/commonにも残す。`drivers/base/dev.c` 自体のpc98-devices所有は変えない。.d監査でもこの共有ヘッダを非選択装置ヘッダと誤判定せず、同dir内のソース所有とは分ける。共通dirにあるCPU/装置専用ヘッダは個別の所有で引き続き検出する。
- **S1で**全選択mkを `PROFILE_MAKEFILES` とし、最上位の遅期include完了後、Makefile:72型の一括通常前提 `$(ALL_TARGET_OBJECTS): $(PROFILE_MAKEFILES)` を追加（order-only不可）。ALL_TARGET_OBJECTSはcore/R1/全13部品/provider/stub/boot/programs/libs/CRTを含む既存変数の和、未選択は空。ld/layoutにも追加。同じBUILD_OUTでfeatureを変えた場合も選択・旗stamp更新で再構築。既定PPC出力は `build/out/ppc-powermac`、x86は `build/out`。異なるtargetで同一出力先の再利用はtarget stamp不一致で拒否。
### 全旧語→所有者（A9）
旧kernel.mkの基底C 216語（kstring_cの非空側を含む）、条件付きC 4語、ASM/shim 9語を下表で一度ずつ列挙。語の順序はorder.mkが保持し、所有者表の並びで再連結しない。純粋な装置補助関数も当段は保守的に装置所有へ置く。
|所有者（各mkの選択集合）|全ソース語|
|---|---|
|feature/geforce|`kdebug/gf5_test.c kdebug/gf_display_test.c drivers/video/geforce/edid.c drivers/video/geforce/tv_data.c drivers/video/geforce/gf_display_data.c drivers/video/geforce/geforce_module.c drivers/video/geforce/geforce_platform.c drivers/video/geforce/gf_wc.c`|
|arch/x86|`arch/x86/kernel/mtrr_wc.c kernel/kstack_hw.c arch/x86/kernel/gdt.c arch/x86/kernel/double_fault.c arch/x86/kernel/tss.c arch/x86/kernel/idt.c arch/x86/kernel/isr_handlers.c kernel/time/cpu_calibrate.c kernel/time/sysclk.c exec/ring3_ls.c arch/x86/kernel/kentry.asm arch/x86/kernel/isr_stub.asm arch/x86/kernel/ring3_entry.asm arch/x86/kernel/setjmp.asm lib/kstring_asm.asm exec/ring3_ls.S`|
|feature/x87|`arch/x86/kernel/x87.c`|
|platform/pc98|`init/boot_platform.c init/boot_display.c platform/pc98/emergency.c platform/pc98/boot.c init/boot_splash.c platform/pc98/splash.c platform/pc98/splash_image.c platform/pc98/clock.c platform/pc98/timer.c platform/pc98/lan_match.c platform/pc98/device_loop.c platform/pc98/device_ide.c platform/pc98/device_fs.c platform/pc98/device_pci.c platform/pc98/device_host.c platform/pc98/device_kcg.c platform/pc98/pci_resources.c platform/pc98/boot_disk.c platform/pc98/memory.c platform/pc98/d2_bind.c platform/pc98/boot_names.c platform/pc98/console.c platform/pc98/display.c platform/pc98/system.c platform/pc98/pic.c platform/pc98/console_attr.c`|
|common（移植成功の宣言ではない）|`kernel/console/font_guard.c init/kernel.c kernel/time/cpu_calibrate_math.c kernel/time/pit_math.c init/bootinfo.c init/bootinfo_check.c mm/paging.c mm/paging_app.c mm/dma_pool.c mm/dma_pool_math.c mm/physmem.c mm/pgalloc.c mm/memory_boot.c mm/shlib.c mm/shm.c kernel/gui.c kernel/input_service.c kernel/time/clock_service.c mm/kmalloc.c kernel/console/console.c kernel/console/con_sink.c init/bootlog.c init/bootlog_save.c kernel/kbd_inject.c kernel/sys.c init/sysconfig.c kernel/kaudit.c init/kaudit_boot.c kernel/kselftest_report.c kernel/irq/irq.c kernel/irq/irq_math.c kernel/irq/tick.c kernel/time/ktime.c kernel/time/time_math.c kernel/ver_collect.c kernel/ver_format.c kernel/console/font_source.c kernel/module/module.c kernel/module/module_image.c kernel/module/module_runtime.c kernel/console/unicode_file.c lib/dma_contract.c gfx/gfx_core.c gfx/gfx_osri.c gfx/gfx_osri_view.c gfx/gfx_vram.c gfx/gfx_scroll.c gfx/palette.c gfx/gfx_emergency.c fs/fat/fatfs/ff.c fs/fat/fatfs/diskio.c fs/fat/fatfs_vfs.c fs/ext2/ext2_super.c fs/ext2/ext2_inode.c fs/ext2/ext2_dir.c fs/ext2/ext2_file.c fs/ext2/ext2_fmt.c fs/ext2/ext2_layout.c fs/ext2/ext2_vfs.c fs/vfs.c fs/vfs_fd.c fs/fd_redirect.c fs/pipe_buffer.c fs/hostdrv/hostdrvfs.c fs/serialfs/sfs_proto.c fs/serialfs/sfs_client.c fs/serialfs/serialfs.c fs/serialfs/serialfs_session.c fs/serialfs/serialfs_persistent.c exec/exec.c exec/shell_service.c exec/appmem.c exec/appmem_map.c exec/appmem_unmap.c exec/redir_access.c exec/access_walk.c exec/surface_query.c exec/system_surface.c exec/lease.c exec/exec_heap.c exec/appslot.c exec/launch.c exec/ring3_str.c exec/os32x_hdr.c kapi/kapi_print.c kapi/kapi_strings.c kapi/kapi_generated.c kapi/kapi_db.c kapi/kapi_sys.c kapi/kapi_osri.c kapi/kapi_host.c kapi/kapi_host_notice.c lib/path.c lib/utf8.c lib/kprintf.c lib/kprintf_attr.c lib/os_time.c lib/kstring.c lib/kutf16.c lib/kmath.c lib/crc32.c lib/divdi3.c lib/kstring_c.c`|
|feature/v86|`arch/x86/legacy/v86.c arch/x86/legacy/v86_mem.c kernel/v86/v86_snapshot.c arch/x86/legacy/v86_module.c arch/x86/legacy/v86_gcap.c arch/x86/legacy/v86_call.c arch/x86/legacy/guest.c arch/x86/legacy/lowmap.c arch/x86/legacy/v86_entry.asm`|
|feature/kdebug|`kernel/module/kdebug_call.c`|
|feature/pc98-devices|`kernel/time/pit.c kernel/port_service.c kernel/serial_diag.c drivers/input/kbd.c drivers/input/kbd_status.c drivers/input/kbd_dlog.c drivers/char/serial.c drivers/char/serial_plan.c drivers/block/fdc.c drivers/block/fdc_decide.c drivers/block/fdc_track.c drivers/dma/dma8237.c drivers/dma/dma8237_math.c drivers/pci/pci.c drivers/pci/pci_hwb.c drivers/pci/pci_decode.c drivers/pci/pci_bind.c drivers/pci/pci_bind_match.c drivers/ata/ide.c drivers/ata/ide_addr.c drivers/block/pc98pt.c drivers/char/rtc.c drivers/base/dev.c drivers/char/np2sysp.c drivers/block/loop_dev.c drivers/input/mouse.c drivers/input/mouse_bus.c drivers/input/mouse_seamless.c drivers/net/ne2000/lgy98.c drivers/net/ne2000/ne2000.c drivers/net/ne2000/ne2000_ring.c drivers/video/pc98/pc98_route.c net/link.c net/link_notify.c net/host_mailbox.c net/nic.c net/nic_rx.c net/nic_lgy98.c drivers/video/pc98/backend_pc98.c drivers/video/pc98/backend_pc98_osri.c drivers/video/pc98/backend_pegc.c drivers/video/pc98/splash_raster.c drivers/video/pc98/splash_raster_image.c drivers/video/pc98/splash_raster_plan.c drivers/video/pc98/splash_raster_play.c drivers/video/pc98/splash_raster_run.c drivers/net/ne2000/ne2000_io.asm`|
|feature/sqlite|`kernel/sqlite_engine.c kernel/sqlite_outer.c lib/sqlite3/os32_sqlite_test.c lib/sqlite3/sqlite_stack.asm`|
|feature/hwdev|`kernel/module/hwdev_module.c`|
|feature/fep|`kernel/ime/fep_module.c`|
|feature/cd|`drivers/ata/cd_module.c`|
|feature/kcg|`drivers/video/kcg/kcg_module.c drivers/video/kcg/kcg.c`|
|feature/fm|`drivers/sound/fm/fm_module.c`|
|feature/pcm|`drivers/sound/cs4231/pcm_module.c drivers/sound/cs4231/pcm_e4_diag.c drivers/sound/cs4231/pcm_e4_guest.c`|
|feature/lan|`drivers/net/e82557/lan_module.c`|
|feature/trident|`drivers/video/trident/trident_module.c drivers/video/trident/resources.c`|
|feature/cirrus|`drivers/video/cirrus/cirrus_module.c drivers/video/cirrus/cirrus_identify.c`|
|platform/pc98（条件付き診断）|`platform/pc98/splash_probe.c platform/pc98/splash_obs.c`|
- 後段module内のbody/provider/generated .Sは上表の旧C_KERNEL外であり、各既存module.mkに残して同じfeatureの遅期includeで選ぶ。GF DMA/BOOTのC_KERNEL_OBJ追記も遅期に保持。新規PPC語は§3の7本のみ（kstring_cは旧語）。
### feature依存（A17）
|要求する機能|必須の組合せ（自動ONなし、欠落はqueryでも明示エラー）|
|---|---|
|pc98-devices|ARCH=x86、PLATFORM=pc98|
|v86 / x87|ARCH=x86（v86のPC98装置呼出しはpc98-devices必須）|
|kdebug / sqlite / hwdev|当段はx86-pc98限定。hwdev→kdebug（pair生成のkdebug.bin）。sqlite→pc98-devices（x86 sqlite_stack含む）|
|geforce / trident / cirrus|全てpc98-devices。geforce→hwdev→kdebug。trident/cirrusはpc98-devicesを要求し、GF専用hwdevを暗黙にONにしない|
|pcm / fm / lan / kcg / cd / fep|pc98-devices必須、fep→sqlite（DB呼出し）。当段のmodule graphはx86-pc98限定|
- off試験は依存元もoffにした閉じた構成を正常対照とし、kdebug=0/hwdev=1等を拒否対照にする。PPCでは上記featureは全off、完全非搭載kernelのリンクは後段。

## 3. `make ARCH=ppc PLATFORM=powermac` の具体的経路
1. 最上位でPPC graphを選び、既定all→kernel-compileへ。x86のboot/modules/programs/libs/sdk/image/deploy規則とMakefileの世代生成・MODULE_PROVIDER_OBJS前提（`:68`以降）をPPC graphへ流さない。
2. `kernel-compile` は選んだCとGASの.oだけに依存。PPCの `C_ARCH_NEW` は現状空、`GAS_KERNEL` は既存 `arch/ppc/entry.S ofw_note.S`、`C_PLATFORM_NEW` は既存 `platform/powermac/{ofw,escc_tree,escc,boot}.c`、C_COMMONにkstring_cを旧挿入位置で選ぶ。
3. 共有kernel-objects.mkの `.c` 規則はPPC CCと従来のパス別旗を使う。`.S` は静的パターン規則 `$(GAS_KERNEL_OBJ): $(KERNEL_OBJ_PREFIX)%.o: %.S` で対象を固定し、recipeを `$(CC) $(CFLAGS_MACHINE) $(INC_COMMON) -c $< -o $@`（hwdev stubと同型、-include unit_stamp等のC旗なし）とする。`exec/ring3_ls.c` と `.S` が共存するため、Cの暗黙パターン規則との競合でGAS対象の入力を誤選択させない。x86 NASM、ring3_ls_shim、PC98ソース、module/SDKのldを実行しない。PPC compileにはx86のLAYOUT_HEADERを一律前提にしない。
4. `arch_io.h/arch_cpu.h/platform_io.h` はppc-mmu待ち。通常makeは共通候補の欠落ヘッダ等で非0終了してよい。`make -k -j1 kernel-compile` で全件を試行し、必須成功集合を `platform/powermac/{ofw,escc_tree,escc,boot}.c lib/kstring_c.c arch/ppc/{entry,ofw_note}.S` の7本に固定する。起草時は独立ppc.mkの実規則で7/7成功。さらに提案のCFLAGS_MACHINE＋C用旗/個別include、.S用旗をconfigから展開して7/7成功、全.oがELF32/BE/EM_PPC=20、生成.dにx86/pc98混入なし（新graph自体は未実装）。新graphでも同じ7本を実行・成功必須とし、一覧欠落・コンパイル失敗・古い.o流用はRED。他の成功TUは実装初回測定で明示一覧に追加固定し以後減少を拒否。ヘッダの空実装・x86へのfallbackは作らない。
5. 到達点は「PPC一覧による実コンパイラ実行＋成功.o＋残課題の列挙」。計測入口の終了値は7本以上の固定成功集合充足・全選択TU試行・ELF検証・依存検査の成否で決め、make -kの非0だけでPASSにしない。第1段の成功TUの.d受入は、正規化・重複除去した非選択ヘッダ依存が下記の固定一覧と一致し、一覧外の依存がゼロであることとする（既知違反ゼロを意味しない）。全Cの成功、kernel ELF、起動成功は受入外。`kernel` は当段ではcompile後に未対応リンクの明示エラー、SDK/モジュール/配備の明示要求はコンパイル前に未対応エラー。
6. `make -f build/ppc.mk all/check` はそのまま保持。`ppc-boot` aliasは `$(MAKE) -f build/ppc.mk CROSS_DIR=$(PPC_CROSS_DIR) all`、check用も同じ転送でcheckを呼ぶ。起草時に無指定転送が `/home/hight/opt/cross/bin/powerpc-elf-gcc` を選び、明示転送でcross-ppcへ直ることを再現済み。後段でbootとkernelの接続時にツール定義だけ共通arch profileへ寄せる（現在のbootstrap独自最適化旗を勝手に替えない）。
- 計測器は変更可能。`ppc-gauge/tools/ppc/check_compile.py:37,53,61,118` はx86 makeを使いARM計測器へPPC旗を足す実装。`--source-profile=legacy-x86|selected` を追加し既定をselectedへ、旧数字はlegacy-x86として別系列保存。
- selectedは新 `tools/build_profile.py` のmake展開結果（ARCH/PLATFORM/feature/C_KERNEL/CC/実コンパイルargv）を使い、各TUの `-c` を実行。ARMのX86_ONLY_FLAGS除去やinclude対応表を流用しない。PPC CC不在は通常計測ではSKIP、`--accept` 受入入口では非0。実コンパイル入口は `tools/ppc/check_compile.py --source-profile selected --accept --json`（NOTEST、tools/ppc/README.mdに実行条件を記載）。
- 生成ヘッダ準備は選択したPython生成規則だけを隔離コピーで実行。無条件の `*-module.mk` globと固定CROSS_DIRを廃止。未選択module由来の欠落は別分類、hwdevのi386 DWARF出力をPPC成功の証拠にしない。JSONに選択順/argv/target/生成失敗/最初のエラー/成功.oのELF class・endian・machineを記録。


### S3 の既知の非選択ヘッダ依存（第1段だけの受入）
- **案 (a) を採用**：実測 `final-ppc.json` と実ソースの include 経路が一致する6成功TU・11組の既知違反を固定し、x86の振る舞い・生成物とC本体・ヘッダの所有を変えず第1段を閉じる。
- 理由：commonは共通候補であり、6 TUを除外すると成功集合を縮めて未分離の依存を隠す。依存の解消は第2段以降の契約分離で行い、当段は増加検出を受入にする。

一覧の実装上の置き場は `tools/ppc/known_header_dependencies.json`（selected / ppc-powermac専用、キーはリポジトリ相対のTUとヘッダの完全一致ペア、後段IDも記録）。初期値は次表だけとし、ディレクトリ・TU単位の包括許可や自動追加は禁止。全成功TUの実.dを `..` を含め正規化し重複を除いて照合し、1組でも追加されたらRED。減少時は消えた組を一覧から削除して再検査するまでREDとし、残存する組を復活用の許可として残さない。成功TUの欠落・失敗・.d未生成を削減とは扱わず、固定成功集合の受入を維持する。失敗TUの.d未生成は従来どおり別集計し成功扱いしない。既知違反はJSONに明示して残し、一覧との一致だけで他の受入を省略しない。

|成功TU|固定する非選択ヘッダ（各パスが1組）|第2段以降の解消先（SURVEY ID）|
|---|---|---|
|`mm/memory_boot.c`|`platform/pc98/platform_memory_desc.h`|第2段 I2/V5：初期化・機種メモリ記述の共通契約へ分離する追加対象|
|`kernel/gui.c`|`kernel/ime/ime.h`、`kernel/ime/ime_render.h`、`kernel/ime/ime_store.h`|第5段 V3：GUIからFEPへの描画接続・非搭載契約を分離する追加対象|
|`init/bootlog_save.c`|`arch/x86/kernel/gdt.h`、`arch/x86/kernel/idt.h`|第2段 I2：起動ログが使う時計・例外の共通宣言を分離|
|`kernel/kaudit.c`|`arch/x86/legacy/v86_mem.h`|第2段 V1：V86監査の非搭載契約を分離|
|`kernel/ver_collect.c`|`arch/x86/kernel/gdt.h`、`arch/x86/kernel/idt.h`|第2段 I2：診断収集が使う時計・例外の共通宣言を分離|
|`kernel/ver_format.c`|`arch/x86/kernel/gdt.h`、`arch/x86/kernel/idt.h`|第2段 I2：診断表示が使う時計・例外の共通宣言を分離|

## 4. T1 profileの口（PPCモジュール形式は未決定のまま）
|profile API|x86-pc98|ppc-powermac（当段）|
|---|---|---|
|`elf_identity`|ELF32/LE/EM_386=3|コンパイル検証用ELF32/BE/EM_PPC=20のみ|
|`module_codec`|現在のKMOD v1、REL、R_386_32/PC32、既存LE wireをそのまま呼ぶ|なし。`unsupported module target ppc-powermac`|
|`layout`|既存include/memmap.h、配置監査・manifest・layout header|なし。`unsupported kernel layout target`。bootstrapのppc_layout.hとは別|
|`link_guard`|既存刻印/世代検査＋i386 ar、出力.inputs.json互換|なし。`unsupported guarded-link target`。独立bootstrapは従来経路|
- `sdk/target_profile.py` を単一正典にし、mkmod/gen_memmap/link_guardへ `--target` を追加。無指定はx86-pc98互換。未知名を拒否し、PPC未対応は入力解析/出力作成/外部ld実行**前**に止める。
- `sdk/link_guard.py --target x86-pc98 --ar <AR> <linker> ...` の新形と従来 `<linker> ...` を受理。guardは既存同様sibling import、tools側はPath(__file__)からsdkを明示してimport。SDK stagingでは正典をguardの隣へコピーする規則を `build/sdk.mk` に追加（SDK公開APIは変えない）。
- 当段はELF全般の共通decoderへ大改造しない。既存Elf/read_elf/H.Elf32には選択profileがx86のときだけ到達。未知machine/endianをx86へ推測fallbackしない。KMOD headerのLEと入力ELFのendianを一つの旗に束ねない。
- profileの入口試験はPPC/未知名/EM不一致/RELA入力/未知relocを拒否すること、x86の既存golden byte・配置・guard判定が変わらないこと。PPCの再配置番号・TOC・PLT・module headerは後段の設計対象。

## 5. 消費者の全検索と壊れ方
- 再現検索：`rg -n 'Makefile|build/[^ ]*\.mk|C_KERNEL|parse_make_vars|kernel_includes|source_roots|clang_ast|make.*(-n|--dry-run)' tools sdk Makefile build --glob '*.py' --glob '*.sh' --glob '*.mk' --glob Makefile`。拡張結果は隣の `consumers.r2.raw.txt`（1120行）、旧索引は `consumers.raw.txt`、build固定値は `fixed.raw.txt`（注釈・coversも含む）。下の索引はr1の全ヒットに必須修正3本を追記したもの。拡張検索の全file:lineはconsumers.r2.raw.txtを正典とする。追加で `kernel\.mk|config\.mk`、間接利用 `kernel_includes|clang_ast|source_roots|parse_c_kernel` も確認。
- **確実に修正必要**：ARMの生テキストparseはinclude先/条件/filter/追記を解決できない。`dep_dir_d_console/display/emergency/splash` はmake -qpの**未展開**C_KERNELをregexで読むため変数化だけでも落ちる。`module_policy:56` のpaired列、`tick:58` / `memory_boot:240` / `shell_service:186` の文字列所属も展開問い合わせへ置換。
- `tools/build_profile.py` は一時BUILD_OUTでmakeに展開させる。基点の製品queryは `make -f Makefile -f <一時mk> __profile` として一時mkのrecipe内で値を展開（parse途中のinfoは禁止）。新木のconfig＋selectだけのqueryは別モード。どちらも隔離写し・一時BUILD_OUTで実行。基点にないprofile-query.mkは要求しない。配列の順序を保存、set/sortは禁止。compile argvは実規則から取得し、特別な-Os/includeをPythonで再実装しない。
- **コピー不足で落ちる**：`test_pcm_e4:50–52` のconfig単体コピーはS1で `build/arch` をコピー対象に追加し、以後追加されるinclude先mkも同時に追従する。`test_src_layout:59,77,155,185,355` の小木/変異は展開APIを保ち、コピー外の本物へfallbackしない。`tools/move_files.py:107` のconfig専用更新も新sources/include列の所在を認識させる。
- **静かな取りこぼし**：`source_roots.py:4,5` はx86 root固定。従来ARM測定の範囲は保持しselected計測だけ全arch/platformを分類してDRIFT対象にする。`clang_ast/build.py:86` はi386 GCCのみ、`:21,163,209` / `__init__.py:142` unitsはdry-run依存。x86検査は明示x86に固定、selected計測は宣言されたCCだけを許可し任意のhost gcc行を拾わない。
- `tools/include_resolve.py:33`、`check_c_dialect.py:434`、no-libgcc/build-no-deploy/依存dry-run試験はx86のargv不変なら継続。C dialect変異`:552,574` は移設後の唯一の製品行へ移す。clang AST unitsの「ビルド設定変更なら全TU」規則は `build/**/*.mk` を維持。
- `tools/checks/run.py:28,124,771,809,857,872,972,1038` のcovers/階層判定は新mkにもtier4を適用。単純ソース追記として高速化する判定にprofile変更を誤認させない。新mkはBUILD_CONFIG_GLOBSにより全CHECKをtier4にするので、既存coversの一括編集はしない。新CHECKだけ実入力を宣言し、全host試験のx86 include順は維持する。ASM_EXTRA_OBJは既存*_OBJ認識を利用し、C_ORDER_LEGACYの変更は単純ソース追加routeへ入れず設定変更tier4に固定。
|全ヒット索引（同じ行の各項目は独立したfile:line）|
|---|
|`tools/check_arm_compile.py:17,20,39,76,95,96,97,135,136,137,138,139,220,223,230,253,255,259,295,296,302,441,451,464,470,531,534`；`tools/check_c_dialect.py:4,31,434,439`；`tools/checks/check-select-host.py:3`；`tools/checks/docs-orphans.py:3`；`tools/checks/fep-budget-host.py:3`；`tools/checks/hwdev-module-host.py:13`；`tools/checks/kcallback-host.py:3`|
|`tools/checks/le-access.py:3`；`tools/checks/map.py:3`；`tools/checks/memory-host.py:3`；`tools/checks/run.py:771,972`；`tools/checks/test_run.py:169,318,322,348,351,354,355,362,363,367,368,369,370,371,372,426,430,431,442,448,459,463,466,471,473,474,475,477,478,479,482,487,488,490,502,503,504,558,561,565,735`；`tools/checks/tests-inventory.py:3`；`tools/checks/vk32-crc-host.py:3`|
|`tools/ci/build_cross.sh:5`；`tools/clang_ast/build.py:92,163,183,209`；`tools/emu_agent/agent.py:28,123`；`tools/gen_hwdev_contract.py:191`；`tools/include_resolve.py:33`；`tools/move_files.py:107`；`tools/nhd_deploy.py:52`|
|`tools/run_host_group.py:63`；`tools/tests/kernel_includes.py:3,16,23,24,25,29`；`tools/tests/test_app_band_pde.py:56`；`tools/tests/test_build_no_deploy.py:36`；`tools/tests/test_c_dialect.py:17,491,493,497,501,525,552,574,576,578`；`tools/tests/test_check_result_cache.py:4`；`tools/tests/test_check_times.py:61,117`|
|`tools/tests/test_ci_stab.py:3,176`；`tools/tests/test_cirrus_module.py:27`；`tools/tests/test_cirrus_win.py:35`；`tools/tests/test_clang_ast.py:61`；`tools/tests/test_con_sink.py:25`；`tools/tests/test_dep_dir_a_l01.py:79`；`tools/tests/test_dep_dir_a_l02.py:46`|
|`tools/tests/test_dep_dir_d_console.py:69,99,198`；`tools/tests/test_dep_dir_d_display.py:76`；`tools/tests/test_dep_dir_d_emergency.py:48,124,158`；`tools/tests/test_dep_dir_d_splash.py:65`；`tools/tests/test_exec_fgpoll.py:10`；`tools/tests/test_exec_hwb_abort.py:15`；`tools/tests/test_exec_syscall_fgpoll.py:11`|
|`tools/tests/test_fep_conn.py:7`；`tools/tests/test_fep_core_boot.py:4`；`tools/tests/test_fep_meta.py:14`；`tools/tests/test_fep_module.py:8`；`tools/tests/test_fep_store_contracts.py:13`；`tools/tests/test_fep_store_info.py:8`；`tools/tests/test_fep_store_search.py:32`|
|`tools/tests/test_fep_user_store.py:9`；`tools/tests/test_font_source.py:44`；`tools/tests/test_geforce.py:3`；`tools/tests/test_geforce_fgpoll.py:5`；`tools/tests/test_geforce_heat_backend.py:12`；`tools/tests/test_gf1_instmem_map.py:13`；`tools/tests/test_gf_command.py:16`|
|`tools/tests/test_gf_display.py:3`；`tools/tests/test_host_mailbox.py:22`；`tools/tests/test_host_notice.py:24`；`tools/tests/test_hostsvc_block.py:13`；`tools/tests/test_hostsvc_block_contract.py:7`；`tools/tests/test_hostsvc_boot.py:51`；`tools/tests/test_hostsvc_contracts.py:16`|
|`tools/tests/test_hostsvc_e82557.py:8`；`tools/tests/test_hostsvc_fd.py:9`；`tools/tests/test_hostsvc_init.py:10`；`tools/tests/test_hostsvc_lgy98.py:8`；`tools/tests/test_hostsvc_lgy98_attach.py:11`；`tools/tests/test_hostsvc_link_api.py:8`；`tools/tests/test_hostsvc_link_core.py:8`|
|`tools/tests/test_hostsvc_media.py:11`；`tools/tests/test_hostsvc_netcd.py:13`；`tools/tests/test_hostsvc_nic.py:11`；`tools/tests/test_hostsvc_nic_rx.py:10`；`tools/tests/test_hostsvc_persistent.py:21`；`tools/tests/test_hostsvc_serialfs.py:21`；`tools/tests/test_hostsvc_sfs_deadline.py:12`|
|`tools/tests/test_hostsvc_sfs_lan.py:11`；`tools/tests/test_hsync_h1.py:49,54`；`tools/tests/test_hsync_protect.py:26`；`tools/tests/test_hwdev_image.py:83,84`；`tools/tests/test_kbd_fgpoll.py:5`；`tools/tests/test_kbd_inject.py:28`；`tools/tests/test_kpeek.py:40`|
|`tools/tests/test_kstring_c.py:26,55`；`tools/tests/test_ledger_device_map.py:10`；`tools/tests/test_ledger_surface_map.py:10`；`tools/tests/test_link_notify.py:21`；`tools/tests/test_memmap_boot.py:35,218`；`tools/tests/test_memory_boot.py:240`；`tools/tests/test_module_policy.py:56`|
|`tools/tests/test_nano_adapter.py:180`；`tools/tests/test_nano_inputs.py:7`；`tools/tests/test_net_link.py:43`；`tools/tests/test_nhd_deploy_failure.py:4`；`tools/tests/test_no_libgcc.py:20,21,64`；`tools/tests/test_np21w_ctl.py:10`；`tools/tests/test_np21w_ini_live.py:4`|
|`tools/tests/test_paging_bounds.py:28`；`tools/tests/test_paging_device_map.py:13`；`tools/tests/test_pcm_e4.py:9,50,52`；`tools/tests/test_pegc_mode.py:38`；`tools/tests/test_pgalloc_range.py:18`；`tools/tests/test_shell_service.py:17,25,45,182,184,186`；`tools/tests/test_sqlite_module_proto.py:4,502,538,539`|
|`tools/tests/test_src_layout.py:7,59,62,77,84,91,113,114,116,117,124,155,185,355`；`tools/tests/test_stat_cmd.py:31`；`tools/tests/test_tick.py:3,58`；`tools/tests/test_trident_contracts.py:13`；`tools/tests/test_trident_emergency.py:15,24`；`tools/tests/test_trident_lifecycle.py:4`；`tools/tests/test_trident_mode.py:18`|
|`tools/tests/test_trident_module.py:20,21`；`tools/tests/test_trident_palette.py:7`；`tools/tests/test_trident_pc98init.py:16`；`tools/tests/test_trident_regs.py:11`；`tools/tests/test_trident_resources.py:10`；`tools/tests/test_trident_restore.py:18`；`tools/tests/test_trident_route.py:14`|
|`tools/tests/test_ver_version.py:82–88`（DEPFILES anchor）、`tools/tests/test_ci_stab.py:228`（clean行）、`tools/tests/test_vmkernel_layout.py:162–165`（VK32規則）、`Makefile:44–64,72–105,116,145`（include/依存/all/clean）を追加。移設先の唯一の実規則を読むよう追従、旧場所にダミーanchorを残さない|
|`tools/tests/test_trident_save.py:16`；`tools/tests/test_trident_vram.py:12`；`tools/tests/test_unicode_lookup.py:8`；`tools/tests/test_ver_version.py:46`；`tools/tests/test_vmkernel_layout.py:162`|

## 6. 独立受入の小段・ファイル別変更量
行数は追加＋変更の概算（削除した移設元は別記）、各段で実装者がfonts不要のx86 kernel・部品比較を完了してPMへ渡し、PM側の全製品比較をもって受入完了とする（§7）。共通Cの物理移動なし。1機能1ファイルを守り、featureの本体実装はこの枝へ混ぜない。
|段|変更ファイルと各行数の見積り|独立受入|
|---|---|---|
|S0 観測口|`tools/build_profile.py` +140、`tools/tests/test_build_profile.py` +140、`tools/build_compare.py` +120、`tools/tests/test_build_compare.py` +100、`tools/checks/build-compare-host.py` +15|現行x86の展開値/全obj/ELF比較を固定。.aはメンバ名・順序・中身で比較。fontsをsnapshotへコピーし、同じ絶対パスで順次clean buildする。programs/lib/SDKもbase/base再現性ゲート対象。profile導入前にも使える|
|S1 ツール選択|`build/config.mk` 45（旧旗20行削除）、`build/arch/x86.mk` +40、`build/arch/ppc.mk` +40、`tools/tests/test_target_toolchain.py` +100、`Makefile` +15（一括通常依存）、`tools/checks/target-toolchain-host.py` +15、`tools/tests/test_pcm_e4.py` 10（:50–52、configのコピーにbuild/archを追加）|x86 argv同値、PPC旗とツールのquery、未知ARCH拒否。まだ製品PPC対応とはしない|
|S2 一覧選択|`build/select.mk` +60、`build/sources/order.mk` +90、`build/sources/common.mk` +45、`build/arch/x86-sources.mk` +30、`ppc-sources.mk` +15、`build/platform/pc98.mk` +55、`powermac.mk` +20、`build/kernel.mk` 75（旧一覧約50行削除）|C・ASM・includeの集合と順序をx86/PPC/feature-offで受入。新mk依存・構成変更時再構築。module・診断旗の遅期適用の受入はS3|
|S2 機能別ファイル|`build/features/v86.mk` +15、`x87.mk` +8、`pc98-devices.mk` +40、`geforce.mk` +18、`trident.mk` +10、`cirrus.mk` +10、`pcm.mk` +12、`fm.mk` +10、`lan.mk` +12、`kcg.mk` +10、`cd.mk` +10、`fep.mk` +10、`sqlite.mk` +10、`hwdev.mk` +10、`kdebug.mk` +10|各機能on/offのC・ASM・include集合を受入。module・診断旗の遅期適用と相互整合の受入はS3。PC98装置群は後段で独立機器へさらに分割可能|
|S2 利用者修正|`tools/check_arm_compile.py` 55、`tools/source_roots.py` 8、`tools/tests/kernel_includes.py` 15、`tools/move_files.py` 15、`tools/tests/test_src_layout.py` 45|make展開APIへ移行、小木/コピー/DRIFT互換|
|S2 試験追従|`tools/tests/test_dep_dir_d_console.py` 25、`test_dep_dir_d_display.py` 15、`test_dep_dir_d_emergency.py` 20、`test_dep_dir_d_splash.py` 15、`test_module_policy.py` 12、`test_tick.py` 5、`test_memory_boot.py` 5、`test_shell_service.py` 5（:186の所属検査を展開化）、`test_c_dialect.py` 15、`test_build_selection.py` +160|既存の所属/変異検出を維持し、単なる文字列を成功条件にしない|
|S2 selector|`tools/checks/run.py` 10、`tools/checks/test_run.py` 35、`tools/tests/test_clang_ast.py` 20|新mkだけ変更しても全必要検査が選ばれる。既存coversを一括削除しない|
|S3 PPC到達|`Makefile` 60、`build/targets/x86-pc98.mk` +100（旧Makefileのgraph移設）、`ppc-powermac.mk` +55、`build/kernel-objects.mk` +160（旧kernel.mkから約140行移設）、`build/kernel.mk` 155（移設削除含む）、`tools/ppc/check_compile.py` 90（gauge枝から取込）、`tools/tests/test_ppc_selection.py` +140、`test_vmkernel_layout.py` 10、`test_shell_service.py` 10（:181–184のcore/R1個別旗規則参照）、`test_ver_version.py` 15、`test_ci_stab.py` 20|PPC実コンパイル開始・成功.o・失敗分類。module・診断旗の遅期適用を含むon/off整合を受入。bootstrap独立経路不変|
|S3 計測入力|`tools/clang_ast/build.py` 30、`tools/clang_ast/__init__.py` 10、`tools/ppc/README.md` +20、`tools/checks/ppc-selection-host.py` +15、`tools/ppc/check_accept.py` +80（NOTEST、実コンパイル結果検証）|宣言CCの識別、selected/legacy-x86の区別、空の計測対象を失敗とする|
|S4 T1|`sdk/target_profile.py` +70、`tools/mkmod.py` 25、`tools/gen_memmap.py` 25、`sdk/link_guard.py` 30、`build/sdk.mk` 5、`tools/tests/test_target_profile.py` +140、`tools/checks/target-profile-host.py` +15|既存x86のbyte/guard/layout同値、PPC未対応拒否、SDK staging単独動作|
|S4 呼出元|`build/kdebug.mk` 8、`hwdev.mk` 12、`geforce-module.mk` 8、`sqlite-proto.mk` 5、`{cd,kcg,fm,pcm,v86,lan,cirrus,fep,trident}-module.mk` 各5|MODULE_LD等の置換、明示profile渡し。x86リンクコマンド本体の順序維持|
|S5 正典|`arch/README.md` 40、`docs/08_build.md` 20|ヘッダ追加だけで移植完了という記述を訂正。profile/選択/能力/計測/未対応の足し方を同じ枝で文書化|
- 新CHECKは `build-profile-host / target-toolchain-host / build-selection-host / ppc-selection-host / target-profile-host / build-compare-host`、tier=1、needs=[]、budget秒=30/30/30/30/15/30。`tools/checks/<name>.py`各+15行（既出を除く）、対応testは各段表のもの。純Python/Makeのqueryと保存ELF fixtureだけで動き、cross compiler不在のCIでも正常対照を実行する。libgccのqueryはclang_ast同様、-print-libgcc-file-nameだけ固定値を返し他の呼出しを拒否する一時シェルをPATHに置く（CC名は変えない）。純queryではLDFLAGSを展開せず、製品dry-runではこのシェルと外部libc.aの一時規則を使いコンパイラを起動しない。profileは正常x86対拒否PPC、選択は正常x86/PPC対未知feature、比較は同一fixture対1byte変更を対照とする。予算は実測で確定、超過は放置しない。
- `ppc-selection-host` は7本の選択・.S argv・alias転送・.d scannerをfixtureで検査し、実行器を成功したふりのfixtureで代替しない。実PPC計測はNOTESTで上記tools/ppc入口からPMへJSONと失敗一覧を渡す。新CHECK登録後のlint/inventoryで未登録試験を拒否。

## 7. 受入→観測試験→変異（各段の完了条件）
- S0でbase/baseの `--all` 再現性ゲートをprograms・lib・SDK・image/isoへ適用する。各snapshotを同じ絶対パスへ順に展開し、毎回clean buildして結果を外へ退避する。base/target比較も同じ絶対パスで順次組む（`build/programs.mk:171,626,675` のcargo呼出しにremap-path指定が無いため）。別パスの同時buildで比較しない。
- git管理外のfontsも同一入力として両snapshotへコピーし、ファイル集合と内容を記録する。IPAexのTTF・生成fontは `.gitignore:33–40` により通常のgit snapshotだけでは入らない。programs/lib/SDKの再現性不一致は原因/差を記録して受入未完了とし、広いmaskや無条件許可はしない。image/isoは再現性ゲート通過後に対象へ追加し、不一致時は追加を保留（現時点では対象未確定）。
- `.a` はコンテナ全体のbyte比較ではなく、重複名を含む全メンバの名前・格納順・各メンバ内容を比較する。`build/libs.mk:106` の `$(AR) rcs` で作るarmapには時刻が入るため、その管理情報を機械語差としない。メンバの欠落・追加・並替え・内容差はREDとし、通常の.o/ELFは全体bytes比較を維持する。
- このPCにはIPAexが無く、x86の `make all` と `--all` は手元で完走しない（読取木の `assets/fonts` はライセンスとREADMEのみ）。実装者はfonts不要のkernel・部品の比較までを実行して報告する。fontsを準備したos32-v3のPM側でS0のbase/baseゲートと `make all kernel-r1` の全生成物 `--all` 比較を実行することがPMの条件。以下の全製品比較・needs生成・NP21/W受入はPM側で完了させ、手元の部分比較を全製品の受入済みとはしない。
- SDKの既存バイナリ/公開ヘッダは同一必須（.aは上記メンバ比較）。S4で意図するtarget_profile.py新設とlink_guard.pyのtarget dispatch差だけは名前・内容の差分を明示して許可し、単独stagingのx86 golden/拒否判定で検証。その他のSDKファイル増減はRED。
|受入|観測可能な試験|必ずREDになる変異|
|---|---|---|
|x86の同じ順序・旗|旧/新のquery JSONを配列で比較：C/ASM/shim/R1/paired/include/全argv。既定＋LGY98 off/diag＋splash diag/obs＋PCM diag。-Osとshell_service尾呼出禁止も確認|C2本を交換、shim削除、FatFS include順交換、R1_SMALLから1本削除|
|x86の機械語・生成物|補強した `tools/build_compare.sh 89a81bc33454035dacadb4281fed91f2a7e64c80 HEAD --all --output <この作業領域>/compare`。--allではmake all kernel-r1を実行し、kernel/kernel-r1＋全paired部品＋bootの.o/ELF/bin、programsの実行像、libの.o/.a、SDKのCRT/.a/ヘッダ/スクリプト/公開配置を相対パス集合とbytes（.aはメンバ名・順序・内容）で比較。生成先build/out以外（boot/programs/userland/sdk/build/sdk）も対象。追加診断構成も同じ手順|未参照関数の命令1個を変更：binが同じでも.o差で失敗。部品ELF削除も失敗|
|差の除外を限定|両snapshotを `OS32_BUILD_COMMIT=layout-compare SOURCE_DATE_EPOCH=946684800` で生成しbuild_idと__DATE__/__TIME__だけ固定。ELF全体比較を先行、差時objdump -drwとsection bytesで理由を報告。build_id.o丸ごと除外しない。.d/.map/guard inputs.json/ログ中の絶対作業パスは機械語比較から外して別報告|日時以外のrodata変更も失敗。ELF symbol/section消失を「命令同じ」で許さない|
|非搭載とPPC選択|featureごとoffのquery/dry-run、実 `make -k -j1 ARCH=ppc PLATFORM=powermac kernel-compile` を隔離コピーで実行。x86/pc98のTU/ASM不在、成功.oのEM_PPC/BE、固定7本以上の成功集合と.d実依存、失敗TUと理由が一覧に残る|共通のkstring_c TUへarch/x86/arch_io.h include追加（実.dでRED）、必須7本の1本除外、PPC CCをi386へ、失敗分類を成功へ書換|
|graph遮断|PPCのall/defaultにSDK/module/boot x86生成が含まれない。未対応kernel-link/module/deploy要求は指定メッセージと非0。既存bootstrap all/checkを別途実行。aliasは環境/コマンドラインCROSS_DIR=x86でもPPC_CROSS_DIRが子へ渡ることをdry-runと実7本で確認|PPCでx86 allをinclude、module生成器を無条件前提に戻す、aliasのCROSS_DIR明示を削除|
|profileと再構築|x86 golden module/配置＋guard世代検査、stagingしたguardでarchive入力検査。PPC/未知target/異machineを出力未作成で拒否。S1からprofile mkをtouchしcore/R1＋13部品の通常C/provider/stub .oとboot/programs/lib/CRTがmake -nで再構築されること、無変更時は不要であることを確認|未知targetをx86へfallback、arを固定値へ、profile依存mkを1本削除|
|検査の追従|小木のquery、test_runでbuild/arch/ppc.mk・build/sources/order.mk変更→tier4、unitsの設定変更→全TU、各改修試験の局所変異を実行|copy先のmkを無視し元の木から読む、BUILD_CONFIG_GLOBSのbuild/**/*.mkを削除し新パスtier4のassertを破る、PPC CC行を黙って捨てる|
- 実装時環境は `CROSS_DIR=/home/hight/opt/cross HOST32_RUNNERS=qemu TMPDIR=<この作業領域>/tmp`。PM側のfontsを準備した隔離コピーでpreflight（指定基点、fonts修復）→ `make all kernel-r1 boot-debug` と各試験のneeds生成→改修試験 `--mutate` → `python3 -B tools/checks/run.py --lint`。元の読取木では実行しない。
- PACK指定 `CC_MODE=fast CC_TIER=4 CC_MUT1='' CC_FILES_JSON='[]'` の **373件のCC_STAGE1全列を候補として保持**（正確な列は同じディレクトリのPACK.md）。`CC_NEEDS='all assets/fonts/ipaexg16.kcgfont boot-debug build/out/geforce/geforce_imports.h build/out/kernel.elf kernel-r1 osri-enc2-core sqlite-module-proto'` を先に満たす。
- 本線の増減後はselectorで候補を再算出し差を記録するが、今回必要な上記検査を落とさない。373件=実行済みとは書かない。ローカルのmake check-changed/全mutation一括は行わず、全件非変異は着地CI check/build check-fast、全mutationはfull-check workflowへ（読取元AGENTS.mdの運用）。追加/修正検査の局所変異だけ前景で完了させる。
- PMへの渡し方：S0→S1→S2→S3→S4→S5の受入済み小コミット相当差分。S2は一覧と消費者を同時着地。S3の規則分割ではver_version/ci_stabを再実行しvmkernel_layoutの移設参照も同時着地。Trident/GeForce/Host Servicesの本線がmkを触るため、PMが最新基点を確定し順序表を再採取、各所有者の追加語を同位置へ反映して比較し直す。古い全mkをコピーで上書きしない。
- 衝突削減：部品mkはS4のld/profile引数数行のみ、graph移設はMakefileの機械的切出し。feature一覧は以後その機能の1ファイルへ追記、順序表の挿入位置を同時指定。flags/リンク順/生成物比較のログは最新差分後のものだけを渡す。
- NP21/W受入は全段os32-v3のPMが実施し、実機の回は設けない。各段の比較ログと同じx86成果物・deploy-setを渡す。配備先はPM検証用NHD `build/nhd/os32.nhd`→設定されたNP21W_DIR、HostDrv `C:\os32`。停止→`make CROSS_DIR=/home/hight/opt/cross deploy-kernel`→起動を行う（userlandと/sysも一式配備）。ISO/FDはS0の再現性ゲート通過後だけ同じ検証用エミュレータで起動。
|段|PMが保存するNP21/W観測（各段共通でserial/画面・deploy-set照合を記録）|
|---|---|
|S0|基点像で起動→shell prompt・ver表示・世代照合を記録。起動時kdebug/fep/cd/kcg/fm/pcm/v86/lan/cirrus/hwdev/geforce/tridentの搭載対象のload/CRC結果とSQLite初回DB操作時のloadを基準化。非搭載装置はその状態を記録|
|S1/S2|新像を停止中に一式配備、S0と同じ機器構成で同じ起動・paired load・shell/入力/表示/DB操作を比較。compile-only off構成は配備しない|
|S3|x86共有.o規則の新像で同上、R1はそのpaired一式と対でt2h専用の検証用NHDへ配備する。`build/deploy.mk:108–112` に従い、stop → umount → pull --profile t2h → `make CROSS_DIR=/home/hight/opt/cross deploy-kernel-r1 PROFILE=t2h` → deploy --profile t2h → start の順で行い、fixtureの成功ログを照合する。通常のdeploy-kernelへR1を流さない。PPCはNP21/W対象外、固定7本の測定JSONを添付|
|S4/S5|profile経由の全製品を配備し同上、module拒否/世代不一致の新規エラーなしを確認。S5は文書手順を同じ像で追試。全paired集合はKERNEL_PAIRED_MODULESと観測を突合し漏れを残さない|

## 8. 未決
- **ユーザー判断が今必要な未決：0件。** 当段の到達点と案Dは決定済み。PPC module形式、完全非搭載サービス、PPC kernel配置は後段で設計し、その段で必要な判断だけ提出する。推奨は本書のcompile-only＋明示未対応停止で着手し、ABIを先回りして固定しない。

## 反論
- 0件。起草で確認した反例はPM決定と整合した。

## 確定 (2026-11-07、Opus 5.5 第 2 回の条件付き Approve を反映)
- B1: .aのメンバ比較、fontsコピー、同一絶対パスの順次build、programs/lib/SDKの再現性ゲートを反映。
- B2: S2はC・ASM・include集合の受入とし、module・診断旗の遅期適用の受入をS3へ移した。
- B3: test_pcm_e4:50–52のコピー修正をS1へ移し、build/archを対象に加えた。
- B4: kernel-objects.mkの移設範囲に.FORCEとR1の224–226・231–254を加えた。
- B5: shell_service:181–184・ver_version・ci_stabをS3へ移し、S2はshell_service:186の展開化とした。
- B6: KSTRING語の空・非空を差分検査で別扱いとする契約を明記した。
- B7: GASの.S規則をGAS_KERNEL_OBJ対象の静的パターンに固定した。
- B8: KERNEL_SRC_DIRS全dirのinclude所有者表とdrivers/base共有を明記し、R1のt2h専用配備前後の手順を反映した。
- 追加（IDなし）: IPAex不在による手元の比較範囲と、os32-v3 PM側で全生成物比較を完了する条件を明記した。
- S3 の実装で埋めた欠け：成功TUの非選択ヘッダ依存6 TU・11組は§3の固定一覧で増加・陳腐化を拒否し、後段IDへ解消を引き継ぐ受入に確定した（実装・再計測の合格は別途確認）。
- ppc-mmu の取り込みの後 (2026-11-07、PM): arch/ppc/arch_io.h が揃って成功 TU が 69→86 本に増え、同じ検査が新しい依存を見つけた。固定一覧は 11 TU・20 組 (exec/ring3_str.c、kernel/irq/irq.c、kernel/sys.c、kernel/time/ktime.c、mm/paging.c を追加)、selected_success.json も更新。製品のコードは変えていない。mm/paging.c の v86_mem.h・platform_memory_desc.h は案 D で mm を繋ぐ前に解く。
