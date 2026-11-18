# PowerMac の DB・IME 提供者を分ける案

調査基点は `bd2d6eb7ff4b426ac4a0f46828658f438d594a78`。先行調査 `../ppc-kapi-deps/FINDINGS.md` と現行計画 `docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md:193-196` を照合した。指定の `../lease-bundle/DESIGN.final.md` は参照先に見つからなかったため、REQ.md の項目と現行計画に沿って記す。以下の file:line は調査基点。ビルド、preflight の修復、比較、エミュレータによる受入は実施していない。

## DB 17本 — 7b-選択 → 7b

### 所有者と契約

- 共通のまま選択する公開窓口は `kapi_db_open/close/exec/prepare/step/column_int/column_text/finalize/last_error/mem_used` と `kapi_db_open_existing/prepare_only/bind_int/bind_text/bind_blob/bind_null/error_code`。全名に `kapi_db_` が付く。handle 表、入力検証、結果 SHM の組立、`db_cleanup_owned` と `db_cleanup_all` を同じ TU に置く（`kapi/kapi_db.c:27-100,181-465`、`kapi/kapi_db_helpers.inc:17-78`、`kapi/kapi_db_v50.inc:139,286,352-404`）。ユーザー識別は既存の exec、VFS 権限は既存の redir/VFS を用いる。
- `sqlite_engine_available/run/stack_high_water/stack_report`、`sqlite_start/quiesce`、`os32_sqlite_start`、`sqlite_module_ready`、生成された outer の呼出群と `os32_sqlite_snprintf`、`os32_sqlite_test`、SQLite module/VFS 本体は共用（`kernel/sqlite_engine.c:33-98,112-205`、`kernel/sqlite_outer.c:3-23`、`build/sqlite-proto.mk:8-24`）。ガードページの割当・非 present 化・解放時の復帰も `sqlite_start/quiesce` と共通 paging に残す。ガードを確立できなければ既存の開始失敗、再マップできなければ回収を保留する。PowerMac の通常 RAM マップでガードが実際に効くことまで確認する。
- `include/exec_arch.h:11-13` の既存 FP 契約を再利用。 `int arch_kernel_fp_begin(struct arch_kernel_fp_guard *g)` は旧スタック上の guard を受け取り、成功 0、拒否は非 0。成功時だけ、旧スタック帰還後に `void arch_kernel_fp_end(const struct arch_kernel_fp_guard *g)`。併せて既存の `void sqlite_call_on_alt_stack(void (*call)(void *), u32 top, void *frame)` の宣言をローカル extern（`kernel/sqlite_engine.c:37`）から同ヘッダへ。検証済みの stack top と同期 callback を渡し一回実行、エラー値は callback の frame。 `sqlite_engine_run` は未準備・再入・IRQ/例外・IRQ 禁止中の I/O・FP begin 拒否で callback を呼ばず 0、実行済みなら 1（同 :39-73）。拒否後は active/IRQ 状態を元へ。SQL の戻り値と区別する。
- PC-98 に残すのは `platform_boot_services` の呼出順、x86 の `x87_probe`、`arch_fp_save/restore/init`、既存 NASM `sqlite_call_on_alt_stack`。x87 は probe 後に実 FPU または既存エミュレータ側へ入り、x86 用のカーネル FP guard は現状の空実装を維持する（`platform/pc98/kernel.c:251-278`、`platform/pc98/kernel_boot_memory.inc:96`、`arch/x86/kernel/x87.c:75-135`、`arch/x86/arch_kernel_fp.h:3-12`、`build/features/sqlite.mk:3`）。
- PPC に残すのは段 S の `sqlite_call_on_alt_stack`（16 byte 整列、LR・旧 SP 復帰、`arch/ppc/sqlite_stack_ppc.S:6-27`）と `arch_kernel_fp_begin/end`（`arch/ppc/kernel_fp.c:8-45`、FPR/FPSCR/MSR を保存復帰、禁止文脈で拒否）。 `platform/powermac/ppc_kernel.c:294,367-430` の post-root、IRQ 有効、exec 公開前からサービスを一度起動する。 `platform/powermac/boot_capture.c:5-10` の起動情報に検証済みの pairing 値を既存 boot-info 経由で渡すのは実際の検証後。起動失敗時は shell を維持し、共有の失敗経路を使う。 `docs/decisions/ppc/PPC_SQLITE_STACK_DESIGN.md:118-122` の PPC feature 拒否は段 S 時点の境界。

### 選択と依存

- `build/select.mk:162-167,253-257` の x86 限定・PC98_DEVICES 必須を、対応像・EXEC・VFS/書込可能な試験 mount・モジュール基盤の条件へ。書込可能 mount の有無は実行時に VFS が返す。既存 x86 有効構成の入力順を維持し、未対応 PPC 構成は準備完了まで明示拒否。 `build/features/sqlite.mk:2-5` の選択を `build/sources/common.mk` の新規 `C_COMMON_SQLITE` へ移し、engine/outer/test/db を列挙し EXEC かつ SQLITE の時だけ `C_COMMON` に加える。 `kapi_db.c` を pending から外す際は採用を同時に行い、 `sqlite_engine.c` の compile-only を feature off でも検査できるよう PPC の検査集合側で補う（`build/sources/common.mk:10-11`、`build/targets/ppc-powermac.mk:82-89`）。NASM は x86 のみ。PPC は `build/arch/ppc-sources.mk` と `build/select.mk:268-276` の像選択に SQLITE を加えて GAS を一つ選ぶ。
- `HOOK_kapi_db` は選択した実 TU と同じ条件で PPC EXEC に立てる。既存 x86 有効像のフックを維持し、無効構成は生成 wrap とフックの両方が不在実体を選ぶようにする（`build/sources/hooks.mk:6-10`、`exec/exec_kapi_db.h:4-9`、`exec/exec_finish.inc:124-138`）。DB 結果 SHM は既存規約・PPC のユーザー写像・ビッグエンディアンでの SDK 解釈を照合する。
- **DB 単独起動の境界**。 `services_boot` は `SERVICES_UNICODE` から始まり、辞書が無い場合も両 .mod を要求した後にだけ `SERVICES_SQLITE_ONLY` を公開する。FEP ロード・rollback・診断を直接参照する（`kernel/sqlite_engine.c:458-562`、`kernel/module/module_schema.h:47`）。7b では共通のここで FEP 選択を受け、DB 単独時は SQLite だけの必須パス、既存 settings close/outer boot check の後で SQLITE_ONLY を立てる。FEP 側の呼出を選択で外し、両有効の PC-98 では必須モジュール・辞書欠如・逆順 rollback と隔離を維持。 `services_boot/report/ver_state`、`services_settings`、`fep_boot_config_get/fep_boot_user_path_get/fep_meta_probe` は共通のまま。 `init/bootinfo.c:6-12` の起動情報（6e）、`kernel/module/module_image.c:120-128,227-232` と `module_runtime.c:87-90` の共通モジュール基盤（7c と共有）、`tools/prototypes/sqlite_imports.def:113-138` の時刻、engine の `kdebug` 付きメモリ集計（:208-214,551-553）を先に解決。集計に必要なのは既存 memory ledger と任意の診断領域だけ。
- **PPC module の実依存**。 `build/targets/ppc-powermac.mk:51-61,132-157` はモジュール成果物・export object の結線を持たず、 `tools/mkmod.py:65,149-188,266-279` は LE i386/REL 専用、 `tools/gen_sqlite_imports.py:62` も i386。案は共通の loader/検証/寿命を保ち、PPC 対象選択、整数 ABI import thunk、PPC ELF relocation の変換、対象 endian の payload 書込、同じ wire 検証と pair を実装すること。実 ELF の relocation 種別を列挙して許可集合を定める。PPC の main kernel/outer/VFS/import は整数 ABI のまま、FP 命令は guard 内の SQLite 演算域に限定し、同一 module 内の FP 値を受け渡す TU は ABI を揃える。 `kernel/sqlite_outer.c:20-23` の binary64 整数境界と `lib/sqlite3/os32_sqlite_config.h:60` の x86 FP 設定を取り違えない。 .mod を pair 元 CRC の範囲外の検証済み VFS 媒体に置ける構成を先に決める。同じ CRC 対象へ自己参照で埋め込む設計は成立しない。

### 受入

- NP21/W で x87 実 FPU と既存 no-FPU 試験像を通し、書込 VFS で create/insert、既存 DB open、4 型 bind（bind 後に入力バッファを変更）、prepare/step/column、finalize/close/reopen、readonly・不正 handle・別 owner の拒否を確認。終了した子の handle/statement/使用量回収と SHM 結果の寿命を確認。通常の FEP 同時起動、辞書なし SQLITE_ONLY、モジュール欠落・失敗での隔離も確認。
- QEMU mac99 7450/7455 で同じ DB 操作。実数を含む SQL と整数/binary64 結果を既知値で照合し、保存した FPR/FPSCR/MSR・旧 SP・スタック整列、guard page、拒否後の再利用、子終了後の所有者解放を確認。別像ペア、壊れた module、読取専用 mount で既存エラーと shell 継続を確認。SQLITE off の wrap は型別不在、on は map で既存共通定義へ解決。

## IME 17本 — 7a-選択 → 7a（DB が先）

### 所有者と契約

- 既存の resident adapter `kernel/ime/fep_module.c:20-23,84-199` を共有。 `ime_init/toggle/feed_key/set_render/is_active/set_mode/get_mode/getchar/trygetchar/getkey/trygetkey/getchar_kapi/getkey_kapi/switch_dict/user_list_facade/user_delete_facade/user_export_facade/user_clear_facade/dict_info_facade/dict_suspend/owner_exit`（各 `ime_` 接頭辞）と `fep_start/quiesce/module_load/module_abort/module_pages`、`fep_kbd_shift_state` を維持する。モジュールの同名実体は既存の別リンク単位にあり、`fep_module_init` と `FepModuleApi` で接続（`kernel/ime/fep_body.c:5-36`）。 `ime_dict_boot/boot_abort`、変換・文字配送、`fep_store_open/switch/close/search/learn/user_list/user_delete/user_export/user_clear/info`、`fep_user_open`、`fep_conn_close/ready` は共通（`kernel/ime/ime.c:72-111,198-433`、`ime_dict.c:766-1060`、`ime_dict_info.c:94-124`、`ime_user_store.c:171`、`ime_conn.c:11-44`、後者のパスはいずれも `kernel/ime/`）。
- PC-98 固有は `platform/pc98/platform_ime_render.h:4-5` の既定表、`platform/pc98/ime_render_tvram.c:11-60` の `tvram_render_begin/end/putc/putw/clear_row`、`platform/pc98/ime_display.c:16-69` の固定画面寸法と VRAM 2 plane。 `ime_display_cols/preedit_row/boot/bytes/abort/save/restore` を共通の `kernel/ime/ime_display.c` に出し、3行の保存域と確保・失敗時解放・候補を閉じる方針を共有する。機種には寸法とセル I/O を残す。入力は `kbd_trygetchar/kbd_trygetkey` と `input_shift_state` の既存境界（`tools/prototypes/fep_imports.def:40-43,88-97`、`kernel/ime/fep_module.c:23,45-65`）を再利用。ASCII/HID から IME への変換を PPC 専用に作る必要はない。
- 既存の console geometry・read・描画と `IME_Render` を使い、共通化に足りない口は **生セル復元1つ**。 `include/console_cell_contract.h`（新設案）に `int con_backend_restore_cells(int first_row, int rows, const u16 *codes, const u8 *attrs);`。配列は現 geometry の列数で行優先、readchar が返した生セルを行単位で復元。成功 0、範囲・引数不正は INVAL、表示先なしは NOSYS、拒否時は書き始めない。共通 display 側で `ime_display_save/restore(int top,int rows)` の既存 1=成功/0=使用不可に変換し、boot は既存 0/負値、bytes は確保量、abort は反復可。PC-98 は元の 2 plane store を `platform/pc98/ime_display.c` の後端に残す。PowerMac は `platform/powermac/console.c:11-26` と fbcon のセル描画を使用し、全角ペアの属性・幅を保って復元。必要なら fbcon 内部で幅情報を既存セルの表現に沿って保存する。変換・候補描画の手順は既存 `kernel/ime/ime_render.inc` を共有。PPC の `platform_ime_render.h` は共通 console レンダラ表を選ぶ小さな定義ファイル。レンダラを共有化する際は ANK/JIS・属性変換を既存 console API に通し、PC-98 の生セル・色依存だけを後端に置く。
- PowerMac は `platform/powermac/ppc_kernel.c:367-421` の root・Unicode・表示準備以降に DB と同じ `services_boot` へ入る。 `platform/powermac/usb_platform.c` の既存 HID 配送から共通入力を読む。GUI は `kernel/gui.c:178-184` の登録経由で既存の `ime_set_render` に `IME_Render` 表を渡す。入力・辞書機能の公開に GUI 登録は不要。表示未準備時はセル復元 NOSYS、描画を利用する公開は表示の受入後とする。

### 選択と受入

- `build/select.mk:152-157,243-251` の arch/PC98_DEVICES 条件を EXEC・SQLITE・FEP モジュール供給可能な構成へ置換。SQLITE 必須は維持する。辞書の存在は VFS の runtime 判定。 `build/features/fep.mk:2-5` の adapter を `build/sources/common.mk` の `C_COMMON_FEP` へ移し、EXEC＋FEP 有効時だけ選ぶ。共通 display TU と renderer を `build/fep-module.mk:8-19` のモジュール本体の一覧に一回だけ追加し、同規則の機種別ソースはセル・既定表だけを選ぶ。PPC core/noexec で選択しない。 `HOOK_ime` は両像とも実 adapter の選択に揃え、`exec/exec_finish.inc:169` の `exec_ime_owner_exit` を通す。PPC は DB と共通の pack/load・export 検査を再利用し、辞書・必要なフォントとユーザー辞書に使う書込先を root 構成へ結線する。
- NP21/W で CUI の英数/かな/ローマ字、変換・候補キャンセル・候補画面と属性の復帰・最終行の scroll reserve を確認。GUI 登録と解除、入力の shell 復帰、子の終了時の未確定入力回収を確認。辞書切替、編集・export・suspend/resume、失敗した切替後に旧辞書が利用できることを確認。辞書破損や表示領域確保失敗時は FEP と SQLite の逆順 rollback／隔離を確認。
- QEMU mac99 7450/7455 では USB キーで同じ読み・候補を再現し、実画面寸法で候補の表示・元セル復元、全角・属性と端を確認。try は入力が無い時の通常値を返し、非選択像の NOSYS と区別する。CUI/GUI の切替と owner exit、辞書メタ情報、書込可のユーザー辞書で編集・export、無し/読取専用時の既存戻り値、失敗後の shell 継続を照合。GUI 受入は段 5d 後。試験辞書二つは同じ読みで違う候補にして切替の実効性を検出する。

## 本線の4段と x86 比較

|計画 §3|その場の変更から純移動・採用へ進む順|x86 build_compare|
|---|---|---|
|7b-選択|現行順のまま実前提を定義し、共通選択リスト化と CPU 別 ASM 選択。loader 未成立の像は拒否を維持。|既存 x86 有効像で関数差0を要求。PC98_DEVICES 無効で新たに成立する構成は選択テストで確認。|
|7b|engine の位置で契約化と DB 単独起動を作り、PPC 側に段 S・module・boot を結線。既存 engine/outer/db の物理移動は不要。サービスの分割が必要なら動作編集の後に純移動。PPC の DB 公開は最後。|PPC の結線・公開表のみと純移動は関数差0を要求。共通 services/loader の x86 生成関数に差が出る変更は別差分として申告し NP21/W で受入。|
|7a-選択|7b の SQLite 選択を前提に、現行順で FEP 選択を整理し、adapter の所属を共通 feature リストへ。|既存 x86 有効像で関数差0を要求。機能 off と不整合を選択テストで確認。|
|7a|PC-98 表示の場所で寸法・セル入出力と共有保存処理を先に分離し NP21/W。その後共有部分だけを共通 TU へ純移動。PPC のモジュール・入力・表示を結線し、辞書・文字/キー・GUI render の順に受入済み slot を公開。|セル復元の外部呼出し化や寸法・表示処理の変更は現状との関数差0を前提にできない。切り出し後を基点とする純移動と PPC のみの変更は関数差0を要求。|

各採用段でリンク対象と定義元を map/nm で照合。x86 の順は `build/sources/order.mk:7,29-31`、PPC の選択と検査は `build/targets/ppc-powermac.mk:38-64,82-89,132-144`。純移動は SRC_MOVE の対応表と試験 covers・変異アンカーを追従し、レイヤ違反0と common-compile を確認。公開表は `sdk/kapi_target_ppc.py:117-135,143-161,223-228` の受入済みスロットと include を更新し、`IME_SLOTS` 利用者も追従。現行計画 §4–5 に従い x86 生成物のバイト一致と PPC 通常 wrap の定義元、残りの型別不在を照合する。

## 本線判断が要る点（2件）

1. **PPC の .mod の供給と pair 元**。7c と共有する PPC pack/load の先行作業と、pair 元 CRC の範囲外で検証済み .mod を読む配備経路をどの像で受け入れるか。初回は書込テスト用 mount を別途確保した EXEC 像を提案。
2. **PPC の最初の IME 対象**。7a の前半を表示・USB 付き CUI 像で受け入れる案。最初に同梱する辞書とユーザー辞書の書込先、および GUI render を 5d 後の公開差分とする境界を本線で確定する。
