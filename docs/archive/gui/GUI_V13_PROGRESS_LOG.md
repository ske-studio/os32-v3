# GUI 文書の作業記録 — GUI_V13_PROGRESS_LOG.md

> 状態: 完了記録
> 2026-10-17 に現行票から抽出。以下は当時の記録であり、未確認事項の現在の扱いは元の票を参照。

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
- 統合から独立した [T4 セル/UTF-8/clipモデル](../gui_v13/TASK_T4_MODEL.md) の作業票を発行。
  T4は `userland/libos32term/` に実装し受入済み。[PM判定](../gui_v13_reviews/REVIEW_T4.md)を参照。
  gshell+CUI統合は未着手。
- CodexのTAB修正・L1明記・Cellホスト実寸確認を受領し、独立再レビューで受入可。
  PMが `build/sdk.mk` に `check-term-model` を追加し `check` の依存へ登録。
  修正後、新ターゲットを含む `make check` 全体（モデル44試験）、fmt、差分チェックが成功。
  manifest検査はCargo incrementalの `.bin` も未配備一覧へ列挙する(非エラー)。
- PMが[guestターゲットの限定リンク検査](../gui_v13_reviews/CROSS_LINK_T4.md)を実施し成功。
  Grid::clipのi64除算ヘルパ解決とCellのターゲット実寸8B/align4Bを確認。ゲスト実行はなし。
- CodexのT5表示設計を受領。[T5表示票](../gui_v13/TASK_T5_DISPLAY.md)に方向性と実装前ゲートを記録。
  独立表示アプリT5aを先行し、常駐接続T5bは後段へ分離する。実装未発注、CUI起動を含めない。
- T5aゲートの独立調査を受領し、[PM照合](../gui_v13_reviews/REVIEW_T5_GATES.md)を記録。
  glyphビット順・JIS各バイト検査・utf8_prog.oの接続を確認。
  純粋描画アダプタを `userland/libos32term_render/` に先行実装する配置を決定。
- [T5R作業票](../gui_v13/TASK_T5_RENDER.md)で文字分類・clip・失敗時副作用・人工glyph試験を確定し、Codexへ実装発注。
  T5Rは対象再実装後の独立レビューとPM再実行を通過し受入済み。
  check-term-renderをmake checkへ登録し全体成功。[受入記録](../gui_v13_reviews/REVIEW_T5_RENDER.md)。
- [T5aアプリ票](../gui_v13/TASK_T5A_APP.md)を発行しCodexへ新規アプリ内限定で実装発注。
  既存モデル/rendererの事前ハッシュは `/tmp/os32-t5a-source-baseline.json`。
  workspace/build/deploy登録とゲスト最終リンクは提出後PMが実施。配備・ゲスト操作は未承認。
