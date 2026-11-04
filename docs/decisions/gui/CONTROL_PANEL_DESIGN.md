# コントロールパネル 詳細設計・改訂2

> 出所: ~/os32-tmp/run/cpanel-design/DESIGN.final.md (astra 起草 r1〜r2、Opus レビュー 2 回、PM が確定)
> 状態: **現行 (2026-11-05)** — 段の状態は票 TASK_CONTROL_PANEL が持つ。

## r1→r2 の変更

- A1: Apply の on_click で同期保存し、on_idle・AppVTable 拡張・ABI/プロトコル世代上げを削除。
- A2: 引数なし RELOAD の予約だけに簡素化し、表示は保存済みまで、反映失敗は gshell の Notice で通知。
- A3: base との競合検出と Conflict を削除し、変更したキーだけ保存。
- A4: bootcfg 専用の固定一時名を作成前に unlink し、残骸から復旧可能に変更。
- A5: O_EXCL を使わず、FS ごとの O_EXCL/NOSYS 対応を不要化。
- A6: CHECK 宣言、gshell host 試験、正常対照、NP21/W の打鍵・目印・判定・終了値を追加。
- A7: 既知キー範囲検査を GUI と cfg set に限定し、cfg import の現行動作を維持。
- A8: writer 統合と短書込修正を前置きの別段・別コミットとし、cpanel 受入から分離。
- A9: 日付と時刻・システム情報・MouseKeys・ダブルクリックの初版採否を推奨付き未決へ変更。
- PM-FEP: FEP の取り分と辞書を後続に回す案を、推奨付きのユーザー判断として残す。


設計案 / 2026-11-05 / 作成: Codex（実行モデルIDは取得不能）。対象リポジトリは `/home/hight/os32-v3`。以下の参照は同ディレクトリ起点。
設計のみ。リポジトリの編集・ビルド・配備・実機試験は行っていない。

## 1. 実物の確認

計画の根拠は `TASK_CONTROL_PANEL.md` §1〜6、`V3_PLAN.md:65`、`TASK_T7_AND_FOLLOWUPS.md:236`。実物との差分は次の **7件**。

| # | 票の前提 → 実物（file:line） | 設計への反映 |
|---|---|---|
| D1 | 設定はCUI中心 → `userland/gshell/src/settings.rs:168`、`:256`、`:452` に既存Settings予約・保存、`modal.rs:652` にGUIがある | 既存の色・時計・font反映を再利用し、入口を新アプリへ置換する |
| D2 | GUIからcfg_*で保存 → `userland/rust/libos32gui/src/cfgro.rs:431` の公開setterはapp scope限定、`:399` は1キー1トランザクション | gshell複数キー保存にはそのsetterを使わず、Cのlibos32cfgをアプリへ静的リンクする |
| D3 | GFXは4値 → `userland/shell/cmd_sys.c:538` と `init/kernel_gfx_config.inc:27` はgeforceも扱う。GUI=1では別backendになる | 既存5値を表示し、保存値と稼働backendを同一視しない |
| D4 | 壁紙は既存設定候補 → `assets/settings/defaults.tsv:22` にキーがあるが、`userland/gshell/src/settings.rs:370` の取得・`:597` の適用にはない | 永続キーがあるだけでは編集項目にしない。初版は壁紙を見送る |
| D5 | FEP予約名はfep.mem_reserve_kb → 実際はsystem/fep_mem_reserve_kb（`kernel/sqlite_engine.c:216`）。defaults.tsvにも行がない | 新しい綴りのキーを作らない。初版では編集を見送る（§2） |
| D6 | 後続票はFEP再起動要求 → `kernel/ime/ime_dict_api.inc:4`、`ime_dict.c:460` はclose→openで即時切替、永続選択を保存しない | 現APIをGUI選択の実装に流用しない。辞書meta後続へ渡す |
| D7 | 音は未整備 → `sdk/kapi.json:1684` にpcm_set_volume、`drivers/sound/cs4231/pcm_cs4231.c:638` に実装があるがPCM owner限定 | アプリ所有の再生音量を全体設定と扱わない。永続キーを増設しない |

確認済みの共通基盤: `kapi/kapi_db.c:141` はEXEC group取得、`:154` はowner記録、`:458` は所有接続回収。接続をgshellへ渡す方式は採らない。
`libos32cfg.h:32` の状態と`:105` のcfg_read_intを使う。cfg_get_intの既定値だけから「読めた」と判定しない。
`about/src/lib.rs:38` の初期化・`:81` の画面内配置、filerのApp/Uiイベント処理、`libos32gui/src/widget.rs:157` 以降のrow/column/label/button/checkbox/textbox/listboxを手本とする。

## 2. 初版の設定と範囲

1本の通常GUIアプリ `/usr/bin/cpanel.bin`、窓1枚、2ページ（§10の未決に対する推奨案）。常駐・アプレット・サービスプロセスは作らない。
シングルタスク・協調切替を守る。数値はすべて整数。9801互換を追加しない（既存GFX値pc98を削除することとも区別する）。

| ページ / 項目 | 保存先・型・許容値 | 反映 |
|---|---|---|
| Desktop / 背景色 | settings.db、gshell/desktop/color、int 0..15（settings.rs:88） | commit後gshellへ再読込要求 |
| Desktop / 24時間時計 | 同、gshell/taskbar/clock_24h、int 0/1 | 同上 |
| Desktop / フォント | 同、gshell/font_set、int 0=標準・1=KCGのみ | 同上。font/genを同一transactionで増分 |
| Startup / backend | /etc/system.cfg、GFX、auto/pc98/pegc/cirrus/geforce | 次のOS起動。現在の描画装置は変えない |
| Startup / GUI起動 | 同、GUI、0/1 | 次のOS起動。現在のGUIは終了しない |

`font/gen` は既存の内部世代で編集欄にしない。初期値の正典defaults.tsvを変更しない。
欠落キーは「未設定」と表示し、既存最小既定値を編集候補として示すだけ。変更した項目だけ保存する。既定値へ戻すボタンは置かない。

| 見送り案（ユーザー未決は§10） | 実物に基づく理由 / 後続境界 |
|---|---|
| 表示周波数 | system.cfgの起動読取（init/kernel_gfx_config.inc:15）とgfxmode（cmd_sys.c:520）に周波数設定がない。Hzキー・モード切替APIを新設しない |
| FEP辞書S/M/L | D6。再起動後の選択を保存・消費する契約がない。§10 U2の推奨案としてTASK_DICT_METAで永続選択と予約を整えてから追加 |
| FEPメモリ予約 | 実在するが起動資源設定。99KiB最小・192KiB既定（lib/sqlite3/os32_sqlite_vfs.h:62）、512KiB全体から管理域とlive量を引く（os32_sqlite_vfs.c:278）。99..512を「安全範囲」と偽らない。予約検証と再起動要求は後続へ（§10 U2、未決） |
| キーボード | input_config.c:54はアプリ別action binding保存であり全体リピートではない |
| MouseKeys、ダブルクリック | 現状は定数。「新しい設定を足さない」だけでは除外理由にならない。初版採否は§10 U1で未決 |
| 音量・出力先 | D7。既存PCM再生のownerを奪わず、master volumeキーを作らない |
| 壁紙・言語 | 壁紙はD4、言語はTASK_I18Nの後。locale/langを先に追加しない |
| 日付と時刻、システム情報 | dateで日時変更が可能、情報表示は既存aboutがある。初版採否は§10 U1で未決 |
| ネットワーク | 個別票の範囲 |

Q1〜Q8への結論: 単一アプリ、明示適用、最小再読込op、次回起動と復旧案内、ソース許可リストなし、全操作キーボード対応、既存CUIと値規則共有、定義表は§4の1か所。

## 3. 画面・編集状態・文言

最大幅560px・高さ340px、実画面の幅−16・高さ−40で縮める。row/columnでページ選択listbox、項目、状態行、Apply/Closeを配置する。
ページを切り替えるとき未保存変更があればSave / Discard / Cancel。保存失敗時は元ページに留まり、未保存値を保持する。
初版はページ単位で保存する。DBとsystem.cfgを1回のApplyで横断して変更しない。両者を原子的に扱うかのような表示を避ける。
Tab/Shift+Tabでフォーカス移動、矢印で一覧、Spaceでcheckbox、Enterで選択ボタン、Esc/窓×でCloseと同じ未保存確認。Enterだけで起動設定を無確認保存しない。
色・backend・fontは既存listboxを使う。slider/combobox等の新widgetは不要。無効値は数値/原文字列と原因を表示し、黙ってclampして保存しない。

```rust
struct Edit<T> { base: T, draft: T, present: u32, dirty: u32 }
enum SaveState { Clean, Dirty, Saving, Saved,
                 RestartRequired, Failed, DurabilityUnknown }
struct Failure { stage: Stage, code: i32, rollback: i32, close: i32 }
// base = 確認済み保存値。dirtyを消すのは保存確定後だけ。実反映の状態は取得しない。
fn apply_page(edit: &mut Edit<Page>) -> SaveReport;
fn request_reload() -> Result<(), i32>; // 保存完了後、DB接続を閉じてから
```

文言は `cpanel/src/messages.rs` のMsgId→英語バイト列へ集約。管理名はapp:cpanelのdesktop.title、save.failedのようにTASK_I18Nのkey規則へ合わせる。
共通のok/cancel/close等はcommon相当として分類し、バイナリ内はenum番号と英語だけ。名称と意味を固定し、後続の生成IDへ置換する。
翻訳DB・locale項目・独自書式処理は追加しない。値と診断は別labelへ置き、後続の番号付き引数対応を妨げない。
描画・幅計測はlibos32guiのfont APIを使用し、UTF-8途中で切らない。KCG fallbackとfont変更による再配置を受け入れる。独自グリフcacheを作らない。

## 4. 設定の読み書き境界

新規 `userland/lib/cfg/cfg_rules.c/.h` に今回のDB既知キーの型・範囲とGUI表示用の項目IDを置く。初期値は移さない。
同ファイルの検査をCUI cfgのsetと新GUI保存経路で呼ぶ。未知のキーは従来の型/長さ検査へ通す。これはソースやアプリの許可リストではない。cfg importは現行のままとし、旧exportの範囲外値も従来どおり取り込める。
GFX/GUIの値検査は新規bootcfg.cに置き、gfxmode/os32guiとGUIから同じ関数を呼ぶ。

```c
/* 新規。公開ヘッダはC89。名前・配列長・列挙値は実装時に固定する。 */
int cfg_validate_known(const char *scope, const char *key, int type, int value);
/* 未知キーは0。既知キーの型/範囲違反はOS32_ERR_INVAL。 */
int bootcfg_validate(const char *key, const char *value);
int bootcfg_read(struct BootCfg *out); /* 値＋存在印＋原文、最大1023B */
int bootcfg_save(const struct BootCfg *draft,
                unsigned changed, struct BootSaveReport *out);
```

cpanelは既存libos32cfgを静的リンクし、アプリのKernelAPIをcfg_backendへ渡す。libos32gui内の静的CfgDbとは別インスタンス。
Apply の on_click 内で cfg_open→begin→set→commit→close を同期実行する。open〜close の間は GUI call/yield/別callback を挟まない。client.rs:356-368 の handler 制限は wait() に対するものであり、DB 操作を禁止するものではない。
gshell 側では settings.rs:13-30 の契約どおり X4/handler で DB に触らず、§5の予約を top-level で消費する。アプリの通常 loop・AppVTable・stub・shlib の変更は不要。

App側のDB handleはアプリownerのままopen→closeする。SHMへCfgDbやSQLiteポインタを渡さない。kernel/kapi_dbの実装変更は不要。

Desktop保存手順:
1. draftとdirtyを固定、cfg_open(writable=1)、cfg_status==CFG_OKを確認する。MISSING/CORRUPT/VERSION/ERRORなら書かない。
2. cfg_beginし、dirtyのキーだけを保存対象にする。baseとの競合検出は行わず、未変更キーは触らない。
3. cfg_validate_known後、dirtyのキーだけcfg_set_int。fontが変わる場合は最新font/genを同transaction内で読み、u32 wrapping_add(1)のビット列をi32で保存する。
4. cfg_commitの返値を確認。失敗なら旧baseを維持。rollbackを試み、最初の失敗とrollback/close失敗を別々に記録して接続を閉じる。
5. commit成功ならbaseを更新してdirtyを消しSavedへ。close失敗は「保存済み・接続解放失敗」とし、再保存を促さない。
6. 接続を閉じた後に§5のreloadを要求する。成功応答は予約受付であり、表示は「保存済み」まで。要求失敗は保存済み表示に要求失敗を添え、保存を繰り返さず要求だけ再試行できる。

ロードではcfg_get_info/cfg_read_intとget後のcfg_status、close結果を保持する。欠損キーとI/O失敗を混同しない。
DB欠損はcfg init、破損はインストール媒体からのrecover-settingsを案内。GUIからDB生成・削除・復元はしない。
既存Settingsの保存は入口置換後に撤去し、読み込み・適用・起動時診断を残す。旧ダイアログ用Save状態を新アプリと二重管理しない。

## 5. gshellへの反映要求

新GUI op `GUI_OP_CFG_RELOAD` だけを番号末尾の未使用値へ追加する。名称は提案。C正典 `sdk/include/os32/os32_gui_shared.h` とRust protoを一致させる。GUI_PROTO_VERSIONは据え置く。同ヘッダ:88 の契約により、未実装opは古いgshellでもOS32_ERR_NOSYSで安全に失敗する。これはKAPI追加ではなく既存GUI要求の拡張であり、公開ジャンプ表・AppVTable・stub・shlibの世代変更やGUIアプリの全再build・混在拒否は不要。生成物は生成器から更新し、アプリは既存client::callを使う。

RELOADは引数なし。handlerは共有のpendingフラグを立て、予約受付の成否だけを返す。重複要求は1件にまとめる。値・token・結果構造体を持たず、STATUS問い合わせも追加しない。X4内のDB/VFS/font openは禁止。
`cfg_reload::pending()`をmultiappのpark条件に足す。gshell top-levelで予約を一度消費し、既存settings::load経路で読み、DBを閉じた後で色・時計・fontを適用する。
load失敗ならrunningを変えず、既存settingsのNotice経路で通知する。commit済みの保存は取り消さない。アプリは反映完了を問い合わせず、「保存済み」まで表示する。
既存settings::apply/apply_fontを専用settings_apply.rsへ切り出し共有。font_set変更時はfont/genとGUI_EV_FONT通知、既存font::reload・lease閉鎖・再描画経路を使う。fallback等の診断もgshell側に残す。
予約はownerに属さず、要求元が終了してもtop-levelで消費する。owner別の結果保持や終了時回収は不要。
REQに保存値を載せない。gshellはDBの最新値を読み、古い画面の値で上書き適用しない。
ソース名・実行パスでcallerを選別しない。通常のGUI slot/owner/版検査のみ行う。アプリは任意DBを開けるという既存権限モデルを変更しない。

## 6. system.cfgの保存と復旧

StartupページApply時は「次回起動から有効」と変更差分を表示し、GFX変更なら映らない場合の復旧文を確認してから保存する。自動再起動はしない。
稼働中に試し切替しないため、N秒確認・自動復帰タイマーは作らない。現在映っている画面を成功判定にも使わない。
復旧案内: インストールFDDから起動 → mount /hd0 → edit /hd0/etc/system.cfg → 直前に記録したGFX/GUI値へ戻す。DBリカバリとは別手順。

前置きの別段として、`cmd_sys.c`・gshell・cpanelが使うwriterをbootcfgへ統合する。機械的移動・接続のコミットと、短書込等のバグ・冗長修正のコミットを分ける。この整理の試験はcpanel受入から外し、先に独立して完了する。
`cmd_sys.c:260` の読取上限・対象キー置換をbootcfg.cへ切り出す。既存実装`:333` のO_TRUNC直書きは短書込で原本を失うので、新GUIにはそのまま流用しない。
1. 原文を最後まで読み（1023B＋余分1B検査）、未知行・コメントを保持する。変更したキーだけを書き換えるため、保存時の最新原文を使う。読取失敗や過長なら書込0回。
2. 重複した対象キーを1行に統合し変更値を追記。未変更キー・未解釈値は保持。結果1023B超、NUL、不正値はINVAL/NOSPCで拒否する。
3. bootcfg専用の固定一時名 `/etc/.system.cfg.bootcfg.tmp` を使う。作成前にunlinkし、NOTFOUND以外の失敗は中止する。O_CREAT|O_TRUNC|O_WRONLYで作成し、O_EXCLは使わない。rename前の電源断による残骸も次の保存で除去できる。writerは処理中yieldせず、協調実行下でこの一時名を共用する。短書込は残りを進め、0/負値なら中止。close後vfs_sync成功を確認する。
4. sys_renameで原本へ置換、vfs_syncを再実行する。置換前失敗なら原本不変。置換後sync失敗ならDurabilityUnknownとし成功にしない。
5. sys_closeはvoid（sdk/kapi.json:626）なので架空のcloseエラー判定はしない。既存rename/syncの返値を検査し、電源断原子性を保証したとは記載しない。
6. DurabilityUnknownでは再読込して現在見える内容を表示するが「永続化済み」とはしない。再同期の明示再試行を用意し、成功後RestartRequiredへ。

O_EXCLを使わないため、ext2以外のFSにおける同フラグのNOSYS分岐や、§1への起動形態調査の追加は不要（A5の条件）。
一時ファイル名・長さ上限はbootcfg.hに集約。cfg_set_keyを使う既存CUIもこの保存経路へ移す（KDEBUG/SFS等の既存値検査は各コマンドのまま）。
CUIとの抽出は関数範囲を取得して機械的に行い、移動差分と短書込改善を分離する。gshellのGUI=更新も同writerへつなぎ、3個のwriterを作らない。

## 7. エラーと資源予算

| 条件 | 結果・表示・再試行 |
|---|---|
| 型/範囲/不正文字 | OS32_ERR_INVAL。項目にフォーカス、書込なし |
| 容量超過 | OS32_ERR_NOSPC。切捨て禁止 |
| DB busy | 既存負コードを保持しBusy表示。自動無限再試行なし |
| DB状態不良 | status＋生のcodeを表示、編集値保持、適用なし |
| commit失敗 | Failed、旧base、cleanup失敗も併記 |
| commit成功・close失敗 | 保存済み・close警告、再反映だけを再試行できる |
| reload要求失敗 | 保存済み・要求失敗。要求だけ再試行し、保存操作を繰り返さない |
| gshellの再読込・反映失敗 | gshellの既存Noticeで通知。cpanelは実反映の成否を表示しない |
| 起動設定の確定保存 | RestartRequired。現在値が変わったと表示しない |
| rename後sync失敗 | DurabilityUnknown。成功表示・自動再起動なし |

追加依存は既存MIT系SDK/GUI/cfgだけ。GPL/LGPLをkernel・部品・SDKへ導入しない。C内部GNU11、公開ヘッダC89、SQLite本体は変更しない。
見積: アプリ専用Rust 700〜1000行、C共有規則/bootcfg 300〜450行、gshell追加・切出し250〜400行。各機能ファイル500行程度以下。
アプリ追加常駐データは編集値/原文/結果を含め8KiB以内、動的全画面bufferなし。GUI既存私有font領域は別計上（libos32gui/src/font.rs）。
DB接続は保存/ロード時だけ。追加像は64KiB以内を目標としてリンクmapで実測する（現時点は未測定）。2MB私有アプリ枠、FEP共存時のpool peak/終了後baselineを受入で測る。

## 8. ファイルごとの変更一覧

下記は後続実装の配置。並列可は同一契約を固定した後の作業分割可否であり、本作業でagentを追加する指示ではない。

| ファイル（新規は＋） | 1機能・関数/状態・見積行数 | 順序・並列 |
|---|---|---|
| ＋userland/rust/cpanel/src/lib.rs | main/App/on_clickで同期保存 100 | model後、直列統合 |
| ＋同/src/model.rs | Edit/SaveState/dirty/保存結果表示 140 | API固定後、UIと並列可 |
| ＋同/src/view.rs | 2ページ・widget・keyboard 240 | model後 |
| ＋同/src/messages.rs | MsgIdと英語文言 90 | UIと並列可 |
| ＋同/src/store.rs | cfg FFI、load/save、SaveReport 220 | 規則確定後 |
| ＋同/src/boot.rs | BootCfg FFI・確認・永続化不明状態 120 | bootcfg後 |
| ＋同/src/reload.rs | 引数なしRELOAD要求・要求失敗表示 40 | proto固定後 |
| ＋同/Cargo.toml | crate・既存ライブラリ依存 | buildと直列 |
| ＋userland/lib/cfg/cfg_rules.c | DB既知キー検査 70 | 独立可 |
| ＋同/cfg_rules.h | 検査宣言・項目ID、共有定義のみの例外 35 | cと同一担当 |
| ＋userland/lib/cfg/bootcfg.c | テキスト設定の読取・置換・確定保存 280 | 前置き別段、移動と修正は別コミット |
| ＋同/bootcfg.h | BootCfg/BootSaveReport・定数、共有定義のみの例外 70 | cと同一担当 |
| userland/cmds/cfg.c | setの既知値検査呼出し | rules後 |
| userland/shell/cmd_sys.c | cfg writer切出し・GFX/GUI検査の共用 | 前置き別段、bootcfgと直列 |
| ＋userland/gshell/src/cfg_reload.rs | pending/consume、失敗時の既存Notice接続 80 | proto後 |
| ＋同/settings_apply.rs | settings.rsからapply/apply_font切出し 約120 | settingsと直列 |
| 同/settings.rs | 起動load/診断を維持、旧編集保存状態撤去 | apply抽出後 |
| 同/handler.rs | RELOADをcfg_reloadへ転送 | proto後 |
| 同/multiapp.rs | pendingでpark | reload後 |
| 同/lib.rs | reload消費、モジュール登録（GUI=writer共用は前置き別段） | 直列統合 |
| 同/startmenu.rs | Settings→cpanel.binの既存launch経路へ | UI完成後 |
| 同/modal.rs | 旧Settings専用UI/コールバック撤去、共通modal維持 | 入口切替と同時 |
| sdk/include/os32/os32_gui_shared.h | RELOAD op正典、version据置き | 最初に固定 |
| sdk/rust/os32api/src/gui/proto.rs | C正典のRELOAD opに追随 | 正典と同時 |
| build/programs.mk | cpanel登録、cfg/bootcfgの必要リンク | crate後 |
| userland/rust/Cargo.toml | workspace登録 | buildと直列 |
| userland/deploy.yaml | cpanel.binを/usr/binへ登録 | build後 |
| ＋tools/tests/cpanel_model.rs | 編集・保存結果・RELOAD要求失敗のhost試験 | modelと並列可 |
| ＋tools/tests/cpanel_store_host.c | 実cfg/rules、故障注入・変異（bootcfg内部は前置き別段で試験） | storeと並列可 |
| ＋tools/tests/test_cpanel.py | 上記harness実行、正常対照後に変異 | 最終統合 |
| ＋userland/gshell/host/cfg_reload_tests.rs | 実handlerの予約・重複要求・park・top-level消費・Noticeを検証 | reload後 |
| userland/gshell/host/integration.py | 上記試験を既存host harnessへ登録、変異と正常対照 | host試験後 |
| ＋tools/checks/cpanel-host.py | CHECK宣言とtest_cpanel.py呼出し、終了値伝播 | 最終統合 |

`tools/checks/cpanel-host.py` の宣言案（budgetは秒、初回実測で調整）:

```python
CHECK = dict(name='cpanel-host', tier=1,
    covers=['userland/rust/cpanel/**', 'userland/lib/cfg/**',
            'userland/cmds/cfg.c', 'sdk/include/os32/os32_gui_shared.h',
            'sdk/rust/os32api/src/gui/proto.rs',
            'tools/tests/cpanel_model.rs', 'tools/tests/cpanel_store_host.c',
            'tools/tests/test_cpanel.py', 'tools/checks/cpanel-host.py'],
    needs=[], budget=30.0)
```

run(ctx)はctx.rootをcwdとし、HOST32_RUNNERSにctx.runnersを渡してtest_cpanel.pyを実行、ctx.mutateなら--mutateを渡し、子の終了値を返す。gshell側は既存tools/checks/gshell-host.pyのcoversとintegration.pyを使う。

## 9. 受入 → 試験と変異

以下は実装時の試験計画であり、今回の合格記録ではない。hostは製品コードをcompile/linkし、I/O等の境界だけfakeにする。

| 受入 | host試験・REDになる変異 | NP21/W / 実機 |
|---|---|---|
| 推奨案の既存5項目、GUIとcfg set同範囲 | 型違い、0/15/16、bool 2、未知キー維持、旧exportのcolor=20をimport可能。変異: 範囲判定削除 | cfg getとGUI再表示を照合 |
| 一括DB保存 | begin/set各位置/commitの故障、font/gen同時性。変異: commit確認削除・gen別txn | FEP有効で色/時計/font保存→再起動後一致 |
| 保存と反映を区別 | commit失敗でrunning不変、close失敗は保存済み。変異: 先行apply・close失敗でdirty復活 | read-only媒体・未反映再試行を確認 |
| X4でI/Oしない | gshell handlerのDB/VFS呼出し0、pending→park→consumeのtrace。変異: gshell handlerでload | アプリWAIT中も完了し、入力停止しない |
| アプリ終了で資源回収 | commit前/後の終了でFD/DB/font lease回収、予約元終了後もRELOAD消費。変異: close漏れ・消費削除 | 正常終了/STOP/fault後にpoolがbaselineへ |
| Startup保存の接続と表示 | bootcfgの成功/失敗/永続化不明をUIへ伝播。変異: 失敗を成功表示・確認迂回 | 使い捨て設定でGUI/GFX→次回起動、FD編集による復旧 |
| font経路再利用 | font/gen wrap、通知取りこぼし、fallback。変異: gen更新/再読込削除 | 標準/KCG、font欠損、再前景化、文字の欠けを確認 |
| keyboardのみ操作可能 | フォーカス順・Esc未保存確認・disabled操作。変異: 確認を迂回 | マウス無しで全5項目、400ライン画面、実機配列で実施 |
| 容量と共存 | サイズ集計、DB全エラー経路で解放。変異: close漏れ | NP21/Wで像/peak/tick、実機で応答時間を別記録 |

実装段階の該当build/check-changedと変更対象の変異を行う。古いgshellへのRELOADがNOSYSとなっても保存成功を失敗へ戻さないことを検査し、KAPI追加が必要になった場合のみ[ABI]のjson末尾追加・再生成・clean全buildを追加する。
NP21/Wは停止後配備・起動して実バイナリ同一性を確認。実機の物理操作は担当者の承認済み手順で行い、hostの結果を実機合格に置換しない。
電源断試験はこの受入に含めない。通常配備でsettings.dbを上書きせず、強制終了試験は承認済みの使い捨て媒体だけ。

前置き別段のbootcfg試験は、1023/1024B、短読/短書/0書込、rename/sync故障、未知行・コメント・重複キー、固定一時名の残骸を除去後に再保存可能なこと、O_EXCLを使わないことを確認する。移動コミットの同値性確認とバグ修正コミットの故障試験を分け、cpanelの受入件数に含めない。

変異は必ず同じfixture・同じ製品コードの無変異版が終了値0になることを先に確認する。各変異では指定assertionが非0になることを要求し、compile失敗・タイムアウトは変異検出の合格としない。変異を戻した版も再度0を確認する。範囲検査削除には有効な0/15と不正16、commit検査削除にはcommit成功と失敗、handler内loadには予約だけとtop-level消費の正常対照を対応させる。

NP21/W GUI試験はrun-os32のkey注入でmake/breakを対に送り、各操作後に目印を待って次のキーを送る。固定sleepだけで進めない。キー名は以下の意味で、実行器の既存キー表へ対応させる。Tab回数は§3のフォーカス順から試験fixtureに固定する。試験用kprintをシリアルで収集し、以下のCPANEL_* / CFG_RELOAD_*は実装時に追加する目印とする（現存するログとは扱わない）。

| 打鍵の列（準備条件） | 目印・観測 | 判定 |
|---|---|---|
| 起動済みcpanelでTab→色一覧→矢印→TabでApply→Enter | CPANEL_SAVE_COMMIT_OK→CPANEL_SAVED、CFG_RELOAD_CONSUMED。rshellでcfg get gshell desktop/color | DBが選択値、画面は保存済み、実反映はgshell観測で確認 |
| 時計欄へTab→Space、font欄へTab→矢印→Apply→Enter（FEP有効） | 保存目印、rshellでclock_24h/font_set/font/genを取得、再起動後にも取得 | 値一致、gen増分、標準/KCG/fallbackの表示を観測 |
| commit故障fixtureで色変更→Apply→Enter | CPANEL_SAVE_FAILED(stage=commit)、rshellの旧値、dirty表示 | CPANEL_SAVEDが出ない、旧値と編集値を維持、正常fixtureでは保存目印が出る |
| RELOAD読込故障fixtureで保存→Enter | CPANEL_SAVED、gshellのNoticeとCFG_RELOAD_FAILED | 保存値は新値、gshell適用値は旧値、成功反映の表示をしない。正常対照ではCONSUMED後の適用を確認 |
| Startup選択→GFX/GUIを変更→Apply→Enterで確認、Cancelも別走行 | CPANEL_RESTART_REQUIRED、rshellで/etc/system.cfgを読み、次回起動を観測 | 確認前/Cancel時は原本不変、確定後は指定値、現在の画面を反映成功としない |
| 編集→Esc→Cancel、編集→ページ切替→Discard、編集→Close→Save | 確認画面とCPANEL_DIRTY/CPANEL_CLOSED、rshellの保存値 | Cancelで編集保持、Discardで書込なし、Save失敗なら窓を維持 |
| 保存後にClose→Enter、別走行でSTOP/fault | 終了目印、FD/DB/font leaseとpool baseline、予約消費目印 | 資源回収、要求元終了後も予約を消費、入力応答を維持 |

run-os32実行結果に加え、試験wrapperは全目印とrshell読戻しの一致で終了値0、不一致・欠落・待機上限超過・起動/配備失敗で非0を返す。各待機上限は30秒とし、ログ・キー列・読戻し値を保存する。人の目で見るfont欠けや実機配列試験は別に記録し、未実施を自動試験の成功で代用しない。

## 10. 未決（ユーザー判断が必要なもの）

**2件。** §2の2ページ・5項目は以下の推奨を採用した場合の案であり、候補除外を確定扱いしない。

| ID | ユーザー判断 | 推奨と理由 |
|---|---|---|
| U1（A9） | 日付と時刻・システム情報・MouseKeys・ダブルクリックを初版に含めるか | 今回は5項目で先行し、4候補は後続へ回す案を推奨。ただし日付と時刻は既存dateで変更でき、システム情報は表示だけでも成立し、MouseKeys・ダブルクリックは現状定数なので「新しい設定を足さない」だけでは除外できない。初版に含める判断なら対応ページと操作範囲を追記する。 |
| U2（PM-FEP） | FEPの取り分（メモリ予約）と辞書を初版に含めず後続へ回すか | 後続を推奨。予約は起動資源の予算検証が必要で、辞書は永続選択と起動時消費の契約をTASK_DICT_METAで整える必要がある。ユーザーが採否を確定する。 |

反論: 0件。

## 確定 (2026-11-05、Opus 第 2 回 Approve、PM が反映)
- B1 (P3): RELOAD の GUI op の番号は **67** (モーダル・セッションの予約帯 67〜79、sdk/include/os32/os32_gui_shared.h:86-88)。「番号末尾の未使用値」(80〜 は予備帯で OWNER_EXIT・TRIM_DONE はカーネルの内部 op、:93-96・kernel/gui.c:70-71) は使わない。
- ユーザー判断として残るのは §10 の U1 (日付と時刻・システム情報・マウスキー・ダブルクリックを後続へ) と U2 (FEP の取り分と辞書を後続へ)。Opus 第 2 回は両方の推奨を妥当とした。
- ユーザー決定 (2026-11-05、7 件とも推奨どおり): §10 の U1 (日付と時刻・システム情報・マウスキー・ダブルクリックは初版から外し後に回す) と U2 (FEP の取り分と辞書の設定も後に回す) は推奨どおりに決まった。§10 の未決はこれで決まった。
