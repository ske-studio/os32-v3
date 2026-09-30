# T4 独立レビューとPM判定

> 発行: PM (2026-09-09) / 状態: **完了記録 (2026-09-10)**

T4受入済み（独立ホストモデルの範囲）。実装 Codex、独立レビュー claude-opus-5。
親: [TASK_T4_MODEL.md](../gui_v13/TASK_T4_MODEL.md)、[PLAN.md](../../tasks/gui/v13/PLAN.md)。
原レビュー: `/tmp/os32-v13-t4-review.txt` (一時ファイル)。

## 検証済み

修正後のPM再実行でmake check全体（host試験44件とhost lib checkを含む）、fmt check、git diff --checkが成功。
check-term-modelはbuild/sdk.mkでmake checkへ登録済み。targetのGit無視も確認済み。
独立再レビュー `/tmp/os32-v13-t4-rereview.txt` はM1/L1/M3限定の受入可、新規ブロッカーなし。
ゲストクロスリンク・配備は未実施。以下の初回指摘の行番号は修正前を指す。

## PMがコードと照合した判定

### M1: TABの境界契約 — 修正・再レビュー済み

`userland/libos32term/src/model.rs:110-120` は現在列から空白個数を計算し、
行を跨いでもその個数を使う。5列の `A\t\t` は次行の列3となり、
票の「4セル境界まで空白」と整合しない。

修正契約: TABは現在行の次の4列境界まで空白を書き、行末でクランプする。
TAB自体では改行しない。遅延折返し位置(x == cols)では状態・セルを変えず成功する。
次の通常文字は既存の遅延折返し規則に従う。途中の全角ペア解除は既存不変条件を維持する。
1列、5列、最終保存行、既存次行の非変更、x == cols、全角片側を含めてRED/GREENを要求。
READMEと既存の旧TAB期待値もこの契約に合わせる。ANSI/VT互換全体を意味しない。

### H1/H2: 有限履歴と保留出力 — 統合ゲート、現在の票違反とはしない

`model.rs:95-106,133-150`、`stream.rs:31-68` とREADMEの境界動作を照合。
Full時は明示的な位置変更で再試行できるため「永久に復旧不能」は過剰な表現。
TooWideが保留中の同じ1列モデルでは後続入力を消費しないが、明示エラーと保留を返す設計である。
有限結果表示のT4へ無条件にscroll_up/clear/discard_pendingを追加しない。

統合前には、上限到達で表示を止める通知、TooWide時の可視化方針、次の結果へのモデル再初期化、
未表示データの扱いを決める。履歴破棄や保留破棄を採用するなら別の明示的契約と試験を発行する。
原レビューの「継続セルから復元不能」は先頭Wideセルにscalarが残る点を考慮していない。
「全角ペアが行境界を跨る」試験要求も、現行の跨がない不変条件と区別する。

### その他の分類

- M2: C0/ESCの生セル保存はANSI非解釈の範囲。統合時に未対応制御文字の表示方針を決める。
  CSI除去を勝手に必須化しない。
- M3: Cellのhost実寸はsize 8B / align 4B。tests/model.rsのhost限定試験で確認。
  [MEMORY_BUDGET.md](../../tasks/gui/v13/MEMORY_BUDGET.md)の案と一致するが、guest配置やRust ABIの保証ではない。
- M4: i64演算のguestリンクは未検証。ヘルパ未解決が起こるとは未確認なので断定しない。
  クロスリンクを統合前ゲートとし、解決目的だけでi32化しない。
- L1: x == colsはset_cursorへ戻せない。READMEとAPIコメントに位置指定と完全状態復元の違いを明記済み。
- L2: pendingはTerminalの非公開フィールドで、公開Decodedから任意注入できない。
  現在到達不能の防御強化案であり、データ損失バグとして扱わない。
- L3: viewport相対座標と保存行の対応は統合時に検査する。
- L4: clear_pairの境界は全角ペア不変条件で成立。saturatingで不変条件違反を隠さない。
- L5: targetのGit無視を確認済み。

## 残作業

T4の必須修正・検証は完了。gshell+CUI統合の承認は別である。
統合前にはH1/H2/M2/M4とguestセル配置、viewport座標対応、遅延折返し時のカーソル描画を決める。
非阻害の試験補強候補: x < colsのTABによるlimit解除の直接試験。
make checkはcargoとx86_64-unknown-linux-gnu host targetを必要とする。
Cellのhost固定値はコンパイラ変更時に再測定が必要。
