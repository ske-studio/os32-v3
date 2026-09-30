use libos32term::model::{Cell, Error, Model, Width, BLANK};
use libos32term::stream::Terminal;

fn width(ch: char) -> Width {
    if ch == '漢' {
        Width::Wide
    } else {
        Width::Single
    }
}

#[test]
fn escape_sequences_are_literal_and_finish_allows_a_new_segment() {
    let mut cells = [BLANK; 8];
    let mut term = Terminal::new(Model::new(&mut cells, 8, 1, width).unwrap());
    assert_eq!(term.feed(b"\x1b[31m").error, None);
    assert_eq!(term.finish().error, None);
    assert_eq!(term.feed(b"\0Z").consumed, 2);
    assert_eq!(
        &term.model().cells()[..7],
        &[
            Cell::Single('\x1b'),
            Cell::Single('['),
            Cell::Single('3'),
            Cell::Single('1'),
            Cell::Single('m'),
            Cell::Single('\0'),
            Cell::Single('Z')
        ]
    );
}

fn partitioned(
    input: &[u8],
    cuts: usize,
    cols: usize,
    rows: usize,
) -> (
    Vec<Cell>,
    libos32term::model::State,
    [Option<char>; 2],
    usize,
    Option<Error>,
) {
    let mut cells = vec![BLANK; cols * rows];
    let mut term = Terminal::new(Model::new(&mut cells, cols, rows, width).unwrap());
    let mut start = 0;
    let mut consumed = 0;
    let mut error = None;
    for end in 1..=input.len() {
        if end == input.len() || cuts & (1 << (end - 1)) != 0 {
            assert_eq!(term.feed(&[]).error, None);
            let report = term.feed(&input[start..end]);
            consumed += report.consumed;
            error = report.error;
            if error.is_some() {
                break;
            }
            start = end;
        }
    }
    if error.is_none() {
        error = term.finish().error;
    }
    (
        term.model().cells().to_vec(),
        term.model().state(),
        term.pending(),
        consumed,
        error,
    )
}

#[test]
fn all_chunk_partitions_match_cells_cursor_limits_and_pending() {
    let inputs: &[&[u8]] = &[
        b"A\0\xc2\xa2\xe6\xbc\xa2\xf0\x9f\xa6\x80",
        b"\xe2\x82A\xc0\xaf\xed\xa0\x80",
        b"\xf4\x90\x80\x80\xe0\x80\x80",
        b"A\r\n\tB\x08Z\n\n",
        b"\xe2\xc2\xa2\xf0\x9f\xa6",
        b"AB\xe2X!",
        b"\x1b[31m",
        b"\xff\0\x80",
    ];
    for input in inputs {
        for (cols, rows) in [(8, 4), (2, 1), (1, 2)] {
            let expected = partitioned(input, 0, cols, rows);
            for cuts in 0..1 << (input.len() - 1) {
                assert_eq!(
                    partitioned(input, cuts, cols, rows),
                    expected,
                    "{input:x?}, {cuts}, {cols}x{rows}"
                );
            }
        }
    }
}

#[test]
fn finish_flushes_prefix_once_and_can_retry_after_full() {
    let mut cells = [BLANK; 1];
    let mut term = Terminal::new(Model::new(&mut cells, 1, 1, width).unwrap());
    assert_eq!(term.feed(b"A\xf0\x9f\xa6").consumed, 4);
    assert_eq!(term.finish().error, Some(Error::Full));
    assert_eq!(term.pending(), [Some('�'), None]);
    assert_eq!(term.finish().error, Some(Error::Full));
    assert_eq!(term.model_mut().set_cursor(0, 0), Ok(()));
    assert_eq!(term.finish().error, None);
    assert_eq!(term.model().cells(), &[Cell::Single('�')]);
    let state = term.model().state();
    assert_eq!(term.finish().error, None);
    assert_eq!(term.model().state(), state);
}

#[test]
fn finish_preserves_prefix_behind_blocked_replacement() {
    let mut cells = [BLANK; 1];
    let mut term = Terminal::new(Model::new(&mut cells, 1, 1, width).unwrap());
    assert_eq!(term.feed(b"A\xe2\xc2").error, Some(Error::Full));
    assert_eq!(term.finish().error, Some(Error::Full));
    assert_eq!(term.model_mut().set_cursor(0, 0), Ok(()));
    assert_eq!(term.finish().error, Some(Error::Full));
    assert_eq!(term.pending(), [Some('�'), None]);
    assert_eq!(term.model_mut().set_cursor(0, 0), Ok(()));
    assert_eq!(term.finish().error, None);
    assert_eq!(term.pending(), [None; 2]);
}

#[test]
fn full_preserves_decoded_output_and_reports_consumed_bytes_for_retry() {
    let mut cells = [BLANK; 2];
    let mut term = Terminal::new(Model::new(&mut cells, 2, 1, width).unwrap());
    let report = term.feed(b"AB\xe2X!");
    assert_eq!(report.error, Some(Error::Full));
    assert_eq!(report.consumed, 4);
    assert_eq!(report.state.retained_rows, 0..1);
    assert_eq!(term.pending(), [Some('�'), Some('X')]);
    assert_eq!(
        term.model().cells(),
        &[Cell::Single('A'), Cell::Single('B')]
    );
    let repeated = term.feed(b"!");
    assert_eq!(repeated.consumed, 0);
    assert_eq!(repeated.error, Some(Error::Full));
    assert_eq!(term.pending(), [Some('�'), Some('X')]);
    assert_eq!(term.model_mut().set_cursor(0, 0), Ok(()));
    assert_eq!(term.feed(&[]).error, None);
    assert_eq!(term.pending(), [None; 2]);
    assert_eq!(
        term.model().cells(),
        &[Cell::Single('�'), Cell::Single('X')]
    );
}

#[test]
fn error_after_first_of_two_outputs_keeps_only_second() {
    let mut cells = [BLANK; 2];
    let mut term = Terminal::new(Model::new(&mut cells, 2, 1, width).unwrap());
    let report = term.feed(b"A\xe2X!");
    assert_eq!(report.error, Some(Error::Full));
    assert_eq!(report.consumed, 3);
    assert_eq!(
        term.model().cells(),
        &[Cell::Single('A'), Cell::Single('�')]
    );
    assert_eq!(term.pending(), [Some('X'), None]);
}

#[test]
fn one_column_error_keeps_wide_scalar_and_does_not_consume_later_input() {
    let mut cells = [BLANK; 4];
    let mut term = Terminal::new(Model::new(&mut cells, 1, 4, width).unwrap());
    let report = term.feed("漢Z".as_bytes());
    assert_eq!(report.error, Some(Error::TooWide));
    assert_eq!(report.consumed, 3);
    assert_eq!(term.pending(), [Some('漢'), None]);
    assert_eq!(term.model().cells(), &[BLANK; 4]);
    assert_eq!(term.feed(b"Z").consumed, 0);
}

#[test]
fn chunks_keep_utf8_prefix_and_apply_controls_and_nul() {
    let mut cells = [BLANK; 16];
    let mut term = Terminal::new(Model::new(&mut cells, 8, 2, width).unwrap());
    for (input, cursor) in [
        (&b"A\0\xe6"[..], (2, 0)),
        (&b"\xbc"[..], (2, 0)),
        (&b"\xa2\r"[..], (0, 0)),
        (&b"\n\xc2\xa2\xf0\x9f\xa6\x80"[..], (2, 1)),
    ] {
        let report = term.feed(input);
        assert_eq!(report.consumed, input.len());
        assert_eq!(report.error, None);
        assert_eq!(report.state.cursor, cursor);
    }
    assert_eq!(
        &term.model().cells()[..4],
        &[
            Cell::Single('A'),
            Cell::Single('\0'),
            Cell::Wide('漢'),
            Cell::Continuation
        ]
    );
    assert_eq!(
        &term.model().cells()[8..10],
        &[Cell::Single('¢'), Cell::Single('🦀')]
    );
    assert_eq!(term.pending(), [None; 2]);
    assert_eq!(term.feed(&[]).consumed, 0);
}
