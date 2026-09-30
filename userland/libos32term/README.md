# libos32term (T4)

独立した no_std / edition 2021 クレート。外部依存、内部 alloc、unsafe、OS API は使わない。
既存 workspace、CUI、gshell、描画には接続しない。

## 実装前に定める境界動作

- UTF-8 は妥当な scalar prefix を最大3バイト保持する。不正バイトに遭遇したら、
  それまでの未完了 prefix を U+FFFD 1文字へ置換し、そのバイトを先頭として再評価する。
  単独で不正なバイトは各1個の U+FFFD。E0/ED/F0/F4 の第2バイト制限も直ちに適用する。
  例: E2 82 41 → U+FFFD A、E0 80 80 → U+FFFD 3個。
  最終化は残る prefix 全体を1個へ置換する。空の最終化を繰り返しても追加出力しない。
  NUL は通常の U+0000 セル。ANSI/VT は解釈しない。
- 幅は呼出側の `fn(char) -> Width` に委ねる。Single/Wide のみを扱い、
  結合文字、ゼロ幅、Unicode幅テーブルの選択は呼出側の責任。
- cols と保存行数は各1以上。1列で Wide を書くと `TooWide`。置換・分割しない。
  caller の余分な容量には触れず、必要容量不足・乗算overflowは初期化前にエラーにする。
- 行末は遅延折返し。最終列を書き終えたカーソルは x=cols。
  次の表示文字で折り返す。CRは同じ行のx=0、LFは次行のx=0なのでCRLFは1改行。
  BSは同じ行内の前文字先頭へ戻り、消さない。行頭BSは何もしない。
  TABは現在行の次の4列境界まで空白を書き、行末でクランプする。TAB自体では改行しない。
  空白数は `min(4 - x % 4, cols - x)`、幅関数に関係なくSingle。全角片側を上書きすると
  旧ペアの両側を解除する（相方がTAB範囲外でも空白になる）。
  x==colsのTABはセル・状態全体（limitを含む）を変えず成功し、次の通常文字で遅延折返しする。
  例: 5列の `A\t\t` は同じ行のx=5。これはANSI/VT互換全体を意味しない。
- 保存は先頭からの有限履歴。満杯でも自動破棄・循環上書きをしない。
  新行が必要な操作は `Full`、セルとカーソルは変えず、状態の limit に理由を残す。
  最終セルちょうどの成功と、次の操作の Full を区別する。
  CR/BSによる明示的な既存セル上書きは可能。成功した文字操作で limit を解除する（x==colsのTABを除く）。
  保持行範囲は常に0..used_rowsであり、viewport変更は保存内容を変えない。
- chunk入力が表示エラーになった場合、エラーを生じたデコード出力は内部固定2文字枠に保持する。
  consumed はその出力を生成したバイトを含む。呼出側は consumed 分だけ進め、
  再試行時は空chunkでも保留出力を先に処理できる。保留出力を黙って捨てない。
  最終化も同じ規約。finishは現在のprefixを確定する操作であり、その後の入力も可能。
  保留中は後続CR等も先に処理しない。Fullの復旧は呼出側が `model_mut().set_cursor(...)`
  等で明示的に書込位置を変えてから再試行する。TooWideの1列モデルには収容できないので
  同じモデルでの再試行は同じエラーになる。リサイズや保留破棄APIは提供しない。
- clipは符号付き64bitの半開画素矩形。逆転端点は不正、ゼロ面積は空。
  幾何計算のoverflowは明示エラー。読出セル矩形は全角ペアのため左右に
  最大1セル余分に取得してよいが、write矩形は要求clipと表示面の厳密な交差のまま。
  レンダラはこのwrite矩形を必ず守る。描画そのものは提供しない。

## APIと保持状態

- `utf8::Decoder`: `push(u8)` と `finish()` が固定配列 `Decoded([Option<char>; 2])` を返す。
- `model::Model::new(&mut [Cell], cols, rows, fn(char) -> Width)`: 保存行数を固定して初期化。
  `write_char` は1操作単位。`set_cursor` は保存済み行内の任意セル（全角継続を含む）へ移動する。
  指定可能な列は `0 <= x < cols` であり、遅延折返し位置x==colsは戻せない。
  `set_cursor` は位置指定であって完全状態復元ではなく、保持範囲やlimitも復元しない。
  範囲外移動は状態を変えない。直接変更できる可変セル参照は公開しない。
- `State`: `(x, y)`、保持行の半開範囲、最後の文字操作の容量エラーを返す。
  初期状態でも空の1行を保持する。`cells()` は未使用の空行を含む設定容量全体を返す。
- `viewport(top, height)`: 保存済み行だけを厳密に取得。範囲外はエラー。
  `tail(height)` は末尾の `min(height, used_rows)` 行を取得する。
  スクロールは呼出側がtopを変える読出操作。自動スクロール・循環履歴はない。
  dirtyフラグは持たず、同じviewportを何度でも再取得できる。
- `stream::Terminal::new(model)`: `feed(&[u8])` と `finish()` が `Report` を返す。
  Reportの `error` はその呼出しの結果。`state.limit` は最後の文字操作のエラーを記憶するため、
  空chunkやprefixだけの入力では解除しない。`pending()` で保留文字を観測できる。
- `clip::Grid::clip(Rect)`: `Ok(None)` は空交差。Gridのrowsは表示viewportの行数、
  返るセル行範囲はviewport先頭からの相対値。保存行との加算・対応は呼出側が行う。
  Grid自体にセル保存領域や物理画面は渡さない。

## 受入分類と試験

`#[test]` は44件（UTF-8 8、model 21、stream 8、clip 7）。ループ内ケースを件数に加算しない。

| 作業票の受入分類 | 対応試験ファイル・内容 |
| --- | --- |
| 1 UTF-8全分割・不正列・最終未完了・NUL | `tests/utf8.rs`, `tests/stream.rs`: 各fixtureの全partition、モデル・消費数・保留状態一致 |
| 2 全角・上書き・制御文字 | `tests/model.rs`: 右端折返し、左右片側と二重ペア上書き、BS/CR/LF/CRLF/TAB/連続改行 |
| 3 次元・容量・overflow・失敗状態 | `tests/model.rs`, `tests/stream.rs`: 最小/ゼロ次元、容量ちょうど/不足/余剰、Full/TooWideと原子性 |
| 4 ペアと番兵 | `operation_sequences_preserve_pairs_and_storage_sentinels`: 幅1〜5、各4096操作列で全角両方向の不変条件・番兵 |
| 5 clip | `tests/clip.rs`: 四辺、負原点、空/逆転矩形、整数端点、部分全角、非整列、画素単位oracle |
| 6 スクロール・viewport・再露出 | `tests/model.rs`: top往復、tail、範囲外/overflow、繰返し取得で保存内容と状態が不変 |

UTF-8では追加で全65536通りの2バイト列と4096通りの4バイト列をhost標準ライブラリの
置換結果と照合する。本体はcoreのみで、std/Vec/Stringは試験側だけで使用する。

## Cellのhost実測

`rustc 1.97.0-nightly (37d85e592 2026-04-28)`、target `x86_64-unknown-linux-gnu` で
`core::mem::size_of::<Cell>() == 8` バイト、`align_of::<Cell>() == 4` バイトを実測した。
`tests/model.rs` の `cell_host_layout_measurement` に出力と同host target向けの実測値assertを記録。
これは現在のコンパイラ・targetでの回帰確認値であり、Rust ABIの保証値でもguest実寸でもない。
コンパイラ更新時は再測定が必要。guest配置・クロスリンクは未検証。

## ホスト検証

リポジトリルート `~/os32` から実行する。

```sh
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline
cargo check --manifest-path userland/libos32term/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline
cargo fmt --manifest-path userland/libos32term/Cargo.toml -- --check
```

ホスト検査はゲストクロスビルド・ゲスト試験の代替ではない。
初回18サイクルとM1修正の実行済みRED/GREENおよび検証出力は `TDD_LOG.md` を参照。
追加回帰のテスト側コンパイルエラー1回と期待値修正1回も記録し、コンパイルエラーをREDに数えない。

make checkへの登録、既存make checkの実行、独立レビュー解消はPM側の残作業。
通常のmake checkは `Makefile:24` の `.env` 読取とクレート外生成を伴うため、今回の禁止条件で未実施。
配備・エミュレータ・他エージェント起動・commit/pushも実施していない。
本成果はT4の独立モデル実装とホスト検証であり、作業票全体の完了やCUI統合済みを意味しない。
