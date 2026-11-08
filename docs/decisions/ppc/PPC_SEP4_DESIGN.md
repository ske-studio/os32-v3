# PPC_SEP4_DESIGN — 分離の第 4 段 (型の幅・配置・モジュール)

> 状態: **現行 (2026-11-09)** — 共通のコードが i386 の型の幅・符号・配置を暗黙に前提にしている所を、ビルドの契約 (-fsyntax-only の static assert・生成器の target 別の出力) として明示する。x86 の ABI・配置・生成物は変えない (対象 D1〜D4)。段の状態は票 TASK_PPC_SEP が持つ。
> 出所: ~/os32-tmp/run/ppc-sep4/DESIGN.final.md を 2026-11-09 に写した (gpt-6-astra 起草 r1〜r2、Opus 5.5 第 1・2 回、PM が第 2 回の P3 と PM 決定を反映)。
## r1→r2 の変更
- ID 1（PM 決定）：型・record/assert probe を `-fsyntax-only` に統一し、.o と build/out の出力を作らない。
- ID 2（PM 決定）：PPC 依存検査・別 pin は手元/PM 専用へ分離し、CHECK/DEFERRED/CI の対象外、段の受入証拠として記録。
- ID 3：新規則は Makefile から include し、build/config.mk とその写し試験を変更しない。
- ID 4：probe の recipe を製品 flags + probe 専用 include + `-fsyntax-only` と明記。
- ID 5：host32 は libc/crt 不要の `_start`・int 0x80 と `-nostdlib -static` で作る。
- ID 6：機械 flags の検査対象を C recipe に限定し、Makefile/build/*.mk の字面・flags 経路検査を指定。
- ID 7（PM 決定）：PPC 整数非整列の説明を修正し、製品変更段 C と native_i32_load_unaligned を外部アプリ再開まで延期、strict-align は実測後に決定。
- ID 8：uptr の出典を include/types.h:41–43 のみに修正。
- ID 9：HWDEV PPC layout-only probe の PC98/paging include を明示的な例外にする。
- ID 10：GUI/PCI 既存 assert を再利用し、新規検査を PPC 照合と shell 写し一致に限定。
- ID 11：第3段 A は完了済みのため段 A の依存を「なし」に変更。

- 基点 `5b780d18`（実 HEAD 一致）、2026-11-08。調査 D1〜D4 のみ。リポジトリ無変更、コンパイラ・preflight・ビルド・試験未実行（PACK の指定）。
- 目的：x86 ABI/配置/振る舞い不変。案Dのソフト2段表を維持。PPC32/BE は配置検査まで、カーネル整数のみ、module/SDK 実走を解禁しない。NP21/W は PM、実機要求なし。外部未ビルド（H-7）。

## 1. 実物との照合（内容の食い違い2件、単なる行番号移動は数えない）
|調査|5b780d18 の証拠|判断|
|---|---|---|
|D1|`include/types.h:25–43`、`sdk/include/os32/os32_kapi_shared.h:21–26` は u32/i32 が long。uptr は `include/types.h:41–43` のみ。`build/config.mk:139,143,180` が flags を合成|幅 assert 不足は存続。ただし signed-char/no-short-enums は `build/arch/x86.mk:11`・`ppc.mk:8` に既設（食い違い1：未固定ではない）。ABI_WORD_* は引数語、u64 整列の代用にしない|
|D2|`hwdev/generated/hwdev_contract.h:2509–2510` の gf5=1768/align4、`hwdev/gf/feature5.h:150–170`|失敗要因は存続。旧2439/2440・feature5:138という参照位置は移動|
|D3|`sdk/include/os32/os32_gui_shared.h:256,278–288`|Widget=6/value@2、Event=16/value@10 の packed は存続。失敗・例外の実測は未確認|
|D4 PCI|`include/pci_dev_contract.h:29–46`、`userland/shell/cmd_pci.c:23`|native 数値のコピー。u16 align2/u32 align4なら両targetで40B、bar@12、command@38。PCI config 空間の LE バイト列とは別|
|D4 boot|`include/bootinfo.h:8–11` は共通 native。`platform/pc98/bootinfo_native.h:5–28`、`bootinfo_wire.h`、`bootinfo_check.c:17–34,83–121` に分離済み|旧 include/bootinfo.h:95 の i386 配置指摘は現所在に合わない（食い違い2）。wire は offset 読み、native の memcpy 解釈ではない|
- `arch/README.md` は build/source/target 選択と selected の限界を既に記載。追加は「型幅・整列・byte orderを別々に検査」「module native 配置はtarget毎」だけ。
- 第3段 §2 A2 と末尾確定に従う。現 shared.h:32–33 の旧 cdecl 定義と A2 設計の差は第3段所有であり、本段で SDK ヘッダを編集しない。

## 2. gf5 手計算と PPC のアクセス条件
|位置|x86（生成値）|PPC32、long long の構造体内8整列を採用した計算|
|---|---:|---:|
|fan|132、sizeof=136、終端268|同左|
|next_auto/next_step/next_sensor/next_toggle|268/276/284/292|272/280/288/296（最初に4B padding）|
|tile/tile_live/tile_slot|300/540/552|304/544/556|
|image/plan_guard/rom_generation/heat|1656/1660/1720/1724|1660/1664/1724/1728|
|stop/temperature/終端|1760/1764/1768|1764/1768/1772|
|sizeof/align|1768/4|1776/8（末尾4B padding）|
- 根拠：feature5.h:12,83–94,151–169、生成ヘッダ:2525–2567。struct gf_production の内包配置も連鎖する（生成値:3259–3272）。gf5だけの定数差替えでは不足、全 inventory をtarget compilerから再採取する。
- BSD参照の接頭辞は `/home/hight/os32-tmp/ref/`。`openbsd/sys/arch/powerpc/include/_types.h:54–68` は最大整列/stack16/整数幅、同 `endian.h:31` はBE。`netbsd/sys/arch/powerpc/include/int_types.h:58–67` はcompiler整数型と32bit時long long。これは幅と整列を別管理する手本であり、OS32 compiler の構造体配置の実測ではない。
- 8整列は採用するPPC SysV/EABI契約。上表はその規則からの予測、実 compiler の flags/ABIによる一致は未確認。実装時は `{ char c; unsigned long long n; }` の offset を検査（typedef自体の `__alignof__` は i386でもpreferred alignmentを返し得るので不適切）。
- PPC BE の通常整数の未整列 load/store はハードが処理する見込みで、packed が分割命令を保証するとはしない（命令列・実行は未確認）。soft-float 前提でも lmw/stmw・lwarx とキャッシュ禁止領域（MMIO）は別扱い。7450 のメーカー資料で命令/領域条件を確認すること。
- `openbsd/sys/arch/powerpc/powerpc/trap.c:577–600` の fix_unaligned は LFD/STFD のみで、整数の一般的な救済の根拠にならない。同 :409–420 の USER 補正失敗 SIGBUS も OS32 仕様には転用しない。OS32 PPC の回復/kill方針は未実装。

## 3. 契約の境目と所有
|対象|byte order/配置の決定|実装方法と所有|
|---|---|---|
|内部型|ILP32、char signed、enum4、long4、pointer4、u64=8B、member alignはx86=4/PPC=8|新 include/type_contract.h → 固定名 arch_type_contract.h。provider は arch/x86 と arch/ppc。ABI_WORD_*と独立。状態なし|
|公開 KAPI/GUI/PCI record|x86の全配置とLEを維持。PPCでは同じoffset/sizeで**target native endian**。異target間で生コピーしない|同一targetのkernel/userだけの交換形式。ディスク・ネット形式ではない。SDK公開字面変更なし。GUI（os32_gui_shared.h:278–290）とPCI（drivers/pci/pci.c）の既存STATIC_ASSERTを再利用し、新規はPPC照合とshell写しとの一致のみ。pointerを含むKAPIは常にtarget ABI|
|PC98 boot wire|既存LEバイト列とoffsetを固定、PPCで解釈するときもLE|既存 `lib/le_internal.h:6–29`、PC98 rd16/wr16/wr32を維持。boot_commonはproviderがnative値を公開。powermac製品にPC98構造体を持ち込まない（HWDEV PPC layout-only probe の umbrella が PC98 native/paging を引くのは例外）|
|gf5/HWDEV内部|native target別。x86既存配置、PPC上表。保存imageも借用pointerを含む同一実行内データ|productionがgf5を所有、image/trip/copiesはcaller、core/loaderは既存所有。生成物で両者のCRC一致を検査。永続wireへの流用禁止|
|moduleコンテナ|既存x86 codecを維持。PPC codec/relocation未実装の拒否を維持|layout-only検査の成功をmodule可動としない。target_profileの能力gateは緩めない|
```c
/* include/type_contract.h（製品公開ヘッダからはincludeしない） */
#include "types.h"
#include "arch_type_contract.h" /* member-align=4/8, endian=LE/BE */
enum type_contract_enum { TYPE_CONTRACT_ZERO, TYPE_CONTRACT_MINUS = -1 };
struct type_contract_u64 { char c; unsigned long long n; };
STATIC_ASSERT(sizeof(int)==4 && sizeof(long)==4 && sizeof(void *)==4, ilp32);
STATIC_ASSERT(__CHAR_BIT__==8 && (char)-1<0, signed_char_8);
STATIC_ASSERT(sizeof(enum type_contract_enum)==4, enum_32);
STATIC_ASSERT(sizeof(unsigned long long)==8, u64_width);
STATIC_ASSERT(__builtin_offsetof(struct type_contract_u64,n)==ARCH_U64_MEMBER_ALIGN, u64_member_align);
/* 実装ではu8/i8/u16/i16/u32/i32/uptr・short幅・u16/u32 member-align・byte orderもassert */
```
- includeの循環・HWDEV preprocessed CRC・LP64 hostへの波及を避けるため types.h 自体には追記しない。新 tools/abi/type_contract.c の recipe は `$(対象CC) $(製品profileの機械・言語を含む実CFLAGS) -Iarch/$(ARCH) -Iinclude -fsyntax-only tools/abi/type_contract.c`。追加 -I はprobe専用で製品flagsを変えない。x86 kernel/userland/SDK/SQLite（GNU89）各profileを必須prerequisiteで毎回評価し、.o/出力ファイルなし。前提は order-only で書く（例 `kernel.elf: | abi-contract-probe`）。通常の前提にすると出力の無い probe のために kernel.elf が毎回リンクし直される。公開ヘッダ単独GNU89 probeも同方式。PPCは手元/PM専用で実施する。
- C recipe のみを対象に Makefile・build/*.mk の `-f(unsigned|signed)-char` / `-f(no-)?short-enums` の字面とflags変数の経路をhostで照合し、既存profile以外の上書きや未登録のC recipeを拒否する。機械flagsを継承しない例外C recipeも同じprobeへ登録。.S recipe（kernel-objects.mk:65,103）は対象外。
- record probe は既存GUIヘッダのassertをそのまま評価する。PCIはkernelの共通型とshellの写しについてsize/全field offsetを照合し、既存assertの重複新設を避ける。PPC定数採取は手元/PM、x86写し照合はhost CHECKに分ける。
- D3：GuiEvtWidget の製品利用はツリー内0件。製品アクセス変更段 C と native_i32_load_unaligned の導入・fixture検証は外部アプリ再開まで延期する。再開時にPPC -O0/-O2命令列と7450資料を確認し、その後に -mstrict-align の要否を決める（本段ではprofileを変更しない）。LE wireには既存 le_read32 を使い、nativeと混同しない。

## 4. HWDEV 生成器
- `tools/gen_hwdev_contract.py:99–156` は DWARFから**名前と親子だけ**を列挙、target objectの定数配列でsize/align/offset/enumを取る方式を維持。typesをPythonで再構築しない。
- 現在の固定箇所は readelf:122、objcopy:139、`struct.unpack('<...')`:141、cdecl stubs:176–183、CLI:195–214、呼出 `build/kdebug.mk:81–85`。compiler名の文字列置換による推測をやめ、既存profileから明示のcc/readelf/objcopy/ELF class,data,machineを渡す。
- CLI案：`--target x86-pc98|ppc-powermac --readelf PATH --objcopy PATH [--layout-only]`。既存generate(cc,flags,includes,...) の位置引数とx86既定を残す（test_hwdev_image.py:239 の直接呼出を壊さない）。未知target・ELF不一致・PPC full生成は出力前に拒否。
- x86は現 `hwdev/generated/` の**全ファイルbyte一致**、同じCRC。PPCは `build/out/ppc-powermac/hwdev-layout/` に `hwdev_contract.h`（umbrella+全assert+CRC、実装なし）と `hwdev_layout.txt` のみ。i386 stub/core/moduleを生成しない。
- endianはtarget objectのEI_DATAから検査して `<I` / `>I` で読む。ELF32/EM_386またはEM_PPCを同時に照合。指定とobjectが違えばfail。raw長さが式数×4であることも検査する。
- PPC layout-only umbrella が `platform/pc98/bootinfo_native.h` と `mm/paging.h` を引くのは配置採取だけの例外で、PPC製品の依存許可ではない。
- 同じ `tools/hwdev_layout.json` の名前/メンバ集合を双方で照合、seedで差を黙認しない。PPCに異なるenum値や欠落型が出たら設計逸脱。PPC outputをkernelの既存相対includeへ無理に差し込まず、独立probe TUがその絶対pathをincludeする。
- CRCの入力順/正規化/x86出力を変えない。target識別は出力ディレクトリとELF gateが担う。PPC layout CRCは実装時の採取後reviewで固定、予測値を手書きしない。

## 5. 実装段・変更台帳（各行1担当、半日〜1日。+新規、他は変更行概算）
|段/依存|ファイルごとの台帳|区分・指紋/容量|受入|
|---|---|---|---|
|A / なし（第3段A済み）|include/type_contract.h +55、arch/{x86,ppc}/arch_type_contract.h 各+15、tools/abi/type_contract.c +8、tools/tests/test_type_contract.py +130|I、製品0B、KAPI/SDK/CRT/FEP不変|H1|
|B / A|build/abi_contract.mk +55、Makefile 3（ここからinclude）、tools/tests/test_type_contract.py 35（C recipe/flags検査）|I、probeは-fsyntax-only、config.mk不変、既存profile flags不変|H1/H5|
|C-layout / A（製品変更Cとは別）|tools/abi/record_layout_contract.c +30、tools/tests/test_record_layout.py +100|I、既存assert再利用、shell写し一致、公開字面不変、製品0B|H2/H3|
|D / C-layout|tools/gen_hwdev_contract.py 90、tools/tests/test_hwdev_target_layout.py +170|I、x86生成物byte一致、PPC検査出力追加|H4|
|E / B,D|build/kdebug.mk 12、build/abi_contract.mk 15、tools/tests/test_hwdev_module.py 12、tools/tests/test_hwdev_image.py 15、tools/tests/hwdev_contract_fixture.py 12、tools/ppc/check_layout.py +150（A/C-layout/DのPPC検査を集約）|I、既存CRC pin維持。PPC別pinはPM専用driverで管理、x86を上書きしない|H4/H5|
|F / 全段・第3段F後|arch/README.md 12、docs/tasks/ppc/TASK_PPC_SEP.md 10、tools/tests/test_record_layout.py 20、tools/tests/test_hwdev_target_layout.py 20|I、selected90/known21維持、差は実測のみ記録|H1〜H6|
- 見積合計約1,200行（うち試験・PM検査約750行）、生成物は自動出力で別計上。元計画の150〜500行は製品/生成器のみなら約400行、試験を含めると不足。上の同一ファイルは必ず直列、各段で試験も同担当が追従。
- 第3段D2/D3のkapi_strings/kapi_print/kapi_call、E2/E3のsyscall生成/文字列adapterには触らない。第3段Fのselected/known、host fixture・ABI境界試験と統合時に競合し得るのでF後に基準を再採取。SDK A2指紋変更は第3段の基点へ帰属、本段と混ぜない。
- 容量：型/record/assert probeは-fsyntax-onlyで.oなし（生成器の定数採取objectは既存方式、PPC分はPM専用出力）、kernel/R1/起動FDの増分0を目標。x86生成物のtext/rodata/data/bss・relocation・ELF load section・vmkernel.lz4・R1・FDを比較。PPC gf5は+8B、内包側も増えるがPPC製品は未生成。延期したruntime copy段を再開する場合だけ容量を別計測。
- [ABI] 本案は sdk/include/os32/*.h を変更せず、KAPI番号/版/SDK/CRT/FEP指紋不変。`gen_sqlite_imports.py:40–45` と `tools/prototypes/fep_imports.def:3–6` の公開ヘッダ字面hashを確認した。公開字面変更が必要になった場合は変更段を分離し、対応生成器からFEP/importsと関連KAPIを再生成、ABI変更なら版・世代とSDK/CRTを一体更新、`make clean`→`make all`、NP21/W module load/アプリ実行、外部未ビルドを記録する。

## 6. 受入項目 → 試験 → 変異（一項一行）
- H1 型幅/符号/member整列：CI CHECKはhost32 runtime＋x86の各profile/SDK GNU89 syntax probe。host32はtest_abi_boundary.py:41–51と同じ `gcc -m32 -ffreestanding -fno-pie -no-pie -nostdlib -static`、`_start`からint 0x80で終了、host32.build/run（qemu）を使いcrt/libc不要。unsigned-char・enum短縮・long幅誤りは個別anchorで拒否しhost driverのruntime assertionでRED。PPC syntax probeとalign期待値4への変異はPM専用。
- H2 GUI/PCI：CI CHECKは既存GUI assertとkernel PCI/shell写しのsize/全field offset一致を確認 → shell写しのbar@12を16へ変異しruntime oracleでRED。PPC object定数照合とGUI value@2を4とする期待値変異の拒否はPM専用。新規の非整列copy/helper試験は延期。
- H3 boot/endian：既存test_bootinfo/test_boot_readers＋LE固定vector（12 34 56 78→0x78563412）をCI CHECKで検証 → le_read32のbyte順交換でRED。PPC syntax成功はPM専用で記録し、BE runtime検証済みと称さない。
- H4 target生成：CI CHECKはtest_hwdev_module/test_hwdev_image、新target-layout hostのx86全生成物byte一致とsynthetic BE/ELF fixtureによるdecode/gateの否定試験 → endian逆decode、ELF machine不一致、offset/CRC破壊、PPC full生成許可の各変異をruntime oracleでRED。実PPC compiler/tool選択・全assert syntax検査・gf5=1776/8照合・別pin確定はPM専用。既存loaderのCRC不一致拒否も維持。
- H5 不変：tools/build_compare.shで基点とbuild_id/日時等条件を揃え比較＋FEP_MODULE_ABI_GEN/SDK/CRT照合 → probe の recipe から `-fsyntax-only` を外して build/out に .o を出す変異 → build_compare が DIFF になることを確認。x86 assertだけの段なら追加NP21/W不要。
- H6 selected（PM専用）：tools/ppc/check_compile.py --source-profile selected --accept --json、実.dと失敗理由を保存 → 成功一覧への偽TU追加/knownの偽削除をvalidatorが拒否。現90成功、known21組のうち本段で削除できる組は0、新規成功保証TUは0。
- H6根拠：`build/platform/powermac.mk:2` はfeature既定off、`build/features/hwdev.mk:2` のkernel/module/hwdev_module.cはselected外。旧調査の1失敗解消はlayout独立probeで確認し、selectedの+1とは数えない。既存knownはIME/PC98/個別device依存のみで型配置とは無関係。将来HWDEV選択時にも他のarch依存が残るので成功を先約しない。
- C（製品アクセス変更段、延期）：外部アプリ再開時に実caller・命令列を確認して再設計する。helper追加やcopyへの変更を本段の受入条件には含めない。機械語が変わる場合は別途容量比較とPMのNP21/W受入を定め、Iと偽装しない。
- 検査の分離：test_type_contract.py/test_record_layout.py/test_hwdev_target_layout.py のCHECKとinputs/coversはPPC toolchain不要の部分だけ。tools/ppc/check_layout.py の実PPC検査はcheck_compile.py同様、CHECKもDEFERRED登録もしない。make check-fast/CIから起動せず、x86 makeの依存にも入れない。PMが使用tool/flags・採取値・別pin・合否を段の受入証拠に残す。PPC未実施をCIのSKIPで代替しない。
- 実装時は触ったkernel/R1/userlandをビルド、担当試験--mutateとlint、新CHECKのinputsを宣言。製品行を動かす前に全mutation anchorを検索し移す。landingはCI check/build、全mutationはfull-check、check-changedなし。本設計では全検証未実行。

## 7. host試験・coversの全検索台帳
- 検索語：types.h / os32_kapi_shared.h / feature5.h / os32_gui_shared.h / pci_dev_contract.h / bootinfo.h / gen_hwdev_contract / hwdev_contract.h / REVIEWED_LAYOUT_CRC / INVENTORY_SHA を `rg -n` でtools/tests・tools/checks全体から取得。以下は一致ファイル全件（番号は最初の一致行、単なる文書も除外しない）。末尾の追加走査も併用する。
- build/config.mkは変更せず、新build/abi_contract.mkはトップMakefileだけからincludeする。config/selectのみ写すtest_pcm_e4.py:46–53・test_tick_probe.py:8には新依存を作らないため、config.mk参照87本の変更台帳追加は不要。
- 追従規則：HEADERS/TREE_FILES/MIRROR/FILESの写し・自前gcc/-I・関数抽出は新probeの根を明記。共通types.hを変更しないため既存host LP64を一斉ILP32化しない。変更する生成器/fixtureの依存はCHECK coversにも加える。抽出関数には新helperを同じ木から供給し、元repoへのinclude逃げを禁止。
- 重点：test_hwdev_image.py:239 のgenerate直接呼出、test_hwdev_module.py:23,46,131 の自前flags、test_gf5.py:381,427 の写し/gcc、test_bootinfo.py:38,101,165、test_pci_bind.py:75,90,108、test_abi_boundary.py:49,68。kernel_includes.py:25–45 はx86を明示し写し先へrebaseする。PM専用PPC検査でこのx86 helperを流用しない。
- 固定値：hwdev_contract_fixture.py:62 のREVIEWED_LAYOUT_CRC=0x927d90af、:64 のREVIEWED_INVENTORY_SHA=1b5fe57d12d1adaf489c2c43f8dcd707842f99ef4181b1af65cca73ccdf65214。名前集合不変なのでinventory pinは維持。x86 layout pinをPPC値に更新して通すことは禁止。
- `tools/tests/` 直接一致 331 ファイル（各列挙行は同じ接頭辞）：
  - `about_info_tdd.md:43`、`access_walk_host.c:1`、`app_band_pde_host.c:10`、`app_bb_overlap_host.c:5`、`appmem_host.c:5`、`appmem_map_host.c:4`、`b8_hostdrv_host.c:34`、`b8_open_host.c:38`
  - `b8_tdd.md:936`、`boot_splash_native_host.c:35`、`bootinfo_tdd.md:5`、`c_dialect_tdd.md:17`、`cirrus_win_host.c:24`、`clock_caller_host.c:2`、`con_sink_tdd.md:4`、`db_errstr_tdd.md:6`
  - `dep_dir_a_l08_host.c:5`、`dep_dir_a_l09_host.c:5`、`dep_dir_a_l10_host.c:5`、`dep_dir_b_timer_host.c:8`、`dep_dir_c_memory_host.c:5`、`dep_dir_d3_disk_host.c:2`、`dep_dir_d_console_grid_host.c:4`、`dep_dir_d_console_host.c:2`
  - `dep_dir_d_display_host.c:2`、`dep_dir_d_emergency_host.c:3`、`dep_dir_d_splash_host.c:2`、`device_reservation_stage_host.c:4`、`display_cleanup_host.c:4`、`e10c_gcap_host.c:1`、`e82557_drv_host.c:98`、`exec_entry_host.c:2`
  - `exec_image_host.c:4`、`ext2_read_bound_host.c:26`、`fep_boundary_host.c:9`、`fep_standard_host.c:10`、`fixtures/dep_dir_c/manifest.json:113`、`fixtures/dep_dir_d/console_attr.json:9`、`fm_playback_host.c:2`、`font_source_host.c:14`
  - `fs_kind_callers_tdd.md:230`、`fstat_redir_host.c:325`、`fstat_redir_tdd.md:33`、`gf1_dispatch_host.c:3`、`gf1_foundation_host.c:3`、`gf1_instmem_map_host.c:5`、`gf1_irq_host.c:3`、`gf4_display_fake.h:24`
  - `gf5_fake.h:27`、`gf5_gpio_diag_host.c:5`、`gf5_observe_host.c:6`、`gf_accel_unbound_stubs.c:7`、`gf_cui_shell_host.c:3`、`gf_guard_fake.h:12`、`gfx_boot_host.c:53`、`gfx_kernel_fb_host.c:6`
  - `h3_tdd.md:241`、`h4_backing_host.c:5`、`highram_stage_host.c:1`、`host_mailbox_host.c:4`、`host_notice_host.c:6`、`hostsvc_block_host.c:5`、`hostsvc_boot_host.c:25`、`hostsvc_e82557_host.c:4`
  - `hostsvc_fd_host.c:3`、`hostsvc_lgy98_attach_host.c:11`、`hostsvc_lgy98_host.c:5`、`hostsvc_link_api_host.c:3`、`hostsvc_link_core_host.c:3`、`hostsvc_netcd_host.c:6`、`hostsvc_nic_host.c:8`、`hostsvc_nic_rx_host.c:6`
  - `hostsvc_open_host.c:6`、`hostsvc_persistent_host.c:6`、`hostsvc_serialfs_host.c:6`、`hostsvc_sfs_deadline_host.c:4`、`hostsvc_sfs_lan_host.c:4`、`hsync_fep_protect_host.c:5`、`hsync_sqlite_product_host.c:5`、`hwb_fake.h:7`
  - `hwb_shell_host.c:13`、`hwb_wire_host.c:3`、`hwdev_contract_fixture.py:19`、`hwdev_fixture.c:7`、`k5b_kernel_tdd.md:462`、`kapi_audit_host.c:3`、`kapi_bounds_host.c:4`、`kbd_dlog_host.c:26`
  - `kbd_fgpoll_host.c:3`、`kcg_boot_host.c:7`、`kdebug_loader_host.c:7`、`kpeek_cli_host.c:21`、`kpeek_host.c:18`、`kstring_c_host.c:26`、`lanfs_host.c:6`、`launch_host.c:23`
  - `lease_host.c:1`、`legacy_callers_host.c:3`、`link_notify_host.c:7`、`lspci_cmd_host.c:20`、`memmap_boot_host.c:16`、`memmap_tdd.md:181`、`memory_boot_host.c:1`、`multiapp_impl_host.c:30`
  - `n1_tdd.md:186`、`nano_adapter_host.c:4`、`ne2000_dma_host.c:4`、`net_link_host.c:19`、`os32x_layout_host.c:4`、`owner_reclaim_host.c:22`、`paging_bounds_host.c:1`、`paging_device_map_host.c:5`
  - `pc98_route_host.c:2`、`pcm_cs4231_host.c:24`、`pegc_mode_host.c:30`、`pgalloc_range_host.c:2`、`physmem_tdd.md:52`、`r1_fixture_host.c:3`、`r1_recovery_host.c:6`、`raw_wait_sql_host.c:9`
  - `recover_hostshim/io.h:3`、`result_conv_tdd.md:23`、`ring3_guard_host.c:42`、`ring3_str_host.c:25`、`s0_tdd.md:390`、`s2_tdd.md:191`、`s5_tdd.md:202`、`serialfs_host.c:34`
  - `shell_service_host.c:37`、`shlib_high_host.c:10`、`shm_user_host.c:2`、`splash_obs_host.c:3`、`splash_probe_host.c:3`、`splash_raster_host.c:2`、`splash_v2_host.c:2`、`splash_wait_host.c:2`
  - `sqlite_engine_host.c:3`、`sqlite_memsys5_linux.c:7`、`sqlite_module_host.c:11`、`stat_cmd_host.c:205`、`t8_tdd.md:130`、`test_abi_boundary.py:4`、`test_allocator_bench.py:4`、`test_appmem.py:23`
  - `test_appmem_map.py:29`、`test_b8_hostdrv.py:31`、`test_b8_open.py:52`、`test_boot_named_constants.py:4`、`test_boot_readers.py:11`、`test_bootinfo.py:7`、`test_c_dialect.py:238`、`test_cd_module.py:8`
  - `test_cd_read.py:27`、`test_ci_stab.py:3`、`test_cirrus_module.py:5`、`test_db_errstr.py:32`、`test_debug_probe.py:5`、`test_dep_dir_a_l01.py:14`、`test_dep_dir_a_l02.py:6`、`test_dep_dir_a_l04.py:6`
  - `test_dep_dir_a_l05a.py:6`、`test_dep_dir_a_l05b.py:6`、`test_dep_dir_a_l06.py:3`、`test_dep_dir_a_l08.py:3`、`test_dep_dir_a_l09.py:3`、`test_dep_dir_a_l10.py:3`、`test_dep_dir_d_console.py:3`、`test_dep_dir_d_display.py:3`
  - `test_dep_dir_d_splash.py:3`、`test_double_fault.py:65`、`test_e82557_drv.py:4`、`test_e82557_eeprom.py:6`、`test_e9_observation.py:133`、`test_exec_context.py:3`、`test_exec_entry.py:5`、`test_exec_r1.py:46`
  - `test_exec_syscall_fgpoll.py:26`、`test_fep_core_boot.py:4`、`test_fep_meta.py:14`、`test_fep_store_info.py:10`、`test_fep_store_search.py:20`、`test_fm_module.py:5`、`test_fstat_redir.py:115`、`test_geforce.py:3`
  - `test_gf1_dispatch.py:10`、`test_gf1_engine_stop.py:4`、`test_gf1_foundation.py:11`、`test_gf1_irq.py:6`、`test_gf4_probes.py:10`、`test_gf5.py:89`、`test_gf5_boot_target_diag.py:7`、`test_gf5_critical_service.py:10`
  - `test_gf5_display_lifetime.py:10`、`test_gf5_fancheck.py:15`、`test_gf5_gpio_diag.py:7`、`test_gf5_observe.py:7`、`test_gf5_rom_guard.py:15`、`test_gf5_service_live.py:10`、`test_gf5_therm_t1.py:15`、`test_gf_accel.py:13`
  - `test_gf_command.py:16`、`test_gf_cui.py:8`、`test_gf_display.py:3`、`test_gf_guard_imports.py:5`、`test_gf_objwait.py:2`、`test_gfx_accel_hal.py:16`、`test_gfx_cmd.py:15`、`test_gfx_draw_direct.py:15`
  - `test_gfx_movecopy.py:12`、`test_gfx_transfer.py:31`、`test_guest_acceptance.py:267`、`test_gui_button_dispatch.py:18`、`test_gui_desc.py:7`、`test_h3_park_resume.py:6`、`test_h5_shapes.py:5`、`test_hdd_stage1.py:979`
  - `test_hdd_stage2.py:83`、`test_header_boundary.py:4`、`test_host_mailbox.py:19`、`test_host_notice.py:21`、`test_hostsvc_block.py:7`、`test_hostsvc_block_contract.py:5`、`test_hostsvc_boot.py:31`、`test_hostsvc_e82557.py:6`
  - `test_hostsvc_image.py:5`、`test_hostsvc_init.py:5`、`test_hostsvc_lgy98.py:6`、`test_hostsvc_lgy98_attach.py:9`、`test_hostsvc_link_api.py:5`、`test_hostsvc_link_core.py:6`、`test_hostsvc_netcd.py:10`、`test_hostsvc_nic.py:8`
  - `test_hostsvc_nic_rx.py:7`、`test_hostsvc_open.py:18`、`test_hostsvc_persistent.py:18`、`test_hostsvc_sdk.py:6`、`test_hostsvc_serialfs.py:18`、`test_hostsvc_sfs_deadline.py:6`、`test_hostsvc_sfs_lan.py:8`、`test_hsync_fep_protect.py:6`
  - `test_hsync_h3.py:156`、`test_hwb_access.py:6`、`test_hwb_exec.py:5`、`test_hwb_parse.py:4`、`test_hwb_scripts.py:7`、`test_hwb_shell.py:4`、`test_hwb_wire.py:8`、`test_hwdev_image.py:3`
  - `test_hwdev_module.py:18`、`test_kcg_module.py:3`、`test_kdebug_init.py:5`、`test_kdebug_probe.py:7`、`test_kernel_shell.py:7`、`test_kpeek.py:21`、`test_kpeek_cli.py:13`、`test_kprintf_attr.py:19`
  - `test_kprintf_va.py:4`、`test_kstop.py:15`、`test_kstring_c.py:54`、`test_lan_module.py:7`、`test_lanfs.py:37`、`test_ledger.py:59`、`test_link_notify.py:19`、`test_mem_stat.py:8`
  - `test_memmap_gen.py:87`、`test_module_image.py:5`、`test_module_runtime.py:5`、`test_mtrr_wc.py:5`、`test_ne2000_dma.py:3`、`test_np21w_symbols.py:6`、`test_owner_reclaim.py:62`、`test_pci_bind.py:19`
  - `test_pci_hwb.py:6`、`test_pcm_module.py:5`、`test_pgalloc_model.py:32`、`test_physmem.py:3`、`test_platform_memory.py:11`、`test_public_surface.py:37`、`test_raw_wait.py:8`、`test_recover.py:6`
  - `test_recover_gf.py:3`、`test_serial_vfast.py:49`、`test_serialfs.py:39`、`test_splash_probe.py:11`、`test_splash_raster.py:6`、`test_splash_raster_diag.py:6`、`test_splash_v2.py:8`、`test_splash_wire.py:12`
  - `test_sqlite_module_proto.py:4`、`test_surface_lease.py:3`、`test_tick.py:3`、`test_time_math.py:20`、`test_tri_burst.py:10`、`test_tri_burst6.py:8`、`test_tri_w32.py:8`、`test_trident_contracts.py:13`
  - `test_trident_lifecycle.py:4`、`test_trident_mode.py:12`、`test_trident_pc98init.py:16`、`test_trident_present.py:9`、`test_trident_regs.py:12`、`test_trident_resources.py:26`、`test_trident_restore.py:18`、`test_trident_route.py:14`
  - `test_trident_save.py:18`、`test_trident_vram.py:12`、`test_trim_flow.py:9`、`test_trim_kernel.py:7`、`test_unicode_lookup.py:8`、`test_unicode_surface.py:112`、`test_v86_gcap.py:26`、`test_v86_module.py:6`
  - `test_ver_collect.py:8`、`test_ver_layout.py:3`、`test_ver_shell.py:9`、`test_ver_version.py:3`、`test_vfs_excl.py:68`、`tick_host.c:3`、`trident_emergency_host.c:4`、`trim_flow_host.c:8`
  - `unicode_lookup_host.c:11`、`v86_call_host.c:4`、`v86_gcap_abort_host.c:6`、`v86_low_host.c:5`、`ver_collect_host.c:11`、`ver_test_utils.py:31`、`vfs_excl_tdd.md:23`、`vfs_fd_sqlite_host.c:6`
  - `vk32_crc_tdd.md:36`、`x11_shared_host.c:4`、`x87_lifecycle_host.c:3`
- `tools/checks/` 直接一致 28 ファイル（各列挙行は同じ接頭辞）：
  - `d2-services-host.py:3`、`dep-dir-b-host.py:3`、`dep-dir-d3-host.py:3`、`double-fault-host.py:3`、`exec-image-host.py:3`、`fep-budget-host.py:3`、`fep-worst-load-host.py:15`、`gf-runner-host.py:3`
  - `gfinit-host.py:3`、`gshell-host.py:3`、`guest-acceptance-host.py:3`、`h2-fixtures-host.py:3`、`h3b-fixtures-host.py:3`、`hwdev-module-host.py:3`、`kstring-c-host.py:3`、`memory-host.py:3`
  - `mkmod-host.py:3`、`np21w-gui-host.py:3`、`osri-view-host.py:18`、`privileged.py:3`、`recover-tables-host.py:3`、`trim-back-rs-host.py:3`、`ver-api-host.py:3`、`ver-format-host.py:3`
  - `ver-info-host.py:3`、`ver-services-host.py:3`、`ver-shell-host.py:3`、`ver-state-name-host.py:3`
- coversのglob照合追加（直接一致とは別、関連するplatform分離後も検索）：`arch-asm.py`、`c-dialect.py`、`check-select-host.py`、`font-guard-host.py`、`hwdev-audit-host.py`、`kapi-layout-host.py`、`kapi-ranges-host.py`、`le-access.py`、`memmap-audit-host.py`、`p2v.py`、`r1-fixture-host.py`、`run.py`、`shm-user-host.py`、`tvram-auth-host.py`、`vk32-crc-host.py`。
- fixture importers（直接列挙と重複あり）：`hwdev_contract_fixture.py`、`test_ci_stab.py`、`test_dep_dir_a_l01.py`、`test_dep_dir_a_l02.py`、`test_dep_dir_a_l05a.py`、`test_dep_dir_a_l06.py`、`test_dep_dir_a_l08.py`、`test_dep_dir_a_l09.py`、`test_dep_dir_a_l10.py`、`test_gf_guard_imports.py`、`test_hwdev_image.py`。

- 型名/移設先追加検索（GuiEvtWidget/GuiEvent/PCI_DEV_STRUCT_SIZE/struct pci_dev/gf5/bootinfo_wire/native）で上の直接一覧に無いもの：`dep_dir_a_l02_host.c`、`e82557_eeprom_host.c`、`fm_module_host.c`、`hwdev_module_host.c`、`kcg_module_host.c`、`lan_module_host.c`、`pci_bind_host.c`、`pcm_module_host.c`、`test_dep_dir_d3_disk.py`、`test_device_reservation.py`、`test_vk32_crc.py`、`trident_module_host.c`、`trim_fixtures_host.c`。

## 8. 未決・完了条件
- ユーザー判断が必要な未決：0。PPC配置の実compiler確認はPM側の受入課題。packed/strict-alignは外部アプリ再開時の命令列実測後に判断する。本段のx86不変が崩れた場合は原因を直して再検証し、延期Cへ暗黙に拡張しない。
- 完了：x86生成物/公開指紋不変、x86 CHECKとPM専用のtarget別型/record/HWDEV probe・否定試験の証拠、host依存追従、selected基準を維持。PPC起動・module実装・別targetとのKAPI byte列互換は達成項目に含めない。

反論：0件（指摘1〜11を受け入れ、PM決定3件を反映）。

## 確定 (2026-11-09、Opus 5.5 第 2 回の可を PM が反映)
- r2-1 (P3)：H5 の変異を「probe の recipe から -fsyntax-only を外して build/out に .o を出す → build_compare が DIFF」に置き換えた (probe が .o を作らないので「link 集合へ混入」は作れない)。
- r2-2 (P3)：probe の前提は order-only (`kernel.elf: | abi-contract-probe`) で書くと §3 に足した。
- PM 決定 (REVIEW.r1 末尾)：probe は -fsyntax-only で .o を作らない。PPC に依存する検査は tools/ppc/ の手元・PM の検査で CI・CHECK・DEFERRED に入れない。PPC の profile の -mstrict-align は PPC の命令列を実測した後に決める。
- 第 1 回の P1 3 件・P2 4 件・P3 4 件はすべて r2 で直り、第 2 回で確認された。ユーザー判断の未決 0 件。
