# T5a TDD 実行ログ

新挙動のassertionを未実装のstubに対して実行してから実装する。
依存os32apiは独自panic_handler/allocatorとゲストFFIを持つのでhostから除外。
host_testsは本体srcをpath参照する独立workspace。コマンドはリポジトリrootで実行。

## S1 RED normal fixture / controls / history

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml
     Locking 2 packages to latest compatible versions
   Compiling libos32term v0.1.0 (~/os32/userland/libos32term)
   Compiling libos32term_render v0.1.0 (~/os32/userland/libos32term_render)
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.76s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 1 test
test state::tests::normal_fixture_and_controls ... FAILED

failures:

---- state::tests::normal_fixture_and_controls stdout ----

thread 'state::tests::normal_fixture_and_controls' (157) panicked at src/../../src/state.rs:34:9:
assertion `left == right` failed
  left: 0
 right: 229
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    state::tests::normal_fixture_and_controls

test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## S1 GREEN attempt

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.28s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 1 test
test state::tests::normal_fixture_and_controls ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

   Doc-tests t5a_display_host_tests

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## S2 RED exact / Full / TooWide / finish assertions

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.28s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 5 tests
test state::tests::full_stops_with_pending_and_unconsumed_tail ... FAILED
test state::tests::exact_capacity ... FAILED
test state::tests::incomplete_final_utf8_fails_only_at_finish ... FAILED
test state::tests::normal_fixture_and_controls ... ok
test state::tests::one_column_too_wide ... FAILED

failures:

---- state::tests::full_stops_with_pending_and_unconsumed_tail stdout ----

thread 'state::tests::full_stops_with_pending_and_unconsumed_tail' (94) panicked at src/../../src/state.rs:81:9:
assertion `left == right` failed
  left: Complete
 right: Feed(Full)

---- state::tests::exact_capacity stdout ----

thread 'state::tests::exact_capacity' (93) panicked at src/../../src/state.rs:73:9:
assertion `left == right` failed
  left: 0
 right: 2560
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- state::tests::incomplete_final_utf8_fails_only_at_finish stdout ----

thread 'state::tests::incomplete_final_utf8_fails_only_at_finish' (95) panicked at src/../../src/state.rs:100:9:
assertion `left == right` failed
  left: Complete
 right: Finish(Full)

---- state::tests::one_column_too_wide stdout ----

thread 'state::tests::one_column_too_wide' (97) panicked at src/../../src/state.rs:90:9:
assertion `left == right` failed
  left: Complete
 right: Feed(TooWide)


failures:
    state::tests::exact_capacity
    state::tests::full_stops_with_pending_and_unconsumed_tail
    state::tests::incomplete_final_utf8_fails_only_at_finish
    state::tests::one_column_too_wide

test result: FAILED. 1 passed; 4 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## S2 GREEN

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.28s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 5 tests
test state::tests::exact_capacity ... ok
test state::tests::full_stops_with_pending_and_unconsumed_tail ... ok
test state::tests::one_column_too_wide ... ok
test state::tests::incomplete_final_utf8_fails_only_at_finish ... ok
test state::tests::normal_fixture_and_controls ... ok

test result: ok. 5 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

   Doc-tests t5a_display_host_tests

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## S3 RED top / layout / clip; reinitialize is initial GREEN regression (not RED evidence)

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.34s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 9 tests
test state::tests::exact_capacity ... ok
test state::tests::full_stops_with_pending_and_unconsumed_tail ... ok
test state::tests::normal_fixture_and_controls ... ok
test state::tests::incomplete_final_utf8_fails_only_at_finish ... ok
test state::tests::one_column_too_wide ... ok
test state::tests::top_round_trip ... FAILED
test view::tests::client_rows_and_gui_preflight ... FAILED
test state::tests::reinitialize_same_storage_regression ... ok
test view::tests::paint_intersects_body_and_rejects_bad_coordinates ... FAILED

failures:

---- state::tests::top_round_trip stdout ----

thread 'state::tests::top_round_trip' (114) panicked at src/../../src/state.rs:121:41:
assertion `left == right` failed
  left: 0
 right: 1
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- view::tests::client_rows_and_gui_preflight stdout ----

thread 'view::tests::client_rows_and_gui_preflight' (115) panicked at src/../../src/view.rs:19:9:
assertion `left == right` failed
  left: 1
 right: 12

---- view::tests::paint_intersects_body_and_rejects_bad_coordinates stdout ----

thread 'view::tests::paint_intersects_body_and_rejects_bad_coordinates' (116) panicked at src/../../src/view.rs:28:9:
assertion `left == right` failed
  left: Rect { x0: 3, y0: 80, x1: 20, y1: 110 }
 right: Rect { x0: 8, y0: 88, x1: 20, y1: 110 }


failures:
    state::tests::top_round_trip
    view::tests::client_rows_and_gui_preflight
    view::tests::paint_intersects_body_and_rejects_bad_coordinates

test result: FAILED. 6 passed; 3 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## S3 GREEN

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.31s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 9 tests
test state::tests::full_stops_with_pending_and_unconsumed_tail ... ok
test state::tests::exact_capacity ... ok
test state::tests::normal_fixture_and_controls ... ok
test state::tests::one_column_too_wide ... ok
test state::tests::top_round_trip ... ok
test view::tests::client_rows_and_gui_preflight ... ok
test state::tests::incomplete_final_utf8_fails_only_at_finish ... ok
test state::tests::reinitialize_same_storage_regression ... ok
test view::tests::paint_intersects_body_and_rejects_bad_coordinates ... ok

test result: ok. 9 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

   Doc-tests t5a_display_host_tests

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## S4 RED static storage single owner

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml storage::tests
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
warning: unused import: `Ordering`
 --> src/../../src/storage.rs:1:75
  |
1 | use core::{cell::UnsafeCell, mem::MaybeUninit, sync::atomic::{AtomicBool, Ordering}};
  |                                                                           ^^^^^^^^
  |
  = note: `#[warn(unused_imports)]` (part of `#[warn(unused)]`) on by default

warning: fields `taken` and `cells` are never read
 --> src/../../src/storage.rs:6:5
  |
5 | pub struct Storage {
  |            ------- fields in this struct
6 |     taken: AtomicBool,
  |     ^^^^^
7 |     cells: UnsafeCell<MaybeUninit<[Cell; CAPACITY]>>,
  |     ^^^^^
  |
  = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: `t5a_display_host_tests` (lib test) generated 2 warnings (run `cargo fix --lib -p t5a_display_host_tests --tests` to apply 1 suggestion)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.29s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 1 test
test storage::tests::one_owner_and_repeated_initialization_rejected ... FAILED

failures:

---- storage::tests::one_owner_and_repeated_initialization_rejected stdout ----

thread 'storage::tests::one_owner_and_repeated_initialization_rejected' (209) panicked at src/../../src/storage.rs:24:9:
assertion failed: first.is_some()
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    storage::tests::one_owner_and_repeated_initialization_rejected

test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 9 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## S4 GREEN

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml storage::tests
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.27s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 1 test
test storage::tests::one_owner_and_repeated_initialization_rejected ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 9 filtered out; finished in 0.00s


exit=0
```

## S5 RED key actions

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml input::tests
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.32s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 1 test
test input::tests::fixed_keys_and_keyup_ignored ... FAILED

failures:

---- input::tests::fixed_keys_and_keyup_ignored stdout ----

thread 'input::tests::fixed_keys_and_keyup_ignored' (192) panicked at src/../../src/input.rs:10:9:
assertion failed: matches!(key(b'1', true), Action::Select(Fixture::Normal))
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    input::tests::fixed_keys_and_keyup_ignored

test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 10 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## S5 GREEN

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml input::tests
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.26s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 1 test
test input::tests::fixed_keys_and_keyup_ignored ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 10 filtered out; finished in 0.00s


exit=0
```

## S6 RED status labels

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml status::tests
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
warning: unused import: `Stop`
 --> src/../../src/status.rs:1:38
  |
1 | use crate::state::{Display, Fixture, Stop};
  |                                      ^^^^
  |
  = note: `#[warn(unused_imports)]` (part of `#[warn(unused)]`) on by default

warning: `t5a_display_host_tests` (lib test) generated 1 warning (run `cargo fix --lib -p t5a_display_host_tests --tests` to apply 1 suggestion)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.32s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 1 test
test status::tests::stopped_status_distinguishes_bytes_pending_and_finish ... FAILED

failures:

---- status::tests::stopped_status_distinguishes_bytes_pending_and_finish stdout ----

thread 'status::tests::stopped_status_distinguishes_bytes_pending_and_finish' (204) panicked at src/../../src/status.rs:30:9:
assertion `left == right` failed
  left: []
 right: [51, 32, 70, 85, 76, 76, 32, 102, 101, 101, 100, 58, 70, 117, 108, 108]
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    status::tests::stopped_status_distinguishes_bytes_pending_and_finish

test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 11 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## S6 GREEN

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml status::tests
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.31s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 1 test
test status::tests::stopped_status_distinguishes_bytes_pending_and_finish ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 11 filtered out; finished in 0.00s


exit=0
```

## S7 RED renderer connection / preflight / background

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml paint::tests
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.44s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 3 tests
test paint::tests::invalid_geometry_has_no_callbacks ... FAILED
test paint::tests::missing_saved_rows_are_background ... FAILED
test paint::tests::partial_japanese_reexposure_matches_full_and_preserves_model ... FAILED

failures:

---- paint::tests::invalid_geometry_has_no_callbacks stdout ----

thread 'paint::tests::invalid_geometry_has_no_callbacks' (258) panicked at src/../../src/paint.rs:62:9:
assertion failed: body(&s, Layout::new(340, 300).unwrap(),
        Rect { x0: 0, y0: 0, x1: i64::MAX, y1: 300 }, &mut font,
        &mut out).is_err()
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- paint::tests::missing_saved_rows_are_background stdout ----

thread 'paint::tests::missing_saved_rows_are_background' (259) panicked at src/../../src/paint.rs:71:9:
assertion `left == right` failed
  left: 0
 right: 16

---- paint::tests::partial_japanese_reexposure_matches_full_and_preserves_model stdout ----

thread 'paint::tests::partial_japanese_reexposure_matches_full_and_preserves_model' (260) panicked at src/../../src/paint.rs:40:9:
assertion failed: stats.runs > 0


failures:
    paint::tests::invalid_geometry_has_no_callbacks
    paint::tests::missing_saved_rows_are_background
    paint::tests::partial_japanese_reexposure_matches_full_and_preserves_model

test result: FAILED. 0 passed; 3 failed; 0 ignored; 0 measured; 12 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## S7 GREEN

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml paint::tests
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.34s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 3 tests
test paint::tests::invalid_geometry_has_no_callbacks ... ok
test paint::tests::missing_saved_rows_are_background ... ok
test paint::tests::partial_japanese_reexposure_matches_full_and_preserves_model ... ok

test result: ok. 3 passed; 0 failed; 0 ignored; 0 measured; 12 filtered out; finished in 0.04s


exit=0
```

## S8 RED JIS range and two-window screen layout

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml boundary::tests
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.34s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 2 tests
test boundary::tests::jis_checks_both_bytes ... FAILED
test boundary::tests::two_windows_fit_supported_screens ... FAILED

failures:

---- boundary::tests::jis_checks_both_bytes stdout ----

thread 'boundary::tests::jis_checks_both_bytes' (303) panicked at src/../../src/boundary.rs:9:46:
assertion `left == right` failed
  left: None
 right: Some(8481)
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace

---- boundary::tests::two_windows_fit_supported_screens stdout ----

thread 'boundary::tests::two_windows_fit_supported_screens' (304) panicked at src/../../src/boundary.rs:16:29:
assertion failed: r.x1 > r.x0 && r.y1 > r.y0


failures:
    boundary::tests::jis_checks_both_bytes
    boundary::tests::two_windows_fit_supported_screens

test result: FAILED. 0 passed; 2 failed; 0 ignored; 0 measured; 15 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## S8 GREEN

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml boundary::tests
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.35s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 2 tests
test boundary::tests::jis_checks_both_bytes ... ok
test boundary::tests::two_windows_fit_supported_screens ... ok

test result: ok. 2 passed; 0 failed; 0 ignored; 0 measured; 15 filtered out; finished in 0.00s


exit=0
```

## S9 RED in-loop fixture switching ownership boundary

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml session::tests
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
warning: field `backing` is never read
 --> src/../../src/session.rs:7:5
  |
5 | pub struct Session<'a> {
  |            ------- field in this struct
6 |     display: Option<Display<'a>>,
7 |     backing: NonNull<[Cell]>,
  |     ^^^^^^^
  |
  = note: `#[warn(dead_code)]` (part of `#[warn(unused)]`) on by default

warning: `t5a_display_host_tests` (lib test) generated 1 warning
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.36s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 1 test
test session::tests::switching_drops_pending_limit_top_and_storage_contents ... FAILED

failures:

---- session::tests::switching_drops_pending_limit_top_and_storage_contents stdout ----

thread 'session::tests::switching_drops_pending_limit_top_and_storage_contents' (158) panicked at src/../../src/session.rs:35:9:
assertion `left == right` failed
  left: Full
 right: Normal
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


failures:
    session::tests::switching_drops_pending_limit_top_and_storage_contents

test result: FAILED. 0 passed; 1 failed; 0 ignored; 0 measured; 17 filtered out; finished in 0.00s

error: test failed, to rerun pass `--lib`

exit=101
```

## S9 GREEN

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml session::tests
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.34s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 1 test
test session::tests::switching_drops_pending_limit_top_and_storage_contents ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 17 filtered out; finished in 0.00s


exit=0
```

## Final host test (all pure modules and ownership boundary)

```text
$ cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml
   Compiling t5a_display_host_tests v0.1.0 (~/os32/userland/rust/t5a_display/host_tests)
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.57s
     Running unittests src/lib.rs (userland/rust/t5a_display/host_tests/target/debug/deps/t5a_display_host_tests-68c8df534be032f4)

running 18 tests
test boundary::tests::jis_checks_both_bytes ... ok
test boundary::tests::two_windows_fit_supported_screens ... ok
test input::tests::fixed_keys_and_keyup_ignored ... ok
test paint::tests::invalid_geometry_has_no_callbacks ... ok
test paint::tests::missing_saved_rows_are_background ... ok
test state::tests::full_stops_with_pending_and_unconsumed_tail ... ok
test state::tests::exact_capacity ... ok
test state::tests::normal_fixture_and_controls ... ok
test state::tests::incomplete_final_utf8_fails_only_at_finish ... ok
test state::tests::one_column_too_wide ... ok
test state::tests::reinitialize_same_storage_regression ... ok
test state::tests::top_round_trip ... ok
test storage::tests::one_owner_and_repeated_initialization_rejected ... ok
test view::tests::client_rows_and_gui_preflight ... ok
test status::tests::stopped_status_distinguishes_bytes_pending_and_finish ... ok
test view::tests::paint_intersects_body_and_rejects_bad_coordinates ... ok
test session::tests::switching_drops_pending_limit_top_and_storage_contents ... ok
test paint::tests::partial_japanese_reexposure_matches_full_and_preserves_model ... ok

test result: ok. 18 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.04s

   Doc-tests t5a_display_host_tests

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s


exit=0
```

## Final fmt check (guest syntax formatting included; not guest typecheck)

```text
$ rustfmt --check --edition 2021 userland/rust/t5a_display/src/lib.rs userland/rust/t5a_display/host_tests/src/lib.rs

exit=0
```
