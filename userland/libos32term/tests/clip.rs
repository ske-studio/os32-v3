use libos32term::clip::{Clip, ClipError, Grid, Rect};

fn rect(x0: i64, y0: i64, x1: i64, y1: i64) -> Rect {
    Rect { x0, y0, x1, y1 }
}

fn grid() -> Grid {
    Grid {
        origin: (-5, -7),
        cols: 4,
        rows: 3,
        cell_width: 8,
        cell_height: 16,
    }
}

#[test]
fn rectangle_enumeration_matches_pixel_permission_oracle() {
    let grid = Grid {
        origin: (-2, -3),
        cols: 3,
        rows: 3,
        cell_width: 3,
        cell_height: 2,
    };
    for x0 in -4..9 {
        for x1 in x0..10 {
            for y0 in -5..5 {
                for y1 in y0..6 {
                    let requested = rect(x0, y0, x1, y1);
                    let plan = grid.clip(requested).unwrap();
                    let mut count = 0;
                    for y in -5..6 {
                        for x in -4..10 {
                            let expected = x >= x0
                                && x < x1
                                && y >= y0
                                && y < y1
                                && (-2..7).contains(&x)
                                && (-3..3).contains(&y);
                            let actual = plan.as_ref().is_some_and(|p| {
                                x >= p.write.x0
                                    && x < p.write.x1
                                    && y >= p.write.y0
                                    && y < p.write.y1
                            });
                            assert_eq!(actual, expected);
                            if expected {
                                count += 1;
                                let p = plan.as_ref().unwrap();
                                assert!(p.columns.contains(&(((x + 2) / 3) as usize)));
                                assert!(p.rows.contains(&(((y + 3) / 2) as usize)));
                            }
                        }
                    }
                    assert_eq!(plan.is_some(), count > 0);
                    if let Some(p) = plan {
                        assert!(p.columns.end <= 3 && p.rows.end <= 3);
                    }
                }
            }
        }
    }
}

#[test]
fn nonaligned_clip_preserves_write_rectangle_and_expands_only_read_columns() {
    assert_eq!(
        grid().clip(rect(4, 10, 5, 11)),
        Ok(Some(Clip {
            write: rect(4, 10, 5, 11),
            columns: 0..3,
            rows: 1..2,
        }))
    );
    assert_eq!(
        grid().clip(rect(3, 9, 11, 25)),
        Ok(Some(Clip {
            write: rect(3, 9, 11, 25),
            columns: 0..3,
            rows: 1..2,
        }))
    );
    assert_eq!(
        grid().clip(rect(-100, -100, 100, 100)),
        Ok(Some(Clip {
            write: rect(-5, -7, 27, 41),
            columns: 0..4,
            rows: 0..3,
        }))
    );
}

#[test]
fn grid_edges_and_empty_intersections_are_bounded() {
    for (input, write, columns, rows) in [
        (rect(-10, 0, -4, 1), rect(-5, 0, -4, 1), 0..2, 0..1),
        (rect(26, 0, 50, 1), rect(26, 0, 27, 1), 2..4, 0..1),
        (rect(0, -10, 1, -6), rect(0, -7, 1, -6), 0..2, 0..1),
        (rect(0, 40, 1, 50), rect(0, 40, 1, 41), 0..2, 2..3),
    ] {
        assert_eq!(
            grid().clip(input),
            Ok(Some(Clip {
                write,
                columns,
                rows
            }))
        );
    }
    for input in [
        rect(27, 0, 28, 1),
        rect(0, 41, 1, 42),
        rect(0, 0, 0, 1),
        rect(0, 0, 1, 0),
    ] {
        assert_eq!(grid().clip(input), Ok(None));
    }
}

#[test]
fn grid_rejects_invalid_dimensions_rectangles_and_arithmetic_overflow() {
    let clip = rect(0, 0, 1, 1);
    for invalid in [
        Grid { cols: 0, ..grid() },
        Grid { rows: 0, ..grid() },
        Grid {
            cell_width: 0,
            ..grid()
        },
        Grid {
            cell_height: -1,
            ..grid()
        },
    ] {
        assert_eq!(invalid.clip(clip), Err(ClipError::InvalidGrid));
    }
    for overflow in [
        Grid {
            cols: usize::MAX,
            ..grid()
        },
        Grid {
            rows: usize::MAX,
            ..grid()
        },
        Grid {
            cell_width: i64::MAX,
            ..grid()
        },
        Grid {
            cell_height: i64::MAX,
            ..grid()
        },
        Grid {
            origin: (i64::MAX, 0),
            ..grid()
        },
        Grid {
            origin: (0, i64::MAX),
            ..grid()
        },
    ] {
        assert_eq!(overflow.clip(clip), Err(ClipError::Overflow));
    }
    assert_eq!(grid().clip(rect(1, 0, 0, 1)), Err(ClipError::InvalidRect));
    let extreme = Grid {
        origin: (i64::MIN, i64::MIN),
        cols: 1,
        rows: 1,
        cell_width: i64::MAX,
        cell_height: i64::MAX,
    };
    assert_eq!(
        extreme.clip(rect(i64::MIN, i64::MIN, i64::MAX, i64::MAX)),
        Ok(Some(Clip {
            write: rect(i64::MIN, i64::MIN, -1, -1),
            columns: 0..1,
            rows: 0..1,
        }))
    );
}

#[test]
fn partial_kanji_reads_both_halves_but_never_expands_pixel_permission() {
    use libos32term::model::{Cell, Model, Width, BLANK};
    let mut cells = [BLANK; 4];
    let mut model = Model::new(&mut cells, 4, 1, |ch| {
        if ch == '漢' {
            Width::Wide
        } else {
            Width::Single
        }
    })
    .unwrap();
    for ch in "A漢B".chars() {
        assert_eq!(model.write_char(ch), Ok(()));
    }
    for x in 3..19 {
        let requested = rect(x, -5, x + 1, 0);
        let result = grid().clip(requested);
        assert!(matches!(result, Ok(Some(_))));
        let clip = result.unwrap().unwrap();
        assert_eq!(clip.write, requested);
        let row = model.viewport(0, 1).unwrap();
        let read = &row[clip.columns];
        assert!(read.contains(&Cell::Wide('漢')) && read.contains(&Cell::Continuation));
    }
}

#[test]
fn intersection_clips_each_edge_and_negative_origins() {
    let bounds = rect(-10, -20, 30, 40);
    for (input, expected) in [
        (rect(-50, -10, 20, 30), rect(-10, -10, 20, 30)),
        (rect(0, -50, 20, 30), rect(0, -20, 20, 30)),
        (rect(0, -10, 50, 30), rect(0, -10, 30, 30)),
        (rect(0, -10, 20, 50), rect(0, -10, 20, 40)),
        (rect(-50, -50, 50, 50), bounds),
    ] {
        assert_eq!(bounds.intersection(input), Ok(Some(expected)));
        assert_eq!(input.intersection(bounds), Ok(Some(expected)));
    }
}

#[test]
fn empty_touching_inverted_and_integer_endpoints() {
    let bounds = rect(0, 0, 10, 10);
    for input in [
        rect(0, 0, 0, 10),
        rect(0, 0, 10, 0),
        rect(10, 0, 20, 10),
        rect(-10, 0, 0, 10),
        rect(0, 10, 10, 20),
        rect(0, -10, 10, 0),
    ] {
        assert_eq!(bounds.intersection(input), Ok(None));
    }
    for input in [rect(1, 0, 0, 10), rect(0, 1, 10, 0)] {
        assert_eq!(bounds.intersection(input), Err(ClipError::InvalidRect));
        assert_eq!(input.intersection(bounds), Err(ClipError::InvalidRect));
    }
    let huge = rect(i64::MIN, i64::MIN, i64::MAX, i64::MAX);
    assert_eq!(huge.intersection(huge), Ok(Some(huge)));
    assert_eq!(huge.intersection(bounds), Ok(Some(bounds)));
}
