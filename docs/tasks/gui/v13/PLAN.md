# GUI v1.3 — 着手計画

> 発行: PM (2026-09-09) / 状態: **受入完了 (2026-09-14)**

2026-09-10 に監査 ([AUDIT_2026-09-10.md](../../../archive/gui_v13/AUDIT_2026-09-10.md)) を経て再編。
ユーザー決裁: T5b (常駐表示パネル) は撤去、T6a (監査済み有限実行) は破棄、設定 F1/F2 は保持、
**端末の形は B = 契約 T2a (GUI アプリ 4 本) を v1.3 の最初に実装し、端末は外部アプリ**。
最初の票は [K5 (4 アプリ同時実行)](../../../archive/gui_v13/TASK_K5_multiapp.md)。以下の旧記述は経緯として残す。
**2026-09-11**: K5a の設計を決裁 (D9 の 8 分岐 + D11、票末尾の「決裁」)。KAPI **v44** を予約。
[K5b-K (カーネル)](../../../archive/gui_v13/TASK_K5B_kernel.md) と [K5b-W (gshell)](../../../archive/gui_v13/TASK_K5B_gshell.md) は実装済み・**実機受入は G9 以外すべて合格**
(2026-09-12、G1〜G8 / G10。G9 は 2 本以上を同時 ready にする観測手段が無く未実施)。受入中に見つけた
K7 (8MB で shlib .data 4 ページの勘定漏れ、`8cc13d8`) と W-3 (背面窓の枠線が前面のクライアント面に落ちる、`36ccf15`) は修正・実機確認済み。
**[K6-RAM (物理 RAM 上限の撤廃)](../../../archive/gui_v13/TASK_K6_ram_ceiling.md)** は `f6ec520` で着地、M1〜M5 合格 (32MB / 128MB / 15MB / 8MB)。
K6 の決裁事項 4 件は 2026-09-12 にユーザー決裁 (2.8GB 見送り / `mem` に実 RAM 合計を並記 / 0594h 無しは何もしない / Cirrus 窓の移動は Cirrus レーン再開時)。G9 はホスト模型を受入とし K5b は**完了**。feat/gui → main のマージも同日承認。**K6C console の差し込み口は完了** (K 側 `d381000` KAPI v46、端末アプリ `abef34f`、受入 C1〜C4 / A1〜A5、2026-09-12)。**K7 入力統合も完了** (設計は独立レビュー通過、K `c6a775d` KAPI v47 / A / W / W2、受入 I1〜I5、2026-09-12。GUI 中の CUI プログラムは `kbd_getchar` で park され、端末アプリの打鍵で起きる)。**T7「CUI コマンドを端末で流す」も完了** (K `7a6b124` EXIT レコード、A `ce0406b` プロンプト / 起動 / 接続モード、受入 T1〜T5、2026-09-12)。**T8 full-screen GFX 復帰は完了** (画面の所有者 + 宣言ビット `gfx` / `cui`、gshell の全画面モード、ポーリング型の協調 yield (tick に 1 回)、受入 F1〜F8 合格、2026-09-12)。PEGC 構成も確認済み (K6-3 `1a559a0` で K6 の probe 回帰を修正)。8MB は端末 + 子が NOMEM で仕様どおり。次工程 **T9 shell script** (常駐シェルのソースを CPL=3 の `sh.bin` としてもビルドし端末から起動、外部プログラムは端末経由で起動して `sys_yield` で待つ) は設計 `TASK_T9_sh.md` 第 6 版が Codex の独立レビュー (3 往復) を通過 (2026-09-12)、K / ビルド系 → S / W / A を発注。その後 設定 S0〜。
レビュアー (ChatGPT) は枯渇し、以後の合否はユーザーが PM の材料で判断する。

T5b 撤去 (2026-09-10、`1c98613`) で消えた被覆: 「上位 UI (メニュー / モーダル / タスクバー / FEP)
の上で離しても、アプリの次の押下を飲み込まない」ホスト試験 4 本は、パネルが最初の押下を食う
仕掛けに依存していたため削除した。**完了 (2026-09-14)** — パネルに依存しない形で書き直し済み。
上位 UI 自身が押下を取る側は `89ff220` の
`upper_ui_press_release_then_app_press(kind)` → `taskbar` / `menu` / `modal` / `fep`
の 4 本、モーダル中の捕捉済み離しは `captured_release_{,right_}is_delivered_while_modal_is_open` /
`captured_release_survives_the_pump_while_modal_is_open` / 負例
`new_press_during_modal_is_not_forwarded_to_the_app` (記録 `tools/tests/gui_review4_20260910_tdd.md`)。
残っていた裏側 (**アプリが受けた押下の対になる離しを上位 UI の上で行う** + その直後の押下)
は `app_release_over_{taskbar,menu,modal,fep}_reaches_the_app_and_the_next_press_lands` の 4 本で
メニュー / FEP まで広げた (記録 `tools/tests/gshell_buttonup_tdd.md`、`check-gshell-host` 100 本)。
製品コードの修正は無し (`release_capture()` 経路は 4 経路とも正しかった)。

**CI の範囲 (独立レビュー 2026-09-10 の指摘)**: `.github/workflows/check.yml` は静的整合性
(manifest / 制約 ID / KAPI 版数) と NE2000 試験が中心で、`check-gshell-host` や filer の
ホスト試験は**走っていない** (CI 環境に rustc が無い)。**CI 成功を GUI の動作確認とは扱わない。**
GUI 側の回帰は手元の `make check` (rustc あり) と実機が正。CI に rustc を足すかは別途決める。

**残件 (ユーザー指示 2026-09-10)**: 試験項目の棚卸し — 「テスト内容が必要ない項目がある」ので、
ホスト試験 (`make check` の各 `check-*`、gshell の `wm_tests.rs`、`tools/tests/*`) を
「何を守っているか」で分類し、重複・不要・弱い (構造ガードだけ) ものを整理する。次回の議題。
**残件**: ゲストで `st_dev` / `st_ino` を観測する手段が無い (`stat` コマンド不在)。`2005d70` の
実機観測は未実施でホスト試験が根拠。小さな `stat` コマンド (userland/cmds) を足すかは別途。

(旧) 状態: T4/T5R受入済み・T5a残受入未完了・T5b/T6a設計の独立レビュー待ち。
体制は [agents/ROLES.md](../../agents/ROLES.md) が正典 (2026-09-09 に 4 役へ移行)。
既存の票 (`TASK_*.md` / `REVIEW_*.md`) の担当欄は**発行時点の記録**であり、書き換えない。
T5aは部分ゲスト検証済み。最新の依頼者報告ではCirrus hal_testが実backend `cirrus` / `packed8` /
`640x480`、`hw_fill_rect/blit0` を確認したが、T5a表示・再露出・終了復元等の残項目は未受入。
この更新で新しいゲスト試験は実施していない。CUI実行統合契約は未凍結、統合実装・検証は未実施。

次ゲート: [T5b常駐表示 / T6a有限実行票](../../../archive/gui_v13/TASK_T5B_FINITE.md)の独立レビューとPM契約凍結。
T5a残受入 → **表示専用T5b** → **新設T6a (監査済みテスト子の有限実行)** の順に別々に受け入れる。
T5bへCUI起動/FD捕捉を混入しない。T6aは対話端末完成ではなく、完全なv1.3目標と設定S0以降は維持する。
後続の専任ゲスト操作はHermesが直接担当し、承認済みの通常工程は定例確認で停止せず継続する。
仕様拡張・kernel/KAPI変更・image/ini上書き等の実際の承認境界は別途決裁する。

親計画: [ROADMAP](../../../ROADMAP.md)。現行契約: [v1.2 CONTRACTS](../v12/CONTRACTS.md)。
設定レジストリの仕様案は [settings DESIGN](../../settings/DESIGN.md) を正典とし、ここには進行条件だけを置く。

## 1. 範囲と進め方

**2026-09-10 以降の順序** (決裁 B): K5 4 アプリ同時実行 (契約 T2a) → K6 console の差し込み口
(GUI モード時の `console_write` / `shell_print` を端末モデルへ) → K7 入力統合 (`kbd_getchar` ←
GUI イベント、FEP) → 端末アプリ (T4/T5R を Paint に接続、外部アプリ) → 既存 CUI コマンドを
端末で実際に流す → full-screen GFX 復帰 → shell script → 設定 S0〜。固定 fixture の段は作らない。
以下の §1 本文は hermes 期 (09-08〜09) の記述で、「single-foreground を新契約まで変えない」は
**撤回** (契約は 4 本。1 本は v1.2 の暫定)。

v1.3 の目標は GUI 上の CUI 実行、入出力抽象化、設定レジストリ。
最初から対話端末の完成を主張せず、有限入力・終了後出力表示の縦切りから安全性を確かめる。
既存の single-foreground-app、X3/X4 の制約は、新契約を承認するまで変更しない。
KAPI の追加が必要なら予約番号を KAPI_SPEC §3-2 と調停する。v43 を無断で使用しない。

## 2. 確認した接点とブロッカー

### ターミナル

- `docs/tasks/gui/v12/CONTRACTS.md` S1/S2: 外部アプリは1本。handler/X3/X4から直接execは禁止。
- `fs/fd_redirect.c:166-179,197-209`: バッファ入力は空でEOF、出力は固定容量のshort write。対話ストリームではない。
- 調査指摘: GUI中はraw入力で、既存CUIのcooked待機へ直接は届かない (`drivers/kbd.c:361-408`)。
- 調査指摘: FD捕捉だけでは直接コンソール出力を拾えない (`kernel/console.c:214-346`)。
- 調査指摘: lconsoleは80×25・原点固定描画。窓内surface/clipへそのまま接続できない
  (`userland/lib/gfx/lconsole.h:10-27`, `userland/lib/gfx/text/lconsole.c:297-360`)。

最小候補は gshell 常駐側で結果を表示し、top-levelからCUIを1本実行する構成。
ROADMAPの「terminal app」との差異は未決であり、外部terminalから外部CUIをネスト起動する実装は発注しない。
対話入力、実行中描画、FEPには安全な待機・サービス点と終了契約を別途定義する。

### 設定レジストリ

- `kapi/kapi_db.c:187-216`: 現行openは `sqlite3_open` と `journal_mode=DELETE`。真のreadonly/no-create契約を提供していない。
- `kapi/kapi_db.c:254-281`: execはSQLを固定バッファへコピーし、先頭statementだけprepare/stepする。
  `BEGIN; UPDATE; COMMIT` を1回渡す設計にしない。
- 調査指摘: 通常NHD配備は対象を上書きする (`tools/nhd_deploy.py:642-655`)。
  マスタを通常配備へ登録すると、ユーザー設定を保持する要求と衝突する。
- 調査指摘: bind API不在とSQLサイズ制限により4KB blob書込み方式が未成立。
- 調査指摘: SQLite VFSのsyncエラー伝播、接続所有権、全接続共通SHM、FEPとのプール共存を追加確認する。

「KAPI追加なし」は未検証の目標。非破壊性を優先し、S0で可否を決定する。

## 3. 作業票と担当予定

| 票 | 担当予定 | 成果物・許可範囲 | 完了条件 |
|---|---|---|---|
| T0 待機・所有権契約 | claude5 / opus5 | 読取調査と契約案。kernel/exec/FD/kbdの接点を追う。コード変更不可 | 常駐型/外部型比較、blocking read中のサービス点、出力捕捉、CTRL+STOP/fault/起動失敗時の回収を根拠付きで示す |
| T1 端末表示設計 | Codex | T0と並行でlconsole/gshell調査。コード変更不可 | セル・UTF-8・窓内clip・再露出の設計と、有限入出力の最小縦切りを定義する |
| S0 非破壊DB契約 | claude5 / opus5、PM決裁 | settings DESIGNの改訂案。配備・KAPI変更不可 | 下記の非破壊/transaction/型/媒体契約を凍結し、後続基盤票を分離する |
| R0 独立レビュー | 実装・設計担当と別のエージェント | 読取専用レビュー | 未解決の重大指摘を列挙し、解決前は実装ゲートを通さない |
| V0 検証設計 | PM + 専任検証セッション | ホスト/ゲスト試験の対象と証跡形式 | 下記受入条件を再現可能な台本に落とす。実行は成果物確定後 |

T0 (claude-opus-5) とT1 (Codex) の読み取り専用設計を受領済み。
PMレビューのR1〜R5をT0修正版が反映。統合の安全条件が未解決のため契約凍結は保留。S0は未発注。
判定と次のゲートは [REVIEW_T0_T1.md](../../../archive/gui_v13_reviews/REVIEW_T0_T1.md) を参照。
メモリのホスト測定は [MEMORY_BUDGET.md](MEMORY_BUDGET.md)。ゲスト実行時の空きは未測定。
T0とS0を同一エージェントへ同時発注しない。共有ファイルの編集はPMが直列化する。

## 4. S0で凍結する事項

1. missing/破損/異版/readonlyのopen挙動。missingは仮想ハンドル等で扱い、新規DBを自動生成しない。
2. readonly/no-createを既存APIで保証できるか。query_onlyや事前statだけを非変更保証の代わりにしない。
3. BEGINは明示呼出しに一本化する案を検討。set失敗後のcommit、close時rollback、接続直列化を定義。
4. int32、scope/key/text/blob上限、UTF-8、SQL引用、cap不足、エラーと既定値、列挙再入の契約。
5. 4KB blobの方式: 制限変更、分割書込み、bind API追加を比較し、黙って切り捨てない。
6. 媒体マスタ、新規インストール、通常更新、明示リカバリを分離。通常deployは既存/欠損DBとも変更しない。
7. sync/rollback journalの耐障害性。MEMORY journalをクラッシュ回復の代替にしない。
8. FEPとの共有プール、共通SHM、子終了時のDB cleanupを含む寿命。必要なら独立した基盤修正票を切る。

## 5. 受入試験候補 (未実行)

### ターミナル

- 有限stdinのEOF、stdout/stderr分離、容量超過の通知、次コマンドへのredirect残留なし。
- 正常終了、起動失敗、fault、CTRL+STOPでdesktop・所有資源を回収する。
- 完全統合段階ではprintf/kprintf/直接コンソール出力を窓へ集約し、TVRAM漏れなし。
- UTF-8分割入力、右端の日本語、CR/LF/BS、スクロール、窓再露出、窓外描画なし。
- 対話段階ではblocking read/getkey、ASCII/矢印/FEP、未入力時の待機、Quit/abortを確認。
- PC98/PEGC/Cirrusの回帰とGFX子終了後のdesktop復元。

### 設定

- missing/空/破損/異版/開けないDBでfallbackし、元DBとjournalを変更しない。
- readonlyで全更新を拒否。hot journalを含む失敗時も非変更条件を検証。
- commit/reopen、rollback、close、途中失敗、容量不足で期待する永続化と回復を確認。
- 型・長さ境界、空blob、最大blob、SQL引用、cap不足、先頭行、enum再入。
- FEP変換/学習との交互実行と異常終了後のpool/FD/DB slot回収。
- 通常配備で設定非変更。recover失敗で旧DB維持、成功時もsystem.cfg/boot領域は不変。

破損・強制終了試験は承認済みの使い捨てイメージのみ。通常NHDへの障害注入は禁止。
ビルド・配備・ゲスト操作は専任検証セッションに集約し、[D1]〜[D3]の承認を別途取得する。

## 6. 進捗

- Hermes調査サブエージェントがターミナルと設定を読み取り専用で並行監査。
- PMがsingle-foreground契約、buffer redirect、DB open/execの主要指摘を現行ファイルと照合。
- 本計画を作成。契約凍結・コード実装・ゲスト試験は未実施。
- T1: Codexが終了コード0で設計報告を提出。ローカル報告は `/tmp/os32-v13-t1-design.txt`
  (一時成果物、仕様の正典ではない)。常駐結果表示UIを推奨。対話入力・完全捕捉は未解決。
- T1の追加論点: stdout/stderrの相互順序は別バッファから復元不能。満杯と出力欠落は保存長だけでは
  区別不能。stdio終了時flush、親redirectの保護、終了理由の識別をT0と合わせて確認する。
- T0: 起動コマンドは `claude`、正確なモデルIDは `claude-opus-5` と確認。調査は35ターン上限で停止したが、
  同一セッションを再開して最終報告を受領 (exit 0)。原報告は `/tmp/os32-v13-t0-design-final.txt`。
- PMレビュー: 戻り値からの原因断定、redirect消失後の長さ、pipeのメモリ負担、外部直列型の扱い等を差し戻し。
  常駐型の有限入出力案は方向性として採用するが、T2/T3の実装発注は未承認。
- 設定実装S1/S2はS0完了まで保留。T1も設計提出段階であり、受入・契約凍結・実装完了ではない。
- T0修正を同一claude-opus-5セッションへ発注。R1〜R5の修正と独立モデル試験票の提案に限定し、実装は禁止。
- PMが `make gshell` を実行し成功。静的メモリ配置とT1容量案を測定・記録。NHD未配備。
- T0修正版を受領 (exit 0、`/tmp/os32-v13-t0-revised.txt`)。R1〜R5の誤記・断定は撤回済みと確認。
  バッファ方式/実行時容量、監査対象の同一性、対話待機の安全条件は未解決。
- 統合から独立した [T4 セル/UTF-8/clipモデル](../../../archive/gui_v13/TASK_T4_MODEL.md) の作業票を発行。
  T4は `userland/libos32term/` に実装し受入済み。[PM判定](../../../archive/gui_v13_reviews/REVIEW_T4.md)を参照。
  gshell+CUI統合は未着手。
- CodexのTAB修正・L1明記・Cellホスト実寸確認を受領し、独立再レビューで受入可。
  PMが `build/sdk.mk` に `check-term-model` を追加し `check` の依存へ登録。
  修正後、新ターゲットを含む `make check` 全体（モデル44試験）、fmt、差分チェックが成功。
  manifest検査はCargo incrementalの `.bin` も未配備一覧へ列挙する(非エラー)。
- PMが[guestターゲットの限定リンク検査](../../../archive/gui_v13_reviews/CROSS_LINK_T4.md)を実施し成功。
  Grid::clipのi64除算ヘルパ解決とCellのターゲット実寸8B/align4Bを確認。ゲスト実行はなし。
- CodexのT5表示設計を受領。[T5表示票](../../../archive/gui_v13/TASK_T5_DISPLAY.md)に方向性と実装前ゲートを記録。
  独立表示アプリT5aを先行し、常駐接続T5bは後段へ分離する。実装未発注、CUI起動を含めない。
- T5aゲートの独立調査を受領し、[PM照合](../../../archive/gui_v13_reviews/REVIEW_T5_GATES.md)を記録。
  glyphビット順・JIS各バイト検査・utf8_prog.oの接続を確認。
  純粋描画アダプタを `userland/libos32term_render/` に先行実装する配置を決定。
- [T5R作業票](../../../archive/gui_v13/TASK_T5_RENDER.md)で文字分類・clip・失敗時副作用・人工glyph試験を確定し、Codexへ実装発注。
  T5Rは対象再実装後の独立レビューとPM再実行を通過し受入済み。
  check-term-renderをmake checkへ登録し全体成功。[受入記録](../../../archive/gui_v13_reviews/REVIEW_T5_RENDER.md)。
- [T5aアプリ票](../../../archive/gui_v13/TASK_T5A_APP.md)を発行しCodexへ新規アプリ内限定で実装発注。
  既存モデル/rendererの事前ハッシュは `/tmp/os32-t5a-source-baseline.json`。
  workspace/build/deploy登録とゲスト最終リンクは提出後PMが実施。配備・ゲスト操作は未承認。
