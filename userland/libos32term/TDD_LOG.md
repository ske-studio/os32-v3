# T4 RED / GREEN 実行記録

作業root: `~/os32`。各節は実コマンド、終了コード、実出力を記録する。
RED はコンパイル成功後の assertion 失敗を確認してから実装した。
初期の型/API stub は空出力または明示エラーのみ。挙動実装は対応REDの後。

## 01 ASCII/NUL RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test utf8
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 1.23s
     Running tests/utf8.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/utf8-ed5f1e078ab67bb1)

running 1 test
test ascii_including_nul_is_length_delimited ... FAILED

failures:

---- ascii_including_nul_is_length_delimited stdout ----

thread 'ascii_including_nul_is_length_delimited' (99) panicked at tests/utf8.rs:7:5:
assertion `left == right` failed
  left: ""
 right: "a\0Z"
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    ascii_including_nul_is_length_delimited

test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test utf8`
```

## 01 ASCII/NUL GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test utf8
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.84s
     Running tests/utf8.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/utf8-ed5f1e078ab67bb1)

running 1 test
test ascii_including_nul_is_length_delimited ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 02 2/3/4byte UTF-8 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test utf8
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.87s
     Running tests/utf8.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/utf8-ed5f1e078ab67bb1)

running 2 tests
test ascii_including_nul_is_length_delimited ... ok
test valid_multibyte_waits_for_complete_scalar ... FAILED

failures:

---- valid_multibyte_waits_for_complete_scalar stdout ----

thread 'valid_multibyte_waits_for_complete_scalar' (88) panicked at tests/utf8.rs:9:13:
assertion `left == right` failed
  left: [None, None]
 right: [Some('é'), None]
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    valid_multibyte_waits_for_complete_scalar

test result: FAILED. 1 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test utf8`
```

## 02 2/3/4byte UTF-8 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test utf8
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 1.00s
     Running tests/utf8.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/utf8-ed5f1e078ab67bb1)

running 2 tests
test ascii_including_nul_is_length_delimited ... ok
test valid_multibyte_waits_for_complete_scalar ... ok

test result: ok. 2 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 03 不正列置換 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test utf8
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.81s
     Running tests/utf8.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/utf8-ed5f1e078ab67bb1)

running 3 tests
test ascii_including_nul_is_length_delimited ... ok
test valid_multibyte_waits_for_complete_scalar ... ok
test malformed_sequences_use_documented_replacement_units ... FAILED

failures:

---- malformed_sequences_use_documented_replacement_units stdout ----

thread 'malformed_sequences_use_documented_replacement_units' (88) panicked at tests/utf8.rs:15:9:
assertion `left == right` failed: [e2, 82, 41]
  left: ""
 right: "�A"
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    malformed_sequences_use_documented_replacement_units

test result: FAILED. 2 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test utf8`
```

## 03 不正列置換 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test utf8
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.89s
     Running tests/utf8.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/utf8-ed5f1e078ab67bb1)

running 3 tests
test ascii_including_nul_is_length_delimited ... ok
test malformed_sequences_use_documented_replacement_units ... ok
test valid_multibyte_waits_for_complete_scalar ... ok

test result: ok. 3 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 04 最終未完了・分割不変性 RED

分割不変性は回帰として同時追加。新挙動のREDは最終化assertion。

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test utf8
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.98s
     Running tests/utf8.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/utf8-ed5f1e078ab67bb1)

running 5 tests
test ascii_including_nul_is_length_delimited ... ok
test finalization_replaces_only_pending_prefix_once ... FAILED
test malformed_sequences_use_documented_replacement_units ... ok
test valid_multibyte_waits_for_complete_scalar ... ok
test every_partition_has_identical_utf8_output ... ok

failures:

---- finalization_replaces_only_pending_prefix_once stdout ----

thread 'finalization_replaces_only_pending_prefix_once' (111) panicked at tests/utf8.rs:8:9:
assertion `left == right` failed
  left: [None, None]
 right: [Some('�'), None]
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    finalization_replaces_only_pending_prefix_once

test result: FAILED. 4 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.01s

error: test failed, to rerun pass `--test utf8`
```

## 04 最終未完了・分割不変性 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test utf8
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 1.99s
     Running tests/utf8.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/utf8-ed5f1e078ab67bb1)

running 5 tests
test malformed_sequences_use_documented_replacement_units ... ok
test ascii_including_nul_is_length_delimited ... ok
test finalization_replaces_only_pending_prefix_once ... ok
test valid_multibyte_waits_for_complete_scalar ... ok
test every_partition_has_identical_utf8_output ... ok

test result: ok. 5 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.01s
```

## 05 固定容量初期化 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
warning: fields `cols`, `rows`, and `width` are never read
  --> src/model.rs:21:5
   |
19 | pub struct Model<'a> {
   |            ----- fields in this struct
20 |     cells: &'a mut [Cell],
21 |     cols: usize,
   |     ^^^^
22 |     rows: usize,
   |     ^^^^
23 |     width: fn(char) -> Width,
   |     ^^^^^
   |
   = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term` (lib) generated 1 warning
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.97s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 2 tests
test construction_accepts_minimum_exact_and_extra_storage ... FAILED
test construction_checks_dimensions_capacity_overflow_without_writes ... FAILED

failures:

---- construction_accepts_minimum_exact_and_extra_storage stdout ----

thread 'construction_accepts_minimum_exact_and_extra_storage' (98) panicked at tests/model.rs:22:73:
called `Result::unwrap()` on an `Err` value: InvalidDimensions
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- construction_checks_dimensions_capacity_overflow_without_writes stdout ----

thread 'construction_checks_dimensions_capacity_overflow_without_writes' (99) panicked at tests/model.rs:12:9:
assertion `left == right` failed
  left: Some(InvalidDimensions)
 right: Some(InsufficientStorage)


failures:
    construction_accepts_minimum_exact_and_extra_storage
    construction_checks_dimensions_capacity_overflow_without_writes

test result: FAILED. 0 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test model`
```

## 05 固定容量初期化 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
warning: fields `cols`, `rows`, and `width` are never read
  --> src/model.rs:21:5
   |
19 | pub struct Model<'a> {
   |            ----- fields in this struct
20 |     cells: &'a mut [Cell],
21 |     cols: usize,
   |     ^^^^
22 |     rows: usize,
   |     ^^^^
23 |     width: fn(char) -> Width,
   |     ^^^^^
   |
   = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term` (lib) generated 1 warning
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.97s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 2 tests
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok

test result: ok. 2 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 06 単独セル・遅延折返し・満杯 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
warning: fields `cols`, `rows`, and `width` are never read
  --> src/model.rs:21:5
   |
19 | pub struct Model<'a> {
   |            ----- fields in this struct
20 |     cells: &'a mut [Cell],
21 |     cols: usize,
   |     ^^^^
22 |     rows: usize,
   |     ^^^^
23 |     width: fn(char) -> Width,
   |     ^^^^^
   |
   = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term` (lib) generated 1 warning
    Finished `test` profile [unoptimized + debuginfo] target(s) in 1.00s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 4 tests
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test one_cell_screen_accepts_one_single_scalar ... FAILED
test single_cells_wrap_lazily_and_full_preserves_history ... FAILED

failures:

---- one_cell_screen_accepts_one_single_scalar stdout ----

thread 'one_cell_screen_accepts_one_single_scalar' (106) panicked at tests/model.rs:25:5:
assertion `left == right` failed
  left: Err(Full)
 right: Ok(())
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- single_cells_wrap_lazily_and_full_preserves_history stdout ----

thread 'single_cells_wrap_lazily_and_full_preserves_history' (107) panicked at tests/model.rs:9:40:
assertion `left == right` failed
  left: Err(Full)
 right: Ok(())


failures:
    one_cell_screen_accepts_one_single_scalar
    single_cells_wrap_lazily_and_full_preserves_history

test result: FAILED. 2 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test model`
```

## 06 単独セル・遅延折返し・満杯 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
warning: field `width` is never read
  --> src/model.rs:23:5
   |
19 | pub struct Model<'a> {
   |            ----- field in this struct
...
23 |     width: fn(char) -> Width,
   |     ^^^^^
   |
   = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term` (lib) generated 1 warning
    Finished `test` profile [unoptimized + debuginfo] target(s) in 1.98s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 4 tests
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test one_cell_screen_accepts_one_single_scalar ... ok

test result: ok. 4 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 07 幅注入・全角折返し・1列 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 101
warning: field `width` is never read
  --> src/model.rs:23:5
   |
19 | pub struct Model<'a> {
   |            ----- field in this struct
...
23 |     width: fn(char) -> Width,
   |     ^^^^^
   |
   = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term` (lib) generated 1 warning
   Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.85s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 6 tests
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test construction_accepts_minimum_exact_and_extra_storage ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... FAILED
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... FAILED

failures:

---- one_column_wide_is_explicit_error_without_mutation stdout ----

thread 'one_column_wide_is_explicit_error_without_mutation' (84) panicked at tests/model.rs:28:5:
assertion `left == right` failed
  left: Ok(())
 right: Err(TooWide)
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- wide_pairs_wrap_before_last_column_and_width_is_injected stdout ----

thread 'wide_pairs_wrap_before_last_column_and_width_is_injected' (86) panicked at tests/model.rs:10:5:
assertion `left == right` failed
  left: [Single('漢'), Single(' '), Single(' ')]
 right: [Wide('漢'), Continuation, Single(' ')]


failures:
    one_column_wide_is_explicit_error_without_mutation
    wide_pairs_wrap_before_last_column_and_width_is_injected

test result: FAILED. 4 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test model`
```

## 07 幅注入・全角折返し・1列 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.91s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 6 tests
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok

test result: ok. 6 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 08 CR/LF/CRLF・連続改行 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.88s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 8 tests
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... FAILED
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... FAILED
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok

failures:

---- full_newline_is_atomic_and_cr_allows_explicit_overwrite stdout ----

thread 'full_newline_is_atomic_and_cr_allows_explicit_overwrite' (89) panicked at tests/model.rs:26:5:
assertion `left == right` failed
  left: Err(Full)
 right: Ok(())

---- cr_lf_crlf_and_consecutive_newlines_have_distinct_actions stdout ----

thread 'cr_lf_crlf_and_consecutive_newlines_have_distinct_actions' (88) panicked at tests/model.rs:12:9:
assertion `left == right` failed: "A\r"
  left: (2, 0)
 right: (0, 0)
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    cr_lf_crlf_and_consecutive_newlines_have_distinct_actions
    full_newline_is_atomic_and_cr_allows_explicit_overwrite

test result: FAILED. 6 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test model`
```

## 08 CR/LF/CRLF・連続改行 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.94s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 8 tests
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok

test result: ok. 8 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 09 BS RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.84s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 10 tests
test backspace_at_new_line_does_not_enter_previous_row ... FAILED
test construction_accepts_minimum_exact_and_extra_storage ... ok
test backspace_returns_to_character_start_without_erasing ... FAILED
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok

failures:

---- backspace_at_new_line_does_not_enter_previous_row stdout ----

thread 'backspace_at_new_line_does_not_enter_previous_row' (86) panicked at tests/model.rs:23:5:
assertion `left == right` failed
  left: (1, 1)
 right: (0, 1)
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- backspace_returns_to_character_start_without_erasing stdout ----

thread 'backspace_returns_to_character_start_without_erasing' (87) panicked at tests/model.rs:12:9:
assertion `left == right` failed
  left: Err(Full)
 right: Ok(())


failures:
    backspace_at_new_line_does_not_enter_previous_row
    backspace_returns_to_character_start_without_erasing

test result: FAILED. 8 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test model`
```

## 09 BS GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.96s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 10 tests
test backspace_at_new_line_does_not_enter_previous_row ... ok
test backspace_returns_to_character_start_without_erasing ... ok
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok

test result: ok. 10 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 10 明示カーソル移動の範囲検査 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 1.05s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 11 tests
test backspace_at_new_line_does_not_enter_previous_row ... ok
test backspace_returns_to_character_start_without_erasing ... ok
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test explicit_cursor_checks_retained_bounds_and_preserves_state_on_error ... FAILED
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok

failures:

---- explicit_cursor_checks_retained_bounds_and_preserves_state_on_error stdout ----

thread 'explicit_cursor_checks_retained_bounds_and_preserves_state_on_error' (116) panicked at tests/model.rs:10:5:
assertion `left == right` failed
  left: Err(OutOfBounds)
 right: Ok(())
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    explicit_cursor_checks_retained_bounds_and_preserves_state_on_error

test result: FAILED. 10 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test model`
```

## 10 明示カーソル移動の範囲検査 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.91s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 11 tests
test backspace_at_new_line_does_not_enter_previous_row ... ok
test backspace_returns_to_character_start_without_erasing ... ok
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test explicit_cursor_checks_retained_bounds_and_preserves_state_on_error ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok

test result: ok. 11 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 11 全角の左右上書き・隣接ペア修復 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.93s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 13 tests
test backspace_at_new_line_does_not_enter_previous_row ... ok
test construction_accepts_minimum_exact_and_extra_storage ... ok
test backspace_returns_to_character_start_without_erasing ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test explicit_cursor_checks_retained_bounds_and_preserves_state_on_error ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test overwrite_either_half_clears_old_pair ... FAILED
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test wide_overwrite_across_two_old_pairs_repairs_both_neighbors ... FAILED
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok

failures:

---- overwrite_either_half_clears_old_pair stdout ----

thread 'overwrite_either_half_clears_old_pair' (94) panicked at tests/model.rs:15:9:
assertion `left == right` failed
  left: [Single('A'), Continuation, Single(' ')]
 right: [Single('A'), Single(' '), Single(' ')]
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- wide_overwrite_across_two_old_pairs_repairs_both_neighbors stdout ----

thread 'wide_overwrite_across_two_old_pairs_repairs_both_neighbors' (96) panicked at tests/model.rs:26:5:
assertion `left == right` failed
  left: [Wide('漢'), Wide('界'), Continuation, Continuation]
 right: [Single(' '), Wide('界'), Continuation, Single(' ')]


failures:
    overwrite_either_half_clears_old_pair
    wide_overwrite_across_two_old_pairs_repairs_both_neighbors

test result: FAILED. 11 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test model`
```

## 11 全角の左右上書き・隣接ペア修復 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.98s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 13 tests
test backspace_at_new_line_does_not_enter_previous_row ... ok
test backspace_returns_to_character_start_without_erasing ... ok
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test explicit_cursor_checks_retained_bounds_and_preserves_state_on_error ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test overwrite_either_half_clears_old_pair ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test wide_overwrite_across_two_old_pairs_repairs_both_neighbors ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok

test result: ok. 13 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 12 TAB境界・原子的容量不足 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 1.05s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 15 tests
test backspace_at_new_line_does_not_enter_previous_row ... ok
test backspace_returns_to_character_start_without_erasing ... ok
test construction_accepts_minimum_exact_and_extra_storage ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test explicit_cursor_checks_retained_bounds_and_preserves_state_on_error ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test overwrite_either_half_clears_old_pair ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test tab_capacity_failure_is_atomic_and_success_repairs_pairs ... FAILED
test tab_writes_single_spaces_to_four_cell_stops_and_wraps ... FAILED
test wide_overwrite_across_two_old_pairs_repairs_both_neighbors ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok

failures:

---- tab_capacity_failure_is_atomic_and_success_repairs_pairs stdout ----

thread 'tab_capacity_failure_is_atomic_and_success_repairs_pairs' (96) panicked at tests/model.rs:29:5:
assertion `left == right` failed
  left: Ok(())
 right: Err(Full)
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- tab_writes_single_spaces_to_four_cell_stops_and_wraps stdout ----

thread 'tab_writes_single_spaces_to_four_cell_stops_and_wraps' (97) panicked at tests/model.rs:11:5:
assertion `left == right` failed
  left: (2, 0)
 right: (4, 0)


failures:
    tab_capacity_failure_is_atomic_and_success_repairs_pairs
    tab_writes_single_spaces_to_four_cell_stops_and_wraps

test result: FAILED. 13 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.01s

error: test failed, to rerun pass `--test model`
```

## 12 TAB境界・原子的容量不足 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.99s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 15 tests
test backspace_returns_to_character_start_without_erasing ... ok
test backspace_at_new_line_does_not_enter_previous_row ... ok
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test explicit_cursor_checks_retained_bounds_and_preserves_state_on_error ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test overwrite_either_half_clears_old_pair ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test tab_capacity_failure_is_atomic_and_success_repairs_pairs ... ok
test tab_writes_single_spaces_to_four_cell_stops_and_wraps ... ok
test wide_overwrite_across_two_old_pairs_repairs_both_neighbors ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok

test result: ok. 15 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 13 viewport/再露出 RED + ペア・番兵の回帰

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 1.27s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 18 tests
test backspace_at_new_line_does_not_enter_previous_row ... ok
test backspace_returns_to_character_start_without_erasing ... ok
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test explicit_cursor_checks_retained_bounds_and_preserves_state_on_error ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test overwrite_either_half_clears_old_pair ... ok
test scrolling_viewport_and_reexposure_read_saved_rows_without_dirty ... FAILED
test tab_capacity_failure_is_atomic_and_success_repairs_pairs ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test tab_writes_single_spaces_to_four_cell_stops_and_wraps ... ok
test viewport_rejects_unretained_rows_and_overflow_and_accepts_empty ... FAILED
test wide_overwrite_across_two_old_pairs_repairs_both_neighbors ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok
test operation_sequences_preserve_pairs_and_storage_sentinels ... ok

failures:

---- scrolling_viewport_and_reexposure_read_saved_rows_without_dirty stdout ----

thread 'scrolling_viewport_and_reexposure_read_saved_rows_without_dirty' (126) panicked at tests/model.rs:9:5:
assertion `left == right` failed
  left: Err(OutOfBounds)
 right: Ok([Single(' '), Single(' '), Single(' ')])
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- viewport_rejects_unretained_rows_and_overflow_and_accepts_empty stdout ----

thread 'viewport_rejects_unretained_rows_and_overflow_and_accepts_empty' (130) panicked at tests/model.rs:28:5:
assertion `left == right` failed
  left: Err(OutOfBounds)
 right: Err(Overflow)


failures:
    scrolling_viewport_and_reexposure_read_saved_rows_without_dirty
    viewport_rejects_unretained_rows_and_overflow_and_accepts_empty

test result: FAILED. 16 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.05s

error: test failed, to rerun pass `--test model`
```

## 13 viewport/再露出 GREEN + ペア・番兵の回帰

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 1.04s
     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 18 tests
test backspace_at_new_line_does_not_enter_previous_row ... ok
test backspace_returns_to_character_start_without_erasing ... ok
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test explicit_cursor_checks_retained_bounds_and_preserves_state_on_error ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test overwrite_either_half_clears_old_pair ... ok
test scrolling_viewport_and_reexposure_read_saved_rows_without_dirty ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test tab_capacity_failure_is_atomic_and_success_repairs_pairs ... ok
test tab_writes_single_spaces_to_four_cell_stops_and_wraps ... ok
test viewport_rejects_unretained_rows_and_overflow_and_accepts_empty ... ok
test wide_overwrite_across_two_old_pairs_repairs_both_neighbors ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok
test operation_sequences_preserve_pairs_and_storage_sentinels ... ok

test result: ok. 18 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.05s
```

## 14 長さ付きchunk入力 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test stream
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
warning: field `decoder` is never read
  --> src/stream.rs:13:5
   |
11 | pub struct Terminal<'a> {
   |            -------- field in this struct
12 |     model: Model<'a>,
13 |     decoder: Decoder,
   |     ^^^^^^^
   |
   = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term` (lib) generated 1 warning
warning: unused import: `Error`
 --> tests/stream.rs:1:32
  |
1 | use libos32term::model::{Cell, Error, Model, Width, BLANK};
  |                                ^^^^^
  |
  = note: `#[warn(unused_imports)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term` (test "stream") generated 1 warning (run `cargo fix --test "stream" -p libos32term` to apply 1 suggestion)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.97s
     Running tests/stream.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/stream-778d381a245c0799)

running 1 test
test chunks_keep_utf8_prefix_and_apply_controls_and_nul ... FAILED

failures:

---- chunks_keep_utf8_prefix_and_apply_controls_and_nul stdout ----

thread 'chunks_keep_utf8_prefix_and_apply_controls_and_nul' (103) panicked at tests/stream.rs:13:9:
assertion `left == right` failed
  left: 0
 right: 3
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    chunks_keep_utf8_prefix_and_apply_controls_and_nul

test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test stream`
```

## 14 長さ付きchunk入力 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test stream
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
warning: unused import: `Error`
 --> tests/stream.rs:1:32
  |
1 | use libos32term::model::{Cell, Error, Model, Width, BLANK};
  |                                ^^^^^
  |
  = note: `#[warn(unused_imports)]` (part of `#[warn(unused)]`) on by default

warning: `libos32term` (test "stream") generated 1 warning (run `cargo fix --test "stream" -p libos32term` to apply 1 suggestion)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.92s
     Running tests/stream.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/stream-778d381a245c0799)

running 1 test
test chunks_keep_utf8_prefix_and_apply_controls_and_nul ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 15 chunk入力の容量エラー・保留・再試行 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test stream
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.86s
     Running tests/stream.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/stream-778d381a245c0799)

running 4 tests
test chunks_keep_utf8_prefix_and_apply_controls_and_nul ... ok
test error_after_first_of_two_outputs_keeps_only_second ... FAILED
test full_preserves_decoded_output_and_reports_consumed_bytes_for_retry ... FAILED
test one_column_error_keeps_wide_scalar_and_does_not_consume_later_input ... FAILED

failures:

---- error_after_first_of_two_outputs_keeps_only_second stdout ----

thread 'error_after_first_of_two_outputs_keeps_only_second' (80) panicked at tests/stream.rs:31:5:
assertion `left == right` failed
  left: None
 right: Some(Full)
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- full_preserves_decoded_output_and_reports_consumed_bytes_for_retry stdout ----

thread 'full_preserves_decoded_output_and_reports_consumed_bytes_for_retry' (81) panicked at tests/stream.rs:11:5:
assertion `left == right` failed
  left: None
 right: Some(Full)

---- one_column_error_keeps_wide_scalar_and_does_not_consume_later_input stdout ----

thread 'one_column_error_keeps_wide_scalar_and_does_not_consume_later_input' (82) panicked at tests/stream.rs:42:5:
assertion `left == right` failed
  left: None
 right: Some(TooWide)


failures:
    error_after_first_of_two_outputs_keeps_only_second
    full_preserves_decoded_output_and_reports_consumed_bytes_for_retry
    one_column_error_keeps_wide_scalar_and_does_not_consume_later_input

test result: FAILED. 1 passed; 3 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test stream`
```

## 15 RED再実行（実装前の期待値修正）

`AB E2 X !` で保留出力を生成するXまでの消費は4バイト。誤って5とした期待値を4へ修正。
容量エラーのassertion失敗を再確認してから実装する。

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test stream
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.80s
     Running tests/stream.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/stream-778d381a245c0799)

running 4 tests
test chunks_keep_utf8_prefix_and_apply_controls_and_nul ... ok
test error_after_first_of_two_outputs_keeps_only_second ... FAILED
test full_preserves_decoded_output_and_reports_consumed_bytes_for_retry ... FAILED
test one_column_error_keeps_wide_scalar_and_does_not_consume_later_input ... FAILED

failures:

---- error_after_first_of_two_outputs_keeps_only_second stdout ----

thread 'error_after_first_of_two_outputs_keeps_only_second' (80) panicked at tests/stream.rs:31:5:
assertion `left == right` failed
  left: None
 right: Some(Full)
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- full_preserves_decoded_output_and_reports_consumed_bytes_for_retry stdout ----

thread 'full_preserves_decoded_output_and_reports_consumed_bytes_for_retry' (81) panicked at tests/stream.rs:11:5:
assertion `left == right` failed
  left: None
 right: Some(Full)

---- one_column_error_keeps_wide_scalar_and_does_not_consume_later_input stdout ----

thread 'one_column_error_keeps_wide_scalar_and_does_not_consume_later_input' (82) panicked at tests/stream.rs:42:5:
assertion `left == right` failed
  left: None
 right: Some(TooWide)


failures:
    error_after_first_of_two_outputs_keeps_only_second
    full_preserves_decoded_output_and_reports_consumed_bytes_for_retry
    one_column_error_keeps_wide_scalar_and_does_not_consume_later_input

test result: FAILED. 1 passed; 3 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test stream`
```

## 15 chunk入力の容量エラー・保留・再試行 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test stream
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.95s
     Running tests/stream.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/stream-778d381a245c0799)

running 4 tests
test chunks_keep_utf8_prefix_and_apply_controls_and_nul ... ok
test error_after_first_of_two_outputs_keeps_only_second ... ok
test full_preserves_decoded_output_and_reports_consumed_bytes_for_retry ... ok
test one_column_error_keeps_wide_scalar_and_does_not_consume_later_input ... ok

test result: ok. 4 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 16 stream最終化・保留とprefixの保持 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test stream
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.90s
     Running tests/stream.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/stream-778d381a245c0799)

running 6 tests
test chunks_keep_utf8_prefix_and_apply_controls_and_nul ... ok
test error_after_first_of_two_outputs_keeps_only_second ... ok
test finish_flushes_prefix_once_and_can_retry_after_full ... FAILED
test finish_preserves_prefix_behind_blocked_replacement ... FAILED
test full_preserves_decoded_output_and_reports_consumed_bytes_for_retry ... ok
test one_column_error_keeps_wide_scalar_and_does_not_consume_later_input ... ok

failures:

---- finish_flushes_prefix_once_and_can_retry_after_full stdout ----

thread 'finish_flushes_prefix_once_and_can_retry_after_full' (111) panicked at tests/stream.rs:11:5:
assertion `left == right` failed
  left: None
 right: Some(Full)
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- finish_preserves_prefix_behind_blocked_replacement stdout ----

thread 'finish_preserves_prefix_behind_blocked_replacement' (112) panicked at tests/stream.rs:27:5:
assertion `left == right` failed
  left: None
 right: Some(Full)


failures:
    finish_flushes_prefix_once_and_can_retry_after_full
    finish_preserves_prefix_behind_blocked_replacement

test result: FAILED. 4 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test stream`
```

## 16 stream最終化・保留とprefixの保持 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test stream
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.93s
     Running tests/stream.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/stream-778d381a245c0799)

running 6 tests
test chunks_keep_utf8_prefix_and_apply_controls_and_nul ... ok
test error_after_first_of_two_outputs_keeps_only_second ... ok
test finish_flushes_prefix_once_and_can_retry_after_full ... ok
test finish_preserves_prefix_behind_blocked_replacement ... ok
test full_preserves_decoded_output_and_reports_consumed_bytes_for_retry ... ok
test one_column_error_keeps_wide_scalar_and_does_not_consume_later_input ... ok

test result: ok. 6 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 分割不変性のモデル回帰（既存挙動）

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test stream
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 1.16s
     Running tests/stream.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/stream-778d381a245c0799)

running 7 tests
test chunks_keep_utf8_prefix_and_apply_controls_and_nul ... ok
test error_after_first_of_two_outputs_keeps_only_second ... ok
test finish_flushes_prefix_once_and_can_retry_after_full ... ok
test finish_preserves_prefix_behind_blocked_replacement ... ok
test full_preserves_decoded_output_and_reports_consumed_bytes_for_retry ... ok
test one_column_error_keeps_wide_scalar_and_does_not_consume_later_input ... ok
test all_chunk_partitions_match_cells_cursor_limits_and_pending ... ok

test result: ok. 7 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.01s
```

## 17 半開矩形の交差 RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test clip
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.76s
     Running tests/clip.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/clip-9f6efbc955e77ea7)

running 2 tests
test empty_touching_inverted_and_integer_endpoints ... FAILED
test intersection_clips_each_edge_and_negative_origins ... FAILED

failures:

---- empty_touching_inverted_and_integer_endpoints stdout ----

thread 'empty_touching_inverted_and_integer_endpoints' (69) panicked at tests/clip.rs:28:9:
assertion `left == right` failed
  left: Ok(None)
 right: Err(InvalidRect)
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- intersection_clips_each_edge_and_negative_origins stdout ----

thread 'intersection_clips_each_edge_and_negative_origins' (70) panicked at tests/clip.rs:15:9:
assertion `left == right` failed
  left: Ok(None)
 right: Ok(Some(Rect { x0: -10, y0: -10, x1: 20, y1: 30 }))


failures:
    empty_touching_inverted_and_integer_endpoints
    intersection_clips_each_edge_and_negative_origins

test result: FAILED. 0 passed; 2 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test clip`
```

## 17 半開矩形の交差 GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test clip
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.82s
     Running tests/clip.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/clip-9f6efbc955e77ea7)

running 2 tests
test empty_touching_inverted_and_integer_endpoints ... ok
test intersection_clips_each_edge_and_negative_origins ... ok

test result: ok. 2 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 18 画素→セルclip・部分全角・overflow RED

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test clip
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 1.12s
     Running tests/clip.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/clip-9f6efbc955e77ea7)

running 6 tests
test empty_touching_inverted_and_integer_endpoints ... ok
test grid_rejects_invalid_dimensions_rectangles_and_arithmetic_overflow ... FAILED
test grid_edges_and_empty_intersections_are_bounded ... FAILED
test intersection_clips_each_edge_and_negative_origins ... ok
test nonaligned_clip_preserves_write_rectangle_and_expands_only_read_columns ... FAILED
test partial_kanji_reads_both_halves_but_never_expands_pixel_permission ... FAILED

failures:

---- grid_rejects_invalid_dimensions_rectangles_and_arithmetic_overflow stdout ----

thread 'grid_rejects_invalid_dimensions_rectangles_and_arithmetic_overflow' (117) panicked at tests/clip.rs:38:9:
assertion `left == right` failed
  left: Ok(None)
 right: Err(InvalidGrid)

---- grid_edges_and_empty_intersections_are_bounded stdout ----

thread 'grid_edges_and_empty_intersections_are_bounded' (116) panicked at tests/clip.rs:27:9:
assertion `left == right` failed
  left: Ok(None)
 right: Ok(Some(Clip { write: Rect { x0: -5, y0: 0, x1: -4, y1: 1 }, columns: 0..2, rows: 0..1 }))
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- nonaligned_clip_preserves_write_rectangle_and_expands_only_read_columns stdout ----

thread 'nonaligned_clip_preserves_write_rectangle_and_expands_only_read_columns' (119) panicked at tests/clip.rs:9:5:
assertion `left == right` failed
  left: Ok(None)
 right: Ok(Some(Clip { write: Rect { x0: 4, y0: 10, x1: 5, y1: 11 }, columns: 0..3, rows: 1..2 }))

---- partial_kanji_reads_both_halves_but_never_expands_pixel_permission stdout ----

thread 'partial_kanji_reads_both_halves_but_never_expands_pixel_permission' (120) panicked at tests/clip.rs:61:9:
assertion failed: matches!(result, Ok(Some(_)))


failures:
    grid_edges_and_empty_intersections_are_bounded
    grid_rejects_invalid_dimensions_rectangles_and_arithmetic_overflow
    nonaligned_clip_preserves_write_rectangle_and_expands_only_read_columns
    partial_kanji_reads_both_halves_but_never_expands_pixel_permission

test result: FAILED. 2 passed; 4 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--test clip`
```

## 18 画素→セルclip・部分全角・overflow GREEN

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test clip
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.91s
     Running tests/clip.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/clip-9f6efbc955e77ea7)

running 6 tests
test empty_touching_inverted_and_integer_endpoints ... ok
test grid_edges_and_empty_intersections_are_bounded ... ok
test grid_rejects_invalid_dimensions_rectangles_and_arithmetic_overflow ... ok
test intersection_clips_each_edge_and_negative_origins ... ok
test nonaligned_clip_preserves_write_rectangle_and_expands_only_read_columns ... ok
test partial_kanji_reads_both_halves_but_never_expands_pixel_permission ... ok

test result: ok. 6 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 既存挙動の追加回帰（境界値・独立oracle・TAB・ANSI対象外）

新しい挙動の実装はなく、18サイクルで実装済みの契約を広く検査した。

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline
exit: 101
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
error[E0277]: can't compare `String` with `&mut str`
 --> tests/utf8.rs:9:48
  |
9 |         for cuts in 0..1 << (text.len() - 1) { assert_eq!(decode_chunks(text.as_bytes(), cuts), text); }
  |                                                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ no implementation for `String == &mut str`
  |
  = help: the trait `PartialEq<&mut str>` is not implemented for `String`
  = help: `String` implements trait `PartialEq<Rhs>`:
            PartialEq<&str>
            PartialEq<ByteStr>
            PartialEq<ByteString>
            PartialEq<Cow<'_, str>>
            PartialEq<Path>
            PartialEq<PathBuf>
            PartialEq<str>
            PartialEq

For more information about this error, try `rustc --explain E0277`.
error: could not compile `libos32term` (test "utf8") due to 1 previous error
warning: build failed, waiting for other jobs to finish...
```

## 追加回帰のコンパイル修正と再実行

直前はテスト側の `String == &mut str` 型不一致。REDとしては数えない。
`encode_utf8` の結果を `&str` に束縛し直した。本体変更なし。

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.98s
     Running unittests src/lib.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/libos32term-504a1294aad4cb91)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/clip.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/clip-9f6efbc955e77ea7)

running 7 tests
test empty_touching_inverted_and_integer_endpoints ... ok
test grid_edges_and_empty_intersections_are_bounded ... ok
test grid_rejects_invalid_dimensions_rectangles_and_arithmetic_overflow ... ok
test intersection_clips_each_edge_and_negative_origins ... ok
test nonaligned_clip_preserves_write_rectangle_and_expands_only_read_columns ... ok
test partial_kanji_reads_both_halves_but_never_expands_pixel_permission ... ok
test rectangle_enumeration_matches_pixel_permission_oracle ... ok

test result: ok. 7 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.03s

     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 19 tests
test backspace_at_new_line_does_not_enter_previous_row ... ok
test backspace_returns_to_character_start_without_erasing ... ok
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test explicit_cursor_checks_retained_bounds_and_preserves_state_on_error ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test overwrite_either_half_clears_old_pair ... ok
test scrolling_viewport_and_reexposure_read_saved_rows_without_dirty ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test tab_at_pending_wrap_uses_current_column_and_is_atomic ... ok
test tab_capacity_failure_is_atomic_and_success_repairs_pairs ... ok
test tab_writes_single_spaces_to_four_cell_stops_and_wraps ... ok
test viewport_rejects_unretained_rows_and_overflow_and_accepts_empty ... ok
test wide_overwrite_across_two_old_pairs_repairs_both_neighbors ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok
test operation_sequences_preserve_pairs_and_storage_sentinels ... ok

test result: ok. 19 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.05s

     Running tests/stream.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/stream-778d381a245c0799)

running 8 tests
test chunks_keep_utf8_prefix_and_apply_controls_and_nul ... ok
test error_after_first_of_two_outputs_keeps_only_second ... ok
test escape_sequences_are_literal_and_finish_allows_a_new_segment ... ok
test finish_flushes_prefix_once_and_can_retry_after_full ... ok
test finish_preserves_prefix_behind_blocked_replacement ... ok
test full_preserves_decoded_output_and_reports_consumed_bytes_for_retry ... ok
test one_column_error_keeps_wide_scalar_and_does_not_consume_later_input ... ok
test all_chunk_partitions_match_cells_cursor_limits_and_pending ... ok

test result: ok. 8 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.01s

     Running tests/utf8.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/utf8-ed5f1e078ab67bb1)

running 8 tests
test ascii_including_nul_is_length_delimited ... ok
test finalization_replaces_only_pending_prefix_once ... ok
test malformed_sequences_use_documented_replacement_units ... ok
test scalar_boundaries_roundtrip_including_max_unicode ... ok
test valid_multibyte_waits_for_complete_scalar ... ok
test every_partition_has_identical_utf8_output ... ok
test sampled_four_byte_inputs_match_oracle_at_every_partition ... ok
test all_two_byte_inputs_match_independent_lossy_utf8_oracle ... ok

test result: ok. 8 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.05s

   Doc-tests libos32term

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 最終 no_std host check

```text
cargo check --manifest-path userland/libos32term/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline
exit: 0
Checking libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `dev` profile [unoptimized + debuginfo] target(s) in 0.56s
```

## 最終 fmt check

```text
cargo fmt --manifest-path userland/libos32term/Cargo.toml -- --check
exit: 0
(出力なし)
```

## 最終 host test（整形・API説明後）

整形は `rustfmt --edition 2021 --config skip_children=true --emit stdout` の出力をpatchで適用。
コードの直接上書きはしていない。

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline
exit: 0
Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 1.50s
     Running unittests src/lib.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/libos32term-504a1294aad4cb91)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

     Running tests/clip.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/clip-9f6efbc955e77ea7)

running 7 tests
test empty_touching_inverted_and_integer_endpoints ... ok
test grid_rejects_invalid_dimensions_rectangles_and_arithmetic_overflow ... ok
test grid_edges_and_empty_intersections_are_bounded ... ok
test intersection_clips_each_edge_and_negative_origins ... ok
test nonaligned_clip_preserves_write_rectangle_and_expands_only_read_columns ... ok
test partial_kanji_reads_both_halves_but_never_expands_pixel_permission ... ok
test rectangle_enumeration_matches_pixel_permission_oracle ... ok

test result: ok. 7 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.03s

     Running tests/model.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/model-d838952001b05b71)

running 19 tests
test backspace_at_new_line_does_not_enter_previous_row ... ok
test backspace_returns_to_character_start_without_erasing ... ok
test construction_accepts_minimum_exact_and_extra_storage ... ok
test construction_checks_dimensions_capacity_overflow_without_writes ... ok
test cr_lf_crlf_and_consecutive_newlines_have_distinct_actions ... ok
test explicit_cursor_checks_retained_bounds_and_preserves_state_on_error ... ok
test full_newline_is_atomic_and_cr_allows_explicit_overwrite ... ok
test one_cell_screen_accepts_one_single_scalar ... ok
test one_column_wide_is_explicit_error_without_mutation ... ok
test overwrite_either_half_clears_old_pair ... ok
test scrolling_viewport_and_reexposure_read_saved_rows_without_dirty ... ok
test single_cells_wrap_lazily_and_full_preserves_history ... ok
test tab_at_pending_wrap_uses_current_column_and_is_atomic ... ok
test tab_capacity_failure_is_atomic_and_success_repairs_pairs ... ok
test tab_writes_single_spaces_to_four_cell_stops_and_wraps ... ok
test viewport_rejects_unretained_rows_and_overflow_and_accepts_empty ... ok
test wide_overwrite_across_two_old_pairs_repairs_both_neighbors ... ok
test wide_pairs_wrap_before_last_column_and_width_is_injected ... ok
test operation_sequences_preserve_pairs_and_storage_sentinels ... ok

test result: ok. 19 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.05s

     Running tests/stream.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/stream-778d381a245c0799)

running 8 tests
test chunks_keep_utf8_prefix_and_apply_controls_and_nul ... ok
test escape_sequences_are_literal_and_finish_allows_a_new_segment ... ok
test error_after_first_of_two_outputs_keeps_only_second ... ok
test finish_preserves_prefix_behind_blocked_replacement ... ok
test finish_flushes_prefix_once_and_can_retry_after_full ... ok
test full_preserves_decoded_output_and_reports_consumed_bytes_for_retry ... ok
test one_column_error_keeps_wide_scalar_and_does_not_consume_later_input ... ok
test all_chunk_partitions_match_cells_cursor_limits_and_pending ... ok

test result: ok. 8 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.01s

     Running tests/utf8.rs (userland/libos32term/target/x86_64-unknown-linux-gnu/debug/deps/utf8-ed5f1e078ab67bb1)

running 8 tests
test ascii_including_nul_is_length_delimited ... ok
test malformed_sequences_use_documented_replacement_units ... ok
test finalization_replaces_only_pending_prefix_once ... ok
test scalar_boundaries_roundtrip_including_max_unicode ... ok
test valid_multibyte_waits_for_complete_scalar ... ok
test every_partition_has_identical_utf8_output ... ok
test sampled_four_byte_inputs_match_oracle_at_every_partition ... ok
test all_two_byte_inputs_match_independent_lossy_utf8_oracle ... ok

test result: ok. 8 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.05s

   Doc-tests libos32term

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

## 新規ファイル検査・残制約

新規14ファイルを `git diff --no-index --check /dev/null <file>` で検査した。
初回の集計スクリプトはno-indexの「差分あり」終了コード1を失敗と誤判定し、exit 1となった。
診断出力は全ファイル空。終了コード0/1かつ診断なしを成功とする集計へ修正して再実行:

```text
python3 - <<'PY'
from pathlib import Path
import subprocess
root = Path('userland/libos32term')
files = [p for p in root.rglob('*') if p.is_file() and 'target' not in p.relative_to(root).parts]
failed = False
for path in sorted(files):
    result = subprocess.run(['git', 'diff', '--no-index', '--check', '/dev/null', str(path)], capture_output=True, text=True)
    if result.returncode not in (0, 1) or result.stdout or result.stderr:
        failed = True
        print(path, result.returncode, result.stdout, result.stderr)
print(f'new-file whitespace check: {len(files)} files, failed={failed}')
raise SystemExit(1 if failed else 0)
PY
exit: 0
new-file whitespace check: 14 files, failed=False
```

最終git statusでは開始時からの変更一覧に新規 `userland/libos32term/` のみが追加。
Cargo.lockには自クレートのみ。`git check-ignore userland/libos32term/target` で生成先の除外を確認。
新規14ファイル以外の既存未コミット変更には手を触れていない。

残制約: 通常のmake checkはMakefileの `.env` 読取とクレート外生成が今回の禁止条件に反するため未実施。
PMによるmake check登録・実行と独立レビュー解消は未完了。ゲストクロスビルド、ゲスト試験、
配備、エミュレータ操作、他エージェント起動、CUI/gshell統合、commit/pushは実施していない。


## 2026-09-08 T4レビュー修正 M1 / M3 / L1

書込範囲はuserland/libos32termのみ。旧TAB試験3件を新PM契約へ置換し、
既存次行と4列境界を跨ぐ全角ペア試験、Cell host実測試験を追加した。
本体を変更する前に以下のassertion REDを実行確認した（コンパイル成功）。

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model -- --nocapture
exit: 101
Cell host size=8 align=4
17 passed; 4 failed

tab_at_pending_wrap_preserves_complete_state_and_cells:
  assertion left == right failed: left Err(Full), right Ok(())
tab_on_final_row_clamps_and_repairs_either_wide_half:
  assertion left == right failed: left Err(Full), right Ok(())
tab_preserves_existing_next_row_and_clears_pair_across_stop:
  assertion left == right failed: left (3, 1), right (5, 0)
tab_writes_single_spaces_to_four_cell_stops_and_clamps:
  assertion left == right failed: left (3, 1), right (5, 0)
```

最小実装: TAB空白数を行内残数でクランプ。x==colsではwrite_charから即成功を返し、
limitを含めた状態全体を保持する。他の成功操作では従来どおりlimitを解除する。
既存put/clear_pairを用いて全角片側上書き時のペア解除を維持する。
H1/H2のAPIは追加していない。

試験は1列・3列・5列、最終保存行、次行非変更、全角左右片側と4列境界越しの相方解除、
x==colsで繰返しTAB、既存Full/TooWide状態の保持、続く通常文字の折返し/Fullを確認する。
既存4096操作列×幅1〜5による全角不変条件と番兵試験も成功した。

Cellの実測環境:
```text
rustc -Vv
exit: 0
rustc 1.97.0-nightly (37d85e592 2026-04-28)
host: x86_64-unknown-linux-gnu
LLVM version: 22.1.4
size_of::<Cell>() = 8 bytes
align_of::<Cell>() = 4 bytes
```
実測後に同host targetの回帰assert (8, 4)を試験へ記録しREADMEにも記載した。
Rust ABI保証・guest実寸ではない。L1のset_cursorはx==colsを指定できず、
完全状態復元ではないこともREADMEとAPIコメントへ明記した。

GREENと最終検査:
```text
cargo fmt --manifest-path userland/libos32term/Cargo.toml
exit: 0
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline
exit: 0
clip: 7 passed; model: 21 passed; stream: 8 passed; utf8: 8 passed
合計44 passed, 0 failed（unit/doc各0件）
cargo check --manifest-path userland/libos32term/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline
exit: 0
cargo fmt --manifest-path userland/libos32term/Cargo.toml -- --check
exit: 0
```

残事項: make check登録・実行、独立再レビューはPM側。make checkは.env読取と
クレート外生成を伴うため今回も未実施。guest実寸・クロスリンク・ゲスト試験は未検証。
共有ファイル・docs・buildの編集、外部依存追加、配備、エミュレータ、他エージェント、
.env/credential/docs/hw読取、commit/pushは行っていない。


## 2026-09-13 折り返しをまたぐ BS (票 T9 実装レビュー 往復 4 の S blocker)

書込範囲は `userland/libos32term` のみ (`src/model.rs` の `\x08` 分岐と
`tests/model.rs`)。40 桁の端末で `sh> echo ` + 36 文字以上を打って行が折り返すと、
行末の BS が x == 0 で止まって前の行へ戻らず、sh の `sh_backspace_tail`
(`\b` + 空白 + `\b`) が前行末の文字を消せずに画面とバッファが 1 文字ずれた。
ユーザー決裁 (2026-09-13) で **端末モデル側で直す** — sh は端末の幅を知らなくて済む。

本体を変更する前に、契約を書いた試験 3 件の RED を実行確認した (コンパイル成功)。

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline --test model
exit: 101
21 passed; 3 failed

backspace_at_column_zero_erases_across_the_wrap:
  assertion left == right failed: left (0, 1), right (39, 0)
backspace_across_the_wrap_skips_a_wide_half:
  assertion left == right failed: left (0, 1), right (38, 0)
backspace_after_a_newline_also_reaches_the_previous_row_end:
  assertion left == right failed: left (0, 1), right (0, 0)
```

最小実装: `write_inner` の `\x08` で `x == 0 && y > 0` のとき `y -= 1` して
`x = cols - 1` へ置く。`Continuation` を飛ばす既存の 1 段 (`x > 0` のときだけ
`x -= 1`) は分岐の外へ出して両経路で共有した。`x > 0` の既存挙動 (1 セル左、
全角の右半分なら先頭へ) は変えていない。戻り先は `put` の折り返し
(`width > cols - x` で `x = 0, y += 1`) の出所そのものなので、
「空白を書いて BS」で前行末が消え、書き込みは再び pending wrap (x == cols) に戻る。

**判断と根拠** (票の「折り返し継続の印が無ければ単純な形でよい」): モデルは行が
折り返しで続いているかを覚えていない (`State` は cursor / retained_rows / limit
だけ)。印を足すには行ごとの旗が要り、`cells` は呼び手が渡すスライスなので置き場が無い。
そこで `y > 0` なら一律に前行末へ戻す形にした。代償は「明示的な改行の直後の BS も
前行末に載る」ことだが、**BS 単体では 1 文字も消さない** (カーソルを動かすだけ) ため、
その位置へ続けて書く呼び手がいない限り表示は変わらない。端末 (`t5a_display`) は
プロンプト行の編集にしか BS を使わず、sh は自分が今書いた文字の上でしか
`\b` 空白 `\b` を出さない。この契約変更に合わせ、旧試験
`backspace_at_new_line_does_not_enter_previous_row` は
`backspace_after_a_newline_also_reaches_the_previous_row_end` として書き直した
(試験本文に理由を書いてある)。

足した試験 (model 21 → 24 件):

| 試験 | 見るもの |
|---|---|
| `backspace_at_column_zero_erases_across_the_wrap` | 40 桁で 41 文字目が折り返した後、BS で `(39, 0)` へ戻り、空白で前行末が消え、続く `x` がそこへ載る |
| `backspace_across_the_wrap_skips_a_wide_half` | 前行末が `Continuation` なら全角の先頭 `(38, 0)` へ戻り、空白が両半分を消す |
| `backspace_at_the_first_column_of_the_first_row_stays` | `y == 0` の `x == 0` は動かない (連打しても `(0, 0)`) |
| `backspace_after_a_newline_also_reaches_the_previous_row_end` | 上の判断を固定 (印が無いので改行直後も前行末へ) |

GREEN と最終検査:

```text
cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline
exit: 0
clip: 7 passed; model: 24 passed; stream: 8 passed; utf8: 8 passed  (0 failed)
cargo check --manifest-path userland/libos32term/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline
exit: 0
cargo fmt --manifest-path userland/libos32term/Cargo.toml -- --check
exit: 0
```

下流の回帰 (このクレートを取り込む側):

```text
cargo test --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml --target x86_64-unknown-linux-gnu --offline
exit: 0   62 passed; 0 failed
cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
exit: 0   39 passed; 0 failed
cargo check --release -p t5a_display   (userland/rust workspace)   exit: 0 警告 0
python3 -B tools/check_constraints.py  exit: 0
```

残事項: `make` (全ターゲット)・配備・エミュレータ・commit は禁止のため未実施 ([V4])。
ゲスト実機での「折り返した行の BS」の確認は PM / テスターへ。
