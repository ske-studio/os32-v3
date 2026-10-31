# DEP_DIR_D — 表示 (通常 console・障害表示・復旧・splash) を platform へ — 設計

> 状態: **現行 (2026-10-28)** — 確定 (astra 起草・Opus 2 往復・PM が N1・N2 を反映)。段の状態は票 TASK_DEP_DIR が持つ。
> 出所: ~/os32-tmp/run/depdir-d/DESIGN.final.md を 2026-10-28 に写した。冒頭の『r1→r2 の変更』は往復の記録として残す。


## r1→r2 の変更

- F1: gfinit・kapi_audit・raw_wait の追従と対応表を追加。
- F2: 切出し・変異・コピー元に合わせ既存試験の担当段を訂正。
- F3: L19 の KCG body adapter・display 差し替え・import固定・対応表を追加。
- F4: resident一覧の静的検査とヘッダ欠落の期待失敗を実行時変異から区別。
- F5: 各段に check-ci-stab-host を追加し、新 check_map の broad 指定を禁止。
- F6: v86.mod 不在像・DF注入像の作成条件と実機持越し先を明記。
- F7: PM決定により L16a（属性）と L16b（grid・カーソル）へ分割。
- F8: PM決定により SQLite の直書きを範囲外として §1 と票の「後」への引継ぎに記録。

- 調査基点: `/home/hight/os32-v3` main `b400acb6`、2026-10-28。設計のみ。実装・ビルド・ゲスト試験は未実施。
- 根拠: `PLAN.final.md` §2–4、`SURVEY.md` #29/#46/#47/#48/#53/#54、`docs/tasks/v3/TASK_DEP_DIR.md` §2。
- 単一利用者・単一タスク・協調切替。バグ・機器誤動作からの復帰を維持。権限分離、攻撃者対策、9801互換は追加しない。
- C11、整数演算、既存定数の三層、P2V系を維持。KAPI番号・ABI・モジュールのimport契約は不変。

## 1. 実物の確認

行番号は上記基点。SURVEYの6件はすべて現存し、引用行番号のずれは0件。対象の記述不足を食い違いとして2件数える。

|SURVEY|現在の実物 file:line|判定・設計への反映|
|---|---|---|
|#29|`drivers/video/kcg/kcg_module.c:34–40`|一致。ポート→wait→`public_api()->invalidate()`。未ロード時のstubを残す|
|#46|`kernel/console/console.c:75,102,176,197,486,502`|一致。scrollの属性はu16、通常clear/文字の属性はu8。単一cell書込みに統一しない|
|#47|`arch/x86/kernel/isr_handlers.c:88–100`、`arch/x86/kernel/double_fault.c:72–77`、`kernel/v86/v86_mem.c:171–176`|一致。障害属性はu16。isrの0x07をkprintf変換に通さない|
|#48|`kernel/v86/v86.c:973,985,1044`|一致。復旧本体は既に常駐。v86.mod不在対応を新規機能と扱わない|
|#53|`init/boot_splash.c:62,272,300–305,344–347`|一致。強制選択だけでなくplanar確認・失敗時shutdownも現存|
|#54|`lib/kprintf_attr.c:34,65`|一致。E0h判定→visible補完／旧来前景色変換。全256入力を保存|
|差1: 初期表示の記載なし|`init/boot_display.c:8–18`、`init/kernel.c:139,154`|L18に包含。折返し・下端打切り・属性u8を保存。kernelの呼出位置・文字列も不変|
|差2: recoverセルの記載なし|`kernel/v86/v86.c:1050–1061`、`kernel/console/display_restore.h:10`|状態判定だけでなく専用TVRAMセルread/writeと物理定数もL19で移す|

- F8（範囲外・持越し）: `lib/sqlite3/os32_sqlite_test.c:11–19` は常駐で0xA0000/0xA2000をP2Vなしで直書き。群Dでは触らない（PM決定）。

- 現platform/pc98は boot/boot_disk/boot_names/d2_bind/pci_resources/clock/system/pic/timer/memory と関連ヘッダ。表示実装はまだ無い。
- `platform/pc98/boot.c:243–256`でD2等をbindし、`:229–239`に既存step表。`init/kernel.c:117`のbindは初期表示より前。
- 確定境界だけ継承: D2 §2/4のConOutputOps・EmergencyOps、D3 §3のboot_context/step、B §2/8の固定口・基点列、C §2の固定名ヘッダstatic const。

## 2. 共通境界・所有・呼出文脈

- 今回はBの形を採用し、外部関数をPC98実装1個へリンクする。新規登録関数0、ops構造体0、起動step追加0。
- 通常consoleにも固定口を使う。初期clear/カーソルとkprintf純粋変換が登録状態に依存せず、既存利用者のリンク差し替えも容易。
- D2の`con_output_register(const ConOutputOps *)`と固定`const EmergencyOps emergency_output`はそのまま。TVRAM障害口をどちらにも混ぜない。
- D3の`boot_platform_register(const struct boot_platform *)`／`boot_platform_run(...)`、PC98 bindの呼出位置・順序は変更しない。
- 新ヘッダに機種の物理定数を漏らさない。表示用の新しいstatic const記述表は不要。既存Cのmemory記述は変更しない。

|所有者|残す状態・責務|呼ばれる文脈|
|---|---|---|
|kernel/console/console.c|cursor_x/y、scroll reserve、UTF-8処理、描画抑止、sink/bootlog/rshellの順序|起動前景・通常前景、既存診断の割込み側呼出しも待ち追加なし|
|PC98 console backend|TVRAM表現、漢字2セル、反転ビット、GDCカーソル列。追加可変状態なし|呼出側のIFを維持、IRQ禁止化・確保・登録待ちを追加しない|
|PC98 emergency backend|文字/属性面へのアクセスだけ。可変大域状態なし|paging前の起動、例外、DF専用スタック、V86 teardown（IRQ/例外内を含む）|
|kernelの障害処理|文章・16進整形・表示行・停止／復帰の判断|既存スタックとIFのまま。backendはログ/通常consoleを呼ばない|
|gfx・con_sink・v86低位保存・kcg module|各既存状態の唯一の所有者|platformから参照・更新してよい。ドライバ→kernel依存は残す|
|PC98 splash backend|既存struct gfx_kernel_fbの記述子1個（借用ポインタ）|起動前景のみ、IRQから呼ばない。バッファ自体はgfx所有|

## 3. L18 — 初期・障害表示を固定口へ

```c
/* 新規 include/platform_emergency.h。登録関数・構造体なし。 */
void platform_boot_puts(int x, int y, const char *s, u8 attr);
void platform_fault_cell(u32 pos, u16 code, u8 attr);
/* platform/pc98/emergency.c: 通常console/sink/kprintfに依存しない */
void platform_fault_cell(u32 pos, u16 code, u8 attr) {
    ((volatile u16 *)P2V_IO(TVRAM_BASE))[pos] = code;
    ((volatile u16 *)P2V_IO(TVRAM_ATTR))[pos] = (u16)attr;
}
/* init/boot_display.c: 既存名・引数・呼出側を残す */
void tvram_print(int x, int y, const char *s, u8 attr) {
    platform_boot_puts(x, y, s, attr);
}
```

- `platform_boot_puts`へboot_display.c:10–18のループをそのまま移す。文字u16→属性u8、80列折返し・25行打切り。新しい座標補正をしない。
- isrのputsループはposの進行と整形をkernelに残し、2 storeだけ`platform_fault_cell(pos, (u16)(u8)*str, attr)`へ。
- DFはline→guard→残列空白の順、V86失敗はline→残列空白の順で同口を使う。文字列全体の確保・結合なし。
- 停止ループ、DFのpolled serial、V86の停止前診断とカウンタ、例外時の表示行・0x07/TATTR_REDは一切変更しない。
- `kernel.c`の全tvram_print呼出しは同じ名前で固定口に到達。初期tvram_clear/カーソルはL16bの別口へ移す。
- 固定関数・P2V_IO・既存メモリだけで成立。ロック、待ち、動的登録、ヒープ、V86、モジュール、通常consoleへの呼戻しなし。

## 4. L16a / L16b — 通常consoleと属性

- L16aは純粋属性変換とkprintf wrapperだけを移す。L16bでgridとカーソルを移し、同じconsole試験を拡張する。

```c
/* 新規 kernel/console/console_backend.h。リンク時固定、全関数PC98常駐。 */
void con_backend_clear(void);
void con_backend_scroll(int rows); /* 対象行数をkernelが決める */
void con_backend_putchar(int x, int y, char ch, u8 raw_attr);
void con_backend_readchar(int x, int y, u16 *code, u8 *attr);
void con_backend_putkanji(int x, int y, u16 jis, u8 raw_attr);
int  con_backend_reverse(int x, int y);
void con_backend_cursor_at(int x, int y);
void con_backend_cursor_shape(int visible);
u8 con_backend_printf_attr(u8 input); /* 純粋変換、通常raw属性とは別 */
/* kernel/console/console.cの例。抑止と論理状態の所有者は変えない。 */
void tvram_scroll(void) { con_backend_scroll(TVRAM_ROWS - g_scroll_reserve); }
/* clear: sink通知→GUIならreturn→backend_clear→cursor_x/y=0→sync */
/* cursor_sync: v86ならreturn→con_backend_cursor_at(cursor_x,cursor_y) */
/* lib/kprintf_attr.c: 既存公開名を維持する互換wrapper */
u8 kprintf_attr_to_pc98(u8 attr) { return con_backend_printf_attr(attr); }
```

- clear/scroll/putchar/readchar/putkanji/reverseのメモリ操作・既存境界検査をPC98側へ。kernelのtvram_*署名を維持。
- reverseの右半分判定・左への調整・1/2セル返値、JIS高位補正をbackend所有にする。UTF-8→ANK/JISと論理文字幅は現consoleに残す。
- scrollは文字u16・属性u16の読書き、clearは文字u16・属性u8。高位属性byteの保持／上書きまで基点と同じにする。
- raw_attrは無変換。kprintfだけ従来どおり変換してからsink等へ渡す。shell入力や障害属性を二重変換しない。
- 色ビット判定・visible補完・旧来の青緑赤変換・黒→白を`platform/pc98/console_attr.c`へ全移動。単独hostリンクできる純粋関数に分離。
- GDCのCSRWとCSRFORMだけをbackendへ。shape(1)→sync、stop時sink_enable→V86判定→shape(0)を保存。
- start時sink_disable→kbd_inject_discard→GUIだった場合だけclear→enableを保存。clearはV86でも書く現挙動を勝手に抑止しない。
- gridは現TVRAM_COLS/ROWSの契約を維持し可変解像度化しない。既存の画面サイズAPI・KAPI・sdkは変更なし。

## 5. L19 — 常駐復旧と状態判定

```c
/* kernel/console/display_restore.h: 既存5関数の署名を保ち、機種includeを除く。 */
void display_text_start(void);
void display_restore_98(void);
int display_recover_ready(void);
int display_recover_cell_read(u32 *value);
int display_recover_cell_write(u32 value);
/* 新規 include/platform_display.h: KCGのハード操作だけ */
void platform_display_kcg_code(void);
/* drivers/video/kcg/kcg_module.c: module状態とinvalidate呼出しは元の所有者 */
void kcg_v86_restore(void) {
    platform_display_kcg_code(); /* outp(MODE_FF1_PORT,MFF1_KCG_CODE); io_wait(); */
    public_api()->invalidate();
}
/* kernel/v86/v86.c: この互換入口だけ残す */
void v86_cui_display_restore(void) { display_restore_98(); }
```

- display_outと5関数の本体を`platform/pc98/display.c`へ移す。既存名そのものが固定platform口なので転送層を増やさない。
- `DISPLAY_RECOVER_CELL`はPC98側内部ヘッダへ移す。既存テスト等の利用があればそのincludeを追従。公開の物理アドレスAPIを新設しない。
- 保存済みBDAの所有はv86側、`v86_bios_real_lowmem()`で取得。ゲストBDAに替えない。v86.mod・kcg.mod・登録が無くてもハード復旧は成立。
- 列を固定: GFX STOP→保存BDA条件別EGC/GRCG/PEGC解除→16色/HIRES→400lineパラメータ→PRAM8byte→text_start→表示/アクセスpage0→palette_init→console_hw_cursor_enable。
- text_start内部は高さ/flip更新→GFX STOP→DISP_ON→TEXT START、各wait位置も保存。新しい再試行や同期待ちを足さない。
- readyはsink/高さ/flipで先に拒否し、次にHSYNCの31kHzを判定。readのNULL、writeの0xffff超はI/O前に拒否。専用セルu16読書きと返値を維持。
- gfx_current_height/flipはgfx所有のまま、platformが現順序で更新。回復用セルの貸出管理を新設しない。
- 復旧の通常前景と既存異常復帰文脈を保存。IF操作を追加しない。KCG invalidateは既存関数表経由（L10との共有点はこの1行）。
- [HW1]の新規描画命令なし。既存mode解除、START/STOP、SCROLL設定を移すだけで復旧アルゴリズムを変えない。

## 6. L20 — splashのplanar部分と機種選択

```c
/* 新規 include/platform_splash.h。登録/opsなし、起動前景専用。 */
int  platform_splash_begin(void); /* 0=描画可、失敗はshutdown済み */
void platform_splash_clear(u8 color);
void platform_splash_fill_rect(int x, int y, int w, int h, u8 color);
void platform_splash_clear_pages(void);
/* platform/pc98/splash.c: 旧記述子を移動、バッファを複製しない */
static struct gfx_kernel_fb splash_fb;
/* begin: pref保存→PC98指定→gfx_init→pref復元→framebuffer/format/4面確認 */
/* 失敗: gfx_shutdown→非0。kernel側がtvram_clearしてreturn。 */
/* 終了のkernel側: clear_pages→gfx_shutdown→palette_init→tvram_clear */
```

- bb_clear/bb_fill_rectの本体を移動し呼出名を置換。clip、端byteのmask、4面のbit順、GFX_PLANE_SZ/BPLはPC98側へ。
- planar確認もbeginに移す。異常時に別backendをprobeしない。prefは成功・失敗とも検証前に元に戻す。
- `platform_splash_clear_pages`は現`gfx_clear_planar_pages(GFX_PLANE_SZ)`だけ。CPU描画→既存gfx_present_rasterの経路を保つ。
- glyph配列、配置、整数HSV、gradient、palette設定、dirty矩形、3秒/tickループ、終了時期はinit/boot_splash.cに残す。
- splash_fbはbegin毎に既存取得経路で更新、gfx停止後には使わない。一般描画API・複数backend対応・新しいメモリ確保は追加しない。

## 7. 大きさの見積もり

i386の同じ最適化条件での常駐像増減（text+rodata+data、B）。移動は容量削減と数えない。実測前の予算。

|段|予想増減B|根拠|
|---|---:|---|
|L18|−64〜+192|同じstoreを外部関数へ。puts/整形/停止を複製しない|
|L16a+b合計|0〜+512|既存低位処理移動、9固定関数と既存入口のcall/return差|
|L19|−64〜+128|常駐間の移動、KCGハード口1個。モジュールからの移入は0|
|L20|−64〜+192|planar描画移動とbegin/終了口。fb記述子移動のみ|
|計|−192〜+1024|静的可変状態の増分0B。評価予算+1536B、超過なら重複・配置差を再確認|

- 同じ基点/設定でkernel.bin・vmkernel.lz4・resident end（BSS/align含む）を比較。MEM_KERNEL_IMAGE_MAXと既存link ASSERTを通す。
- 起動FD両形式はBOOT+MINIMAL集合（build/image.mk:6–10）。圧縮・クラスタ丸め後の使用量/空きBを記録。MINIMALの収録物を削らない。
- kdebug.binはcode/import追加0、サイズ増分見込0B。kernelペア情報は再生成。image/memory上限262144B・load span524288Bを既存pack検査で確認（B設計§6）。
- 上限に触れない見込みだが余裕は未実測。最後のmake allでFD2形式/MINIMAL/kdebugを揃えて容量判定し、上限引上げでは解決しない。

## 8. ファイルごとの変更一覧

行数は移動・追加・削除を含む概算。以下はリポジトリ相対名。既存試験の本文は実装時に該当箇所だけ調べる。

|段|新規/変更ファイル|概算行・内容|
|---|---|---|
|L18|新規 include/platform_emergency.h、platform/pc98/emergency.c|15 / 35、固定口と2種類の書込み幅|
|L18|変更 init/boot_display.c、isr_handlers.c、double_fault.c、v86_mem.c|各15 / 12 / 10 / 8、store移動。kernel.cは呼出位置確認のみ、変更0|
|L16a|新規 kernel/console/console_backend.h、platform/pc98/console_attr.c、変更 lib/kprintf_attr.c|ヘッダ属性口 / 65 / 60、純粋変換とwrapper|
|L16b|拡張 kernel/console/console_backend.h、新規 platform/pc98/console.c、変更 kernel/console/console.c|ヘッダ計25 / 150 / 180、grid・カーソルと機種include除去|
|L19|新規 include/platform_display.h、platform/pc98/display.c、display_private.h|10 / 115 / 10、常駐復旧・セル定数|
|L19|変更 kernel/v86/v86.c、kcg_module.c、display_restore.h|105 / 6 / 7、移動と既存署名維持|
|L20|新規 include/platform_splash.h、platform/pc98/splash.c|15 / 100、fb所有とplanar操作|
|L20|変更 init/boot_splash.c|110、機種選択・検証・描画を転送|
|各段|変更 build/kernel.mk|計10〜20、通常/派生ビルドともplatform常駐。v86.mod/kdebugへ入れない|
|各段|新規 tools/tests/dep_dir_d_{emergency,console,display,splash}_host.c|各100〜180、段ごと独立harness|
|各段|新規 tools/tests/test_dep_dir_d_{emergency,console,display,splash}.py|各80〜120、ビルド・fixture比較・変異準備|
|各段|新規 tools/tests/fixtures/dep_dir_d/*、tools/check_map.d/check-dep-dir-d-*.yaml|fixture各20〜100、map各20、基点SHA・入力・列/byte期待値・新ヘッダを登録|
|各段|変更 tools/testsの下記既存利用者|各10〜40、コピー/include/link/置換先追従。期待値緩和なし|

## 9. 受入・試験・変異・退行・段分け

H=host、N=NP21/W。機能受入はエミュレータで足りる（ユーザー決定2026-10-28）。実機目視は次の立会いへ記録し、実装/受入の停止条件にしない。
基点b400acb6の出力を同じshimで一度採取し、入力とSHA付きで固定fixture化。変更後から期待値を生成しない。実行時git不要。

|受入項目|H / N の試験|1行変異（Hで不一致を検出）|
|---|---|---|
|L18 初期表示|H:右端折返し/下端停止、属性上位byteを番兵にし全store幅/順序比較。N:FD/HDDの初期表示|bootの属性storeをu16へ替える|
|L18 障害表示|H:登録なし・通常console未リンクで例外/DF/V86行と空白末尾を比較。N:各障害を別起動で注入し文字/属性と再起動を確認|fault属性storeをu8へ替える|
|L16a 属性|H:全256変換を固定値比較、kprintf wrapper経由も確認。N:CUI色|visible補完のOR定数を0へ替える|
|L16b grid|H:端座標、漢字左右、反転返値、scroll reserve 0/24を固定値比較。N:CUI色/反転/点滅/漢字|reverseのXORをORへ替える|
|L16b cursor・sink|H:V86/GUI組合せ、clearの例外、sink→bootlog→serial/描画列。N:GUI/CUI/V86切替・編集カーソル|stopのsink_enableをV86ガード後へ移す|
|L19 復旧|H:BDA条件全組合せのoutb/outw/wait/状態更新/パレット/カーソル列。N:31kHzでV86正常/異常終了→CUI|PRAMの0x19を0x18へ替える|
|L19 不在/KCG|H:v86.mod未リンク、KCG stub/ロード済みのinvalidate順。N:v86.mod無しでrecover、kcg.mod無しでCUI復帰|KCGのio_waitを削る|
|L19 ready/cell|H:sink/高さ/flip/HSYNC、NULL/範囲、専用セルread/writeの幅。N:31kHzのrecover往復と画面維持|HSYNC判定の==を!=へ替える|
|L20 splash|H:clip・1byte/跨byte・全4面を固定byte比較、pref復元、欠損面、cleanup列。N:31kHzのsplash→CUI|mask_firstの右shiftを左shiftへ替える|
|境界・ABI・容量|H:残存I/O/物理定数の対象関数検査、KAPI/import差分なし、map/pack/FD集合比較。N:同じペア像のFD/HDD起動|C_KERNELを静的に読む検査で常駐display.cの所属を検証し、一覧から外す変異をbuild前に検出|
|新ヘッダ可視性|H:変異用の段コピーだけで全新ヘッダと固定名memoryヘッダを解決し原本参照漏れも検出|コピーmanifestの新ヘッダ1行を削り、隔離コピーのヘッダ解決失敗を期待する（-Werror成功規則の対象外）|
|変異ビルド健全性|H:実行時変異は-Werrorでbuild成功後、assertion不一致で失敗すること。build失敗をkillに数えない|書込み削除は`(void)value;`等の消費を残す1行へ置換|
|変異当て先の一意性|H:対象段の現在ソースにold文字列がちょうど1回、置換後old=0/new=1を確認。0/複数なら準備失敗|一致検査用fixtureの対象行を1行複製する|

- 新しい関数等の変異文字列は実装後のソースから確定する。旧SURVEY行番号・同名の別ファイルを当て先にしない。
- 新しい--mutate付き試験4本は各段のcheck-ci-stab-host対象とする（consoleはL16aで導入、L16bで拡張）。新check_mapにbuild/*.mkを直接列挙しない（broad回避）。C_KERNEL検査の依存は既存の選択規約に沿う限定指定で扱い、lintで確認する。
- 新試験のコピーmanifestとcheck_mapは、共通ヘッダ→platform内部ヘッダ→依存ソースまで列挙。L12/L13/reclogと同じコピー欠落・未使用変数を再発させない。
- HのMMIO shimは値だけでなくアクセス幅・順序・高位byteを観測。Nでは文字取得APIだけを合格根拠にせず、属性面と画面も保存。
- N不在復旧はv86.modを収録から外した専用NHDで起動し、不在を確認してrecoverする。読込み失敗の代用にはしない。kcg.mod不在は別像で確認する。
- Nの#DFは`build/kernel.mk:79–81`のDF_GUARD_TEST=1/2それぞれの像を作成し別起動で注入、設定と結果を記録する。
- 実機障害表示は`docs/tasks/DEFERRED_TESTS.md`のKSTK-DF / T4T7-RHの既存行に条件・確認項目・未実施を追記する（実装時）。
- Nではhost agentを起動しhost_testも実行。実際に起動したkernelペア/版と結果を記録し、HostDrvに置いた事実だけで合格にしない。
- 既存利用者（名前だけgrepで確認）を段別に追従。mock・差し替え定義の二重定義、切出し元、fixture、道具のinclude/link漏れを確認する。

|段|既存利用者（tools/tests、拡張子省略）|
|---|---|
|L18|test_double_fault/double_fault_host、test_v86_low/v86_low_host、test_v86_gcap_abort、test_sqlite_module_proto、test_bootlog、test_dep_dir_d3_boot|
|L16a|test_kprintf_attr/kprintf_attr_host、test_kapi_print、test_gfinit_owner（gfinit_output_host.h）: 属性backendをlink|
|L16b|test_con_sink/con_sink_host、test_unicode_surface、test_tvram_auth、test_kapi_bounds、test_d2_services、test_gfinit_owner、test_kapi_audit/kapi_audit_host、test_raw_wait（check-raw-wait-host対応表）: con_backend_*を解決。test_h3b_fixturesの名前切出し、test_pegc_modeのconsole_cursor_no_dc変異先を追従|
|L19|test_display_cleanup/display_cleanup_host、test_v86_module、test_kcg_module/kcg_module_host/kcg_body_host（platform_display_kcg_codeとMFF1_KCG_CODEの観測）、kcg_display_host、r1_recovery、test_pegc_mode、e10c_gcap_host、kdebug_init_host、recover_virtual_host（display_*差し替え）、test_kdebug_imports（名前/index固定維持）、check-mkmod-host・check-kdebug-init-host（display_restore.h対応表）|
|L20/共通|test_boot_splash_native/boot_splash_native_host、test_display_cleanup/display_cleanup_host（L19とL20両方）、test_dep_dir_c_memory、test_np21w_ctl、test_guest_acceptance|

|段（各Codex最大2時間）|完了範囲・引継ぎ|並列可否|
|---|---|---|
|L18|固定障害口・既存利用者追従・H原本・変異準備。N障害表示受入後L16aへ|群D内は直列|
|L16a|純粋属性変換/kprintf wrapper・kprintf_attr/kapi_print/gfinit追従・H原本・変異準備。N色受入後L16bへ|L18受入待ち、群D内直列|
|L16b|grid/カーソル・既存利用者追従・H原本・変異準備。N通常表示受入後L19へ|L16a受入待ち、L19とは不可|
|L19|常駐復旧/判定/KCG・既存利用者追従・H原本・変異準備。N不在復旧受入後L20へ|L10とkcg_module.cを同時編集しない|
|L20|splash局所移動・既存利用者追従・H原本・変異準備。全体buildとNを節目Dへ引継ぐ|群D内は直列|

- 各段は実装＋関連Hに絞る。実装者はmake kernel・追加/変更HをMUTATE=0・check_select.py --lint・check-ci-stab-host成功を各段の完了条件とする。変異の適用実行と全体検査、NはPMの受入作業として別枠。
- 最終段後はmake all、群D差分の選択検査MUTATE=0と既定静的検査。PMが各1行変異の実行・復元後の原本成功、FD2形式/MINIMAL容量、N/host_testを記録。
- 試験の削除・期待値緩和で2時間に収めない。2時間に収まらないと判明した段は残件を明記して再分割し、次の表示段へ未受入で進まない。

## 10. 未決

- ユーザー判断が必要な未決は0件。固定口・5段直列・NP21/W機能受入を推奨案として採用済み。実測容量と試験結果は実装時の検証事項。

- 票の「後」への追記: F8 `lib/sqlite3/os32_sqlite_test.c:11–19` の常駐TVRAM直書き/P2V未使用は別途対応へ持越し、群Dでは変更しない。

反論: 0件。

## 確定 (2026-10-28、Opus 5.5 の第 2 回 (条件付き承認) を PM が反映 — 本文の該当の行より優先する)

- **N1 (P2)**: L16a の属性の口の宣言は **include/console_attr.h** に置く (kernel/console/console_backend.h ではない)。lib の規則 (build/kernel.mk:174-175 の INC_LIB = INC_COMMON -Ilib、-Ikernel なし) と host の test_kprintf_attr.py:38・:76 の include の道から見えるようにするため。lib/kprintf_attr.c は `#include "console_attr.h"`。kernel/console/console_backend.h は L16b の grid・カーソルの口だけを持ち、属性の口は include/console_attr.h を読む。L16a の写しの manifest と -I の追従 (kprintf_attr・kapi_print・gfinit_owner) に console_attr.h と platform/pc98/console_attr.c を足す。
- **N2 (P3)**: 新しい対応表には build/*.mk を書かず broad に任せる (ROLES §4-1)。
- L16a の変異「visible の補完の OR 定数を 0 へ」の当て先は `attr | TATTR_VISIBLE` (1 回だけ一致)。`TATTR_VISIBLE` だけでは 2 回当たる。
- 作業順は L18 → L16a → L16b → L19 → L20 の直列。各段の NP21/W の受入 (§9 の N) は PM が段の後に回してから次の段へ。L19 と群 A の L10 は drivers/video/kcg/kcg_module.c を同時に触らない (群 A は L10 を L19 の後に)。
