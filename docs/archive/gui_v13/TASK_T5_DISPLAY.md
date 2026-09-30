# T5 — 固定テキスト表示の段階分離

> 発行: PM (2026-09-09) / 状態: **完了記録 (2026-09-10)**

設計受領・方向性採用・実装未発注。
PM: Hermes。設計: Codex。親: [PLAN.md](../../tasks/gui/v13/PLAN.md)。
原報告: `/tmp/os32-v13-t5-design.txt`（一時ファイル）。

## 採用する範囲

T5aは独立GUI表示試験アプリで固定fixtureを表示する。T5bの常駐gshell接続は後段へ分離する。
両段階ともCUI子起動、FD捕捉、対話端末、KAPI追加を含めない。
配備・エミュレータ操作は別途承認と専任検証を必要とする。

## PMが照合した接点

- `userland/rust/libos32gui/src/draw.rs:529-570`: textは全角セル全体がclip内に収まる場合だけ描く。
  offscreen全角経路もないため、既存textへ渡すだけでは部分日本語clipを満たさない。
- `sdk/rust/os32api/src/kapi_generated.rs:107-108`: kcg_read_ank/kanjiの読出し口は既存。
- `userland/rust/libos32gui_stub/src/draw.rs:10-42`: fill_rect/hlineは既存。
  glyph読出しと厳密clip済み水平runを組み合わせるアダプタは新規実装となる。
- `lib/utf8.c:220-240`: ANK変換は全角英数を半角glyphへ変換する。glyph選択とセル幅判定を混同しない。

## 契約候補

- 8×16セル、Wideは2セル。幅変換とglyph選択を同一方針に基づく境界へ分離する。
  ASCII・半角カナ・円記号はSingle、全角英数はWide枠内にANKを中央配置する案。
  未収録文字・結合文字・U+FFFDはWide代替枠。C0/DEL（既存制御操作を除く）はSingle可視マーク。
  NUL/ESCを消去せずANSI解釈もしない。実装前に全分類の表と具体的マークを確定する。
- Paint、本文、Gridの交差をwrite許可範囲とする。背景も前景も許可範囲外へ書かない。
  読出しhaloはwriteを広げない。Continuationは先頭Wideのglyphから描く。
- viewport相対行と保存行の対応を一か所で計算し、topの二重加算を禁止する。
  保存行が足りない部分は背景だけ描く。描画時にモデルを変更しない。
- Paintごとに再露出を再描画し、dirtyセルの有無に依存しない。入力カーソルは設けない。
- Full/TooWideは本文外の状態欄に停止理由を表示し、無限再試行しない。
  consumed、pending、未消費末尾を区別し、消費済みを表示済みと呼ばない。
- fixture切替時は旧Terminalを破棄し、同じ所有領域からModel/Terminalを新規初期化。
  top、prefix、pending、limit、消費位置を持ち越さない。
- 初期保存容量案は40列×64行。大配列をスタックへ置かない。
  [限定クロスリンク](../gui_v13_reviews/CROSS_LINK_T4.md)でCellのターゲットsize8B/align4Bは確認済みだが、
  アプリ全体の最終リンク・BSS/ヒープ/スタック配置は未検証。

## 実装発注前のゲート

1. glyphパターンのANK/漢字ビット配置、読出し失敗・未収録時の扱いを定義と用例から確認する。
2. Unicode変換関数のアプリからのリンク経路と漢字表ready判定を確認する。無条件ready設定は禁止。
3. 新規アプリとホスト試験可能な描画アダプタの配置・責任範囲を確定する。
   workspace/build/deploy manifestの共有編集はPMが直列で行う。
4. GUI座標型への変換境界、Paint clipの寿命、glyph読出しを含むCPL3呼出しを独立レビューする。

## 受入候補（未実行）

- 記録描画先で全描画点がwrite内。日本語左右半分・上下1px・非整列clipを全描画の切抜きと照合。
- 文字分類表、TAB/CR/LF/BS、NUL/ESC、不正UTF-8、全角端のfixtureを期待セル・画素と照合。
- top往復と同一アプリ第二窓による遮蔽/再露出で画像復元。保存セル・状態・窓外番兵は非変更。
- 容量ちょうど/Full、1列TooWide、finish失敗、fixture切替後の状態残留なし。
- 最終アプリクロスリンクと配置確認。その後、承認済み配備でバイナリ同一性、
  PC98/PEGC/Cirrus表示と終了後desktop復元を検証する。

T5a受入はT5bやCUI実行統合の承認ではない。
