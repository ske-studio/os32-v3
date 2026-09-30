# T5R 今回のテスト先行再実装（2026-09-08）

## 除去前の範囲決定

ユーザーはTDD逸脱を容認せず再実装を決定した。本ログは今回の証跡であり、
TDD_LOG.mdの過去の未履行・初回GREENという事実を取り消さない。
既存21試験は仕様／回帰として無変更で保持する。既存実装の単発mutationと復元は行わない。

除去するもの:
- `render` 本体全体（座標受理、Grid接続、保存行部分不足、top、halo選択、
  glyph引数受渡しを含む呼出し経路）。公開シグネチャを残し無出力Ok stubにする。
- `draw` 全体（部分clip内での画素走査とrun生成）。無出力stubから再構築する。
- glyph供給のdispatch。無読出しの固定ゼロbitmap stubから再構築する。

過去RED済みで維持するもの:
- 公開幅分類／ANK算術写像、型／定数／trait。
- ANKの左寄せ／Wide中央配置、漢字BEデコード、制御／代替マークの画素計算。
  これらは副作用のないhelperに抽出して保持する（供給dispatchは保持しない）。
- 既存13試験が覆う拒否検査・背景・連続runの仕様は保持するが、render/draw内の
  実装は今回除去範囲に含め、再実装時にもREDを実行する。

予定する小段階（各実装の前にassertion失敗を実行）:
1. 無出力render → 事前検査・厳密背景・i32受理境界。
2. 空の保存行選択 → 可視行と保存行の交差、top一回加算・部分不足。
3. 空のセル選択 → haloからのWide head、Continuation除外、交差glyph限定。
4. ゼロbitmap供給 → ANK引数、Unicode→JIS→漢字引数、禁止読出し回避。
5. 無出力draw → 部分clip（左右半分、上下1px、非整列、負原点）と連続run。

行別字形のtop往復は既存最終形で検証する。型によるモデル不変性、固定行上限なし、
依存Modelの不正次元拒否、初回GREENだった追加試験は回帰と明記し、人為的な故障は作らない。
幅不一致はREADMEどおり可視行／haloのみを検査する。
外部依存・unsafe・本体alloc追加なし。host test/check/fmtのみ、ゲスト等は禁止範囲。

以下の採取器は各時点のソース／試験のSHA256、完全なstdout/stderr、終了コードを追記する。

## R1 RED 無出力stub・既存全21試験（境界assertionを含む）

```text
c05b5e7053e70a7774df6b28346b30f9248c81e9f3e9a315cb821426c3388fe8  userland/libos32term_render/src/lib.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: unused imports: `Cell`, `ClipError`, and `Grid`
  --> src/lib.rs:28:12
   |
28 |     clip::{ClipError, Grid},
   |            ^^^^^^^^^  ^^^^
29 |     model::{Cell, Model},
   |             ^^^^
   |
   = note: `#[warn(unused_imports)]` (part of `#[warn(unused)]`) on by default

warning: function `ank_bits` is never used
  --> src/lib.rs:79:4
   |
79 | fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
   |    ^^^^^^^^
   |
   = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `kanji_bits` is never used
  --> src/lib.rs:86:4
   |
86 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
   |    ^^^^^^^^^^

warning: function `mark_bits` is never used
  --> src/lib.rs:93:4
   |
93 | fn mark_bits(width: i64) -> [u16; 16] {
   |    ^^^^^^^^^

warning: `libos32term_render` (lib) generated 4 warnings (run `cargo fix --lib -p libos32term_render` to apply 1 suggestion)
warning: `libos32term_render` (lib test) generated 4 warnings (4 duplicates)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.71s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 21 tests
test ascii_pixels_runs_counts ... FAILED
test background_and_empty ... FAILED
test forwarded_glyph_arguments_and_forbidden_lookups ... FAILED
test control_marks ... FAILED
test fullwidth_centered ... FAILED
test i32_valid_edges_and_huge_clipped_height ... FAILED
test invalid_geometry_has_no_callbacks ... FAILED
test invalid_jis_bytes ... FAILED
test kana_yen_and_ascii_ank_frames ... FAILED
test japanese_halves_and_pixel_clips ... FAILED
test kanji_layout ... FAILED
test model_rejects_invalid_dimensions_and_capacity ... ok
test negative_origin_top_roundtrip_and_reexposure ... FAILED
test no_fixed_row_cutoff ... FAILED
test only_visible_glyph_read_with_halo ... FAILED
test replacement_ignores_mapping ... FAILED
test replacement_marks ... FAILED
test width_mismatch_preflight ... FAILED
test zero_glyph_is_blank ... FAILED
test ank_mapping ... ok
test widths ... ok

failures:

---- ascii_pixels_runs_counts stdout ----

thread 'ascii_pixels_runs_counts' (160) panicked at tests/render.rs:301:13:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 2
 right: 0
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- background_and_empty stdout ----

thread 'background_and_empty' (161) panicked at tests/render.rs:185:5:
assertion `left == right` failed
  left: []
 right: [(-2, -4, 9, false), (-2, -3, 9, false), (-2, -2, 9, false), (-2, -1, 9, false), (-2, 0, 9, false), (-2, 1, 9, false), (-2, 2, 9, false), (-2, 3, 9, false), (-2, 4, 9, false), (-2, 5, 9, false), (-2, 6, 9, false), (-2, 7, 9, false), (-2, 8, 9, false), (-2, 9, 9, false), (-2, 10, 9, false), (-2, 11, 9, false), (-2, 12, 9, false), (-2, 13, 9, false), (-2, 14, 9, false), (-2, 15, 9, false), (-2, 16, 9, false), (-2, 17, 9, false), (-2, 18, 9, false), (-2, 19, 9, false), (-2, 20, 9, false), (-2, 21, 9, false), (-2, 22, 9, false), (-2, 23, 9, false), (-2, 24, 9, false), (-2, 25, 9, false), (-2, 26, 9, false), (-2, 27, 9, false), (-2, 28, 9, false), (-2, 29, 9, false), (-2, 30, 9, false), (-2, 31, 9, false), (-2, 32, 9, false), (-2, 33, 9, false), (-2, 34, 9, false)]

---- forwarded_glyph_arguments_and_forbidden_lookups stdout ----

thread 'forwarded_glyph_arguments_and_forbidden_lookups' (163) panicked at tests/render.rs:657:9:
assertion `left == right` failed
  left: 0
 right: 1

---- control_marks stdout ----

thread 'control_marks' (162) panicked at tests/render.rs:301:13:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 2
 right: 1

---- fullwidth_centered stdout ----

thread 'fullwidth_centered' (164) panicked at tests/render.rs:301:13:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 2
 right: 0

---- i32_valid_edges_and_huge_clipped_height stdout ----

thread 'i32_valid_edges_and_huge_clipped_height' (165) panicked at tests/render.rs:519:9:
assertion `left == right` failed
  left: Ok(Stats { runs: 0, glyph_reads: 0 })
 right: Ok(Stats { runs: 16, glyph_reads: 0 })

---- invalid_geometry_has_no_callbacks stdout ----

thread 'invalid_geometry_has_no_callbacks' (166) panicked at tests/render.rs:169:5:
View { top: 0, height: 0, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }: expected InvalidDimensions, got Ok(Stats { runs: 0, glyph_reads: 0 })
View { top: 2, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }: expected InvalidTop, got Ok(Stats { runs: 0, glyph_reads: 0 })
View { top: 1, height: 18446744073709551615, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }: expected Overflow, got Ok(Stats { runs: 0, glyph_reads: 0 })
View { top: 0, height: 18446744073709551615, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }: expected Overflow, got Ok(Stats { runs: 0, glyph_reads: 0 })
View { top: 0, height: 1, origin: (9223372036854775807, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }: expected Overflow, got Ok(Stats { runs: 0, glyph_reads: 0 })
View { top: 0, height: 1, origin: (0, 9223372036854775807), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }: expected Overflow, got Ok(Stats { runs: 0, glyph_reads: 0 })
View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 2, y0: 0, x1: 1, y1: 1 } }: expected InvalidClip, got Ok(Stats { runs: 0, glyph_reads: 0 })
View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 2, x1: 1, y1: 1 } }: expected InvalidClip, got Ok(Stats { runs: 0, glyph_reads: 0 })
View { top: 0, height: 1, origin: (-9223372036854775808, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }: expected CoordinateRange, got Ok(Stats { runs: 0, glyph_reads: 0 })
View { top: 0, height: 1, origin: (2147483647, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }: expected CoordinateRange, got Ok(Stats { runs: 0, glyph_reads: 0 })

---- invalid_jis_bytes stdout ----

thread 'invalid_jis_bytes' (167) panicked at tests/render.rs:301:13:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 2
 right: 1

---- kana_yen_and_ascii_ank_frames stdout ----

thread 'kana_yen_and_ascii_ank_frames' (169) panicked at tests/render.rs:301:13:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 2
 right: 0

---- japanese_halves_and_pixel_clips stdout ----

thread 'japanese_halves_and_pixel_clips' (168) panicked at tests/render.rs:301:13:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 3, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 48, y1: 48 } }
  left: 2
 right: 1

---- kanji_layout stdout ----

thread 'kanji_layout' (170) panicked at tests/render.rs:301:13:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 2
 right: 1

---- negative_origin_top_roundtrip_and_reexposure stdout ----

thread 'negative_origin_top_roundtrip_and_reexposure' (172) panicked at tests/render.rs:301:13:
assertion `left == right` failed: pixel (-7,-9), View { top: 0, height: 3, origin: (-7, -9), clip: Rect { x0: -7, y0: -9, x1: 41, y1: 39 } }
  left: 2
 right: 1

---- no_fixed_row_cutoff stdout ----

thread 'no_fixed_row_cutoff' (173) panicked at tests/render.rs:675:5:
assertion `left == right` failed
  left: Stats { runs: 0, glyph_reads: 0 }
 right: Stats { runs: 49200, glyph_reads: 1025 }

---- only_visible_glyph_read_with_halo stdout ----

thread 'only_visible_glyph_read_with_halo' (174) panicked at tests/render.rs:565:9:
assertion `left == right` failed
  left: 0
 right: 1

---- replacement_ignores_mapping stdout ----

thread 'replacement_ignores_mapping' (175) panicked at tests/render.rs:301:13:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 2
 right: 1

---- replacement_marks stdout ----

thread 'replacement_marks' (176) panicked at tests/render.rs:301:13:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 2
 right: 1

---- width_mismatch_preflight stdout ----

thread 'width_mismatch_preflight' (177) panicked at tests/render.rs:339:5:
assertion `left == right` failed
  left: Ok(Stats { runs: 0, glyph_reads: 0 })
 right: Err(WidthMismatch)

---- zero_glyph_is_blank stdout ----

thread 'zero_glyph_is_blank' (179) panicked at tests/render.rs:301:13:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 2
 right: 0


failures:
    ascii_pixels_runs_counts
    background_and_empty
    control_marks
    forwarded_glyph_arguments_and_forbidden_lookups
    fullwidth_centered
    i32_valid_edges_and_huge_clipped_height
    invalid_geometry_has_no_callbacks
    invalid_jis_bytes
    japanese_halves_and_pixel_clips
    kana_yen_and_ascii_ank_frames
    kanji_layout
    negative_origin_top_roundtrip_and_reexposure
    no_fixed_row_cutoff
    only_visible_glyph_read_with_halo
    replacement_ignores_mapping
    replacement_marks
    width_mismatch_preflight
    zero_glyph_is_blank

test result: FAILED. 3 passed; 18 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

error: test failed, to rerun pass `--test render`

exit=101
```

## R1b RED i32上下限を別assertionで実行

```text
c05b5e7053e70a7774df6b28346b30f9248c81e9f3e9a315cb821426c3388fe8  userland/libos32term_render/src/lib.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test rework
warning: unused imports: `Cell`, `ClipError`, and `Grid`
  --> src/lib.rs:28:12
   |
28 |     clip::{ClipError, Grid},
   |            ^^^^^^^^^  ^^^^
29 |     model::{Cell, Model},
   |             ^^^^
   |
   = note: `#[warn(unused_imports)]` (part of `#[warn(unused)]`) on by default

warning: function `ank_bits` is never used
  --> src/lib.rs:79:4
   |
79 | fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
   |    ^^^^^^^^
   |
   = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `kanji_bits` is never used
  --> src/lib.rs:86:4
   |
86 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
   |    ^^^^^^^^^^

warning: function `mark_bits` is never used
  --> src/lib.rs:93:4
   |
93 | fn mark_bits(width: i64) -> [u16; 16] {
   |    ^^^^^^^^^

warning: `libos32term_render` (lib) generated 4 warnings (run `cargo fix --lib -p libos32term_render` to apply 1 suggestion)
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.31s
     Running tests/rework.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/rework-a44ed2b7fc01ef6a)

running 2 tests
test accept_i32_max_pixel ... FAILED
test accept_i32_min_pixel ... FAILED

failures:

---- accept_i32_max_pixel stdout ----

thread 'accept_i32_max_pixel' (80) panicked at tests/rework.rs:21:5:
assertion `left == right` failed
  left: Ok(Stats { runs: 0, glyph_reads: 0 })
 right: Ok(Stats { runs: 16, glyph_reads: 0 })
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- accept_i32_min_pixel stdout ----

thread 'accept_i32_min_pixel' (81) panicked at tests/rework.rs:21:5:
assertion `left == right` failed
  left: Ok(Stats { runs: 0, glyph_reads: 0 })
 right: Ok(Stats { runs: 16, glyph_reads: 0 })


failures:
    accept_i32_max_pixel
    accept_i32_min_pixel

test result: FAILED. 0 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test rework`

exit=101
```

## R1 GREEN i32上下限

```text
515ae13e1420da54e9e848784f675f6aec17ae495dfa096436fa452cdfffc840  userland/libos32term_render/src/lib.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test rework
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: unused import: `Cell`
  --> src/lib.rs:29:13
   |
29 |     model::{Cell, Model},
   |             ^^^^
   |
   = note: `#[warn(unused_imports)]` (part of `#[warn(unused)]`) on by default

warning: function `ank_bits` is never used
   --> src/lib.rs:109:4
    |
109 | fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
    |    ^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `kanji_bits` is never used
   --> src/lib.rs:116:4
    |
116 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    |    ^^^^^^^^^^

warning: function `mark_bits` is never used
   --> src/lib.rs:123:4
    |
123 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^

warning: `libos32term_render` (lib) generated 4 warnings (run `cargo fix --lib -p libos32term_render` to apply 1 suggestion)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.34s
     Running tests/rework.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/rework-a44ed2b7fc01ef6a)

running 2 tests
test accept_i32_max_pixel ... ok
test accept_i32_min_pixel ... ok

test result: ok. 2 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## R1 GREEN 拒否側回帰

```text
515ae13e1420da54e9e848784f675f6aec17ae495dfa096436fa452cdfffc840  userland/libos32term_render/src/lib.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test render invalid_geometry
warning: unused import: `Cell`
  --> src/lib.rs:29:13
   |
29 |     model::{Cell, Model},
   |             ^^^^
   |
   = note: `#[warn(unused_imports)]` (part of `#[warn(unused)]`) on by default

warning: function `ank_bits` is never used
   --> src/lib.rs:109:4
    |
109 | fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
    |    ^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `kanji_bits` is never used
   --> src/lib.rs:116:4
    |
116 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    |    ^^^^^^^^^^

warning: function `mark_bits` is never used
   --> src/lib.rs:123:4
    |
123 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^

warning: `libos32term_render` (lib) generated 4 warnings (run `cargo fix --lib -p libos32term_render` to apply 1 suggestion)
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.37s
     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 1 test
test invalid_geometry_has_no_callbacks ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 20 filtered out; finished in 0.00s


exit=0
```

## R1 GREEN 背景と空交差

```text
515ae13e1420da54e9e848784f675f6aec17ae495dfa096436fa452cdfffc840  userland/libos32term_render/src/lib.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test render background_and_empty
warning: unused import: `Cell`
  --> src/lib.rs:29:13
   |
29 |     model::{Cell, Model},
   |             ^^^^
   |
   = note: `#[warn(unused_imports)]` (part of `#[warn(unused)]`) on by default

warning: function `ank_bits` is never used
   --> src/lib.rs:109:4
    |
109 | fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
    |    ^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `kanji_bits` is never used
   --> src/lib.rs:116:4
    |
116 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    |    ^^^^^^^^^^

warning: function `mark_bits` is never used
   --> src/lib.rs:123:4
    |
123 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^

warning: `libos32term_render` (lib) generated 4 warnings (run `cargo fix --lib -p libos32term_render` to apply 1 suggestion)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.00s
     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 1 test
test background_and_empty ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 20 filtered out; finished in 0.00s


exit=0
```

## R1 GREEN 巨大表示高clip

```text
515ae13e1420da54e9e848784f675f6aec17ae495dfa096436fa452cdfffc840  userland/libos32term_render/src/lib.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test render i32_valid
warning: unused import: `Cell`
  --> src/lib.rs:29:13
   |
29 |     model::{Cell, Model},
   |             ^^^^
   |
   = note: `#[warn(unused_imports)]` (part of `#[warn(unused)]`) on by default

warning: function `ank_bits` is never used
   --> src/lib.rs:109:4
    |
109 | fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
    |    ^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `kanji_bits` is never used
   --> src/lib.rs:116:4
    |
116 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    |    ^^^^^^^^^^

warning: function `mark_bits` is never used
   --> src/lib.rs:123:4
    |
123 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^

warning: `libos32term_render` (lib) generated 4 warnings (run `cargo fix --lib -p libos32term_render` to apply 1 suggestion)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.00s
     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 1 test
test i32_valid_edges_and_huge_clipped_height ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 20 filtered out; finished in 0.00s


exit=0
```

## R2 RED 空saved_rows stub・部分不足とtop

```text
94e2d7dfe9ef8ae950647810e4a9a9c712753435bd446d0edf293ac2dc37c40b  userland/libos32term_render/src/lib.rs
7a1d3d02462067b15b1a5e67fe90654326e3c582198c1020310d653de740079b  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --lib saved_
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: function `ank_bits` is never used
   --> src/lib.rs:109:4
    |
109 | fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
    |    ^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `kanji_bits` is never used
   --> src/lib.rs:116:4
    |
116 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    |    ^^^^^^^^^^

warning: function `mark_bits` is never used
   --> src/lib.rs:123:4
    |
123 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^

warning: `libos32term_render` (lib test) generated 3 warnings
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.30s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 2 tests
test rework_tests::saved_clip_below_storage_and_top_roundtrip ... FAILED
test rework_tests::saved_partial_shortage_and_top_once ... FAILED

failures:

---- rework_tests::saved_clip_below_storage_and_top_roundtrip stdout ----

thread 'rework_tests::saved_clip_below_storage_and_top_roundtrip' (79) panicked at src/rework_tests.rs:24:9:
assertion `left == right` failed
  left: []
 right: [Single('A')]
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- rework_tests::saved_partial_shortage_and_top_once stdout ----

thread 'rework_tests::saved_partial_shortage_and_top_once' (80) panicked at src/rework_tests.rs:9:5:
assertion `left == right` failed
  left: []
 right: [Single('C'), Single('D')]


failures:
    rework_tests::saved_clip_below_storage_and_top_roundtrip
    rework_tests::saved_partial_shortage_and_top_once

test result: FAILED. 0 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## R2 GREEN 保存行交差・top

```text
a0f3b988af9d3bac4c132962d3f9c5b8a74d119fccb2859087a917e3e6a4bab4  userland/libos32term_render/src/lib.rs
7a1d3d02462067b15b1a5e67fe90654326e3c582198c1020310d653de740079b  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --lib saved_
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: function `ank_bits` is never used
   --> src/lib.rs:120:4
    |
120 | fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
    |    ^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `kanji_bits` is never used
   --> src/lib.rs:127:4
    |
127 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    |    ^^^^^^^^^^

warning: function `mark_bits` is never used
   --> src/lib.rs:134:4
    |
134 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^

warning: `libos32term_render` (lib test) generated 3 warnings
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.26s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 2 tests
test rework_tests::saved_clip_below_storage_and_top_roundtrip ... ok
test rework_tests::saved_partial_shortage_and_top_once ... ok

test result: ok. 2 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## R2 GREEN 幅不一致事前検査（R1でRED）

```text
a0f3b988af9d3bac4c132962d3f9c5b8a74d119fccb2859087a917e3e6a4bab4  userland/libos32term_render/src/lib.rs
7a1d3d02462067b15b1a5e67fe90654326e3c582198c1020310d653de740079b  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test render width_mismatch
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: function `ank_bits` is never used
   --> src/lib.rs:120:4
    |
120 | fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
    |    ^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `kanji_bits` is never used
   --> src/lib.rs:127:4
    |
127 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    |    ^^^^^^^^^^

warning: function `mark_bits` is never used
   --> src/lib.rs:134:4
    |
134 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^

warning: `libos32term_render` (lib) generated 3 warnings
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.40s
     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 1 test
test width_mismatch_preflight ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 20 filtered out; finished in 0.00s


exit=0
```

## R3 RED head選択None stub・haloと交差

```text
6384ed24ce725184c241e4720e37f538ed6a3172115c8ffa89ecc7c8055c4666  userland/libos32term_render/src/lib.rs
46c26c59f2acd9cab2823489266b0f14a29b108df1f4722013fae773950df96a  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --lib halo_
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: function `ank_bits` is never used
   --> src/lib.rs:120:4
    |
120 | fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
    |    ^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `kanji_bits` is never used
   --> src/lib.rs:127:4
    |
127 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    |    ^^^^^^^^^^

warning: function `mark_bits` is never used
   --> src/lib.rs:134:4
    |
134 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^

warning: function `glyph_bits` is never used
   --> src/lib.rs:165:4
    |
165 | fn glyph_bits(_c: char, _width: i64, _glyphs: &mut impl Glyphs, _stats: &mut Stats) -> [u16; 16] {
    |    ^^^^^^^^^^

warning: function `draw` is never used
   --> src/lib.rs:168:4
    |
168 | fn draw(_bits: &[u16; 16], _x: i64, _y: i64, _width: i64, _write: Rect,
    |    ^^^^

warning: `libos32term_render` (lib test) generated 5 warnings
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.28s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 2 tests
test rework_tests::halo_single_and_left_half_at_negative_origin ... FAILED
test rework_tests::halo_wide_head_for_right_half ... FAILED

failures:

---- rework_tests::halo_single_and_left_half_at_negative_origin stdout ----

thread 'rework_tests::halo_single_and_left_half_at_negative_origin' (85) panicked at src/rework_tests.rs:44:5:
assertion `left == right` failed
  left: None
 right: Some(('A', 8))
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- rework_tests::halo_wide_head_for_right_half stdout ----

thread 'rework_tests::halo_wide_head_for_right_half' (86) panicked at src/rework_tests.rs:36:5:
assertion `left == right` failed
  left: None
 right: Some(('日', 16))


failures:
    rework_tests::halo_single_and_left_half_at_negative_origin
    rework_tests::halo_wide_head_for_right_half

test result: FAILED. 0 passed; 2 failed; 0 ignored; 0 measured; 2 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## R3 GREEN head選択・renderへ接続（glyph/drawはstub）

```text
d27b5e4cb71f02590d6e8f5b4d1635fea3a6f6768f48cd41cf200f714194286b  userland/libos32term_render/src/lib.rs
46c26c59f2acd9cab2823489266b0f14a29b108df1f4722013fae773950df96a  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --lib halo_
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: function `ank_bits` is never used
   --> src/lib.rs:130:4
    |
130 | fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
    |    ^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `kanji_bits` is never used
   --> src/lib.rs:137:4
    |
137 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    |    ^^^^^^^^^^

warning: function `mark_bits` is never used
   --> src/lib.rs:144:4
    |
144 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^

warning: `libos32term_render` (lib test) generated 3 warnings
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.29s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 2 tests
test rework_tests::halo_single_and_left_half_at_negative_origin ... ok
test rework_tests::halo_wide_head_for_right_half ... ok

test result: ok. 2 passed; 0 failed; 0 ignored; 0 measured; 2 filtered out; finished in 0.00s


exit=0
```

## R4a RED ANK無供給stub・引数と読出数

```text
d27b5e4cb71f02590d6e8f5b4d1635fea3a6f6768f48cd41cf200f714194286b  userland/libos32term_render/src/lib.rs
dd999f8136ec4f22e28fe780fd52edb91711c677ec49bebc043f64353deb2f45  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --lib dispatch_ank
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: function `ank_bits` is never used
   --> src/lib.rs:130:4
    |
130 | fn ank_bits(data: [u8; 16], wide: bool) -> [u16; 16] {
    |    ^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `kanji_bits` is never used
   --> src/lib.rs:137:4
    |
137 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    |    ^^^^^^^^^^

warning: function `mark_bits` is never used
   --> src/lib.rs:144:4
    |
144 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^

warning: `libos32term_render` (lib test) generated 3 warnings
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.30s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 1 test
test rework_tests::dispatch_ank_arguments ... FAILED

failures:

---- rework_tests::dispatch_ank_arguments stdout ----

thread 'rework_tests::dispatch_ank_arguments' (88) panicked at src/rework_tests.rs:76:9:
assertion `left == right` failed: ANK A
  left: 0
 right: 1
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    rework_tests::dispatch_ank_arguments

test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 4 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## R4a GREEN ANK供給

```text
9a32e9483a13bf19679eb0bba04b09b9cc19b321cc1481aee6d35544526daa70  userland/libos32term_render/src/lib.rs
dd999f8136ec4f22e28fe780fd52edb91711c677ec49bebc043f64353deb2f45  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --lib dispatch_ank
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: function `kanji_bits` is never used
   --> src/lib.rs:137:4
    |
137 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    |    ^^^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `mark_bits` is never used
   --> src/lib.rs:144:4
    |
144 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^

warning: `libos32term_render` (lib test) generated 2 warnings
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.30s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 1 test
test rework_tests::dispatch_ank_arguments ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 4 filtered out; finished in 0.00s


exit=0
```

## R4b RED 非ANK未実装・UnicodeとJIS受渡し

```text
9a32e9483a13bf19679eb0bba04b09b9cc19b321cc1481aee6d35544526daa70  userland/libos32term_render/src/lib.rs
2b41c7cdbb21d18bc1aff8cbff60814bc53cccd02027ae941a20d5b1ca3c871e  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --lib dispatch_unicode
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: function `kanji_bits` is never used
   --> src/lib.rs:137:4
    |
137 | fn kanji_bits(data: [u8; 32]) -> [u16; 16] {
    |    ^^^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: function `mark_bits` is never used
   --> src/lib.rs:144:4
    |
144 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^

warning: `libos32term_render` (lib test) generated 2 warnings
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.28s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 1 test
test rework_tests::dispatch_unicode_and_jis_arguments ... FAILED

failures:

---- rework_tests::dispatch_unicode_and_jis_arguments stdout ----

thread 'rework_tests::dispatch_unicode_and_jis_arguments' (179) panicked at src/rework_tests.rs:88:9:
assertion `left == right` failed
  left: 0
 right: 1
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    rework_tests::dispatch_unicode_and_jis_arguments

test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 5 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## R4b GREEN Unicode/JIS供給

```text
4a1a3a7d25c96004f5e5be8c9a1d517bb9b3e481b8fdabcdb87114f10d31e59c  userland/libos32term_render/src/lib.rs
2b41c7cdbb21d18bc1aff8cbff60814bc53cccd02027ae941a20d5b1ca3c871e  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --lib dispatch_unicode
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: function `mark_bits` is never used
   --> src/lib.rs:144:4
    |
144 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term_render` (lib test) generated 1 warning
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.29s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 1 test
test rework_tests::dispatch_unicode_and_jis_arguments ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 5 filtered out; finished in 0.00s


exit=0
```

## R4b GREEN render引数受渡し・禁止lookup（既存試験）

```text
4a1a3a7d25c96004f5e5be8c9a1d517bb9b3e481b8fdabcdb87114f10d31e59c  userland/libos32term_render/src/lib.rs
2b41c7cdbb21d18bc1aff8cbff60814bc53cccd02027ae941a20d5b1ca3c871e  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test render forwarded_glyph
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: function `mark_bits` is never used
   --> src/lib.rs:144:4
    |
144 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term_render` (lib) generated 1 warning
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.48s
     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 1 test
test forwarded_glyph_arguments_and_forbidden_lookups ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 20 filtered out; finished in 0.00s


exit=0
```

## R4c RED 代替経路のゼロstub

```text
4a1a3a7d25c96004f5e5be8c9a1d517bb9b3e481b8fdabcdb87114f10d31e59c  userland/libos32term_render/src/lib.rs
ff75de9fa5d6443091d843461c626bfa0460839d2bc8b19be77f0de15349ab17  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --lib dispatch_fallback
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: function `mark_bits` is never used
   --> src/lib.rs:144:4
    |
144 | fn mark_bits(width: i64) -> [u16; 16] {
    |    ^^^^^^^^^
    |
    = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term_render` (lib test) generated 1 warning
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.28s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 1 test
test rework_tests::dispatch_fallback_without_bitmap_read ... FAILED

failures:

---- rework_tests::dispatch_fallback_without_bitmap_read stdout ----

thread 'rework_tests::dispatch_fallback_without_bitmap_read' (328) panicked at src/rework_tests.rs:102:9:
assertion `left == right` failed
  left: 0
 right: 65280
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    rework_tests::dispatch_fallback_without_bitmap_read

test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 6 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## R4c GREEN 保持したマーク計算へ接続

```text
eba610432120e9a09b4749bacb45e07402d250a39c943cde4c62e518af58a1f7  userland/libos32term_render/src/lib.rs
ff75de9fa5d6443091d843461c626bfa0460839d2bc8b19be77f0de15349ab17  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
4ab5efc3e20e2d6d277e1f104fb03b579bebf0fc2bcbce2d50128a50c6652150  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --lib dispatch_
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.28s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 3 tests
test rework_tests::dispatch_ank_arguments ... ok
test rework_tests::dispatch_fallback_without_bitmap_read ... ok
test rework_tests::dispatch_unicode_and_jis_arguments ... ok

test result: ok. 3 passed; 0 failed; 0 ignored; 0 measured; 4 filtered out; finished in 0.00s


exit=0
```

## R5 RED 無出力draw・独立した8挙動の画素assertion

```text
eba610432120e9a09b4749bacb45e07402d250a39c943cde4c62e518af58a1f7  userland/libos32term_render/src/lib.rs
ff75de9fa5d6443091d843461c626bfa0460839d2bc8b19be77f0de15349ab17  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
33d863affebe931302624746c6e137f82300dfa5bab1d4652314d42857ddafae  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test rework crop_
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.40s
     Running tests/rework.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/rework-a44ed2b7fc01ef6a)

running 8 tests
test crop_bottom_pixel ... FAILED
test crop_left_half ... FAILED
test crop_local_row_offset_and_top ... FAILED
test crop_negative_origin ... FAILED
test crop_partial_saved_shortage ... FAILED
test crop_right_half_from_halo ... FAILED
test crop_top_pixel ... FAILED
test crop_unaligned ... FAILED

failures:

---- crop_bottom_pixel stdout ----

thread 'crop_bottom_pixel' (218) panicked at tests/rework.rs:87:13:
assertion `left == right` failed: local (7,15), top=0, Rect { x0: 0, y0: 15, x1: 16, y1: 16 }
  left: 0
 right: 1
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- crop_left_half stdout ----

thread 'crop_left_half' (219) panicked at tests/rework.rs:87:13:
assertion `left == right` failed: local (0,0), top=0, Rect { x0: 0, y0: 0, x1: 8, y1: 16 }
  left: 0
 right: 1

---- crop_local_row_offset_and_top stdout ----

thread 'crop_local_row_offset_and_top' (220) panicked at tests/rework.rs:87:13:
assertion `left == right` failed: local (2,18), top=1, Rect { x0: -7, y0: 9, x1: 25, y1: 39 }
  left: 0
 right: 1

---- crop_negative_origin stdout ----

thread 'crop_negative_origin' (221) panicked at tests/rework.rs:87:13:
assertion `left == right` failed: local (9,4), top=0, Rect { x0: 2, y0: -5, x1: 23, y1: 22 }
  left: 0
 right: 1

---- crop_partial_saved_shortage stdout ----

thread 'crop_partial_saved_shortage' (222) panicked at tests/rework.rs:87:13:
assertion `left == right` failed: local (0,0), top=2, Rect { x0: 0, y0: 0, x1: 32, y1: 48 }
  left: 0
 right: 1

---- crop_right_half_from_halo stdout ----

thread 'crop_right_half_from_halo' (223) panicked at tests/rework.rs:87:13:
assertion `left == right` failed: local (8,0), top=0, Rect { x0: 8, y0: 0, x1: 16, y1: 16 }
  left: 0
 right: 1

---- crop_top_pixel stdout ----

thread 'crop_top_pixel' (224) panicked at tests/rework.rs:87:13:
assertion `left == right` failed: local (0,0), top=0, Rect { x0: 0, y0: 0, x1: 16, y1: 1 }
  left: 0
 right: 1

---- crop_unaligned stdout ----

thread 'crop_unaligned' (225) panicked at tests/rework.rs:87:13:
assertion `left == right` failed: local (9,3), top=0, Rect { x0: 9, y0: 3, x1: 29, y1: 29 }
  left: 0
 right: 1


failures:
    crop_bottom_pixel
    crop_left_half
    crop_local_row_offset_and_top
    crop_negative_origin
    crop_partial_saved_shortage
    crop_right_half_from_halo
    crop_top_pixel
    crop_unaligned

test result: FAILED. 0 passed; 8 failed; 0 ignored; 0 measured; 2 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test rework`

exit=101
```

## R5 GREEN 部分clip8挙動

```text
6cc2ee8d1dde22d5f3765cae3b28eca7aadf5e02124ab75dea1c27329a2da363  userland/libos32term_render/src/lib.rs
ff75de9fa5d6443091d843461c626bfa0460839d2bc8b19be77f0de15349ab17  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
33d863affebe931302624746c6e137f82300dfa5bab1d4652314d42857ddafae  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test rework crop_
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.33s
     Running tests/rework.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/rework-a44ed2b7fc01ef6a)

running 8 tests
test crop_bottom_pixel ... ok
test crop_left_half ... ok
test crop_local_row_offset_and_top ... ok
test crop_negative_origin ... ok
test crop_partial_saved_shortage ... ok
test crop_right_half_from_halo ... ok
test crop_top_pixel ... ok
test crop_unaligned ... ok

test result: ok. 8 passed; 0 failed; 0 ignored; 0 measured; 2 filtered out; finished in 0.00s


exit=0
```

## R5 GREEN 既存21件の統合回帰（初回GREEN試験を含む）

```text
6cc2ee8d1dde22d5f3765cae3b28eca7aadf5e02124ab75dea1c27329a2da363  userland/libos32term_render/src/lib.rs
ff75de9fa5d6443091d843461c626bfa0460839d2bc8b19be77f0de15349ab17  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
33d863affebe931302624746c6e137f82300dfa5bab1d4652314d42857ddafae  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test render
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.36s
     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 21 tests
test ascii_pixels_runs_counts ... ok
test background_and_empty ... ok
test control_marks ... ok
test i32_valid_edges_and_huge_clipped_height ... ok
test forwarded_glyph_arguments_and_forbidden_lookups ... ok
test invalid_geometry_has_no_callbacks ... ok
test fullwidth_centered ... ok
test invalid_jis_bytes ... ok
test kana_yen_and_ascii_ank_frames ... ok
test kanji_layout ... ok
test model_rejects_invalid_dimensions_and_capacity ... ok
test only_visible_glyph_read_with_halo ... ok
test replacement_ignores_mapping ... ok
test replacement_marks ... ok
test width_mismatch_preflight ... ok
test zero_glyph_is_blank ... ok
test japanese_halves_and_pixel_clips ... ok
test negative_origin_top_roundtrip_and_reexposure ... ok
test no_fixed_row_cutoff ... ok
test ank_mapping ... ok
test widths ... ok

test result: ok. 21 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s


exit=0
```

## 回帰追加・初回GREEN（幅不一致の可視行/halo限定契約）

```text
6cc2ee8d1dde22d5f3765cae3b28eca7aadf5e02124ab75dea1c27329a2da363  userland/libos32term_render/src/lib.rs
ff75de9fa5d6443091d843461c626bfa0460839d2bc8b19be77f0de15349ab17  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
0e1ed11ccd540c671a8306a5140f0774a8223fe723bd9da536be574282534ca2  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test rework width_mismatch
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.30s
     Running tests/rework.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/rework-a44ed2b7fc01ef6a)

running 1 test
test width_mismatch_visible_rows_and_halo_scope_regression ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 10 filtered out; finished in 0.00s


exit=0
```

## 今回の段階対応と証跡の限界

| 段階 | 除去後のRED | 最小再実装とGREEN |
|---|---|---|
| R1/R1b | 無出力renderで既存18失敗。上下限は独立した2試験も失敗 | checked_axis、Grid交差、背景。境界2件＋拒否・背景・巨大高各1件GREEN |
| R2 | 空saved_rowsで2件assertion失敗（空配列対C/D、A） | 保存行との交差とtop一回加算。2件GREEN、幅事前検査も再接続 |
| R3 | None headで2件assertion失敗 | Wide head/Single選択、Continuationと交差外除外。2件GREEN。供給/drawはまだstub |
| R4a | 無読出しでANK読出数0対1 | 算術写像値をANKへ渡す。1件GREEN |
| R4b | 非ANK未実装で変換数0対1 | Unicode→JIS→漢字の値受渡し。1件GREEN、既存forwarded試験もGREEN |
| R4c | 代替経路ゼロbitmapで0対65280 | 保持したマーク計算へ接続。dispatch 3件GREEN |
| R5 | 無出力drawで左右/上下/非整列/負原点/部分不足/行offsetの8件が各々画素0対1 | write交差内走査と終端でrunを出す処理。8件GREEN、既存21件統合GREEN |

R2/R3は本体helperの選択結果を直接検証し、R5と既存統合試験がそのrender接続と画素を検証する。
行別字形の既存top往復試験の最終形は無変更で保持し、統合GREENで確認した。
R1の全体stubでno_fixed_row_cutoff等も失敗したが、それを打切り機構の固有REDとは数えない。
モデル不変性、固定上限なし、依存側Model拒否等は引き続き回帰であり、故障を挿入していない。
新規width_mismatch_visible_rows_and_halo_scope_regressionは初回GREENの回帰である。

新規17試験（unit 7 + integration 10）に今回のassertion RED/GREENがある。
既存21件と新規回帰1件を合わせ最終39件。stub途中のunused/dead_code警告はそのまま採取した。
今回の指定本体経路について再実装未了はないが、過去TDD不履行の事実は変わらない。
公開Modelから到達不能なInvalidCapacityと、巨大容量が必要なwrite長拒否分岐の実行未確認は残る。
PM再実行・独立レビュー・共有ゲート登録は本作業では未実施。配備/guest/commit/push等は禁止範囲。

以下は整形後の全体再検証。

## FINAL host test

```text
0de33ac3b33b545d12c2b9eeeacf85e0ce17e6870a2aa94eefb6949bb2e4332f  userland/libos32term_render/src/lib.rs
9d8a33c4cf52f6862807ec0ce35a219a9d2bc4ad35aca00f107a35d539d2a5ed  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
474d631fa6df33cfd8ee3c06baeaea1660016871c36ca28e6a59ee6eb9f7ca82  userland/libos32term_render/tests/rework.rs
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.37s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 7 tests
test rework_tests::dispatch_ank_arguments ... ok
test rework_tests::dispatch_fallback_without_bitmap_read ... ok
test rework_tests::halo_single_and_left_half_at_negative_origin ... ok
test rework_tests::dispatch_unicode_and_jis_arguments ... ok
test rework_tests::halo_wide_head_for_right_half ... ok
test rework_tests::saved_clip_below_storage_and_top_roundtrip ... ok
test rework_tests::saved_partial_shortage_and_top_once ... ok

test result: ok. 7 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 21 tests
test ascii_pixels_runs_counts ... ok
test background_and_empty ... ok
test forwarded_glyph_arguments_and_forbidden_lookups ... ok
test i32_valid_edges_and_huge_clipped_height ... ok
test fullwidth_centered ... ok
test invalid_geometry_has_no_callbacks ... ok
test control_marks ... ok
test invalid_jis_bytes ... ok
test kana_yen_and_ascii_ank_frames ... ok
test kanji_layout ... ok
test model_rejects_invalid_dimensions_and_capacity ... ok
test replacement_ignores_mapping ... ok
test japanese_halves_and_pixel_clips ... ok
test only_visible_glyph_read_with_halo ... ok
test replacement_marks ... ok
test width_mismatch_preflight ... ok
test zero_glyph_is_blank ... ok
test negative_origin_top_roundtrip_and_reexposure ... ok
test no_fixed_row_cutoff ... ok
test ank_mapping ... ok
test widths ... ok

test result: ok. 21 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

     Running tests/rework.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/rework-a44ed2b7fc01ef6a)

running 11 tests
test accept_i32_min_pixel ... ok
test accept_i32_max_pixel ... ok
test crop_bottom_pixel ... ok
test crop_left_half ... ok
test crop_negative_origin ... ok
test crop_partial_saved_shortage ... ok
test crop_right_half_from_halo ... ok
test crop_local_row_offset_and_top ... ok
test crop_top_pixel ... ok
test crop_unaligned ... ok
test width_mismatch_visible_rows_and_halo_scope_regression ... ok

test result: ok. 11 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

   Doc-tests libos32term_render

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## FINAL host check

```text
0de33ac3b33b545d12c2b9eeeacf85e0ce17e6870a2aa94eefb6949bb2e4332f  userland/libos32term_render/src/lib.rs
9d8a33c4cf52f6862807ec0ce35a219a9d2bc4ad35aca00f107a35d539d2a5ed  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
474d631fa6df33cfd8ee3c06baeaea1660016871c36ca28e6a59ee6eb9f7ca82  userland/libos32term_render/tests/rework.rs
$ cargo check --manifest-path userland/libos32term_render/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline
    Checking libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `dev` profile [unoptimized + debuginfo] target(s) in 0.06s

exit=0
```

## FINAL fmt check

```text
0de33ac3b33b545d12c2b9eeeacf85e0ce17e6870a2aa94eefb6949bb2e4332f  userland/libos32term_render/src/lib.rs
9d8a33c4cf52f6862807ec0ce35a219a9d2bc4ad35aca00f107a35d539d2a5ed  userland/libos32term_render/src/rework_tests.rs
566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c  userland/libos32term_render/tests/render.rs
474d631fa6df33cfd8ee3c06baeaea1660016871c36ca28e6a59ee6eb9f7ca82  userland/libos32term_render/tests/rework.rs
$ cargo fmt --manifest-path userland/libos32term_render/Cargo.toml -- --check

exit=0
```

最終結果: host test 39件成功（unit 7 / integration 32 / doc 0）、host check / fmt checkともexit=0、警告なし。
既存tests/render.rsのSHA256は最初の除去後採取時と一致: 566684ab5459d6d80eaa0fbe921e75479411de2e88c691010a351558ebd6cd3c。
Cargo.tomlの依存はlibos32termのpath依存のみ。no_std/forbid(unsafe_code)保持、本体alloc追加なし。
