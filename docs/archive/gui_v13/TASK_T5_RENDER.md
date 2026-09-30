# T5R — 純粋描画アダプタ

> 発行: PM (2026-09-09) / 状態: **受入完了 (2026-09-09)**

担当Codex、独立レビューclaude-opus-5、PM Hermes。
親: [TASK_T5_DISPLAY.md](TASK_T5_DISPLAY.md)、[ゲート判定](../gui_v13_reviews/REVIEW_T5_GATES.md)。
本票はホストアダプタのみ。ゲストアプリ・CUI・gshell・配備を含めない。

## 配置と境界

新規 `userland/libos32term_render/`、独立workspace、edition2021、no_std。
唯一の依存は `../libos32term`。既存T4クレートは変更しない。
内部alloc/unsafe/OS APIなし。glyph供給と水平run出力を呼出側から注入する。
ゲストのC変換・KCGラッパは本票で実装しない。

## 確定する表示契約

- セル8×16、Wide枠16×16。幅分類は純粋関数として公開しT4の幅コールバックに使用可能にする。
- ASCII U+0020..007E、半角カナU+FF61..FF9F、円記号U+00A5はSingle。
  C0 U+0000..001FとDEL U+007FもSingle。それ以外はWide。
  CR/LF/BS/TABの操作はT4で処理済み。保存セル中のC0/DELは可視マークとして扱う。
- ASCII/半角カナ/円記号のANK変換は上記範囲の算術写像。
  全角英数U+FF01..FF5EはANKをWide枠の中央に置く。幅を縮めない。
- その他は注入されたUnicode→JIS変換結果を使う。JISの上位・下位をそれぞれ0x21..0x7Eで検査。
  無効/未対応ではglyph供給を呼ばず、Wide代替枠を表示。
  U+FFFDは常に代替枠。結合処理はせず、変換不能文字は代替枠とする。
- 制御マークは8×16内の外周1px枠＋対角線。Wide代替マークは16×16の外周1px枠＋対角線。
  対角線は幅wに対し各行yの x=floor(y*(w-1)/15) とする。形状をREADMEと試験へ固定する。
  各制御コードの識別は呼出側の本文外凡例で行う（本票には状態欄UIなし）。
- ANKは16B、漢字は32B。漢字のrow*2が左、row*2+1が右、各バイトのMSBが左。
  範囲内glyphの全ゼロは空白として尊重し、欠落と推測しない。

## 描画契約

- 保存モデルを不変借用し、top/表示高/本文原点/要求clipを引数に取る。
  T4のGrid/clipとviewportを利用し、topは一度だけ加算する。
- 背景を含む全出力は要求clipと表示本文の厳密な交差内。
  保存行が足りない表示部分は背景だけ。空交差は出力なし。
- 読出haloからWide先頭を取得する。Continuationを別文字として描かない。
  背景を先に塗り、前景runを後に出すなど、隣セルの背景で全角の半分を消さない。
- 座標はi64で検査し、出力sinkはi32のx/y/長さを受ける。
  GUIのi16境界は後続アプリの責任。i32逸脱、算術overflow、無効次元/top/容量を明示エラー。
- 検査可能な入力エラーはglyph読出し・描画の前に検出し、出力なしとする。
  sinkは失敗しない契約とする。glyph供給は固定配列を返し、OS失敗の扱いはゲスト側で別定義。
- 全行を書き出す固定上限で描画を黙って打ち切らない。出力run数・glyph読出数を計数して返す。
  字形内の連続する同色前景画素は水平runへまとめる。
- 描画でモデル・カーソル・limitを変えない。毎回同じ保存状態から再描画できる。

## TDDと受入

小さな挙動ごとにassertion RED→最小実装→GREENを実行しTDD_LOG.mdにコマンドと結果を残す。
試験用glyphは人工固定パターンであり、実ROM取得結果と主張しない。
独立した画素oracleで全描画画像の切抜きと比較する（期待値を本体のclip/run関数から生成しない）。

必須分類:
1. 文字幅/ANK写像/全角英数中央配置/制御と代替マーク/無効JIS各バイト/空glyph。
2. 日本語左右半分・上下1px・非整列clip・負原点・空交差。
3. top往復、保存行不足、最終行、halo全角、モデル状態非変更、再露出の同一画素。
4. i32/i64境界、overflow、不正入力でglyph/sink呼出しゼロ。
5. 番兵画素非変更、runの全画素がwrite内、連続run化と呼出数の一致。

rootから実行:
`cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline`
`cargo check --manifest-path userland/libos32term_render/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline`
`cargo fmt --manifest-path userland/libos32term_render/Cargo.toml -- --check`

PM再実行・独立レビュー・共有ゲート登録後に受入。ゲスト性能・表示・リンクは後段。
禁止: 既存クレート/共有ファイル編集、外部依存、commit/push、配備、エミュレータ、他エージェント起動。
