use libos32term::model::{Cell, Error, Model, Width, BLANK};

fn width(ch: char) -> Width {
    if ch == '漢' || ch == '界' {
        Width::Wide
    } else {
        Width::Single
    }
}

#[test]
fn tab_at_pending_wrap_preserves_complete_state_and_cells() {
    for cols in [1, 3, 5] {
        for rows in [1, 2] {
            for failed in [false, true] {
                let mut cells = vec![BLANK; cols * rows];
                let mut model = Model::new(&mut cells, cols, rows, width).unwrap();
                for _ in 0..cols {
                    assert_eq!(model.write_char('A'), Ok(()));
                }
                if failed {
                    // Full on the last row; TooWide on a one-column first row.
                    if rows == 1 {
                        assert_eq!(model.write_char('B'), Err(Error::Full));
                    } else if cols == 1 {
                        assert_eq!(model.write_char('漢'), Err(Error::TooWide));
                    }
                }
                let state = model.state();
                let saved = model.cells().to_vec();
                for _ in 0..2 {
                    assert_eq!(model.write_char('\t'), Ok(()));
                    assert_eq!(model.state(), state);
                    assert_eq!(model.cells(), saved);
                }
                if rows == 2 {
                    assert_eq!(model.write_char('B'), Ok(()));
                    assert_eq!(model.state().cursor, (1, 1));
                } else {
                    assert_eq!(model.write_char('B'), Err(Error::Full));
                    assert_eq!(model.cells(), saved);
                }
            }
        }
    }
}

#[test]
fn scrolling_viewport_and_reexposure_read_saved_rows_without_dirty() {
    let mut cells = [BLANK; 12];
    let mut model = Model::new(&mut cells, 3, 4, width).unwrap();
    assert_eq!(model.tail(3), Ok(&[BLANK; 3][..]));
    for ch in "A\n漢\nB\nC".chars() {
        assert_eq!(model.write_char(ch), Ok(()));
    }
    let saved = model.cells().to_vec();
    let state = model.state();
    for top in [0, 1, 2, 1, 0, 2] {
        for _ in 0..2 {
            assert_eq!(model.viewport(top, 2), Ok(&saved[top * 3..(top + 2) * 3]));
        }
    }
    assert_eq!(model.tail(2), Ok(&saved[6..]));
    assert_eq!(model.tail(usize::MAX), Ok(&saved[..]));
    assert_eq!(model.state(), state);
    assert_eq!(model.cells(), saved);
}

#[test]
fn viewport_rejects_unretained_rows_and_overflow_and_accepts_empty() {
    let mut cells = [BLANK; 6];
    let model = Model::new(&mut cells, 3, 2, width).unwrap();
    assert_eq!(model.viewport(0, 2), Err(Error::OutOfBounds));
    assert_eq!(model.viewport(2, 0), Err(Error::OutOfBounds));
    assert_eq!(model.viewport(usize::MAX, 1), Err(Error::Overflow));
    assert_eq!(model.viewport(1, 0), Ok(&[][..]));
    assert_eq!(model.tail(0), Ok(&[][..]));
}

#[test]
fn operation_sequences_preserve_pairs_and_storage_sentinels() {
    let sentinel = Cell::Single('#');
    let ops = ['A', '漢', '界', '\r', '\n', '\x08', '\t', '\0'];
    for cols in 1..=5 {
        for sequence in 0usize..4096 {
            let mut cells = [sentinel; 17];
            {
                let mut model = Model::new(&mut cells[1..16], cols, 3, width).unwrap();
                let mut sequence = sequence;
                for step in 0..4 {
                    if step == 2 {
                        let _ = model.set_cursor(sequence % cols, 0);
                    }
                    let saved = model.cells().to_vec();
                    let state = model.state();
                    if model.write_char(ops[sequence % ops.len()]).is_err() {
                        assert_eq!(model.cells(), saved);
                        assert_eq!(model.state().cursor, state.cursor);
                        assert_eq!(model.state().retained_rows, state.retained_rows);
                    }
                    for row in model.cells().chunks(cols) {
                        for (x, cell) in row.iter().enumerate() {
                            match cell {
                                Cell::Continuation => {
                                    assert!(x > 0 && matches!(row[x - 1], Cell::Wide(_)))
                                }
                                Cell::Wide(_) => {
                                    assert!(x + 1 < cols && row[x + 1] == Cell::Continuation)
                                }
                                Cell::Single(_) => {}
                            }
                        }
                    }
                    sequence /= ops.len();
                }
            }
            assert_eq!(cells[0], sentinel);
            assert!(cells[1 + cols * 3..].iter().all(|c| *c == sentinel));
        }
    }
}

#[test]
fn tab_writes_single_spaces_to_four_cell_stops_and_clamps() {
    let mut cells = [BLANK; 10];
    let mut model = Model::new(&mut cells, 5, 2, width).unwrap();
    assert_eq!(model.write_char('A'), Ok(()));
    assert_eq!(model.write_char('\t'), Ok(()));
    assert_eq!(model.state().cursor, (4, 0));
    assert_eq!(model.write_char('\t'), Ok(()));
    assert_eq!(model.state().cursor, (5, 0));
    assert_eq!(model.state().retained_rows, 0..1);
    assert_eq!(model.cells()[1..], [BLANK; 9]);
    for rows in [1, 4] {
        let mut narrow = [BLANK; 4];
        let mut model = Model::new(&mut narrow, 1, rows, |_| Width::Wide).unwrap();
        assert_eq!(model.write_char('\t'), Ok(()));
        assert_eq!(model.state().cursor, (1, 0));
        assert_eq!(model.state().retained_rows, 0..1);
        assert_eq!(model.cells(), vec![BLANK; rows]);
    }
}

#[test]
fn tab_on_final_row_clamps_and_repairs_either_wide_half() {
    for x in [0, 1] {
        let mut cells = [BLANK; 6];
        let mut model = Model::new(&mut cells, 3, 2, width).unwrap();
        for ch in "A\n漢Z".chars() {
            assert_eq!(model.write_char(ch), Ok(()));
        }
        assert_eq!(model.set_cursor(x, 1), Ok(()));
        assert_eq!(model.write_char('\t'), Ok(()));
        assert_eq!(model.state().cursor, (3, 1));
        assert_eq!(model.state().retained_rows, 0..2);
        assert_eq!(
            model.cells(),
            &[Cell::Single('A'), BLANK, BLANK, BLANK, BLANK, BLANK]
        );
    }
}

#[test]
fn tab_preserves_existing_next_row_and_clears_pair_across_stop() {
    for x in [3, 4] {
        let mut cells = [BLANK; 10];
        let mut model = Model::new(&mut cells, 5, 2, width).unwrap();
        for ch in "ABC漢\nNEXT!".chars() {
            assert_eq!(model.write_char(ch), Ok(()));
        }
        let next = model.cells()[5..].to_vec();
        assert_eq!(model.set_cursor(x, 0), Ok(()));
        assert_eq!(model.write_char('\t'), Ok(()));
        assert_eq!(model.state().cursor, (if x == 3 { 4 } else { 5 }, 0));
        assert_eq!(model.state().retained_rows, 0..2);
        assert_eq!(
            model.cells()[..5],
            [
                Cell::Single('A'),
                Cell::Single('B'),
                Cell::Single('C'),
                BLANK,
                BLANK
            ]
        );
        assert_eq!(model.cells()[5..], next);
    }
}

#[test]
fn cell_host_layout_measurement() {
    let measured = (core::mem::size_of::<Cell>(), core::mem::align_of::<Cell>());
    println!("Cell host size={} align={}", measured.0, measured.1);
    // Observed with rustc 1.97.0-nightly (37d85e592), x86_64-unknown-linux-gnu.
    // A host regression snapshot, not a Rust ABI guarantee or guest measurement.
    #[cfg(all(target_arch = "x86_64", target_os = "linux", target_env = "gnu"))]
    assert_eq!(measured, (8, 4));
}

#[test]
fn overwrite_either_half_clears_old_pair() {
    for x in [0, 1] {
        let mut cells = [BLANK; 3];
        let mut model = Model::new(&mut cells, 3, 1, width).unwrap();
        assert_eq!(model.write_char('漢'), Ok(()));
        assert_eq!(model.set_cursor(x, 0), Ok(()));
        assert_eq!(model.write_char('A'), Ok(()));
        let mut expected = [BLANK; 3];
        expected[x] = Cell::Single('A');
        assert_eq!(model.cells(), &expected);
    }
}

#[test]
fn wide_overwrite_across_two_old_pairs_repairs_both_neighbors() {
    let mut cells = [BLANK; 4];
    let mut model = Model::new(&mut cells, 4, 1, width).unwrap();
    for ch in "漢漢".chars() {
        assert_eq!(model.write_char(ch), Ok(()));
    }
    assert_eq!(model.set_cursor(1, 0), Ok(()));
    assert_eq!(model.write_char('界'), Ok(()));
    assert_eq!(
        model.cells(),
        &[BLANK, Cell::Wide('界'), Cell::Continuation, BLANK]
    );
}

#[test]
fn explicit_cursor_checks_retained_bounds_and_preserves_state_on_error() {
    let mut cells = [BLANK; 6];
    let mut model = Model::new(&mut cells, 3, 2, width).unwrap();
    assert_eq!(model.write_char('漢'), Ok(()));
    assert_eq!(model.set_cursor(1, 0), Ok(()));
    assert_eq!(model.state().cursor, (1, 0));
    let state = model.state();
    let saved = model.cells().to_vec();
    for (x, y) in [(3, 0), (0, 1), (usize::MAX, 0), (0, usize::MAX)] {
        assert_eq!(model.set_cursor(x, y), Err(Error::OutOfBounds));
        assert_eq!(model.state(), state);
        assert_eq!(model.cells(), saved);
    }
}

#[test]
fn backspace_returns_to_character_start_without_erasing() {
    let mut cells = [BLANK; 4];
    let mut model = Model::new(&mut cells, 4, 1, width).unwrap();
    for ch in "A漢B".chars() {
        assert_eq!(model.write_char(ch), Ok(()));
    }
    let saved = model.cells().to_vec();
    for x in [3, 1, 0, 0] {
        assert_eq!(model.write_char('\x08'), Ok(()));
        assert_eq!(model.state().cursor, (x, 0));
        assert_eq!(model.cells(), saved);
    }
}

#[test]
fn backspace_at_column_zero_erases_across_the_wrap() {
    // A line longer than cols continues on the next row, so a caller erasing
    // its own last character with BS, space, BS must reach the previous row's
    // end without knowing the width.
    let mut cells = [BLANK; 80];
    let mut model = Model::new(&mut cells, 40, 2, width).unwrap();
    for _ in 0..41 {
        assert_eq!(model.write_char('A'), Ok(()));
    }
    assert_eq!(model.state().cursor, (1, 1));
    // The wrapped character on the second row goes first.
    for ch in "\x08 \x08".chars() {
        assert_eq!(model.write_char(ch), Ok(()));
    }
    assert_eq!(model.state().cursor, (0, 1));
    assert_eq!(model.cells()[40], BLANK);
    // The next BS crosses the wrap to the last column of the first row, and
    // the space erases the character standing there.
    assert_eq!(model.write_char('\x08'), Ok(()));
    assert_eq!(model.state().cursor, (39, 0));
    assert_eq!(model.write_char(' '), Ok(()));
    assert_eq!(model.cells()[39], BLANK);
    // Writing the space consumed the row again (pending wrap), so BS returns
    // to the same column and the following character replaces it.
    assert_eq!(model.state().cursor, (40, 0));
    assert_eq!(model.write_char('\x08'), Ok(()));
    assert_eq!(model.state().cursor, (39, 0));
    assert_eq!(model.write_char('x'), Ok(()));
    assert_eq!(model.cells()[39], Cell::Single('x'));
    assert_eq!(model.cells()[38], Cell::Single('A'));
}

#[test]
fn backspace_across_the_wrap_skips_a_wide_half() {
    // The previous row ends in a continuation, so the cursor lands on the wide
    // character's own column; a space then clears both halves.
    let mut cells = [BLANK; 80];
    let mut model = Model::new(&mut cells, 40, 2, width).unwrap();
    for _ in 0..38 {
        assert_eq!(model.write_char('A'), Ok(()));
    }
    for ch in "漢B".chars() {
        assert_eq!(model.write_char(ch), Ok(()));
    }
    assert_eq!(model.state().cursor, (1, 1));
    for ch in "\x08 \x08\x08".chars() {
        assert_eq!(model.write_char(ch), Ok(()));
    }
    assert_eq!(model.state().cursor, (38, 0));
    assert_eq!(model.write_char(' '), Ok(()));
    assert_eq!(model.cells()[38], BLANK);
    assert_eq!(model.cells()[39], BLANK);
}

#[test]
fn backspace_at_the_first_column_of_the_first_row_stays() {
    let mut cells = [BLANK; 4];
    let mut model = Model::new(&mut cells, 2, 2, width).unwrap();
    for ch in "漢\x08\x08\x08".chars() {
        assert_eq!(model.write_char(ch), Ok(()));
    }
    assert_eq!(model.state().cursor, (0, 0));
    assert_eq!(
        model.cells(),
        &[Cell::Wide('漢'), Cell::Continuation, BLANK, BLANK]
    );
}

#[test]
fn backspace_after_a_newline_also_reaches_the_previous_row_end() {
    // The model keeps no wrap flag, so column 0 reached by LF is not told apart
    // from column 0 reached by a wrap. Landing on the previous row's end erases
    // nothing by itself, and callers only backspace over what they just wrote.
    let mut cells = [BLANK; 4];
    let mut model = Model::new(&mut cells, 2, 2, width).unwrap();
    for ch in "漢\n\x08".chars() {
        assert_eq!(model.write_char(ch), Ok(()));
    }
    assert_eq!(model.state().cursor, (0, 0));
    assert_eq!(
        model.cells(),
        &[Cell::Wide('漢'), Cell::Continuation, BLANK, BLANK]
    );
}

#[test]
fn cr_lf_crlf_and_consecutive_newlines_have_distinct_actions() {
    for (input, cursor, rows) in [
        ("A\r", (0, 0), 1),
        ("A\n", (0, 1), 2),
        ("A\r\n", (0, 1), 2),
        ("\n\n", (0, 2), 3),
        ("ABC\r\n", (0, 1), 2),
    ] {
        let mut cells = [BLANK; 9];
        let mut model = Model::new(&mut cells, 3, 3, width).unwrap();
        for ch in input.chars() {
            assert_eq!(model.write_char(ch), Ok(()));
        }
        assert_eq!(model.state().cursor, cursor, "{input:?}");
        assert_eq!(model.state().retained_rows, 0..rows);
        assert!(model
            .cells()
            .iter()
            .all(|c| !matches!(c, Cell::Single('\r' | '\n'))));
    }
}

#[test]
fn full_newline_is_atomic_and_cr_allows_explicit_overwrite() {
    let mut cells = [BLANK; 1];
    let mut model = Model::new(&mut cells, 1, 1, width).unwrap();
    assert_eq!(model.write_char('A'), Ok(()));
    assert_eq!(model.write_char('\n'), Err(Error::Full));
    assert_eq!(model.state().cursor, (1, 0));
    assert_eq!(model.cells(), &[Cell::Single('A')]);
    assert_eq!(model.write_char('\r'), Ok(()));
    assert_eq!(model.state().limit, None);
    assert_eq!(model.write_char('B'), Ok(()));
    assert_eq!(model.cells(), &[Cell::Single('B')]);
}

#[test]
fn wide_pairs_wrap_before_last_column_and_width_is_injected() {
    let mut cells = [BLANK; 6];
    let mut model = Model::new(&mut cells, 3, 2, width).unwrap();
    assert_eq!(model.write_char('漢'), Ok(()));
    assert_eq!(
        model.cells()[..3],
        [Cell::Wide('漢'), Cell::Continuation, BLANK]
    );
    assert_eq!(model.write_char('界'), Ok(()));
    assert_eq!(model.state().cursor, (2, 1));
    assert_eq!(
        model.cells()[3..],
        [Cell::Wide('界'), Cell::Continuation, BLANK]
    );
    let saved = model.cells().to_vec();
    assert_eq!(model.write_char('漢'), Err(Error::Full));
    assert_eq!(model.cells(), saved);
    assert_eq!(model.state().cursor, (2, 1));
    let mut ascii = [BLANK; 2];
    let mut model = Model::new(&mut ascii, 2, 1, |_| Width::Wide).unwrap();
    assert_eq!(model.write_char('A'), Ok(()));
    assert_eq!(model.cells(), &[Cell::Wide('A'), Cell::Continuation]);
}

#[test]
fn one_column_wide_is_explicit_error_without_mutation() {
    let mut cells = [BLANK; 2];
    let mut model = Model::new(&mut cells, 1, 2, width).unwrap();
    assert_eq!(model.write_char('漢'), Err(Error::TooWide));
    assert_eq!(model.state().cursor, (0, 0));
    assert_eq!(model.state().retained_rows, 0..1);
    assert_eq!(model.cells(), &[BLANK; 2]);
    assert_eq!(model.state().limit, Some(Error::TooWide));
    assert_eq!(model.write_char('A'), Ok(()));
    assert_eq!(model.state().limit, None);
}

#[test]
fn single_cells_wrap_lazily_and_full_preserves_history() {
    let mut cells = [BLANK; 4];
    let mut model = Model::new(&mut cells, 2, 2, width).unwrap();
    for ch in ['A', '\0', 'é', '🦀'] {
        assert_eq!(model.write_char(ch), Ok(()));
    }
    assert_eq!(
        model.cells(),
        &[
            Cell::Single('A'),
            Cell::Single('\0'),
            Cell::Single('é'),
            Cell::Single('🦀')
        ]
    );
    assert_eq!(model.state().cursor, (2, 1));
    assert_eq!(model.state().retained_rows, 0..2);
    assert_eq!(model.state().limit, None);
    let saved = model.cells().to_vec();
    for _ in 0..3 {
        assert_eq!(model.write_char('X'), Err(Error::Full));
    }
    assert_eq!(model.cells(), saved);
    assert_eq!(model.state().cursor, (2, 1));
    assert_eq!(model.state().limit, Some(Error::Full));
}

#[test]
fn one_cell_screen_accepts_one_single_scalar() {
    let mut cells = [BLANK];
    let mut model = Model::new(&mut cells, 1, 1, width).unwrap();
    assert_eq!(model.write_char('é'), Ok(()));
    assert_eq!(model.state().cursor, (1, 0));
    assert_eq!(model.write_char('A'), Err(Error::Full));
    assert_eq!(model.cells(), &[Cell::Single('é')]);
}

#[test]
fn construction_checks_dimensions_capacity_overflow_without_writes() {
    let sentinel = Cell::Single('#');
    let mut cells = [sentinel; 8];
    for (cols, rows, expected) in [
        (0, 1, Error::InvalidDimensions),
        (1, 0, Error::InvalidDimensions),
        (3, 3, Error::InsufficientStorage),
        (usize::MAX, 2, Error::Overflow),
        (usize::MAX, 1, Error::InsufficientStorage),
    ] {
        assert_eq!(
            Model::new(&mut cells, cols, rows, width).err(),
            Some(expected)
        );
        assert_eq!(cells, [sentinel; 8]);
    }
}

#[test]
fn construction_accepts_minimum_exact_and_extra_storage() {
    for (cols, rows) in [(1, 1), (2, 3), (3, 2)] {
        let mut cells = [Cell::Single('#'); 8];
        {
            let model = Model::new(&mut cells[1..7], cols, rows, width).unwrap();
            assert_eq!(model.cells(), vec![BLANK; cols * rows]);
            assert_eq!(model.state().cursor, (0, 0));
            assert_eq!(model.state().retained_rows, 0..1);
            assert_eq!(model.state().limit, None);
        }
        assert_eq!(cells[0], Cell::Single('#'));
        assert!(cells[1 + cols * rows..]
            .iter()
            .all(|c| *c == Cell::Single('#')));
    }
}
