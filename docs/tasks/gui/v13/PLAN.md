# GUI v1.3 — 着手計画

> 発行: PM (2026-09-09) / 状態: **受入完了 (2026-09-14)**

2026-09-10 に監査 (`docs/archive/gui_v13/AUDIT_2026-09-10.md`) を経て再編。
ユーザー決裁: T5b (常駐表示パネル) は撤去、T6a (監査済み有限実行) は破棄、設定 F1/F2 は保持、
**端末の形は B = 契約 T2a (GUI アプリ 4 本) を v1.3 の最初に実装し、端末は外部アプリ**。
最初の票は K5 (4 アプリ同時実行) (アーカイブ: `docs/archive/gui_v13/TASK_K5_multiapp.md`)。段ごとの最終判定は §6 を参照する。
**2026-09-11**: K5a の設計を決裁 (D9 の 8 分岐 + D11、票末尾の「決裁」)。KAPI **v44** を予約。
K5b-K (カーネル) (アーカイブ: `docs/archive/gui_v13/TASK_K5B_kernel.md`) と K5b-W (gshell) (アーカイブ: `docs/archive/gui_v13/TASK_K5B_gshell.md`) は実装済み・**実機受入は G9 以外すべて合格**
(2026-09-12、G1〜G8 / G10。G9 は 2 本以上を同時 ready にする観測手段が無く未実施)。受入中に見つけた
K7 (8MB で shlib .data 4 ページの勘定漏れ、`8cc13d8`) と W-3 (背面窓の枠線が前面のクライアント面に落ちる、`36ccf15`) は修正・実機確認済み。
**K6-RAM (物理 RAM 上限の撤廃) (アーカイブ: `docs/archive/gui_v13/TASK_K6_ram_ceiling.md`)** は `f6ec520` で着地、M1〜M5 合格 (32MB / 128MB / 15MB / 8MB)。
K6 の決裁事項 4 件は 2026-09-12 にユーザー決裁 (2.8GB 見送り / `mem` に実 RAM 合計を並記 / 0594h 無しは何もしない / Cirrus 窓の移動は Cirrus レーン再開時)。G9 はホスト模型を受入とし K5b は**完了**。feat/gui → main のマージも同日承認。**K6C console の差し込み口は完了** (K 側 `d381000` KAPI v46、端末アプリ `abef34f`、受入 C1〜C4 / A1〜A5、2026-09-12)。**K7 入力統合も完了** (設計は独立レビュー通過、K `c6a775d` KAPI v47 / A / W / W2、受入 I1〜I5、2026-09-12。GUI 中の CUI プログラムは `kbd_getchar` で park され、端末アプリの打鍵で起きる)。**T7「CUI コマンドを端末で流す」も完了** (K `7a6b124` EXIT レコード、A `ce0406b` プロンプト / 起動 / 接続モード、受入 T1〜T5、2026-09-12)。**T8 full-screen GFX 復帰は完了** (画面の所有者 + 宣言ビット `gfx` / `cui`、gshell の全画面モード、ポーリング型の協調 yield (tick に 1 回)、受入 F1〜F8 合格、2026-09-12)。PEGC 構成も確認済み (K6-3 `1a559a0` で K6 の probe 回帰を修正)。8MB は端末 + 子が NOMEM で仕様どおり。後続の **T9 shell script** (常駐シェルのソースを CPL=3 の `sh.bin` としてもビルドし端末から起動、外部プログラムは端末経由で起動して `sys_yield` で待つ) は設計 `TASK_T9_sh.md` 第 6 版が Codex の独立レビュー (3 往復) を通過 (2026-09-12)、K / ビルド系 → S / W / A を発注。その後の T9・設定も受入済み (§6)。
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

試験項目の棚卸しと `stat` の追加は 2026-09-14 に着地済み (§6 の ROADMAP)。

旧 T5a 未受入・T5b/T6a の次ゲートという記述は、決裁 B と受入完了により失効。
現在の段ごとの判定は §6 を参照する。

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
判定と次のゲートは `docs/archive/gui_v13_reviews/REVIEW_T0_T1.md` を参照。
メモリのホスト測定は `docs/archive/gui_v13/MEMORY_BUDGET.md`。ゲスト実行時の空きは未測定。
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

v1.3 は 2026-09-14 受入完了。最終状態と後続段は [ROADMAP の v1.3](../../../ROADMAP.md#v13--ターミナル統合とcui抽象化--完了-2026-09-14) を参照。

| 段 | 判定・証拠 |
|---|---|
| T4 / T5R | 受入済み: T4 レビュー (アーカイブ: `docs/archive/gui_v13_reviews/REVIEW_T4.md`)、T5R レビュー (アーカイブ: `docs/archive/gui_v13_reviews/REVIEW_T5_RENDER.md`) |
| T5a | 受入済み (2026-09-12): 票 (アーカイブ: `docs/archive/gui_v13/TASK_T5A_APP.md`) |
| T5b / T6a | 決裁 B で撤去 / 破棄 (T5b の記録 (アーカイブ: `docs/archive/gui_v13/TASK_T5B_FINITE.md`))。未完了の実装待ちとして扱わない |
| K5 / K6-RAM / K6C / K7 / T7 / T8 | 受入済み。K5 の G9 はホスト模型で受入 (冒頭の証拠リンク) |
| T9 | 受入済み (2026-09-13): 票 (アーカイブ: `docs/archive/gui_v13/TASK_T9_sh.md`) |

旧設計からの調査・作業経緯は 完了記録 (アーカイブ: `docs/archive/gui/GUI_V13_PROGRESS_LOG.md`)。
試験棚卸し・stat 等の小物は ROADMAP の 2026-09-14 着地記録で閉鎖済み。
後続の保留事項は同節の S6-P と U6 決裁先へ引き継ぐ。v3 の外部端末に依る未実施は
[DEFERRED_TESTS の H-7](../../DEFERRED_TESTS.md) に残る。
