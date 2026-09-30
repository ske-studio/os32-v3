# libos32term_render — T5R ホスト描画アダプタ

独立 workspace / edition 2021 / `no_std` / `forbid(unsafe_code)`。
依存は `../libos32term` のみ。本体に alloc、OS API、自己参照構造はない。
テストの Vec と Python ログ採取器はホスト専用。

## 具体APIと所有権

`render(&Model<'_>, View, &mut impl Glyphs, &mut impl Sink) -> Result<Stats, Error>`。
Model は既存 `libos32term::model::Model` を不変借用する。構築時の幅関数には
公開 `width: fn(char) -> libos32term::model::Width` を渡す。

`View` は `top: usize`（保存行）、`height: usize`（表示行数）、
`origin: (i64, i64)`（本文の画素原点）、`clip: libos32term::clip::Rect`。
Rect は半開区間で、要求clipはi64のまま渡せる。

既存Modelに列数getterはないため `viewport(0, 1)?.len()` から列数を取得する。
Modelは最低1行を保持し、構築時に非ゼロ次元・容量・積overflowを検査する。
重複した列数・容量引数を作らず、公開APIでは不正容量のModelを渡せない。
Wide/ContinuationのペアはModelが保証し、全保存セルの再検査はしない。

既存 `Grid::clip` から write と halo列・表示ローカル行を取得し、保存行の不足を
差し引いた `viewport(top + row_start, row_count)` を借用する。topを加えるのは
この保存スライス取得時だけで、画素座標は表示ローカル行から計算する。
モデル、カーソル、limit、保存領域は変更しない。参照を保存するラッパも不要。

`Glyphs` は次の固定配列を値として返す。失敗しない契約で、OS失敗は後続ゲスト境界で定義する。

- `jis(char) -> Option<u16>`: 注入するUnicode→JIS変換。Noneは未対応。
- `ank(u8) -> [u8; 16]`: 各バイトが1行、MSBが左。
- `kanji(u16) -> [u8; 32]`: row*2が左、row*2+1が右、両方MSBが左。

`Sink::run(x: i32, y: i32, length: i32, foreground: bool)` は失敗しない。
falseは背景、trueは前景。実際の色・Paint権限・GUIのi16変換は呼出側が担う。
先にwriteの全背景を1行1runで塗り、続いて字形内の連続前景を水平runで出す。
字形間のrun結合はしない。背景でWide右半分を消さない。
`Stats { runs: u64, glyph_reads: u64 }` は実sink呼出数と実bitmap読出数を返す。
JIS変換はglyph_readsに含めない。同じ供給内容なら再描画の画素は同一。

## 文字とマーク

セル8×16、Wideは16×16。U+0000..007F、U+00A5、U+FF61..FF9FだけSingle、
他のUnicode scalarはWide。ASCII U+0020..007Eは同値ANK、円記号は0x5C、
半角カナは `scalar - 0xFF61 + 0xA1`。
全角英数U+FF01..FF5Eは `scalar - 0xFF01 + 0x21` のANKをWideのx=4..11へ置く。
公開 `ank(char) -> Option<u8>` はこの写像であり、幅判定とは別。

保存されたC0/DELは制御マーク。CR/LF/BS/TABはModelが操作として消費するため、
公開Model経由ではそれらの保存セルを作れない。制御コード識別の凡例は本文外の呼出側UI。
U+FFFDは変換器もbitmap供給も呼ばず常に代替マーク。
その他はJIS各バイトが独立に0x21..0x7Eである場合だけ漢字を読む。
未対応・無効JISはbitmapを読まずWide代替マーク。結合処理は行わない。
結合文字も通常のWide文字として変換器を利用し、変換不能なら代替マーク。
有効glyphの全ゼロは空白として尊重し、欠落と判断しない。

マークは幅w（制御8、代替16）、高さ16。
前景条件は `x==0 || x==w-1 || y==0 || y==15 || x==floor(y*(w-1)/15)`。
外周1pxと左上→右下の対角線。人工glyphと独立画素oracleでこの形を固定する。

## 検査と安全側の解釈

- height=0はInvalidDimensions。topは0..=retained_end。endを超えるとInvalidTop。
  top==endや表示高が保存行を越える部分は背景だけ。top+heightのusize overflowはOverflow。
- 次元のi64変換、8/16との乗算、本文端点の加算をchecked演算で検査する。
  空交差でも本文全体の画素座標がi32に収まることを要求し、逸脱はCoordinateRange。
  半開終端i32::MAX+1は許容する。要求clip自体はi32に縮めない。
- 不正RectはInvalidClip。writeの水平長がi32::MAXを超える場合はCoordinateRange。
  大きなrunの暗黙分割・黙った打切りはしない。
- 容量はModelの保証を利用。viewport失敗は防御的なInvalidCapacityとして返すが、
  現行Modelの公開APIと上記検査の下では到達不能。不正容量はModel::newの明示エラーで試験する。
- 異なる幅コールバックで作ったモデルは、可視行とhalo列のSingle/Wide分類を事前検査し、
  不一致はWidthMismatch。非表示の保存領域までは走査しない。空交差では幅検査も不要。
  この点は必ず `width` をModel構築に渡すという入力契約で担保する。
- すべての返却可能な入力・計算エラーは、JIS変換、bitmap読出、sink出力より前に返す。
  callback自身のpanicやOS失敗はResultに変換しない。
- 検査後は本文境界・Modelのペア整合性により添字と座標演算が有界。
  write幅<=2^31-1、高さ<=2^32。前景run数<=write面積、背景run数=高さなので、
  合計<=2^63。glyph読出数も面積以下。u64計数はoverflowしない。

clip前の全行・全セル走査はない。計算量は可視行×halo付き可視列の検査・読出、
write高の背景、交差する字形内の可視画素に比例する。bitmap生成は最大16×16の固定作業。
出力行数に固定上限はなく、巨大表示高でも小さいclipなら非表示行を走査しない。
これはアルゴリズム上の性質であり、ゲスト性能の実測結果ではない。

## 検証と残る受入条件

rootから実行するコマンドは作業票の3本。過去の証跡はTDD_LOG.md、
今回の除去・stubからの再実装証跡は[REWORK_LOG.md](REWORK_LOG.md)へ保存。
39 tests（unit 7、integration 32）、doc testsは0件。
既存integration 21件は無変更で保持し、幅/写像は全Unicode scalarを走査する。
人工ANKは各行0x74、人工漢字は行ごとに左右が異なる固定パターン。
実ROMから取得したglyphではない。独立oracleは本体のclip/run/bitmapデコードを使わず
全体画素を生成して切抜きと比較し、番兵・全runのwrite内拘束も検査する。

過去01〜06の13テストにはassertion RED→実装→GREENの実行証跡がある。
07〜08の追加8テストは初回GREENであり、そのうち実質6件は未RED本体経路の唯一の証拠だった。
過去ログの「追加回帰」という分類だけでは手順不足を説明できない。
ユーザーの差し戻し決定を受け、render/draw/供給dispatchを実際に除去し、
今回のテスト先行で座標受理・保存行選択・halo・引数受渡し・部分clipを再構築した。
新規17試験は今回assertion RED→最小再実装→GREEN、新規1試験は幅検査範囲の初回GREEN回帰。
過去の未履行を解消した証跡とは主張しない。TDD_LOG.mdは書き換えていない。
型による不変性・固定行上限なし・依存Model検査等は回帰として区別する。

共有make check登録、PM再実行と独立レビューは未実施。共有ファイルは編集しない。
配備・エミュレータ・ゲスト表示・性能・クロスリンク確認は本作業で実施していない。
