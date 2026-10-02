# T5b 常駐表示接続 / T6a 監査済み有限実行 — 独立レビュー用作業票

> 発行: PM (2026-09-09) / 状態: **撤回 (2026-09-10)**

不採用・凍結 (2026-09-10 ユーザー決裁)。T5b は gshell から撤去、T6a は破棄。
理由: 端末を GUI 協定の外の WM 常駐パネルに置き、単一アプリの暫定を前提に固定テスト子だけを
有限実行する設計は、契約 T2a (4 アプリ) と ROADMAP の目標 (既存 CUI が端末窓で動く) に反する。
経緯と決裁: [AUDIT_2026-09-10.md](AUDIT_2026-09-10.md)。以下は hermes 期の記録として残す。

(旧) 状態: T5b display-only は下記PM決裁で実装発注。T6aは設計案。独立レビュー・受入・ゲスト検証は別ゲート。
PM: Hermes。実装担当予定: Codex。独立レビュー: 実装担当とは別の担当者。
親: [PLAN.md](../../tasks/gui/v13/PLAN.md)、[T5表示分離](TASK_T5_DISPLAY.md)、[T0/T1判定](../gui_v13_reviews/REVIEW_T0_T1.md)。
ファイル名の `T5B_FINITE` は指定された格納名であり、有限実行をT5bへ改称するものではない。

## 1. 到達点と依存関係

- **T5bは表示専用のまま**。常駐gshellで固定fixtureを描く。CUI子起動・FD捕捉は一切含めない。
- **新設T6a**はT5b受入後、固定した監査済みテスト子だけをtop-levelで有限実行し、復帰後にstdout/stderrを別表示する。
- T4モデルとT5R描画アダプタは受入済み。T5aの残受入は未完了。
  最新の依頼者報告ではCirrus `hal_test` が実backend `cirrus` / `packed8` / `640x480`、
  `hw_fill_rect/blit0` を確認済み。これは本票作成者の新規実測ではなく、T5a表示・再露出・復帰の合格でもない。
  [途中保存](../gui_v13_reviews/VERIFICATION_PROGRESS.md)のPC98設定記述は過去のスナップショットとして扱う。
- PM決裁: T5bの実装はT5a残検証と並行可能。**受入順序はT5a → T5b → T6a**。
  T6aは対象BIN監査・同一性/所有権ゲートを経る。並行実装をT5aの受入扱いにしない。
- **完全なv1.3目標は縮小しない**。既存CUI互換、terminal windowとlconsole接続方針、
  console_write/shell_print等の仮想コンソール化、kbd_getchar/getkeyとGUI入力/FEP、対話待機、
  shell script、CUI/GUI I/O抽象化、full-screen GFX復帰、設定レジストリを後続ゲートとして維持する。
  初段の常駐型は製品版の外部terminal appを永久に却下する決定ではない。
  T6aの合格を「対話端末完成」「任意CUI互換」「v1.3完了」と報告しない。

## 2. 現行ソースとの照合根拠

以下は静的確認であり、動作合格や新APIの存在を意味しない。

| 接点 | 根拠と設計への拘束 |
|---|---|
| top-level | `userland/gshell/src/lib.rs:152-167,221-241,255-290`。現行run_programは同期execとGFX復帰。handler/X3/X4からの直接execは禁止 |
| セッション | `userland/gshell/src/session.rs:84-106,117-169`、[v1.2 S1–S8](../../tasks/gui/v12/CONTRACTS.md)。pendingは1件、受理は完了ではない。slotのないCUIの実行中判定をowner_activeだけに依存できない |
| WM描画 | `userland/gshell/src/wm.rs:781-844`。desktop/chrome/modal/taskbar/menuを合成する。T5aのapp用Window/Paintをそのまま常駐WMへ移植する設計にはしない |
| buffer FD | `fs/fd_redirect.c:86-103,109-147,166-179,197-224`。設定成功時に旧redirectをreset、空stdinはEOF、満杯outputはshort write、resetで長さ消失 |
| 親所有 | `exec/exec.c:448-510,933-939,990-1019`。子ownerだけのFD回収と親heap復元。`exec_exit_status`をそのまま返すため負のmain returnとexecエラーが衝突する |
| heap | `exec/exec_heap.c:28-35,59-77`、`sdk/kapi.json`のmem_alloc/mem_free。現在のexec heapに作用し、親管理変数は子実行中に切り替わる |
| 待機/中断 | `exec/exec.c:234-302`、[09_exec](../../09_exec.md)。「KAPI境界でポンプ」は任意のblocking KAPI内部を安全に中断・再描画できる保証ではない |
| モデル | `userland/libos32term/src/stream.rs:4-78`。consumedは入力受理バイト、pendingと未消費末尾は別。finishにも失敗がある |

既存KAPIの `mem_alloc/mem_free`、`sys_open/sys_read/sys_close`、`sys_is_redirected`、
`sys_redirect_fd_buf`、`sys_redirect_get_buf_len`、`sys_reset_redirect`、`exec_run` の宣言と接続を
実装前レビューで再照合する。カーネル内部関数名を新しいKAPI名として呼ばない。

## 3. T5b — 常駐display-onlyの契約

1. WM所有の結果パネルを1個追加する。app Window / Surface / SHM slot / child ownerを使わない。
   固定fixture、stdout/stderr表示タブ、top移動、開閉のみ。実行ボタン・コマンド入力はまだ置かない。
2. 40列×64保存行の独立モデルをストリームごとに持つ案とする。セル/UTF-8/rendererの既存意味を変えない。
   描画は保存状態から再構成し、再露出のためにモデルを再feedしない。
3. WM compositorへ接続する専用Sinkとglyph接続を新規境界にする。screen_infoから本文/状態欄を計算し、
   screen・本文・再描画clip・上位遮蔽物をすべて交差させる。座標変換は描画開始前に検証する。
   modal/menu/taskbar/FEP/cursorを上書きせず、appのPaint/COMMITも結果パネルを破壊しないよう可視領域に反映する。
   PM決裁: 固定パネルのZ順は全appより上、modal/taskbar/menu/FEP/cursorより下。
   app可視領域からパネルを差し引き、Pointer/down/upを背後へ漏らさない。closeは下地をinvalidateし再露出させる。
   chromeの全体書込みは実clip、または実際の書込み領域の再合成で解決する。要求clip内だけを描いたと仮定しない。
4. X4は既存S8のbounded-workのまま。大きな再描画、glyph走査、VFS、確保/解放をX4から行わない。
   PM決裁: 入力は小さい私有flag更新のみ。確保/解放/モデル変更/fixture再初期化はtop-level限定で、子実行中は延期する。
   保存モデルのread-only再描画は既存の非X4 compositor (X3を含む) で許可する。X4ではglyph走査も描画もしない。
   モデル借用と再入のguardを定義し、再入越しの可変借用・二重貸出を防ぐ。
5. 大配列をstackに生成しない。親exec heap上の単一所有領域をtop-levelで確保・初期化し、
   一時的な巨大配列コピーや自己参照struct、根拠のないstatic lifetime延長を避ける。
   非表示でも外部子実行中は親領域を解放せず、再入callbackから親mem_alloc/freeを呼ばない。
   PM決裁: heap上のCellを有効値でin-place初期化してからslice化する。Terminalの寿命終了後にfreeする。
   確保失敗は安全に表示要求を拒否し、nullの借用・自動再試行・部分的な表示開始をしない。
6. fixture切替では旧Terminalの借用を終了して再初期化し、top/prefix/pending/limit/consumedを持ち越さない。
   Full/TooWide/finish失敗を本文外へ表示し、無限retryをしない。
7. T5aの文字・clip・境界fixtureを再利用しつつ、常駐寿命とcompositor接続の新挙動を別試験にする。
   T5b成果物にはexec呼出し、FD設定/捕捉、有限子BINを追加しない。
8. PM決裁: 既存SessionActionは変更しない。display私有flagは先行pendingを上書きしない。

## 4. T6a — 状態機械と単一セッション調停

推奨私有状態は `Idle / Pending / Preparing / Running / Collecting / Result / Refused`。
名前の実装変更は可だが、以下の遷移と不変条件を維持する。

- Idle/Resultから固定テストの要求を受理し、stdinの私有コピーと固定case IDを確定してPendingへ。
  stdinは長さ付き最大256B、超過は全拒否し切り捨てない。初段UIは承認済みfixture選択のみで、自由入力は提供しない。
- terminal要求と既存LAUNCH/SWITCH_CUI/SHUTDOWNは**セッション全体でpending 1件**。
  terminal専用の第2待ち行列を作らない。双方の入口で既存pendingを保護し、競合はFULL/明示拒否。
  GUI公開op・共有プロトコル値は追加せず、WM私有のtag/payloadと共通調停へ接続する。
- 他の外部app実行中にterminal要求を受けた場合は既存sticky Quit→終了→top-levelの順序を守る。
  既存appの正常return/fault/CTRL+STOPで受理済みpendingを消さない。
- top-levelでpendingの私有payloadを取り出して一度だけ消費しPreparingへ。失敗した要求を自動再実行しない。
  入力/描画callbackやX1/X3/X4からVFS照合・FD操作・execを呼ばない。
- **Runningをexec呼出し前に明示設定して画面にもpresent**し、PreparingからCollecting完了までbusyとする。
  CUI子はGUI slotを作らないため `owner_active == false` でもbusyを解除しない。
  Running中は2本目のterminal実行を拒否。通常SessionActionは既存仕様どおり1件だけ保留できるが、
  実行/切替/停止は子復帰と捕捉cleanup後のみ。pendingが空でも再入execは禁止。
- X4/再入経路でclose・clear・fixture切替・freeを実行しない。要求flagとして延期する。
  Collecting→Resultを確定し、FD解除と親heap処理が終わってから保留SessionActionを処理する。
  CUI切替等で結果が表示されない場合も、表示済みとは報告しない。
- Running表示は「起動試行中/終了待ち」であって子開始の証明ではない。実行中出力更新・応答性保証は初段に含めない。
  復帰後の表示は常に `exec戻り値: N (原因未判定)`。0や負値から成功/fault/abort/loader失敗を分類しない。
  preflight拒否は「未実行・拒否理由」でありexec戻り値を捏造しない。

## 5. 固定対象BINの監査と同一性

- 新規テスト子候補は `userland/tests/term_finite_test.c`、配備先候補は
  `/usr/bin/term_finite_test.bin`。現存/実装済みという意味ではない。C89、通常CPL3、CRTを含む最終リンク対象を監査する。
- 任意パス、PATH探索、Runダイアログの任意command、shell構文、任意argv、外部terminal経由のネストexecをこの機能へ接続しない。
  既存一般LAUNCH機能の存在は本捕捉経路への許可ではない。
  case IDはWM私有enumから監査表の固定引数列へ写す (自由argvは不可)。stdin payloadとは分離し、空入力caseでも選択可能にする。
  execへ渡す文字列は固定絶対パスとその固定引数だけで構成し、利用者文字列を連結しない。
  `sdk/crt/crt0_c.c:22-40` の自動help経路へ入る `--help` / `-h` / `/?` は固定引数表に含めず、CRTの_initも監査対象とする。
- 子の通常caseは長さ付きFD0入力を有限回readしEOFを観測、FD1/2へ直接sys_writeする。
  全ループに入力長または明示反復上限。short write/0/errorを検査し、0への無限retryは禁止。
  kbd/IME待機、console/TVRAM/GFX出力、GUI登録、redirect/reset/pipe、exec、DB/SHM/FS更新は禁止。
  未flush stdioを完全捕捉と仮定せず、初段はstdioに依存しない。
- 監査証跡はソース/CRT/リンクライブラリ、ビルド条件、BIN全体、固定case表、禁止経路の到達性を結び付ける。
  リンク全体に意図しないconsole経路がないか独立確認する。ソース名やヘッダ一致だけでは許可しない。
- gshell側に**監査済み完成BIN全バイト**の比較用参照をビルド時に結び付ける案とする。
  固定絶対パスをread-onlyで開き、partial readを扱いながら全バイト比較し、期待長の直後に追加readでEOFを確認する。
  missing/open/read/close失敗、短い/長いBIN、同長の1バイト変更、参照欠落は拒否しexecしない。
  build依存は子BIN→参照生成→gshellの順とし循環を作らない。参照生成・登録はPMが直列化する。
- **非atomicの限界**: 比較用FDを閉じてからexec_runがパスを再openするため、比較後/ロード前の差替え競合は防げない。
  実行直前比較でもatomic identity、安全な任意BIN実行、TOCTOU解消を主張しない。
  初段は管理下の監査テスト専用で、照合から復帰までhotdeploy/対象ファイル更新を停止する運用を前提とする。
  この限定を独立レビューとPMが受容できない場合T6aを止め、kernel/APIのatomic launch設計を別票にする。
  ハッシュ表示・事後readbackは証跡であり、この競合を解消しない。

## 6. 容量・FD・cleanup

| 項目 | 提案上限 |
|---|---:|
| stdin (空入力もbufferへ) | 256 B、実長0〜256 |
| stdout / stderr捕捉 | 各4096 B、初期長0 |
| stdout / stderrモデル | 各40列×64行 |
| Cellの限定リンク実寸 | 8 B、align 4 B ([CROSS_LINK_T4](../gui_v13_reviews/CROSS_LINK_T4.md)) |
| セル2面 | 40960 B |
| 入出力とセルだけの合計 | 49408 B |

これは管理構造、モデル状態、allocator header、比較用参照BIN/読取scratch、コード、alignmentを除く下限。
[MEMORY_BUDGET](MEMORY_BUDGET.md)の静的空間は実行時空きではない。参照BINを含む最終ELF/BSS/heap/stackを再測定する。

1. PreparingでFD0/1/2のすべてが非redirectであることを確認。1本でも既存redirectがあれば、
   設定もresetもせず拒否。既存FDを保存したつもりで上書きしない (復元用APIはない)。
2. バッファとモデル領域は親文脈でmem_alloc。必要な確保が全部成功するまで子を起動しない。
   null/容量不足/寸法overflow/同一性失敗は当該試行の新規領域のみ巻き戻す。
   以前のResult領域を再利用する場合も明示所有し、未使用の生ポインタからsliceを作らない。
3. FD0はcapacity256/実長、FD1/2はcapacity4096/len0でsys_redirect_fd_buf。
   成功したFDだけをbitmask等で追跡。途中設定失敗は成功分のみresetしてから新規領域をfreeし、exec回数0を保証する。
4. 子実行中は親の全領域を固定。mem_alloc/freeは現行heapへ作用するので、terminalだけでなく到達可能なWM callbackを監査し、
   親領域の確保/解放を延期する。別経路が子heapへ親寿命データを確保する問題があれば実装前blockerとする。
   Rustのmut参照をexec/KAPI再入越しに保持して別callbackに同領域を貸さない。短い借用と私有phase境界を独立レビューする。
5. exec復帰後、**reset前に**FD1/2の状態と保存長を取得・検査する。長さは各capacity以下のみ利用。
   sys_is_redirectedは元buffer/owner/世代を証明しないため、維持の根拠は限定子の監査と実測に限る。
   redirect消失・不正長・監査逸脱では該当捕捉を「長さ不明・利用不能」とし、NUL探索や残存bytesで推測しない。
6. 取得した有効長を私有メタデータへ保存し、当該試行が設定したFDをresetしてから表示処理/解放へ進む。
   監査外のredirect置換を検出した場合は通常成功にせず、初段の所有権前提破れとして検証停止・証跡化する。
7. stdout/stderrは別のraw bytes/model/status/top。相互時系列は復元しない。
   満杯は「保存上限到達・追加出力の有無/量は不明」、未満でも「全出力捕捉済み」としない。
   モデルFull/TooWide/finish失敗、入力consumed、pending、未消費末尾を別項目として保持する。
   捕捉上限と表示上限を混同せず、満杯だけから「切捨て発生」、finish成功だけから「出力完全」を表示しない。
8. 結果は次の明示実行/clear/closeまたはshell切替まで保持。モデルの借用終了→領域解放の順。
   全失敗経路・連続実行・close・CUI切替で二重free/UAF/redirect残留/古い結果の誤表示を防ぐ。

## 7. テストを契約へ結び付ける (未実行の受入条件)

新挙動ごとの最初のassertion RED→最小実装→GREENを証跡化する。初回GREENの既存回帰は別記し、
後付けREDでTDD履歴を偽装しない。mockだけでguest FFI/所有権復元の合格にはしない。

| ID | 対象・必須の負例 | 合格条件/証跡 |
|---|---|---|
| B1 | T5b開閉、fixture切替、top先頭/途中/末尾/先頭、Full/TooWide/finish | 保存状態・セル照合。旧prefix/pending/limitなし。exec/FD操作0件 |
| B2 | 3 backend、非整列遮蔽、日本語半分/上下1px、app/modal/menu/FEP/cursor重なり | clip外番兵、再露出画像、閉じた下地とdesktop復元。T5aの画像だけで代替しない |
| A1 | terminal pending→通常action、および逆順、二重terminal要求 | 先の1件だけ保持し後続FULL。exec最大1本、受理と完了を区別 |
| A2 | slotなしRunning中の再入、close/clear、LAUNCH/CUI/SHUTDOWN | phaseで実行中を検知。親free/新exec/設定更新なし、1件の通常pendingはcleanup後処理 |
| A3 | 既存GUI app正常終了/fault/CTRL+STOP後のpending terminal | 受理済みpayloadが維持されtop-levelで一度だけ実行。X1/X3/X4実行0件 |
| I1 | 同長1byte変更、短縮、追記、missing、partial read、read/close失敗、参照なし | 全BIN+EOF照合。拒否はFD非変更/exec0。mockで照合後差替え限界を示し安全保証としない |
| M1 | 全allocation点の順次失敗、既存redirect各FD、設定各段失敗 | fail-refusal、既存FD非変更、成功分だけcleanup、番兵/所有権/リーク検査 |
| M2 | 子復帰前後の親領域、連続実行、reset順序 | get lengths→reset→freeの実呼出し順。親heap/cell番兵とFD残留なし。未公開heap統計APIを捏造しない |
| O1 | stdin長0/256/257、EOF反復、stdout/stderr別の空/境界/超過 | 257拒否、空もEOF。outputは4095/4096/4097要求、short/0の有限処理。既知fixture期待値とUI一般表示を分離 |
| O2 | NUL/ESC/無効UTF8/分割UTF8/末尾prefix/大量改行 | raw長・consumed・pending・表示停止を個別照合。stdout/stderr相互順序・truncated/complete断定なし |
| O3 | main return 0/-1/-2/-3/-4/-5、exec mockの同値 | 表示は全てraw値・原因未判定。負returnを起動失敗modalへ流用しない |
| E1 | 真の起動失敗、fault、明示CTRL+STOP | 下記監査例外に限定。復帰・FD解除・親領域・desktop・後続pendingを別々に証明 |

### 監査例外と故障試験の境界

- 通常caseの「有限read/writeのみ」とfault/control-stop試験は同じ契約ではない。
  固定BIN内の**事前監査したtest-only case ID**に限り、faultはCPL3の故意の不正アクセスを1回、
  CTRL+STOPは明示上限付きの十分長いKAPI反復など、中断を観測できる専用caseを例外として認める案。
  例外の命令/API、回数上限、触る領域、回復手順をレビューで確定し、通常caseからの到達を禁止する。
  任意アドレス指定、任意opcode、永久待機、kbd/IME blocking、新規一般abort機能を追加しない。
- 同じ監査BINのfixture選択にbindし、対象を別の未監査BINに差し替えて通さない。
  制御キーは検証担当Hermesが**該当試験の明示台本でだけ**送る。通常の表示試験や自動復旧にCTRL+STOPを混ぜない。
- 真のloader失敗は通常identity preflightで弾かれるものと区別する。host injectionでcleanupを先に検証し、
  guestでは監査済み専用test configurationの起動資源不足等、再現条件/副作用を独立承認して実施する。
  実際に到達していなければ「起動失敗後回収guest未検証」と残す。identity bypassを製品経路へ作らない。
- guest側のfault/abort識別は承認されたdebug証跡・既存counter・試験入力と照合する検証者の判定。
  UIがraw returnから識別できるという意味ではない。中断前に正常終了したcaseをCTRL+STOP合格としない。
- kernelのloader/fault診断はFD経由でないconsole出力を起こし得る。異常系のTVRAM非漏出は初段の保証外で、
  観測した漏れとdesktop復元を分けて記録する。正常caseについても監査した出力経路の範囲だけ主張する。
  実行不能/危険な故障試験はskip理由と残ゲートを残し、host mockをguest合格へ繰り上げない [V4]。

## 8. 発注範囲・分担・検証運用

**PM決裁: 今回はT5b display-onlyの下記範囲を発注する。T6aは実装範囲外。本票の変更はこのPM決裁事項のみ。**

- T5b許可範囲: `userland/gshell/src/terminal.rs` (新規)、同`lib.rs/wm.rs/visible.rs/input.rs/startmenu.rs`、
  `ffi.rs`の既存symbol私有宣言、必要な場合の`chrome.rs`のclip変更のみ。対応するgshell host試験/TDD証跡。
  `userland/gshell/Cargo.toml`と対応するdependency lockはT5b実装担当の排他所有。
- T6a実装候補: 上記terminalとtop-level接続、`userland/gshell/src/session.rs`、
  `userland/tests/term_finite_test.c` (新規)、独立host試験。FD/execはmock可能な境界へ分離する。
- PM直列所有: 既存buildのgshell/guest-test規則、
  `userland/deploy.yaml`、監査BIN参照生成規則と証跡。既存manifest/targetを読んで正式名を確定し、推測のビルドコマンドを発注しない。
- T4/T5R/T5aソース、共有GUI protocol/shlib、`sdk/kapi.json`と生成物、`kernel/exec/fs/drivers/gfx`の変更は含めない。
  必要性が判明したらblockerを報告して独立基盤票へ。KAPI/ABI変更やv43予約使用を潜り込ませない [ABI1–ABI3]。
- 各段で独立レビュー→対応host試験→`make gshell`と当該子の最終リンク→`make check`、
  ELF/BIN同一性とmemory配置を確認してからguestへ。display-only T5bでは有限子をbuildしない。
  変更なしの既存試験回帰と新挙動のTDD証跡を区別する。
- 後続guest検証の専任操作は**Hermesの直接エミュレータ操作**。local AIの提案待ちを通常進行条件にしない。
  coding/review担当はemulator/配備を操作しない。backendは実hal_testと結び付け、設定名だけで判断しない。
- userlandは既存hotdeploy経路と正確なゲスト対象readback [V1,V2]。常駐gshellの更新は安全なCUI/rshell状態で行う。
  kernel/NHDが別途必要ならbuild→停止とexit確認→emulatorの現imageをローカルへコピー→承認範囲の配備→再起動。
  image/ini上書き・破壊的復旧は操作別承認 [D1,D2]。今回は実施しない。
- 既存dirty変更を保護し、他文書・環境/秘密・`docs/hw/`・ネットワーク・commit/push・追加agent起動を本票作成では扱わない。

## 9. 決裁と完了報告

独立レビューで必須: T5b/T6aの境界、全セッション単一pendingとRunning、WM再入時のheap/借用、
全BIN+EOF比較の非atomic限界、容量下限と確保失敗、各negative testと監査例外の到達性。
重大未解決があれば実装発注しない。

PMが外部レビューを受けて決裁する提案: 常駐結果パネル、256/4096/40×64の初期容量、
管理下BIN限定の非atomic同一性運用、fault/control-stop/loader失敗の具体的監査fixture。
残る重大な仕様判断・kernel/KAPI拡張・破壊的操作はユーザー承認へ上げる。
通常の修正/再レビュー/承認済み試験は段ごとにユーザーへ「次へ進むか」を尋ねず継続する。

各段の報告は変更ファイル、実施試験/証跡、未実施・失敗・制約、独立レビュー判定、次ゲートを分離する。
T5bとT6aを別々に受け入れ、T5a残試験やv1.3の対話/抽象化/設定/互換性の残項目を消さない。
