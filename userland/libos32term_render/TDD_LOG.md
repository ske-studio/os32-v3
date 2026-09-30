# T5R 実行証跡（2026-09-08）

全コマンドは `~/os32` から実行。以下はstdout/stderrの実出力と子プロセス終了コード。
採取器 `record.py` の呼出形式は `python3 userland/libos32term_render/record.py <段階名> <以下のコマンド>`。
初期採取器は記録対象のexitを表示し自身は0で終了した。最終版は対象exitをそのまま返す。
REDはすべてコンパイル後のassertion失敗（対象exit=101）であり、コンパイル失敗を含めない。

| 段階 | RED時の未実装状態 | GREENで加えたもの |
|---|---|---|
| 01 | widthは常にWide、ankはNone | 幅分類と算術写像 |
| 02 | renderは無出力Ok | 次元/top/座標/clipの検査（全10入力を集約検査） |
| 03 | 検査だけで無出力 | 厳密write内の背景run |
| 04 | 背景だけ | ANK画素の連続runと幅分類の事前検査 |
| 05 | 左寄せANK、他は空画素 | 全角中央配置、制御・代替枠 |
| 06 | 非ANKはすべて枠 | JIS変換・各バイト検査・漢字bitmap・全ゼロ尊重 |

01〜06の13テストでRED→最小実装→GREEN。
07〜08の8テストは既存実装の追加回帰として初回からGREEN。
この8件の事前REDは未実施であり、全挙動に事前REDを求める手順契約には不足がある。
後付けのmutationを初期REDと呼ぶことはしていない。
09は整形・説明追記と行別に異なる字形を用いたtop試験補強後の最終21件。
10/11は指定のhost check/fmt check。ゲスト表示・性能・リンク試験は実施していない。


## 01 RED 幅・ANK（コンパイル済assertion失敗）

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
     Locking 1 package to latest compatible version
   Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.81s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 2 tests
test ank_mapping ... FAILED
test widths ... FAILED

failures:

---- ank_mapping stdout ----

thread 'ank_mapping' (165) panicked at tests/render.rs:23:13:
assertion `left == right` failed: 20
  left: None
 right: Some(32)
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- widths stdout ----

thread 'widths' (166) panicked at tests/render.rs:8:13:
assertion `left == right` failed: 0
  left: Wide
 right: Single


failures:
    ank_mapping
    widths

test result: FAILED. 0 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test render`

exit=101
```

## 01 GREEN 幅・ANK

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.48s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 2 tests
test ank_mapping ... ok
test widths ... ok

test result: ok. 2 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

   Doc-tests libos32term_render

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## 02 RED 入力検査の副作用ゼロ

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: unused imports: `Cell`, `ClipError`, and `Grid`
  --> src/lib.rs:17:26
   |
17 | use libos32term::{clip::{Grid, ClipError}, model::{Model, Cell}};
   |                          ^^^^  ^^^^^^^^^                  ^^^^
   |
   = note: `#[warn(unused_imports)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term_render` (lib) generated 1 warning (run `cargo fix --lib -p libos32term_render` to apply 1 suggestion)
warning: `libos32term_render` (lib test) generated 1 warning (1 duplicate)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.35s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 3 tests
test invalid_geometry_has_no_callbacks ... FAILED
test ank_mapping ... ok
test widths ... ok

failures:

---- invalid_geometry_has_no_callbacks stdout ----

thread 'invalid_geometry_has_no_callbacks' (127) panicked at tests/render.rs:64:9:
assertion `left == right` failed: View { top: 0, height: 0, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: Ok(Stats { runs: 0, glyph_reads: 0 })
 right: Err(InvalidDimensions)
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    invalid_geometry_has_no_callbacks

test result: FAILED. 2 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

error: test failed, to rerun pass `--test render`

exit=101
```

## 02 RED補足 全10入力を実行しassertion集約

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline invalid_geometry
warning: unused imports: `Cell`, `ClipError`, and `Grid`
  --> src/lib.rs:17:26
   |
17 | use libos32term::{clip::{Grid, ClipError}, model::{Model, Cell}};
   |                          ^^^^  ^^^^^^^^^                  ^^^^
   |
   = note: `#[warn(unused_imports)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term_render` (lib) generated 1 warning (run `cargo fix --lib -p libos32term_render` to apply 1 suggestion)
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
warning: `libos32term_render` (lib test) generated 1 warning (1 duplicate)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.30s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 1 test
test invalid_geometry_has_no_callbacks ... FAILED

failures:

---- invalid_geometry_has_no_callbacks stdout ----

thread 'invalid_geometry_has_no_callbacks' (95) panicked at tests/render.rs:69:5:
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
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    invalid_geometry_has_no_callbacks

test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 2 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test render`

exit=101
```

## 02 GREEN 入力検査

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.32s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 3 tests
test invalid_geometry_has_no_callbacks ... ok
test ank_mapping ... ok
test widths ... ok

test result: ok. 3 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

   Doc-tests libos32term_render

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## 03 RED 背景・保存行不足・空交差

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline background_and_empty
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.30s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 1 test
test background_and_empty ... FAILED

failures:

---- background_and_empty stdout ----

thread 'background_and_empty' (241) panicked at tests/render.rs:78:5:
assertion `left == right` failed
  left: []
 right: [(-2, -4, 9, false), (-2, -3, 9, false), (-2, -2, 9, false), (-2, -1, 9, false), (-2, 0, 9, false), (-2, 1, 9, false), (-2, 2, 9, false), (-2, 3, 9, false), (-2, 4, 9, false), (-2, 5, 9, false), (-2, 6, 9, false), (-2, 7, 9, false), (-2, 8, 9, false), (-2, 9, 9, false), (-2, 10, 9, false), (-2, 11, 9, false), (-2, 12, 9, false), (-2, 13, 9, false), (-2, 14, 9, false), (-2, 15, 9, false), (-2, 16, 9, false), (-2, 17, 9, false), (-2, 18, 9, false), (-2, 19, 9, false), (-2, 20, 9, false), (-2, 21, 9, false), (-2, 22, 9, false), (-2, 23, 9, false), (-2, 24, 9, false), (-2, 25, 9, false), (-2, 26, 9, false), (-2, 27, 9, false), (-2, 28, 9, false), (-2, 29, 9, false), (-2, 30, 9, false), (-2, 31, 9, false), (-2, 32, 9, false), (-2, 33, 9, false), (-2, 34, 9, false)]
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    background_and_empty

test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 3 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test render`

exit=101
```

## 03 GREEN 背景・空交差

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.31s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 4 tests
test background_and_empty ... ok
test invalid_geometry_has_no_callbacks ... ok
test ank_mapping ... ok
test widths ... ok

test result: ok. 4 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

   Doc-tests libos32term_render

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## 04 RED ANK画素・連続run・幅不一致事前検査

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.36s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 6 tests
test ascii_pixels_runs_counts ... FAILED
test background_and_empty ... ok
test invalid_geometry_has_no_callbacks ... ok
test width_mismatch_preflight ... FAILED
test ank_mapping ... ok
test widths ... ok

failures:

---- ascii_pixels_runs_counts stdout ----

thread 'ascii_pixels_runs_counts' (113) panicked at tests/render.rs:125:9:
assertion `left == right` failed: pixel (1,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 0
 right: 1
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- width_mismatch_preflight stdout ----

thread 'width_mismatch_preflight' (116) panicked at tests/render.rs:145:5:
assertion `left == right` failed
  left: Ok(Stats { runs: 16, glyph_reads: 0 })
 right: Err(WidthMismatch)


failures:
    ascii_pixels_runs_counts
    width_mismatch_preflight

test result: FAILED. 4 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

error: test failed, to rerun pass `--test render`

exit=101
```

## 04 GREEN ANK画素・連続run・幅不一致事前検査

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.41s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 6 tests
test ascii_pixels_runs_counts ... ok
test background_and_empty ... ok
test width_mismatch_preflight ... ok
test invalid_geometry_has_no_callbacks ... ok
test ank_mapping ... ok
test widths ... ok

test result: ok. 6 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

   Doc-tests libos32term_render

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## 05 RED 中央配置・制御マーク・代替マーク

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.33s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 10 tests
test ascii_pixels_runs_counts ... ok
test control_marks ... FAILED
test background_and_empty ... ok
test fullwidth_centered ... FAILED
test invalid_geometry_has_no_callbacks ... ok
test replacement_ignores_mapping ... FAILED
test replacement_marks ... FAILED
test width_mismatch_preflight ... ok
test ank_mapping ... ok
test widths ... ok

failures:

---- control_marks stdout ----

thread 'control_marks' (118) panicked at tests/render.rs:125:9:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 0
 right: 1
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- fullwidth_centered stdout ----

thread 'fullwidth_centered' (119) panicked at tests/render.rs:125:9:
assertion `left == right` failed: pixel (1,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 1
 right: 0

---- replacement_ignores_mapping stdout ----

thread 'replacement_ignores_mapping' (121) panicked at tests/render.rs:125:9:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 0
 right: 1

---- replacement_marks stdout ----

thread 'replacement_marks' (122) panicked at tests/render.rs:125:9:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 0
 right: 1


failures:
    control_marks
    fullwidth_centered
    replacement_ignores_mapping
    replacement_marks

test result: FAILED. 6 passed; 4 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

error: test failed, to rerun pass `--test render`

exit=101
```

## 05 GREEN 中央配置・マーク

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.32s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 10 tests
test ascii_pixels_runs_counts ... ok
test background_and_empty ... ok
test fullwidth_centered ... ok
test control_marks ... ok
test replacement_ignores_mapping ... ok
test replacement_marks ... ok
test width_mismatch_preflight ... ok
test invalid_geometry_has_no_callbacks ... ok
test ank_mapping ... ok
test widths ... ok

test result: ok. 10 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

   Doc-tests libos32term_render

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## 06 RED 漢字バイト順・ゼロglyph・JIS検査

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.31s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 13 tests
test ascii_pixels_runs_counts ... ok
test background_and_empty ... ok
test control_marks ... ok
test invalid_geometry_has_no_callbacks ... ok
test replacement_ignores_mapping ... ok
test kanji_layout ... FAILED
test fullwidth_centered ... ok
test width_mismatch_preflight ... ok
test replacement_marks ... ok
test invalid_jis_bytes ... FAILED
test zero_glyph_is_blank ... FAILED
test ank_mapping ... ok
test widths ... ok

failures:

---- kanji_layout stdout ----

thread 'kanji_layout' (288) panicked at tests/render.rs:125:9:
assertion `left == right` failed: pixel (1,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 1
 right: 0
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- invalid_jis_bytes stdout ----

thread 'invalid_jis_bytes' (287) panicked at tests/render.rs:186:5:
assertion `left == right` failed
  left: 0
 right: 1

---- zero_glyph_is_blank stdout ----

thread 'zero_glyph_is_blank' (293) panicked at tests/render.rs:125:9:
assertion `left == right` failed: pixel (0,0), View { top: 0, height: 1, origin: (0, 0), clip: Rect { x0: 0, y0: 0, x1: 16, y1: 16 } }
  left: 1
 right: 0


failures:
    invalid_jis_bytes
    kanji_layout
    zero_glyph_is_blank

test result: FAILED. 10 passed; 3 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

error: test failed, to rerun pass `--test render`

exit=101
```

## 06 GREEN 漢字・JIS・空glyph

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.37s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 13 tests
test ascii_pixels_runs_counts ... ok
test background_and_empty ... ok
test fullwidth_centered ... ok
test invalid_geometry_has_no_callbacks ... ok
test control_marks ... ok
test invalid_jis_bytes ... ok
test kanji_layout ... ok
test width_mismatch_preflight ... ok
test replacement_marks ... ok
test replacement_ignores_mapping ... ok
test zero_glyph_is_blank ... ok
test ank_mapping ... ok
test widths ... ok

test result: ok. 13 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

   Doc-tests libos32term_render

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## 07 追加回帰（既存実装の合否をそのまま記録）

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.37s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 18 tests
test ascii_pixels_runs_counts ... ok
test background_and_empty ... ok
test control_marks ... ok
test i32_valid_edges_and_huge_clipped_height ... ok
test fullwidth_centered ... ok
test invalid_geometry_has_no_callbacks ... ok
test invalid_jis_bytes ... ok
test kana_yen_and_ascii_ank_frames ... ok
test kanji_layout ... ok
test replacement_ignores_mapping ... ok
test only_visible_glyph_read_with_halo ... ok
test width_mismatch_preflight ... ok
test replacement_marks ... ok
test zero_glyph_is_blank ... ok
test japanese_halves_and_pixel_clips ... ok
test negative_origin_top_roundtrip_and_reexposure ... ok
test ank_mapping ... ok
test widths ... ok

test result: ok. 18 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

   Doc-tests libos32term_render

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## 08 追加回帰 容量保証・供給引数・固定行上限なし

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.35s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 21 tests
test ascii_pixels_runs_counts ... ok
test background_and_empty ... ok
test forwarded_glyph_arguments_and_forbidden_lookups ... ok
test i32_valid_edges_and_huge_clipped_height ... ok
test invalid_geometry_has_no_callbacks ... ok
test control_marks ... ok
test fullwidth_centered ... ok
test invalid_jis_bytes ... ok
test kana_yen_and_ascii_ank_frames ... ok
test model_rejects_invalid_dimensions_and_capacity ... ok
test kanji_layout ... ok
test replacement_ignores_mapping ... ok
test only_visible_glyph_read_with_halo ... ok
test width_mismatch_preflight ... ok
test replacement_marks ... ok
test zero_glyph_is_blank ... ok
test japanese_halves_and_pixel_clips ... ok
test negative_origin_top_roundtrip_and_reexposure ... ok
test no_fixed_row_cutoff ... ok
test ank_mapping ... ok
test widths ... ok

test result: ok. 21 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.02s

   Doc-tests libos32term_render

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## 09 最終host test（行ごとに異なる字形でtopを検査）

```text
$ cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.85s
     Running unittests src/lib.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/libos32term_render-5dff5fbdedfd54a5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/render.rs (userland/libos32term_render/target/x86_64-unknown-linux-gnu/debug/deps/render-d81e800db59649a1)

running 21 tests
test ascii_pixels_runs_counts ... ok
test forwarded_glyph_arguments_and_forbidden_lookups ... ok
test background_and_empty ... ok
test control_marks ... ok
test fullwidth_centered ... ok
test i32_valid_edges_and_huge_clipped_height ... ok
test invalid_geometry_has_no_callbacks ... ok
test kanji_layout ... ok
test kana_yen_and_ascii_ank_frames ... ok
test invalid_jis_bytes ... ok
test model_rejects_invalid_dimensions_and_capacity ... ok
test japanese_halves_and_pixel_clips ... ok
test only_visible_glyph_read_with_halo ... ok
test no_fixed_row_cutoff ... ok
test zero_glyph_is_blank ... ok
test replacement_marks ... ok
test width_mismatch_preflight ... ok
test replacement_ignores_mapping ... ok
test negative_origin_top_roundtrip_and_reexposure ... ok
test ank_mapping ... ok
test widths ... ok

test result: ok. 21 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.04s

   Doc-tests libos32term_render

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## 10 最終host check

```text
$ cargo check --manifest-path userland/libos32term_render/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline
    Checking libos32term v0.1.0 (~/os32/userland/libos32term)
    Checking libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
    Finished `dev` profile [unoptimized + debuginfo] target(s) in 0.20s

exit=0
```

## 11 最終fmt check

```text
$ cargo fmt --manifest-path userland/libos32term_render/Cargo.toml -- --check

exit=0
```
