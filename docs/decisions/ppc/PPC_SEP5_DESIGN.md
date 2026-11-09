# PPC_SEP5_DESIGN — 分離の第 5 段 (描画・残る asm・SDK)

> 状態: **現行 (2026-11-09)** — 共通のコードが持つ PC-98 の画面・x86 の命令・SDK の i386 前提を、表示の backend・arch・SDK の target profile の後ろへ移す。x86 の ABI・生成物・表示と FEP の指紋は変えない (対象 V2〜V4・A3・A5・A7・K1・K2)。段の状態は票 TASK_PPC_SEP が持つ。
> 出所: ~/os32-tmp/run/ppc-sep5/DESIGN.final.md を 2026-11-09 に写した (gpt-6-astra 起草 r1〜r2、Opus 5.5 第 1・2 回、PM が第 2 回の P3 と PM 決定を反映)。
## r1→r2 の変更
- F1: 全部品contractsの字面不変を列挙。ime_render.hの編集を除きH9変異先を修正。
- F2: ime_render.incと保存領域確保を追加。PC98 backendへ保存復元を移しC2→C1に変更。
- F3: FEPのsource規則とbasename保持を明示、C2をC区分に変更。
- F4: PM決定に従いboot移設を撤回し、既存targetの拒否試験のみに縮小（食い違い3）。
- F5: 既存display_restore.hとplatform/pc98/display.cを延長し置き場を統一。
- F6: 段ごとのbuild_compare.pyのobject対応とSDK tool sha256 pin更新を追加。
- F7: 移設と試験/coversを段で対応づけ、x11/sqlite-engine/hsync-fepの漏れを補完。
- F8: H1/H5のhost32経路、libc不要の起動コードとCPU原始操作の差替えを明記。
- F9: Rust config.tomlの既定targetと綴りを保持、profileは検証のみ、H8に.a比較追加。
- F10: 公開os32_callconv.h撤回のPM決定と第3段Fの正典反映、将来PPC SDKのABI手順を明記。
- F11: MESSAGESにsdkを追加。
- F12: PEGCコピー呼出元とuserland複製を追記し、後段の範囲を明示。
- F13: display_scrollは内部専用、公開gfx_hardware_scrollの相対行契約を保持。
## 0. 範囲・判定
- 基点 `5b780d18e74c26eafc6702062f2e267299554fb2`。調査の8行 V2/V3/V4/A3/A5/A7/K1/K2、TASK_PPC_SEP.md:70,75–79 に対応。第3段A/D0/E1/B/C1/C2済み、D1実装中という依頼時点で設計する。
- 第一目標はx86 ABI・表示不変。案D、整数カーネル、協調切替、NP21/Wのみのx86受入、外部未ビルド（H-7）を固定。PPCのFB/CRT/Rust実装、mm表変更、新しい描画コマンドは含めない。
- 到達点は①selected TUが組める ②初期化して動く ③ユーザ実行を分離。本段は①の障害を減らし、②③の契約を定義する。PPC全リンク・画面・ユーザ実行の達成を宣言しない。
- 今回は読取と本票作成のみ。PACKの書込制限を優先しpreflight --fix-fonts、build、host、変異、lint、NP21/W、selected実測は全て未実施。数値は設計概算。独立レビューはPMの次工程。
## 1. 実物の照合（行は基点）
|ID|実物 file:line|判定・移設先|
|---|---|---|
|V2|gfx/gfx_core.c:256–279（TVRAM消去/FF2）、:388–404（両plane消去）、gfx/gfx_scroll.c:5–64、gfx/palette.c:36–41|指摘存続。platform/pc98/{gfx_init.c,gfx_scroll.c,display.c}へ。palette shadowは共通に残す|
|V2補足|gfx/gfx_core_init.inc:4,29–48、gfx/gfx_vram.c:221–222,245、gfx/gfx_core_surface.inc:119–125、gfx/gfx.h:11、gfx/gfx_core.c:56,89|初期化inc、flip/vsync、TVRAM surface、既定backend/候補もPC98依存。代表3ファイルだけの移設では完了しない|
|V3|kernel/ime/ime_render_tvram.c:20–75、kernel/ime/ime_render.inc:7,19,61–86,187–213（TVRAM依存23箇所）、ime.c:94–97（保存領域確保）、kernel/console/console.c:75–89,174,245–274,381–383,450–451、include/tvram.h:9–13|指摘存続。低位操作は既にplatform/pc98/console.c:21–125、属性はconsole_attr.c:61。既存con_backendを拡張し全consoleを移さない|
|A3|gfx/gfx_internal.h:70–80|rep movsw/movslが残る。arch/x86/arch_copy.hへ同じinlineを移す。gfx_vramは間接利用|
|A5|drivers/video/geforce/gf_wc.c:5,30–90,93–125,169–185、gf_accept.c:64–145|CPUID/RDMSR/flags、x86_wc_lease、PAT/alias検証あり。MTRR書込は既にarch/x86/kernel/mtrr_wc.c。GPU判定だけ共通に残す|
|A7|lib/sqlite3/sqlite3.c:76622,76896,97488,100672|TRACE中のESP asm 4点が存続。診断用inline providerへ。SQLite全面移設なし|
|V4|boot/boot_hdd.asm:7,102、boot/boot_fat.asm:7、boot/loader_hdd.asm:451、boot/boot_main.c:71–120、build/targets/x86-pc98.mk:2|PC98 BIOS/x86専用。boot.mk選択は既設。build/targets/ppc-powermac.mkのboot/sdk/programs拒否を試験（食い違い3）|
|K1|sdk/crt/crt0.asm:5,33–37、sdk/link/app.ld:4,11–15、build/config.mk:207,217–218|CRT/配置の指摘存続。ただしelf_i386固定は既にbuild/arch/x86.mk:13のELF_EMULATIONへ移動（食い違い1）|
|K2|sdk/rust/i686-os32-none.json:2–11、sdk/rust/os32api/src/ls.rs:25–26|指摘存続。extern C callbackはtarget ABIに従う。JSONだけPPC化しても動かない|
- 食い違いはSURVEYのelf_emulation、確定設計の公開callconv（PM撤回済み）、boot選択の既実装の計3件。行番号ずれ、V2補足、既存HALは数えない。
- HAL確認：gfx/gfx_hw_submit.c:60–70はbackend mono/CPU fallbackへ、gfx/gfx_policy.c:51はmono有効化、gfx/gfx_hw_mono_cpu.c:7は共通fallback。userland/lib/gfx/draw/gfx_cmd.cを通る投入とdraw/geom、userland/rust/libos32guiは既存HALを利用し本票で再設計しない。V2の機種制御とA3のコピーはなお残る。
- 第3段の属性口は既にinclude/callconv.h（arch/README.md末尾）、公開shared.h:32–33は旧cdeclを保持（食い違い2。PM決定で公開os32_callconv.h新設は取り下げ。PPC_SEP3_DESIGN.md A2との差は第3段Fで正典へ反映）。初回4引数はarch/x86/kernel/exec_entry.c:23–28。CRTのargc/argv/api/auxとUSER20B/CPL0 16Bをそのまま使う。
- 第2段PPC_SEP2_DESIGN.md:119–130,233のbootinfo native/wire分離、boot_common_setとplatform captureは変更しない。ローダ選択はruntime boot/platform口とは別。PPC入口はplatform/powermac/boot.cを継続する。
## 2. 契約と所有
### 2.1 描画・console・IME
```c
/* 既存 kernel/console/display_restore.h へ内部宣言を追加。GfxBackendは不変 */
void display_text_hide(void); /* PC98: char=0/attr=0。console_clearとは別 */
void display_palette4_write(int index, u8 r, u8 g, u8 b);
void display_wait_vsync(void);
void display_scroll(int y, int height, int flip, int page);
void display_flip_pages(int display, int access);
/* kernel/console/console_backend.hへ追加。情報はbackend所有、読取りだけ */
struct con_geometry { int cols, rows; };
const struct con_geometry *con_backend_geometry(void);
u8 con_backend_color(unsigned role); /* default-white等。既存raw属性ABIは保持 */
/* include/ime_control.h（内部の狭いfacade、IME_Stateを露出しない） */
void ime_set_render(void *table);
```
- display_*はbuildで選ぶ静的provider。PC98実体はplatform/pc98/。既存display.cへpalette/text/vsync/flipを追加、scroll/init/selector/surfacesも同ディレクトリへ。既存display_text_start/restore_98/recover_*とkdebug kexportsの宣言・挙動・輸入契約は保持する。PPCには未対応provider（描画未実装）を置き、selected用に無操作口を供給しても起動profileは拒否を維持する。FBへGDCのポートやPRAMを渡さない。新ハード駆動は後段。
- `display_scroll`は内部専用。公開KAPI `gfx_hardware_scroll(int lines)`（sdk/kapi.json:693、tilemap利用）の相対行という意味は不変。`display_scroll`の入力は論理行/寸法/ページ。正規化したvram_scroll_yは共通gfx/gfx_scroll.cが所有、PC98側だけSL倍率・GFX_WPL・PRAM・vsync順を知る。bb無しは旧通り状態も触らない。非scroll backendは能力なしとして呼ばずCPU presentを使う（PPC実装は後段）。
- palette.cの16色shadow、4bit mask、初期値、shadow_set/getはKAPI互換の共通状態。writeだけPC98へ。PEGCの8bit paletteとshadow_setを二重変換しない。新FBはこのlegacy16色とnative pixel色を区別して実装できる口。
- gfx_coreのdirty/bb/世代/revokeは共通所有。_gfx_common_initのTVRAM消去は4plane存在確認後、bb消去の前、FF2は後という順を維持。PC98専用init・clear pages・fallback選択は新gfx_init.c/selector.cへ。PC98 fallbackのNULL init慣行も保持。
- `gfx_core_surface.inc`の固定面descriptor/機種候補をplatform/pc98/gfx_surfaces.cへ。共通のlease検証・世代管理は残し、providerからconst descriptor群と既定backend/候補を取得する。TVRAM面のrevokeを省略しない。PC98のboot reserve順・BB確保・identifyとprobeの区別を保存する。
- `gfx_vram.c`のページI/Oとtext-vsync、splash診断をPC98 providerへ。CPUコピー/present/dirtyは共通に残す。gfx.h/gfx_internal.hのpc98.h依存を除き、PC98定数は各実体が直接includeする。legacy640×200/400・planar4の形式定数と実装ポートを混同しない。
- consoleはcursor_x/y、g_scroll_reserve、sink/V86/GUI gateを所有。geometryは固定x86 80×25、別providerで任意の正の寸法を表す。console_get_sizeとwrap/clamp/scrollが同じgeometryを使う。TVRAM_ROWS/ATTR_WHITEの暗黙includeを除去する。
- tvram_*は既存KAPI/import利用者用の互換入口を残す。include/tvram.hは宣言だけにしTVRAM_BASE/ATTRはPC98実装へ。生のJIS/readchar/raw属性契約はlegacy adapterに残し、共通UnicodeからFBへはcell drawの別実装が可能。既存利用者のコード/属性値は変えない。
- IME_Renderの字面・形・g_ime_render_tvramシンボルは維持。ime_render_tvram.cは同basenameのplatform/pc98/ime_render_tvram.cへ移し、FEP内のprivate backendとしてリンク。共通IMEは非契約ヘッダkernel/ime/ime_display_private.hの寸法/末行・保存復元・資源操作を呼ぶ。IME_Renderやkexports/importsへ追加しない。
- private口はime_display_cols/preedit_row、ime_display_save/restore(top,rows)、ime_display_boot(fault)/bytes/abort。PC98 backendがbufferとTVRAM read/writeを所有、既存3行×80列の確保量・失敗注入・部分確保・解放順・bytes計上を保持。ime.c:86–98,120–123は委譲、ime_render.incから番地/BPR/COLS/固定24を除く。候補表示の論理状態と非TVRAM rendererのclear_row経路は共通に残し、TVRAM renderer時だけ保存復元する。
- PC98のgeometryは従来の80列/末行24（GUI rendererも同じ論理寸法）、hostでは別寸法を注入。PPCは未対応providerで製品起動を拒否。defaultはplatform/{pc98,powermac}/ime_default_render.hで選ぶ。C2はC区分、通常/R1のFEPを組んでからC1でtvram.hのpc98.hを外す。
- kernel/gui.cはime_control.hだけをinclude（ime_set_render(NULL)の所有者検証・復旧順不変）。init/kernel.cのIME boot呼出は別の狭いime_boot.h宣言にし、重いime.h/ime_store.h連鎖を外す。public SDKヘッダには触らない。
### 2.2 コピー・WC・診断
```c
/* include/copy_units.h -> 固定名arch_copy.h。既存名はgfx内inline alias */
static inline void arch_copy16(void *dst, const void *src, unsigned units);
static inline void arch_copy32(void *dst, const void *src, unsigned units);
/* include/video_wc.h: 実体はarch。単一GPUのleaseはarchが所有 */
int arch_video_wc_active(void);
int arch_video_wc_eligible(u32 fb, u32 mapped, u32 extent, u32 regs, u32 reg_bytes);
int arch_video_wc_start(u32 base, u32 bytes); /* 0=無し、正=既存結果、負=失敗 */
int arch_video_wc_stop(void);               /* identity喪失後もdrain/release */
/* 受入ログ生成はarch所有。reportは既存module内関数への借用callback */
int arch_video_cache_inspect(u32 phase, u32 base, u32 bytes,
                             void (*report)(const char *));
/* include/diag_stack.h -> arch_diag_stack.h: inline unsigned long arch_diag_sp(void) */
```
- copyは非重複、単位数（byte数でない）、ゼロ可、callerが有効範囲を保証。x86 providerは現asm/constraints/DF=0前提をそのままinline、PPCはvolatile u8ポインタによるCコピーでLE/BEいずれもbyte保存、アクセスを維持しMMIO幅最適化は後段。memmoveやDMA同期へ契約を拡大しない。
- `gf_wc.c`に残すのはBAR/ledger/surface/identity/RAM重複判定。mapped_uc/no_alias/bar0_uc/PATとlease/resultはarch/x86/kernel/video_wc.cへ、既存mtrr_wc.cに委譲。PPC providerはactive=0/eligible=0/start=0/stop=0、MTRRを真似ない。
- startはactiveなら旧結果を返す順を維持（再identifyより先）。非active時にGPU判定→arch eligible→acquire。stopはGPUの再識別を要求しない。単一GPUという現仕様で静的状態1組、汎用allocator/ロックは足さない。
- `gf_accept.c:95–145`のinspect全体とcpu/msr record生成をarch/x86/video/gf_accept_cpu.cへ移す。GeForce moduleと一緒にsource選択しkernelの新importにはしない。既存report callbackを借用、CPU recordの整形bufferはarch側の呼出中だけ所有し、順序/phase/ログ文字列を保持。PPC版は0（判定不可）を返しUC/WC測定成功に見せない。
- CPUID可否検査→vendor→MSR能力→RDMSR、flags復元はx86内部。共通にCPUID leafやMSR番号を公開しない。mmのソフト2段表をCPUキャッシュ属性と同一視せず、x86のPAT解釈/alias walkをarchへ閉じる。
- SQLiteはTRACE時だけdiag_stack.hをincludeして4箇所をarch_diag_sp()へ（unsigned longは現u32と同幅）、x86はalways_inlineの同命令。PPCは0（診断未提供）で実装可能、ログのesp表記はx86互換として保持。TRACE=offは前処理後byte不変を目標、onの診断値/生成物は別比較。GNU89を維持しSQLite本体にCPU ifdefを足さない。
### 2.3 boot・SDK target profile
```python
# sdk/target_profile.py の既存PROFILESを拡張（表の概形）
'x86-pc98': dict(sdk=True, call_abi='i386-cdecl', crt='crt/crt0.o',
                app_link='link/app.ld', rust='rust/i686-os32-none.json')
'ppc-powermac': dict(sdk=False, call_abi=None, crt=None, app_link=None, rust=None)
MESSAGES['sdk'] = 'sdk'
# require(target, 'sdk') はinput読取・出力削除・tool起動より前に拒否
```
- 既存elf_identity/module_codec/layout/link_guardは残す。make用の同じ値をprofile CLIから出力し、build/sdk-target.mkが読む。CRT_USER/RESIDENT・app/app_sys/shlib link・Rust target・ELF emulation・call ABIをひと組で検証する。未対応PPCの任意パス上書きで拒否を迂回させない。
- build/config.mk:207,217–218、build/sdk.mk:19–50、sdk/example/hello/Makefile:36,41とRustビルド入口をこの選択へ接続。Rustはuserland/{rust,gshell}/.cargo/config.tomlの既定target・相対pathの綴りとcargo引数を保持し、profileとsdk/rustc_stamp.pyは選択targetをtool起動前に検証するだけにする。単独SDKには同じprofile入口を配布する。既定x86の引数順、crt順、staging内の既存パスとCRT/ライブラリ内容を保持し、ツールとprofileメタデータのみ変更可。CRT asm/C、link scripts、Rust JSON/ls.rs、生成publicヘッダは編集しない。
- `sdk/gen_kapi.py`の第3段target/ABI選択を再実装しない。kapi_abi_x86.py/内部callconvと公開shared.hの既存cdecl、およびprofileの整合をhostで検証。確定設計との差を解消するために公開ヘッダを後追い編集しない。PPCでextern Cが使えることをPPC OS32 syscall/CRT対応済みと解釈しない。
- boot/とbuild/boot.mkは現状維持。includeは既にbuild/targets/x86-pc98.mkのみ。Fはbuild/targets/ppc-powermac.mkがboot/sdk/programsをunsupportedでtool起動前に拒否する試験だけを追加。boot規則・既存字面試験を変えない。
- SDK公開ヘッダと下記全contractsはコメントも含め字面不変（sqlite_imports.defはcontractsなし）。tools/gen_sqlite_imports.pyの本文連結hashを守り、import/exportsも増やさない。PPC SDKで公開呼出属性を変える段は[ABI]として版/世代・指紋の正規再生成・clean→all・SDK/CRT/module再ビルドと受入をセットにした別段。本段のx86公開ヘッダは不変。
- 字面不変 cd: `drivers/ata/cd_module.h`, `drivers/base/dev.h`, `fs/vfs.h`。
- 字面不変 cirrus: `drivers/video/cirrus/cirrus_module.h`, `include/gfx_hal.h`, `include/gfx_mode.h`, `drivers/video/cirrus/wab_glue.h`。
- 字面不変 fep: `kernel/ime/fep_module.h`, `kernel/ime/ime_render.h`, `sdk/include/os32/os32_kapi_shared.h`。
- 字面不変 fm: `include/fm_contract.h`, `drivers/sound/fm/fm_module.h`, `drivers/sound/fm/fm.h`, `drivers/sound/fm/snd_engine.h`。
- 字面不変 geforce: `drivers/video/geforce/geforce_module.h`, `include/gfx_hal.h`, `include/video_service.h`, `hwdev/gf/production.h`, `hwdev/gf/foundation.h`, `hwdev/gf/internal.h`, `drivers/video/geforce/geforce_tick.h`, `hwdev/gf/gf1_test.h`, `kdebug/gfinit_bus.h`, `include/debug_probe.h`。
- 字面不変 kcg: `drivers/video/kcg/kcg_module.h`。
- 字面不変 lan: `include/lan_contract.h`, `include/pci_dev_contract.h`, `include/pci_bind_contract.h`, `drivers/net/e82557/lan_module.h`。
- 字面不変 pcm: `include/pcm_contract.h`, `include/dma_contract.h`, `drivers/sound/cs4231/pcm_module.h`, `drivers/sound/cs4231/pcm_cs4231.h`, `kernel/irq/irq.h`, `mm/dma_pool.h`。
- 字面不変 trident: `drivers/video/trident/trident_module.h`, `drivers/video/trident/trident.h`, `include/gfx_hal.h`, `include/gfx_mode.h`, `gfx/palette.h`, `include/debug_probe.h`。
- 字面不変 v86: `arch/x86/legacy/v86_module.h`, `kernel/module/module.h`。
## 3. BSDとの照合・未確認
- 以下N/Fは `/home/hight/os32-tmp/ref/netbsd` / `freebsd`。GPLは読んでいない。新ハード実装はないため新規vendor仕様の採用はない。PC98の既存I/O列を移設し増やさない。
- N/sys/arch/powerpc/oea/ofw_rascons.c:255–258,274–283はwidth/height/depth/stride→rasops、:124はtextops境界。N/sys/arch/macppc/dev/ofb.c:202,264–290,438–452はFB寸法/stride/paletteをdevice側で扱う。TVRAM番地を共通consoleへ渡さない根拠。
- F/stand/powerpc/ofw/start.c:39–80は専用_start・r1・BSS・C入口、Makefile:18,45–49は専用source/linkscript選択、powerpc.ldscript:1はPPC ELF形式。これはboot起動処理でありユーザCRTの互換性の証拠ではない。
- ローカル参照木にlib/csuのPPCユーザcrt0/crt1は見つからず、ユーザCRTのBSD file:lineは未確認。PPCユーザCRT実装前の必要調査とする（本票では実装しない）。Rust公式target仕様も手元で未発見、外部取得なし。PPC JSONは作らず、既存x86 JSONのbyte保存のみを本段受入にする。
## 4. 実装段とファイル別見積もり
- 各行1担当・半日〜1日。I=不変目標、C=機械語変化を予定（表示契約は不変）。Iも不一致なら理由を記録しCとしてNP受入するまで次へ積まない。共有ファイルの編集は直列。新規は+、移設は移、数字は概算行数。B4/B5のprovider宣言はgfx既存backend/ledger型を借用し、ABI共有構造体を拡張しない。
- 全行KAPI slot/世代・SDK公開ヘッダ・CRT入口・FEP指紋は不変。SDKツール/stagingメタデータだけS段で変化。kdebug等のpaired checksum変化とABI変化を区別する。
|段/前提|主ファイル・補助（新規/変更規模）|区分・受入（§6）|
|---|---|---|
|A コピー/基点|gfx/gfx_internal.h 16、include/copy_units.h +12、arch/{x86,ppc}/arch_copy.h 各+25、tools/tests/host_arch/arch_copy.h +25|I、H1/比較。HALとgfx_internalで衝突。drivers/video/pc98/backend_pegc_present.inc:73の呼出もH1対象|
|B1 palette/A|gfx/palette.c 12、kernel/console/display_restore.h +25、platform/pc98/display.c +20、platform/powermac/display.c +25（未対応口）|C、H2/N1/N3。HAL palette試験と直列|
|B2 scroll/B1|gfx/gfx_scroll.c 55、platform/pc98/gfx_scroll.c 移55、kernel/console/display_restore.h 5|C、H2/N3。vram_scroll_yは移さない|
|B3 init/B2|gfx/gfx_core.c 70、gfx/gfx_core_init.inc 35、platform/pc98/gfx_init.c +110、同.h +15|C、H3/N1/N3。HAL coreと直列|
|B4 provider/B3|gfx/gfx_core.c 60、platform/pc98/gfx_selector.c +100、include/display_provider.h +35、platform/powermac/display_provider.c +25（未対応）|C、H3/N1/N3。default/list/probe/fallbackのみ|
|B5 surfaces/B4|gfx/gfx_core_surface.inc 160、platform/pc98/gfx_surfaces.c 移160、display_provider.h 10|C、H3/N1/N3。第4段r2は配置probeのみ、製品編集の競合なし|
|B6 present/B5|gfx/gfx_vram.c 55、platform/pc98/display.c +65、gfx/gfx.h 5、gfx_internal.h 5、残る直接pc98定数読者のinclude各1–3|C、H1/H2/H3/N3。HAL present/dirtyと直列|
|C2 IME/B1|kernel/ime/ime_render_tvram.c→platform/pc98/ime_render_tvram.c 移76＋保存復元/資源処理100、ime_render.inc 90、ime.c 35、ime_display_private.h +25、platform/{pc98,powermac}/ime_default_render.h 各+8、powermac/ime_display.c +25、include/ime_control.h +10、kernel/gui.c 1、build/fep-module.mk 15、tools/build_compare.py 5|C、H4/H9/N2、GUI known 3組。ime_render.hは0行|
|C1 console/C2|kernel/console/console.c 45、console_backend.h +12、platform/pc98/console.c 15、include/tvram.h 5、platform/pc98/tvram_private.h +10、PC98 の FEP 部品 2 本 (C2 で移した platform/pc98/ime_render_tvram.c と PC98 の ime_display 実装) は pc98.h を直に include 各 1|C、H4/N1/N2。第3段D2のlegacy gate試験後|
|C3 boot IME facade/C2|include/ime_boot.h +20、init/kernel.c 1、既存ime.hの宣言include整理5|I、H4/H9、init known 3組（§7）|
|D1 WC/独立|drivers/video/geforce/gf_wc.c 130、arch/x86/kernel/video_wc.c 移150、include/video_wc.h +25、arch/ppc/video_wc.c +20|C、H5/N4。mtrr_wc.cは既存実体を使用|
|D2 accept/D1|drivers/video/geforce/gf_accept.c 110、arch/x86/video/gf_accept_cpu.c 移130、arch/ppc/video/gf_accept_cpu.c +8、build/geforce-module.mk:89–96のGFA source/規則15|C、H5/N4。module ABI/exportsは不変|
|E SQLite/独立|lib/sqlite3/sqlite3.c 9、include/diag_stack.h +8、arch/{x86,ppc}/arch_diag_stack.h 各+12|I(off)/C(on)、H6。第4段r2はtypesを変更せず競合なし|
|F boot選択/独立|tools/tests/test_sep5_boot.py +120、tools/checks/sep5-boot-host.py +15。製品ファイル変更0|I、H7。既設のPPC boot/sdk/programs拒否、boot像不変|
|S1 profile/第3段F後|sdk/target_profile.py 40、build/sdk-target.mk +45、build/config.mk 15、sdk/target_profile_cli.py +45、tools/build_compare.py 10（tool pin）|I、H8。config/order登録は他段と直列|
|S2 SDK接続/S1|build/sdk.mk 15、sdk/example/hello/Makefile 15、sdk/rustc_stamp.py 15（target拒否）、build/programs.mk:163,171,610,626のRust呼出規則20、userland/{rust,gshell}/.cargo/config.toml 0（検証入力のみ）、SDK README/arch README各15、tools/build_compare.py 10（配布tool pin）|I、H8/N5。PPC SDK拒否確認、CRT/JSON本文変更なし。arch/README.mdは第4段Fと直列|
|Z 全段・第4段F後|tools/ppc/{selected_success,known_header_dependencies}.json 実測分、arch/README.md・TASK_PPC_SEP.md 各15、受入台本30|I、H9、全Nと容量/比較記録。第4段Fとarch/README.md・docs/tasks/ppc/TASK_PPC_SEP.mdが衝突（S2のarch READMEも同じ）|
- source結線は製品のsourceを変える各段（Fは除く）のbuild/{sources/order.mk,select.mk,arch/x86-sources.mk,platform/pc98.mk,platform/powermac.mk}に各3–20行、tools/tests/fixtures/build_profile_x86.json各3–15行。移設位置で元のリンク順を保持する。全部Zへ先送りしない。
- 新規hostはtools/tests/test_sep5_{copy,display,provider,console,wc,trace,boot,sdk,boundary}.py 各120–220行、必要なC harness各80–180行。各CHECK inputsと新tools/checks/sep5-{copy,display,provider,console,wc,trace,boot,sdk,boundary}-host.py登録各10–25行。既存試験の追従は§5各ファイル2–30行（抽出変更は30–100行）。
- 製品の新規/変更約1,600–2,400行＋移設約600行、試験新規約1,800–3,000行、既存追従約500–1,200行。原計画900–2,000行は試験とinc/選択漏れを含まないため幅を広げた。実装開始時に各段のdiffで更新する。
- 第3段D2/D3/E2/E3はexec/kapiの共有口を変えるためS1/S2は第3段F完了後。C1のlegacy_guest_fakeとgfx host抽出もD2と直列。第4段r2 §5はtypes/GF/GUIの製品編集を含まないためB5/D1/E/S1のその前提は外す。S2/Zの文書衝突は上表。GFX_ACCEL_HALのcore/internal/palette/vram・draw/geom・GUI試験は同時編集しない。
- Aのuserland複製userland/lib/gfx/libgfx_internal.h:6–10は後段（GFX_ACCEL_HALのH2a・L2–L6・G3、PEGC DIRECTの残件と同じ台帳）。本段で解消した扱いにしない。
- C2のbuild/fep-module.mkはFEP_OBJSのime_render_tvram.oの位置/出力名を保持し、kernel/ime/%.cのstatic pattern対象からそれだけ外しplatform/pc98/ime_render_tvram.cの明示規則を追加。同じFEP_INC/OS32_FEP_MODULEでprivate口を部品内解決、新kernel importは不要。
- 比較台帳: tools/build_compare.pyのH2_OBJECT_MOVESをB2/B5/D1/D2で点検（各3–10行）。元objectが残る抽出は移動表へ入れず旧objectの比較と新objectのC差分を記録し、丸ごとの移動だけ実際の旧新名を追加。C2はFEP出力名を保持するため追加不要、source/STT_FILEを確認。A/C3/E(off)/F/Zはobject移動なし、不一致ならI受入を止める。
- S1/S2はALLOWED_TOOL_DIFFSへ実配布のtarget_profile.py/CLI/rustc_stamp.py等の旧新sha256を段ごとにレビュー後pin（新規の旧値はNone）。未配布ファイルは登録せず、パスだけの例外や全SDK除外を作らない。基点/段直前の比較を両方記録。
## 5. host依存の全件台帳
- T=tools/tests/、C=tools/checks/。下表で移設元/先と担当段を結び、source/include/抽出/MIRRORとCHECK covers/inputsを同段で更新。契約ヘッダのcoversは削除しない。既存globで新パスを覆う場合は重複登録不要。
|段|移設/変更対象|T試験・C coversの追従（拡張子省略）|
|---|---|---|
|A|gfx_internal→copy_units/arch_copy、PEGC呼出|T test_gfx_copy/attach/kernel_fb/transfer・host_arch; C gfx-host・arch-asm・c-dialect|
|B1/B2|palette/scroll→platform/pc98/display.c・gfx_scroll.c、display_restore.h|T test_trident_palette/pegc_mode/gfx_boot/dep_dir_d_display/display_cleanup; C gfx-host・gf-runner-host・gui-host、新sep5-display-host|
|B3/B4|core/init→gfx_init/selector、display_provider|T test_gfx_boot/reinit/reattach/attach/kernel_fb・gfx_boot_host; C gfx-host・gui-host・gf-runner-host、新sep5-provider-host|
|B5/B6|core_surface/vram→gfx_surfaces/display、gfx.h/internal|T test_gfx_kernel_fb/attach/map_policy/transfer・test_gf_display・Trident present/lifecycle群; C gfx-host・gf-runner-host・gui-host|
|C2|ime_render_tvram→同basenameのplatform実体、ime_render.inc/ime.c/private口、gui|T test_ime_display/unicode_surface/fep_core_boot/fep_budget/fep_boundary/fep_module/fep_generator_meta/hsync_fep/x11_shared・fep_host_build・ime_display_host; C x11-shared-host・sqlite-engine-host・fep-budget-host・fep-standard-host・fep-worst-load-host・gui-host、新sep5-console-host|
|C1|console/backend、tvram.h→PC98 private定数|T test_dep_dir_d_console/con_sink/legacy_callers/ime_display・kernel_includes・console_grid.json; C con-sink-host・d2-services-host・gui-host・x11-shared-host|
|C3|init/kernel.c・ime.h→ime_boot.h|T test_fep_core_boot/fep_boundary/sqlite_engine/hsync_fep・kernel_includes; C sqlite-engine-host・x11-shared-host・gui-host|
|D1/D2|gf_wc/accept→arch video_wc/gf_accept_cpu|T test_geforce（wc_lifecycleのCPU範囲抽出/独自-I）・gf_wc_host/recovery・test_gf_production/gf_display; C gf-runner-host・gfinit-host、新sep5-wc-host|
|E|sqlite3.c→diag_stack/arch_diag_stack|T test_sqlite_memsys5/sqlite_product_sql/sqlite_module_proto; C sqlite-memsys5-host・sqlite-engine-host、新sep5-trace-host|
|F|boot資産/targetsは不変、拒否試験追加|T test_build_selection/ppc_selection/c_dialect:583/no_libgcc:22/hdd_stage1/2（MIRROR不変）; C hdd-stage1-host・hdd-stage2-host、新sep5-boot-host|
|S1/S2|profile/CLI/sdk-target/rustc_stamp/sdk・programs、比較pin|T test_build_profile/gen_kapi_cli/host_sdk_rs/kapi_layout/ls_client/h2_fixtures; C target-profile-host・target-toolchain-host・tools-host・h2-fixtures-host、新sep5-sdk-host|
|Z/全段|selected/known、source/order登録、比較台帳|T test_build_selection/ppc_selection/src_layout/check_result_cache; C check-select-host・tests-inventory・docs-orphans、新sep5-boundary-host|
- test_gfx_attachのHEADERS、test_gfx_kernel_fbの関数差替/gcc、test_dep_dir_d_consoleのFILES/GRID_FILES、test_ime_displayの抽出/gccは各担当段で更新。fakeだけでなく移設後の製品関数を組む。全変更/削除行のmutation anchorを検索し一意化してruntime REDを確認。以下はr1の広い検索候補を保持した補助台帳。直接追従は上表が主、無関係な試験を一括編集せず各段でcovers/inputs閉包を再確認する。
- T実物参照：`fep_host_build.py:16`、`kernel_includes.py:3,16,27`、`r1_recovery.py:143`、`test_app_band_pde.py:56`、`test_appmem_map.py:30`、`test_boot_named_constants.py:60,102,113`。 `test_boot_splash_native.py:3,34,37,46`、`test_bootinfo.py:7,15,28,118`、`test_bootlog.py:21,177`、`test_build_profile.py:16,17,20`、`test_build_selection.py:74`、`test_c_dialect.py:17,98,99,108`。 `test_cfg.py:12,185`、`test_check_result_cache.py:4`、`test_ci_stab.py:3`、`test_cirrus_module.py:5`、`test_cirrus_win.py:35`、`test_clang_ast.py:71,176,177`。 `test_con_sink.py:25,30,32`、`test_d2_services.py:215`、`test_db_errstr.py:32,397`、`test_debug_probe.py:8`、`test_dep_dir_a_l04.py:6`、`test_dep_dir_a_l05a.py:6`。 `test_dep_dir_a_l06.py:3`、`test_dep_dir_a_l08.py:3`、`test_dep_dir_a_l09.py:3`、`test_dep_dir_a_l10.py:3`、`test_dep_dir_d_console.py:3,130,133`、`test_dep_dir_d_display.py:3,28`。
- T実物参照：`test_dep_dir_d_splash.py:3,27`、`test_deploy_set.py:64,69,488`、`test_display_cleanup.py:6,20`、`test_edit_doc.py:25`、`test_exec_fgpoll.py:10`、`test_exec_hwb_abort.py:15`。 `test_exec_syscall_fgpoll.py:11`、`test_fep_boundary.py:111`、`test_fep_budget.py:45,56,184`、`test_fep_conn.py:7`、`test_fep_core_boot.py:4`、`test_fep_generator_meta.py:40`。 `test_fep_meta.py:14,50`、`test_fep_module.py:8`、`test_fep_standard.py:86`、`test_fep_store_contracts.py:13`、`test_fep_store_info.py:8`、`test_fep_store_search.py:32,63`。 `test_fep_user_store.py:9`、`test_font_source.py:44`、`test_geforce.py:3,236,247,249`、`test_geforce_fgpoll.py:5,7`、`test_geforce_heat_backend.py:10,12`、`test_gen_kapi_cli.py:22`。 `test_gf1_instmem_map.py:13`、`test_gf5.py:359,360`、`test_gf_accel.py:4,9,543,602`、`test_gf_command.py:3`、`test_gf_direct.py:8`、`test_gf_display.py:3,1593,1599`。
- T実物参照：`test_gf_objwait.py:2`、`test_gf_production.py:3`、`test_gfx_accel_a0.py:14`、`test_gfx_accel_hal.py:14`、`test_gfx_attach.py:15,21,31,44`、`test_gfx_boot.py:8,46,105,110`。 `test_gfx_bounds.py:23`、`test_gfx_copy.py:14`、`test_gfx_draw_direct.py:13`、`test_gfx_geom_direct.py:18`、`test_gfx_kernel_fb.py:17,19,127`、`test_gfx_map_policy.py:9,10,33`。 `test_gfx_policy.py:13`、`test_gfx_policy_triggers.py:10,14,15,16`、`test_gfx_reattach.py:15,69`、`test_gfx_reinit.py:13,20,23,24`、`test_gfx_transfer.py:28`、`test_h2_fixtures.py:168`。 `test_h5_shapes.py:5`、`test_hdd_stage1.py:854`、`test_hdd_stage2.py:13,82,121,256`、`test_host_mailbox.py:22`、`test_host_notice.py:24`、`test_host_sdk_rs.py:5`。 `test_hostsvc_block.py:13`、`test_hostsvc_block_contract.py:7`、`test_hostsvc_boot.py:52`、`test_hostsvc_contracts.py:16`、`test_hostsvc_e82557.py:8`、`test_hostsvc_fd.py:9`。
- T実物参照：`test_hostsvc_init.py:11`、`test_hostsvc_lgy98.py:8`、`test_hostsvc_lgy98_attach.py:11`、`test_hostsvc_link_api.py:8`、`test_hostsvc_link_core.py:8`、`test_hostsvc_media.py:11`。 `test_hostsvc_netcd.py:13`、`test_hostsvc_nic.py:11`、`test_hostsvc_nic_rx.py:10`、`test_hostsvc_open.py:22`、`test_hostsvc_persistent.py:21`、`test_hostsvc_serialfs.py:21`。 `test_hostsvc_sfs_deadline.py:12`、`test_hostsvc_sfs_lan.py:11`、`test_hsync_h1.py:49,54`、`test_hsync_protect.py:26`、`test_hwb_scripts.py:4,25`、`test_ime_display.py:4,40,48`。 `test_install_recover.py:11,61`、`test_kapi_audit.py:13`、`test_kapi_bounds.py:13`、`test_kapi_layout.py:51,335,350,354`、`test_kbd_fgpoll.py:5`、`test_kbd_inject.py:28`。 `test_kcg_module.py:3,84`、`test_kpeek.py:40`、`test_kstring_c.py:26,55`、`test_lan_module.py:109`、`test_layout_defs.py:110`、`test_ledger_device_map.py:10`。
- T実物参照：`test_ledger_surface_map.py:10`、`test_legacy_callers.py:5,39`、`test_link_notify.py:21`、`test_lowmap_callers.py:3`、`test_ls_client.py:51,54,70,73`、`test_mem_stat.py:8`。 `test_memmap_audit.py:75,78,80,86`、`test_memmap_boot.py:35,218`、`test_memmap_gen.py:93,95,104,420`、`test_mk_settings_db.py:133,214,320,328`、`test_nano_adapter.py:6,173`、`test_nano_inputs.py:7`。 `test_net_link.py:43`、`test_no_libgcc.py:22,23`、`test_np21w_ini.py:183`、`test_osri.py:284`、`test_osri_view.py:14`、`test_packages.py:67,69,160,170`。 `test_paging_bounds.py:28`、`test_paging_device_map.py:13`、`test_pcm_e4.py:50,52`、`test_pegc_mode.py:20,38`、`test_pgalloc_range.py:18`、`test_ppc_selection.py:98`。 `test_raster_splash_asset.py:146`、`test_raw_wait.py:8,102,113`、`test_result_conv.py:239`、`test_serialfs.py:25,1278`、`test_shell_service.py:258`、`test_shm_user.py:51,127,151`。
- T実物参照：`test_shutdown_probe.py:7,9,42`、`test_splash_probe.py:10`、`test_splash_raster_diag.py:11`、`test_splash_wire.py:13`、`test_sqlite_memsys5.py:28`、`test_sqlite_module_proto.py:4,502,538,547`。 `test_sqlite_product_sql.py:14,264`、`test_sqlite_vfs_identity.py:3,77`、`test_src_layout.py:7,59,77,119`、`test_stat_cmd.py:31`、`test_trident_contracts.py:13`、`test_trident_cpu32.py:10`。 `test_trident_emergency.py:9,13,23,30`、`test_trident_gap.py:14`、`test_trident_lifecycle.py:4`、`test_trident_mode.py:12`、`test_trident_module.py:20,22`、`test_trident_palette.py:4,7,17,64`。 `test_trident_pc98init.py:16`、`test_trident_present.py:8,11`、`test_trident_regs.py:11`、`test_trident_resources.py:10`、`test_trident_restore.py:18`、`test_trident_route.py:14`。
- T実物参照：`test_trident_save.py:16`、`test_trident_vram.py:12`、`test_trim_back_rs.py:118,123`、`test_trim_flow.py:9`、`test_trim_kernel.py:7`、`test_unicode_surface.py:7,26,32,42`。 `test_v86_low.py:6`、`test_vfs_fd_path.py:55,444`、`test_vk32_crc.py:7,8,43,44`、`test_vmkernel_layout.py:61`、`test_vmkernel_lz4.py:7,9,24,44`。 `appmem_map_host.c:10`、`boot_splash_native_host.c:44`、`cdinst_host.c:547,548`、`cfg_host.c:6`、`con_sink_host.c:31,34,43,44`、`db_errstr_host.c:9`。 `dep_dir_d3_boot_host.c:10`、`dep_dir_d_console_grid_host.c:6`、`dep_dir_d_display_host.c:4`、`dep_dir_d_emergency_host.c:4`、`dep_dir_d_splash_host.c:4`、`ext2_mini_host.c:7`。 `fep_boundary_host.c:167`、`gf4_b8_host.c:6`、`gf_display_palette_host.c:1`、`gf_wc_host.c:40,50,51,52`、`gfx_accel_a0_host.c:3`、`gfx_boot_host.c:9,147`。
- T実物参照：`gfx_kernel_fb_host.c:30`、`h3b_fixtures_host.c:9`、`hwb_shell_host.c:15,313`、`ime_dict_host.c:5`、`ime_display_host.c:7,45`、`install_recover_host.c:6`。 `kapi_audit_host.c:184`、`kapi_bounds_host.c:6,27`、`kapi_db_v50_host.c:5`、`lease_host.c:2`、`osri_view_host.c:6`、`pegc_mode_host.c:52,57,398,405`。 `r1_recovery_host.c:21`、`shm_user_host.c:8`、`surface_bundle_host.c:13`、`v86_low_host.c:18`、`vk32_host.c:9,10`、`vmkernel_lz4_host.c:6,9`。 `x11_shared_host.c:78,152,186`。 `gfinit_output_host.h:3`。 `gf_wc_recovery_host.inc:35,123`。 `fixtures/build_profile_x86.json:75,153,162,169`、`fixtures/dep_dir_c/manifest.json:149`、`fixtures/dep_dir_d/console_grid.json:3`。
- C covers（glob一致も含む）：`arch-asm.py:3`、`c-dialect.py:3`、`check-select-host.py:3`、`con-sink-host.py:3`、`d2-services-host.py:3`、`deploy-set-host.py:3`、`docs-orphans.py:3`。
- C covers（glob一致も含む）：`double-fault-host.py:3`、`fep-budget-host.py:3`、`fep-standard-host.py:3`、`fep-worst-load-host.py:3`、`font-guard-host.py:3`、`font-host.py:3`、`gf-runner-host.py:3`。
- C covers（glob一致も含む）：`gfinit-host.py:3`、`gfx-host.py:3`、`gshell-host.py:3`、`gui-host.py:3`、`h2-fixtures-host.py:3`、`h3b-fixtures-host.py:3`、`hdd-stage1-host.py:3`。
- C covers（glob一致も含む）：`hdd-stage2-host.py:3`、`kapi-audit-host.py:3`、`kapi-bounds-host.py:3`、`kapi-layout-host.py:3`、`kapi-out.py:3`、`le-access.py:3`、`ls-client-host.py:3`。
- C covers（glob一致も含む）：`manifests.py:3`、`map.py:3`、`memmap-audit-host.py:3`、`memmap-host.py:3`、`memmap.py:3`、`memory-host.py:3`、`mkmod-host.py:3`。
- C covers（glob一致も含む）：`np21w-gui-host.py:3`、`osri-view-host.py:3`、`p2v.py:3`、`privileged.py:3`、`r1-fixture-host.py:3`、`shm-user-host.py:3`、`sqlite-memsys5-host.py:3`。
- C covers（glob一致も含む）：`target-profile-host.py:3`、`target-toolchain-host.py:3`、`test_run.py:156`、`tests-inventory.py:3`、`tools-host.py:3`、`trim-back-rs-host.py:3`、`ver-gfx-host.py:3`。
- C covers（glob一致も含む）：`ver-info-host.py:3`、`vk32-crc-host.py:3`。
- C補助参照：`map.py:3`、`run.py:119,127`、`test_run.py:223,346,350,376`。移設でcovers/inputsの選択範囲が狭まらないことも更新対象。既存のglobに含まれる新ファイルは重複登録不要。
## 6. 受入項目 → 試験 → 変異（実装時）
|ID/項目|host/selected/NPの観測と既存試験|一意anchorの実行時RED変異|
|---|---|---|
|H1 コピー|新copy hostで0/1/奇数単位、unaligned許容C版、前後canary、byte列一致。既存gfx_kernel_fb/attach/copy/transfer。x86 .o比較、PPC selectedでasm制約消失|unitsをbyteとして使用し末尾不一致を観測|
|H2 palette/scroll/flip|新display hostでI/O traceを旧列と照合、-1/height±1、200/400、flip両面、bb無し、palette範囲外/4bit mask/PEGC shadow-only。既存trident_palette/pegc_mode/gfx_boot|SL倍率削除、G/R順逆転、shadow_onlyからI/O、display/access逆転を個別RED|
|H3 gfx lifecycle|新provider hostでinit→fallback/shutdown、boot reserve→identify→probe順、TVRAM/CLIENT/DISPLAY revoke、packed/planar分岐。既存gfx_boot/reinit/reattach/attach/kernel_fb、gf/Trident群|bb無し時TVRAM書込、fallback取り違え、TVRAM revoke省略、両pageの片方省略|
|H4 console/IME|新console hostで80×25と異なるgeometry、末端全角/置換文字/予約行/scroll、sink/GUI/V86 gate、NULL render復帰、候補保存復元の文字/属性byte一致・確保失敗/部分確保/abort/bytes計上。既存dep_dir_d_console/ime_display/unicode_surface/con_sink/legacy_callers、GUI owner拒否|25固定を戻す、最終列全角書込、予約行をscroll、owner検証削除、NULL復帰先誤り、候補attr復元省略、部分確保リーク|
|H5 WC/CPU能力|移設実体＋mock flags/CPUID/MSR、非Intel/能力無し時MSR読取0、flags復元、PAT/alias/範囲、owned/borrowed/二重stop/identity喪失。既存test_geforce wc_lifecycle・gf_wc_host/recovery、acceptログtrace|能力gate迂回、flags復元削除、alias検証削除、active再acquire、borrowed drain省略|
|H6 TRACE|既存sqlite_memsys5/sqlite_product_sql/module_protoにTRACE on/offを追加、実SQLiteをGNU89で組みDB処理成功、x86診断inline/文字列比較、PPC compile-only onでESP消失|fake sp値を0へ置換してTRACE出力のsentinel不一致。off生成物にprovider参照が混入する負例も検知|
|H7 boot選択|新boot hostで既存x86 source/recipe不変、PPC boot/sdk/programs全入口のunsupported拒否とtool spy起動0・出力未作成。既存build_selection/ppc_selection/c_dialect/no_libgccの字面維持|既存PPC拒否gateを1入口ずつ迂回しtool spy起動/出力でRED|
|H8 SDK|新SDK host＋target_profile/build_profile/kapi_layout/gen_kapi_cli/ls_client/host_sdk_rs。単独SDK GNU89/CとRust callbackをbuild、CRT4引数/exit・.os32_kapi_layout/generations整合、同一config/target綴りのRust全.aをmetadata/symbol含めbyte比較。PPCはC/Rust/staging/link全入口で未対応|PPC sdk=True化が実際にtool spy起動を許す、CRT順入替、api/aux交換、call ABI不一致、旧JSON以外選択をそれぞれRED|
|H9 依存・指紋|selected実.dで§7、90既存TU維持、SDK公開契約byte/生成指紋比較、通常/R1 .o/ELF/load section/paired modules/boot像比較。新boundary hostは非PC98 providerを用い暗黙includeを検知|GUIのime.h再include、tvram.hのpc98.h再include、kernel/ime/ime_render.hのコメント1字変更で実生成器を動かしFEP_MODULE_ABI_GEN変化を検知|
- H1/H5のC harnessはhost32.build/run経由、HOST32_RUNNERS=qemu。gcc -m32 -ffreestanding -fno-pie -no-pie -nostdlib -static、自前_startからexit（test_abi_boundary.py:41–51型）、32bit libc/crt1.o不要。H1はx86実copy asmとC providerを各々検証。H5はtest_geforce wc_lifecycle同様にCPU原始操作だけhost_archへ差替え、flags/CPUID/MSRの履歴を返す。製品の能力判定/PAT/alias/leaseはそのまま組む。pushfl等を64bit libc harnessへ混入させない。
- H7/H8のCLI負例は終了コードだけでなく外部tool呼出回数・出力未作成を観測。I/Oやruntime条件の変異を単なる字面assertでREDにしない。x86 CPU命令の特権操作はhostではmock、実命令列は生成物比較の別検証。
|NP21/W項目（PMがCI成果物を停止→配備→起動）|観測する結果|
|---|---|
|N1 起動/復帰（B/C/F）|起動FD（既存1.23MB/1.44MB両製品）、HDD起動、通常/R1、splash→CUI→GUI→CUI。文字/色/カーソル/解像度/ログ順が基点と同じ|
|N2 IME/console（C）|CUI入力・末端全角・候補/予約行、GUI IME、gshell終了後のNULL render復帰、V86往復。表示と入力の両方を確認|
|N3 gfx（B）|planar200/400・利用可能なpacked backend、palette、scroll/flip、dirty present、HAL mono/copy/fill、アタッチ解除/再初期化。基点と画像/画素結果比較|
|N4 WC（D）|起動/描画退行無し・非対応時正常fallback・受入ログ。NP21/Wで対応GPUのWC効果は測れない。実効帯域や実機試験を完了条件に足さない|
|N5 SDK（S）|配布SDKだけで組んだC helloとRustアプリの起動・引数・終了・再起動、LS callback、resident shell aux/APIを確認。boot/CRT選択の変更はbyte一致でもN1/N5で受入|
- I段はtools/build_compare.shでbuild_id/日時固定、対応.oのtext/rodata/relocation、ELF load section、vmkernel.lz4、通常/R1/moduleを比較。移設に伴うsymbol番地の差を正規化して一致扱いにしない。比較の基点は各段直前＋本票基点の双方を記録。
- 実装者は変更したkernel/SQLite・R1・GF受入module・boot像・SDK/該当userlandを省略せずビルド、触った試験--mutateと`python3 -B tools/checks/run.py --lint`。ABI変更ならclean→all。landingはCI check/build、全mutationはfull-check。check-changedは回さない。最後のソース変更後のログのみ合格とする。
## 7. selectedとknownの差分目標
- 現行固定値は90 TU/21組。check_compile.pyの`--source-profile selected --accept --json`と実.dで確認してからJSONを減らす。通過TUを選択から外して件数を良く見せない。移設元/先の同一性も別欄で記録する。
- A/Bの新規通過候補は`gfx/gfx_vram.c`、`gfx/gfx_core.c`、C1は`kernel/console/console.c`、D1は`drivers/video/geforce/gf_wc.c`（GF配置の残る二次障害は実装時に別計測）。各TUの二次障害を実測し、通過数は未確定。gfx_scroll.c/palette.cは既に90本に含まれ、新規成功には加算しない。
- `ime_render_tvram`はPC98選択へ移り、PPCのFB実装ではない。gf_acceptは受入module、SQLiteは専用buildのTRACE両構成、CRT/bootは専用製品なので、selectedの成功数と混ぜない。provider新規TUも共通TUの解消と別集計する。
- C2で解く3組：`kernel/gui.c → kernel/ime/{ime.h,ime_render.h,ime_store.h}`。C3で解く3組：`init/kernel.c → kernel/ime/{ime.h,ime_render.h,ime_store.h}`。実.d消失を確認し21→15組を目標とする。新facadeは本当に必要な関数だけで重いincludeを持たない。
- 残す15組：kernel/sys.c→geforce_module.h（pollが現役）、init/kernel.c→double_fault/gdt/idt（3）、cd/lan/pcm/fm/snd_engine（5）、cirrus/geforce/kcg/trident/trident_module（5）、platform/pc98/boot.h（1）。本票で空ヘッダ化/allowlist削除はしない。sys→GFの1組もWC移設だけでは解消しない。
- selected成功は原始口/依存の検査でありbootinfo・surface endian・callback ABI・PPC全リンク/実走の代替ではない。未対応display/SDK providerを選んだ製品が実行可能扱いされないことをH7/H8/H9で検査する。
## 8. 容量予算・完了・未決
|対象|上限の根拠・本段の見込み|実装時の必須記録|
|---|---|---|
|kernel通常/R1 resident|include/memmap.h:230式は0x275000=2516KiB。移設中心、provider/call増分text概算0–8KiB、rodata/data/bss 0–1KiB、巨大buffer追加なし|__kernel_resident_end/map、通常/R1各headroom。R1も同じ予算で個別計測|
|vmkernel圧縮/起動FD|boot/boot_defs.h:116 MAX_IMAGE_SIZE=508KiB。FD1.44MBはbuild/image.mk:35の1474560byte、1.23MBも既存生成器の空きsectorで判定。圧縮増分は0–4KiB概算だが保証できない|vmkernel.lz4実sizeとFD配置空き。boot sector/loaderはI段でbyte/長さ一致、HDD loaderも比較|
|FEP module|tools/module_schema.py:79 の image_max/mem_max 各 64KiB。C2 で FEP に約 100 行を移す。11-05 の build で fep.elf は text 17.3KB・bss 7.6KB で超えるおそれは低い [推]|C2 の前後の fep.elf の image/mem の大きさ|
|kdebug.bin/paired modules|tools/module_schema.py:55でkdebug image/mem各256KiB。kdebugコード追加なし、import変更無しが目標。kernel pair CRC変更でbin byteは変わり得る|image_size/mem_sizeとpair一致。WCはkernel/GeForce受入module側、受入module自身のpolicy上限も確認|
- 空き容量の基点実測は本作業では未実施。上限を跨がないとは断言しない。超過した段は不受入とし不要wrapper/重複表を減らす。SHM/heap/FDフォーマット・上限の引上げを本票に混ぜない。
- 完了は各段の比較またはNP証拠、既存host追従/変異/lint、CI、selected実.d、容量、KAPI/SDK/CRT/FEP指紋保存が揃ったとき。外部apps/gameはH-7により外部未ビルド。PPC FB/ユーザCRT/target実装は後段。
- ユーザー判断を要する未決：0件。BSDユーザCRT・Rust公式仕様の未確認は後段実装前の調査事項で、今回のPPC明示拒否の選択を妨げない。推奨はx86公開契約を保持し本票の順で分離すること。
## 9. 反論
- 反論なし（0件）。

## 確定 (2026-11-09、Opus 5.5 第 2 回の可を PM が反映)
- N1 (P3)：C1 の行に、C2 で移した PC98 の FEP 部品 2 本 (platform/pc98/ime_render_tvram.c と PC98 の ime_display 実装) が pc98.h を直に include することを足した。
- N2 (P3)：§8 の容量の表に FEP module (tools/module_schema.py:79 の image/mem 各 64KiB) の行を足した。
- PM 決定 (REVIEW.r1 末尾)：F10 公開の os32_callconv.h は作らない (第 3 段 A の実装で公開の os32_kapi_shared.h を byte 一致に保ち、内部の include/callconv.h を呼出属性の口にした決定を正とする。確定設計 PPC_SEP3_DESIGN の A2 との差は第 3 段 F で正典に反映)。F4 boot の段は ppc-powermac の拒否を確かめる試験だけに縮める。F2 IME の candlist の保存と復元は PC98 の表示の backend に置き、IME_Render の口 (FEP の契約・kexports) に足さない。
- 第 1 回の F1〜F13 はすべて r2 で直り、第 2 回で確認された。第 4 段の確定版とは文書 2 本 (arch/README.md・TASK_PPC_SEP.md) だけが重なり、S2/Z を第 4 段 F の後に直列で置く。ユーザー判断の未決 0 件。
